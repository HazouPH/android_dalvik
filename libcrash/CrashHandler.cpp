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

#include "Dalvik.h"
#include "DbgBuff.h"
#include "HeapInfo.h"
#include "CompilerInfo.h"
#include "ThreadInfo.h"

/* List of supported signals */
#define FATAL_SIGNALS {SIGSEGV, SIGILL, SIGABRT, SIGBUS, SIGIOT, SIGFPE, SIGPIPE}

/*
 * @brief VM signal handler
 * @details Signal handler allows to collect an additional information
 * about current DVM state after crash is happend. After collecting info
 * function will raised signal to the saved signal handler.
 * @param signum signal number
 * @param info signal info
 * @param ctx saved context of the crashed thread
 */
static void handleDvmCrash(int signum, siginfo_t *info, void *ctx)
{
    int fatalSignalsList[] = FATAL_SIGNALS;

    ts_buf *buff = getDumpBuff();

    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(info);

    /* Return old signal handler to avoid reenterability */
    for (int i = 0; i < NELEM(fatalSignalsList); i++) {
        sigaction(fatalSignalsList[i], &gDvm.gOldSigAction, NULL);
    }

    Thread *currentThread = dvmThreadSelf();

    /*
     * TODO
     * the method to stop all threads except current one to decrease possible
     * consequences should be called here. But for now we don't know reliable
     * mechanism to do it.
     */

    writeDebugMessage(buff, "\nJava frames:\n");
    dvmDumpThreadStack(buff, currentThread);

    writeDebugMessage(buff, "\nThreads:\n");
    dvmDumpThreadList(buff);

    writeDebugMessage(buff, "\nHeap information:\n");
    dvmDumpHeapInfo(buff);

    writeDebugMessage(buff, "\nCompiler information:\n");
    dvmDumpCompilerInfo(buff);

    kill(getpid(), signum);
}

extern "C" void configureSignalsHandler(void)
{
    int fatalSignalsList[] = FATAL_SIGNALS;

    ts_buf *buff = getDumpBuff();

    if (buff == 0) {
        return;
    }

    static char stack[SIGSTKSZ];
    stack_t ss;
    ss.ss_size = SIGSTKSZ;
    ss.ss_sp = stack;
    ss.ss_flags = 0;

    struct sigaction sa;
    sa.sa_sigaction = handleDvmCrash;
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;

    sigaltstack(&ss, 0);

    /* Save old signals handler function */
    sigaction(fatalSignalsList[0], NULL, &gDvm.gOldSigAction);

    /* Install our signals handler */
    for (int i = 0; i < NELEM(fatalSignalsList); i++) {
        sigaction(fatalSignalsList[i], &sa, NULL);
    }
}

