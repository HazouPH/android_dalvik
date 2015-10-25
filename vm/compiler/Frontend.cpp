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
#include "libdex/DexOpcodes.h"
#include "libdex/DexCatch.h"
#include "interp/Jit.h"
#include "CompilerInternals.h"
#include "Dataflow.h"
#include "Loop.h"
#include "LoopInformation.h"
#include "RegisterizationME.h"
#include "Utility.h"
#include "MethodContextHandler.h"

#ifdef ARCH_IA32
#define BYTECODE_FILTER
#endif

#ifdef PROFILE_OPCODE
#include "dalvikvm/ProfileOpcodes.h"
extern int opcodeJit[kNumPackedOpcodes];
#endif

#ifdef ARCH_IA32
#include "codegen/x86/lightcg/CompilationUnit.h"
#endif

#if defined(VTUNE_DALVIK)
#include "VTuneSupport.h"
#endif

//Need it for UINT_MAX
#include <limits.h>


static inline bool contentIsInsn(const u2 *codePtr) {
    u2 instr = *codePtr;
    Opcode opcode = (Opcode)(instr & 0xff);

    /*
     * Since the low 8-bit in metadata may look like OP_NOP, we need to check
     * both the low and whole sub-word to determine whether it is code or data.
     */
    return (opcode != OP_NOP || instr == 0);
}

/*
 * Parse an instruction, return the length of the instruction
 */
static inline int parseInsn(const u2 *codePtr, DecodedInstruction *decInsn,
                            bool printMe)
{
    // Don't parse instruction data
    if (!contentIsInsn(codePtr)) {
        return 0;
    }

    u2 instr = *codePtr;
    Opcode opcode = dexOpcodeFromCodeUnit(instr);

    dexDecodeInstruction(codePtr, decInsn);
    if (printMe) {
        char *decodedString = dvmCompilerGetDalvikDisassembly(decInsn, NULL);
        ALOGD("%p: %#06x %s", codePtr, opcode, decodedString);
    }
    return dexGetWidthFromOpcode(opcode);
}

#define UNKNOWN_TARGET 0xffffffff

/*
 * Identify block-ending instructions and collect supplemental information
 * regarding the following instructions.
 */
static inline bool findBlockBoundary(const Method *caller, MIR *insn,
                                     unsigned int curOffset,
                                     unsigned int *target, bool *isInvoke,
                                     const Method **callee)
{
    switch (insn->dalvikInsn.opcode) {
        /* Target is not compile-time constant */
        case OP_RETURN_VOID:
        case OP_RETURN:
        case OP_RETURN_WIDE:
        case OP_RETURN_OBJECT:
        case OP_THROW:
          *target = UNKNOWN_TARGET;
          break;
        case OP_INVOKE_VIRTUAL:
        case OP_INVOKE_VIRTUAL_RANGE:
        case OP_INVOKE_INTERFACE:
        case OP_INVOKE_INTERFACE_RANGE:
        case OP_INVOKE_VIRTUAL_QUICK:
        case OP_INVOKE_VIRTUAL_QUICK_RANGE:
            *isInvoke = true;
            break;
        case OP_INVOKE_SUPER:
        case OP_INVOKE_SUPER_RANGE: {
            int mIndex = caller->clazz->pDvmDex->
                pResMethods[insn->dalvikInsn.vB]->methodIndex;
            const Method *calleeMethod =
                caller->clazz->super->vtable[mIndex];

            if (calleeMethod && !dvmIsNativeMethod(calleeMethod)) {
                *target = (unsigned int) calleeMethod->insns;
            }
            *isInvoke = true;
            *callee = calleeMethod;
            break;
        }
        case OP_INVOKE_STATIC:
        case OP_INVOKE_STATIC_RANGE: {
            const Method *calleeMethod =
                caller->clazz->pDvmDex->pResMethods[insn->dalvikInsn.vB];

            if (calleeMethod && !dvmIsNativeMethod(calleeMethod)) {
                *target = (unsigned int) calleeMethod->insns;
            }
            *isInvoke = true;
            *callee = calleeMethod;
            break;
        }
        case OP_INVOKE_SUPER_QUICK:
        case OP_INVOKE_SUPER_QUICK_RANGE: {
            const Method *calleeMethod =
                caller->clazz->super->vtable[insn->dalvikInsn.vB];

            if (calleeMethod && !dvmIsNativeMethod(calleeMethod)) {
                *target = (unsigned int) calleeMethod->insns;
            }
            *isInvoke = true;
            *callee = calleeMethod;
            break;
        }
        case OP_INVOKE_DIRECT:
        case OP_INVOKE_DIRECT_RANGE: {
            const Method *calleeMethod =
                caller->clazz->pDvmDex->pResMethods[insn->dalvikInsn.vB];
            if (calleeMethod && !dvmIsNativeMethod(calleeMethod)) {
                *target = (unsigned int) calleeMethod->insns;
            }
            *isInvoke = true;
            *callee = calleeMethod;
            break;
        }
        case OP_GOTO:
        case OP_GOTO_16:
        case OP_GOTO_32:
            *target = curOffset + (int) insn->dalvikInsn.vA;
            break;

        case OP_IF_EQ:
        case OP_IF_NE:
        case OP_IF_LT:
        case OP_IF_GE:
        case OP_IF_GT:
        case OP_IF_LE:
            *target = curOffset + (int) insn->dalvikInsn.vC;
            break;

        case OP_IF_EQZ:
        case OP_IF_NEZ:
        case OP_IF_LTZ:
        case OP_IF_GEZ:
        case OP_IF_GTZ:
        case OP_IF_LEZ:
            *target = curOffset + (int) insn->dalvikInsn.vB;
            break;

        default:
            return false;
    }
    return true;
}

static inline bool isGoto(MIR *insn)
{
    switch (insn->dalvikInsn.opcode) {
        case OP_GOTO:
        case OP_GOTO_16:
        case OP_GOTO_32:
            return true;
        default:
            return false;
    }
}

/*
 * Identify unconditional branch instructions
 */
static inline bool isUnconditionalBranch(MIR *insn)
{
    switch (insn->dalvikInsn.opcode) {
        case OP_RETURN_VOID:
        case OP_RETURN_VOID_BARRIER:
        case OP_RETURN:
        case OP_RETURN_WIDE:
        case OP_RETURN_OBJECT:
            return true;
        default:
            return isGoto(insn);
    }
}

/*
 * dvmHashTableLookup() callback
 */
static int compareMethod(const CompilerMethodStats *m1,
                         const CompilerMethodStats *m2)
{
    return (int) m1->method - (int) m2->method;
}

/*
 * Analyze the body of the method to collect high-level information regarding
 * inlining:
 * - is empty method?
 * - is getter/setter?
 * - can throw exception?
 *
 * Currently the inliner only handles getters and setters. When its capability
 * becomes more sophisticated more information will be retrieved here.
 */
static int analyzeInlineTarget(DecodedInstruction *dalvikInsn, int attributes,
                               int offset)
{
    int flags = dexGetFlagsFromOpcode(dalvikInsn->opcode);
    int dalvikOpcode = dalvikInsn->opcode;

    if (flags & kInstrInvoke) {
        attributes &= ~METHOD_IS_LEAF;
    }

    if (!(flags & kInstrCanReturn)) {
        if (!(dvmCompilerDataFlowAttributes[dalvikOpcode] &
              DF_IS_GETTER)) {
            attributes &= ~METHOD_IS_GETTER;
        }
        if (!(dvmCompilerDataFlowAttributes[dalvikOpcode] &
              DF_IS_SETTER)) {
            attributes &= ~METHOD_IS_SETTER;
        }
    }

    /*
     * The expected instruction sequence is setter will never return value and
     * getter will also do. Clear the bits if the behavior is discovered
     * otherwise.
     */
    if (flags & kInstrCanReturn) {
        if (dalvikOpcode == OP_RETURN_VOID) {
            attributes &= ~METHOD_IS_GETTER;
        }
        else {
            attributes &= ~METHOD_IS_SETTER;
        }
    }

    if (flags & kInstrCanThrow) {
        attributes &= ~METHOD_IS_THROW_FREE;
    }

    if (offset == 0 && dalvikOpcode == OP_RETURN_VOID) {
        attributes |= METHOD_IS_EMPTY;
    }

    /*
     * Check if this opcode is selected for single stepping.
     * If so, don't inline the callee as there is no stack frame for the
     * interpreter to single-step through the instruction.
     */
    if (SINGLE_STEP_OP(dalvikOpcode)) {
        attributes &= ~(METHOD_IS_GETTER | METHOD_IS_SETTER);
    }

    return attributes;
}

/*
 * Analyze each method whose traces are ever compiled. Collect a variety of
 * statistics like the ratio of exercised vs overall code and code bloat
 * ratios. If isCallee is true, also analyze each instruction in more details
 * to see if it is suitable for inlining.
 */
CompilerMethodStats *dvmCompilerAnalyzeMethodBody(const Method *method,
                                                  bool isCallee)
{
    const DexCode *dexCode = dvmGetMethodCode(method);
    const u2 *codePtr = dexCode->insns;
    const u2 *codeEnd = dexCode->insns + dexCode->insnsSize;
    int insnSize = 0;
    int hashValue = dvmComputeUtf8Hash(method->name);
    unsigned int numBytecodes = 0;

    CompilerMethodStats dummyMethodEntry; // For hash table lookup
    CompilerMethodStats *realMethodEntry; // For hash table storage

    /* For lookup only */
    dummyMethodEntry.method = method;
    realMethodEntry = (CompilerMethodStats *)
        dvmHashTableLookup(gDvmJit.methodStatsTable,
                           hashValue,
                           &dummyMethodEntry,
                           (HashCompareFunc) compareMethod,
                           false);

    /* This method has never been analyzed before - create an entry */
    if (realMethodEntry == NULL) {
        realMethodEntry =
            (CompilerMethodStats *) calloc(1, sizeof(CompilerMethodStats));
        realMethodEntry->method = method;

        dvmHashTableLookup(gDvmJit.methodStatsTable, hashValue,
                           realMethodEntry,
                           (HashCompareFunc) compareMethod,
                           true);
    }

    /* This method is invoked as a callee and has been analyzed - just return */
    if ((isCallee == true) && (realMethodEntry->attributes & METHOD_IS_CALLEE))
        return realMethodEntry;

    /*
     * Similarly, return if this method has been compiled before as a hot
     * method already.
     */
    if ((isCallee == false) &&
        (realMethodEntry->attributes & METHOD_IS_HOT))
        return realMethodEntry;

    int attributes;

    /* Method hasn't been analyzed for the desired purpose yet */
    if (isCallee) {
        /* Aggressively set the attributes until proven otherwise */
        attributes = METHOD_IS_LEAF | METHOD_IS_THROW_FREE | METHOD_IS_CALLEE |
                     METHOD_IS_GETTER | METHOD_IS_SETTER;
    } else {
        attributes = METHOD_IS_HOT;
    }

    /* Count the number of instructions */
    while (codePtr < codeEnd) {
        DecodedInstruction dalvikInsn;
        int width = parseInsn(codePtr, &dalvikInsn, false);

        /* Terminate when the data section is seen */
        if (width == 0)
            break;

        /* We have a valid instruction so increment number of instructions */
        numBytecodes++;

        if (isCallee) {
            attributes = analyzeInlineTarget(&dalvikInsn, attributes, insnSize);
        }

        insnSize += width;
        codePtr += width;
    }

    /*
     * Only handle simple getters/setters with one instruction followed by
     * return
     */
    if ((attributes & (METHOD_IS_GETTER | METHOD_IS_SETTER)) &&
        (insnSize != 3)) {
        attributes &= ~(METHOD_IS_GETTER | METHOD_IS_SETTER);
    }

    /*
     * Each bytecode unit is 2 bytes large so to get the total size we multiply
     * number of bytecode units by size of bytecode unit.
     */
    realMethodEntry->dalvikSize = insnSize * sizeof (u2);

    /* Keep track of number of bytecodes in method */
    realMethodEntry->numBytecodes = numBytecodes;

    /* Set the method attributes */
    realMethodEntry->attributes |= attributes;

#if 0
    /* Uncomment the following to explore various callee patterns */
    if (attributes & METHOD_IS_THROW_FREE) {
        ALOGE("%s%s is inlinable%s", method->clazz->descriptor, method->name,
             (attributes & METHOD_IS_EMPTY) ? " empty" : "");
    }

    if (attributes & METHOD_IS_LEAF) {
        ALOGE("%s%s is leaf %d%s", method->clazz->descriptor, method->name,
             insnSize, insnSize < 5 ? " (small)" : "");
    }

    if (attributes & (METHOD_IS_GETTER | METHOD_IS_SETTER)) {
        ALOGE("%s%s is %s", method->clazz->descriptor, method->name,
             attributes & METHOD_IS_GETTER ? "getter": "setter");
    }
    if (attributes ==
        (METHOD_IS_LEAF | METHOD_IS_THROW_FREE | METHOD_IS_CALLEE)) {
        ALOGE("%s%s is inlinable non setter/getter", method->clazz->descriptor,
             method->name);
    }
#endif

    return realMethodEntry;
}

/*
 * Crawl the stack of the thread that requesed compilation to see if any of the
 * ancestors are on the blacklist.
 */
static bool filterMethodByCallGraph(Thread *thread, const char *curMethodName)
{
    /* Crawl the Dalvik stack frames and compare the method name*/
    StackSaveArea *ssaPtr = ((StackSaveArea *) thread->interpSave.curFrame) - 1;
    while (ssaPtr != ((StackSaveArea *) NULL) - 1) {
        const Method *method = ssaPtr->method;
        if (method) {
            int hashValue = dvmComputeUtf8Hash(method->name);
            bool found =
                dvmHashTableLookup(gDvmJit.methodTable, hashValue,
                               (char *) method->name,
                               (HashCompareFunc) strcmp, false) !=
                NULL;
            if (found) {
                ALOGD("Method %s (--> %s) found on the JIT %s list",
                     method->name, curMethodName,
                     gDvmJit.includeSelectedMethod ? "white" : "black");
                return true;
            }

        }
        ssaPtr = ((StackSaveArea *) ssaPtr->prevFrame) - 1;
    };
    return false;
}

/**
 * @brief Checks if bytecode in method reference fully resolved classes, methods, and fields.
 * @details If not resolved, tries to resolve it.
 * @param method The method that contains the bytecode
 * @param insn The decoded instruction that we are examining
 * @param failureMessage In case of false return, it may be updated to point to a failure message.
 * @return Returns whether class/method/field is fully resolved.
 */
bool resolveReferences (const Method *method, const DecodedInstruction *insn, const char **failureMessage)
{
    //Check if resolved and resolve if not
    bool resolved = dvmCompilerCheckResolvedReferences (method, insn, true);

    if (resolved == false && failureMessage != 0)
    {
        *failureMessage = "references could not be resolved";
    }

    return resolved;
}

/**
 * Used to split a basic block into two, thus creating a new BB in the cUnit
 * @see Compiler.h
 */
BasicBlock *dvmCompilerSplitBlock (GrowableList *blockList,
                                   MIR *mirToSplitAt,
                                   BasicBlock *origBlock,
                                   BasicBlock **immedPredBlockP)
{
    /* The first instruction of the new block is the mirToSplitAt */
    MIR *insn = mirToSplitAt;

    if (insn == 0)
    {
        //We weren't able to determine a split point so we have no immediate block
        if (immedPredBlockP != 0)
        {
            *immedPredBlockP = 0;
        }

        //If we have no MIR to split at, then we have no work to do
        return 0;
    }

    /* Create a new block for bottom */
    BasicBlock *bottomBlock = dvmCompilerNewBBinList (*blockList, kDalvikByteCode);

    /* Update the offset for the block */
    bottomBlock->startOffset = insn->offset;

    /*
     * Copy the write back requests from parent in case they have already been generated.
     * We simply copy because the new block contains a subset of the origBlock's instructions
     * and therefore it is okay to request same set for writeback.
     * TODO: It would be better to actually recalculate the writeback requests at the
     * end of every pass but registerization currently depends on these requests.
     */
    dvmCopyBitVector (bottomBlock->requestWriteBack, origBlock->requestWriteBack);

    /* Move all required mirs to the new block */
    dvmCompilerMoveLinkedMIRsAfter (bottomBlock, 0, insn);

    /* Take origBlock's taken and make it taken of bottomBlock */
    dvmCompilerReplaceChildBasicBlock (origBlock->taken, bottomBlock, kChildTypeTaken);

    /* Make the taken for origBlock be null */
    dvmCompilerReplaceChildBasicBlock (0, origBlock, kChildTypeTaken);

    /* Take origBlock's fallthrough and make it fallthrough of bottomBlock */
    dvmCompilerReplaceChildBasicBlock (origBlock->fallThrough, bottomBlock, kChildTypeFallthrough);

    /* Make origBlock's fallthrough be the newly created bottomBlock */
    dvmCompilerReplaceChildBasicBlock (bottomBlock, origBlock, kChildTypeFallthrough);

    /* The original block needs a fallthrough branch now and new bottom block must respect previous decision */
    bottomBlock->needFallThroughBranch = origBlock->needFallThroughBranch;
    origBlock->needFallThroughBranch = true;

    /* Handle the successor list */
    if (origBlock->successorBlockList.blockListType != kNotUsed) {
        bottomBlock->successorBlockList = origBlock->successorBlockList;
        origBlock->successorBlockList.blockListType = kNotUsed;
        GrowableListIterator iterator;

        dvmGrowableListIteratorInit(&bottomBlock->successorBlockList.blocks,
                                    &iterator);
        while (true) {
            SuccessorBlockInfo *successorBlockInfo =
                (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);
            if (successorBlockInfo == NULL) break;
            BasicBlock *bb = successorBlockInfo->block;
            if (bb != 0)
            {
                dvmCompilerClearBit(bb->predecessors, origBlock->id);
                dvmCompilerSetBit(bb->predecessors, bottomBlock->id);
            }
        }
    }

    /*
     * Update the immediate predecessor block pointer so that outgoing edges
     * can be applied to the proper block.
     */
    if (immedPredBlockP) {
        assert(*immedPredBlockP == origBlock);
        *immedPredBlockP = bottomBlock;
    }
    return bottomBlock;
}

/**
 * @brief Splits an existing block from the specified code offset into two.
 * @param blockList The list of blocks in CFG
 * @param codeOffset The code offset to include in the new block
 * @param origBlock The original block which to split
 * @param immedPredBlockP If non-null, updated to point to newly created block after split
 * @return Returns the newly created block after the split.
 */
static BasicBlock *splitBlock(GrowableList *blockList,
                              unsigned int codeOffset,
                              BasicBlock *origBlock,
                              BasicBlock **immedPredBlockP)
{
    //Get the block's first instruction
    MIR *insn = origBlock->firstMIRInsn;

    //Iterate through instruction until we find the one with the desired offset
    while (insn != 0)
    {
        if (insn->offset == codeOffset)
        {
            break;
        }

        insn = insn->next;
    }

    //Now do the actual split
    return dvmCompilerSplitBlock (blockList, insn, origBlock, immedPredBlockP);
}

/*
 * Given a code offset, find out the block that starts with it. If the offset
 * is in the middle of an existing block, split it into two. If immedPredBlockP
 * is non-null and is the block being split, update *immedPredBlockP to point
 * to the bottom block so that outgoing edges can be setup properly (by the
 * caller).
 */
static BasicBlock *findBlock(GrowableList *blockList,
                             unsigned int codeOffset,
                             bool split, bool create,
                             BasicBlock **immedPredBlockP)
{
    BasicBlock *bb;
    unsigned int i;

    for (i = 0; i < blockList->numUsed; i++) {
        bb = (BasicBlock *) blockList->elemList[i];
        if (bb->blockType != kDalvikByteCode) continue;
        if (bb->startOffset == codeOffset) return bb;
        /* Check if a branch jumps into the middle of an existing block */
        if ((split == true) && (codeOffset > bb->startOffset) &&
            (bb->lastMIRInsn != NULL) &&
            (codeOffset <= bb->lastMIRInsn->offset)) {
            BasicBlock *newBB = splitBlock(blockList, codeOffset, bb,
                                           bb == *immedPredBlockP ?
                                               immedPredBlockP : NULL);
            return newBB;
        }
    }
    if (create == true)
    {
        bb = dvmCompilerNewBBinList (*blockList, kDalvikByteCode);
        bb->startOffset = codeOffset;
        return bb;
    }
    return NULL;
}

/**
  * @brief Request a File creation with a given name
  * @param cUnit the CompilationUnit
  * @param dirPrefix the directory prefix to be used
  * @param suffix a suffix string for the file name
  * @return returns a FILE or NULL if creation failed
  */
static FILE *dvmCompilerDumpGetFile (CompilationUnit *cUnit, const char *dirPrefix, const char *suffix = "")
{
    const Method *method;
    char *signature;
    char id[80];
    char startOffset[80];
    char *fileName;
    int length;

    //Not thread safe
    static int cnt = 0;

    //Start with some paranoid handling:
    if (cUnit == NULL || cUnit->method == NULL || cUnit->method->clazz == NULL)
        return NULL;

    //Create the filename
    method = cUnit->method;
    signature = dexProtoCopyMethodDescriptor(&method->prototype);

    //Add unique counter, and increment it
    snprintf (id, sizeof (id), "_%d", cnt);
    cnt++;

    //Also get minimim startOffset: no offset can be UINT_MAX so it will be our start test
    unsigned int minOffset = UINT_MAX;

    //Iterate through them
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    while (true) {
        BasicBlock *bbscan = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bbscan == NULL) break;

        //We only care about code blocks for this
        if (bbscan->blockType != kDalvikByteCode)
        {
            continue;
        }

        //Get offset
        unsigned int tmpOffset = bbscan->startOffset;

        if (minOffset > tmpOffset)
        {
            minOffset = tmpOffset;
        }
    }

    snprintf (startOffset, sizeof (startOffset), "_%x", minOffset);

    length = strlen(dirPrefix) +
                 strlen(method->clazz->descriptor) +
                 strlen(method->name) +
                 strlen(signature) +
                 strlen(id) +
                 strlen(startOffset) +
                 strlen(suffix) +
                 strlen(".dot") + 1;
    fileName = (char *) dvmCompilerNew(length, true);

    snprintf(fileName, length, "%s%s%s%s%s%s%s.dot", dirPrefix,
            method->clazz->descriptor, method->name, signature, id, startOffset, suffix);
    free(signature), signature = NULL;

    /*
     * Convert the special characters into a filesystem- and shell-friendly
     * format.
     */
    int i;
    for (i = strlen(dirPrefix); fileName[i] != '\0'; i++) {
        if (fileName[i] == '/') {
            fileName[i] = '_';
        } else if (fileName[i] == ';') {
            fileName[i] = '#';
        } else if (fileName[i] == '$') {
            fileName[i] = '+';
        } else if (fileName[i] == '(' || fileName[i] == ')') {
            fileName[i] = '@';
        } else if (fileName[i] == '<' || fileName[i] == '>') {
            fileName[i] = '=';
        }
    }

    //Now actually return the file opening
    return fopen(fileName, "w");
}

/**
 * @brief Dump an MIR
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock to dump
 * @param file the File in which to dump the BasicBlock
 */
static void dumpMIRInstructions (CompilationUnit *cUnit, const BasicBlock *bb, FILE *file)
{
    for (const MIR *mir = bb->firstMIRInsn; mir; mir = mir->next) {
        char buffer[256];
        dvmCompilerExtendedDisassembler (cUnit, mir, & (mir->dalvikInsn), buffer, sizeof (buffer));
        fprintf(file, "    {%04x %s\\l} | \\\n", mir->offset, buffer);
    }
}

#if defined(WITH_JIT_TUNING)
/*
 * @class edge
 * @brief An edge in method's CFG
 */
struct CfgEdge{
    unsigned int    startOffset;    /**< @brief The start point's bytecode offset */
    int             startValue;     /**< @brief The execution count of start bytecode */
    unsigned int    endOffset;      /**< @brief The end point's bytecode offset */
    int             endValue;       /**< @brief The execution count of end bytecode */
    int             value;          /**< @brief The execution count of the edge */
};

/**
  * @brief Get an edge's value in edges' set
  * @param edgeList the edges' set
  * @param startOffset the startOffset of the edge
  * @param endOffset the endOffset of the edge
  * @return the edge's value
  */
static int getEdgeValue(GrowableList* edgeList, unsigned int startOffset, unsigned int endOffset) {
    int value = 0;
    unsigned int idx;
    struct CfgEdge *edge;
    for (idx = 0; idx < dvmGrowableListSize(edgeList); idx++) {
        edge = (struct CfgEdge *) dvmGrowableListGetElement(edgeList, idx);
        if (edge->startOffset == startOffset && edge->endOffset == endOffset) {
            value = edge->value;
            break;
        }
    }
    return value;
}

/**
  * @brief dump an edge with value
  * @param cUnit the CompilationUnit
  * @param bb the BasicBlock
  * @param bbName the BasicBlock's name
  * @param blockName the target BasicBlock's name
  * @param file the File in which to dump the edge
  * @param isTaken is the edge a taken or a fallThrough?
  */
static void dumpEdgeWithValue(CompilationUnit *cUnit, BasicBlock *bb, char* bbName, char* blockName, FILE *file, bool isTaken) {
        int value = 0;
        BasicBlock *targetBB;
        if (isTaken == true) {
            targetBB = bb->taken;
        } else {
            targetBB = bb->fallThrough;
        }

        if (targetBB != NULL && bb->lastMIRInsn != NULL && targetBB->firstMIRInsn != NULL) {
            // Get the edges' set
            GrowableList* edgeList = static_cast<GrowableList*> (cUnit->walkData);

            // Get the value of this edge in edges' set
            if (edgeList != NULL) {
                value = getEdgeValue(edgeList, bb->lastMIRInsn->offset, targetBB->firstMIRInsn->offset);
            }
        }

        // if the edge's value is not zero, label it on the CFG
        if (value != 0) {
            if (isTaken == true) {
                fprintf(file, "  %s:s -> %s:n [style=dotted, label=\"%d\"]\n",
                        bbName, blockName, value);
            } else {
                fprintf(file, "  %s:s -> %s:n [label=\"%d\"]\n",
                        bbName, blockName, value);
            }
        } else {
            if (isTaken == true) {
                fprintf(file, "  %s:s -> %s:n [style=dotted]\n",
                        bbName, blockName);
            } else {
                fprintf(file, "  %s:s -> %s:n \n",
                        bbName, blockName);
            }
        }
}
#endif

/**
  * @brief Dump a BasicBlock
  * @param cUnit the CompilationUnit
  * @param bb the BasicBlock
  * @param file the File in which to dump the BasicBlock
  * @param dominators do we wish to dump the domination information? (default == false)
  */
void dvmDumpBasicBlock (CompilationUnit *cUnit, BasicBlock *bb, FILE *file, bool dominators = false)
{
        char blockName[BLOCK_NAME_LEN], bbName[BLOCK_NAME_LEN];

        if (bb == NULL)
            return;

        //Get BB's name
        dvmGetBlockName(bb, bbName);

        if (bb->blockType == kEntryBlock) {
            fprintf(file, "  entry [shape=Mdiamond];\n");
        } else if (bb->blockType == kExitBlock) {
            fprintf(file, "  exit [shape=Mdiamond];\n");
        } else {
            fprintf(file, "  %s [shape=record,label = \"{ \\\n",
                    bbName);

            //First print out the block id for dalvik code or the name for others
            if (bb->blockType == kDalvikByteCode) {
                // For BBs that have MIRs, print them
                fprintf(file, "    {block id %d\\l} |\\\n", bb->id);
            } else {
                // For other kinds of BBs, simply print its name
                fprintf(file, "    {%s\\l}|\\\n", bbName);
            }

            //Then dump any architecture specific information
            dvmCompilerDumpArchSpecificBB (cUnit, bb, file, true);

            //Dump live ins
            if (bb->dataFlowInfo != 0) {
                dvmDumpBitVectorDotFormat (file, "Live Ins: ", bb->dataFlowInfo->liveInV, true, false);
            }

            //Then dump the instructions if any
            dumpMIRInstructions (cUnit, bb, file);

            //Dump live outs
            if (bb->dataFlowInfo != 0) {
                dvmDumpBitVectorDotFormat (file, "Live Outs: ", bb->dataFlowInfo->liveOutV, true, false);
            }

            //Again dump any architecture specific information
            dvmCompilerDumpArchSpecificBB (cUnit, bb, file, false);

            //Finally, dump spill requests
            dvmDumpBitVectorDotFormat (file, "Write Backs: ", bb->requestWriteBack, true, true);

            fprintf(file, "  }\"];\n\n");
        }

        if (bb->taken) {
            dvmGetBlockName(bb->taken, blockName);

#if defined(WITH_JIT_TUNING)
            dumpEdgeWithValue(cUnit, bb, bbName, blockName, file, true);
#else
            fprintf(file, "  %s:s -> %s:n [style=dotted]\n",
                    bbName, blockName);
#endif

        }

        if (bb->fallThrough) {
            dvmGetBlockName(bb->fallThrough, blockName);

#if defined(WITH_JIT_TUNING)
            dumpEdgeWithValue(cUnit, bb, bbName, blockName, file, false);
#else
            fprintf(file, "  %s:s -> %s:n\n", bbName, blockName);
#endif

        }

        if (bb->successorBlockList.blockListType != kNotUsed) {
            fprintf(file, "  succ%04x [shape=%s,label = \"{ \\\n",
                    bb->id,
                    (bb->successorBlockList.blockListType == kCatch) ?
                        "Mrecord" : "record");
            GrowableListIterator iterator;
            dvmGrowableListIteratorInit(&bb->successorBlockList.blocks,
                                        &iterator);
            SuccessorBlockInfo *successorBlockInfo =
                (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);

            int succId = 0;
            while (true) {
                if (successorBlockInfo == NULL) break;

                BasicBlock *destBlock = successorBlockInfo->block;
                SuccessorBlockInfo *nextSuccessorBlockInfo =
                  (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);

                fprintf(file, "    {<f%d> %04x: %04x\\l}%s\\\n",
                        succId++,
                        successorBlockInfo->key,
                        destBlock->id,
                        (nextSuccessorBlockInfo != NULL) ? " | " : " ");

                successorBlockInfo = nextSuccessorBlockInfo;
            }
            fprintf(file, "  }\"];\n\n");

            fprintf(file, "  %s:s -> succ%04x:n [style=dashed]\n",
                    bbName, bb->id);

            if (bb->successorBlockList.blockListType == kPackedSwitch ||
                bb->successorBlockList.blockListType == kSparseSwitch) {

                dvmGrowableListIteratorInit(&bb->successorBlockList.blocks,
                                            &iterator);

                succId = 0;
                while (true) {
                    SuccessorBlockInfo *successorBlockInfo =
                        (SuccessorBlockInfo *)
                            dvmGrowableListIteratorNext(&iterator);
                    if (successorBlockInfo == NULL) break;

                    BasicBlock *destBlock = successorBlockInfo->block;

                    dvmGetBlockName(destBlock, blockName);
                    fprintf(file, "  succ%04x:f%d:e -> %s:n\n",
                            bb->id, succId++,
                            blockName);
                }
            }
        }
        fprintf(file, "\n");

        /*
         * If we need to debug the dominator tree
         */
        if (dominators == true)
        {
            fprintf(file, "  cfg%s [label=\"%s\", shape=none];\n",
                    bbName, bbName);
            if (bb->iDom) {
                dvmGetBlockName(bb->iDom, blockName);
                fprintf(file, "  cfg%s:s -> cfg%s:n\n\n",
                        blockName, bbName);
            }
        }
}

/**
  * @brief Dump the CFG into a DOT graph
  * @param cUnit the CompilationUnit
  * @param dirPrefix the directory prefix to be used
  * @param suffix a suffix string for the file name
  */
void dvmDumpCFG(CompilationUnit *cUnit, const char *dirPrefix, const char *suffix)
{
    FILE *file = dvmCompilerDumpGetFile (cUnit, dirPrefix, suffix);

    if (file == NULL)
        return;

    fprintf(file, "digraph G {\n");

    fprintf(file, "  rankdir=TB\n");

    int numReachableBlocks = cUnit->numReachableBlocks;
    int idx;
    const GrowableList *blockList = &cUnit->blockList;

    //Dump only the reachable basic blocks
    for (idx = 0; idx < numReachableBlocks; idx++) {
        int blockIdx = cUnit->dfsOrder.elemList[idx];
        BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(blockList,
                                                                  blockIdx);
        dvmDumpBasicBlock (cUnit, bb, file);
    }
    fprintf(file, "}\n");
    fclose(file);
}

/* It's ugly but it is the best method available */
static FILE *dvmCreateGraphFile = NULL;
/**
 * @brief Handler for the BasicBlock dumping into a DOT graph, if the block is visted already, do nothing
 * @param cUnit the CompilationUnit
 * @param curBlock current block to be dumped
 * @return whether or not this changes anything for the walker
 */
static bool dvmCompilerDumpBasicBlockHandler (CompilationUnit *cUnit, BasicBlock *curBlock)
{
    //Paranoid
    if (dvmCreateGraphFile == NULL)
        return false;

    //If visited, then we have already dumped it, therefore nothing to do
    if (curBlock->visited == true)
        return false;
    curBlock->visited = true;

    //Dump the basic block
    dvmDumpBasicBlock (cUnit, curBlock, dvmCreateGraphFile);

    //We did no change
    return false;
}


/**
 * @brief Dump the CFG of every BasicBlock into a DOT graph
 * @param cUnit the CompilationUnit
 * @param dirPrefix the directory prefix to be used
 * @param suffix a suffix string for the file name (default: "")
 * @param dumpLoopInformation do we dump the loop information (default: false)
 */
void dvmCompilerDumpBasicBlocks (CompilationUnit *cUnit, const char *dirPrefix, const char *suffix, bool dumpLoopInformation)
{
    //Clear visiting flags
    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
            dvmCompilerClearVisitedFlag,
            kAllNodes,
            false /* isIterative */);

    dvmCreateGraphFile = dvmCompilerDumpGetFile (cUnit, dirPrefix, suffix);

    //Paranoid
    if (dvmCreateGraphFile != NULL)
    {
        //Prepare dot header
        fprintf (dvmCreateGraphFile, "digraph G {\n");

        //Dump the basic blocks
        dvmCompilerDataFlowAnalysisDispatcher (cUnit,
                dvmCompilerDumpBasicBlockHandler,
                kAllNodes,
                false);

#ifdef ARCH_IA32
        //Do we dump the loop information?
        if (dumpLoopInformation == true)
        {
            if (cUnit->loopInformation != 0)
            {
                cUnit->loopInformation->dumpInformationDotFormat (cUnit, dvmCreateGraphFile);
            }
        }
#endif

        //Print out epilogue
        fprintf (dvmCreateGraphFile, "}\n");

        //Close file
        fclose (dvmCreateGraphFile), dvmCreateGraphFile = NULL;
    }

    //Clear visiting flags
    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
            dvmCompilerClearVisitedFlag,
            kAllNodes,
            false /* isIterative */);
}


/* Verify if all the successor is connected with all the claimed predecessors */
static bool verifyPredInfo(CompilationUnit *cUnit, BasicBlock *bb)
{
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    while (true) {
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);
        if (blockIdx == -1) break;
        BasicBlock *predBB = (BasicBlock *)
            dvmGrowableListGetElement(&cUnit->blockList, blockIdx);
        bool found = false;
        if (predBB->taken == bb) {
            found = true;
        } else if (predBB->fallThrough == bb) {
            found = true;
        } else if (predBB->successorBlockList.blockListType != kNotUsed) {
            GrowableListIterator iterator;
            dvmGrowableListIteratorInit(&predBB->successorBlockList.blocks,
                                        &iterator);
            while (true) {
                SuccessorBlockInfo *successorBlockInfo =
                    (SuccessorBlockInfo *)
                        dvmGrowableListIteratorNext(&iterator);
                if (successorBlockInfo == NULL) break;
                BasicBlock *succBB = successorBlockInfo->block;
                if (succBB == bb) {
                    found = true;
                    break;
                }
            }
        }
        if (found == false) {
            char blockName1[BLOCK_NAME_LEN], blockName2[BLOCK_NAME_LEN];
            dvmGetBlockName(bb, blockName1);
            dvmGetBlockName(predBB, blockName2);
            dvmDumpCFG(cUnit, "/sdcard/cfg/");
            ALOGE("Successor %s not found from %s",
                 blockName1, blockName2);
            dvmAbort();
        }
    }
    return true;
}

/* Identify code range in try blocks and set up the empty catch blocks */
static void processTryCatchBlocks (const Method *meth, GrowableList *blockList, BitVector *tryBlockAddr)
{
    const DexCode *pCode = dvmGetMethodCode(meth);
    int triesSize = pCode->triesSize;
    int i;
    int offset;

    if (triesSize == 0) {
        return;
    }

    const DexTry *pTries = dexGetTries(pCode);

    /* Mark all the insn offsets in Try blocks */
    for (i = 0; i < triesSize; i++) {
        const DexTry* pTry = &pTries[i];
        /* all in 16-bit units */
        int startOffset = pTry->startAddr;
        int endOffset = startOffset + pTry->insnCount;

        for (offset = startOffset; offset < endOffset; offset++) {
            dvmCompilerSetBit(tryBlockAddr, offset);
        }
    }

    /* Iterate over each of the handlers to enqueue the empty Catch blocks */
    offset = dexGetFirstHandlerOffset(pCode);
    int handlersSize = dexGetHandlersSize(pCode);

    for (i = 0; i < handlersSize; i++) {
        DexCatchIterator iterator;
        dexCatchIteratorInit(&iterator, pCode, offset);

        for (;;) {
            DexCatchHandler* handler = dexCatchIteratorNext(&iterator);

            if (handler == NULL) {
                break;
            }

            /*
             * Create dummy catch blocks first. Since these are created before
             * other blocks are processed, "split" is specified as false.
             */
            findBlock(blockList, handler->address,
                      /* split */
                      false,
                      /* create */
                      true,
                      /* immedPredBlockP */
                      NULL);
        }

        offset = dexCatchIteratorGetEndOffset(&iterator, pCode);
    }
}

/* Process instructions with the kInstrCanBranch flag */
static void processCanBranch(GrowableList *blockList, BasicBlock **curBlockPtr,
                             MIR *insn, int curOffset, int width, int flags,
                             const u2* codePtr, const u2* codeEnd)
{
    int target = curOffset;
    switch (insn->dalvikInsn.opcode) {
        case OP_GOTO:
        case OP_GOTO_16:
        case OP_GOTO_32:
            target += (int) insn->dalvikInsn.vA;
            break;
        case OP_IF_EQ:
        case OP_IF_NE:
        case OP_IF_LT:
        case OP_IF_GE:
        case OP_IF_GT:
        case OP_IF_LE:
            target += (int) insn->dalvikInsn.vC;
            break;
        case OP_IF_EQZ:
        case OP_IF_NEZ:
        case OP_IF_LTZ:
        case OP_IF_GEZ:
        case OP_IF_GTZ:
        case OP_IF_LEZ:
            target += (int) insn->dalvikInsn.vB;
            break;
        default:
            ALOGE("Unexpected opcode(%d) with kInstrCanBranch set",
                 insn->dalvikInsn.opcode);
            dvmAbort();
    }
    BasicBlock *takenBlock = findBlock(blockList, target,
                                       /* split */
                                       true,
                                       /* create */
                                       true,
                                       /* immedPredBlockP */
                                       curBlockPtr);

    //Make the new takenBlock be the taken path of curBlock
    dvmCompilerReplaceChildBasicBlock (takenBlock, *curBlockPtr, kChildTypeTaken);

    /* Always terminate the current block for conditional branches */
    if (flags & kInstrCanContinue) {
        BasicBlock *fallthroughBlock = findBlock(blockList,
                                                 curOffset +  width,
                                                 /*
                                                  * If the method is processed
                                                  * in sequential order from the
                                                  * beginning, we don't need to
                                                  * specify split for continue
                                                  * blocks. However, this
                                                  * routine can be called by
                                                  * compileLoop, which starts
                                                  * parsing the method from an
                                                  * arbitrary address in the
                                                  * method body.
                                                  */
                                                 true,
                                                 /* create */
                                                 true,
                                                 /* immedPredBlockP */
                                                 curBlockPtr);

        //Make the fallthroughBlock be the fallthrough path of curBlock
        dvmCompilerReplaceChildBasicBlock (fallthroughBlock, *curBlockPtr, kChildTypeFallthrough);

    } else if (codePtr < codeEnd) {
        /* Create a fallthrough block for real instructions (incl. OP_NOP) */
        if (contentIsInsn (codePtr) == true) {
            findBlock(blockList, curOffset + width,
                      /* split */
                      false,
                      /* create */
                      true,
                      /* immedPredBlockP */
                      NULL);
        }
    }
}

/* Process instructions with the kInstrCanSwitch flag */
static void processCanSwitch(GrowableList *blockList, BasicBlock **curBlockPtr,
                             MIR *insn, const u2 *baseInsnsAddr, int curOffset,
                             int width, int flags)
{
    u2 *switchData= (u2 *) (baseInsnsAddr + curOffset +
                            insn->dalvikInsn.vB);
    int size;
    int *keyTable;
    int *targetTable;
    int i;
    int firstKey;

    /*
     * Packed switch data format:
     *  ushort ident = 0x0100   magic value
     *  ushort size             number of entries in the table
     *  int first_key           first (and lowest) switch case value
     *  int targets[size]       branch targets, relative to switch opcode
     *
     * Total size is (4+size*2) 16-bit code units.
     */
    if (insn->dalvikInsn.opcode == OP_PACKED_SWITCH) {
        assert(switchData[0] == kPackedSwitchSignature);
        size = switchData[1];
        firstKey = switchData[2] | (switchData[3] << 16);
        targetTable = (int *) &switchData[4];
        keyTable = NULL;        // Make the compiler happy
    /*
     * Sparse switch data format:
     *  ushort ident = 0x0200   magic value
     *  ushort size             number of entries in the table; > 0
     *  int keys[size]          keys, sorted low-to-high; 32-bit aligned
     *  int targets[size]       branch targets, relative to switch opcode
     *
     * Total size is (2+size*4) 16-bit code units.
     */
    } else {
        assert(switchData[0] == kSparseSwitchSignature);
        size = switchData[1];
        keyTable = (int *) &switchData[2];
        targetTable = (int *) &switchData[2 + size*2];
        firstKey = 0;   // To make the compiler happy
    }

    if ((*curBlockPtr)->successorBlockList.blockListType != kNotUsed) {
        ALOGE("Successor block list already in use: %d",
                (*curBlockPtr)->successorBlockList.blockListType);
        dvmAbort();
    }
    (*curBlockPtr)->successorBlockList.blockListType =
        (insn->dalvikInsn.opcode == OP_PACKED_SWITCH) ?
        kPackedSwitch : kSparseSwitch;
    dvmInitGrowableList(&((*curBlockPtr)->successorBlockList.blocks), size);

    for (i = 0; i < size; i++) {
        BasicBlock *caseBlock = findBlock(blockList, curOffset + targetTable[i],
                                          /* split */
                                          true,
                                          /* create */
                                          true,
                                          /* immedPredBlockP */
                                          curBlockPtr);

        //We should always have a block, especially since we pass argument to create one
        assert (caseBlock != 0);

        //However, we still need to check here that we have a block because we will be
        //dereferencing it.
        if (caseBlock != 0)
        {
            SuccessorBlockInfo *successorBlockInfo =
                (SuccessorBlockInfo *) dvmCompilerNew(sizeof(SuccessorBlockInfo),
                                                      false);
            successorBlockInfo->block = caseBlock;
            successorBlockInfo->key = (insn->dalvikInsn.opcode == OP_PACKED_SWITCH)?
                                      firstKey + i : keyTable[i];
            dvmInsertGrowableList(&((*curBlockPtr)->successorBlockList.blocks),
                                  (intptr_t) successorBlockInfo);
            dvmCompilerSetBit(caseBlock->predecessors, (*curBlockPtr)->id);
        }
    }

    /* Fall-through case */
    BasicBlock *fallthroughBlock = findBlock(blockList,
                                             curOffset +  width,
                                             /* split */
                                             false,
                                             /* create */
                                             true,
                                             /* immedPredBlockP */
                                             NULL);
    (*curBlockPtr)->fallThrough = fallthroughBlock;
    dvmCompilerSetBit(fallthroughBlock->predecessors, (*curBlockPtr)->id);
}

/* Process instructions with the kInstrCanThrow flag */
static void processCanThrow(const Method *method, GrowableList *blockList,
                            BasicBlock *curBlock,
                            MIR *insn, int curOffset, int width, int flags,
                            BitVector *tryBlockAddr, const u2 *codePtr,
                            const u2* codeEnd)
{
    const DexCode *dexCode = dvmGetMethodCode(method);

    /* In try block */
    if (dvmIsBitSet(tryBlockAddr, curOffset)) {
        DexCatchIterator iterator;

        if (!dexFindCatchHandler(&iterator, dexCode, curOffset)) {
            ALOGE("Catch block not found in dexfile for insn %x in %s",
                 curOffset, method->name);
            dvmAbort();

        }
        if (curBlock->successorBlockList.blockListType != kNotUsed) {
            ALOGE("Successor block list already in use: %d",
                 curBlock->successorBlockList.blockListType);
            dvmAbort();
        }
        curBlock->successorBlockList.blockListType = kCatch;
        dvmInitGrowableList(&curBlock->successorBlockList.blocks, 2);

        for (;;) {
            DexCatchHandler* handler = dexCatchIteratorNext(&iterator);

            if (handler == NULL) {
                break;
            }

            BasicBlock *catchBlock = findBlock(blockList, handler->address,
                                               /* split */
                                               false,
                                               /* create */
                                               false,
                                               /* immedPredBlockP */
                                               NULL);

            SuccessorBlockInfo *successorBlockInfo =
              (SuccessorBlockInfo *) dvmCompilerNew(sizeof(SuccessorBlockInfo),
                                                    false);
            successorBlockInfo->block = catchBlock;
            successorBlockInfo->key = handler->typeIdx;
            dvmInsertGrowableList(&curBlock->successorBlockList.blocks,
                                  (intptr_t) successorBlockInfo);
            dvmCompilerSetBit(catchBlock->predecessors, curBlock->id);
        }
    } else {
        BasicBlock *ehBlock = dvmCompilerNewBBinList (*blockList, kExceptionHandling);
        curBlock->taken = ehBlock;
        ehBlock->startOffset = curOffset;
        dvmCompilerSetBit(ehBlock->predecessors, curBlock->id);
    }

    /*
     * Force the current block to terminate.
     *
     * Data may be present before codeEnd, so we need to parse it to know
     * whether it is code or data.
     */
    if (codePtr < codeEnd) {
        /* Create a fallthrough block for real instructions (incl. OP_NOP) */
        if (contentIsInsn (codePtr) == true) {
            BasicBlock *fallthroughBlock = findBlock(blockList,
                                                     curOffset + width,
                                                     /* split */
                                                     false,
                                                     /* create */
                                                     true,
                                                     /* immedPredBlockP */
                                                     NULL);
            /*
             * OP_THROW and OP_THROW_VERIFICATION_ERROR are unconditional
             * branches.
             */
            if (insn->dalvikInsn.opcode != OP_THROW_VERIFICATION_ERROR &&
                insn->dalvikInsn.opcode != OP_THROW) {
                curBlock->fallThrough = fallthroughBlock;
                dvmCompilerSetBit(fallthroughBlock->predecessors, curBlock->id);
            }
        }
    }
}

#if defined(WITH_JIT_TUNING)
/* (documented in header file) */
int dvmCompilerDumpMethodCFGHandle(void* data, void* arg)
{
    struct Method* method = (struct Method*)data;
    dvmCompilerDumpMethodCFG(method, method->profileTable);
    return 0;
}

/**
 * @brief Add an edge into the edges' set
 * @param edgeList the edges' set
 * @param startMir the MIR of the edge's startOffset
 * @param endMir the MIR of the edge's endOffset
 * @param profileTable the method's profile table
 */
static void addEdge(GrowableList &edgeList, MIR* startMir, MIR* endMir, int* profileTable) {
    struct CfgEdge *edge = (struct CfgEdge *) dvmCompilerNew(sizeof(struct CfgEdge), true);
    edge->startOffset = startMir->offset;
    edge->startValue = profileTable[edge->startOffset];
    edge->endOffset = endMir->offset;
    edge->endValue = profileTable[edge->endOffset];
    edge->value = -1;
    dvmInsertGrowableList(&edgeList, (intptr_t) edge);
}

/* (documented in header file) */
void dvmCompilerDumpMethodCFG(const Method* method, int* profileTable)
{
    CompilationUnit cUnit;
    GrowableListIterator iterator;
    GrowableListIterator iterator1;
    BasicBlock *bb;

    unsigned int idx;
    struct CfgEdge *edge;
    struct CfgEdge *edge1;
    GrowableList edgeList;

    bool singleOut;         //Flags to indicate if the edge being processed shared the start point (startOffset) with other edges, true for not share
    bool singleIn;          //Flags to indicate if the edge being processed shared the end point (endOffset) with other edges, true for not share
    bool change = true;     //Whether we need process the edges set one more time

    /* Initialize cUnit */
    memset(&cUnit, 0, sizeof(cUnit));
    cUnit.method = method;

    /* Initialize the block list */
    dvmInitGrowableList(&cUnit.blockList, 4);

    /* Build CGF for the method */
    bool createdCFG = dvmCompilerBuildCFG (cUnit.method, &cUnit.blockList);

    if (createdCFG == false)
    {
        return;
    }

    /* We need calculate each edge's execution count from profileTable, and then lable the value of each edge on CFG.
     * Bellow is the process:
     * 1. Initlize the edges set.
     * 2. Find the method's edges and filled the edges into edges set.
     *    Now only conside the "tabken" and "fallThrough" edges.
     *    There are 5 elements for an edge, startOffset, endOffset, startValue, endValue and value.
     *    startOffset is the basicblock's lastMIRInsn offset and endOffset is the taken bb's or fallThrough bb's firstMIRInsn offset.
     *    startValue and endValue is the execution count of the correspongding Insn which can be get from profileTable.
     *    value stands for edge's execution count, which is set to -1 at the first.
     * 3. Calculate the value of each edge in the edges set.
     *    Two rules that can guide us to evaluate the value of each edge:
     *        i.    if the edge does not share startOffset with other edge, then : edge.value = edge.startValue
     *              if the edge does not share endOffset with other edge, the : edge.value = edge.endValue
     *        ii.   Once we can confirm a edge's value, say edgeA, the edges set can be updated as bellow:
     *              a.  For each edge that share startOffset with this edge, say edgeB, then : edgeB.startValue = edgeB.startValue - edgeA.value
     *              b.  For each edge that share endOffset with this edge, say edgeB, then : edgeB.endValue = edgeB.endValue - edgeA.value
     *              c.  Remove the edgeA from the edges set.
     *     We can apply the two rules on the edges set, until all the edge is empty. Then we get all the edges's value been fixed.
     */

    // Initialize the edges set
    dvmInitGrowableList(&edgeList, 8);

    // Pass the edges set to dvmCompilerDumpBasicBlocks via Cunit's walkData
    cUnit.walkData = static_cast<void *> (&edgeList);

    // Fill up the edges set with method's edges, after this, each edge's startOffset, startValue, endOffset, endValue is set, and value is -1;
    dvmGrowableListIteratorInit(&cUnit.blockList, &iterator);
    for (bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator));
         bb != NULL;
         bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator))) {

        if (bb->taken != NULL && bb->lastMIRInsn != NULL && bb->taken->firstMIRInsn != NULL) {
            addEdge(edgeList, bb->lastMIRInsn, bb->taken->firstMIRInsn, profileTable);
        }
        if (bb->fallThrough != NULL && bb->lastMIRInsn != NULL && bb->fallThrough->firstMIRInsn != NULL) {
            addEdge(edgeList, bb->lastMIRInsn, bb->fallThrough->firstMIRInsn, profileTable);
        }
    }

    // Process the edges set and try to fix the value for each edge according to the rules described above.
    // Actually we do not really remove the fixed edges from set, but just ignore the edges if the value has been set.
    // If there is a edge's value being fixed, then we process the edges set one more time. Same as check if the set is empty
    while (change == true) {
        change = false;

        dvmGrowableListIteratorInit(&edgeList, &iterator);
        for (edge = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator));
             edge != NULL;
             edge = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator))) {

            if (edge == NULL) {
                break;
            }
            if (edge->value != -1) {
                continue;
            }

            singleOut = true;
            singleIn = true;

            // Check if this edge shared startOffset or endOffset with other edges in the set, and set the flag
            dvmGrowableListIteratorInit(&edgeList, &iterator1);
            for (edge1 = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator1));
                 edge1 != NULL;
                 edge1 = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator1))) {

                if (edge1->value != -1 || edge == edge1) {
                    continue;
                }

                if (edge->startOffset == edge1->startOffset) {
                    singleOut = false;
                }

                if (edge->endOffset == edge1->endOffset) {
                    singleIn = false;
                }
            }

            // This edge does not share startOffset with others, set the value and update edges set
            if (singleOut == true) {
                edge->value = edge->startValue;

                dvmGrowableListIteratorInit(&edgeList, &iterator1);
                for (edge1 = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator1));
                     edge1 != NULL;
                     edge1 = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator1))) {

                    if (edge1->value != -1) {
                        continue;
                    }

                    if (edge->endOffset == edge1->endOffset) {
                        edge1->endValue -= edge->startValue;
                    }
                }
                change = true;
                continue;
            }

            // This edge does not share endOffset with others, set the value and update edges set
            if (singleIn == true) {
                edge->value = edge->endValue;

                dvmGrowableListIteratorInit(&edgeList, &iterator1);
                for (edge1 = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator1));
                     edge1 != NULL;
                     edge1 = (struct CfgEdge *) (dvmGrowableListIteratorNext(&iterator1))) {

                    if (edge1->value != -1) {
                        continue;
                    }
                    if (edge->startOffset == edge1->startOffset) {
                        edge1->startValue -= edge->endValue;
                    }
                }
                change = true;
                continue;
            }
        }
    }

    // Finally, we dump the method CFG
    // This is the default path where the cfg files will be placed, can be changed by gDvmJit.cfgDirPrefix (-Xjitmethodprofileprefix)
    const char* dirPrefix = "/sdcard/cfg/method/";
    if (gDvmJit.cfgDirPrefix != NULL) {
        dirPrefix = gDvmJit.cfgDirPrefix;
    }

    dvmCompilerDumpBasicBlocks(&cUnit, dirPrefix);

    // Just for safety, reset walkData
    cUnit.walkData = 0;
}
#endif

/**
 * Decodes methods and creates control flow graph for it with single entry and single exit.
 * @see Compiler.h
 */
bool dvmCompilerBuildCFG (const Method *method, GrowableList *blockList, BasicBlock **entry, BasicBlock **exit,
        BitVector *tryBlockAddr, bool (*bytecodeGate) (const Method *, const DecodedInstruction *, const char **))
{
    /* Initialize variables */
    const DexCode *dexCode = dvmGetMethodCode (method);
    const u2 *codePtr = dexCode->insns;
    const u2 *codeEnd = dexCode->insns + dexCode->insnsSize;
    unsigned int curOffset = 0;

    /* Create the default entry and exit blocks and enter them to the list */
    BasicBlock *entryBlock = dvmCompilerNewBBinList (*blockList, kEntryBlock);
    if (entry != 0)
    {
        *entry = entryBlock;
    }

    BasicBlock *exitBlock = dvmCompilerNewBBinList (*blockList, kExitBlock);
    if (exit != 0)
    {
        *exit = exitBlock;
    }

    /* Create initial block to record parsed instructions */
    BasicBlock *curBlock = dvmCompilerNewBBinList (*blockList, kDalvikByteCode);
    dvmCompilerReplaceChildBasicBlock (curBlock, entryBlock, kChildTypeFallthrough);

    /* Parse all instructions and put them into containing basic blocks */
    while (codePtr < codeEnd) {
        /* Parse instruction */
        DecodedInstruction dalvikInsn;
        int width = parseInsn (codePtr, &dalvikInsn, false);

        /* Terminate when the data section is seen */
        if (width == 0)
            break;

        /* Set up MIR */
        MIR *insn = dvmCompilerNewMIR ();
        insn->dalvikInsn = dalvikInsn;
        insn->offset = curOffset;
        insn->width = width;

        /* Keep track which method this MIR is from. Initially we assume that there is no method nesting caused by inlining */
        insn->nesting.sourceMethod = method;

        if (bytecodeGate != 0)
        {
            bool accept = bytecodeGate (method, &insn->dalvikInsn, 0);

            /* If the bytecode gate supplied does not accept this bytecode, then we reject */
            if (accept == false)
            {
                return false;
            }
        }

        dvmCompilerAppendMIR (curBlock, insn);

        codePtr += width;
        int flags = dexGetFlagsFromOpcode(insn->dalvikInsn.opcode);

        //Handle case when instruction can branch
        if ((flags & kInstrCanBranch) != 0)
        {
            processCanBranch (blockList, &curBlock, insn, curOffset, width, flags,
                             codePtr, codeEnd);
        }
        //Handle case when we can throw and have try/catch information
        else if ((flags & kInstrCanThrow) != 0 && tryBlockAddr != 0)
        {
            processCanThrow (method, blockList, curBlock, insn, curOffset, width, flags,
                             tryBlockAddr, codePtr, codeEnd);
        }
        //Handle case when instruction can return or unconditionally throw
        else if (((flags & kInstrCanReturn) != 0) || (flags == kInstrCanThrow))
        {
            dvmCompilerReplaceChildBasicBlock (exitBlock, curBlock, kChildTypeFallthrough);

            /*
             * Terminate the current block if there are instructions
             * afterwards.
             */
            if (codePtr < codeEnd) {
                /*
                 * Create a fallthrough block for real instructions
                 * (incl. OP_NOP).
                 */
                if (contentIsInsn (codePtr) == true) {
                    findBlock (blockList, curOffset + width,
                               /* split */
                               false,
                               /* create */
                               true,
                               /* immedPredBlockP */
                               NULL);
                }
            }
        }
        //Handle case when instruction can switch
        else if ((flags & kInstrCanSwitch) != 0)
        {
            processCanSwitch (blockList, &curBlock, insn, method->insns, curOffset, width, flags);
        }

        curOffset += width;

        BasicBlock *nextBlock = findBlock (blockList, curOffset,
                                           /* split */
                                           false,
                                           /* create */
                                           false,
                                           /* immedPredBlockP */
                                           NULL);

        if (nextBlock) {
            /*
             * The next instruction could be the target of a previously parsed
             * forward branch so a block is already created. If the current
             * instruction is not an unconditional branch, connect them through
             * the fall-through link.
             */
            assert(curBlock->fallThrough == NULL ||
                   curBlock->fallThrough == nextBlock ||
                   curBlock->fallThrough == exitBlock);

            if ((curBlock->fallThrough == NULL) && (flags & kInstrCanContinue))
            {
                dvmCompilerReplaceChildBasicBlock (nextBlock, curBlock, kChildTypeFallthrough);
            }

            curBlock = nextBlock;
        }
    }

    /* Building CFG succeeded */
    return true;
}

bool dvmCompilerFillCUnitWithMethodData(CompilationUnit &cUnit, const Method *method, bool needTryCatchBlocks) {
    //Clear the cUnit to begin with
    memset(&cUnit, 0, sizeof(cUnit));
    cUnit.method = method;

    const DexCode *dexCode = dvmGetMethodCode(method);

    cUnit.jitMode = kJitMethod;

    //Set up the jit verbose infrastructure
    std::vector<std::pair<BBType, char*> > code_block_table;
    cUnit.code_block_table = &code_block_table;

    /* Initialize the block list */
    dvmInitGrowableList(&cUnit.blockList, 4);

    /*
     * FIXME - PC reconstruction list won't be needed after the codegen routines
     * are enhanced to true method mode.
     */
    /* Initialize the PC reconstruction list */
    dvmInitGrowableList(&cUnit.pcReconstructionList, 8);

    cUnit.tryBlockAddr = 0;

    // See if we need to process exception blocks
    if (needTryCatchBlocks == true ) {
        /* Allocate the bit-vector to track the beginning of basic blocks */
        BitVector *tryBlockAddr = dvmCompilerAllocBitVector(dexCode->insnsSize,
                true /* expandable */);
        cUnit.tryBlockAddr = tryBlockAddr;

        /* Identify code range in try blocks and set up the empty catch blocks */
        processTryCatchBlocks (cUnit.method, &cUnit.blockList, cUnit.tryBlockAddr);
    }

    /* Build CGF for the method */
    bool createdCFG = dvmCompilerBuildCFG (cUnit.method, &cUnit.blockList,
            &cUnit.entryBlock, &cUnit.exitBlock, cUnit.tryBlockAddr, 0);

    if (createdCFG == false)
    {
        return false;
    }

    /* Now that we finished inserting blocks, let's update the number of blocks in cUnit */
    cUnit.numBlocks = dvmGrowableListSize (&cUnit.blockList);

    const int numDalvikRegisters = cUnit.method->registersSize;
    dvmCompilerUpdateCUnitNumDalvikRegisters (&cUnit, numDalvikRegisters);

    /* Verify if all blocks are connected as claimed */
    /* FIXME - to be disabled in the future */
    dvmCompilerDataFlowAnalysisDispatcher(&cUnit, verifyPredInfo,
                                          kAllNodes,
                                          false /* isIterative */);

    return true;
}

/*
 * Similar to dvmCompileTrace, but the entity processed here is the whole
 * method.
 *
 * TODO: implementation will be revisited when the trace builder can provide
 * whole-method traces.
 */
bool dvmCompileMethod(const Method *method, JitTranslationInfo *info)
{
    const DexCode *dexCode = dvmGetMethodCode(method);
    const u2 *codePtr = dexCode->insns;

    /* Method already compiled */
    if (dvmJitGetMethodAddr(codePtr) != 0) {
        info->codeAddress = 0;
        return false;
    }

    CompilationUnit cUnit;

    bool success = dvmCompilerFillCUnitWithMethodData(cUnit, method, true);

    if (success == false) {
        return success;
    }

    /*
     * We want to allocate the constantValues and degeneratePhiMap maps on stack
     * together with the cUnit, so that both are destroyed together and we don't
     * have to handle that. For this reason, it is not filled in the
     * dvmCompilerFillCUnitWithMethodData
     */
    std::map<int, int> constantValues;
    cUnit.constantValues = &constantValues;

    std::map<int, int> degeneratePhiMap;
    cUnit.degeneratePhiMap = &degeneratePhiMap;

    /* Perform SSA transformation for the whole method */
    dvmCompilerMethodSSATransformation(&cUnit);

#ifndef ARCH_IA32
    dvmCompilerInitializeRegAlloc(&cUnit);  // Needs to happen after SSA naming

    /* Allocate Registers using simple local allocation scheme */
    dvmCompilerLocalRegAlloc(&cUnit);
#endif

    /* Before lowering, print out the compilation unit */
    if (cUnit.printMe == true) {
        dvmCompilerDumpCompilationUnit(&cUnit);
    }

    /* Convert MIR to LIR, etc. */
    dvmCompilerMethodMIR2LIR(&cUnit);

    // Debugging only
    //dvmDumpCFG(&cUnit, "/sdcard/cfg/");

    /* Method is not empty */
    if (cUnit.firstLIRInsn) {
        /* Convert LIR into machine code. Loop for recoverable retries */
        do {
            dvmCompilerAssembleLIR(&cUnit, info);
            cUnit.assemblerRetries++;
            if (cUnit.printMe && cUnit.assemblerStatus != kSuccess)
                ALOGD("Assembler abort #%d on %d",cUnit.assemblerRetries,
                      cUnit.assemblerStatus);
        } while (cUnit.assemblerStatus == kRetryAll);

        if (cUnit.printMe) {
            dvmCompilerCodegenDump(&cUnit);
        }

        if (info->codeAddress) {
            dvmJitSetCodeAddr(dexCode->insns, info->codeAddress,
                              info->instructionSet, true, 0);
            /*
             * Clear the codeAddress for the enclosing trace to reuse the info
             */
            info->codeAddress = NULL;
        }
    }

    return false;
}

/* Extending the trace by crawling the code from curBlock */
static bool exhaustTrace(CompilationUnit *cUnit, BasicBlock *curBlock)
{
    unsigned int curOffset = curBlock->startOffset;
    const u2 *codePtr = cUnit->method->insns + curOffset;

    if (curBlock->visited == true) return false;

    curBlock->visited = true;

    if (curBlock->blockType == kEntryBlock ||
        curBlock->blockType == kExitBlock) {
        return false;
    }

    /*
     * Block has been parsed - check the taken/fallThrough in case it is a split
     * block.
     */
    if (curBlock->firstMIRInsn != NULL) {
          bool changed = false;
          if (curBlock->taken)
              changed |= exhaustTrace(cUnit, curBlock->taken);
          if (curBlock->fallThrough)
              changed |= exhaustTrace(cUnit, curBlock->fallThrough);
          return changed;
    }
    while (true) {
        /* Parse instruction */
        DecodedInstruction dalvikInsn;
        int width = parseInsn (codePtr, &dalvikInsn, false);

        /* Terminate when the data section is seen */
        if (width == 0)
            break;

        /* Set up MIR */
        MIR *insn = dvmCompilerNewMIR ();
        insn->dalvikInsn = dalvikInsn;
        insn->offset = curOffset;
        insn->width = width;

        /* Keep track which method this MIR is from. Initially we assume that there is no method nesting caused by inlining */
        insn->nesting.sourceMethod = cUnit->method;

        /* Add it to current BB */
        dvmCompilerAppendMIR(curBlock, insn);

        codePtr += width;
        int flags = dexGetFlagsFromOpcode(insn->dalvikInsn.opcode);

        /*
         * Stop extending the trace after seeing these instructions:
         *  It depends on the style of loop formation we are in:
         *      - New style: Whether it returns, switches or is the OP_THROW instruction
         *      - Old style: Whether it returns, switches, calls or is the OP_THROW instruction
         */
        bool test = false;

        if (gDvmJit.oldLoopDetection == true)
        {
            test = (flags & (kInstrCanReturn | kInstrCanSwitch | kInstrInvoke));
        }
        else {
            test = ( (flags & (kInstrCanReturn | kInstrCanSwitch)) || (insn->dalvikInsn.opcode == OP_THROW));
        }

        if (test == true) {
            curBlock->fallThrough = cUnit->exitBlock;
            dvmCompilerSetBit(cUnit->exitBlock->predecessors, curBlock->id);
            break;
        } else if (flags & kInstrCanBranch) {
            processCanBranch(&cUnit->blockList, &curBlock, insn, curOffset, width, flags,
                             codePtr, NULL);
            if (curBlock->taken) {
                exhaustTrace(cUnit, curBlock->taken);
            }
            if (curBlock->fallThrough) {
                exhaustTrace(cUnit, curBlock->fallThrough);
            }
            break;
        }
        curOffset += width;
        BasicBlock *nextBlock = findBlock(&cUnit->blockList, curOffset,
                                          /* split */
                                          false,
                                          /* create */
                                          false,
                                          /* immedPredBlockP */
                                          NULL);
        if (nextBlock) {
            /*
             * The next instruction could be the target of a previously parsed
             * forward branch so a block is already created. If the current
             * instruction is not an unconditional branch, connect them through
             * the fall-through link.
             */
            assert(curBlock->fallThrough == NULL ||
                   curBlock->fallThrough == nextBlock ||
                   curBlock->fallThrough == cUnit->exitBlock);

            if ((curBlock->fallThrough == NULL) &&
                (flags & kInstrCanContinue)) {
                curBlock->needFallThroughBranch = true;
                curBlock->fallThrough = nextBlock;
                dvmCompilerSetBit(nextBlock->predecessors, curBlock->id);
            }
            /* Block has been visited - no more parsing needed */
            if (nextBlock->visited == true) {
                return true;
            }
            curBlock = nextBlock;
        }
    }
    return true;
}

/**
 * @brief Print out the information about the loop
 * @param cUnit the CompilationUnit
 */
static void printAcceptedLoop (CompilationUnit *cUnit)
{
    const Method *method;
    char *signature;

    //Start with some paranoid handling:
    if (cUnit == NULL || cUnit->method == NULL || cUnit->method->clazz == NULL)
        return;

    //Get method and signature
    method = cUnit->method;
    signature = dexProtoCopyMethodDescriptor(&method->prototype);

    //Print out the acceptance
    ALOGD ("Accepted Loop from method %s%s, its signature is %s, offset is %d",
            method->clazz->descriptor, method->name, signature,
            cUnit->entryBlock->startOffset);

    //De-allocation
    free (signature), signature = NULL;
}

/**
 * @brief Count the bytecodes in the CompilationUnit
 * @param cUnit the CompilationUnit
 * @return the number of bytecodes to be compiled
 */
static unsigned int countByteCodes (CompilationUnit *cUnit)
{
    unsigned int res = 0;

    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);

    while (true)
    {
        BasicBlock *bbscan = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bbscan == NULL)
        {
            break;
        }

        for (MIR *mir = bbscan->firstMIRInsn; mir != 0; mir = mir->next)
        {
            res++;
        }
    }

    return res;
}

/* Compile a loop */
static bool compileLoop(CompilationUnit *cUnit, unsigned int startOffset,
                        JitTraceDescription *desc, int numMaxInsts,
                        JitTranslationInfo *info, jmp_buf *bailPtr,
                        int optHints)
{
    int numDalvikRegisters = 0;
    unsigned int curOffset = startOffset;
    bool changed;
#if defined(WITH_JIT_TUNING)
    CompilerMethodStats *methodStats;
#endif

    //Calculate code pointer
    const u2 *codePtr = cUnit->method->insns + curOffset;

    if (gDvmJit.knownNonLoopHeaderCache.find (codePtr) != gDvmJit.knownNonLoopHeaderCache.end ())
    {
        /* Retry the original trace with JIT_OPT_NO_LOOP disabled */
        dvmCompilerArenaReset();
        return dvmCompileTrace(desc, numMaxInsts, info, bailPtr, optHints | JIT_OPT_NO_LOOP);
    }

    cUnit->jitMode = kJitLoop;

    /* Initialize the block list */
    dvmInitGrowableList(&cUnit->blockList, 4);

    /* Initialize the PC reconstruction list */
    dvmInitGrowableList(&cUnit->pcReconstructionList, 8);

    /* Create the default entry and exit blocks and enter them to the list */
    BasicBlock *entryBlock = dvmCompilerNewBBinList (cUnit->blockList, kEntryBlock);
    entryBlock->startOffset = curOffset;
    cUnit->entryBlock = entryBlock;

    BasicBlock *exitBlock = dvmCompilerNewBBinList (cUnit->blockList, kExitBlock);
    cUnit->exitBlock = exitBlock;

    /* Current block to record parsed instructions */
    BasicBlock *curBlock = dvmCompilerNewBBinList (cUnit->blockList, kDalvikByteCode);
    curBlock->startOffset = curOffset;

    /* Set entry's fallthrough be the bytecode block */
    dvmCompilerReplaceChildBasicBlock (curBlock, entryBlock, kChildTypeFallthrough);

    do {
        dvmCompilerDataFlowAnalysisDispatcher(cUnit,
                dvmCompilerClearVisitedFlag,
                kAllNodes,
                false /* isIterative */);
        changed = exhaustTrace(cUnit, curBlock);
    } while (changed);

#ifndef ARCH_IA32
    /* A special block to host PC reconstruction code */
    dvmCompilerNewBBinList (cUnit->blockList, kPCReconstruction);
#endif

    /* And one final block that publishes the PC and raises the exception */
    cUnit->puntBlock = dvmCompilerNewBBinList (cUnit->blockList, kExceptionHandling);

    /* Now that we finished inserting blocks, let's update the number of blocks in cUnit */
    cUnit->numBlocks = dvmGrowableListSize (&cUnit->blockList);

#ifdef BYTECODE_FILTER
    GrowableListIterator iterator;
    MIR *insn;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    while (true) {
        BasicBlock *bbscan = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bbscan == NULL) break;
        if (bbscan->blockType == kDalvikByteCode) {
            for (insn = bbscan->firstMIRInsn; insn; insn = insn->next)
                if (!dvmIsOpcodeSupportedByJit(insn->dalvikInsn))
                    goto bail;
        }
    }
#endif

    numDalvikRegisters = cUnit->method->registersSize;
    dvmCompilerUpdateCUnitNumDalvikRegisters (cUnit, numDalvikRegisters);

    /* Verify if all blocks are connected as claimed */
    /* FIXME - to be disabled in the future */
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, verifyPredInfo,
            kAllNodes,
            false /* isIterative */);

    //Mark off any non loop header block for future reference
    dvmCompilerLoopMarkOffNonHeaderBlocks (cUnit);

    /* Try to identify a loop */
    if (dvmCompilerCalculateBasicBlockInformation (cUnit) == false)
    {
        // Reason for failure already logged
        goto bail;
    }

    //Set that the CompilationUnit is a loop
    dvmCompilerLoopOpt(cUnit);

    //If over accepted amount, bail
    {
        int numByteCodes = countByteCodes (cUnit);
        if (numByteCodes > JIT_MAX_TRACE_LEN)
        {
            if (cUnit->printMe == true)
            {
                ALOGD("JIT_INFO: Loop trace @ offset %04x aborted due too many byte codes (%d/%d)",
                      cUnit->entryBlock->startOffset, numByteCodes, JIT_MAX_TRACE_LEN);
            }
            goto bail;
        }
    }

    //If anybody wanted to quit, exit now
    if (cUnit->quitLoopMode == true)
    {
        // No message needed - reason should be already logged
        goto bail;
    }

#if defined(ARCH_IA32)


    //Before lowering, print out the compilation unit
    if (cUnit->printMe == true)
    {
        //Finally dump the compilation unit
        dvmCompilerDumpCompilationUnit (cUnit);
    }

    //Get global registerization information
    {
        int gRegisterization = gDvmJit.maximumRegisterization;

        //If the global information gave us something
        if (gRegisterization >= 0)
        {
            //Get the minimum between what we have and the global registerization
            int min = cUnit->maximumRegisterization;

            if (min > gRegisterization)
            {
                min = gRegisterization;
            }

            cUnit->maximumRegisterization = min;
        }
    }

    {
        //Get backend gate function
        bool (*backEndGate) (CompilationUnit *) = gDvmJit.jitFramework.backEndGate;

        //Suppose we are going to call the back-end
        bool callBackend = true;

        //If it exists
        if (backEndGate != 0)
        {
            callBackend = backEndGate (cUnit);
        }

        //Call if need be
        if (callBackend == true)
        {
            dvmCompilerMIR2LIR (cUnit, info);
        }
        else
        {
            //Otherwise bail
            goto bail;
        }
    }
#else
    dvmCompilerInitializeRegAlloc(cUnit);

    /* Allocate Registers using simple local allocation scheme */
    dvmCompilerLocalRegAlloc(cUnit);

    /* Convert MIR to LIR, etc. */
    dvmCompilerMIR2LIR(cUnit, info);
#endif

    /* Loop contains never executed blocks / heavy instructions */
    if (cUnit->quitLoopMode) {
        if (cUnit->printMe || gDvmJit.receivedSIGUSR2) {
            ALOGD("JIT_INFO: Loop trace @ offset %04x aborted due to unresolved code info",
                    cUnit->entryBlock->startOffset);
        }
        goto bail;
    }

    //We got to this point, the loop is accepted by the middle end
    if (cUnit->printMe == true)
    {
        printAcceptedLoop (cUnit);
    }

    /* Convert LIR into machine code. Loop for recoverable retries */
    do {
        dvmCompilerAssembleLIR(cUnit, info);
        cUnit->assemblerRetries++;
        if (cUnit->printMe && cUnit->assemblerStatus != kSuccess)
            ALOGD("Assembler abort #%d on %d", cUnit->assemblerRetries,
                    cUnit->assemblerStatus);
    } while (cUnit->assemblerStatus == kRetryAll);

    /* Loop is too big - bail out */
    if (cUnit->assemblerStatus == kRetryHalve) {
        if (cUnit->printMe == true)
        {
            ALOGD("JIT_INFO: Loop trace @ offset %04x aborted because trace is too large",
                    cUnit->entryBlock->startOffset);
        }
        goto bail;
    }

    if (cUnit->printMe || gDvmJit.receivedSIGUSR2) {
        ALOGD("JIT_INFO: Loop trace @ offset %04x", cUnit->entryBlock->startOffset);
        dvmCompilerCodegenDump(cUnit);
    }

    /*
     * If this trace uses class objects as constants,
     * dvmJitInstallClassObjectPointers will switch the thread state
     * to running and look up the class pointers using the descriptor/loader
     * tuple stored in the callsite info structure. We need to make this window
     * as short as possible since it is blocking GC.
     */
    if (cUnit->hasClassLiterals && info->codeAddress) {
        dvmJitInstallClassObjectPointers(cUnit, (char *) info->codeAddress);
    }

    /*
     * Since callsiteinfo is allocated from the arena, delay the reset until
     * class pointers are resolved.
     */
    dvmCompilerArenaReset();

    assert(cUnit->assemblerStatus == kSuccess);
#if defined(WITH_JIT_TUNING)
    /* Locate the entry to store compilation statistics for this method */
    methodStats = dvmCompilerAnalyzeMethodBody(desc->method, false);
    methodStats->nativeSize += cUnit->totalSize;
#endif

#if defined(VTUNE_DALVIK)
    /* Send the loop trace information to the VTune */
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        if(info->codeAddress) {
            sendTraceInfoToVTune(cUnit, desc);
        } else {
            LOGD("Invalid trace\n");
        }
    }
#endif

    return info->codeAddress != NULL;

bail:
    /* Retry the original trace with JIT_OPT_NO_LOOP disabled */
    dvmCompilerArenaReset();
    return dvmCompileTrace(desc, numMaxInsts, info, bailPtr, optHints | JIT_OPT_NO_LOOP);
}

static bool searchClassTablePrefix(const Method* method) {
    if (gDvmJit.classTable == NULL) {
        return false;
    }
    HashIter iter;
    HashTable* pTab = gDvmJit.classTable;
    for (dvmHashIterBegin(pTab, &iter); !dvmHashIterDone(&iter);
        dvmHashIterNext(&iter))
    {
        const char* str = (const char*) dvmHashIterData(&iter);
        if (strncmp(method->clazz->descriptor, str, strlen(str)) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Main entry point to start trace compilation. Basic blocks are constructed
 * first and they will be passed to the codegen routines to convert Dalvik
 * bytecode into machine code.
 */
bool dvmCompileTrace(JitTraceDescription *desc, int numMaxInsts,
                     JitTranslationInfo *info, jmp_buf *bailPtr,
                     int optHints)
{
    const DexCode *dexCode = dvmGetMethodCode(desc->method);
    const JitTraceRun* currRun = &desc->trace[0];
    unsigned int curOffset = currRun->info.frag.startOffset;
    unsigned int startOffset = curOffset;
    unsigned int numInsts = currRun->info.frag.numInsts;
    const u2 *codePtr = dexCode->insns + curOffset;
    int traceSize = 0;  // # of half-words
    const u2 *startCodePtr = codePtr;
    BasicBlock *curBB, *entryCodeBB;
    static int compilationId;
#ifndef ARCH_IA32
    CompilationUnit cUnit;
#else
    CompilationUnit_O1 cUnit;
#endif
    GrowableList *blockList;
#if defined(WITH_JIT_TUNING)
    CompilerMethodStats *methodStats;
#endif

    /* If we've already compiled this trace, just return success */
    if (dvmJitGetTraceAddr(startCodePtr) && !info->discardResult) {
        /*
         * Make sure the codeAddress is NULL so that it won't clobber the
         * existing entry.
         */
        info->codeAddress = NULL;
        return true;
    }

    /* If the work order is stale, discard it */
    if (info->cacheVersion != gDvmJit.cacheVersion) {
        return false;
    }

    compilationId++;
    memset(&cUnit, 0, sizeof(CompilationUnit));

    //Set the constant values
    std::map<int, int> constantValues;
    cUnit.constantValues = &constantValues;

    // Initialize the degenerate PHI map
    std::map<int, int> degeneratePhiMap;
    cUnit.degeneratePhiMap = &degeneratePhiMap;

    //Set up the jit verbose infrastructure
    std::vector<std::pair<BBType, char*> > code_block_table;
    cUnit.code_block_table = &code_block_table;

#if defined(WITH_JIT_TUNING)
    /* Locate the entry to store compilation statistics for this method */
    methodStats = dvmCompilerAnalyzeMethodBody(desc->method, false);
#endif

    /* Set the recover buffer pointer */
    cUnit.bailPtr = static_cast<jmp_buf *> (bailPtr);

    /* Initialize the printMe flag */
    cUnit.printMe = gDvmJit.printMe;

    /* Setup the method */
    cUnit.method = desc->method;

    /* Store the trace descriptor and set the initial mode */
    cUnit.traceDesc = desc;
    cUnit.jitMode = kJitTrace;

    /* Initialize the PC reconstruction list */
    dvmInitGrowableList(&cUnit.pcReconstructionList, 8);

    /* Initialize the basic block list */
    blockList = &cUnit.blockList;
    dvmInitGrowableList(blockList, 8);

    /* Identify traces that we don't want to compile */
    if (gDvmJit.classTable) {
        bool classFound = searchClassTablePrefix(desc->method);
        if (gDvmJit.classTable && gDvmJit.includeSelectedMethod != classFound) {
            return false;
        }
    }
    if (gDvmJit.methodTable) {
        int len = strlen(desc->method->clazz->descriptor) +
                  strlen(desc->method->name) + 1;
        char *fullSignature = (char *)dvmCompilerNew(len, true);
        strcpy(fullSignature, desc->method->clazz->descriptor);
        strcat(fullSignature, desc->method->name);

        int hashValue = dvmComputeUtf8Hash(fullSignature);

        /*
         * Doing three levels of screening to see whether we want to skip
         * compiling this method
         */

        /* First, check the full "class;method" signature */
        bool methodFound =
            dvmHashTableLookup(gDvmJit.methodTable, hashValue,
                               fullSignature, (HashCompareFunc) strcmp,
                               false) !=
            NULL;

        /* Full signature not found - check the enclosing class */
        if (methodFound == false) {
            int hashValue = dvmComputeUtf8Hash(desc->method->clazz->descriptor);
            methodFound =
                dvmHashTableLookup(gDvmJit.methodTable, hashValue,
                               (char *) desc->method->clazz->descriptor,
                               (HashCompareFunc) strcmp, false) !=
                NULL;
            /* Enclosing class not found - check the method name */
            if (methodFound == false) {
                int hashValue = dvmComputeUtf8Hash(desc->method->name);
                methodFound =
                    dvmHashTableLookup(gDvmJit.methodTable, hashValue,
                                   (char *) desc->method->name,
                                   (HashCompareFunc) strcmp, false) !=
                    NULL;

                /*
                 * Debug by call-graph is enabled. Check if the debug list
                 * covers any methods on the VM stack.
                 */
                if (methodFound == false && gDvmJit.checkCallGraph == true) {
                    methodFound =
                        filterMethodByCallGraph(info->requestingThread,
                                                desc->method->name);
                }
            }
        }

        /*
         * Under the following conditions, the trace will be *conservatively*
         * compiled by only containing single-step instructions to and from the
         * interpreter.
         * 1) If includeSelectedMethod == false, the method matches the full or
         *    partial signature stored in the hash table.
         *
         * 2) If includeSelectedMethod == true, the method does not match the
         *    full and partial signature stored in the hash table.
         */
        if (gDvmJit.methodTable && gDvmJit.includeSelectedMethod != methodFound) {
#ifdef ARCH_IA32
            return false;
#else
            cUnit.allSingleStep = true;
#endif
        } else {
            /* Compile the trace as normal */

            /* Print the method we cherry picked */
            if (gDvmJit.includeSelectedMethod == true) {
                cUnit.printMe = true;
            }
        }
    }

    // Each pair is a range, check whether curOffset falls into a range.
    bool includeOffset = (gDvmJit.num_entries_pcTable < 2);
    for (int pcOff = 0; pcOff < gDvmJit.num_entries_pcTable; ) {
        if (pcOff+1 >= gDvmJit.num_entries_pcTable) {
          break;
        }
        if (curOffset >= gDvmJit.pcTable[pcOff] && curOffset <= gDvmJit.pcTable[pcOff+1]) {
            includeOffset = true;
            break;
        }
        pcOff += 2;
    }
    if (!includeOffset) {
        return false;
    }

#ifdef DEBUG_METHOD_CONTEXT
    //To facilitate debugging, just cause the creation of the context
    MethodContextHandler::getMethodContext(cUnit.method);
#endif

    //Compile as a loop first: only do this in the new loop detection system
    if ( (gDvmJit.oldLoopDetection == false) && (optHints & JIT_OPT_NO_LOOP) == 0) {
        dvmCompilerArenaReset();
        return compileLoop(&cUnit, startOffset, desc, numMaxInsts,
                info, bailPtr, optHints);
    }

    /* Allocate the entry block */
    curBB = dvmCompilerNewBBinList (*blockList, kEntryBlock);
    curBB->startOffset = curOffset;
    cUnit.entryBlock = curBB;

    entryCodeBB = dvmCompilerNewBBinList (*blockList, kDalvikByteCode);
    entryCodeBB->startOffset = curOffset;
    curBB->fallThrough = entryCodeBB;
    curBB = entryCodeBB;

#ifdef BYTECODE_FILTER
// disable certain bytecodes
    while (1) {
        DecodedInstruction insn;
        int width = parseInsn(codePtr, &insn, false);
        if (!dvmIsOpcodeSupportedByJit(insn)) {
            return false;
        }

        assert(width);
        if (--numInsts == 0) {
            if (currRun->info.frag.runEnd) {
                break;
            } else {
                /* Advance to the next trace description (ie non-meta info) */
                do {
                    currRun++;
                } while (!currRun->isCode);

                /* Dummy end-of-run marker seen */
                if (currRun->info.frag.numInsts == 0) {
                    break;
                }

                curOffset = currRun->info.frag.startOffset;
                numInsts = currRun->info.frag.numInsts;
                codePtr = dexCode->insns + curOffset;
            }
        } else {
            curOffset += width;
            codePtr += width;
        }
    }
    currRun = &desc->trace[0];
    curOffset = currRun->info.frag.startOffset;
    numInsts = currRun->info.frag.numInsts;
    codePtr = dexCode->insns + curOffset;
#endif

    if (cUnit.printMe) {
        ALOGD("--------\nCompiler: Building trace for %s, offset %#x",
             desc->method->name, curOffset);
    }

    /*
     * Analyze the trace descriptor and include up to the maximal number
     * of Dalvik instructions into the IR.
     */
    while (1) {
        /* Create and set up MIR */
        MIR *insn = dvmCompilerNewMIR ();
        insn->offset = curOffset;

        /* Keep track which method this MIR is from. Initially we assume that there is no method nesting caused by inlining */
        insn->nesting.sourceMethod = cUnit.method;

        /* Parse the instruction */
        int width = parseInsn(codePtr, &insn->dalvikInsn, cUnit.printMe);

        /* The trace should never include instruction data */
        assert(width);
        insn->width = width;
        traceSize += width;
        dvmCompilerAppendMIR(curBB, insn);
        cUnit.numInsts++;

        int flags = dexGetFlagsFromOpcode(insn->dalvikInsn.opcode);

        if (flags & kInstrInvoke) {
            const Method *calleeMethod = (const Method *)
                currRun[JIT_TRACE_CUR_METHOD].info.meta;
            assert(numInsts == 1);
            CallsiteInfo *callsiteInfo =
                (CallsiteInfo *)dvmCompilerNew(sizeof(CallsiteInfo), true);
            callsiteInfo->classDescriptor = (const char *)
                currRun[JIT_TRACE_CLASS_DESC].info.meta;
            callsiteInfo->classLoader = (Object *)
                currRun[JIT_TRACE_CLASS_LOADER].info.meta;
            callsiteInfo->method = calleeMethod;
            insn->meta.callsiteInfo = callsiteInfo;
        }

        /* Instruction limit reached - terminate the trace here */
        if (cUnit.numInsts >= numMaxInsts) {
            break;
        }
        if (--numInsts == 0) {
            if (currRun->info.frag.runEnd) {
                break;
            } else {
                /* Advance to the next trace description (ie non-meta info) */
                do {
                    currRun++;
                } while (!currRun->isCode);

                /* Dummy end-of-run marker seen */
                if (currRun->info.frag.numInsts == 0) {
                    break;
                }

                curBB = dvmCompilerNewBBinList (*blockList, kDalvikByteCode);
                curOffset = currRun->info.frag.startOffset;
                numInsts = currRun->info.frag.numInsts;
                curBB->startOffset = curOffset;
                codePtr = dexCode->insns + curOffset;
            }
        } else {
            curOffset += width;
            codePtr += width;
        }
    }

#if defined(WITH_JIT_TUNING)
    /* Convert # of half-word to bytes */
    methodStats->compiledDalvikSize += traceSize * 2;
#endif

    /*
     * Now scan basic blocks containing real code to connect the
     * taken/fallthrough links. Also create chaining cells for code not included
     * in the trace.
     */
    size_t blockId;
    for (blockId = 0; blockId < blockList->numUsed; blockId++) {
        curBB = (BasicBlock *) dvmGrowableListGetElement(blockList, blockId);
        MIR *lastInsn = curBB->lastMIRInsn;
        /* Skip empty blocks */
        if (lastInsn == NULL) {
            continue;
        }
        curOffset = lastInsn->offset;
        unsigned int targetOffset = curOffset;
        unsigned int fallThroughOffset = curOffset + lastInsn->width;
        bool isInvoke = false;
        const Method *callee = NULL;

        findBlockBoundary(desc->method, curBB->lastMIRInsn, curOffset,
                          &targetOffset, &isInvoke, &callee);

        /* Link the taken and fallthrough blocks */
        BasicBlock *searchBB;

        int flags = dexGetFlagsFromOpcode(lastInsn->dalvikInsn.opcode);

        if (flags & kInstrInvoke) {
            cUnit.hasInvoke = true;
        }

        /* Backward branch seen: only care if we are in the old loop system */
        if (gDvmJit.oldLoopDetection == true &&
                isInvoke == false &&
                (flags & kInstrCanBranch) != 0 &&
                targetOffset < curOffset &&
                (optHints & JIT_OPT_NO_LOOP) == 0) {
            dvmCompilerArenaReset();
            return compileLoop(&cUnit, startOffset, desc, numMaxInsts,
                    info, bailPtr, optHints);
        }

        /* No backward branch in the trace - start searching the next BB */
        size_t searchBlockId;
        for (searchBlockId = blockId+1; searchBlockId < blockList->numUsed;
             searchBlockId++) {
            searchBB = (BasicBlock *) dvmGrowableListGetElement(blockList,
                                                                searchBlockId);
            if (targetOffset == searchBB->startOffset) {
                curBB->taken = searchBB;
                dvmCompilerSetBit(searchBB->predecessors, curBB->id);
            }
            if (fallThroughOffset == searchBB->startOffset) {
                curBB->fallThrough = searchBB;
                dvmCompilerSetBit(searchBB->predecessors, curBB->id);

                /*
                 * Fallthrough block of an invoke instruction needs to be
                 * aligned to 4-byte boundary (alignment instruction to be
                 * inserted later.
                 */
                if (flags & kInstrInvoke) {
                    searchBB->isFallThroughFromInvoke = true;
                }
            }
        }

        /*
         * Some blocks are ended by non-control-flow-change instructions,
         * currently only due to trace length constraint. In this case we need
         * to generate an explicit branch at the end of the block to jump to
         * the chaining cell.
         */
        curBB->needFallThroughBranch =
            ((flags & (kInstrCanBranch | kInstrCanSwitch | kInstrCanReturn |
                       kInstrInvoke)) == 0);
        if (lastInsn->dalvikInsn.opcode == OP_PACKED_SWITCH ||
            lastInsn->dalvikInsn.opcode == OP_SPARSE_SWITCH) {
            int i;
            const u2 *switchData = desc->method->insns + lastInsn->offset +
                             lastInsn->dalvikInsn.vB;
            int size = switchData[1];
            int maxChains = MIN(size, MAX_CHAINED_SWITCH_CASES);

            /*
             * Generate the landing pad for cases whose ranks are higher than
             * MAX_CHAINED_SWITCH_CASES. The code will re-enter the interpreter
             * through the NoChain point.
             */
            if (maxChains != size) {
                cUnit.switchOverflowPad =
                    desc->method->insns + lastInsn->offset;
            }

            s4 *targets = (s4 *) (switchData + 2 +
                    (lastInsn->dalvikInsn.opcode == OP_PACKED_SWITCH ?
                     2 : size * 2));

            //initialize successorBlockList type
            curBB->successorBlockList.blockListType =
               (lastInsn->dalvikInsn.opcode == OP_PACKED_SWITCH) ? kPackedSwitch : kSparseSwitch;
            dvmInitGrowableList(&curBB->successorBlockList.blocks, size);


            /* One chaining cell for the first MAX_CHAINED_SWITCH_CASES cases */
            for (i = 0; i < maxChains; i++) {
                BasicBlock *caseChain = dvmCompilerNewBBinList (*blockList, kChainingCellNormal);
                caseChain->startOffset = lastInsn->offset + targets[i];

                // create successor and set precedessor for each normal chaining cell for switch cases
                SuccessorBlockInfo *successorBlockInfo =
                   (SuccessorBlockInfo *) dvmCompilerNew(sizeof(SuccessorBlockInfo), false);
                successorBlockInfo->block = caseChain;
                dvmInsertGrowableList(&curBB->successorBlockList.blocks,
                              (intptr_t) successorBlockInfo);
                dvmCompilerSetBit(caseChain->predecessors, curBB->id);

            }

            /* One more chaining cell for the default case */
            BasicBlock *caseChain = dvmCompilerNewBBinList (*blockList, kChainingCellNormal);
            caseChain->startOffset = lastInsn->offset + lastInsn->width;

            // create successor for default case and set predecessor for default case chaining cell block
            SuccessorBlockInfo *successorBlockInfo =
               (SuccessorBlockInfo *) dvmCompilerNew(sizeof(SuccessorBlockInfo), false);
            successorBlockInfo->block = caseChain;
            dvmInsertGrowableList(&curBB->successorBlockList.blocks,
                          (intptr_t) successorBlockInfo);
            dvmCompilerSetBit(caseChain->predecessors, curBB->id);

        /* Fallthrough block not included in the trace */
        } else if (!isUnconditionalBranch(lastInsn) &&
                   curBB->fallThrough == NULL) {
            BasicBlock *fallThroughBB;
            /*
             * If the chaining cell is after an invoke or
             * instruction that cannot change the control flow, request a hot
             * chaining cell.
             */
            if (isInvoke || curBB->needFallThroughBranch) {
                fallThroughBB = dvmCompilerNewBBinList (*blockList, kChainingCellHot);
            } else {
                fallThroughBB = dvmCompilerNewBBinList (*blockList, kChainingCellNormal);
            }
            fallThroughBB->startOffset = fallThroughOffset;
            curBB->fallThrough = fallThroughBB;
            dvmCompilerSetBit(fallThroughBB->predecessors, curBB->id);
        }
        /* Target block not included in the trace */
        if (curBB->taken == NULL &&
            (isGoto(lastInsn) || isInvoke ||
            (targetOffset != UNKNOWN_TARGET && targetOffset != curOffset))) {
            BasicBlock *newBB = NULL;
            if (isInvoke) {
                /* Monomorphic callee */
                if (callee) {
                    /* JNI call doesn't need a chaining cell */
                    if (!dvmIsNativeMethod(callee)) {
                        newBB = dvmCompilerNewBBinList (*blockList, kChainingCellInvokeSingleton);
                        newBB->startOffset = 0;
                        newBB->containingMethod = callee;
                    }
                /* Will resolve at runtime */
                } else {
                    newBB = dvmCompilerNewBBinList (*blockList, kChainingCellInvokePredicted);
                    newBB->startOffset = 0;
                }
            /* For unconditional branches, request a hot chaining cell */
            } else {
#if !defined(WITH_SELF_VERIFICATION)
                newBB = dvmCompilerNewBBinList (*blockList, dexIsGoto (flags) ? kChainingCellHot : kChainingCellNormal);
                newBB->startOffset = targetOffset;
#else
                /* Handle branches that branch back into the block */
                if (targetOffset >= curBB->firstMIRInsn->offset &&
                    targetOffset <= curBB->lastMIRInsn->offset) {
                    newBB = dvmCompilerNewBBinList (*blockList, kChainingCellBackwardBranch);
                } else {
                    newBB = dvmCompilerNewBBinList (*blockList,
                            dexIsGoto (flags) ? kChainingCellHot : kChainingCellNormal);
                }
                newBB->startOffset = targetOffset;
#endif
            }
            if (newBB != 0) {
                dvmCompilerReplaceChildBasicBlock (newBB, curBB, kChildTypeTaken);
            }
        }
    }

#ifndef ARCH_IA32
    /* Now create a special block to host PC reconstruction code */
    curBB = dvmCompilerNewBBinList (*blockList, kPCReconstruction);
#endif

    /* And one final block that publishes the PC and raise the exception */
    curBB = dvmCompilerNewBBinList (*blockList, kExceptionHandling);
    cUnit.puntBlock = curBB;

    cUnit.numBlocks = dvmGrowableListSize (blockList);

    if (cUnit.printMe) {
        char* signature =
            dexProtoCopyMethodDescriptor(&desc->method->prototype);
        ALOGD("TRACEINFO (%d): 0x%08x %s%s.%s %#x %d of %d, %d blocks",
            compilationId,
            (intptr_t) desc->method->insns,
            desc->method->clazz->descriptor,
            desc->method->name,
            signature,
            desc->trace[0].info.frag.startOffset,
            traceSize,
            dexCode->insnsSize,
            cUnit.numBlocks);
        free(signature);
    }

    /* Set the instruction set to use (NOTE: later components may change it) */
    cUnit.instructionSet = dvmCompilerInstructionSet();

    const int numDalvikRegisters = cUnit.method->registersSize;
    dvmCompilerUpdateCUnitNumDalvikRegisters (&cUnit, numDalvikRegisters);

#ifndef ARCH_IA32
    //Try to inline invokes. For x86, the loop framework has inlining pass so we do not do the inlining here.
    if (cUnit.hasInvoke == true)
    {
        dvmCompilerInlineMIR (&cUnit, info);
    }
#endif


#ifndef ARCH_IA32
    /* Preparation for SSA conversion */
    dvmInitializeSSAConversion(&cUnit);

    dvmCompilerNonLoopAnalysis(&cUnit);

    dvmCompilerInitializeRegAlloc(&cUnit);  // Needs to happen after SSA naming
#else
    //Now calculate basic block information. We set the "filter" argument to false because
    //we are in trace mode and thus we do not need to test any loops if they are correctly formed.
    //We also set the "buildLoopInformation" to false because we have no loops to build.
    dvmCompilerCalculateBasicBlockInformation (&cUnit, false, false);
#endif

#ifndef ARCH_IA32
    /* Allocate Registers using simple local allocation scheme */
    dvmCompilerLocalRegAlloc(&cUnit);

    /* Before lowering, print out the compilation unit */
    if (cUnit.printMe == true) {
        dvmCompilerDumpCompilationUnit(&cUnit);
    }

    /* Convert MIR to LIR, etc. */
    dvmCompilerMIR2LIR(&cUnit, info);
#else /* ARCH_IA32 */
    //The loop optimization framework can work for traces as well
    dvmCompilerLoopOpt(&cUnit);

    //If anybody wanted to quit, exit now. We check the "quitLoopMode" because the loop framework sets
    //that flag to true when something wrong is encountered.
    if (cUnit.quitLoopMode == true)
    {
        return false;
    }

    //Before lowering, print out the compilation unit
    if (cUnit.printMe == true)
    {
        dvmCompilerDumpCompilationUnit (&cUnit);
    }

    {
        //Get backend gate function
        bool (*backEndGate) (CompilationUnit *) = gDvmJit.jitFramework.backEndGate;

        //Suppose we are going to call the back-end
        bool callBackend = true;

        //If it exists
        if (backEndGate != 0)
        {
            callBackend = backEndGate (&cUnit);
        }

        //Call if need be
        if (callBackend == true)
        {
            dvmCompilerMIR2LIR (&cUnit, info);
        }
        else
        {
            return false;
        }
    }
#endif

    /* Convert LIR into machine code. Loop for recoverable retries */
    do {
        dvmCompilerAssembleLIR(&cUnit, info);
        cUnit.assemblerRetries++;
        if (cUnit.printMe && cUnit.assemblerStatus != kSuccess)
            ALOGD("Assembler abort #%d on %d",cUnit.assemblerRetries,
                  cUnit.assemblerStatus);
    } while (cUnit.assemblerStatus == kRetryAll);

    if (cUnit.printMe) {
        ALOGD("Trace Dalvik PC: %p", startCodePtr);
        dvmCompilerCodegenDump(&cUnit);
        ALOGD("End %s%s, %d Dalvik instructions",
             desc->method->clazz->descriptor, desc->method->name,
             cUnit.numInsts);
    }

    if (cUnit.assemblerStatus == kRetryHalve) {
        /* Reset the compiler resource pool before retry */
        dvmCompilerArenaReset();

        /* Halve the instruction count and start from the top */
        return dvmCompileTrace(desc, cUnit.numInsts / 2, info, bailPtr,
                               optHints);
    }

    /*
     * If this trace uses class objects as constants,
     * dvmJitInstallClassObjectPointers will switch the thread state
     * to running and look up the class pointers using the descriptor/loader
     * tuple stored in the callsite info structure. We need to make this window
     * as short as possible since it is blocking GC.
     */
    if (cUnit.hasClassLiterals && info->codeAddress) {
        dvmJitInstallClassObjectPointers(&cUnit, (char *) info->codeAddress);
    }

    /*
     * Since callsiteinfo is allocated from the arena, delay the reset until
     * class pointers are resolved.
     */
    dvmCompilerArenaReset();

    assert(cUnit.assemblerStatus == kSuccess);
#if defined(WITH_JIT_TUNING)
    methodStats->nativeSize += cUnit.totalSize;
#endif

#if defined(VTUNE_DALVIK)
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        if(info->codeAddress) {
            sendTraceInfoToVTune(&cUnit, desc);
        } else {
            LOGD("Invalid trace\n");
        }
    }
#endif

    return info->codeAddress != NULL;
}
