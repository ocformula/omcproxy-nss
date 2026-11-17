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

#ifndef MCS_NL_H_
#define MCS_NL_H_

#include <stdbool.h>
#include <stdint.h>
#include <net/if.h>

/* Message types of the qca-mcs netlink protocol (mc_api.h) */
enum {
	MCS_MSG_SET_ENABLE = 0,
	MCS_MSG_SET_POLICY = 2,
	MCS_MSG_SET_RETAG = 4,
	MCS_MSG_SET_PSW_FLOOD = 14,
	MCS_MSG_SET_EVENT_PID = 15,
	MCS_MSG_GET_MDB = 17,
	MCS_EVENT_MDB_UPDATED = 100,
};

/* Status codes returned by the module */
enum {
	MCS_STATUS_BUFFER_OVERFLOW = 4,
};

#define MCS_POLICY_FLOOD 1
#define MCS_INVALID_PID 0xffffffffu

/* Payload of SET_ENABLE, SET_POLICY and SET_EVENT_PID */
struct mcs_param_value {
	uint32_t val;
};

/* Payload of SET_RETAG */
struct mcs_param_retag {
	uint32_t enable;
	uint32_t dscp;
};

/*
 * Open a socket to the module. Returns the descriptor and stores the netlink
 * port the kernel assigned to it, or returns -1 if the module is not there.
 */
int mcs_nl_open(uint32_t *portid);
void mcs_nl_close(int fd);

/*
 * Send a request for a bridge and wait for the reply. The reply payload is
 * copied back into data. Returns 0 on success, the (positive) status code
 * reported by the module, or -1 on a socket error or timeout.
 */
int mcs_nl_request(int fd, uint32_t portid, int type, const char *bridge,
		void *data, uint32_t len, uint32_t *written, uint32_t *needed);

/*
 * Read one pending message without blocking. Returns 1 and the bridge name
 * for a snooping table update event, 0 if there is nothing (or something
 * else) to read, -1 on error.
 */
int mcs_nl_read_event(int fd, char bridge[IFNAMSIZ]);

/* Returns true and the interface name if ifindex is a bridge device */
bool mcs_nl_bridge_name(int ifindex, char name[IFNAMSIZ]);

#endif /* MCS_NL_H_ */
