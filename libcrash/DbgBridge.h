/*
**
** Copyright 2013, Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
*/

#ifndef CRASH_HANDLER_BRIDGE_H_
#define CRASH_HANDLER_BRIDGE_H_

#include <sys/types.h>
#include "core/debuggerd/utility.h"

/*
 * @brief Dump the Dalvik VM's specific information to the tombstone file
 * @param log pointer to the tombstone file descriptor
 * @param tid thread ID
 * @param atFault
 */
extern "C" void dump_ps_data(log_t* log, pid_t tid, bool atFault);

#endif

