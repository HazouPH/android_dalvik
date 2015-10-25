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

#ifndef CRASH_HANDLER_BUF_H_
#define CRASH_HANDLER_BUF_H_

#include <sys/types.h>

/**
 * @class ts_buf
 * @brief This structure holds a pointer to a data which will contain additional information about current DVM state.
 */
struct ts_buf {
    /** @brief used to validate the buffer setted up correctly */
    unsigned long magic;

    /** @brief size of the allocated buffer */
    unsigned long size;

    /** @brief current position in the buffer to write */
    unsigned long offset;

    /** @brief pointer to the buffer content */
    char data[];
};

/*
 * @brief Get a pointer to a dump buffer
 * @details On the first call this fuction will allocate mem
 * and store the pointer to it on stack.
 * @return returns pointer to an allocated buffer
 */
ts_buf *getDumpBuff(void);

/*
 * @brief Writes the C string pointed by format to the buffer
 * @param buff pointer to the structure that describes buffer
 * @param format the C string specifying how to interpret the data passed in additional arguments
 * @param ... additional arguments
 * @return returns number of characters written successful or 0 if an error occured
 */
void writeDebugMessage(ts_buf *buff, const char *format, ...);

/*
 * @brief Copy pointed bytes to a tail of buffer
 * @param buff pointer to the structure that describes buffer
 * @param msg pointer to the C string to write to the buffer
 * @param cnt size of the passed string
 * @return returns number of characters written successful or 0 if an error occured
 */
long writeDebugData(ts_buf *buff, char *msg, long cnt);

/*
 * @brief Copy the data from the dump buffer to the necessary memory block
 * @param pid the ID of the thread to specify the process where buffer is placed
 * @param dest pointer to the memory area where the content is to be copied
 * @param num number of bytes to copy
 * @return returns number of characters was copied
 */
unsigned long readDebugData(pid_t tid, char **dest, unsigned long num);

#endif

