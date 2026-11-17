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
#define UPLINK2 16
#define DOWNLINK 10

int log_level = 0;

/* Same layout as struct proxy_downlink in proxy.c */
struct proxy_downlink_view {
	struct querier_user_iface iface;
	struct mrib_user mrib;
	struct client client;
	enum proxy_flags flags;
};

/* What the fakes saw */
static struct querier_user_iface *downlink_user;
static querier_iface_cb *downlink_cb;
static struct mrib_user *uplink_mrib;
static struct querier_iface fake_iface;
static enum proxy_flags flags_seen_by_cb;
static int querier_attach_calls, querier_detach_calls;

static int client_set_calls;
static int refresh_calls;
static struct mrib_user *refresh_user;
static struct in6_addr refresh_group;

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
	++querier_attach_calls;

	/* The real querier can report members right away */
	flags_seen_by_cb = container_of(user, struct proxy_downlink_view, iface)->flags;
	return 0;
}

void querier_detach(struct querier_user_iface *user)
{
	list_del(&user->user.head);
	++querier_detach_calls;
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
		assert(ifindex == UPLINK || ifindex == UPLINK2);
		if (ifindex == UPLINK)
			uplink_mrib = user;
	}

	return 0;
}

void mrib_detach_user(__unused struct mrib_user *user) {}
int mrib_filter_add(__unused mrib_filter *filter, __unused struct mrib_user *user) { return 0; }

int mrib_refresh(struct mrib_user *user, const struct in6_addr *group)
{
	++refresh_calls;
	refresh_user = user;
	refresh_group = *group;
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

	client_set_calls = refresh_calls = 0;
	querier_attach_calls = querier_detach_calls = 0;
	flags_seen_by_cb = 0;
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
 * re-evaluated when the membership shows up.
 */
static void test_membership_change_refreshes_routes(void)
{
	struct in6_addr group = mapped("233.18.158.251");

	setup();
	downlink_cb(downlink_user, &group, false, NULL, 0);

	assert(client_set_calls == 1);
	assert(refresh_calls == 1);
	assert(refresh_user == uplink_mrib);
	assert(IN6_ARE_ADDR_EQUAL(&refresh_group, &group));
	teardown();
}

static void test_out_of_scope_group_is_ignored(void)
{
	struct in6_addr group = mapped("239.255.255.250");

	setup();
	downlink_cb(downlink_user, &group, false, NULL, 0);

	assert(client_set_calls == 0);
	assert(refresh_calls == 0);
	teardown();
}

/* E.g. one proxy for IPv4 (wan_premium) and one for IPv6 (wan6) to the LAN */
static void test_second_proxy(void)
{
	static const int downlinks[] = { DOWNLINK };

	setup();
	assert(!proxy_set(UPLINK2, downlinks, 1, 0));
	assert(querier_attach_calls == 2);
	assert(querier_detach_calls == 0);
	teardown();
}

/* Setting a proxy again with the same downlinks keeps them */
static void test_set_same_proxy_again(void)
{
	static const int downlinks[] = { DOWNLINK };

	setup();
	assert(!proxy_set(UPLINK, downlinks, 1, 0));
	assert(querier_attach_calls == 1);
	assert(querier_detach_calls == 0);
	teardown();
}

/* The querier may call back during attach, the scope must be set by then */
static void test_downlink_scope_set_before_attach(void)
{
	setup();
	assert(flags_seen_by_cb == PROXY_GLOBAL);
	teardown();
}

int main(void)
{
	test_membership_change_refreshes_routes();
	test_out_of_scope_group_is_ignored();
	test_second_proxy();
	test_set_same_proxy_again();
	test_downlink_scope_set_before_attach();
	printf("test_proxy: ok\n");
	return 0;
}
