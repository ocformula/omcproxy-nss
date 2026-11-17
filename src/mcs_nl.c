/*
 * Netlink transport for the qca-mcs multicast snooping kernel module.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/netlink.h>

#include "mcs_nl.h"

#define NETLINK_QCA_MC 23
#define MCS_REPLY_TIMEOUT_SEC 2
#define MCS_REPLY_TRIES 4

/* struct __mcctl_msg_header: precedes the payload of every message */
struct mcs_msg_hdr {
	char if_name[IFNAMSIZ];
	uint32_t buf_len;
	uint32_t tbl_offsite;
	uint32_t status;
	uint32_t bytes_written;
	uint32_t bytes_needed;
} __attribute__((packed));

int mcs_nl_open(uint32_t *portid)
{
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	struct timeval tv = { .tv_sec = MCS_REPLY_TIMEOUT_SEC };
	socklen_t alen = sizeof(addr);
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_QCA_MC);
	if (fd < 0)
		return -1;

	/* Let the kernel pick the port, the module replies to nlmsg_pid */
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
			getsockname(fd, (struct sockaddr *)&addr, &alen) ||
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv))) {
		close(fd);
		return -1;
	}

	*portid = addr.nl_pid;
	return fd;
}

void mcs_nl_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

int mcs_nl_request(int fd, uint32_t portid, int type, const char *bridge,
		void *data, uint32_t len, uint32_t *written, uint32_t *needed)
{
	struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
	size_t total = NLMSG_SPACE(sizeof(struct mcs_msg_hdr) + len);
	struct nlmsghdr *nlh;
	struct mcs_msg_hdr *hdr;
	int ret = -1;

	if (fd < 0)
		return -1;

	if (!(nlh = calloc(1, total)))
		return -1;

	hdr = NLMSG_DATA(nlh);
	nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*hdr) + len);
	nlh->nlmsg_type = type;
	nlh->nlmsg_pid = portid;
	strncpy(hdr->if_name, bridge, sizeof(hdr->if_name) - 1);
	hdr->buf_len = len;
	if (len)
		memcpy(hdr + 1, data, len);

	if (sendto(fd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&kernel, sizeof(kernel)) < 0)
		goto out;

	/* The module answers by sending the request back with the result */
	for (int i = 0; i < MCS_REPLY_TRIES; ++i) {
		ssize_t n = recv(fd, nlh, total, 0);

		if (n < 0)
			goto out;

		if ((size_t)n < NLMSG_LENGTH(sizeof(*hdr)) || nlh->nlmsg_type != type)
			continue;

		size_t payload = n - NLMSG_LENGTH(sizeof(*hdr));
		if (payload > len)
			payload = len;

		if (payload)
			memcpy(data, hdr + 1, payload);

		if (written)
			*written = hdr->bytes_written;

		if (needed)
			*needed = hdr->bytes_needed;

		ret = hdr->status;
		break;
	}

out:
	free(nlh);
	return ret;
}

int mcs_nl_read_event(int fd, char bridge[IFNAMSIZ])
{
	char buf[NLMSG_SPACE(sizeof(struct mcs_msg_hdr)) + 64];
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	struct mcs_msg_hdr *hdr = NLMSG_DATA(nlh);
	ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);

	if (n < 0)
		return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;

	if ((size_t)n < NLMSG_LENGTH(sizeof(*hdr)) || nlh->nlmsg_type != MCS_EVENT_MDB_UPDATED)
		return 0;

	memcpy(bridge, hdr->if_name, IFNAMSIZ);
	bridge[IFNAMSIZ - 1] = 0;
	return 1;
}

bool mcs_nl_bridge_name(int ifindex, char name[IFNAMSIZ])
{
	char path[64];

	if (ifindex <= 0 || !if_indextoname(ifindex, name))
		return false;

	snprintf(path, sizeof(path), "/sys/class/net/%s/bridge", name);
	return !access(path, F_OK);
}
