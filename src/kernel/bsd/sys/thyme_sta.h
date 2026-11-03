/*
 * Darwin-on-thyme: the station boundary.
 *
 * On one side whatever does the scanning and the joining with a radio: the station logic over
 * ath11k on the phone (wifi/lx in the repository). On the other whoever wants a network: the
 * Apple80211 requests of iOS's Wi-Fi side, answered in bsd/dev/thyme_apple80211.c, and the
 * kernel's own join of the network it was given in a file (bsd/dev/thyme_wifi.c). Frames do
 * not pass here, and neither does "the link is up": those go through bsd/sys/thyme_net.h,
 * which the station side calls itself.
 *
 * Between the two stands bsd/dev/thyme_sta.c: one thread, on which every call into the station
 * side is made, one at a time, and what the last scan heard, kept for whoever asks.
 *
 * Nothing but C's own types here: the station side is not compiled as XNU's code.
 */
#ifndef _SYS_THYME_STA_H_
#define _SYS_THYME_STA_H_

#include <stdbool.h>
#include <stdint.h>

#define THYME_STA_ELEMENTS_MOST       1024

/* One access point, as a scan heard it. */
struct thyme_sta_bss {
	uint8_t bssid[6];
	uint8_t ssid[32], ssid_length;
	uint16_t frequency;                     /* MHz */
	int16_t rssi;                           /* dBm */
	uint16_t beacon_interval;               /* in units of 1024 microseconds */
	uint16_t capability;                    /* the beacon's capability field */
	uint32_t age_ms;                        /* how long ago it was last heard */
	uint16_t elements_length;               /* the beacon's information elements as received: who asks */
	uint8_t elements[THYME_STA_ELEMENTS_MOST];    /* reads the network's protection out of them itself */
};

/* What to scan for. All zero: every channel, the radio's own way. */
struct thyme_sta_scan {
	uint8_t ssid[32], ssid_length;          /* ask for this network by name (a hidden one answers only then) */
	uint16_t frequencies[64], frequency_count;      /* only these channels */
	bool passive;                           /* only listen */
	uint16_t dwell_ms;                      /* on each channel; 0: the radio's own */
};

enum { THYME_STA_OPEN, THYME_STA_WPA2_PSK };

struct thyme_sta_join {
	uint8_t ssid[32], ssid_length;
	uint8_t bssid[6];                       /* all zero: the access point of that name heard best */
	int security;
	uint8_t pmk[32];                        /* WPA2 with a passphrase: the key made of the passphrase and the name ... */
	uint8_t passphrase[64], passphrase_length;      /* ... or, if this is not empty, the passphrase itself */
};

enum thyme_sta_phase { THYME_STA_OFF, THYME_STA_IDLE, THYME_STA_SCANNING, THYME_STA_JOINING, THYME_STA_JOINED };

struct thyme_sta_state {
	enum thyme_sta_phase phase;
	struct thyme_sta_bss bss;             /* the network joined or being joined */
	int last_error;                         /* why the last join failed or the network was lost (an errno); 0: it was not */
};

#define THYME_STA_CHANNEL_PASSIVE     1u      /* the radio may only listen there */
#define THYME_STA_CHANNEL_RADAR       2u      /* ... and has to look out for radar */

struct thyme_sta_channel {
	uint16_t frequency;                     /* MHz */
	uint16_t flags;
};

/*
 * What the station side supplies. All but state() are called on one thread, one at a time, and
 * may take as long as the thing takes: scan() returns when the scan is over, join() when frames
 * can pass or it is known why not. Each gives 0 or an errno.
 */
struct thyme_sta_ops {
	int (*power)(bool on);                  /* off: leave the network, stop the radio; on: start it */
	int (*scan)(const struct thyme_sta_scan *what);
	void (*heard)(void (*each)(const struct thyme_sta_bss *bss, void *context), void *context);   /* by the last scan */
	int (*join)(const struct thyme_sta_join *what);
	int (*leave)(void);
	void (*state)(struct thyme_sta_state *state);         /* on any thread; does not sleep */
	unsigned (*channels)(struct thyme_sta_channel *channels, unsigned most);      /* what the radio may tune to */
	uint8_t address[6];
};

/* What happens, said to whoever listens (the Apple80211 side). */
enum thyme_sta_event {
	THYME_STA_POWER,                      /* status: 1 on, 0 off */
	THYME_STA_SCAN_DONE,                  /* status: 0, or the scan's errno */
	THYME_STA_JOIN_DONE,                  /* status: 0, or the join's errno */
	THYME_STA_LINK,                       /* status: 1 a network is joined, 0 it has been left when asked,
	                                         * an errno: it was lost (the access point showed us the door) */
};

#ifdef KERNEL
/* ---- the station side ---- */
void     thyme_sta_attach(const struct thyme_sta_ops *ops);         /* NULL: it is going away (waits for a call under way) */
void     thyme_sta_lost(int error);                                   /* the network joined is gone by itself */

/* ---- who wants a network: each asks and returns; the outcome is an event. 0, or an errno ---- */
bool     thyme_sta_present(void);
int      thyme_sta_ask_power(bool on);
int      thyme_sta_ask_scan(const struct thyme_sta_scan *what);
int      thyme_sta_ask_join(const struct thyme_sta_join *what);
/* ... and joined again whenever the radio is turned on, until some other network is asked for */
int      thyme_sta_ask_join_standing(const struct thyme_sta_join *what);
int      thyme_sta_ask_leave(void);
bool     thyme_sta_busy(void);                                        /* something asked for is not over yet */
bool     thyme_sta_powered(void);
/* the `index`th access point the last scan heard: true, or false when there are not that many */
bool     thyme_sta_heard(unsigned index, struct thyme_sta_bss *bss);
void     thyme_sta_state(struct thyme_sta_state *state);
unsigned thyme_sta_channels(struct thyme_sta_channel *channels, unsigned most);
bool     thyme_sta_address(uint8_t *address);

/* ---- the listener: bsd/dev/thyme_apple80211.c ---- */
void     thyme_apple80211_event(enum thyme_sta_event event, int status);
void     thyme_apple80211_summary(void);                              /* what was asked in this run, to the log */
#endif

#endif
