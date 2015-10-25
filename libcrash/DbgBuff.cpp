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

#include <string>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <corkscrew/map_info.h>

#include "Dalvik.h"
#include "DbgBuff.h"

/* 40K for output data is a balance between buffer size and provided information */
#define CRASH_BUFFER_SZ 40960
#define CRASH_BUFFER_NAME "dalvik-dump-buffer"
/* ascii symbols 'MAGK' */
#define CRASH_BUFFER_MAGIC 0x4D41474B

/**
 * @class ts_buf
 * @brief Structure holds start and end pointers to an memory area
 */
struct module_addr_t {
    /** @brief module's start address */
    char* start;
    /** @brief module's end address */
    char* end;
};

static void initDumpHeap(ts_buf **buff)
{
    *buff = static_cast<ts_buf *>(dvmAllocRegion(CRASH_BUFFER_SZ,
        PROT_READ | PROT_WRITE, CRASH_BUFFER_NAME));

    if (*buff == 0) {
        ALOGE("Unable to attach shared buffer");
        return;
    }

    (*buff)->magic = CRASH_BUFFER_MAGIC;
    (*buff)->size = CRASH_BUFFER_SZ - sizeof(ts_buf);
    (*buff)->offset = 0;
}

ts_buf *getDumpBuff(void)
{
    static ts_buf *buff = 0;

    if (buff == 0) {
        initDumpHeap(&buff);
    }

    return buff;
}


void writeDebugMessage(ts_buf *buff, const char *format, ...)
{
    int bytesWritten;

    if (buff == 0) {
        return;
    }

    va_list args;
    va_start(args, format);

    bytesWritten = vsnprintf(buff->data + buff->offset, buff->size - buff->offset, format, args);

    if (bytesWritten > 0) {
        buff->offset += bytesWritten;
    }

    va_end(args);
}

long writeDebugData(ts_buf *buff, char *msg, long cnt)
{
    if (buff == 0 || buff->offset > buff->size || msg == 0 || cnt <= 0) {
        return 0;
    }

    if (buff->offset + cnt >= buff->size) {
        cnt = buff->size - buff->offset;
    }

    memcpy(buff->data + buff->offset, msg, cnt);

    buff->offset += cnt;

    return cnt;
}

/*
 * @brief find start/end addresses of module with specified name
 * @param memList list of modules in the memory
 * @param name module name to find
 * @param addr structure to hold start/end addresses
 * @return address of module's start or NULL if no module in memory
 */
static char *findSharedMemByName(map_info_t *memList, char *name, module_addr_t *addr)
{
    if (memList == 0 || name == 0) {
        return 0;
    }

    while (memList != 0) {
        if (memList->name != 0
            && strlen(memList->name) >= strlen(name)
            && strstr(memList->name, name) != 0)
        {
            addr->start = reinterpret_cast<char *>(memList->start);
            addr->end   = reinterpret_cast<char *>(memList->end);
            return reinterpret_cast<char *>(memList->start);
        }
        memList = memList->next;
    }

    return 0;
}

/*
 * @brief find the address of buffer with crash information in process address space
 * @param tid thread id
 * @param size pointer to store buffer's size according to mapping information
 * @return pointer to the buffer or NULL if no buffer found
 */
static void *getDumpBuffPtrByTid(pid_t tid, long *size)
{
    map_info_t *memList;
    void *buffPtr;
    module_addr_t addr;

    addr.start = 0;
    addr.end = 0;

    /* Find the pointer to memory area allocated by the crashed process */
    memList = load_map_info_list(tid);
    buffPtr = findSharedMemByName(memList, (char *)CRASH_BUFFER_NAME, &addr);

    if (buffPtr == 0) {
        return 0;
    }

    *size = addr.end - addr.start;

    if (*size <= 0) {
        return 0;
    }

    /* Check the magic number */
    unsigned long magic = ptrace(PTRACE_PEEKTEXT, tid, (void *)(buffPtr), 0);

    if (magic != CRASH_BUFFER_MAGIC) {
        return 0;
    }

    free_map_info_list(memList), memList = 0;

    return buffPtr;
}

unsigned long readDebugData(pid_t tid, char **out_dest, unsigned long num)
{
    ts_buf buff;
    long memSize = 0;
    char *buffPtr;

    if (num == 0) {
        return 0;
    }

    char *dest = 0;

    buffPtr = reinterpret_cast<char *>(getDumpBuffPtrByTid(tid, &memSize));

    if (buffPtr == 0) {
        return 0;
    }

    /* Read and set all the fields of the ts_buf struct */
    buff.magic = ptrace(PTRACE_PEEKTEXT, tid, (void *)(buffPtr), 0);
    buff.size = ptrace(PTRACE_PEEKTEXT, tid, (void *)(buffPtr + 4), 0);
    buff.offset = ptrace(PTRACE_PEEKTEXT, tid, (void *)(buffPtr + 8), 0);

    if (buff.offset > buff.size ||
        buff.size > (unsigned long)memSize)
    {
        return 0;
    }

    unsigned long cnt = num & 0xfffffffc; //align it to 4 bytes

    if (buff.offset < cnt) {
        cnt = buff.offset & 0xfffffffc;
    }

    /* Allocate the output buffer */
    dest = static_cast<char *>(malloc(cnt));

    if (dest == 0) {
        return 0;
    }

    memset(dest, 0, cnt);

    long *out = reinterpret_cast<long *>(dest);
    long *endPtr = out + (cnt / sizeof(long));
    char *targetDataPtr = buffPtr + 12;

    /* Copy data from the buffer to the dest using ptrace */
    while (out < endPtr) {
        errno = 0;

        long data = ptrace(PTRACE_PEEKTEXT, tid, targetDataPtr, 0);

        if (errno != 0) {
            free(dest), dest = 0;
            return 0;
        }

        *out = data;
        targetDataPtr += sizeof(long);
        out++;
    }
    *out_dest = dest;

    return cnt;
}

