/*
 * Host unit tests for the qca-mcs netlink transport.
 *
 * The build host has no qca-mcs module, so these pin the behaviour on a
 * system where the module is absent: every call must fail cleanly.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>

#include "mcs_nl.h"

static void test_open_fails_without_module(void)
{
	uint32_t portid = 0;

	assert(mcs_nl_open(&portid) == -1);
}

static void test_request_on_bad_fd(void)
{
	char buf[64] = {0};

	assert(mcs_nl_request(-1, 0, MCS_MSG_GET_MDB, "br0", buf, sizeof(buf), NULL, NULL) == -1);
}

static void test_read_event_on_bad_fd(void)
{
	char name[IFNAMSIZ] = {0};

	assert(mcs_nl_read_event(-1, name) == -1);
}

static void test_bridge_name_non_bridge(void)
{
	char name[IFNAMSIZ] = {0};

	assert(!mcs_nl_bridge_name(if_nametoindex("lo"), name));
	assert(!mcs_nl_bridge_name(0, name));
}

int main(void)
{
	test_open_fails_without_module();
	test_request_on_bad_fd();
	test_read_event_on_bad_fd();
	test_bridge_name_non_bridge();
	printf("test_mcs_nl: ok\n");
	return 0;
}
