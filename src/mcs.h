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

#ifndef MCS_H_
#define MCS_H_

/*
 * On Qualcomm NSS platforms the ECM asks qca-mcs for the bridge ports of a
 * multicast group before it offloads a routed multicast flow to a bridge.
 * qca-mcs only answers for bridges it has been enabled on, and only with the
 * ports user space lists in its flood table. These functions enable it on
 * bridged downlinks and keep that table in sync with its snooping table.
 *
 * Everything here is a no-op if the module is not loaded.
 */

/* Connect to the module; call after uloop_init() */
void mcs_init(void);

/* Disable snooping on all attached bridges and disconnect */
void mcs_deinit(void);

/* A downlink was added: start driving the module if it is a bridge */
void mcs_attach(int ifindex);

/* A downlink was removed */
void mcs_detach(int ifindex);

#endif /* MCS_H_ */
