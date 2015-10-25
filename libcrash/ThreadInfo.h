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

#ifndef CRASH_HANDLER_THREADINFO_H_
#define CRASH_HANDLER_THREADINFO_H_

#include "Dalvik.h"
#include "DbgBuff.h"

/*
 * @brief Dump the thread stack info into the specified buffer
 * @param buff pointer to the structure that describes output buffer
 * @param thread pointer to the thread object
 */
void dvmDumpThreadStack(ts_buf *buff, Thread *thread);

/*
 * @brief Dump information about all the threads were rinning in a process
 * @param buff pointer to the structure that describes output buffer
 */
void dvmDumpThreadList(ts_buf *buff);

#endif

