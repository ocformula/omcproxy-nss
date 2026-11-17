/*
 * Wire structures shared with the qca-mcs kernel module (mc_api.h) and the
 * flood table builder.
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

#ifndef MCS_TABLE_H_
#define MCS_TABLE_H_

#include <stddef.h>
#include <stdint.h>

/* Maximum number of ports per flood table entry (MC_DEF_IF_MAX) */
#define MCS_IF_MAX 16

/* struct __mc_group: pro holds the ethertype in network byte order */
struct mcs_group {
	uint32_t pro;
	union {
		uint32_t ip4;
		uint8_t ip6[16];
	} u;
};

/* struct __mc_mdb_entry: one snooped member of a group on a bridge port */
struct mcs_mdb_entry {
	struct mcs_group group;
	uint32_t ifindex;
	uint32_t nsrcs;
	uint8_t srcs[4 * 16];
	uint32_t aging;
	uint8_t filter_mode;
	uint8_t fdb_age_out;
	uint8_t mac[6];
} __attribute__((packed));

/* struct __mc_floodtbl_entry: ports a group is forwarded to */
struct mcs_flood_entry {
	struct mcs_group group;
	uint32_t ifcnt;
	uint32_t ifindex[MCS_IF_MAX];
} __attribute__((packed));

/*
 * Collapse snooping table entries into one flood entry per group, listing
 * the distinct bridge ports that have a member of that group.
 * Returns the number of entries written to out (at most out_max).
 */
size_t mcs_build_flood(const struct mcs_mdb_entry *mdb, size_t mdb_cnt,
		struct mcs_flood_entry *out, size_t out_max);

#endif /* MCS_TABLE_H_ */
