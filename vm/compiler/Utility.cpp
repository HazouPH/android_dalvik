/*
 * Copyright (C) 2009 The Android Open Source Project
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
#include "Dataflow.h"
#include "CompilerInternals.h"
#include "Dataflow.h"
#include "Utility.h"
#include <set>

static ArenaMemBlock *arenaHead, *currentArena;
static int numArenaBlocks;

#ifdef ARCH_IA32

#define ARENA_LOG(...)

//A few additional information for the arena: the current average being used
/** @brief The number of blocks per trace as an accumulator */
static unsigned long blocksPerTraceAccum;
/** @brief The number of traces compiled */
static unsigned long traceCounter;

/** @brief Which arena triming style is in use */
static ArenaTrimStyle arenaTrimStyle = ARENA_NONE;

/** @brief If a user defined a trimed value: minimum will always be one to simplify allocation */
static unsigned int arenaTrimUserValue = 1;

void setArenaTrimUserValue (unsigned int value)
{
    arenaTrimUserValue = value;
}

void setArenaTrimStyle (ArenaTrimStyle value)
{
    arenaTrimStyle = value;
}
#endif

/* Allocate the initial memory block for arena-based allocation */
bool dvmCompilerHeapInit(void)
{
    assert(arenaHead == NULL);
    arenaHead =
        (ArenaMemBlock *) malloc(sizeof(ArenaMemBlock) + ARENA_DEFAULT_SIZE);
    if (arenaHead == NULL) {
        ALOGE("No memory left to create compiler heap memory");
        return false;
    }
    arenaHead->blockSize = ARENA_DEFAULT_SIZE;
    currentArena = arenaHead;
    currentArena->bytesAllocated = 0;
    currentArena->next = NULL;
    numArenaBlocks = 1;

    return true;
}

/* Arena-based malloc for compilation tasks */
void * dvmCompilerNew(size_t size, bool zero)
{
    size = (size + 3) & ~3;
retry:
    /* Normal case - space is available in the current page */
    if (size + currentArena->bytesAllocated <= currentArena->blockSize) {
        void *ptr;
        ptr = &currentArena->ptr[currentArena->bytesAllocated];
        currentArena->bytesAllocated += size;
        if (zero) {
            memset(ptr, 0, size);
        }
        return ptr;
    } else {
#ifdef ARCH_IA32
        //Augment the number of blocks used
        blocksPerTraceAccum++;
#endif

        /*
         * See if there are previously allocated arena blocks before the last
         * reset
         */
        if (currentArena->next) {
            currentArena = currentArena->next;
            goto retry;
        }

        size_t blockSize = (size < ARENA_DEFAULT_SIZE) ?
                          ARENA_DEFAULT_SIZE : size;
        /* Time to allocate a new arena */
        ArenaMemBlock *newArena = (ArenaMemBlock *)
            malloc(sizeof(ArenaMemBlock) + blockSize);
        if (newArena == NULL) {
            ALOGE("Arena allocation failure");
            dvmAbort();
        }
        newArena->blockSize = blockSize;
        newArena->bytesAllocated = 0;
        newArena->next = NULL;
        currentArena->next = newArena;
        currentArena = newArena;
        numArenaBlocks++;
        if (numArenaBlocks > 10)
            ALOGI("Total arena pages for JIT: %d", numArenaBlocks);
        goto retry;
    }
    /* Should not reach here */
    dvmAbort();
}

/* Reclaim all the arena blocks allocated so far */
void dvmCompilerArenaReset(void)
{
    ArenaMemBlock *block;

    for (block = arenaHead; block; block = block->next) {
        block->bytesAllocated = 0;
    }
    currentArena = arenaHead;

#ifdef ARCH_IA32
    //In IA32 case, we want to trim a bit the arena after use depending on a style
    int keepHowMany = 0;
    switch (arenaTrimStyle)
    {
        case ARENA_NONE:
            //We are done then
            return;
        case ARENA_ALL_BUT_ONE:
            keepHowMany = 1;
            break;
        case ARENA_AVERAGE:
            //Calculate average first
            //If for some reason traceCounter overflows, let us reset here
            if (traceCounter == 0)
            {
                blocksPerTraceAccum = 1;
                keepHowMany = 1;
            }
            else
            {
                keepHowMany = blocksPerTraceAccum / traceCounter;
                ARENA_LOG ("Arena: calculating the average: %lu / %lu = %d", blocksPerTraceAccum, traceCounter, keepHowMany);
            }

            //Augment this now, a reset is as good a way of measuring the average trip count
            traceCounter++;
            //Blocks Per trace accum goes up at least once, let increment it here.
            //Technically the only reason it wouldn't go to at least 1 is if no dvmCompilerNew was called in between resets but, for the average,
            //we can leave it thinking at least one block was necessary
            blocksPerTraceAccum++;
            break;
        case ARENA_USER_DEFINED:
            keepHowMany = arenaTrimUserValue;
            break;
        default:
            break;
    }

    //Be paranoid on the value: we want at least one
    keepHowMany = (keepHowMany < 1) ? 1 : keepHowMany;

    //Go forward in the link list until we have hit as many elements
    block = arenaHead;

    ArenaMemBlock *last = arenaHead;

    unsigned int cnt = keepHowMany;
    while (block != NULL && cnt > 0)
    {
        last = block;
        block = block->next;
        cnt--;
    }

    //Let's be paranoid
    if (last != NULL)
    {
        //Unlink last
        last->next = NULL;
    }

    //This is the cut off spot
    cnt = 0;
    while (block != NULL)
    {
        ArenaMemBlock *next = block->next;

        //Free it and go to next
        free (block), block = next;

        cnt++;
    }

    ARENA_LOG ("Arena: triming and only kept %d block(s), %d removed", keepHowMany, cnt);

#endif
}

/* Growable List initialization */
void dvmInitGrowableList(GrowableList *gList, size_t initLength)
{
    gList->numAllocated = initLength;
    gList->numUsed = 0;
    gList->elemList = (intptr_t *) dvmCompilerNew(sizeof(intptr_t) * initLength,
                                                  true);
}

/* Clear List */
void dvmClearGrowableList (GrowableList *gList)
{
    assert (gList != 0);
    gList->numUsed = 0;
}

/* Size of the List */
size_t dvmGrowableListSize(const GrowableList *gList)
{
    assert (gList != 0);
    return gList->numUsed;
}

/* Expand the capacity of a growable list */
static void expandGrowableList(GrowableList *gList)
{
    int newLength = gList->numAllocated;
    if (newLength < 128) {
        newLength <<= 1;
        //If ever newLength was set to 0, we add 1 to at least expand it
        newLength += 1;
    } else {
        newLength += 128;
    }
    intptr_t *newArray =
        (intptr_t *) dvmCompilerNew(sizeof(intptr_t) * newLength, true);
    memcpy(newArray, gList->elemList, sizeof(intptr_t) * gList->numAllocated);
    gList->numAllocated = newLength;
    gList->elemList = newArray;
}

/* Insert a new element into the growable list */
void dvmInsertGrowableList(GrowableList *gList, intptr_t elem)
{
    assert(gList->numAllocated != 0);
    if (gList->numUsed == gList->numAllocated) {
        expandGrowableList(gList);
    }
    gList->elemList[gList->numUsed++] = elem;
}

void dvmGrowableListIteratorInit(GrowableList *gList,
                                 GrowableListIterator *iterator)
{
    iterator->list = gList;
    iterator->idx = 0;
    iterator->size = gList->numUsed;
}

intptr_t dvmGrowableListIteratorNext(GrowableListIterator *iterator)
{
    assert(iterator->size == iterator->list->numUsed);
    if (iterator->idx == iterator->size) return 0;
    return iterator->list->elemList[iterator->idx++];
}

/**
 * @brief Set the last element retrieved by dvmGrowableListIteratorNext, return false if not yet done
 * @param iterator the iterator
 * @param elem the new element
 * @return true on success
 */
bool dvmGrowableListSetLastIterator(GrowableListIterator *iterator, intptr_t elem)
{
    //Make sure we already got one element
    if (iterator->idx == 0)
    {
        return false;
    }

    //Otherwise we can do it
    iterator->list->elemList[iterator->idx - 1] = elem;

    return true;
}

intptr_t dvmGrowableListGetElement(const GrowableList *gList, size_t idx)
{
    //Check for overflow
    if (idx >= gList->numUsed)
    {
        //Return null because we cannot get element with this index
        return 0;
    }
    else
    {
        //Return the element with this index from the list
        return gList->elemList[idx];
    }
}

/* Debug Utility - dump a compilation unit */
void dvmCompilerDumpCompilationUnit(CompilationUnit *cUnit)
{
    BasicBlock *bb;
    const char *blockTypeNames[] = {
        "Normal Chaining Cell",
        "Hot Chaining Cell",
        "Singleton Chaining Cell",
        "Predicted Chaining Cell",
        "Backward Branch",
        "Chaining Cell Gap",
        "N/A",
        "Entry Block",
        "Code Block",
        "Exit Block",
        "PC Reconstruction",
        "Exception Handling",
        "Catch Entry",
        "PreBackward Block",
        "From Interpreter",
    };

    const char *blockSuccTypeNames[] = {
        "Not Used",
        "Catch",
        "Packed Switch",
        "Sparse Switch",
    };

    ALOGD("Compiling %s %s", cUnit->method->clazz->descriptor,
         cUnit->method->name);
    ALOGD("%d insns", dvmGetMethodInsnsSize(cUnit->method));
    ALOGD("%d blocks in total", dvmGrowableListSize(&cUnit->blockList));
    GrowableListIterator iterator;

    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);

    while (true) {
        bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bb == NULL) break;

        //Show only non hidden bb
        if (bb->hidden == true) {
            continue;
        }

        ALOGD("Block %d (%s) (insn %04x - %04x%s)",
             bb->id,
             blockTypeNames[bb->blockType],
             bb->startOffset,
             bb->lastMIRInsn ? bb->lastMIRInsn->offset : bb->startOffset,
             bb->lastMIRInsn ? "" : " empty");

        //Dump instructions
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next) {
            char buffer[256];
            dvmCompilerExtendedDisassembler (cUnit, mir, & (mir->dalvikInsn), buffer, sizeof (buffer));
            ALOGD ("\t%04x %s", mir->offset, buffer);
        }

        //Dump branches
        if (bb->taken) {
            ALOGD("  Taken branch: block %d (%04x)",
                 bb->taken->id, bb->taken->startOffset);
        }
        if (bb->fallThrough) {
            ALOGD("  Fallthrough : block %d (%04x)",
                 bb->fallThrough->id, bb->fallThrough->startOffset);
        }

        if (bb->successorBlockList.blockListType != kNotUsed) {
            const char * blockTypeName = blockSuccTypeNames[bb->successorBlockList.blockListType];
            GrowableListIterator succIterator;
            dvmGrowableListIteratorInit(&bb->successorBlockList.blocks,
                                        &succIterator);
            while (true) {
                SuccessorBlockInfo *successorBlockInfo =
                    (SuccessorBlockInfo *)
                        dvmGrowableListIteratorNext(&succIterator);
                if (successorBlockInfo == NULL) break;

                BasicBlock *destBlock = successorBlockInfo->block;

                if (destBlock != 0)
                {
                    ALOGD("  %s : block %d (%04x)", blockTypeName, destBlock->id, destBlock->startOffset);
                }
            }
        }

        //Dump predecessors
        if (bb->predecessors != 0)
        {
            dvmDumpBitVector ("  Predecessors: ", bb->predecessors, true);
        }
        else
        {
            ALOGD ("  No predecessor information");
        }

    }
}

/*
 * dvmHashForeach callback.
 */
static int dumpMethodStats(void *compilerMethodStats, void *totalMethodStats)
{
    CompilerMethodStats *methodStats =
        (CompilerMethodStats *) compilerMethodStats;
    CompilerMethodStats *totalStats =
        (CompilerMethodStats *) totalMethodStats;

    totalStats->dalvikSize += methodStats->dalvikSize;
    totalStats->compiledDalvikSize += methodStats->compiledDalvikSize;
    totalStats->nativeSize += methodStats->nativeSize;

    /* Enable the following when fine-tuning the JIT performance */
#if 0
    int limit = (methodStats->dalvikSize >> 2) * 3;

    /* If over 3/4 of the Dalvik code is compiled, print something */
    if (methodStats->compiledDalvikSize >= limit) {
        ALOGD("Method stats: %s%s, %d/%d (compiled/total Dalvik), %d (native)",
             methodStats->method->clazz->descriptor,
             methodStats->method->name,
             methodStats->compiledDalvikSize,
             methodStats->dalvikSize,
             methodStats->nativeSize);
    }
#endif
    return 0;
}

/*
 * Dump the current stats of the compiler, including number of bytes used in
 * the code cache, arena size, and work queue length, and various JIT stats.
 */
void dvmCompilerDumpStats(void)
{
    CompilerMethodStats totalMethodStats;

    memset(&totalMethodStats, 0, sizeof(CompilerMethodStats));
    ALOGD("%d compilations using %d + %d + %d bytes",
         gDvmJit.numCompilations,
         gDvmJit.templateSize,
         gDvmJit.codeCacheByteUsed - gDvmJit.templateSize,
         gDvmJit.dataCacheByteUsed);
    ALOGD("Compiler arena uses %d blocks (%d bytes each)",
         numArenaBlocks, ARENA_DEFAULT_SIZE);
    ALOGD("Compiler work queue length is %d/%d", gDvmJit.compilerQueueLength,
         gDvmJit.compilerMaxQueued);
    dvmJitStats();
    dvmCompilerArchDump();
    if (gDvmJit.methodStatsTable) {
        dvmHashForeach(gDvmJit.methodStatsTable, dumpMethodStats,
                       &totalMethodStats);
        ALOGD("Code size stats: %d/%d (compiled/total Dalvik), %d (native)",
             totalMethodStats.compiledDalvikSize,
             totalMethodStats.dalvikSize,
             totalMethodStats.nativeSize);
    }
}

BitVector* dvmCompilerAllocBitVector(void)
{
    return dvmAllocBitVector (1, true, true);
}

/*
 * Allocate a bit vector with enough space to hold at least the specified
 * number of bits.
 *
 * NOTE: this function is now obsolete but instead of changing a lot of common
 * code, it seems simpler for the moment to wrap around it
 */
BitVector* dvmCompilerAllocBitVector(unsigned int startBits, bool expandable)
{
    return dvmAllocBitVector (startBits, expandable, true);
}

/*
 * Mark the specified bit as "set".
 *
 * NOTE: this function is now obsolete but instead of changing a lot of common
 * code, it seems simpler to wrap around it for now
 */
bool dvmCompilerSetBit(BitVector *pBits, unsigned int num)
{
    return dvmSetBit (pBits, num);
}

/*
 * Mark the specified bit as "unset".
 *
 * Returns "false" if the bit is outside the range of the vector and we're
 * not allowed to expand.
 *
 * NOTE: this is the sister implementation of dvmClearBit. In this version
 * memory is allocated from the compiler arena.
 */
bool dvmCompilerClearBit(BitVector *pBits, unsigned int num)
{
    if (num >= pBits->storageSize * sizeof(u4) * 8) {
        ALOGE("Trying to clear a bit that is not set in the vector yet!");
        dvmAbort();
    }

    pBits->storage[num >> 5] &= ~(1 << (num & 0x1f));
    return true;
}

/*
 * If set is true, mark all bits as 1. Otherwise mark all bits as 0.
 */
void dvmCompilerMarkAllBits(BitVector *pBits, bool set)
{
    int value = set ? -1 : 0;
    memset(pBits->storage, value, pBits->storageSize * (int)sizeof(u4));
}

void dvmDebugBitVector(char *msg, const BitVector *bv, int length)
{
    int i;

    ALOGE("%s", msg);
    for (i = 0; i < length; i++) {
        if (dvmIsBitSet(bv, i)) {
            ALOGE("    Bit %d is set", i);
        }
    }
}

void dvmCompilerAbort(CompilationUnit *cUnit)
{
    /* We might want to do a full abort to help with debugging */
    if (gDvmJit.abortOnCompilerError == true)
    {
        ALOGE("Forcing full system abort due to debug flag");
        dvmAbort();
    }

    /* Otherwise, just abort trace compilation */
    ALOGE("Jit: aborting trace compilation, reverting to interpreter");

    /* Force a traceback in debug builds */
    assert(0);

    /*
     * Abort translation and force to interpret-only for this trace
     * Matching setjmp in compiler thread work loop in Compiler.c.
     */
    longjmp(*cUnit->bailPtr, 1);
}

void dvmDumpBlockBitVector(const GrowableList *blocks, char *msg,
                           const BitVector *bv, int length)
{
    int i;

    ALOGE("%s", msg);
    for (i = 0; i < length; i++) {
        if (dvmIsBitSet(bv, i)) {
            BasicBlock *bb =
                (BasicBlock *) dvmGrowableListGetElement(blocks, i);
            char blockName[BLOCK_NAME_LEN];
            dvmGetBlockName(bb, blockName);
            ALOGE("Bit %d / %s is set", i, blockName);
        }
    }
}

void dvmGetBlockName(BasicBlock *bb, char *name)
{
    switch (bb->blockType) {
         case kChainingCellNormal:
            snprintf(name, BLOCK_NAME_LEN, "chain%04x", bb->id);
            break;
        case kChainingCellHot:
            snprintf(name, BLOCK_NAME_LEN, "chainhot%04x", bb->id);
            break;
        case kChainingCellInvokeSingleton:
            snprintf(name, BLOCK_NAME_LEN, "chainsingleton%04x", bb->id);
            break;
        case kChainingCellInvokePredicted:
            snprintf(name, BLOCK_NAME_LEN, "chaininvokepred%04x", bb->id);
            break;
        case kChainingCellBackwardBranch:
            snprintf(name, BLOCK_NAME_LEN, "chainbackward%04x", bb->id);
            break;
        case kChainingCellGap:
            snprintf(name, BLOCK_NAME_LEN, "chain%04x", bb->id);
            break;
        case kChainingCellLast:
            snprintf(name, BLOCK_NAME_LEN, "lastchain%04x", bb->id);
            break;
        case kEntryBlock:
            snprintf(name, BLOCK_NAME_LEN, "entry");
            break;
         case kDalvikByteCode:
            snprintf(name, BLOCK_NAME_LEN, "block%04x", bb->id);
            break;
        case kExitBlock:
            snprintf(name, BLOCK_NAME_LEN, "exit");
            break;
         case kPCReconstruction:
            snprintf(name, BLOCK_NAME_LEN, "pcreconstruction%04x", bb->id);
            break;
         case kExceptionHandling:
            snprintf(name, BLOCK_NAME_LEN, "exception%04x", bb->id);
            break;
         case kPreBackwardBlock:
             snprintf(name, BLOCK_NAME_LEN, "prebackward%04x", bb->id);
             break;
         case kFromInterpreter:
             snprintf(name, BLOCK_NAME_LEN, "fromInterpreter%04x", bb->id);
             break;
         default:
            snprintf(name, BLOCK_NAME_LEN, "??");
            break;
    }
}

/**
 * @brief Get the next BasicBlock when considering a BasicBlock index BitVector
 * @param bvIterator the BitVector iterator
 * @param blockList The list of basic blocks
 * @return 0 if finished, the BasicBlock otherwise
 */
BasicBlock *dvmCompilerGetNextBasicBlockViaBitVector (BitVectorIterator &bvIterator, const GrowableList &blockList)
{
    //Get next element
    int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

    //If done, bail
    if (blockIdx == -1)
    {
        return 0;
    }

    //Get BasicBlock
    BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement (&blockList, blockIdx);

    //Return the element
    return bb;
}

bool dvmCompilerIsOpcodeConditionalBranch (int opcode)
{
    //Get dex flags
    int dexFlags = dvmCompilerGetOpcodeFlags (opcode);

    //Can it continue and can it branch?
    bool result = (dexFlags == (kInstrCanContinue|kInstrCanBranch));

    return result;
}

void dvmCompilerFindEntries (CompilationUnit *cUnit, GrowableList *list)
{
    //Get an iterator over the BasicBlocks
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit (& (cUnit->blockList), &iterator);

    //Walk the CompilationUnit's BasicBlocks
    while (true)
    {
        //Get next element
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);

        //Are we done?
        if (bb == NULL)
        {
            break;
        }

        //If hidden, we can skip
        if (bb->hidden == true)
        {
            continue;
        }

        //Paranoid
        assert (bb->predecessors != 0);

        //Does it have no predecessors?
        if (dvmCountSetBits (bb->predecessors) == 0)
        {
            intptr_t elem = (intptr_t) (bb);

            //Add it to our list
            dvmInsertGrowableList (list, elem);
        }
    }
}

bool dvmCompilerDoesInvokeNeedPrediction (Opcode opcode)
{
    switch (opcode)
    {
        //Return true for all virtual/interface invokes
        case OP_INVOKE_VIRTUAL:
        case OP_INVOKE_VIRTUAL_QUICK:
        case OP_INVOKE_INTERFACE:
        case OP_INVOKE_VIRTUAL_RANGE:
        case OP_INVOKE_VIRTUAL_QUICK_RANGE:
        case OP_INVOKE_INTERFACE_RANGE:
            return true;
        default:
            break;
    }

    //If we get here we do not have a virtual invoke
    return false;
}

/**
 * Checks if invoke calls fully resolved method
 * @see Utility.h
 */
const Method *dvmCompilerCheckResolvedMethod (const Method *methodContainingInvoke,
        const DecodedInstruction *invokeInstr, bool tryToResolve)
{
    const Method *callee = 0;
    u4 methodIdx = invokeInstr->vB;

    switch (invokeInstr->opcode)
    {
        case OP_INVOKE_SUPER:
        case OP_INVOKE_SUPER_RANGE:
        {
            //Get the base method
            Method *baseMethod = dvmDexGetResolvedMethod (methodContainingInvoke->clazz->pDvmDex, methodIdx);

            //Try to resolve the base method if needed
            if (tryToResolve == true && baseMethod == 0)
            {
                baseMethod = dvmResolveMethod (methodContainingInvoke->clazz, methodIdx, METHOD_VIRTUAL);
            }

            //If we resolve the base method and we can find the index in the super vtable, then we try it
            if (baseMethod != 0 && baseMethod->methodIndex < methodContainingInvoke->clazz->super->vtableCount)
            {
                //Get the method to call from the super vtable
                callee = methodContainingInvoke->clazz->super->vtable[baseMethod->methodIndex];
            }

            break;
        }
        case OP_INVOKE_SUPER_QUICK:
        case OP_INVOKE_SUPER_QUICK_RANGE:
            callee = methodContainingInvoke->clazz->super->vtable[methodIdx];
            break;
        case OP_INVOKE_STATIC:
        case OP_INVOKE_STATIC_RANGE:
            callee = dvmDexGetResolvedMethod (methodContainingInvoke->clazz->pDvmDex, methodIdx);

            if (tryToResolve == true && callee == 0)
            {
                callee = dvmResolveMethod (methodContainingInvoke->clazz, methodIdx, METHOD_STATIC);
            }

            break;
        case OP_INVOKE_DIRECT:
        case OP_INVOKE_DIRECT_RANGE:
            callee = dvmDexGetResolvedMethod (methodContainingInvoke->clazz->pDvmDex, methodIdx);

            if (tryToResolve == true && callee == 0)
            {
                callee = dvmResolveMethod (methodContainingInvoke->clazz, methodIdx, METHOD_DIRECT);
            }

            break;

        case OP_INVOKE_OBJECT_INIT_RANGE:
            //The dex optimizer has already determined that we are doing an object-init.
            callee = dvmFindDirectMethodByDescriptor (gDvm.classJavaLangObject, "<init>", "()V");
            break;

        case OP_INVOKE_VIRTUAL:
        case OP_INVOKE_VIRTUAL_RANGE:
        {
            //Get the base method
            Method *baseMethod = dvmDexGetResolvedMethod (methodContainingInvoke->clazz->pDvmDex, methodIdx);

            //Try to resolve the base method if needed
            if (tryToResolve == true && baseMethod == 0)
            {
                baseMethod = dvmResolveMethod (methodContainingInvoke->clazz, methodIdx, METHOD_VIRTUAL);
            }

            //Without the actual object pointer, we cannot figure the actual method being invoked
            //since we need to be able to look at the vtable. Thus we simply return our base method
            //since maybe we were able to resolve that.
            callee = baseMethod;

            break;
        }
        default:
            //For interface invokes we cannot figure out method callee without access to the this pointer
            break;
    }

    return callee;
}

/**
 * Checks if bytecodes in method reference fully resolved classes, methods, and fields
 * @see Utility.h
 */
bool dvmCompilerCheckResolvedReferences (const Method *method, const DecodedInstruction *insn, bool tryToResolve)
{
    assert (insn != 0);

    switch (insn->opcode)
    {
        case OP_NEW_INSTANCE:
        case OP_CHECK_CAST:
        case OP_FILLED_NEW_ARRAY:
        case OP_FILLED_NEW_ARRAY_RANGE:
        case OP_CONST_CLASS:
        case OP_NEW_ARRAY:
        case OP_INSTANCE_OF:
        {
            bool fromUnverifiedSource = false;
            u4 classIdx = insn->vB;

            if (insn->opcode == OP_CONST_CLASS || insn->opcode == OP_INSTANCE_OF)
            {
                fromUnverifiedSource = true;
            }

            if (insn->opcode == OP_NEW_ARRAY || insn->opcode == OP_INSTANCE_OF)
            {
                classIdx = insn->vC;
            }

            ClassObject *classPtr = dvmDexGetResolvedClass (method->clazz->pDvmDex, classIdx);

            if (tryToResolve == true && classPtr == 0)
            {
                classPtr = dvmResolveClass (method->clazz, classIdx, fromUnverifiedSource);
            }

            if (classPtr == 0)
            {
                return false;
            }

            break;
        }
        case OP_SGET:
        case OP_SGET_WIDE:
        case OP_SGET_OBJECT:
        case OP_SGET_BOOLEAN:
        case OP_SGET_BYTE:
        case OP_SGET_CHAR:
        case OP_SGET_SHORT:
        case OP_SPUT:
        case OP_SPUT_WIDE:
        case OP_SPUT_OBJECT:
        case OP_SPUT_BOOLEAN:
        case OP_SPUT_BYTE:
        case OP_SPUT_CHAR:
        case OP_SPUT_SHORT:
        case OP_SGET_VOLATILE:
        case OP_SPUT_VOLATILE:
        case OP_SGET_OBJECT_VOLATILE:
        case OP_SPUT_OBJECT_VOLATILE:
        case OP_SGET_WIDE_VOLATILE:
        case OP_SPUT_WIDE_VOLATILE:
        {
            u4 ref = insn->vB;
            StaticField *sField =
                    reinterpret_cast<StaticField*> (dvmDexGetResolvedField (method->clazz->pDvmDex, ref));

            if (tryToResolve == true && sField == 0)
            {
                sField = dvmResolveStaticField (method->clazz, ref);
            }

            if (sField == 0)
            {
                return false;
            }

            break;
        }
        case OP_CONST_STRING_JUMBO:
        case OP_CONST_STRING:
        {
            StringObject *strPtr = dvmDexGetResolvedString (method->clazz->pDvmDex, insn->vB);

            if (tryToResolve == true && strPtr == 0)
            {
                strPtr = dvmResolveString (method->clazz, insn->vB);
            }

            if (strPtr == 0)
            {
                return false;
            }

            break;
        }
        case OP_IGET:
        case OP_IGET_WIDE:
        case OP_IGET_OBJECT:
        case OP_IGET_BOOLEAN:
        case OP_IGET_BYTE:
        case OP_IGET_CHAR:
        case OP_IGET_SHORT:
        case OP_IPUT:
        case OP_IPUT_WIDE:
        case OP_IPUT_OBJECT:
        case OP_IPUT_BOOLEAN:
        case OP_IPUT_BYTE:
        case OP_IPUT_CHAR:
        case OP_IPUT_SHORT:
        case OP_IGET_VOLATILE:
        case OP_IPUT_VOLATILE:
        case OP_IGET_OBJECT_VOLATILE:
        case OP_IPUT_OBJECT_VOLATILE:
        case OP_IGET_WIDE_VOLATILE:
        case OP_IPUT_WIDE_VOLATILE:
        {
            u4 ref = insn->vC;
            InstField *iField = reinterpret_cast <InstField*> (dvmDexGetResolvedField(method->clazz->pDvmDex, ref));

            if (tryToResolve == true && iField == 0)
            {
                iField = dvmResolveInstField(method->clazz, ref);
            }

            if (iField == 0)
            {
                return false;
            }

            break;
        }
        default:
            break;
    }

    //Check if we have an invoke
    if ((dvmCompilerGetOpcodeFlags (insn->opcode) & kInstrInvoke) != 0)
    {
        //Check if we can resolve callee
        const Method *callee = dvmCompilerCheckResolvedMethod (method, insn, tryToResolve);

        //If callee is resolved, it is non-null
        return (callee != 0);
    }

    //If we get here, everything went okay
    return true;
}

void dvmCompilerUpdateCUnitNumDalvikRegisters (CompilationUnit *cUnit, int newNumberDalvikRegisters)
{
    //We only need to update data structures if the new number of dalvik registers is greater than before
    if (newNumberDalvikRegisters > cUnit->numDalvikRegisters)
    {
        //Invalidate all structures that are size of numDalvikRegisters
        cUnit->dalvikToSSAMap = 0;
        cUnit->defBlockMatrix = 0;
        cUnit->ssaSubScripts = 0;

        //We do not need to invalidate the isConstantV because it is an expandable BitVector
        if (cUnit->isConstantV != 0)
        {
            //Let's go ahead and clear it because we may have different constant information when we update
            dvmClearAllBits(cUnit->isConstantV);
        }

        //We also do not need to invalidate ssaToDalvikMap because it is a GrowableList
        if (cUnit->ssaToDalvikMap != 0)
        {
            //The dalvik registers have changed so the map doesn't hold good content
            dvmClearGrowableList (cUnit->ssaToDalvikMap);
        }

        //For tempDalvikRegisterV and tempSSARegisterV, we don't have to set it to 0, we can just clear all bits
        if (cUnit->tempDalvikRegisterV != 0)
        {
            //Ensure that BitVector grows to the number of dalvik registers
            dvmCompilerSetBit (cUnit->tempDalvikRegisterV, newNumberDalvikRegisters - 1);
            dvmClearAllBits (cUnit->tempDalvikRegisterV);
        }
        if (cUnit->tempSSARegisterV != 0)
        {
            //Ensure that BitVector grows to the number of dalvik registers
            dvmCompilerSetBit (cUnit->tempSSARegisterV, newNumberDalvikRegisters - 1);
            dvmClearAllBits (cUnit->tempSSARegisterV);
        }

        cUnit->numDalvikRegisters = newNumberDalvikRegisters;
    }
}

unsigned int dvmCompilerGetMaxScratchRegisters (void)
{
    //Get the maximum number of scratch registers available
    unsigned int max = dvmArchSpecGetNumberOfScratch ();

    //We do not want to exceed the user configured maximum
    if (max > gDvmJit.maximumScratchRegisters)
    {
        max = gDvmJit.maximumScratchRegisters;
    }

    return max;
}

/**
 * @brief Do we have scratch registers available?
 * @param cUnit The compilation unit
 * @param howMany howMany how many consecutive registers do you want?
 * This allows the request for consecutive VRs for wide or range cases (default: 1)
 * @return Returns whether we have enough scratch registers or not
 */
static bool haveFreeScratchRegisters (CompilationUnit *cUnit, unsigned int howMany = 1)
{
    //Get the maximum number of scratch registers available
    unsigned int max = dvmCompilerGetMaxScratchRegisters ();

    //Return whether we have any more that we can use
    return (cUnit->numUsedScratchRegisters + howMany <= max);
}

int dvmCompilerGetFreeScratchRegister (CompilationUnit *cUnit, unsigned int consecutives)
{
    //Do we have free registers?
    bool haveFree = haveFreeScratchRegisters (cUnit, consecutives);

    //If we do not have any free, then simply return -1
    if (haveFree == false)
    {
        return -1;
    }

    //Now get a free scratch register
    int freeScratch = dvmArchSpecGetPureLocalScratchRegister (cUnit->method, cUnit->numUsedScratchRegisters,
            cUnit->registerWindowShift);

    //We know that middle-end can only support 2^16 virtual registers since for SSA it also encodes
    //the version in the same field. Thus, right now we check if the largest VR in this sequence
    //actually exceeds that value. If it does, then we simply return -1.
    if (freeScratch + consecutives > (1 << 16))
    {
        return -1;
    }

    //Keep track that we are giving out some scratch registers
    cUnit->numUsedScratchRegisters += consecutives;

    //The compilation unit cares about how many are pending in order to synchronize with numDalvikRegisters
    cUnit->pendingScratchRegisters += consecutives;

    return freeScratch;
}

bool dvmCompilerIsPureLocalScratch (const CompilationUnit *cUnit, int reg, bool isSsa)
{
    //Figure out the virtual register first
    const int virtualReg = (isSsa == true ? dvmExtractSSARegister (cUnit, reg) : reg);

    return dvmArchIsPureLocalScratchRegister (cUnit->method, virtualReg, cUnit->registerWindowShift);
}

void dvmCompilerCommitPendingScratch (CompilationUnit *cUnit)
{
    //First check if we have any pending scratch registers
    if (cUnit->pendingScratchRegisters > 0)
    {
        //We have some pending scratch registers so include them in total number of dalvik registers
        dvmCompilerUpdateCUnitNumDalvikRegisters (cUnit, cUnit->numDalvikRegisters + cUnit->pendingScratchRegisters);

        //We have just counted them so reset the counter
        cUnit->pendingScratchRegisters = 0;
    }
}

/**
 * @brief Used to color during DFS traversal of CFG
 */
enum VisitingColor
{
    BeingVisited,  //!< Node is being visited
    DoneVisiting,  //!< Node has already been visited
};

/**
 * @brief Helper method which looks for a loop
 * @param bb The basic block being visited
 * @param visited Map which holds visited block and their visiting color
 * @return Returns whether a loop has been found
 */
static bool lookForLoop (BasicBlock *bb, std::map<BasicBlock *, VisitingColor> &visited)
{
    //If we have already visited it and we're also in the middle of visiting it and its children then we found loop
    if (visited.find (bb) != visited.end ())
    {
        VisitingColor color = visited[bb];

        if (color == BeingVisited)
        {
            return true;
        }
        else if (color == DoneVisiting)
        {
            return false;
        }
    }

    //Insert current BB to visited
    visited[bb] = BeingVisited;

    bool foundLoop = false;

    //Create iterator for visiting children
    ChildBlockIterator childIter (bb);

    //Now iterate through the children to visit each of them first
    for (BasicBlock **childPtr = childIter.getNextChildPtr (); childPtr != 0 && foundLoop == false;
            childPtr = childIter.getNextChildPtr ())
    {
        BasicBlock *child = *childPtr;

        assert (child != 0);

        //Doing a DFS so we look for loop in child first
        foundLoop = lookForLoop (child, visited);
    }

    //Done visiting
    visited[bb] = DoneVisiting;

    return foundLoop;
}

bool dvmCompilerDoesContainLoop (GrowableList &blockList, BasicBlock *entry)
{
    bool foundLoop = false;
    std::map<BasicBlock *, VisitingColor> visited;

    if (entry != 0)
    {
        //Keep track of visited blocks and start from entry
        foundLoop = lookForLoop (entry, visited);
    }

    //We'd like to assume that we visited all blocks by going from entry but now we check to make sure
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit (&blockList, &iterator);

    while (foundLoop == false)
    {
        BasicBlock *bb = reinterpret_cast<BasicBlock *> (dvmGrowableListIteratorNext (&iterator));

        //We break out of loop if we don't have anymore BBs
        if (bb == 0)
        {
            break;
        }

        //Check if we visited this already
        if (visited.find (bb) == visited.end())
        {
            foundLoop = lookForLoop (bb, visited);
        }
    }

    return foundLoop;
}

/**
 * @brief Used to fill a set with visited basic blocks.
 * @param cUnit The compilation unit that is required to have a set of BasicBlock* in its walk data
 * @param bb The basic block currently being visited.
 * @return Always returns false because it does not change the CFG.
 */
bool markVisited (CompilationUnit *cUnit, BasicBlock *bb)
{
    std::set<BasicBlock *> *visited = static_cast<std::set<BasicBlock *> *> (cUnit->walkData);

    assert(visited != 0);

    visited->insert (bb);

    return false;
}

/**
 * @brief Removes block from CFG if dalvik code block and not visited.
 * @param cUnit The compilation unit that is required to have a set of BasicBlock* in its walk data
 * @param bb The basic block to attempt removal on.
 * @return Returns true if the block was removed from the CFG.
 */
bool removeUnreachableBlock (CompilationUnit *cUnit, BasicBlock *bb)
{
    std::set<BasicBlock *> *visited = static_cast<std::set<BasicBlock *> *> (cUnit->walkData);

    assert (visited != 0);

    //If we did not visit this block, then it is a candidate for removal
    if (visited->find (bb) == visited->end ())
    {
        //Since block types can serve special semantic meaning, we only remove unreachable dalvik blocks
        if (bb->blockType == kDalvikByteCode)
        {
            //Remove the block
            dvmCompilerHideBasicBlock (cUnit->blockList, bb);

            return true;
        }
    }

    return false;
}

void dvmCompilerRemoveUnreachableBlocks (CompilationUnit *cUnit)
{
    std::set<BasicBlock *> visited;
    void *walkData = static_cast<void *> (&visited);

    //Mark only the reachable nodes as visited.
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, markVisited, kReachableNodes, false, walkData);

    //Now visit all nodes removing the unreachable blocks.
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, removeUnreachableBlock, kAllNodes, false, walkData);
}

bool dvmCompilerIsRegConstant (const CompilationUnit *cUnit, int ssaReg)
{
    //Paranoid
    if (cUnit->isConstantV == 0)
    {
        return false;
    }

    return dvmIsBitSet (cUnit->isConstantV, ssaReg);
}

bool dvmCompilerGetFirstConstantUsed (const CompilationUnit *cUnit, const MIR *mir, int &constantValue)
{
    long long dfFlags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

    //First check if the bytecode itself has constants encoded
    if ((dfFlags & DF_HAS_CONSTANTS) != 0)
    {
        if ((dfFlags & DF_C_IS_CONST) != 0)
        {
            constantValue = mir->dalvikInsn.vC;
            return true;
        }
    }

    //No constants were found in encoding but it is possible that constant propagation
    //has identified ssa registers that are constants.
    if (mir == 0 || mir->ssaRep == 0 || mir->ssaRep->uses == 0)
    {
        return false;
    }

    //Look for the first ssa register that is constant
    int ssaReg = -1;
    for (int use = 0; use < mir->ssaRep->numUses; use++)
    {
        int regToCheck = mir->ssaRep->uses[use];

        if (dvmCompilerIsRegConstant (cUnit, regToCheck) == true)
        {
            //Found constant register so return it
            ssaReg = regToCheck;
            break;
        }
    }

    //Has an ssa register been found yet?
    if (ssaReg != -1)
    {
        //Paranoid
        if (cUnit->constantValues != 0)
        {
            constantValue = (*(cUnit->constantValues))[ssaReg];
            return true;
        }
    }

    //Did not find any constants
    return false;
}

bool dvmCompilerWillCodeCacheOverflow(unsigned int moreCode)
{
    /* Check if code cache will overflow after adding more code */
    if (gDvmJit.codeCacheFull == true
        || (gDvmJit.codeCacheByteUsed + moreCode > gDvmJit.codeCacheSize)) {
        return true;
    }

    /* No overflow */
    return false;
}

bool dvmCompilerWillDataCacheOverflow(unsigned int moreData)
{
    /* Check if data cache will overflow after adding more data */
    if (gDvmJit.dataCacheFull == true
        || (gDvmJit.dataCacheByteUsed + moreData > gDvmJit.dataCacheSize)) {
        return true;
    }

    /* No overflow */
    return false;
}

void dvmCompilerSetCodeAndDataCacheFull(void)
{
    gDvmJit.codeCacheFull = true;
    gDvmJit.dataCacheFull = true;
}

void dvmCompilerSetDataCacheFull(void)
{
    gDvmJit.dataCacheFull = true;
}
