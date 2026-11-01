# phone.ps1 - every adb/fastboot action behind a device-state check and a timeout.
#
#   .\phone.ps1 state                      do not touch the phone, just say where it is
#   .\phone.ps1 reboot-bootloader          android -> fastboot
#   .\phone.ps1 boot-loader                fastboot -> our UEFI + xnuloader (thyme-start.img)
#   .\phone.ps1 stage -File <payload>      download a payload to the loader
#   .\phone.ps1 run                        start the staged kernel
#   .\phone.ps1 oem -Command "oem log"     one loader command (oem log/stop/screenshot/...)
#   .\phone.ps1 result                     ask for the run's result (short timeout, never hangs)
#   .\phone.ps1 get-staged -Out <file>     fetch loader + kernel log
#   .\phone.ps1 reboot                     cold reset to Android
#   .\phone.ps1 logdump -Out <file>        pull the loader's log partition (needs Android + root)
#
# Nothing here blocks forever: every call is wrapped in a job with a timeout, and every
# action that changes state first asserts the state it expects.
[CmdletBinding()]
param(
    [string]$IOS = $(if ($env:IOS) { $env:IOS } else { Join-Path $PSScriptRoot '..' })
    [Parameter(Position = 0)][string]$Action = 'state',
    [string]$File,
    [string]$Out,
    [string]$Command,
    [string]$Image = $(Join-Path $IOS "thyme\thyme-start.img"),
    [int]$TimeoutSec = 60
)

$ErrorActionPreference = 'SilentlyContinue'
$ADB = $(Join-Path $IOS "adb\adb.exe")
$FB = $(Join-Path $IOS "adb\fastboot.exe")
$LOADER_IMG = $(Join-Path $IOS "thyme\thyme-start.img")

function Invoke-Timed {
    param(
    [string]$IOS = $(if ($env:IOS) { $env:IOS } else { Join-Path $PSScriptRoot '..' })[string]$Exe, [string[]]$ArgList, [int]$Seconds)
    $job = Start-Job -ScriptBlock {
        param(
    [string]$IOS = $(if ($env:IOS) { $env:IOS } else { Join-Path $PSScriptRoot '..' })$e, $a)
        & $e @a 2>&1 | Out-String
    } -ArgumentList $Exe, $ArgList
    if (-not (Wait-Job $job -Timeout $Seconds)) {
        Stop-Job $job; Remove-Job $job -Force
        return $null   # caller decides what a timeout means
    }
    $text = (Receive-Job $job | Out-String)
    Remove-Job $job -Force
    return $text
}

function Get-PhoneState {
    $s = @{ kind = 'none'; serial = ''; product = '' }
    $adb = Invoke-Timed $ADB @('devices') 8
    if ($adb) {
        $dev = ($adb -split "`r?`n") | Where-Object { $_ -match '^\S+\s+device$' } | Select-Object -First 1
        if ($dev) { $s.kind = 'android'; $s.serial = ($dev -split '\s+')[0]; return $s }
    }
    $fb = Invoke-Timed $FB @('devices') 8
    if ($fb) {
        $dev = ($fb -split "`r?`n") | Where-Object { $_ -match '^\S+\s+fastboot$' } | Select-Object -First 1
        if ($dev) {
            $serial = ($dev -split '\s+')[0]
            $s.kind = 'fastboot'; $s.serial = $serial
            # our loader answers as "xnuloader" - that is cheaper and steadier than a getvar
            if ($serial -eq 'xnuloader') { $s.kind = 'loader'; $s.product = 'xnuloader'; return $s }
            $prod = Invoke-Timed $FB @('getvar', 'product') 12
            if ($prod -match 'product:\s*(\S+)') { $s.product = $Matches[1] }
            if ($s.product -eq 'xnuloader') { $s.kind = 'loader' }
            return $s
        }
    }
    return $s
}

function Show-State($s) {
    switch ($s.kind) {
        'android' { "state: ANDROID ($($s.serial))" }
        'fastboot' { "state: FASTBOOT/ABL ($($s.serial)) product=$($s.product)   <- the bootloader, not our loader" }
        'loader' { "state: XNULOADER ($($s.serial))   <- our loader is up and takes commands" }
        default { 'state: NO DEVICE (iOS on screen, powered off, or no cable)' }
    }
}

function Need-State($want) {
    $s = Get-PhoneState
    Show-State $s
    if ($s.kind -ne $want) { throw "expected state '$want', phone is '$($s.kind)'" }
    return $s
}

switch ($Action) {
    'state' { Show-State (Get-PhoneState) }

    'reboot-bootloader' {
        $s = Need-State 'android'
        & $ADB -s $s.serial reboot bootloader | Out-Null
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 2
            $s2 = Get-PhoneState
            if ($s2.kind -eq 'fastboot') { Show-State $s2; return }
        }
        Show-State (Get-PhoneState); throw 'the bootloader did not come up in time'
    }

    'boot-loader' {
        $s = Need-State 'fastboot'
        "booting $Image"
        $r = Invoke-Timed $FB @('boot', $Image) 40
        if ($null -eq $r) { throw 'fastboot boot timed out' }
        "fastboot boot: $($r.Trim())"
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 3
            $s2 = Get-PhoneState
            if ($s2.kind -eq 'loader') { Show-State $s2; return }
        }
        Show-State (Get-PhoneState); throw 'the loader did not answer in time'
    }

    'stage' {
        if (-not $File) { throw 'stage needs -File' }
        $s = Need-State 'loader'
        $item = Get-Item $File
        "staging $($item.Name) ($([math]::Round($item.Length/1MB,1)) MB)"
        $r = Invoke-Timed $FB @('stage', $item.FullName) $TimeoutSec
        if ($null -eq $r) { throw 'stage timed out' }
        "stage: $($r.Trim())"
        if ($r -match 'FAILED|error') { throw 'stage failed' }
        Show-State (Get-PhoneState)
    }

    'run' {
        $s = Need-State 'loader'
        $r = Invoke-Timed $FB @('oem', 'run') 30
        if ($null -eq $r) { throw 'oem run timed out' }
        "oem run: $($r.Trim())"
        'the kernel is running on another core; use:  phone.ps1 oem -Command ''oem log''  to watch it'
    }

    'oem' {
        if (-not $Command) { throw 'oem needs -Command, e.g. -Command "oem log"' }
        $s = Need-State 'loader'
        $words = $Command -split '\s+'
        $r = Invoke-Timed $FB $words $TimeoutSec
        if ($null -eq $r) { "TIMEOUT after $TimeoutSec s (state may be changing; check 'state')"; exit 2 }
        $r.Trim()
    }

    'result' {
        $s = Get-PhoneState
        Show-State $s
        if ($s.kind -ne 'loader') { 'the loader is gone: the run ended in a reset (see logdump)' ; return }
        $r = Invoke-Timed $FB @('getvar', 'result') 25
        if ($null -eq $r) { 'result: still running (getvar did not answer in 25 s)' ; return }
        $r.Trim()
    }

    'get-staged' {
        if (-not $Out) { throw 'get-staged needs -Out <file>' }
        $s = Need-State 'loader'
        $r = Invoke-Timed $FB @('get_staged', $Out) $TimeoutSec
        if ($null -eq $r) { throw 'get_staged timed out' }
        $r.Trim()
        if (Test-Path $Out) {
            "fetched $Out ($((Get-Item $Out).Length) bytes)"
        } else { throw 'get_staged produced no file' }
    }

    'reboot' {
        $s = Get-PhoneState
        Show-State $s
        if ($s.kind -ne 'loader' -and $s.kind -ne 'fastboot') { throw "nothing to reboot ($($s.kind))" }
        $r = Invoke-Timed $FB @('reboot') 25
        "reboot: $($r.Trim())"
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 3
            $s2 = Get-PhoneState
            if ($s2.kind -eq 'android') { Show-State $s2; return }
        }
        Show-State (Get-PhoneState)
    }

    'logdump' {
        if (-not $Out) { throw 'logdump needs -Out <file>' }
        $s = Need-State 'android'
        $r = Invoke-Timed $ADB @('-s', $s.serial, 'shell', "su -c 'dd if=/dev/block/by-name/logdump of=/data/local/tmp/logdump.bin bs=1M count=8'") 60
        "dump: $($r.Trim())"
        $p = Invoke-Timed $ADB @('-s', $s.serial, 'pull', '/data/local/tmp/logdump.bin', $Out) 60
        $p.Trim()
        "pulled $Out ($((Get-Item $Out).Length) bytes)"
    }

    default { throw "unknown action '$Action'" }
}

