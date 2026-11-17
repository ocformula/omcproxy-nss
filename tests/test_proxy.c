/*
 * Host unit tests for the proxy: what happens to multicast routes when the
 * group membership on a downlink changes. The querier, the client, the
 * routing table and qca-mcs are replaced by fakes in this file.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <libubox/list.h>

#include "omcproxy.h"
#include "querier.h"
#include "client.h"
#include "mrib.h"
#include "proxy.h"
#include "mcs.h"

#define UPLINK 15
#define DOWNLINK 10

int log_level = 0;

/* What the fakes saw */
static struct querier_user_iface *downlink_user;
static querier_iface_cb *downlink_cb;
static struct mrib_user *uplink_mrib;
static struct querier_iface fake_iface;

static int client_set_calls;
static int flush_calls;
static struct mrib_user *flush_user;
static struct in6_addr flush_group;
static uint8_t flush_plen;
static const struct in6_addr *flush_source;

/* Fakes */
int querier_init(struct querier *querier)
{
	INIT_LIST_HEAD(&querier->ifaces);
	return 0;
}

void querier_deinit(__unused struct querier *querier) {}

int querier_attach(struct querier_user_iface *user, struct querier *querier,
		int ifindex, querier_iface_cb *cb)
{
	fake_iface.ifindex = ifindex;
	user->iface = &fake_iface;
	user->user_cb = cb;
	user->user.querier = querier;
	list_add(&user->user.head, &querier->ifaces);
	downlink_user = user;
	downlink_cb = cb;
	return 0;
}

void querier_detach(struct querier_user_iface *user)
{
	list_del(&user->user.head);
}

int client_init(__unused struct client *client, __unused int ifindex) { return 0; }
void client_deinit(__unused struct client *client) {}

int client_set(__unused struct client *client, __unused const struct in6_addr *group,
		__unused bool include, __unused const struct in6_addr sources[], __unused size_t cnt)
{
	++client_set_calls;
	return 0;
}

int mrib_attach_user(struct mrib_user *user, int ifindex, mrib_cb *cb_newsource)
{
	/* The uplink is the one that wants to hear about new sources */
	if (cb_newsource) {
		assert(ifindex == UPLINK);
		uplink_mrib = user;
	}

	return 0;
}

void mrib_detach_user(__unused struct mrib_user *user) {}
int mrib_filter_add(__unused mrib_filter *filter, __unused struct mrib_user *user) { return 0; }

int mrib_flush(struct mrib_user *user, const struct in6_addr *group, uint8_t group_plen,
		const struct in6_addr *source)
{
	++flush_calls;
	flush_user = user;
	flush_group = *group;
	flush_plen = group_plen;
	flush_source = source;
	return 0;
}

bool groups_includes_group(__unused struct groups *groups, __unused const struct in6_addr *addr,
		__unused const struct in6_addr *src, __unused omgp_time_t time)
{
	return false;
}

void mcs_attach(__unused int ifindex) {}
void mcs_detach(__unused int ifindex) {}

/* Tests */
static struct in6_addr mapped(const char *v4)
{
	struct in6_addr addr = IN6ADDR_ANY_INIT;

	addr.s6_addr[10] = addr.s6_addr[11] = 0xff;
	inet_pton(AF_INET, v4, &addr.s6_addr[12]);
	return addr;
}

static void setup(void)
{
	static const int downlinks[] = { DOWNLINK };

	client_set_calls = flush_calls = 0;
	downlink_user = NULL;
	uplink_mrib = NULL;
	assert(!proxy_set(UPLINK, downlinks, 1, 0));
	assert(downlink_cb && uplink_mrib);
}

static void teardown(void)
{
	proxy_update(true);
	proxy_flush();
}

/*
 * A stream can reach us before the first membership report, e.g. right after
 * a restart. Its route is then installed without the downlink, and has to be
 * dropped when the membership shows up so that the stream gets re-evaluated.
 */
static void test_membership_change_flushes_routes(void)
{
	struct in6_addr group = mapped("233.18.158.251");

	setup();
	downlink_cb(downlink_user, &group, false, NULL, 0);

	assert(client_set_calls == 1);
	assert(flush_calls == 1);
	assert(flush_user == uplink_mrib);
	assert(IN6_ARE_ADDR_EQUAL(&flush_group, &group));
	assert(flush_plen == 128);
	assert(flush_source == NULL);
	teardown();
}

static void test_out_of_scope_group_is_ignored(void)
{
	struct in6_addr group = mapped("239.255.255.250");

	setup();
	downlink_cb(downlink_user, &group, false, NULL, 0);

	assert(client_set_calls == 0);
	assert(flush_calls == 0);
	teardown();
}

int main(void)
{
	test_membership_change_flushes_routes();
	test_out_of_scope_group_is_ignored();
	printf("test_proxy: ok\n");
	return 0;
}
