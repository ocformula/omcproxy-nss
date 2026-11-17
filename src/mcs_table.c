/*
 * Flood table builder for the qca-mcs kernel module.
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

#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include "mcs_table.h"

#define MCS_PRO_IPV4 htons(0x0800)

/* Groups are passed by value: they live inside packed wire structures */
static bool mcs_group_equal(struct mcs_group a, struct mcs_group b)
{
	if (a.pro != b.pro)
		return false;

	/* Only the first four address bytes are meaningful for IPv4 */
	if (a.pro == MCS_PRO_IPV4)
		return a.u.ip4 == b.u.ip4;

	return !memcmp(a.u.ip6, b.u.ip6, sizeof(a.u.ip6));
}

size_t mcs_build_flood(const struct mcs_mdb_entry *mdb, size_t mdb_cnt,
		struct mcs_flood_entry *out, size_t out_max)
{
	size_t out_cnt = 0;

	for (size_t i = 0; i < mdb_cnt; ++i) {
		struct mcs_flood_entry *fe = NULL;
		uint32_t port = mdb[i].ifindex;
		uint32_t p;

		if (!port)
			continue;

		for (size_t j = 0; j < out_cnt; ++j) {
			if (mcs_group_equal(out[j].group, mdb[i].group)) {
				fe = &out[j];
				break;
			}
		}

		if (!fe) {
			if (out_cnt >= out_max)
				continue;

			fe = &out[out_cnt++];
			memset(fe, 0, sizeof(*fe));
			fe->group = mdb[i].group;
		}

		for (p = 0; p < fe->ifcnt && fe->ifindex[p] != port; ++p);
		if (p == fe->ifcnt && fe->ifcnt < MCS_IF_MAX)
			fe->ifindex[fe->ifcnt++] = port;
	}

	return out_cnt;
}
