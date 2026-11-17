/*
 * Host unit tests for the qca-mcs lifecycle: which requests are sent to the
 * kernel module when downlinks come and go, and when its table changes.
 *
 * Built with a short refresh interval (MCS_REFRESH_MS) and run against a
 * fake transport and the real libubox event loop.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include <libubox/uloop.h>

#include "mcs.h"
#include "fake_mcs_nl.h"

int log_level = 0;

static void setup(void)
{
	fake_reset();
	uloop_init();
}

static void teardown(void)
{
	mcs_deinit();
	uloop_done();
}

static uint32_t req_val(int idx, int word)
{
	uint32_t v;

	memcpy(&v, &fake_reqs[idx].data[word * 4], sizeof(v));
	return v;
}

/* Count requests of a type from index onwards; bridge and val are optional filters */
static int count_reqs(int from, int type, const char *bridge, int64_t val)
{
	int cnt = 0;

	for (int i = from; i < fake_req_cnt; ++i) {
		if (fake_reqs[i].type != type)
			continue;

		if (bridge && strcmp(fake_reqs[i].bridge, bridge))
			continue;

		if (val >= 0 && req_val(i, 0) != (uint32_t)val)
			continue;

		++cnt;
	}

	return cnt;
}

static void add_member(const char *group, uint32_t ifindex)
{
	struct mcs_mdb_entry *e = &fake_mdb[fake_mdb_cnt++];

	e->group.pro = htons(0x0800);
	inet_pton(AF_INET, group, &e->group.u.ip4);
	e->ifindex = ifindex;
}

static void test_unavailable_sends_nothing(void)
{
	setup();
	fake_open_fail = 1;
	mcs_init();
	mcs_attach(100);
	mcs_detach(100);
	mcs_deinit();
	assert(fake_req_cnt == 0);
	teardown();
}

/* Starting up must not change module state: "omcproxy -h" gets this far too */
static void test_init_and_deinit_send_nothing(void)
{
	setup();
	mcs_init();
	mcs_deinit();
	assert(fake_req_cnt == 0);
	teardown();
}

static void test_first_bridge_registers_event_pid(void)
{
	setup();
	mcs_init();
	mcs_attach(100);
	assert(fake_reqs[0].type == MCS_MSG_SET_EVENT_PID);
	assert(fake_reqs[0].portid == FAKE_REQ_PORT);
	assert(fake_reqs[0].len == sizeof(struct mcs_param_value));
	assert(req_val(0, 0) == FAKE_EVENT_PORT);
	assert(!strcmp(fake_reqs[0].bridge, ""));

	mcs_attach(101);
	assert(count_reqs(0, MCS_MSG_SET_EVENT_PID, NULL, -1) == 1);
	teardown();
}

static void test_attach_bridge(void)
{
	static const int expect[] = { MCS_MSG_SET_ENABLE, MCS_MSG_SET_POLICY,
		MCS_MSG_SET_RETAG, MCS_MSG_GET_MDB, MCS_MSG_SET_PSW_FLOOD };
	int mark;

	setup();
	mcs_init();
	mcs_attach(100);
	mark = 1;	/* after the event registration */

	assert(fake_req_cnt - mark == 5);
	for (int i = 0; i < 5; ++i) {
		assert(fake_reqs[mark + i].type == expect[i]);
		assert(!strcmp(fake_reqs[mark + i].bridge, "br100"));
		assert(fake_reqs[mark + i].portid == FAKE_REQ_PORT);
	}

	assert(req_val(mark, 0) == 1);
	assert(req_val(mark + 1, 0) == MCS_POLICY_FLOOD);
	assert(fake_reqs[mark + 2].len == sizeof(struct mcs_param_retag));
	assert(req_val(mark + 2, 0) == 0 && req_val(mark + 2, 1) == 0);
	teardown();
}

static void test_attach_non_bridge(void)
{
	int mark;

	setup();
	mcs_init();
	mark = fake_req_cnt;
	mcs_attach(5);
	mcs_detach(5);
	assert(fake_req_cnt == mark);
	teardown();
}

static void test_detach_unknown_bridge(void)
{
	int mark;

	setup();
	mcs_init();
	mark = fake_req_cnt;
	mcs_detach(100);
	assert(fake_req_cnt == mark);
	teardown();
}

static void test_flood_payload(void)
{
	struct mcs_flood_entry fe;
	struct fake_req *req;

	setup();
	add_member("233.18.158.251", 11);
	add_member("233.18.158.251", 20);
	mcs_init();
	mcs_attach(100);

	req = &fake_reqs[fake_req_cnt - 1];
	assert(req->type == MCS_MSG_SET_PSW_FLOOD);
	assert(req->len == sizeof(fe));
	memcpy(&fe, req->data, sizeof(fe));
	assert(fe.group.u.ip4 == fake_mdb[0].group.u.ip4);
	assert(fe.ifcnt == 2);
	assert(fe.ifindex[0] == 11 && fe.ifindex[1] == 20);
	teardown();
}

static void test_empty_mdb_clears_table(void)
{
	struct fake_req *req;

	setup();
	mcs_init();
	mcs_attach(100);

	req = &fake_reqs[fake_req_cnt - 1];
	assert(req->type == MCS_MSG_SET_PSW_FLOOD);
	assert(req->len == 0);
	teardown();
}

static void test_mdb_overflow_retry(void)
{
	int mark;

	setup();
	add_member("233.18.158.251", 11);
	fake_overflow_once = 1;
	fake_overflow_needed = 300 * sizeof(struct mcs_mdb_entry);
	mcs_init();
	mcs_attach(100);
	mark = 1;	/* after the event registration */

	assert(fake_req_cnt - mark == 6);
	assert(fake_reqs[mark + 3].type == MCS_MSG_GET_MDB);
	assert(fake_reqs[mark + 3].len < fake_overflow_needed);
	assert(fake_reqs[mark + 4].type == MCS_MSG_GET_MDB);
	assert(fake_reqs[mark + 4].len >= fake_overflow_needed);
	assert(fake_reqs[mark + 5].type == MCS_MSG_SET_PSW_FLOOD);
	assert(fake_reqs[mark + 5].len == sizeof(struct mcs_flood_entry));
	teardown();
}

static void test_shared_bridge_refcount(void)
{
	setup();
	mcs_init();
	mcs_attach(100);
	mcs_attach(100);

	mcs_detach(100);
	assert(count_reqs(0, MCS_MSG_SET_ENABLE, "br100", 0) == 0);

	mcs_detach(100);
	assert(count_reqs(0, MCS_MSG_SET_ENABLE, "br100", 0) == 1);
	teardown();
}

static void test_attach_always_refreshes(void)
{
	setup();
	mcs_init();
	mcs_attach(100);
	mcs_attach(100);
	assert(count_reqs(0, MCS_MSG_SET_ENABLE, "br100", 1) == 2);
	assert(count_reqs(0, MCS_MSG_SET_PSW_FLOOD, "br100", -1) == 2);
	teardown();
}

static void test_event_triggers_refresh(void)
{
	int mark;

	setup();
	mcs_init();
	mcs_attach(100);
	mark = fake_req_cnt;

	/* The fake ends the loop once the event has been read */
	fake_queue_event("br100");
	uloop_run_timeout(1000);

	assert(fake_read_event_calls >= 1);
	assert(fake_req_cnt - mark == 2);
	assert(fake_reqs[mark].type == MCS_MSG_GET_MDB);
	assert(fake_reqs[mark + 1].type == MCS_MSG_SET_PSW_FLOOD);
	assert(!strcmp(fake_reqs[mark + 1].bridge, "br100"));
	teardown();
}

static void test_event_for_unknown_bridge(void)
{
	int mark;

	setup();
	mcs_init();
	mcs_attach(100);
	mark = fake_req_cnt;

	fake_queue_event("br7");
	uloop_run_timeout(1000);

	assert(fake_read_event_calls >= 1);
	assert(fake_req_cnt == mark);
	teardown();
}

static void test_timer_refreshes_all(void)
{
	int mark;

	setup();
	mcs_init();
	mcs_attach(100);
	mcs_attach(101);
	mark = fake_req_cnt;

	/*
	 * Several refresh intervals: the timer has to fire and re-arm.
	 * uloop_run_timeout() handles due timers once and then waits for at
	 * most one interval, so it is called repeatedly.
	 */
	for (int i = 0; i < 8; ++i)
		uloop_run_timeout(MCS_REFRESH_MS);

	assert(count_reqs(mark, MCS_MSG_GET_MDB, "br100", -1) >= 2);
	assert(count_reqs(mark, MCS_MSG_SET_PSW_FLOOD, "br100", -1) >= 2);
	assert(count_reqs(mark, MCS_MSG_GET_MDB, "br101", -1) >= 2);
	assert(count_reqs(mark, MCS_MSG_SET_PSW_FLOOD, "br101", -1) >= 2);
	assert(count_reqs(mark, MCS_MSG_SET_ENABLE, NULL, -1) == 0);

	/* Another process may have taken the event registration: renew it */
	assert(count_reqs(mark, MCS_MSG_SET_EVENT_PID, NULL, FAKE_EVENT_PORT) >= 2);
	teardown();
}

static void test_deinit_disables(void)
{
	int mark;

	setup();
	mcs_init();
	mcs_attach(100);
	mark = fake_req_cnt;
	mcs_deinit();

	assert(fake_req_cnt - mark == 2);
	assert(fake_reqs[mark].type == MCS_MSG_SET_ENABLE);
	assert(!strcmp(fake_reqs[mark].bridge, "br100"));
	assert(req_val(mark, 0) == 0);
	assert(fake_reqs[mark + 1].type == MCS_MSG_SET_EVENT_PID);
	assert(req_val(mark + 1, 0) == MCS_INVALID_PID);

	/* Nothing is left to do after that */
	mark = fake_req_cnt;
	mcs_attach(100);
	mcs_deinit();
	assert(fake_req_cnt == mark);
	teardown();
}

int main(void)
{
	test_unavailable_sends_nothing();
	test_init_and_deinit_send_nothing();
	test_first_bridge_registers_event_pid();
	test_attach_bridge();
	test_attach_non_bridge();
	test_detach_unknown_bridge();
	test_flood_payload();
	test_empty_mdb_clears_table();
	test_mdb_overflow_retry();
	test_shared_bridge_refcount();
	test_attach_always_refreshes();
	test_event_triggers_refresh();
	test_event_for_unknown_bridge();
	test_timer_refreshes_all();
	test_deinit_disables();
	printf("test_mcs: ok\n");
	return 0;
}
