/*
 * Where the panel's scan is, on the POCO X5 Pro (redwood): for iokit/Kernel/MiniIOMobileFramebuffer.cpp, which
 * puts every frame into the memory the panel is sent from and, without this, can only guess when the panel
 * reads it -- which is why the picture tore and why frames came at a pace of their own, beside the panel's.
 *
 * The panel is a DSI video panel, sent the framebuffer over and over by the display controller's timing engine
 * at the rate of its timing registers, which nobody touches after the firmware: 2432 lines a frame of which
 * 2400 are shown, 119.93 frames a second (seen: hakios.adreno.c, display_probe). The interface the panel hangs
 * on (INTF 1, dt: mdss_dsi0's mdp_intf_base) counts the frames and, within a frame, the line being sent; this
 * reads those two counters. To read them at all two bus clocks have to run, which the firmware leaves off: the
 * one of the display's clock controller (in GCC) and, in that controller, the one of the display block's
 * registers. They are turned on here, as Linux keeps them on and as display_probe did on this phone; that is all
 * that is ever written. The timing itself is only read.
 *
 * Boot-arg fb_scan=1 (on the phone; QEMU has no such block): on. Then the framebuffer driver
 *   - starts copying a frame into the panel's memory only where the scan lets the copy land whole in one
 *     refresh (redwood_display_window(): see MiniIOMobileFramebuffer.cpp, show), so that no refresh shows
 *     part of one frame and part of the next -- no tearing;
 *   - runs at the panel's own rate (120 Hz) and tells CoreAnimation that rate, and its refresh notifications
 *     come when the panel's frames begin, not on a clock of their own that drifts against them.
 *
 * Registers: Linux drivers/gpu/drm/msm/disp/dpu1/dpu_hw_intf.c (INTF), drivers/clk/qcom/dispcc-sc7280.c.
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <sys/redwood_display.h>
#include <sys/redwood_board.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
/* Preserve early clock/timing diagnostics in quiet boots and live loader logs. */
extern void redwood_tl(const char *, ...) __printflike(1, 2);
#define printf redwood_tl

#define GCC_PHYS                redwood_board_addr("gcc", 0x00100000UL)
#define GCC_DISP_AHB_CBCR       (kona ? 0x0b00c : 0x27004)
#define DISPCC_PHYS             redwood_board_addr("dispcc", 0x0af00000UL)    /* dt: clock-controller@af00000 */
#define DISPCC_MDSS_CORE_GDSCR  (kona ? 0x3000 : 0x1004)
#define DISPCC_MDSS_AHB_CBCR    (kona ? 0x2080 : 0x1050)
#define INTF1_PHYS              redwood_board_addr("intf", kona ? 0x0ae6b800UL : 0x0ae36000UL)
#define CBCR_ENABLE             (1u << 0)
#define CBCR_OFF                (1u << 31)
#define GDSC_POWER_ON           (1u << 31)

#define INTF_TIMING_ENGINE_EN   0x000
#define INTF_HSYNC_CTL          0x008           /* clocks a line << 16 | of which the pulse */
#define INTF_VSYNC_PERIOD       0x00c           /* clocks a frame */
#define INTF_DISPLAY_V_START    0x01c
#define INTF_DISPLAY_V_END      0x024
#define INTF_FRAME_COUNT        0x0ac
#define INTF_LINE_COUNT         0x0b0

/* the DSI controller and PHY of mdss_dsi0 (dt: dsi-ctrl-0, reg = <0xae94000 0x400 0xaf08000 4 0xae6b800 0x300>);
 * read-only, for the refresh-rate work: what the firmware left programmed, and in which mode */
#define DSI0_PHYS               0x0ae94000UL

/* the DSI controller's command-mode registers (vendor techpack dsi_ctrl_reg.h, for the panel's own
 * mode switch: the timings of a command-mode panel live in the panel, and its device tree switches
 * them with a DCS long write, e.g. 2F 01 for 60 Hz and 2F 04 for 90 Hz on the Mi 10S' panels) */
#define DSI_CTRL0                       0x0004
#define DSI_COMMAND_MODE_DMA_CTRL       0x003C
#define DSI_DMA_CMD_LENGTH              0x004C
#define DSI_CMD_MODE_DMA_SW_TRIGGER     0x0090
#define DSI_TEST_PATTERN_GEN_CTRL       0x015C
#define DSI_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL 0x017C
#define DSI_TPG_DMA_FIFO_RESET          0x01EC

static vm_offset_t intf, gcc_map, dispcc_map;
static struct redwood_display_scan scan;
static bool kona;
static bool command_mode;
static uint32_t te_initial, te_idle_start;

int redwood_display_command_mode(void) { return scan.ok && command_mode; }


static inline uint32_t
rd(vm_offset_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

static inline void
wr(vm_offset_t base, uint32_t offset, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + offset) = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

/*
 * Whether the display block's registers can be read right now: its power switch on and the two bus clocks
 * running. On these SoCs a read of a block whose clock is off does not fail, it hangs the bus -- the whole
 * phone, the loader's core with it. The clock controllers themselves always answer (GCC; dispcc once GCC's
 * clock for it runs, which is checked first), so this is asked before every read of the scan's counters,
 * and once the answer is no, the scan is given up for good: frames go back to the driver's own clock.
 */
static int
regs_alive(void)
{
	uint32_t v = rd(gcc_map, GCC_DISP_AHB_CBCR & 0xfffu);
	if (v == UINT32_MAX || !(v & CBCR_ENABLE) || (v & CBCR_OFF)) return 0;
	v = rd(dispcc_map, DISPCC_MDSS_CORE_GDSCR);
	if (v == UINT32_MAX || !(v & GDSC_POWER_ON)) return 0;
	v = rd(dispcc_map, DISPCC_MDSS_AHB_CBCR);
	return v != UINT32_MAX && (v & CBCR_ENABLE) && !(v & CBCR_OFF);
}

/* one bus clock on (and left on); 1 if it runs */
static int
clock_on(vm_offset_t base, uint32_t cbcr, const char *name)
{
	uint32_t v = rd(base, cbcr);
	if (v == UINT32_MAX) return 0;

	if (!(v & CBCR_ENABLE) || (v & CBCR_OFF)) {
		wr(base, cbcr, v | CBCR_ENABLE);
		delay_for_interval(10, NSEC_PER_MSEC);
		v = rd(base, cbcr);
	}
	if (!(v & CBCR_ENABLE) || (v & CBCR_OFF)) {
		printf("display: the %s clock does not take (%#x): no scan to go by\n", name, v);
		return 0;
	}
	return 1;
}

/*
 * Boot-arg disp_dump=1: print what the firmware left in the DSI controller, its PHY and the interface,
 * once, before the scan calibration. Read-only. This is the starting point of the 90 Hz work: the
 * interface's timing registers say at which rate the panel is being sent and whether the timing engine
 * (video mode) or the panel's own TE (command mode) paces it; the DSI registers say how the link and
 * the PHY's PLL are set. The panel itself switches between its modes with DSI commands (in its device
 * tree: qcom,mdss-dsi-timing-switch-command of each timing node), which is what would be sent here.
 */
static void
display_dump(void)
{
	vm_offset_t dsi;
	uint32_t on = 0;

	if (!PE_parse_boot_argn("disp_dump", &on, sizeof(on)) || on != 1) return;
	if (!intf) return;

	/* one line: the loader's console samples keep only the last lines of the log */
	dsi = ml_io_map(DSI0_PHYS, 0x1000);
	printf("display: dump: intf en=%#x hsy=%#x vsy=%#x v0=%#x v1=%#x frm=%#x lin=%#x cmd(te=%#x cf=%#x h=%#x i=%#x au=%#x) | dsi ctrl=%#x st=%#x clk=%#x fi=%#x cm=%#x dma=%#x trg=%#x | dbe %#x %#x %#x %#x | phy %#x %#x %#x %#x %#x %#x %#x %#x\n",
	    rd(intf, INTF_TIMING_ENGINE_EN), rd(intf, INTF_HSYNC_CTL), rd(intf, INTF_VSYNC_PERIOD),
	    rd(intf, INTF_DISPLAY_V_START), rd(intf, INTF_DISPLAY_V_END),
	    rd(intf, INTF_FRAME_COUNT), rd(intf, INTF_LINE_COUNT),
	    rd(intf, 0x284), rd(intf, 0x288), rd(intf, 0x28c), rd(intf, 0x294), rd(intf, 0x2b4),
	    dsi ? rd(dsi, 0x000) : 0, dsi ? rd(dsi, 0x004) : 0, dsi ? rd(dsi, 0x010) : 0,
	    dsi ? rd(dsi, 0x034) : 0, dsi ? rd(dsi, 0x040) : 0, dsi ? rd(dsi, 0x044) : 0,
	    dsi ? rd(dsi, 0x0a0) : 0,
	    dsi ? rd(dsi, 0x048) : 0, dsi ? rd(dsi, 0x04c) : 0, dsi ? rd(dsi, 0x050) : 0,
	    dsi ? rd(dsi, 0x054) : 0,
	    dsi ? rd(dsi, 0x400) : 0, dsi ? rd(dsi, 0x404) : 0, dsi ? rd(dsi, 0x408) : 0,
	    dsi ? rd(dsi, 0x40c) : 0, dsi ? rd(dsi, 0x410) : 0, dsi ? rd(dsi, 0x414) : 0,
	    dsi ? rd(dsi, 0x418) : 0, dsi ? rd(dsi, 0x41c) : 0);
	if (dsi) {
		printf("display: dump: rdbk %#x %#x %#x %#x %#x %#x ack=%#x\n",
		    rd(dsi, 0x006C), rd(dsi, 0x0070), rd(dsi, 0x0074), rd(dsi, 0x0078),
		    rd(dsi, 0x007C), rd(dsi, 0x0080), rd(dsi, 0x0068));
	}
}

/*
 * The same dump again, repeatedly: the loader samples the kernel's console once every 2 s for the
 * first 24 s of the boot, and a sample only carries the last lines of the log, so a single late
 * print can be missed. Printing it every 500 ms through that window makes sure one sample catches it.
 */
static void
display_dump_later(void *arg)
{
	static int times;
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000000 };

	(void)arg;
	display_dump();
	if (++times < 44) {
		bsd_timeout(display_dump_later, NULL, &ts);
	}
}

/*
 * Send one DCS long write (data type 0x39) to the panel through the controller's command FIFO:
 * the sequence the vendor's dsi_ctrl_hw_cmn_kickoff_fifo_command() uses -- the FIFO in embedded
 * mode, the packet's dwords written to its init register, the length, then the trigger. The panel
 * answers the commands in its device tree's timing-switch lists with a new scan rate.
 */
static int
dsi_dcs_write(vm_offset_t dsi, const uint8_t *payload, unsigned len)
{
	uint8_t packet[32];
	uint32_t dwords[8];
	unsigned total, i, ndwords;

	if (len == 0 || len > sizeof(packet) - 4) return 0;
	/* MIPI DCS long write: type, virtual channel 0, length low/high, then the parameters */
	packet[0] = 0x39;
	packet[1] = 0x00;
	packet[2] = (uint8_t)len;
	packet[3] = 0xC0;            /* BIT(6): payload follows, BIT(7): last command (vendor's flags) */
	memcpy(packet + 4, payload, len);
	total = 4 + len;
	ndwords = (total + 3) / 4;
	/* the vendor pads with 0xFF, not zero */
	for (unsigned i2 = total; i2 < ndwords * 4; i2++) packet[i2] = 0xFF;
	for (i = 0; i < ndwords; i++) {
		dwords[i] = 0;
		for (unsigned b = 0; b < 4 && i * 4 + b < ndwords * 4; b++)
			dwords[i] |= (uint32_t)packet[i * 4 + b] << (8 * b);
	}

	/* the vendor's reset_cmd_fifo(): disable the test-pattern generator, then reset the FIFO */
	wr(dsi, DSI_TEST_PATTERN_GEN_CTRL, 0);
	wr(dsi, DSI_TPG_DMA_FIFO_RESET, 1);
	delay_for_interval(1, NSEC_PER_MSEC);
	wr(dsi, DSI_TPG_DMA_FIFO_RESET, 0);

	/* the vendor's kickoff_fifo_command(): FIFO mode, the packet's dwords, the control, the length */
	wr(dsi, DSI_TEST_PATTERN_GEN_CTRL, (1u << 1) | (1u << 2) | (0x3u << 16));
	for (i = 0; i < ndwords; i++)
		wr(dsi, DSI_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL, dwords[i]);
	if ((ndwords & 1))    /* an odd number of dwords: one more, zero */
		wr(dsi, DSI_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL, 0);

	uint32_t reg = rd(dsi, DSI_COMMAND_MODE_DMA_CTRL);
	reg &= ~((1u << 24) | (1u << 29));    /* packet type 0, write count from the length */
	reg |= (1u << 28);                     /* embedded mode: the command is in the FIFO */
	reg &= ~(1u << 26);                    /* HS mode: the panel's timing switch is a dsi_hs_mode command */
	wr(dsi, DSI_COMMAND_MODE_DMA_CTRL, reg);
	wr(dsi, DSI_DMA_CMD_LENGTH, ndwords * 4);
	__asm__ volatile("dmb oshst" ::: "memory");
	wr(dsi, DSI_CMD_MODE_DMA_SW_TRIGGER, 1);
	delay_for_interval(5, NSEC_PER_MSEC);
	return 1;
}

/*
 * Boot-arg disp_cmd=2F04[,2602,...] (comma-separated hex bodies): each is sent as a DCS long write
 * a few seconds into the boot, the DSI's status printed after each, then the scan measured again --
 * the 90 Hz experiment: a command-mode panel switches its own timings, and the rate it scans at
 * afterwards says whether the command was the right one.
 */
static void
display_cmd_test(void *arg)
{
	char text[96];
	vm_offset_t dsi;
	unsigned part = 0;
	uint32_t ints = 0, st = 0, fifo = 0, rf = 0;

	(void)arg;
	if (!PE_parse_boot_argn("disp_cmd", text, sizeof(text))) return;

	dsi = ml_io_map(DSI0_PHYS, 0x1000);
	if (!dsi) { printf("display: disp_cmd: no DSI block\n"); return; }

	/* each comma-separated part is one command body */
	char *p = text;
	while (*p != 0 && part < 8) {
		uint8_t payload[16];
		unsigned len = 0;
		char *end = p;
		while (*end != 0 && *end != ',') end++;

		for (char *q = p; q < end && len < sizeof(payload); q += 2) {
			int hi = q[0] >= 'a' ? q[0] - 'a' + 10 : q[0] >= 'A' ? q[0] - 'A' + 10 : q[0] - '0';
			int lo = (q + 1 < end) ? (q[1] >= 'a' ? q[1] - 'a' + 10 : q[1] >= 'A' ? q[1] - 'A' + 10 : q[1] - '0') : 0;
			if (hi < 0 || hi > 15 || lo < 0 || lo > 15) break;
			payload[len++] = (uint8_t)(hi << 4 | lo);
		}
		if (len > 0) {
			dsi_dcs_write(dsi, payload, len);
			delay_for_interval(60, NSEC_PER_MSEC);
			ints = rd(dsi, 0x0110);
			st = rd(dsi, 0x0008);
			fifo = rd(dsi, 0x01DC);
			rf = rd(dsi, 0x0210);
		}
		if (*end == 0) break;
		p = end + 1;
		part++;
	}
	delay_for_interval(500, NSEC_PER_MSEC);

	/* what rate is the panel scanning at now? */
	scan.ok = 0;
	command_mode = 0;
	uint32_t hz = 0;
	if (redwood_display_scan_start()) {
		const struct redwood_display_scan *s = redwood_display_scan();
		hz = s->millihz;
	}
	/* repeat the one line for a while: the loader's console samples only keep the last lines */
	for (unsigned i = 0; i < 20; i++) {
		printf("display: disp_cmd %s: int=%#x st=%#x tpg=%#x rf=%#x now=%u.%03u Hz\n",
		    text, ints, st, fifo, rf, hz / 1000, hz % 1000);
		delay_for_interval(500, NSEC_PER_MSEC);
	}
}

uint32_t
redwood_display_line(uint32_t *frame)
{
	uint32_t f, line;

	if (intf == 0) {
		return 0;
	}
	if (!regs_alive()) {
		printf("display: the display block's clock or power went off: the scan is no longer read\n");
		intf = 0;
		scan.ok = 0;
		return 0;
	}
	if (command_mode) {
		uint32_t raw = rd(intf, 0x298) & 0xffff;
		if (frame != NULL) *frame = 0;
		line = raw >= te_initial ? raw - te_initial : 0;
		return line < scan.lines ? line : scan.lines - 1;
	}
	/* the two counters together: a frame that begins between the two reads is read again */
	for (unsigned retry = 0; retry < 8; retry++) {
		f = rd(intf, INTF_FRAME_COUNT);
		line = rd(intf, INTF_LINE_COUNT);
		if (rd(intf, INTF_FRAME_COUNT) == f) {
			if (frame != NULL) *frame = f;
			return line;
		}
	}
	/* A broken counter must not trap the presenter in an unbounded retry. */
	intf = 0;
	scan.ok = 0;
	if (frame != NULL) {
		*frame = f;
	}
	return line;
}

const struct redwood_display_scan *
redwood_display_scan(void)
{
	return scan.ok ? &scan : NULL;
}

/* The command-mode write pointer is the display DMA's progress through the
 * framebuffer. Copy only after it finishes, with room before the next TE.
 * Rendering/scaling must already be complete in a separate staging buffer.
 */
int
redwood_display_wait_copy(uint64_t copy_ns, uint64_t limit_ns)
{
	uint64_t start = mach_absolute_time(), elapsed;
	if (!redwood_display_command_mode()) return 0;
	uint64_t budget = copy_ns + copy_ns / 4 + 500000;
	for (;;) {
		uint32_t before = redwood_display_line(NULL);
		if (!scan.ok) return 0;
		uint32_t written = rd(intf, 0x2b0) & 0xffff;
		uint32_t phase = redwood_display_line(NULL);
		if (!scan.ok) return 0;
		uint64_t used = (uint64_t)phase * scan.line_ns;
		/* A completed DMA stays idle until the next TE. Restricting admission
		 * to the first 128 lines of that idle interval made a frame arriving
		 * slightly later wait an entire refresh, even with ample copy time.
		 * Use the measured copy budget instead. Read phase around the DMA
		 * status so a TE wrap cannot pair an old completion with a new frame.
		 */
		if (phase >= before && phase >= te_idle_start && written >= scan.shown &&
		    used < scan.frame_ns && budget < scan.frame_ns - used) return 1;
		absolutetime_to_nanoseconds(mach_absolute_time() - start, &elapsed);
		if (elapsed >= limit_ns) return 0;
		/* A 50 us poll spins in XNU. Sleep through the known active interval
		 * instead of occupying a rendering core for several milliseconds.
		 * Recheck the registers after waking; only the final 200 us is polled.
		 */
		uint64_t ahead = 0, nap = 50000;
		if (phase < te_idle_start) {
			ahead = (uint64_t)(te_idle_start - phase) * scan.line_ns;
		} else if (used >= scan.frame_ns || budget >= scan.frame_ns - used) {
			ahead = (used < scan.frame_ns ? scan.frame_ns - used : 0) +
			    (uint64_t)te_idle_start * scan.line_ns;
		}
		if (ahead > 400000) nap = ahead - 200000;
		if (nap > limit_ns - elapsed) nap = limit_ns - elapsed;
		delay_for_interval((uint32_t)((nap + 999) / 1000), NSEC_PER_USEC);
	}
}

static int
command_scan_start(void)
{
	uint32_t config = rd(intf, 0x288), refresh = rd(intf, 0x2b4);
	te_initial = rd(intf, 0x294) & 0xffff;
	if (!kona || !(rd(intf, 0x284) & 1) || (config & 0x180000) != 0x180000 ||
	    refresh != 0x80000001 || te_initial != 2340) return 0;
	uint64_t start = mach_absolute_time(), previous_time = start, first_wrap = 0, last_wrap = 0;
	uint64_t line_time = 0, line_count = 0, elapsed = 0;
	uint32_t previous = rd(intf, 0x298) & 0xffff, previous_first = previous;
	uint32_t busy_last = 0, written_last = 0, samples = 0;
	unsigned wraps = 0, busy_samples = 0, idle_samples = 0;
	/* this panel's counter may not wrap per frame: sample for a while instead */
	while (wraps < 4 && elapsed < 120000000) {
		delay_for_interval(50, NSEC_PER_USEC);
		uint32_t raw = rd(intf, 0x298) & 0xffff;
		uint32_t written = rd(intf, 0x2b0) & 0xffff;
		uint64_t now = mach_absolute_time(), interval;
		absolutetime_to_nanoseconds(now - start, &elapsed);
		if (elapsed > 150000000) break;
		/* Do not pair the previous frame's read phase with a new DMA transfer. */
		if ((rd(intf, 0x298) & 0xffff) < raw) continue;
		if (raw < previous) {
			if (wraps++ == 0) first_wrap = now;
			last_wrap = now;
		} else if (raw > previous) {
			absolutetime_to_nanoseconds(now - previous_time, &interval);
			line_time += interval;
			line_count += raw - previous;
		}
		uint32_t phase = raw - te_initial;
		if (written > 0 && written < te_initial) {
			if (phase > busy_last) busy_last = phase;
			busy_samples++;
		} else if (written >= te_initial) idle_samples++;
		previous = raw;
		previous_time = now;
		written_last = written;
		samples++;
	}
	if (1) {
		printf("display: TE samples: line counter %u -> %u (%u steps), written %u, wraps %u, "
		    "line_count %llu of %llu ns, busy %u idle %u, busy_last %u\n",
		    (unsigned)(te_initial & 0xffff), (unsigned)(previous & 0xffff), (unsigned)samples,
		    (unsigned)written_last, wraps, (unsigned long long)line_count,
		    (unsigned long long)line_time, busy_samples, idle_samples, busy_last);
	}
	if (!line_count) return 0;
	scan.line_ns = line_time / line_count;
	/* the kona frame is the shown lines at the measured line rate (the reference port's method:
	 * a wrap-derived period came out sixteen lines short of what the panel shows) */
	scan.frame_ns = scan.line_ns * te_initial;
	if (wraps >= 2) {
		uint64_t by_wrap;

		absolutetime_to_nanoseconds(last_wrap - first_wrap, &by_wrap);
		by_wrap /= (wraps - 1);
		printf("display: TE cross-check: %llu ns a frame by the wraps, %llu by the line rate\n",
		    (unsigned long long)by_wrap, (unsigned long long)scan.frame_ns);
	}
	if (1) {
		printf("display: TE computed: frame_ns %llu line_ns %llu wraps %u\n",
		    (unsigned long long)scan.frame_ns, (unsigned long long)scan.line_ns, wraps);
	}
	if (!scan.line_ns || scan.frame_ns < NSEC_PER_SEC / 120 ||
	    scan.frame_ns > NSEC_PER_SEC / 30) {
		printf("display: TE range check failed (frame_ns %llu line_ns %llu; allowed %llu..%llu)\n",
		    (unsigned long long)scan.frame_ns, (unsigned long long)scan.line_ns,
		    (unsigned long long)(NSEC_PER_SEC / 120), (unsigned long long)(NSEC_PER_SEC / 30));
		return 0;
	}
	scan.lines = te_initial;
	scan.shown = te_initial;
	/* the window from the measurement: the last line the DMA was still writing, plus a margin.
	 * (The frame's last 100 lines alone are 0.7 ms, far less than copying the panel's 10.4 MB.) */
	te_idle_start = busy_last + 32;
	if (te_idle_start >= scan.lines || scan.lines - te_idle_start < 200) {
		te_idle_start = scan.lines > 900 ? scan.lines - 900 : scan.lines / 2;
	}
	printf("display: staging window from line %u of %u: %u us of idle before the frame ends\n",
	    te_idle_start, scan.lines, (unsigned)((scan.lines - te_idle_start) * scan.line_ns / 1000));
	if (scan.lines < scan.shown || scan.lines > 8192 || te_idle_start >= scan.lines) {
		printf("display: TE sanity check failed (lines %u shown %u idle_start %u)\n",
		    scan.lines, scan.shown, te_idle_start);
		return 0;
	}
	scan.millihz = (uint32_t)(1000000000000ULL / scan.frame_ns);
	command_mode = true;
	scan.ok = 1;
	printf("display: UMI command TE: %u.%03u Hz, %u ns/line, %u phase lines; DMA idle after %u; staged presentation enabled\n",
	    scan.millihz / 1000, scan.millihz % 1000, (unsigned)scan.line_ns, scan.lines, te_idle_start);
	return 1;
}

/*
 * Wait until the scan is in [first, last) of a frame (lines, 0 = the first line shown), at most `limit_ns`;
 * the wait is a sleep for most of the way and a short spin for the end. 1 when it got there.
 */
int
redwood_display_wait_lines(uint32_t first, uint32_t last, uint64_t limit_ns)
{
	uint64_t start = mach_absolute_time(), now, waited;

	if (!scan.ok || first >= last || last > scan.lines) {
		return 0;
	}
	for (;;) {
		uint32_t line = redwood_display_line(NULL);
		uint64_t ahead_ns;

		if (!scan.ok) return 0;

		if (line >= first && line < last) {
			return 1;
		}
		now = mach_absolute_time();
		absolutetime_to_nanoseconds(now - start, &waited);
		if (waited >= limit_ns) {
			return 0;
		}
		/* lines to go to `first`, as time: sleep all but the last 200 us of it */
		ahead_ns = (uint64_t)((first + scan.lines - line) % scan.lines) * scan.line_ns;
		if (ahead_ns > 400000) {
			uint64_t nap = ahead_ns - 200000;

			if (waited + nap > limit_ns) {
				nap = limit_ns - waited;
			}
			delay_for_interval((uint32_t)(nap / 1000), NSEC_PER_USEC);
		}
	}
}

/* nanoseconds until the next frame begins (its first line shown), from where the scan is now */
uint64_t
redwood_display_next_frame_ns(void)
{
	uint32_t line;

	if (!scan.ok) {
		return 0;
	}
	line = redwood_display_line(NULL);
	return (uint64_t)(scan.lines - line) * scan.line_ns;
}

int
redwood_display_scan_start(void)
{
	uint32_t on = 0, hsync, period, v0, v1, f0, f1, l0, l1;
	uint64_t t0, t1, ns, gone;
	vm_offset_t gcc, dispcc;

	if (scan.ok) {
		return 1;
	}
	if (!PE_parse_boot_argn("fb_scan", &on, sizeof(on)) || on != 1) {
		return 0;
	}
	DTEntry chosen;
	const void *board;
	unsigned board_size;
	/* this port is kona: the Mi 10S's SoC. The loader publishes no /chosen/hakios-board, so the
	 * name cannot be asked for; the register layouts below are the kona ones. */
	kona = true;
	(void)chosen;
	(void)board;
	(void)board_size;
	gcc = gcc_map = ml_io_map(GCC_PHYS + (GCC_DISP_AHB_CBCR & ~0xfffu), 0x1000);
	if (!gcc) return 0;
	if (!clock_on(gcc, GCC_DISP_AHB_CBCR & 0xfffu, "display clock controller's bus")) {
		return 0;
	}
	dispcc = dispcc_map = ml_io_map(DISPCC_PHYS, kona ? 0x4000 : 0x2000);
	if (!dispcc) return 0;
	if (!(rd(dispcc, DISPCC_MDSS_CORE_GDSCR) & GDSC_POWER_ON)) {
		printf("display: the display block has no power (the firmware did not leave the panel on?): no scan to go by\n");
		return 0;
	}
	if (!clock_on(dispcc, DISPCC_MDSS_AHB_CBCR, "display registers' bus")) {
		return 0;
	}
	intf = ml_io_map(INTF1_PHYS, 0x1000);
	if (!intf) return 0;
	printf("display: %s scan registers at %#lx, engine=%#x; GCC AHB=%#x DISPCC AHB=%#x GDSC=%#x\n",
	    kona ? "UMI/Kona" : "Yupik", (unsigned long)INTF1_PHYS,
	    rd(intf, INTF_TIMING_ENGINE_EN), rd(gcc, GCC_DISP_AHB_CBCR & 0xfffu),
	    rd(dispcc, DISPCC_MDSS_AHB_CBCR), rd(dispcc, DISPCC_MDSS_CORE_GDSCR));
	display_dump();
	{
		/* and repeatedly through the window in which the loader samples the console */
		struct timespec dump_ts = { .tv_sec = 2, .tv_nsec = 0 };
		bsd_timeout(display_dump_later, NULL, &dump_ts);
	}
	{
		/* the panel's own mode switch, when disp_cmd= names one */
		struct timespec cmd_ts = { .tv_sec = 8, .tv_nsec = 0 };
		bsd_timeout(display_cmd_test, NULL, &cmd_ts);
	}
	if (!(rd(intf, INTF_TIMING_ENGINE_EN) & 1)) {
		printf("display: command mode: TE enable=%#x config=%#x height=%#x initial=%#x start=%#x auto=%#x\n",
		    rd(intf, 0x284), rd(intf, 0x288), rd(intf, 0x28c), rd(intf, 0x294), rd(intf, 0x2a0), rd(intf, 0x2b4));
		if (command_scan_start()) return 1;
		printf("display: command TE calibration failed; scan sync deferred\n");
		intf = 0;
		return 0;
	}
	hsync = rd(intf, INTF_HSYNC_CTL) >> 16;
	period = rd(intf, INTF_VSYNC_PERIOD);
	v0 = rd(intf, INTF_DISPLAY_V_START);
	v1 = rd(intf, INTF_DISPLAY_V_END);
	if (hsync == 0 || period < hsync || v1 <= v0) {
		printf("display: the timing engine's registers make no sense (line %u, frame %u clocks): no scan to go by\n", hsync, period);
		intf = 0;
		return 0;
	}
	scan.lines = period / hsync;
	scan.shown = (v1 - v0 + 1) / hsync;
	/* the rate the panel is really sent at: the counters 100 ms apart */
	l0 = redwood_display_line(&f0);
	if (!intf) return 0;
	t0 = mach_absolute_time();
	delay_for_interval(100, NSEC_PER_MSEC);
	l1 = redwood_display_line(&f1);
	if (!intf) return 0;
	t1 = mach_absolute_time();
	absolutetime_to_nanoseconds(t1 - t0, &ns);
	gone = (uint64_t)(f1 - f0) * scan.lines + l1 - l0;
	if (f1 == f0 || gone == 0 || ns == 0 || scan.lines < scan.shown || scan.shown == 0 ||
	    scan.lines > 65535 || scan.shown > 8192 || l0 >= scan.lines || l1 >= scan.lines) {
		printf("display: the scan counters do not move (frame %u -> %u, line %u -> %u): no scan to go by\n", f0, f1, l0, l1);
		intf = 0;
		return 0;
	}
	scan.line_ns = ns / gone;
	scan.frame_ns = scan.line_ns * scan.lines;
	if (scan.line_ns == 0 || scan.frame_ns < NSEC_PER_SEC / 240 ||
	    scan.frame_ns > NSEC_PER_SEC / 20) {
		printf("display: implausible scan period %llu ns; scan sync deferred\n", scan.frame_ns);
		intf = 0;
		return 0;
	}
	scan.millihz = (uint32_t)(1000000000000ULL / scan.frame_ns);
	scan.ok = 1;
	printf("display: the panel's scan: %u lines a frame (%u shown), %u.%03u frames a second, %llu ns a line; "
	    "frames follow it (fb_scan=1)\n", scan.lines, scan.shown, scan.millihz / 1000, scan.millihz % 1000, scan.line_ns);
	return 1;
}

/*
 * Map the panel's memory write-combining. ml_io_map_wcomb() would do it, but io_map hands mappings out
 * of a fixed 8 MB submap (and is KMA_NOFAIL), so the panel's 10.4 MB does not fit there and the boot
 * panics. ml_io_map_unmappable() is io_map's mode that takes from kernel_map, which is large.
 */
/* io_map's mode that maps out of kernel_map, and the write-combining flags (osfmk/arm/memory_types.h:
 * VM_WIMG_WCOMB = VM_MEM_NOT_CACHEABLE | VM_MEM_COHERENT). The arch headers are not in bsd's path. */
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, unsigned int flags);
#define REDWOOD_WCOMB   0x6u

vm_offset_t
redwood_display_panel_map(vm_offset_t phys, vm_size_t size)
{
	return ml_io_map_unmappable(phys, size, REDWOOD_WCOMB);
}
