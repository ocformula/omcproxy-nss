/*
 * Support for the qca-mcs multicast snooping kernel module.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>

#include <libubox/list.h>
#include <libubox/uloop.h>

#include "omcproxy.h"
#include "mcs.h"
#include "mcs_nl.h"
#include "mcs_table.h"

/* Interval of the periodic flood table refresh, a safety net for lost events */
#ifndef MCS_REFRESH_MS
#define MCS_REFRESH_MS 5000
#endif

/* Snooping table entries to ask for at first, and at most */
#define MCS_MDB_INITIAL 256
#define MCS_MDB_MAX 4096

/* Maximum number of groups in the flood table */
#define MCS_FLOOD_MAX 256

struct mcs_bridge {
	struct list_head head;
	char name[IFNAMSIZ];
	int ifindex;
	unsigned refs;
	bool configured;
	bool failing;
};

static void mcs_handle_event(struct uloop_fd *fd, unsigned events);
static void mcs_handle_timer(struct uloop_timeout *timeout);

static struct list_head bridges = LIST_HEAD_INIT(bridges);
static struct uloop_fd event_fd = { .fd = -1, .cb = mcs_handle_event };
static struct uloop_timeout timer = { .cb = mcs_handle_timer };
static int req_fd = -1;
static uint32_t req_port, event_port;
static bool active;

static bool mcs_available(void)
{
	return req_fd >= 0;
}

static int mcs_set(const char *bridge, int type, void *data, uint32_t len)
{
	return mcs_nl_request(req_fd, req_port, type, bridge, data, len, NULL, NULL);
}

static int mcs_set_value(const char *bridge, int type, uint32_t val)
{
	struct mcs_param_value param = { .val = val };

	return mcs_set(bridge, type, &param, sizeof(param));
}

/* Report a failure once, not on every retry */
static void mcs_failed(struct mcs_bridge *bridge, const char *what, int ret)
{
	if (!bridge->failing)
		L_WARN("mcs: %s: %s failed (%d)", bridge->name, what, ret);

	bridge->failing = true;
}

/*
 * Enable snooping on a bridge. Groups without members keep being flooded as
 * the bridge would do, and the DSCP of snooped groups is left alone.
 */
static bool mcs_configure(struct mcs_bridge *bridge)
{
	struct mcs_param_retag retag = { .enable = 0, .dscp = 0 };
	int ret;

	if ((ret = mcs_set_value(bridge->name, MCS_MSG_SET_ENABLE, 1))) {
		mcs_failed(bridge, "enabling snooping", ret);
		return false;
	}

	if ((ret = mcs_set_value(bridge->name, MCS_MSG_SET_POLICY, MCS_POLICY_FLOOD)) ||
			(ret = mcs_set(bridge->name, MCS_MSG_SET_RETAG, &retag, sizeof(retag)))) {
		/* The module defaults to dropping and remarking: do not leave it like that */
		mcs_set_value(bridge->name, MCS_MSG_SET_ENABLE, 0);
		mcs_failed(bridge, "configuring snooping", ret);
		return false;
	}

	bridge->configured = true;
	return true;
}

/* Read the snooping table of a bridge; the caller frees *mdb */
static int mcs_read_mdb(struct mcs_bridge *bridge, struct mcs_mdb_entry **mdb, size_t *cnt)
{
	size_t mdb_max = MCS_MDB_INITIAL;
	uint32_t written = 0, needed = 0;
	int ret = -1;

	*mdb = NULL;
	for (int i = 0; i < 2; ++i) {
		struct mcs_mdb_entry *n = realloc(*mdb, mdb_max * sizeof(*n));

		if (!n)
			return -1;

		*mdb = n;
		ret = mcs_nl_request(req_fd, req_port, MCS_MSG_GET_MDB, bridge->name,
				n, mdb_max * sizeof(*n), &written, &needed);
		if (ret != MCS_STATUS_BUFFER_OVERFLOW)
			break;

		mdb_max = needed / sizeof(*n) + 1;
		if (mdb_max > MCS_MDB_MAX)
			mdb_max = MCS_MDB_MAX;
	}

	if (written > mdb_max * sizeof(**mdb))
		written = mdb_max * sizeof(**mdb);

	*cnt = written / sizeof(**mdb);
	return ret;
}

/*
 * Copy the snooping table of a bridge into its flood table.
 *
 * Once snooping is enabled the module forwards a snooped group to the ports
 * of its flood table entry only, and not at all if it has none. So every
 * group it knows gets an entry listing the ports that have a member.
 */
static void mcs_refresh(struct mcs_bridge *bridge, const char *reason)
{
	static struct mcs_flood_entry flood[MCS_FLOOD_MAX];
	struct mcs_mdb_entry *mdb = NULL;
	size_t cnt = 0, groups;
	int ret;

	for (int attempt = 0; ; ++attempt) {
		if (!bridge->configured && !mcs_configure(bridge))
			return;

		if (!(ret = mcs_read_mdb(bridge, &mdb, &cnt)))
			break;

		free(mdb);

		/*
		 * The module refuses when snooping is no longer enabled, e.g.
		 * the bridge was recreated: configure it again and retry once.
		 */
		if (ret > 0 && ret != MCS_STATUS_BUFFER_OVERFLOW)
			bridge->configured = false;

		if (attempt || bridge->configured) {
			mcs_failed(bridge, "reading the snooping table", ret);
			return;
		}
	}

	groups = mcs_build_flood(mdb, cnt, flood, MCS_FLOOD_MAX);
	free(mdb);

	if ((ret = mcs_set(bridge->name, MCS_MSG_SET_PSW_FLOOD, flood, groups * sizeof(*flood)))) {
		mcs_failed(bridge, "writing the flood table", ret);
		return;
	}

	bridge->failing = false;
	L_INFO("mcs: %s: %zu groups (%s)", bridge->name, groups, reason);
}

static struct mcs_bridge* mcs_find(int ifindex, const char *name)
{
	struct mcs_bridge *bridge;

	list_for_each_entry(bridge, &bridges, head)
		if (name ? !strcmp(bridge->name, name) : bridge->ifindex == ifindex)
			return bridge;

	return NULL;
}

static void mcs_handle_event(struct uloop_fd *fd, __unused unsigned events)
{
	char name[IFNAMSIZ];

	while (mcs_nl_read_event(fd->fd, name) > 0) {
		struct mcs_bridge *bridge = mcs_find(0, name);

		if (bridge)
			mcs_refresh(bridge, "event");
	}
}

/*
 * The module sends its update events to a single, global netlink port.
 * Claim it, and keep claiming it in case another process took it over.
 */
static void mcs_register(void)
{
	if (event_fd.fd >= 0)
		mcs_set_value("", MCS_MSG_SET_EVENT_PID, event_port);
}

static void mcs_handle_timer(struct uloop_timeout *timeout)
{
	struct mcs_bridge *bridge;

	mcs_register();
	list_for_each_entry(bridge, &bridges, head)
		mcs_refresh(bridge, "timer");

	uloop_timeout_set(timeout, MCS_REFRESH_MS);
}

/* The first bridge is about to be driven: start listening and refreshing */
static void mcs_start(void)
{
	active = true;
	mcs_register();
	uloop_timeout_set(&timer, MCS_REFRESH_MS);
}

/* The last bridge is gone: leave the module alone again */
static void mcs_stop(void)
{
	if (!active)
		return;

	active = false;
	uloop_timeout_cancel(&timer);
	if (event_fd.fd >= 0)
		mcs_set_value("", MCS_MSG_SET_EVENT_PID, MCS_INVALID_PID);
}

static void mcs_release(struct mcs_bridge *bridge)
{
	mcs_set_value(bridge->name, MCS_MSG_SET_ENABLE, 0);
	list_del(&bridge->head);
	free(bridge);

	if (list_empty(&bridges))
		mcs_stop();
}

void mcs_init(void)
{
	/* Only connect here: module state is not touched before a bridge is attached */
	if ((req_fd = mcs_nl_open(&req_port)) < 0) {
		L_INFO("mcs: qca-mcs is not available");
		return;
	}

	if ((event_fd.fd = mcs_nl_open(&event_port)) >= 0)
		uloop_fd_add(&event_fd, ULOOP_READ);
	else
		L_WARN("mcs: no event socket, relying on periodic refresh");
}

void mcs_deinit(void)
{
	struct mcs_bridge *bridge, *n;

	if (!mcs_available())
		return;

	list_for_each_entry_safe(bridge, n, &bridges, head)
		mcs_release(bridge);

	if (event_fd.fd >= 0) {
		uloop_fd_delete(&event_fd);
		mcs_nl_close(event_fd.fd);
		event_fd.fd = -1;
	}

	mcs_nl_close(req_fd);
	req_fd = -1;
}

void mcs_attach(int ifindex)
{
	struct mcs_bridge *bridge;
	char name[IFNAMSIZ];

	if (!mcs_available() || !mcs_nl_bridge_name(ifindex, name))
		return;

	if (!(bridge = mcs_find(ifindex, NULL))) {
		if (!(bridge = calloc(1, sizeof(*bridge))))
			return;

		if (list_empty(&bridges))
			mcs_start();

		bridge->ifindex = ifindex;
		strncpy(bridge->name, name, sizeof(bridge->name) - 1);
		list_add_tail(&bridge->head, &bridges);
	}

	++bridge->refs;

	/*
	 * Always (re)configure: snooping may still be enabled with a stale
	 * table if a previous instance did not get to clean up.
	 */
	bridge->configured = false;
	mcs_refresh(bridge, "attach");
}

void mcs_detach(int ifindex)
{
	struct mcs_bridge *bridge;

	if (!mcs_available() || !(bridge = mcs_find(ifindex, NULL)))
		return;

	if (!--bridge->refs)
		mcs_release(bridge);
}
