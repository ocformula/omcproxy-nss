/*
 * Host unit tests for the qca-mcs flood table builder.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "mcs_table.h"

#define PRO_IPV4 htons(0x0800)
#define PRO_IPV6 htons(0x86DD)

static struct mcs_mdb_entry v4_entry(const char *group, uint32_t ifindex, uint8_t mac_last)
{
	struct mcs_mdb_entry e;

	memset(&e, 0, sizeof(e));
	e.group.pro = PRO_IPV4;
	inet_pton(AF_INET, group, &e.group.u.ip4);
	e.ifindex = ifindex;
	e.mac[5] = mac_last;
	return e;
}

static struct mcs_mdb_entry v6_entry(const char *group, uint32_t ifindex)
{
	struct mcs_mdb_entry e;

	memset(&e, 0, sizeof(e));
	e.group.pro = PRO_IPV6;
	inet_pton(AF_INET6, group, e.group.u.ip6);
	e.ifindex = ifindex;
	return e;
}

static void test_sizes(void)
{
	assert(sizeof(struct mcs_group) == 20);
	assert(sizeof(struct mcs_mdb_entry) == 104);
	assert(sizeof(struct mcs_flood_entry) == 88);
}

static void test_empty(void)
{
	struct mcs_flood_entry out[4];

	assert(mcs_build_flood(NULL, 0, out, 4) == 0);
}

static void test_single(void)
{
	struct mcs_mdb_entry mdb[] = { v4_entry("233.18.158.251", 11, 1) };
	struct mcs_flood_entry out[4];

	memset(out, 0xff, sizeof(out));
	assert(mcs_build_flood(mdb, 1, out, 4) == 1);
	assert(out[0].ifcnt == 1);
	assert(out[0].ifindex[0] == 11);
	assert(out[0].ifindex[1] == 0);
	assert(!memcmp(&out[0].group, &mdb[0].group, sizeof(struct mcs_group)));
}

static void test_same_port_two_members(void)
{
	struct mcs_mdb_entry mdb[] = {
		v4_entry("233.18.158.251", 11, 1),
		v4_entry("233.18.158.251", 11, 2),
	};
	struct mcs_flood_entry out[4];

	assert(mcs_build_flood(mdb, 2, out, 4) == 1);
	assert(out[0].ifcnt == 1);
}

static void test_two_ports(void)
{
	struct mcs_mdb_entry mdb[] = {
		v4_entry("233.18.158.251", 11, 1),
		v4_entry("233.18.158.251", 20, 2),
	};
	struct mcs_flood_entry out[4];

	assert(mcs_build_flood(mdb, 2, out, 4) == 1);
	assert(out[0].ifcnt == 2);
	assert(out[0].ifindex[0] == 11);
	assert(out[0].ifindex[1] == 20);
}

static void test_two_groups(void)
{
	struct mcs_mdb_entry mdb[] = {
		v4_entry("233.18.158.251", 11, 1),
		v4_entry("233.18.158.250", 11, 1),
		v4_entry("233.18.158.251", 12, 2),
	};
	struct mcs_flood_entry out[4];

	assert(mcs_build_flood(mdb, 3, out, 4) == 2);
	assert(out[0].group.u.ip4 == mdb[0].group.u.ip4);
	assert(out[0].ifcnt == 2);
	assert(out[1].group.u.ip4 == mdb[1].group.u.ip4);
	assert(out[1].ifcnt == 1);
}

static void test_ipv4_ignores_tail_bytes(void)
{
	struct mcs_mdb_entry mdb[] = {
		v4_entry("233.18.158.251", 11, 1),
		v4_entry("233.18.158.251", 12, 2),
	};
	struct mcs_flood_entry out[4];

	memset(&mdb[1].group.u.ip6[4], 0xa5, 12);
	assert(mcs_build_flood(mdb, 2, out, 4) == 1);
	assert(out[0].ifcnt == 2);
}

static void test_ipv6_groups(void)
{
	struct mcs_mdb_entry mdb[] = {
		v6_entry("ff3e::1234", 11),
		v6_entry("ff3e::1235", 11),
		v6_entry("ff3e::1234", 12),
	};
	struct mcs_flood_entry out[4];

	assert(mcs_build_flood(mdb, 3, out, 4) == 2);
	assert(out[0].ifcnt == 2);
	assert(out[1].ifcnt == 1);
	assert(!memcmp(out[1].group.u.ip6, mdb[1].group.u.ip6, 16));
}

static void test_skips_ifindex_zero(void)
{
	struct mcs_mdb_entry mdb[] = { v4_entry("233.18.158.251", 0, 1) };
	struct mcs_flood_entry out[4];

	assert(mcs_build_flood(mdb, 1, out, 4) == 0);
}

static void test_port_cap(void)
{
	struct mcs_mdb_entry mdb[17];
	struct mcs_flood_entry out[4];
	int i;

	for (i = 0; i < 17; i++)
		mdb[i] = v4_entry("233.18.158.251", 100 + i, i);

	assert(mcs_build_flood(mdb, 17, out, 4) == 1);
	assert(out[0].ifcnt == MCS_IF_MAX);
	assert(out[0].ifindex[MCS_IF_MAX - 1] == 115);
}

static void test_out_cap(void)
{
	struct mcs_mdb_entry mdb[] = {
		v4_entry("233.18.158.250", 11, 1),
		v4_entry("233.18.158.251", 11, 1),
		v4_entry("233.18.158.252", 11, 1),
	};
	struct mcs_flood_entry out[3];

	memset(&out[2], 0x5a, sizeof(out[2]));
	assert(mcs_build_flood(mdb, 3, out, 2) == 2);
	assert(out[2].ifcnt == 0x5a5a5a5a);
}

int main(void)
{
	test_sizes();
	test_empty();
	test_single();
	test_same_port_two_members();
	test_two_ports();
	test_two_groups();
	test_ipv4_ignores_tail_bytes();
	test_ipv6_groups();
	test_skips_ifindex_zero();
	test_port_cap();
	test_out_cap();
	printf("test_mcs_table: ok\n");
	return 0;
}
