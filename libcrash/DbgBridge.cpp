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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <cutils/properties.h>

#include "DbgBridge.h"
#include "DbgBuff.h"

#define MAX_SIZE_PROPERTY_NAME "system.debug.data.size"
#define MAX_PRINT_STR_SIZE 1024

/*
 * @brief Writes the formatted string to tombstone file
 * @details formatted string is printed to limited size buffer by vsnprintf(...)
 * Please, refer to documentation for this method for detailed format description.
 * While internal buffer has limited size output message may be truncated.
 * @param log pointer to the structure that describes logger
 * @param inTombstoneOnly true if information should be duplicated to stdout
 * @param fmt format of message
 */
static void logToTombstone(log_t *log, bool inTombstoneOnly, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    /* Formatting output string according required format.
     * The output string size is limited by buffer size.
     * Than formatted string is stored to tombstone file.
     */
    if (log != 0) {
        if (log->tfd >= 0) {
            char buff[MAX_PRINT_STR_SIZE];
            memset(buff, 0, sizeof(buff));
            int len = vsnprintf(buff, sizeof(buff), fmt, ap);
            if (len > 0) {
                write(log->tfd, buff, len);
                fsync(log->tfd);
            }
        }

        if (inTombstoneOnly == false && log->quiet == false) {
            vprintf(fmt, ap);
        }
    }

    va_end(ap);
}

/*
 * @brief Writes the provided data to tombstone file 'as is'
 * @param log pointer to the structure that describes logger
 * @param data pointer to the data
 * @param len size of data block to write
 */
static void logToTombstoneRaw(log_t *log, char *data, int len)
{
    if (log != 0 && log->tfd >= 0 && data != 0 && len > 0) {
        write(log->tfd, data, len);
        fsync(log->tfd);
    }
}

/*
 * @brief read system property and return value as maximum size of output data
 * @return maximum size of output data or ULONG_MAX if no limits defined
 */
static unsigned long getSystemAllowedSize(void)
{
    /* Used to get global properties */
    char propertyBuffer[PROPERTY_VALUE_MAX];
    memset(propertyBuffer, 0, sizeof(propertyBuffer));

    property_get(MAX_SIZE_PROPERTY_NAME, propertyBuffer, 0);

    if (propertyBuffer[0] != 0) {
        long maxVal = strtol(propertyBuffer, 0, 0);
        if (maxVal > 0) {
             return maxVal;
        }
    }

    return ULONG_MAX; // unlimited
}

/*
 * @brief Writes the data from buffer to tombstone file
 * @param log pointer to the structure that describes logger
 * @param tid thread id
 * @param atFault
 */
static void dumpBuffer(log_t *log, pid_t tid, bool atFault)
{
    // size of stored data can be limited by system property
    unsigned long maxAllowedSize = getSystemAllowedSize();
    char *buff = 0;
    unsigned long readCount;

    if (maxAllowedSize != ULONG_MAX) {
        logToTombstone(log, atFault == false, "process's specific data might be truncated according to system settings\n");
    }

    readCount = readDebugData(tid, &buff, maxAllowedSize);

    if (buff == 0) {
        logToTombstone(log, atFault == false, "process does not support collection of specific data for tombstones\n");
        return;
    }

    if (readCount == 0) {
        logToTombstone(log, atFault == false, "process does not provide any specific data to store in tombstones\n");
        return;
    }

    logToTombstoneRaw(log, buff, readCount);

    free(buff), buff = 0;
}

/*
 * @brief Writes the thread information to tombstone file
 * @param log pointer to the structure that describes logger
 * @param tid thread id
 * @param atFault
 */
static void dumpThreadInfo(log_t* log, pid_t tid, bool atFault)
{
    char path[64];
    char threadnamebuf[MAX_PRINT_STR_SIZE];
    char *threadname = 0;
    FILE *fp;

    snprintf(path, sizeof(path), "/proc/%d/comm", tid);
    if ((fp = fopen(path, "r"))) {
        threadname = fgets(threadnamebuf, sizeof(threadnamebuf), fp);
        fclose(fp), fp = 0;
        if (threadname != 0) {
            size_t len = strlen(threadname);
            if (len != 0 && threadname[len - 1] == '\n') {
                threadname[len - 1] = '\0';
            }
        }
    }
    logToTombstone(log, atFault == false, "processing specific data for tid = %d, threadname = %s\n", tid, threadname);
}

extern "C" void dump_ps_data(log_t *log, pid_t tid, bool atFault)
{
    dumpThreadInfo(log, tid, atFault);
    dumpBuffer(log, tid, atFault);
}

