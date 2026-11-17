/*
 * Fake qca-mcs transport for host tests: records every request and plays
 * back a scripted snooping table and scripted events.
 */
#ifndef FAKE_MCS_NL_H_
#define FAKE_MCS_NL_H_

#include "mcs_nl.h"
#include "mcs_table.h"

#define FAKE_REQ_PORT 1001
#define FAKE_EVENT_PORT 1002

struct fake_req {
	int fd;
	uint32_t portid;
	int type;
	char bridge[IFNAMSIZ];
	uint32_t len;
	uint8_t data[512];
};

extern struct fake_req fake_reqs[256];
extern int fake_req_cnt;

/* When set, mcs_nl_open() fails as it does without the kernel module */
extern int fake_open_fail;

/* Snooping table returned for GET_MDB */
extern struct mcs_mdb_entry fake_mdb[32];
extern int fake_mdb_cnt;

/* When set, the next GET_MDB reports a buffer overflow */
extern int fake_overflow_once;
extern uint32_t fake_overflow_needed;

/* The next fake_fail_count requests of fake_fail_type return fake_fail_status */
extern int fake_fail_type, fake_fail_status, fake_fail_count;

/* Number of mcs_nl_read_event() calls so far */
extern int fake_read_event_calls;

void fake_reset(void);

/* Queue an update event for a bridge and make the event socket readable */
void fake_queue_event(const char *bridge);

#endif /* FAKE_MCS_NL_H_ */
