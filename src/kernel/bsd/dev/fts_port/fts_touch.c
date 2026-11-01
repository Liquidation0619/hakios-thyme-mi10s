/*
 * fts_touch.c - the phone's ST FTS touch panel, on top of the ported vendor library.
 *
 * The event format comes from fts_enter_pointer_event_handler() in the vendor driver:
 *
 *     event[0]      event id: 0x13 enter, 0x23 motion, 0x33 leave, 0x43 status
 *     event[1]      [7:4] finger id, [3:0] touch type (0 finger, 1 glove, 2 hover, ...)
 *     event[2]      x low byte
 *     event[3]      [3:0] x high nibble, [7:4] y low nibble
 *     event[4]      y high byte
 *     event[5..6]   area, motion events only
 *     event[7]      [4:0] events still queued
 *
 * The panel reports screen coordinates directly (the device tree gives x-max 1080, y-max 2340),
 * so the values go to redwood_touch_post() unchanged.
 */
#include "compat.h"
#include "ftsSoftware.h"
#include "ftsCore.h"
#include "ftsIO.h"

#include <sys/redwood_input.h>
#include <kern/thread_call.h>
#include <kern/clock.h>

extern SysInfo systemInfo;

struct i2c_adapter fts_touch_adapter;
struct i2c_client fts_touch_client;

static thread_call_t fts_poll_call;
static uint32_t fts_poll_interval_ms = 8;
static boolean_t fts_polling;
static volatile uint32_t fts_poll_count;
static volatile uint32_t fts_event_count;

#define FTS_X_MAX 1080
#define FTS_Y_MAX 2340

static void
fts_post_event(const uint8_t *event)
{
	uint32_t type;
	uint32_t finger;
	uint32_t x, y;

	switch (event[0]) {
	case EVT_ID_ENTER_POINT:
		type = REDWOOD_TOUCH_DOWN;
		break;
	case EVT_ID_MOTION_POINT:
		type = REDWOOD_TOUCH_MOVE;
		break;
	case EVT_ID_LEAVE_POINT:
		type = REDWOOD_TOUCH_UP;
		break;
	default:
		return;                 /* status and error events are not touches */
	}

	finger = (event[1] & 0xF0) >> 4;
	x = (uint32_t)((((uint32_t)event[3] & 0x0F) << 8) | event[2]);
	y = (uint32_t)(((uint32_t)event[4] << 4) | (((uint32_t)event[3] & 0xF0) >> 4));

	if (x > FTS_X_MAX) {
		x = FTS_X_MAX;
	}
	if (y > FTS_Y_MAX) {
		y = FTS_Y_MAX;
	}
	pr_info("touch: type %u id %u x %u y %u\n", type, finger, x, y);
	fts_event_count++;
	redwood_touch_post(type, finger, x, y);
}


/* fts_trace=N: run for N seconds, say what was seen, then let the loader have the machine back */
static void
fts_trace_report(thread_call_param_t p0, thread_call_param_t p1)
{
	(void)p0;
	(void)p1;
	pr_info("fts_trace: poll cycles %u, touch events %u\n", fts_poll_count, fts_event_count);
	panic("fts_trace: report");
}

static void
fts_poll(thread_call_param_t p0, thread_call_param_t p1)
{
	uint8_t events[FIFO_EVENT_SIZE];
	uint64_t deadline;
	int ret;

	(void)p0;
	(void)p1;

	fts_poll_count++;
	ret = fts_writeReadU8UX(FIFO_CMD_READALL, 0, 0, events, FIFO_EVENT_SIZE, DUMMY_FIFO);
	if (ret >= 0 && events[0] != EVT_ID_NOEVENT && events[0] != 0xFF) {
		fts_post_event(events);
	}

	if (fts_polling) {
		clock_interval_to_deadline(fts_poll_interval_ms, NSEC_PER_MSEC, &deadline);
		thread_call_enter_delayed(fts_poll_call, deadline);
	}
}

/* Bring the panel up: reset, read its system information, and start sensing. */
int
fts_touch_init(void)
{
	int ret;

	fts_touch_client.addr = 0x49;
	fts_touch_client.adapter = &fts_touch_adapter;
	openChannel(&fts_touch_client);

	ret = fts_system_reset();
	pr_info("fts_touch: reset %d\n", ret);

	ret = readSysInfo(0);
	pr_info("fts_touch: sysinfo %d  fw %#x cfg %#x chip %#x  panel %dx%d\n", ret,
	    systemInfo.u16_fwVer, systemInfo.u16_cfgVer, systemInfo.u16_chip0Id,
	    systemInfo.u8_scrTxLen, systemInfo.u8_scrRxLen);

	ret = setScanMode(SCAN_MODE_ACTIVE, 0x01);
	pr_info("fts_touch: sense on %d\n", ret);

	fts_poll_call = thread_call_allocate(fts_poll, NULL);
	if (fts_poll_call == NULL) {
		return -1;
	}
	fts_polling = TRUE;
	fts_poll(NULL, NULL);
	{
		uint32_t secs = 0;

		if (PE_parse_boot_argn("fts_trace", &secs, sizeof(secs)) && secs > 0) {
			thread_call_t report = thread_call_allocate(fts_trace_report, NULL);
			uint64_t deadline;

			if (report != NULL) {
				clock_interval_to_deadline(secs, NSEC_PER_SEC, &deadline);
				thread_call_enter_delayed(report, deadline);
				pr_info("fts_touch: will report in %u s\n", secs);
			}
		}
	}
	return 0;
}

uint32_t
fts_touch_poll_count(void)
{
	return fts_poll_count;
}

void
fts_touch_stop(void)
{
	fts_polling = FALSE;
	if (fts_poll_call != NULL) {
		thread_call_cancel(fts_poll_call);
	}
}
