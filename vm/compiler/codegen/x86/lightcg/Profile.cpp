/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Dalvik.h"
#include "libdex/DexOpcodes.h"

#include "compiler/CompilerInternals.h"
#include <sys/mman.h>           /* for protection change */
#include "Profile.h"
#include "Singleton.h"
#include "Scheduler.h"

#if defined(WITH_JIT_TPROFILE)
/*
 * Translation layout in the code cache.  Note that the codeAddress pointer
 * in JitTable will point directly to the code body (field codeAddress).  The
 * chain cell offset codeAddress - 4, the address of the trace profile counter
 * is at codeAddress - 8, and the loop counter address is codeAddress - 12.
 *
 *      +----------------------------+
 *      | Trace Loop Counter addr    |  -> 4 bytes (EXTRA_BYTES_FOR_LOOP_COUNT_ADDR)
 *      +----------------------------+
 *      | Trace Profile Counter addr |  -> 4 bytes (EXTRA_BYTES_FOR_PROF_ADDR)
 *      +----------------------------+
 *   +--| Offset to chain cell counts|  -> 2 bytes (CHAIN_CELL_COUNT_OFFSET)
 *   |  +----------------------------+
 *   |  | Offset to chain cell       |  -> 2 bytes (CHAIN_CELL_OFFSET)
 *   |  +----------------------------+
 *   |  | Trace profile code         |  <- entry point when profiling (16 bytes)
 *   |  .  -   -   -   -   -   -   - .
 *   |  | Code body                  |  <- entry point when not profiling
 *   |  .                            .
 *   |  |                            |
 *   |  +----------------------------+
 *   |  | Chaining Cells             |  -> 16/20 bytes, 4 byte aligned
 *   |  .                            .
 *   |  .                            .
 *   |  |                            |
 *   |  +----------------------------+
 *   |  | Gap for large switch stmt  |  -> # cases >= MAX_CHAINED_SWITCH_CASES
 *   |  +----------------------------+
 *   +->| Chaining cell counts       |  -> 8 bytes, chain cell counts by type
 *      +----------------------------+
 *      | Trace description          |  -> variable sized
 *      .                            .
 *      |                            |
 *      +----------------------------+
 *      | # Class pointer pool size  |  -> 4 bytes
 *      +----------------------------+
 *      | Class pointer pool         |  -> 4-byte aligned, variable size
 *      .                            .
 *      .                            .
 *      |                            |
 *      +----------------------------+
 *      | Literal pool               |  -> 4-byte aligned, variable size
 *      .                            .
 *      .                            .
 *      |                            |
 *      +----------------------------+
 *
 */

/**
 * @brief A map between bytecode offset and source code line number
 */
typedef struct jitProfileAddrToLine {
    u4 lineNum;              /**< @brief The source code line number */
    u4 bytecodeOffset;       /**< @brief The bytecode offset */
} jitProfileAddrToLine;

/**
 * @brief Get the loop counter's address
 * @param p The JitEntry of the trace
 * @return The address of the loop counter
 */
static inline char *getLoopCounterBase(const JitEntry *p)
{
    return (char*)p->codeAddress -
        (EXTRA_BYTES_FOR_PROF_ADDR + EXTRA_BYTES_FOR_CHAINING + EXTRA_BYTES_FOR_LOOP_COUNT_ADDR);
}

/**
 * @brief Get the trace counter's address
 * @param p The JitEntry of the trace
 * @return The address of the trace counter
 */
static inline char *getTraceCounterBase(const JitEntry *p)
{
    return (char*)p->codeAddress -
        (EXTRA_BYTES_FOR_PROF_ADDR + EXTRA_BYTES_FOR_CHAINING);
}

/**
 * @brief Check the trace's loop info
 * @param entry The JitEntry of the trace
 * @return 0 for non-loop, -1 for nested loop, otherwise non-nested loop
 */
static inline int checkLoopInfo(const JitEntry *entry)
{
    if (entry->dPC == 0 || entry->codeAddress == 0) {
        return 0;
    }

    JitTraceCounter_t **addr = (JitTraceCounter_t **) getLoopCounterBase(entry);
    return (int) *addr;
}

/**
 * @brief Retrieve the profile loop count for a loop trace
 * @param entry The JitEntry of the trace
 * @return The loop count value
 */
static inline JitTraceCounter_t getProfileLoopCount(const JitEntry *entry)
{
    if (entry->dPC == 0 || entry->codeAddress == 0) {
        return 0;
    }

    JitTraceCounter_t **p = (JitTraceCounter_t **) getLoopCounterBase(entry);

    return **p;
}

/**
 * @brief Callback function to track the bytecode offset/line number relationiship
 * @param cnxt A point of jitProfileAddrToLine
 * @param bytecodeOffset The offset of the bytecode
 * @param lineNum The line number
 * @return 0 for success
 */
static int addrToLineCb (void *cnxt, u4 bytecodeOffset, u4 lineNum)
{
    jitProfileAddrToLine *addrToLine = (jitProfileAddrToLine *) cnxt;

    /* Best match so far for this offset */
    if (addrToLine->bytecodeOffset >= bytecodeOffset) {
        addrToLine->lineNum = lineNum;
    }
    return 0;
}

/**
 * @brief Reset the trace profile count
 * @param entry The JitEntry of the trace
 */
static inline void resetProfileCount(const JitEntry *entry)
{
    if (entry->dPC == 0 || entry->codeAddress == 0) {
        return;
    }

    JitTraceCounter_t **p = (JitTraceCounter_t **) getTraceCounterBase(entry);

    **p = 0;
}

/**
 * @brief Get the pointer of the chain cell count
 * @param base The pointer point to trace counter
 * @return The pointer of Chain Cell Counts
 */
static inline ChainCellCounts* getChainCellCountsPointer(const char *base)
{
    /* 4 is the size of the profile count */
    u2 *chainCellOffsetP = (u2 *) (base + EXTRA_BYTES_FOR_PROF_ADDR);
    u2 chainCellOffset = *chainCellOffsetP;
    return (ChainCellCounts *) ((char *) chainCellOffsetP + chainCellOffset + EXTRA_BYTES_FOR_CHAINING);
}

/**
 * @brief Get the starting pointer of the trace description section
 * @param base The pointer point to trace counter
 * @return The pointer of Trace Description
 */
static JitTraceDescription* getTraceDescriptionPointer(const char *base)
{
    ChainCellCounts* pCellCounts = getChainCellCountsPointer(base);
    return (JitTraceDescription*) ((char*)pCellCounts + sizeof(*pCellCounts));
}

/**
 * @brief Retrieve the trace profile count
 * @param entry The JitEntry of the trace
 * @return The trace profile count
 */
static inline JitTraceCounter_t getProfileCount(const JitEntry *entry)
{
    if (entry->dPC == 0 || entry->codeAddress == 0) {
        return 0;
    }

    JitTraceCounter_t **p = (JitTraceCounter_t **) getTraceCounterBase(entry);

    return **p;
}

/**
 * @brief Qsort callback function
 * @param entry1 The JitEntry compared
 * @param entry2 The JitEntry compared
 * @return 0 if count is equal, -1 for entry1's counter greater than entry2's, otherwise return 1
 */
static int sortTraceProfileCount(const void *entry1, const void *entry2)
{
    const JitEntry *jitEntry1 = (const JitEntry *)entry1;
    const JitEntry *jitEntry2 = (const JitEntry *)entry2;

    JitTraceCounter_t count1 = getProfileCount(jitEntry1);
    JitTraceCounter_t count2 = getProfileCount(jitEntry2);

    return (count1 == count2) ? 0 : ((count1 > count2) ? -1 : 1);
}

/**
 * @brief Dumps profile info for a single trace
 * @param p The JitEntry of the trace
 * @param silent Wheter to dump the trace count info
 * @param reset Whether to reset the counter
 * @param sum The total count of all the trace
 * @return The trace count
 */
static int dumpTraceProfile(JitEntry *p, bool silent, bool reset,
                            unsigned long sum)
{
    int idx;

    if (p->codeAddress == 0) {
        if (silent == false) {
            ALOGD("TRACEPROFILE NULL");
        }
        return 0;
    }

    JitTraceCounter_t count = getProfileCount(p);

    if (reset == true) {
        resetProfileCount(p);
    }
    if (silent == true) {
        return count;
    }

    JitTraceDescription *desc = getTraceDescriptionPointer(getTraceCounterBase(p));
    const Method *method = desc->method;
    char *methodDesc = dexProtoCopyMethodDescriptor(&method->prototype);
    jitProfileAddrToLine addrToLine = {0, desc->trace[0].info.frag.startOffset};

    /*
     * We may end up decoding the debug information for the same method
     * multiple times, but the tradeoff is we don't need to allocate extra
     * space to store the addr/line mapping. Since this is a debugging feature
     * and done infrequently so the slower but simpler mechanism should work
     * just fine.
     */
    dexDecodeDebugInfo(method->clazz->pDvmDex->pDexFile,
                       dvmGetMethodCode(method),
                       method->clazz->descriptor,
                       method->prototype.protoIdx,
                       method->accessFlags,
                       addrToLineCb, 0, &addrToLine);

    ALOGD("TRACEPROFILE 0x%08x % 10d %5.2f%% [%#x(+%d), %d] %s%s;%s",
         (int) getTraceCounterBase(p),
         count,
         ((float ) count) / sum * 100.0,
         desc->trace[0].info.frag.startOffset,
         desc->trace[0].info.frag.numInsts,
         addrToLine.lineNum,
         method->clazz->descriptor, method->name, methodDesc);
    free(methodDesc);
    methodDesc = 0;

    if (checkLoopInfo(p) != 0 && checkLoopInfo(p) != -1) {
        ALOGD("++++++++++ Loop Trace, loop executed: %d ++++++++++", (int) getProfileLoopCount(p));
    } else if (checkLoopInfo(p) == -1) {
        ALOGD("++++++++++ Loop Trace with Nested Loop, can't handle the loop counter for this currently ++++++++++");
    }

    /* Find the last fragment (ie runEnd is set) */
    for (idx = 0;
         (desc->trace[idx].isCode == true) && (desc->trace[idx].info.frag.runEnd == false);
         idx++) {
    }

    /*
     * runEnd must comes with a JitCodeDesc frag. If isCode is false it must
     * be a meta info field (only used by callsite info for now).
     */
    if (desc->trace[idx].isCode == false) {
        const Method *method = (const Method *)
            desc->trace[idx+JIT_TRACE_CUR_METHOD-1].info.meta;
        char *methodDesc = dexProtoCopyMethodDescriptor(&method->prototype);
        /* Print the callee info in the trace */
        ALOGD("    -> %s%s;%s", method->clazz->descriptor, method->name,
             methodDesc);
        free(methodDesc);
        methodDesc = 0;
    }
    return count;
}

/**
 * @brief Get the size of a jit trace description
 * @param desc the point of jit trace description we want check
 * @return The size of the jit trace description
 */
int getTraceDescriptionSize(const JitTraceDescription *desc)
{
    int runCount;
    /* Trace end is always of non-meta type (ie isCode == true) */
    for (runCount = 0; ; runCount++) {
        if (desc->trace[runCount].isCode &&
            desc->trace[runCount].info.frag.runEnd)
           break;
    }
    return sizeof(JitTraceDescription) + ((runCount+1) * sizeof(JitTraceRun));
}

/**
 * @brief Generate the loop counter profile code for loop trace
 *   Currently only handle the loop trace without nested loops, so just add code to bump up the loop counter before the loop entry basic block
 *   For loop trace with nested loops, set the loop counter's addr to -1
 * @param cUnit The compilation unit of the trace
 * @param bb The basic block is processing
 * @param bbO1 The processing basic block's BasicBlock_O1 version
 * @return the size (in bytes) of the generated code
 */
int genLoopCounterProfileCode(CompilationUnit *cUnit, BasicBlock_O1 *bbO1)
{
    //If the trace is loop trace without nested loop, and the bb processing is the loop entry basic block,
    //      add loop counter before the trace stream and profile code before the bb
    //else if the trace is loop trace with nested loop, and the bb processing is the loop entry basic block,
    //      set the loop counter to -1, so that we can dump the infomation later
    LoopInformation *info = cUnit->loopInformation;
    if (info != 0 && bbO1->lastMIRInsn != 0 && info->getLoopInformationByEntry(bbO1) != 0) {
        if (info->getLoopInformationByEntry(bbO1) != 0) {
            int nesting = info->getNestedNbr();
            if (nesting == 0) {
                if ((gDvmJit.profileMode == kTraceProfilingContinuous) ||
                    (gDvmJit.profileMode == kTraceProfilingDisabled)) {
                        //Set the loop counter address
                        intptr_t addr = (intptr_t)dvmJitNextTraceCounter();
                        unsigned int *intaddr = reinterpret_cast<unsigned int *>(streamMethodStart
                                    - EXTRA_BYTES_FOR_LOOP_COUNT_ADDR - EXTRA_BYTES_FOR_PROF_ADDR - EXTRA_BYTES_FOR_CHAINING);
                        *intaddr = addr;

                        //Add the code before loop entry basic block to bump up the loop counter, the generated code may looks like (19 bytes):
                        //  LEA -4(ESP), ESP
                        //  MOV EAX, 0(ESP)
                        //  MOV #80049734, EAX
                        //  ADD #1, 0(EAX)
                        //  MOV 0(ESP), EAX
                        //  LEA 4(ESP), ESP
                        load_effective_addr(-4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
                        move_reg_to_mem(OpndSize_32, PhysicalReg_EAX, true, 0, PhysicalReg_ESP, true);
                        move_imm_to_reg(OpndSize_32, (int)addr, PhysicalReg_EAX, true);
                        alu_binary_imm_mem(OpndSize_32, add_opc, 1, 0, PhysicalReg_EAX, true);
                        move_mem_to_reg(OpndSize_32, 0, PhysicalReg_ESP, true, PhysicalReg_EAX, true);
                        load_effective_addr(4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);

                        return 19;
                }
            } else {
                // TODO: we should refine the nested loop handle when nested loop enabled
                // For nested loop, currently we just set the loop counter's addr to -1
                ALOGD("This trace contains nested loops, cann't handle this currently");
                unsigned int *intaddr = reinterpret_cast<unsigned int *>(streamMethodStart
                                    - EXTRA_BYTES_FOR_LOOP_COUNT_ADDR - EXTRA_BYTES_FOR_PROF_ADDR - EXTRA_BYTES_FOR_CHAINING);
                *intaddr = -1;

                return 0;
            }
        }
    }
    return 0;
}
#endif /* WITH_JIT_TPROFILE */

/**
 * @brief Sort the trace profile counts and dump them
 */
void dvmCompilerSortAndPrintTraceProfiles(void)
{
#if defined(WITH_JIT_TPROFILE)
    JitEntry *sortedEntries;
    int numTraces = 0;
    unsigned long sum = 0;
    unsigned int i;

    /* Make sure that the table is not changing */
    dvmLockMutex(&gDvmJit.tableLock);

    /* Sort the entries by descending order */
    sortedEntries = (JitEntry *)alloca(sizeof(JitEntry) * gDvmJit.jitTableSize);
    memcpy(sortedEntries, gDvmJit.pJitEntryTable,
           sizeof(JitEntry) * gDvmJit.jitTableSize);
    qsort(sortedEntries, gDvmJit.jitTableSize, sizeof(JitEntry),
          sortTraceProfileCount);

    /* Dump the sorted entries */
    for (i=0; i < gDvmJit.jitTableSize; i++) {
        if (sortedEntries[i].dPC != 0) {
            sum += dumpTraceProfile(&sortedEntries[i],
                                    true,
                                    false,
                                    0);
            numTraces++;
        }
    }

    if (numTraces == 0) {
        numTraces = 1;
    }
    if (sum == 0) {
        sum = 1;
    }

    ALOGI("JIT: Average execution count -> %d",(int)(sum / numTraces));

    /* Dump the sorted entries. The count of each trace will be reset to 0. */
    for (i=0; i < gDvmJit.jitTableSize; i++) {
        if (sortedEntries[i].dPC != 0) {
                dumpTraceProfile(&sortedEntries[i],
                             false /* silent */,
                             true /* reset */,
                             sum);
        }
    }

done:
    dvmUnlockMutex(&gDvmJit.tableLock);
#endif
    return;
}

/**
 *@brief Generate the trace count profile code before the begin of trace code
 *@details Reserve 12 bytes at the beginning of the trace
 *        +----------------------------+
 *        | loop counter addr (4 bytes)|
 *        +----------------------------+
 *        | prof counter addr (4 bytes)|
 *        +----------------------------+
 *        | chain cell offset (4 bytes)|
 *        +----------------------------+
 *
 * ...and then code to increment the execution
 *
 * For continuous profiling (16 bytes)
 *       MOV   EAX, addr     @ get prof count addr    [5 bytes]
 *       ADD   #1, 0(EAX)    @ increment counter      [6 bytes]
 *       NOPS                                         [5 bytes]
 *
 *@param cUnit the compilation unit
 *@return the size (in bytes) of the generated code.
 */
int genTraceProfileEntry(CompilationUnit *cUnit)
{
#if defined(WITH_JIT_TPROFILE)
    intptr_t addr = (intptr_t)dvmJitNextTraceCounter();
    assert(__BYTE_ORDER == __LITTLE_ENDIAN);
    unsigned int *intaddr = reinterpret_cast<unsigned int *>(streamMethodStart - EXTRA_BYTES_FOR_PROF_ADDR - EXTRA_BYTES_FOR_CHAINING);
    *intaddr = addr;

    cUnit->headerSize = EXTRA_BYTES_FOR_PROF_ADDR + EXTRA_BYTES_FOR_CHAINING + EXTRA_BYTES_FOR_LOOP_COUNT_ADDR;
    if ((gDvmJit.profileMode == kTraceProfilingContinuous) ||
        (gDvmJit.profileMode == kTraceProfilingDisabled)) {
        move_imm_to_reg(OpndSize_32, (int)addr, PhysicalReg_EAX, true);
        alu_binary_imm_mem(OpndSize_32, add_opc, 1, 0, PhysicalReg_EAX, true);
        if(gDvmJit.scheduling == true) {
            singletonPtr<Scheduler>()->signalEndOfNativeBasicBlock();
        }
        /*Add 5 nops to the end to make sure trace can align with 16B*/
        stream = encoder_nops(5, stream);
        return 16;
    }
#endif
    return 0;
}
