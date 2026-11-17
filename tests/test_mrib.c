/*
 * Host unit tests for mrib route handling.
 *
 * mrib.c is included directly so that its static state (interfaces, routes)
 * can be set up without a kernel multicast routing socket, and setsockopt()
 * is replaced to record the routes it would install.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/mroute.h>

struct mfc_call {
	int opt;
	struct mfcctl ctl;
};

static struct mfc_call calls[16];
static int call_cnt;

static int fake_setsockopt(__attribute__((unused)) int fd, int level, int opt,
		const void *val, socklen_t len)
{
	if (level == IPPROTO_IP && (opt == MRT_ADD_MFC || opt == MRT_DEL_MFC) &&
			len == sizeof(struct mfcctl)) {
		calls[call_cnt].opt = opt;
		memcpy(&calls[call_cnt].ctl, val, len);
		++call_cnt;
	}

	return 0;
}

#define setsockopt fake_setsockopt
#include "../src/mrib.c"
#undef setsockopt

int log_level = 0;

#define UPLINK_MIF 0
#define DOWNLINK_MIF 1

static struct mrib_user uplink_user, downlink_user;
static bool downlink_wants;

/* Stands in for the proxy: forward to the downlink if it has a receiver */
static void uplink_newsource(__unused struct mrib_user *user, __unused const struct in6_addr *group,
		__unused const struct in6_addr *source, mrib_filter *filter)
{
	if (downlink_wants)
		mrib_filter_add(filter, &downlink_user);
}

static struct in6_addr mapped(const char *v4)
{
	struct in6_addr addr = IN6ADDR_ANY_INIT;

	addr.s6_addr[10] = addr.s6_addr[11] = 0xff;
	inet_pton(AF_INET, v4, &addr.s6_addr[12]);
	return addr;
}

static void setup(void)
{
	memset(mifs, 0, sizeof(mifs));
	memset(calls, 0, sizeof(calls));
	call_cnt = 0;
	downlink_wants = false;
	mrt_fd.fd = 100;	/* any value, setsockopt is faked */

	for (int i = UPLINK_MIF; i <= DOWNLINK_MIF; ++i) {
		mifs[i].ifindex = 10 + i;
		mifs[i].timer.cb = mrib_clean;
		INIT_LIST_HEAD(&mifs[i].routes);
		INIT_LIST_HEAD(&mifs[i].users);
		INIT_LIST_HEAD(&mifs[i].queriers);
	}

	memset(&uplink_user, 0, sizeof(uplink_user));
	memset(&downlink_user, 0, sizeof(downlink_user));
	uplink_user.iface = &mifs[UPLINK_MIF];
	uplink_user.cb_newsource = uplink_newsource;
	list_add(&uplink_user.head, &mifs[UPLINK_MIF].users);
	downlink_user.iface = &mifs[DOWNLINK_MIF];
	list_add(&downlink_user.head, &mifs[DOWNLINK_MIF].users);
}

static void teardown(void)
{
	struct mrib_route *r, *n;

	list_for_each_entry_safe(r, n, &mifs[UPLINK_MIF].routes, head) {
		list_del(&r->head);
		free(r);
	}

	uloop_timeout_cancel(&mifs[UPLINK_MIF].timer);
	mrt_fd.fd = -1;
}

/*
 * A stream seen before its first receiver gets a route without outputs.
 * When the receiver shows up, the route is updated in place: no delete,
 * so packets of a stream that is already being forwarded are not lost.
 */
static void test_refresh_updates_route_in_place(void)
{
	struct in6_addr group = mapped("233.18.158.251");
	struct in6_addr source = mapped("125.159.44.41");
	struct mrib_route *route;

	setup();
	mrib_notify_newsource(&mifs[UPLINK_MIF], &group, &source);
	assert(call_cnt == 1);
	assert(calls[0].opt == MRT_ADD_MFC);
	assert(calls[0].ctl.mfcc_ttls[DOWNLINK_MIF] == 0);

	downlink_wants = true;
	assert(mrib_refresh(&uplink_user, &group) == 0);

	assert(call_cnt == 2);
	assert(calls[1].opt == MRT_ADD_MFC);
	assert(calls[1].ctl.mfcc_parent == UPLINK_MIF);
	assert(calls[1].ctl.mfcc_origin.s_addr == source.s6_addr32[3]);
	assert(calls[1].ctl.mfcc_mcastgrp.s_addr == group.s6_addr32[3]);
	assert(calls[1].ctl.mfcc_ttls[DOWNLINK_MIF] == 1);

	/* The route is still tracked so that it expires as before */
	route = list_first_entry(&mifs[UPLINK_MIF].routes, struct mrib_route, head);
	assert(IN6_ARE_ADDR_EQUAL(&route->group, &group));
	assert(route->valid_until > 0);
	teardown();
}

static void test_refresh_removes_outputs(void)
{
	struct in6_addr group = mapped("233.18.158.251");
	struct in6_addr source = mapped("125.159.44.41");

	setup();
	downlink_wants = true;
	mrib_notify_newsource(&mifs[UPLINK_MIF], &group, &source);
	assert(calls[0].ctl.mfcc_ttls[DOWNLINK_MIF] == 1);

	downlink_wants = false;
	assert(mrib_refresh(&uplink_user, &group) == 0);
	assert(call_cnt == 2);
	assert(calls[1].opt == MRT_ADD_MFC);
	assert(calls[1].ctl.mfcc_ttls[DOWNLINK_MIF] == 0);
	teardown();
}

static void test_refresh_only_touches_the_group(void)
{
	struct in6_addr group = mapped("233.18.158.251");
	struct in6_addr other = mapped("233.18.158.250");
	struct in6_addr source = mapped("125.159.44.41");
	struct in6_addr source2 = mapped("125.159.44.42");

	setup();
	mrib_notify_newsource(&mifs[UPLINK_MIF], &group, &source);
	mrib_notify_newsource(&mifs[UPLINK_MIF], &other, &source);
	mrib_notify_newsource(&mifs[UPLINK_MIF], &group, &source2);
	call_cnt = 0;

	downlink_wants = true;
	assert(mrib_refresh(&uplink_user, &group) == 0);

	/* Both sources of the group, nothing else */
	assert(call_cnt == 2);
	for (int i = 0; i < call_cnt; ++i)
		assert(calls[i].ctl.mfcc_mcastgrp.s_addr == group.s6_addr32[3]);
	teardown();
}

static void test_refresh_without_routes(void)
{
	struct in6_addr group = mapped("233.18.158.251");

	setup();
	assert(mrib_refresh(&uplink_user, &group) == -ENOENT);
	assert(call_cnt == 0);
	teardown();
}

static void test_refresh_detached_user(void)
{
	struct in6_addr group = mapped("233.18.158.251");
	struct mrib_user detached = { .iface = NULL };

	setup();
	assert(mrib_refresh(&detached, &group) == -ENOENT);
	assert(call_cnt == 0);
	teardown();
}

int main(void)
{
	uloop_init();
	test_refresh_updates_route_in_place();
	test_refresh_removes_outputs();
	test_refresh_only_touches_the_group();
	test_refresh_without_routes();
	test_refresh_detached_user();
	uloop_done();
	printf("test_mrib: ok\n");
	return 0;
}
