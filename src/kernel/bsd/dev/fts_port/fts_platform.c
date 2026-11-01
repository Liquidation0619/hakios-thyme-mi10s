/*
 * fts_platform.c - the bridge between the ported ST driver and this machine's bit-banged bus.
 *
 * The ported library talks i2c_msg; redwood_goodix.c owns the pins. This file is the only place
 * the two meet, and it keeps the Linux-shaped headers out of the driver file.
 */
#include "compat.h"
#include "ftsSoftware.h"
#include "ftsCore.h"
#include "ftsIO.h"
#include "ftsCompensation.h"
#include "ftsFrame.h"
#include "ftsTool.h"

/* implemented in redwood_goodix.c, layout-compatible with struct i2c_msg */
struct fts_port_msg {
	unsigned short addr;
	unsigned short flags;
	unsigned short len;
	unsigned char *buf;
};

extern int redwood_fts_transfer(struct fts_port_msg *msgs, int num);
extern void redwood_fts_gpio_set(int gpio, int value);
extern void redwood_fb_blink(int times);

extern SysInfo systemInfo;

static struct i2c_adapter fts_port_adapter;
static struct i2c_client fts_port_client;

int
fts_platform_i2c_transfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	(void)adapter;
	return redwood_fts_transfer((struct fts_port_msg *)msgs, num);
}

void
fts_platform_gpio_set(int gpio, int value)
{
	redwood_fts_gpio_set(gpio, value);
}

static void
fts_port_open(void)
{
	fts_port_client.addr = 0x49;
	fts_port_client.adapter = &fts_port_adapter;
	openChannel(&fts_port_client);
}

/* Sum of one raw mutual frame, for comparing captures. */
int
fts_port_frame_sum(void)
{
	MutualSenseFrame frame;
	int ret;
	int sum = 0;

	fts_port_open();
	ret = getMSFrame3(MS_RAW, &frame);
	if (ret < 0) {
		return ret;
	}
	{
		int nodes = frame.header.force_node * frame.header.sense_node;

		if (nodes > 0 && nodes <= 2000) {
			for (int i = 0; i < nodes; i++) {
				sum += (unsigned short)frame.node_data[i];
			}
		}
	}
	if (frame.node_data != NULL) {
		kfree(frame.node_data);
	}
	return sum;
}

/* The full vendor bring-up: reset, read the system information, start sensing, read frames. */
int
fts_port_selftest(void)
{
	MutualSenseFrame frame;
	int ret;

	fts_port_open();
	pr_info("selftest: channel open, client addr %#x\n", fts_port_client.addr);

	ret = fts_system_reset();
	pr_info("selftest: system reset -> %d\n", ret);

	ret = readSysInfo(0);
	pr_info("selftest: readSysInfo -> %d  fw %#x cfg %#x chip %#x\n", ret,
	    systemInfo.u16_fwVer, systemInfo.u16_cfgVer, systemInfo.u16_chip0Id);

	ret = setScanMode(SCAN_MODE_ACTIVE, 0x01);
	pr_info("selftest: setScanMode -> %d\n", ret);

	for (int pass = 0; pass < 6; pass++) {
		/* exactly what fts_ms_raw_show does: sense on, let a frame land, sense off, read */
		(void)setScanMode(SCAN_MODE_ACTIVE, 0x01);
		mdelay(200);
		(void)setScanMode(SCAN_MODE_ACTIVE, 0x00);
		mdelay(60);
		flushFIFO();
		ret = getMSFrame3(MS_RAW, &frame);
		if (ret < 0) {
			pr_info("selftest: getMSFrame3 -> %d\n", ret);
			continue;
		}
		{
			int nodes = frame.header.force_node * frame.header.sense_node;
			int sum = 0;
			int first = 0;

			if (nodes > 2000) {
				nodes = 2000;
			}
			for (int i = 0; i < nodes; i++) {
				sum += (unsigned short)frame.node_data[i];
				if (i == 0) {
					first = frame.node_data[i];
				}
			}
			pr_info("selftest: frame %d  %dx%d  sum %d  first %d\n",
			    pass, frame.header.force_node, frame.header.sense_node, sum, first);
		}
		if (frame.node_data != NULL) {
			kfree(frame.node_data);
			frame.node_data = NULL;
		}
	}

	return 0;
}

/* Read a frame from the chip as the boot chain left it: no reset, no scan command, no toggling. */
int
fts_port_warm_frame(void)
{
	MutualSenseFrame frame;
	int ret;

	fts_port_open();

	for (int pass = 0; pass < 3; pass++) {
		ret = getMSFrame3(MS_RAW, &frame);
		if (ret < 0) {
			pr_info("warm: getMSFrame3 -> %d\n", ret);
			continue;
		}
		{
			int nodes = frame.header.force_node * frame.header.sense_node;
			int sum = 0;
			int first = 0;

			if (nodes > 0 && nodes <= 2000) {
				for (int i = 0; i < nodes; i++) {
					sum += (unsigned short)frame.node_data[i];
					if (i == 0) {
						first = frame.node_data[i];
					}
				}
			}
			pr_info("warm: %d  %dx%d sum %d first %d\n", pass,
			    frame.header.force_node, frame.header.sense_node, sum, first);
		}
		if (frame.node_data != NULL) {
			kfree(frame.node_data);
			frame.node_data = NULL;
		}
		mdelay(200);
	}

	return 0;
}
