# Audio reminder for the human: play a distinctive sound and speak a message when needed.
# ASCII-only source: the message is passed in as a parameter (PowerShell 5.1 misreads UTF-8 files).
param(
    [string]$SpeakText = "",
    [int]$Repeat = 2
)

$wav = "C:\Windows\Media\Alarm01.wav"
if (-not (Test-Path $wav)) { $wav = "C:\Windows\Media\notify.wav" }
for ($i = 0; $i -lt $Repeat; $i++) {
    try { (New-Object Media.SoundPlayer $wav).PlaySync() } catch { }
    Start-Sleep -Milliseconds 150
}

try { [console]::beep(1200, 250); [console]::beep(1600, 250) } catch { }

if ($SpeakText -ne "") {
    try {
        Add-Type -AssemblyName System.Speech
        $s = New-Object System.Speech.Synthesis.SpeechSynthesizer
        $voice = $s.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like 'zh*' } | Select-Object -First 1
        if ($voice) { $s.SelectVoice($voice.VoiceInfo.Name) }
        $s.Speak($SpeakText)
    } catch { }
}

Write-Output "reminder played"
