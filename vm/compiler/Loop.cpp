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
#include "CompilerInternals.h"
#include "Dataflow.h"
#include "Loop.h"
#include "Utility.h"

#ifdef ARCH_IA32
#include "PassDriver.h"
#endif

#ifdef DEBUG_LOOP_ON
#define DEBUG_LOOP(X) X
#else
#define DEBUG_LOOP(X)
#endif

#ifndef ARCH_IA32
/**
 * Find the predecessor block of a given BasicBlock: the single predecessor whichever if only one predecessor,
 *      the entry block if there are two predecessors and the entry block is a predecessor, 0 otherwise
 */
static BasicBlock *findPredecessorBlock(const CompilationUnit *cUnit,
                                        const BasicBlock *bb)
{
    int numPred = dvmCountSetBits(bb->predecessors);
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);

    if (numPred == 1) {
        int predIdx = dvmBitVectorIteratorNext(&bvIterator);
        return (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList,
                                                        predIdx);
    /* First loop block */
    } else if ((numPred == 2) &&
               dvmIsBitSet(bb->predecessors, cUnit->entryBlock->id)) {
        while (true) {
            int predIdx = dvmBitVectorIteratorNext(&bvIterator);
            if (predIdx == cUnit->entryBlock->id) continue;
            return (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList,
                                                            predIdx);
        }
    /* Doesn't support other shape of control flow yet */
    } else {
        return NULL;
    }
}

/* Used for normalized loop exit condition checks */
static Opcode negateOpcode(Opcode opcode)
{
    switch (opcode) {
        /* reg/reg cmp */
        case OP_IF_EQ:
            return OP_IF_NE;
        case OP_IF_NE:
            return OP_IF_EQ;
        case OP_IF_LT:
            return OP_IF_GE;
        case OP_IF_GE:
            return OP_IF_LT;
        case OP_IF_GT:
            return OP_IF_LE;
        case OP_IF_LE:
            return OP_IF_GT;
        /* reg/zero cmp */
        case OP_IF_EQZ:
            return OP_IF_NEZ;
        case OP_IF_NEZ:
            return OP_IF_EQZ;
        case OP_IF_LTZ:
            return OP_IF_GEZ;
        case OP_IF_GEZ:
            return OP_IF_LTZ;
        case OP_IF_GTZ:
            return OP_IF_LEZ;
        case OP_IF_LEZ:
            return OP_IF_GTZ;
        default:
            ALOGE("opcode %d cannot be negated", opcode);
            dvmAbort();
            break;
    }
    return (Opcode)-1;  // unreached
}

/*
 * A loop is considered optimizable if:
 * 1) It has one basic induction variable.
 * 2) The loop back branch compares the BIV with a constant.
 * 3) We need to normalize the loop exit condition so that the loop is exited
 *    via the taken path.
 * 4) If it is a count-up loop, the condition is GE/GT. Otherwise it is
 *    LE/LT/LEZ/LTZ for a count-down loop.
 *
 * Return false for loops that fail the above tests.
 */
static bool isSimpleCountedLoop(CompilationUnit *cUnit)
{
    unsigned int i;
    BasicBlock *loopBackBlock = cUnit->entryBlock->fallThrough;
    LoopAnalysis *loopAnalysis = cUnit->loopAnalysis;

    if (loopAnalysis->numBasicIV != 1) return false;
    for (i = 0; i < loopAnalysis->ivList->numUsed; i++) {
        InductionVariableInfo *ivInfo;

        ivInfo = GET_ELEM_N(loopAnalysis->ivList, InductionVariableInfo*, i);
        /* Count up or down loop? */
        if (ivInfo->ssaReg == ivInfo->basicSSAReg) {
            /* Infinite loop */
            if (ivInfo->loopIncrement == 0) {
                return false;
            }
            loopAnalysis->isCountUpLoop = ivInfo->loopIncrement > 0;
            break;
        }
    }

    /* Find the block that ends with a branch to exit the loop */
    while (true) {
        loopBackBlock = findPredecessorBlock(cUnit, loopBackBlock);
        /* Loop structure not recognized as counted blocks */
        if (loopBackBlock == NULL) {
            return false;
        }
        /* Unconditional goto - continue to trace up the predecessor chain */
        if (loopBackBlock->taken == NULL) {
            continue;
        }
        break;
    }

    MIR *branch = loopBackBlock->lastMIRInsn;
    Opcode opcode = branch->dalvikInsn.opcode;

    // Remember the offset of the branch MIR in order to use it
    // when generating the extended MIRs
    loopAnalysis->loopBranchMIROffset = branch->offset;

    /* Last instruction is not a conditional branch - bail */
    if (dexGetFlagsFromOpcode(opcode) != (kInstrCanContinue|kInstrCanBranch)) {
        return false;
    }

    int endSSAReg;
    int endDalvikReg;

    /* reg/reg comparison */
    if (branch->ssaRep->numUses == 2) {
        if (branch->ssaRep->uses[0] == loopAnalysis->ssaBIV) {
            endSSAReg = branch->ssaRep->uses[1];
        } else if (branch->ssaRep->uses[1] == loopAnalysis->ssaBIV) {
            endSSAReg = branch->ssaRep->uses[0];
            opcode = negateOpcode(opcode);
        } else {
            return false;
        }
        endDalvikReg = dvmConvertSSARegToDalvik(cUnit, endSSAReg);
        /*
         * If the comparison is not between the BIV and a loop invariant,
         * return false. endDalvikReg is loop invariant if one of the
         * following is true:
         * - It is not defined in the loop (ie DECODE_SUB returns 0)
         * - It is reloaded with a constant
         */
        if ((DECODE_SUB(endDalvikReg) != 0) &&
            !dvmIsBitSet(cUnit->isConstantV, endSSAReg)) {
            return false;
        }
    /* Compare against zero */
    } else if (branch->ssaRep->numUses == 1) {
        if (branch->ssaRep->uses[0] == loopAnalysis->ssaBIV) {
            /* Keep the compiler happy */
            endDalvikReg = -1;
        } else {
            return false;
        }
    } else {
        return false;
    }

    /* Normalize the loop exit check as "if (iv op end) exit;" */
    if (loopBackBlock->taken->blockType == kDalvikByteCode) {
        opcode = negateOpcode(opcode);
    }

    if (loopAnalysis->isCountUpLoop) {
        /*
         * If the normalized condition op is not > or >=, this is not an
         * optimization candidate.
         */
        switch (opcode) {
            case OP_IF_GT:
            case OP_IF_GE:
                break;
            default:
                return false;
        }
        loopAnalysis->endConditionReg = DECODE_REG(endDalvikReg);
    } else  {
        /*
         * If the normalized condition op is not < or <=, this is not an
         * optimization candidate.
         */
        switch (opcode) {
            case OP_IF_LT:
            case OP_IF_LE:
                loopAnalysis->endConditionReg = DECODE_REG(endDalvikReg);
                break;
            case OP_IF_LTZ:
            case OP_IF_LEZ:
                break;
            default:
                return false;
        }
    }
    /*
     * Remember the normalized opcode, which will be used to determine the end
     * value used for the yanked range checks.
     */
    loopAnalysis->loopBranchOpcode = opcode;
    return true;
}

/*
 * Record the upper and lower bound information for range checks for each
 * induction variable. If array A is accessed by index "i+5", the upper and
 * lower bound will be len(A)-5 and -5, respectively.
 */
static void updateRangeCheckInfo(CompilationUnit *cUnit, int arrayReg,
                                 int idxReg)
{
    InductionVariableInfo *ivInfo;
    LoopAnalysis *loopAnalysis = cUnit->loopAnalysis;
    unsigned int i, j;

    for (i = 0; i < loopAnalysis->ivList->numUsed; i++) {
        ivInfo = GET_ELEM_N(loopAnalysis->ivList, InductionVariableInfo*, i);
        if (ivInfo->ssaReg == idxReg) {
            ArrayAccessInfo *arrayAccessInfo = NULL;
            for (j = 0; j < loopAnalysis->arrayAccessInfo->numUsed; j++) {
                ArrayAccessInfo *existingArrayAccessInfo =
                    GET_ELEM_N(loopAnalysis->arrayAccessInfo,
                               ArrayAccessInfo*,
                               j);
                if (existingArrayAccessInfo->arrayReg == arrayReg) {
                    if (ivInfo->constant > existingArrayAccessInfo->maxC) {
                        existingArrayAccessInfo->maxC = ivInfo->constant;
                    }
                    if (ivInfo->constant < existingArrayAccessInfo->minC) {
                        existingArrayAccessInfo->minC = ivInfo->constant;
                    }
                    arrayAccessInfo = existingArrayAccessInfo;
                    break;
                }
            }
            if (arrayAccessInfo == NULL) {
                arrayAccessInfo =
                    (ArrayAccessInfo *)dvmCompilerNew(sizeof(ArrayAccessInfo),
                                                      false);
                arrayAccessInfo->ivReg = ivInfo->basicSSAReg;
                arrayAccessInfo->arrayReg = arrayReg;
                arrayAccessInfo->maxC = (ivInfo->constant > 0) ? ivInfo->constant : 0;
                arrayAccessInfo->minC = (ivInfo->constant < 0) ? ivInfo->constant : 0;
                arrayAccessInfo->loopIncrement = ivInfo->loopIncrement;
                dvmInsertGrowableList(loopAnalysis->arrayAccessInfo,
                                      (intptr_t) arrayAccessInfo);
            }
            break;
        }
    }
}

/* Returns true if the loop body cannot throw any exceptions */
static bool doLoopBodyCodeMotion(CompilationUnit *cUnit)
{
    BasicBlock *loopBody = cUnit->entryBlock->fallThrough;
    MIR *mir;
    bool loopBodyCanThrow = false;

    for (mir = loopBody->firstMIRInsn; mir; mir = mir->next) {
        DecodedInstruction *dInsn = &mir->dalvikInsn;
        int dfAttributes =
            dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        /* Skip extended MIR instructions */
        if ((u2) dInsn->opcode >= kNumPackedOpcodes) continue;

        int instrFlags = dexGetFlagsFromOpcode(dInsn->opcode);

        /* Instruction is clean */
        if ((instrFlags & kInstrCanThrow) == 0) continue;

        /*
         * Currently we can only optimize away null and range checks. Punt on
         * instructions that can throw due to other exceptions.
         */
        if (!(dfAttributes & DF_HAS_NR_CHECKS)) {
            loopBodyCanThrow = true;
            continue;
        }

        /*
         * This comparison is redundant now, but we will have more than one
         * group of flags to check soon.
         */
        if (dfAttributes & DF_HAS_NR_CHECKS) {
            /*
             * Check if the null check is applied on a loop invariant register?
             * If the register's SSA id is less than the number of Dalvik
             * registers, then it is loop invariant.
             */
            int refIdx;
            switch (dfAttributes & DF_HAS_NR_CHECKS) {
                case DF_NULL_N_RANGE_CHECK_0:
                    refIdx = 0;
                    break;
                case DF_NULL_N_RANGE_CHECK_1:
                    refIdx = 1;
                    break;
                case DF_NULL_N_RANGE_CHECK_2:
                    refIdx = 2;
                    break;
                default:
                    refIdx = 0;
                    ALOGE("Jit: bad case in doLoopBodyCodeMotion");
                    dvmCompilerAbort(cUnit);
            }

            int useIdx = refIdx + 1;
            int subNRegArray =
                dvmConvertSSARegToDalvik(cUnit, mir->ssaRep->uses[refIdx]);
            int arraySub = DECODE_SUB(subNRegArray);

            /*
             * If the register is never updated in the loop (ie subscript == 0),
             * it is an optimization candidate.
             */
            if (arraySub != 0) {
                loopBodyCanThrow = true;
                continue;
            }

            /*
             * Then check if the range check can be hoisted out of the loop if
             * it is basic or dependent induction variable.
             */
            if (dvmIsBitSet(cUnit->loopAnalysis->isIndVarV,
                            mir->ssaRep->uses[useIdx])) {
                mir->OptimizationFlags |=
                    MIR_IGNORE_RANGE_CHECK | MIR_IGNORE_NULL_CHECK;
                updateRangeCheckInfo(cUnit, mir->ssaRep->uses[refIdx],
                                     mir->ssaRep->uses[useIdx]);
            }
        }
    }

    return !loopBodyCanThrow;
}

static void genHoistedChecks(CompilationUnit *cUnit)
{
    unsigned int i;
    BasicBlock *entry = cUnit->entryBlock;
    LoopAnalysis *loopAnalysis = cUnit->loopAnalysis;
    int globalMaxC = 0;
    int globalMinC = 0;
    /* Should be loop invariant */
    int idxReg = 0;

    for (i = 0; i < loopAnalysis->arrayAccessInfo->numUsed; i++) {
        ArrayAccessInfo *arrayAccessInfo =
            GET_ELEM_N(loopAnalysis->arrayAccessInfo,
                       ArrayAccessInfo*, i);
        int arrayReg = DECODE_REG(
            dvmConvertSSARegToDalvik(cUnit, arrayAccessInfo->arrayReg));
        idxReg = DECODE_REG(
            dvmConvertSSARegToDalvik(cUnit, arrayAccessInfo->ivReg));

        MIR *rangeCheckMIR = (MIR *)dvmCompilerNew(sizeof(MIR), true);
        rangeCheckMIR->dalvikInsn.opcode = (loopAnalysis->isCountUpLoop) ?
            (Opcode)kMirOpNullNRangeUpCheck : (Opcode)kMirOpNullNRangeDownCheck;
        rangeCheckMIR->dalvikInsn.vA = arrayReg;
        rangeCheckMIR->dalvikInsn.vB = idxReg;
        rangeCheckMIR->dalvikInsn.vC = loopAnalysis->endConditionReg;
        rangeCheckMIR->dalvikInsn.arg[0] = arrayAccessInfo->maxC;
        rangeCheckMIR->dalvikInsn.arg[1] = arrayAccessInfo->minC;
        rangeCheckMIR->dalvikInsn.arg[2] = loopAnalysis->loopBranchOpcode;
        rangeCheckMIR->dalvikInsn.arg[3] = arrayAccessInfo->loopIncrement;
        // set offset to the start offset of entry block
        // this will set rPC in case of bail to interpreter
        rangeCheckMIR->offset = entry->startOffset;
        dvmCompilerAppendMIR(entry, rangeCheckMIR);
        if (arrayAccessInfo->maxC > globalMaxC) {
            globalMaxC = arrayAccessInfo->maxC;
        }
        if (arrayAccessInfo->minC < globalMinC) {
            globalMinC = arrayAccessInfo->minC;
        }
    }

    if (loopAnalysis->arrayAccessInfo->numUsed != 0) {
        if (loopAnalysis->isCountUpLoop) {
            MIR *boundCheckMIR = (MIR *)dvmCompilerNew(sizeof(MIR), true);
            boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpLowerBound;
            boundCheckMIR->dalvikInsn.vA = idxReg;
            boundCheckMIR->dalvikInsn.vB = globalMinC;
            // set offset to the start offset of entry block
            // this will set rPC in case of bail to interpreter
            boundCheckMIR->offset = entry->startOffset;
            dvmCompilerAppendMIR(entry, boundCheckMIR);
        } else {
            if (loopAnalysis->loopBranchOpcode == OP_IF_LT ||
                loopAnalysis->loopBranchOpcode == OP_IF_LE) {
                MIR *boundCheckMIR = (MIR *)dvmCompilerNew(sizeof(MIR), true);
                boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpLowerBound;
                boundCheckMIR->dalvikInsn.vA = loopAnalysis->endConditionReg;
                boundCheckMIR->dalvikInsn.vB = globalMinC;
                // set offset to the start offset of entry block
                // this will set rPC in case of bail to interpreter
                boundCheckMIR->offset = entry->startOffset;
                /*
                 * If the end condition is ">" in the source, the check in the
                 * Dalvik bytecode is OP_IF_LE. In this case add 1 back to the
                 * constant field to reflect the fact that the smallest index
                 * value is "endValue + constant + 1".
                 */
                if (loopAnalysis->loopBranchOpcode == OP_IF_LE) {
                    boundCheckMIR->dalvikInsn.vB++;
                }
                dvmCompilerAppendMIR(entry, boundCheckMIR);
            } else if (loopAnalysis->loopBranchOpcode == OP_IF_LTZ) {
                /* Array index will fall below 0 */
                if (globalMinC < 0) {
                    MIR *boundCheckMIR = (MIR *)dvmCompilerNew(sizeof(MIR),
                                                               true);
                    boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpPunt;
                    // set offset to the start offset of entry block
                    // this will set rPC in case of bail to interpreter
                    boundCheckMIR->offset = entry->startOffset;
                    dvmCompilerAppendMIR(entry, boundCheckMIR);
                }
            } else if (loopAnalysis->loopBranchOpcode == OP_IF_LEZ) {
                /* Array index will fall below 0 */
                if (globalMinC < -1) {
                    MIR *boundCheckMIR = (MIR *)dvmCompilerNew(sizeof(MIR),
                                                               true);
                    boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpPunt;
                    // set offset to the start offset of entry block
                    // this will set rPC in case of bail to interpreter
                    boundCheckMIR->offset = entry->startOffset;
                    dvmCompilerAppendMIR(entry, boundCheckMIR);
                }
            } else {
                ALOGE("Jit: bad case in genHoistedChecks");
                dvmCompilerAbort(cUnit);
            }
        }
    }
}

#else //IA32

/**
 * @brief Used to flip the condition of an "if" bytecode
 * @param opcode Dalvik opcode to negate
 * @param negatedOpcode Updated by function to contain negated
 * opcode. Only valid if function returns true.
 * @return Returns true if successfully negated.
 */
static bool negateOpcode(Opcode opcode, Opcode & negatedOpcode)
{
    //Eagerly assume we will succeed
    bool success = true;

    switch (opcode) {
        /* reg/reg cmp */
        case OP_IF_EQ:
            negatedOpcode = OP_IF_NE;
            break;
        case OP_IF_NE:
            negatedOpcode = OP_IF_EQ;
            break;
        case OP_IF_LT:
            negatedOpcode = OP_IF_GE;
            break;
        case OP_IF_GE:
            negatedOpcode = OP_IF_LT;
            break;
        case OP_IF_GT:
            negatedOpcode = OP_IF_LE;
            break;
        case OP_IF_LE:
            negatedOpcode = OP_IF_GT;
            break;
        /* reg/zero cmp */
        case OP_IF_EQZ:
            negatedOpcode = OP_IF_NEZ;
            break;
        case OP_IF_NEZ:
            negatedOpcode = OP_IF_EQZ;
            break;
        case OP_IF_LTZ:
            negatedOpcode = OP_IF_GEZ;
            break;
        case OP_IF_GEZ:
            negatedOpcode = OP_IF_LTZ;
            break;
        case OP_IF_GTZ:
            negatedOpcode = OP_IF_LEZ;
            break;
        case OP_IF_LEZ:
            negatedOpcode = OP_IF_GTZ;
            break;
        default:
            success = false;
            break;
    }

    return success;
}

/*
 * A loop is considered optimizable if:
 * 1) It has one basic induction variable.
 * 2) The loop back branch compares the BIV with a constant.
 * 3) We need to normalize the loop exit condition so that the loop is exited
 *    via the taken path.
 * 4) If it is a count-up loop, the condition is GE/GT. Otherwise it is
 *    LE/LT/LEZ/LTZ for a count-down loop.
*/

/**
  * @brief Checks if the loops is suitable for hoisting range/null checks
  * @param cUnit the CompilationUnit
  * @param pass the Pass
  * @return false for loops that fail the above tests.
  */
//TODO: this should take a LoopInformation to be tested for inner loops
static bool isSimpleCountedLoop(CompilationUnit *cUnit)
{
    unsigned int i;
    BasicBlock *loopBackBlock = NULL;
    LoopInformation *loopInfo = cUnit->loopInformation;
    GrowableList* ivList = &cUnit->loopInformation->getInductionVariableList ();

    /* This either counted up or down loop, 2 BIVs could bring complication
       to detect that. Potentially we can enhance the algorithm to utilize > 1
       BIV in case inc for all BIVs > 0 ( or < 0)
    */
    if (loopInfo->getNumBasicIV(cUnit) != 1) {
        return false;
    }

    for (i = 0; i < ivList->numUsed; i++) {
        InductionVariableInfo *ivInfo;

        ivInfo = GET_ELEM_N(ivList, InductionVariableInfo*, i);
        /* Count up or down loop? */
        if (ivInfo->isBasicIV () == true) {
            /* Infinite loop */
            if (ivInfo->loopIncrement == 0) {
                return false;
            }
            loopInfo->setCountUpLoop(ivInfo->loopIncrement > 0);
            break;
        }
    }

    // Get Back branch bb, need to find loop exit condition
    // the main hypotethis is that Back branch bb is a
    // predecessor of a loop exit bb.

    BasicBlock *bb = NULL;
    BitVectorIterator bvIterator;
    BitVector *exitbbs = const_cast<BitVector *> (loopInfo->getExitLoops());
    int nexitbbs = dvmCountSetBits(exitbbs);

    // We limit the optimization to cases where just 1 exit bb
    // due to unpredictable behaviour in other cases
    if (nexitbbs != 1) {
        return false;
    }

    /* Get loopBack branch bb */

    // 1. Get exit bb
    dvmBitVectorIteratorInit (exitbbs, &bvIterator);
    int bIdx = dvmBitVectorIteratorNext (&bvIterator);
    assert (bIdx != -1);
    bb = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, bIdx);
    if (bb == NULL) {
        return false;
    }

    // 2. Get loop exit bb predecessor
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    bIdx = dvmBitVectorIteratorNext(&bvIterator);
    assert (bIdx != -1);

    // 3. Finally get loopBack branch bb
    loopBackBlock = (BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, bIdx));
    // paranoid, didn't find loopBack bb
    if (loopBackBlock == NULL) {
        return false;
    }

    // Find exit block which contains loop exit condition
    MIR *branch = loopBackBlock->lastMIRInsn;
    if (branch == NULL) {
        return false;
    }

    Opcode op = branch->dalvikInsn.opcode;
    Opcode opcode = op;

    // Paranoid: Check this is not extendent MIR because
    // dexGetFlagsFromOpcode call is not safe then
    if (opcode >= kNumPackedOpcodes) {
        return false;
    }
    /* Last instruction is not a conditional branch - bail */
    if (dexGetFlagsFromOpcode(opcode) != (kInstrCanContinue|kInstrCanBranch)) {
        return false;
    }

    int endSSAReg;
    int endDalvikReg;

    /* Detect end condition register
       As soon as we found loop back branch we can
       get the condition and a loop limit from it
    */
    if (branch->ssaRep->numUses == 2)
    {
        if (branch->ssaRep->uses[0] == loopInfo->getSSABIV()) {
            endSSAReg = branch->ssaRep->uses[1];
        } else if (branch->ssaRep->uses[1] == loopInfo->getSSABIV()) {
            endSSAReg = branch->ssaRep->uses[0];
            negateOpcode(op, opcode);
        } else {
            return false;
        }
        endDalvikReg = dvmConvertSSARegToDalvik(cUnit, endSSAReg);
        /*
         * If the comparison is not between the BIV and a loop invariant,
         * return false. endDalvikReg is loop invariant if one of the
         * following is true:
         * - It is not defined in the loop (ie DECODE_SUB returns 0)
         * - It is reloaded with a constant
         */
        if ((DECODE_SUB(endDalvikReg) != 0) &&
                dvmCompilerIsRegConstant (cUnit, endSSAReg) == false) {
            return false;
        }
    } else {
        return false;
    }

    if (loopInfo->isCountUpLoop() == true) {
        /*
         * If the normalized condition op is not > or >=, this is not an
         * optimization candidate.
         */
        switch (opcode) {
            case OP_IF_GT:
            case OP_IF_GE:
                break;
            default:
                return false;
        }
        loopInfo->setEndConditionReg(DECODE_REG(endDalvikReg));
    } else  {
        /*
         * If the normalized condition op is not < or <=, this is not an
         * optimization candidate.
         */
        switch (opcode) {
            case OP_IF_LT:
            case OP_IF_LE:
                loopInfo->setEndConditionReg(DECODE_REG(endDalvikReg));
                break;
            case OP_IF_LTZ:
            case OP_IF_LEZ:
                break;
            default:
                return false;
        }
    }

    /*
     * Remember the normalized opcode, which will be used to determine the end
     * value used for the yanked range checks.
     */
    loopInfo->setLoopBranchOpcode(opcode);

    return true;
}


/**
  * @brief Record the upper and lower bound information for range checks for each IV
  * @param cUnit the CompilationUnit
  * @param arrayReg the array register
  * @param idxReg the array index register
  */
// If array A is accessed by index "i+5", the upper and
// lower bound will be len(A)-5 and -5, respectively.

static void updateRangeCheckInfo(CompilationUnit *cUnit, int arrayReg,
                                 int idxReg)
{
    InductionVariableInfo *ivInfo;
    //Get the IV list
    GrowableList* ivList = &cUnit->loopInformation->getInductionVariableList ();
    LoopInformation *loopInfo = cUnit->loopInformation;
    assert (loopInfo != NULL);
    unsigned int i, j;

    // when the tested idxReg is found to be an IV this is an array access point.
    // As soon as such point is found we create array access info or update existing one.
    // The update means identification of maxC and minC which are the min/max of the same index.
    // E.g. A[i], A[i+1], ..., A[i+N] will result in maxC = N. It will be used to aggregate
    // several range checks into a single hoisted one.
    for (i = 0; i < ivList->numUsed; i++) {
        ivInfo = GET_ELEM_N(ivList, InductionVariableInfo*, i);
        if (ivInfo->ssaReg == idxReg) {
            ArrayAccessInfo *arrayAccessInfo = NULL;
            for (j = 0; j < loopInfo->getArrayAccessInfo()->numUsed; j++) {
                ArrayAccessInfo *existingArrayAccessInfo =
                    GET_ELEM_N(loopInfo->getArrayAccessInfo(),
                               ArrayAccessInfo*,
                               j);
                if (existingArrayAccessInfo->arrayReg == arrayReg) {
                    if (ivInfo->constant > existingArrayAccessInfo->maxC) {
                        existingArrayAccessInfo->maxC = ivInfo->constant;
                    }
                    if (ivInfo->constant < existingArrayAccessInfo->minC) {
                        existingArrayAccessInfo->minC = ivInfo->constant;
                    }
                    arrayAccessInfo = existingArrayAccessInfo;
                    break;
                }
            }
            if (arrayAccessInfo == NULL) {
                arrayAccessInfo =
                    (ArrayAccessInfo *)dvmCompilerNew(sizeof(ArrayAccessInfo),
                                                      false);
                arrayAccessInfo->ivReg = ivInfo->basicSSAReg;
                arrayAccessInfo->arrayReg = arrayReg;
                arrayAccessInfo->maxC = (ivInfo->constant > 0) ? ivInfo->constant : 0;
                arrayAccessInfo->minC = (ivInfo->constant < 0) ? ivInfo->constant : 0;
                arrayAccessInfo->inc = ivInfo->loopIncrement;
                dvmInsertGrowableList(cUnit->loopInformation->getArrayAccessInfo(),
                                      (intptr_t) arrayAccessInfo);
            }
            break;
        }
    }
}

void dvmCompilerBodyCodeMotion (CompilationUnit *cUnit, Pass *currentPass)
{
    //Get the BasicBlock vector for this loop
    BitVector *blocks = const_cast<BitVector *> (cUnit->loopInformation->getBasicBlocks ());
    MIR *mir;
    //Iterate through basic blocks
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (blocks, &bvIterator);
    while (true)
    {
        //Get block index
        int blockIdx = dvmBitVectorIteratorNext (&bvIterator);
        //If done, bail
        if (blockIdx == -1)
        {
            break;
        }
        BasicBlock *loopBody = (BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, blockIdx));
        //Paranoid
        if (loopBody == 0)
        {
            break;
        }

        for (mir = loopBody->firstMIRInsn; mir; mir = mir->next) {
            DecodedInstruction *dInsn = &mir->dalvikInsn;
            long long dfAttributes =
                dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

            /* Skip extended MIR instructions */
            if (dInsn->opcode >= kNumPackedOpcodes) {
                continue;
            }

            int instrFlags = dexGetFlagsFromOpcode(dInsn->opcode);

            /* Instruction is clean */
            if ((instrFlags & kInstrCanThrow) == 0) {
                continue;
            }

            /*
             * Currently we can only optimize away null and range checks.
             */
            if ((dfAttributes & DF_HAS_NR_CHECKS) == 0) {
                continue;
            }

            /*
             * This comparison is redundant now, but we will have more than one
             * group of flags to check soon.
             */
            if (dfAttributes & DF_HAS_NR_CHECKS) {
                /*
                 * Check if the null check is applied on a loop invariant register?
                 * If the register's SSA id is less than the number of Dalvik
                 * registers, then it is loop invariant.
                 */
                int refIdx;
                switch (dfAttributes & DF_HAS_NR_CHECKS) {
                    case DF_NULL_N_RANGE_CHECK_0:
                        refIdx = 0;
                        break;
                    case DF_NULL_N_RANGE_CHECK_1:
                        refIdx = 1;
                        break;
                    case DF_NULL_N_RANGE_CHECK_2:
                        refIdx = 2;
                        break;
                    default:
                        refIdx = 0;
                        ALOGE("Jit: bad case in dvmCompilerBodyCodeMotion");
                        // bail - should not reach here
                        dvmCompilerAbort(cUnit);
                        return;
                }

                int useIdx = refIdx + 1;
                int subNRegArray =
                    dvmConvertSSARegToDalvik(cUnit, mir->ssaRep->uses[refIdx]);
                int arraySub = DECODE_SUB(subNRegArray);

                /*
                 * If the register is never updated in the loop (ie subscript == 0),
                 * it is an optimization candidate.
                 */
                if (arraySub != 0) {
                    continue;
                }

                /*
                 * Then check if the range check can be hoisted out of the loop if
                 * it is basic or dependent induction variable.
                 */
                if (cUnit->loopInformation->isAnInductionVariable(cUnit, mir->ssaRep->uses[useIdx], true)) {
                    mir->OptimizationFlags |=
                        MIR_IGNORE_RANGE_CHECK | MIR_IGNORE_NULL_CHECK;
                    updateRangeCheckInfo(cUnit, mir->ssaRep->uses[refIdx],
                                     mir->ssaRep->uses[useIdx]);
                }
            }
        }
    }

    //Unused argument
    (void) currentPass;
}

bool dvmCompilerHoistedChecksGate(const CompilationUnit* cUnit, Pass* pass)
{
    if (cUnit->loopInformation != NULL && isSimpleCountedLoop((CompilationUnit*)cUnit)) {
        return true;
    }

    //Unused argument
    (void) pass;

    return false;
}

/**
  * @brief Dump hoisted checks debugging info
  * @param cUnit is the CompilationUnit
  */
static void dvmDumpHoistedRangeCheckInfo(CompilationUnit* cUnit)
{
    LoopInformation* loopInfo = cUnit->loopInformation;
    InductionVariableInfo *ivInfo;
    //Get the IV list
    GrowableList* ivList = &loopInfo->getInductionVariableList ();
    unsigned int i;

    /* dump IV info */
    ALOGD("BASIC_IV_NUMBER_INFO: number of basic IV: %d", loopInfo->getNumBasicIV(cUnit));
    for (i = 0; i < ivList->numUsed; i++)
    {
        ivInfo = GET_ELEM_N(ivList, InductionVariableInfo*, i);

        if (ivInfo->isBasicIV () == true)
        {
            /* Basic IV */
            ALOGD("BASIC_IV_INFO: ssaReg: %d, basicSSAReg: %d, inc: %d, VR: %d_%dn", ivInfo->ssaReg, ivInfo->basicSSAReg, ivInfo->loopIncrement,
                               dvmExtractSSARegister (cUnit, ivInfo->ssaReg), dvmExtractSSASubscript (cUnit, ivInfo->ssaReg));
        }
        else
        {
            /* Dependent IV */
            ALOGD("DEPENDENT_IV_INFO: ssaReg: %d, depSSAReg: %d, inc: %d, VR: %d_%d c=%d\n", ivInfo->ssaReg, ivInfo->basicSSAReg, ivInfo->loopIncrement,
                               dvmExtractSSARegister (cUnit, ivInfo->ssaReg), dvmExtractSSASubscript (cUnit, ivInfo->ssaReg), ivInfo->constant);
        }
    }

    /* dump array access info */
    for (i = 0; i < loopInfo->getArrayAccessInfo()->numUsed; i++) {
        ArrayAccessInfo *arrayAccessInfo =
            GET_ELEM_N(loopInfo->getArrayAccessInfo(),
                       ArrayAccessInfo*, i);
        ALOGE("JIT_INFO: arrayReg: %d, idxReg: %d, endConditionReg: %d, maxC: %d, minC: %d, inc: %d",
                     arrayAccessInfo->arrayReg, arrayAccessInfo->ivReg, loopInfo->getEndConditionReg(),
                         arrayAccessInfo->maxC, arrayAccessInfo->minC, arrayAccessInfo->inc);
    }
}
#endif


#ifdef ARCH_IA32
// The main purpose of the function is to transform internal array access info into
// hoisted checks extended MIRs at start of a loop which will be transformed to
// assembly using special algorithm and data from hoisted checks MIR's
// Terms: e.g. for (int i=0; i<=100; i+=2) {A[i]...}
// A - array, i - index, end condition reg - 100 (reg), inc - 2(i+=2)
// For loop body like {A[i-1] ...  a[i+N]} maxC = N, minC = -1
// loopbranch opcode - >/>=/</<=, counted up/down cycle - ?inc >0 or <0
void dvmCompilerGenHoistedChecks(CompilationUnit *cUnit, Pass* pass)
{
    unsigned int i;
    if (cUnit->loopInformation == NULL) {
        return;
    }

    BasicBlock *entry = cUnit->entryBlock;
    LoopInformation* loopInfo = cUnit->loopInformation;
    int globalMaxC = 0;
    int globalMinC = 0;
    /* Should be loop invariant */
    int idxReg = 0;

    //TODO The offset in entry->offset may not be correct to use. The offset for exception
    //must use the offset of the first instruction in block before heavy optimizations are
    //applied like invariant hoisting. The same applies for the parent method for these
    //extended MIRs. They technically have no source method but the one matching the first
    //instruction in loop should be assigned. This ensures that correct exit PC is set
    //in case these checks lead to exception.
    const unsigned int offsetForException = entry->startOffset;
    NestedMethod nesting (cUnit->method);

    // Go through array access elements and generate range checks
    // Range check in the current implemntation is the upper border of the loop
    // E.g. for count down loops it is lowest index
    // Lower border check of a loop is done using Bound checks below
    for (i = 0; i < loopInfo->getArrayAccessInfo()->numUsed; i++) {
        ArrayAccessInfo *arrayAccessInfo =
            GET_ELEM_N(loopInfo->getArrayAccessInfo(),
                       ArrayAccessInfo*, i);
        // get reg containing array ref
        int arrayReg = DECODE_REG(
            dvmConvertSSARegToDalvik(cUnit, arrayAccessInfo->arrayReg));
        // get reg containing index
        idxReg = DECODE_REG(
            dvmConvertSSARegToDalvik(cUnit, arrayAccessInfo->ivReg));

        // create new mir using the array access info
        // see genHoistedChecks* to check with the hoisted check algorithm
        MIR *rangeCheckMIR = dvmCompilerNewMIR ();
        rangeCheckMIR->dalvikInsn.opcode = (loopInfo->isCountUpLoop()) ?
            (Opcode)kMirOpNullNRangeUpCheck : (Opcode)kMirOpNullNRangeDownCheck;
        rangeCheckMIR->dalvikInsn.vA = arrayReg;
        rangeCheckMIR->dalvikInsn.vB = idxReg;
        rangeCheckMIR->dalvikInsn.vC = loopInfo->getEndConditionReg();
        rangeCheckMIR->dalvikInsn.arg[0] = arrayAccessInfo->maxC;
        rangeCheckMIR->dalvikInsn.arg[1] = arrayAccessInfo->minC;
        rangeCheckMIR->dalvikInsn.arg[2] = loopInfo->getLoopBranchOpcode();
        rangeCheckMIR->dalvikInsn.arg[3] = arrayAccessInfo->inc;

        // set offset to the start offset of entry block
        // this will set rPC in case of bail to interpreter
        rangeCheckMIR->offset = offsetForException;
        rangeCheckMIR->nesting = nesting;
        dvmCompilerAppendMIR(entry, rangeCheckMIR);
        // To do bound check we need to know globalMaxC/globalMinC value
        // as soon as we're limited with just one BIV globalMaxC will contain
        // the max/min index change inside a loop body
        if (arrayAccessInfo->maxC > globalMaxC) {
            globalMaxC = arrayAccessInfo->maxC;
        }
        if (arrayAccessInfo->minC < globalMinC) {
            globalMinC = arrayAccessInfo->minC;
        }
    }

    // Insert Bound check (lower loop border check)
    // See the bound check algorithm in GenHoistedBoundCheck function
    // Bound check values should be adjusted to meet loop branch condition
    if (loopInfo->getArrayAccessInfo()->numUsed != 0) {
        if (loopInfo->isCountUpLoop()) {
            MIR *boundCheckMIR = dvmCompilerNewMIR ();
            boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpLowerBound;
            boundCheckMIR->dalvikInsn.vA = idxReg;
            boundCheckMIR->dalvikInsn.vB = globalMinC;
            // set offset to the start offset of entry block
            // this will set rPC in case of bail to interpreter
            boundCheckMIR->offset = offsetForException;
            boundCheckMIR->nesting = nesting;
            dvmCompilerAppendMIR(entry, boundCheckMIR);
     } else {
            if (loopInfo->getLoopBranchOpcode() == OP_IF_LT ||
                loopInfo->getLoopBranchOpcode() == OP_IF_LE) {
                MIR *boundCheckMIR = dvmCompilerNewMIR ();
                boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpLowerBound;
                boundCheckMIR->dalvikInsn.vA = loopInfo->getEndConditionReg();
                boundCheckMIR->dalvikInsn.vB = globalMinC;
                // set offset to the start offset of entry block
                // this will set rPC in case of bail to interpreter
                boundCheckMIR->offset = offsetForException;
                boundCheckMIR->nesting = nesting;
                /*
                 * If the end condition is ">" in the source, the check in the
                 * Dalvik bytecode is OP_IF_LE. In this case add 1 back to the
                 * constant field to reflect the fact that the smallest index
                 * value is "endValue + constant + 1".
                 */
                if (loopInfo->getLoopBranchOpcode() == OP_IF_LE) {
                    boundCheckMIR->dalvikInsn.vB++;
             }
                dvmCompilerAppendMIR(entry, boundCheckMIR);
            } else if (loopInfo->getLoopBranchOpcode() == OP_IF_LTZ) {
                /* Array index will fall below 0 */
                if (globalMinC < 0) {
                    MIR *boundCheckMIR = dvmCompilerNewMIR ();
                    boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpPunt;
                    // set offset to the start offset of entry block
                    // this will set rPC in case of bail to interpreter
                    boundCheckMIR->offset = offsetForException;
                    boundCheckMIR->nesting = nesting;
                    dvmCompilerAppendMIR(entry, boundCheckMIR);
                }
            } else if (loopInfo->getLoopBranchOpcode() == OP_IF_LEZ) {
                /* Array index will fall below 0 */
                if (globalMinC < -1) {
                    MIR *boundCheckMIR = dvmCompilerNewMIR ();
                    boundCheckMIR->dalvikInsn.opcode = (Opcode)kMirOpPunt;
                    // set offset to the start offset of entry block
                    // this will set rPC in case of bail to interpreter
                    boundCheckMIR->offset = offsetForException;
                    boundCheckMIR->nesting = nesting;
                    dvmCompilerAppendMIR(entry, boundCheckMIR);
                }
            } else {
                ALOGE("Jit: bad case in genHoistedChecks");
                dvmCompilerAbort(cUnit);
            }
        }
    }

    if (cUnit->printPass == true)
    {
        dvmDumpHoistedRangeCheckInfo(cUnit);
    }

    (void) pass;
}
#endif

void resetBlockEdges(BasicBlock *bb)
{
    bb->taken = NULL;
    bb->fallThrough = NULL;
    bb->successorBlockList.blockListType = kNotUsed;
}

static bool clearPredecessorVector(struct CompilationUnit *cUnit,
                                   struct BasicBlock *bb)
{
    dvmClearAllBits(bb->predecessors);
    return false;
}

/**
 * @brief Check if a BasicBlock has a restricted instruction for a loop
 *        Certain opcodes cannot be included in a loop formation (in the nonFixableOpcodes array)
 *        Certain opcodes can be "fixed" if the function handleFixableOpcode returns true and thus won't be cause to reject the loop
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return whether or not we accept the BasicBlock in regards to the instructions
 */
static bool checkBBInstructionsForCFGLoop (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Non fixable opcodes: tested against the bb's MIR instructions
    //If present, there is nothing we can do about it
    static unsigned int nonFixableOpcodes[] = {
            OP_RETURN_VOID,
            OP_RETURN,
            OP_RETURN_WIDE,
            OP_RETURN_OBJECT,
            OP_MONITOR_ENTER,
            OP_MONITOR_EXIT,
            OP_NEW_INSTANCE,
            OP_NEW_ARRAY,
            OP_THROW,
            OP_RETURN_VOID_BARRIER,
            OP_BREAKPOINT,
            OP_THROW_VERIFICATION_ERROR,

            //We reject virtual/interface invokes because we have no mechanism yet for method prediction.
            //Thus we prefer that we get the trace which has the runtime prediction.
            OP_INVOKE_VIRTUAL,
            OP_INVOKE_VIRTUAL_RANGE,
            OP_INVOKE_VIRTUAL_QUICK,
            OP_INVOKE_VIRTUAL_QUICK_RANGE,
            OP_INVOKE_INTERFACE,
            OP_INVOKE_INTERFACE_RANGE,
    };

    //Go through each instruction
    unsigned int nbr = sizeof (nonFixableOpcodes) / sizeof (nonFixableOpcodes[0]);
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Go through each non supported instructions
        for (unsigned int i = 0; i < nbr; i++)
        {
            //If we don't support it, just quit
            if (mir->dalvikInsn.opcode == nonFixableOpcodes[i])
            {
                if (cUnit->printMe == true)
                {
                    ALOGD("JIT_INFO: Loop trace @ offset %04x not a loop: unsupported opcode in loop : %s",
                          cUnit->entryBlock->startOffset, dexGetOpcodeName(mir->dalvikInsn.opcode));
                }
                return false;
            }
        }
    }

    //If we got here we are good to go
    return true;
}

/**
 * @brief Accept a CFG as a loop (Helper function)
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return whether or not the trace is a loop but acceptCFGLoops must still check min and max
 */
static bool acceptCFGLoopsHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Paranoid check
    if (bb == 0)
    {
        return true;
    }

    //Visited check
    if (bb->visited == true)
    {
        return true;
    }

    //Color it now
    bb->visited = true;

    //If hidden, we stop
    if (bb->hidden == true)
    {
        return true;
    }

    //Check instructions: add the restrictive, we will try to inline later
    bool res = checkBBInstructionsForCFGLoop (cUnit, bb);

    //If it says no, we say no
    if (res == false)
    {
        return false;
    }

    //Now mark it as a BasicBlock of the loop
    dvmCompilerSetBit(cUnit->tempBlockV, bb->id);

    //Now recurse
    res = acceptCFGLoopsHelper (cUnit, bb->taken) && acceptCFGLoopsHelper (cUnit, bb->fallThrough);

    //Return it, whatever it is
    return res;
}

/**
 * @brief Go from the BasicBlock cur to its predecessors, up until first
 * @param cUnit the CompilationUnit
 * @param cur the current BasicBlock
 * @param first the start of the loop
 */
static void climbTheLoopHelper (CompilationUnit *cUnit, BasicBlock *cur, const BasicBlock *first)
{
    BitVectorIterator bvIterator;

    //Paranoid
    assert (cur != 0 && cur->predecessors != 0);

    //Have we visited it yet: normally redundant but paranoid
    if (cur->visited == true)
    {
        return;
    }
    cur->visited = true;

    //Unhide the current block
    cur->hidden = false;

    //Are we done?
    if (cur == first)
    {
        return;
    }

    //Get predecessors
    dvmBitVectorIteratorInit(cur->predecessors, &bvIterator);

    while (true) {
        //Get the next iterator
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

        //If it is finished, exit
        if (blockIdx == -1)
        {
            break;
        }

        BasicBlock *predBB = (BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, blockIdx));

        //Paranoid
        if (predBB == 0)
        {
            break;
        }

        //We found a trail, enable it from first
        if (predBB->taken == cur)
        {
            predBB->taken->hidden = false;
        }
        else
        {
            //Then it must be fallThrough
            assert (predBB->fallThrough == cur);

            predBB->fallThrough->hidden = false;
        }

        //Continue up
        climbTheLoopHelper (cUnit, predBB, first);
    }
}

/*
 * @brief Go from the BasicBlock cur downwards to bottom but bail at notLoop
 * @param cUnit the CompilationUnit
 * @param cur the current BasicBlock
 * @param bottom the end of the loop
 * @param notLoop the exit of the loop
 */
static void descendTheLoopHelper (CompilationUnit *cUnit, BasicBlock *cur, BasicBlock *bottom, BasicBlock *notLoop)
{
    //If nil, or notLoop
    if (cur == 0 || cur == notLoop)
    {
        return;
    }

    //Have we visited it yet: normally redundant but paranoid
    if (cur->visited == true)
    {
        return;
    }
    cur->visited = true;

    //Unhide the current block
    cur->hidden = false;

    //Are we done?
    if (cur == bottom)
    {
        return;
    }

    //Now call children
    descendTheLoopHelper (cUnit, cur->taken, bottom, notLoop);
    descendTheLoopHelper (cUnit, cur->fallThrough, bottom, notLoop);
}

/**
 * @brief Walk the loop from its predecessors that form the loop
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock that is the start of the loop
 */
static bool walkTheLoop (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Get first BasicBlock of the loop
    BasicBlock *first = cUnit->entryBlock->fallThrough;

    //Is it a backward branch
    if (bb->loopTraversalType.walkBackward == true)
    {
        climbTheLoopHelper (cUnit, bb, first);

        //Now what can happen is that we have inter-twined loops,
        //So actually now hide again any child of bb that is not the first
        if (bb->taken != 0 && bb->taken != first)
        {
            bb->taken->hidden = true;
        }
        if (bb->fallThrough != 0 && bb->fallThrough != first)
        {
            bb->fallThrough->hidden = true;
        }
    }
    else
    {
        //Or is it a forward loop
        if (bb->loopTraversalType.walkForward == true)
        {
            //A BB could be both, but we reject the double case if we are walking backwards
            //To the first BB
            if (bb->loopTraversalType.walkBackward == true &&
                    (bb->taken == first || bb->fallThrough == first))
            {
                return false;
            }

            //Find the notLoop
            BasicBlock *notLoop = bb->taken;

            if (notLoop == first)
            {
                notLoop = bb->fallThrough;
            }

            descendTheLoopHelper (cUnit, first, bb, notLoop);
        }
    }

    return false;
}

/**
 * @brief Clear visited and hide dispatched function
 * @param cUnit the CompilationUnit
 * @param bb the current BasicBlock
 * @return returns true to signal we changed the BasicBlock
 */
static bool clearVisitedAndHide (CompilationUnit *cUnit, BasicBlock *bb)
{
    bb->visited = false;
    bb->hidden = true;
    return true;
}

/**
 * @brief Is the loop bottom formed?
 * @param cUnit the CompilationUnit
 * @param first the first BasicBlock of the trace
 * @return whether or not the loop is bottom formed
 */
static bool isBottomFormed (CompilationUnit *cUnit, BasicBlock *first)
{
    //We still have work to do if it isn't topFormed
    BitVectorIterator bvIterator;

    //Paranoid
    assert (first->predecessors != 0);

    //Get predecessors
    dvmBitVectorIteratorInit(first->predecessors, &bvIterator);

    while (true) {
        //Get the next iterator
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

        //If it is finished, exit
        if (blockIdx == -1)
        {
            break;
        }

        BasicBlock *predBB = (BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, blockIdx));

        if (predBB == 0)
        {
            continue;
        }

        //If predBB is first, we can skip it
        if (first == predBB)
        {
            continue;
        }

        //Is the predBB kDalvikByteCode, one child must be towards first
        if (predBB->blockType == kDalvikByteCode)
        {
            if (predBB->taken == first)
            {
                if (predBB->fallThrough == 0 || predBB->fallThrough->hidden == false)
                {
                    return false;
                }
            }
            else
            {
                if (predBB->fallThrough == first)
                {
                    if (predBB->taken == 0 || predBB->taken->hidden == false)
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool acceptOldLoops (CompilationUnit *cUnit)
{
    BasicBlock *firstBB = cUnit->entryBlock->fallThrough;

    /* Record blocks included in the loop */
    dvmClearAllBits(cUnit->tempBlockV);

    dvmCompilerSetBit(cUnit->tempBlockV, cUnit->entryBlock->id);
    dvmCompilerSetBit(cUnit->tempBlockV, firstBB->id);

    BasicBlock *bodyBB = firstBB;

    /*
     * First try to include the fall-through block in the loop, then the taken
     * block. Stop loop formation on the first backward branch that enters the
     * first block (ie only include the inner-most loop).
     */
    while (true) {
        /* Loop formed */
        if (bodyBB->taken == firstBB) {
            /* Check if the fallThrough edge will cause a nested loop */
            if (bodyBB->fallThrough && dvmIsBitSet(cUnit->tempBlockV, bodyBB->fallThrough->id)) {
                return false;
            }
            /* Single loop formed */
            break;
        } else if (bodyBB->fallThrough == firstBB) {
            /* Check if the taken edge will cause a nested loop */
            if (bodyBB->taken && dvmIsBitSet(cUnit->tempBlockV, bodyBB->taken->id)) {
                return false;
            }
            /* Single loop formed */
            break;
        }

        /* Inner loops formed first - quit */
        if (bodyBB->fallThrough && dvmIsBitSet(cUnit->tempBlockV, bodyBB->fallThrough->id)) {
            return false;
        }

        if (bodyBB->taken && dvmIsBitSet(cUnit->tempBlockV, bodyBB->taken->id)) {
            return false;
        }

        if (bodyBB->fallThrough) {
            if (bodyBB->fallThrough->iDom == bodyBB) {
                bodyBB = bodyBB->fallThrough;
                dvmCompilerSetBit(cUnit->tempBlockV, bodyBB->id);
                /*
                 * Loop formation to be detected at the beginning of next
                 * iteration.
                 */
                continue;
            }
        }
        if (bodyBB->taken) {
            if (bodyBB->taken->iDom == bodyBB) {
                bodyBB = bodyBB->taken;
                dvmCompilerSetBit(cUnit->tempBlockV, bodyBB->id);
                /*
                 * Loop formation to be detected at the beginning of next
                 * iteration.
                 */
                continue;
            }
        }
        /*
         * Current block is not the immediate dominator of either fallthrough
         * nor taken block - bail out of loop formation.
         */
        return false;
    }

    //Loop accepted
    return true;
}

/**
 * @brief Accept a CFG as a loop
 * @param cUnit the CompilationUnit
 * @return whether or not the trace is a loop
 */
static bool acceptCFGLoops (CompilationUnit *cUnit)
{
    BasicBlock *first = cUnit->entryBlock->fallThrough;

    //Clear visiting flags
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, clearVisitedAndHide, kAllNodes, false /* isIterative */);

    //We must go through the list by hand because the dispatcher looks at hidden and we set it to true
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    while (true) {
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);

        //Paranoid
        if (bb == NULL)
        {
            break;
        }

        //Ok, either it is the first, or it goes towards the first
        if (bb != first && bb->taken != first && bb->fallThrough != first)
        {
            continue;
        }

        //Call back to walk the loop: we only care about the outer loop
        walkTheLoop (cUnit, bb);
    }

    //Unhide the entry
    cUnit->entryBlock->hidden = false;

    //We have a loop if first->taken or first->fallThrough are not hidden and we aren't either
    bool res = first->hidden;

    if (res == true)
    {
        if (cUnit->printMe == true)
        {
            ALOGD("JIT_INFO: Loop trace @ offset %04x not a loop: first BB hidden",
                  cUnit->entryBlock->startOffset);
        }
        return false;
    }

    //Otherwise, look at the children
    res = (first->taken != 0 && first->taken->hidden == false) ||
          (first->fallThrough != 0 && first->fallThrough->hidden == false);

    if (res == false)
    {
        if (cUnit->printMe == true)
        {
            ALOGD("JIT_INFO: Loop trace @ offset %04x not a loop: children of first BB hidden",
                  cUnit->entryBlock->startOffset);
        }
        return false;
    }

    //Clear bits
    dvmClearAllBits (cUnit->tempBlockV);

    //Reset flags
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, dvmCompilerClearVisitedFlag, kAllNodes, false);

    //Call the helper
    bool found = acceptCFGLoopsHelper (cUnit, cUnit->entryBlock);

    //Ok, if the acceptance returned false, we are done
    if (found == false)
    {
        // message logged above
        return false;
    }

    //Final step check if it is top formed or bottom formed
    bool topFormed = (first->taken != 0 && first->taken->hidden == true) ||
                      (first->fallThrough != 0 && first->fallThrough->hidden == true);

    if (topFormed == false)
    {
        //If it isn't top formed, it must be bottom formed
        bool res = isBottomFormed (cUnit, first);
        if (res == false && cUnit->printMe == true)
        {
            ALOGD("JIT_INFO: Loop trace @ offset %04x not a loop: not top or bottom formed",
                  cUnit->entryBlock->startOffset);
        }
        return res;
    }

    return true;
}

bool dvmCompilerFilterLoopBlocks(CompilationUnit *cUnit)
{
    BasicBlock *firstBB = cUnit->entryBlock->fallThrough;

    //We should only have one exit chaining cell of the loop
    bool normalChainingAdded = false;

    int numPred = dvmCountSetBits(firstBB->predecessors);
    /*
     * A loop body should have at least two incoming edges.
     */
    if (numPred < 2) {
        if (cUnit->printMe == true)
        {
            ALOGD("JIT_INFO: Loop trace @ offset %04x not a loop: only one predecessor",
                  cUnit->entryBlock->startOffset);
        }
        return false;
    }

    //Let us see if we can accept the loop
    //We have two loop acceptance systems: the new system and the old one, which one do we want?
    bool acceptIt = false;

    if (gDvmJit.oldLoopDetection == false)
    {
        acceptIt = acceptCFGLoops (cUnit);
    }
    else
    {
        acceptIt = acceptOldLoops (cUnit);
    }

    //If the acceptance bailed on us, we bail as well
    if (acceptIt == false)
    {
        return false;
    }

    /* Now mark blocks not included in the loop as hidden */
    GrowableList *blockList = &cUnit->blockList;
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(blockList, &iterator);
    while (true) {
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bb == NULL) break;
        if (!dvmIsBitSet(cUnit->tempBlockV, bb->id)) {
            bb->hidden = true;
            /* Clear the insn list */
            bb->firstMIRInsn = bb->lastMIRInsn = NULL;
            resetBlockEdges(bb);
        }
    }

    dvmCompilerDataFlowAnalysisDispatcher(cUnit, clearPredecessorVector,
                                          kAllNodes, false /* isIterative */);

    dvmGrowableListIteratorInit(blockList, &iterator);
    while (true) {
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bb == NULL) break;
        if (dvmIsBitSet(cUnit->tempBlockV, bb->id)) {
            if (bb->taken) {
                /*
                 * exit block means we run into control-flow that we don't want
                 * to handle.
                 */
                if (bb->taken == cUnit->exitBlock) {
                    if (cUnit->printMe == true)
                    {
                        ALOGD("JIT_INFO: Loop trace @ offset %04x taken branch to exit block",
                              cUnit->entryBlock->startOffset);
                    }
                    return false;
                }
                if (bb->taken->hidden) {
                    //We should only be adding one loop exit
                    if (normalChainingAdded == true)
                    {
                        if (cUnit->printMe == true)
                        {
                            ALOGD("JIT_INFO: Loop trace @ offset %04x more than one loop exit",
                                  cUnit->entryBlock->startOffset);
                        }
                        return false;
                    }
                    bb->taken->blockType = kChainingCellNormal;
                    normalChainingAdded = true;
                    bb->taken->hidden = false;
                    //We unhide some BB, so we need to clear its predecessor info
                    clearPredecessorVector (cUnit, bb->taken);
                }
                dvmCompilerSetBit(bb->taken->predecessors, bb->id);
            }
            if (bb->fallThrough) {
                /*
                 * exit block means we run into control-flow that we don't want
                 * to handle.
                 */
                if (bb->fallThrough == cUnit->exitBlock) {
                    if (cUnit->printMe == true)
                    {
                        ALOGD("JIT_INFO: Loop trace @ offset %04x fallthrough to exit block",
                              cUnit->entryBlock->startOffset);
                    }
                    return false;
                }
                if (bb->fallThrough->hidden) {
                    //We should only be adding one loop exit
                    if (normalChainingAdded == true) {
                        if (cUnit->printMe == true)
                        {
                            ALOGD("JIT_INFO: Loop trace @ offset %04x fallthrough to more than one loop exit",
                                  cUnit->entryBlock->startOffset);
                        }
                        return false;
                    }
                    bb->fallThrough->blockType = kChainingCellNormal;
                    normalChainingAdded = true;
                    bb->fallThrough->hidden = false;
                    //We unhide some BB, so we need to clear its predecessor info
                    clearPredecessorVector (cUnit, bb->fallThrough);
                }
                dvmCompilerSetBit(bb->fallThrough->predecessors, bb->id);
            }
            /* Loop blocks shouldn't contain any successor blocks (yet) */
            assert(bb->successorBlockList.blockListType == kNotUsed);
        }
    }

    return true;
}

#ifdef ARCH_IA32
/*
 * Main entry point to do loop, trace, method optimizations
 * Name is remaining the same as ARM for the moment...
 */
bool dvmCompilerLoopOpt(CompilationUnit *cUnit)
{
    dvmCompilerLaunchPassDriver (cUnit);

    return true;
}
#else

#ifdef DEBUG_LOOP_ON
/* Debugging routines */
static void dumpConstants(CompilationUnit *cUnit)
{
    int i;
    ALOGE("LOOP starting offset: %x", cUnit->entryBlock->startOffset);
    for (i = 0; i < cUnit->numSSARegs; i++) {
        if (dvmIsBitSet(cUnit->isConstantV, i)) {
            int subNReg = dvmConvertSSARegToDalvik(cUnit, i);
            ALOGE("CONST: s%d(v%d_%d) has %d", i,
                 DECODE_REG(subNReg), DECODE_SUB(subNReg),
                 (*cUnit->constantValues)[i]);
        }
    }
}

static void dumpIVList(CompilationUnit *cUnit)
{
    unsigned int i;
    GrowableList *ivList = cUnit->loopAnalysis->ivList;

    for (i = 0; i < ivList->numUsed; i++) {
        InductionVariableInfo *ivInfo =
            (InductionVariableInfo *) ivList->elemList[i];
        int iv = dvmConvertSSARegToDalvik(cUnit, ivInfo->ssaReg);
        /* Basic IV */
        if (ivInfo->ssaReg == ivInfo->basicSSAReg) {
            ALOGE("BIV %d: s%d(v%d_%d) + %d", i,
                 ivInfo->ssaReg,
                 DECODE_REG(iv), DECODE_SUB(iv),
                 ivInfo->loopIncrement);
        /* Dependent IV */
        } else {
            int biv = dvmConvertSSARegToDalvik(cUnit, ivInfo->basicSSAReg);

            ALOGE("DIV %d: s%d(v%d_%d) = %d * s%d(v%d_%d) + %d", i,
                 ivInfo->ssaReg,
                 DECODE_REG(iv), DECODE_SUB(iv),
                 ivInfo->m,
                 ivInfo->basicSSAReg,
                 DECODE_REG(biv), DECODE_SUB(biv),
                 ivInfo->constant);
        }
    }
}

static void dumpHoistedChecks(CompilationUnit *cUnit)
{
    LoopAnalysis *loopAnalysis = cUnit->loopAnalysis;
    unsigned int i;

    for (i = 0; i < loopAnalysis->arrayAccessInfo->numUsed; i++) {
        ArrayAccessInfo *arrayAccessInfo =
            GET_ELEM_N(loopAnalysis->arrayAccessInfo,
                       ArrayAccessInfo*, i);
        int arrayReg = DECODE_REG(
            dvmConvertSSARegToDalvik(cUnit, arrayAccessInfo->arrayReg));
        int idxReg = DECODE_REG(
            dvmConvertSSARegToDalvik(cUnit, arrayAccessInfo->ivReg));
        ALOGE("Array access %d", i);
        ALOGE("  arrayReg %d", arrayReg);
        ALOGE("  idxReg %d", idxReg);
        ALOGE("  endReg %d", loopAnalysis->endConditionReg);
        ALOGE("  maxC %d", arrayAccessInfo->maxC);
        ALOGE("  minC %d", arrayAccessInfo->minC);
        ALOGE("  opcode %d", loopAnalysis->loopBranchOpcode);
    }
}
#endif

/*
 * Main entry point to do loop optimization.
 * Return false if sanity checks for loop formation/optimization failed.
 */
bool dvmCompilerLoopOpt(CompilationUnit *cUnit)
{
    LoopAnalysis *loopAnalysis =
        (LoopAnalysis *)dvmCompilerNew(sizeof(LoopAnalysis), true);
    cUnit->loopAnalysis = loopAnalysis;

    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
                                          dvmCompilerDoConstantPropagation,
                                          kAllNodes,
                                          false /* isIterative */);
    DEBUG_LOOP(dumpConstants(cUnit);)

    /* Find induction variables - basic and dependent */
    loopAnalysis->ivList =
        (GrowableList *)dvmCompilerNew(sizeof(GrowableList), true);
    dvmInitGrowableList(loopAnalysis->ivList, 4);
    loopAnalysis->isIndVarV = dvmCompilerAllocBitVector(cUnit->numSSARegs, false);
    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
                                          dvmCompilerFindInductionVariables,
                                          kAllNodes,
                                          false /* isIterative */);
    DEBUG_LOOP(dumpIVList(cUnit);)

    /* Only optimize array accesses for simple counted loop for now */
    if (!isSimpleCountedLoop(cUnit))
        return false;

    loopAnalysis->arrayAccessInfo =
        (GrowableList *)dvmCompilerNew(sizeof(GrowableList), true);
    dvmInitGrowableList(loopAnalysis->arrayAccessInfo, 4);
    loopAnalysis->bodyIsClean = doLoopBodyCodeMotion(cUnit);
    DEBUG_LOOP(dumpHoistedChecks(cUnit);)

    /*
     * Convert the array access information into extended MIR code in the loop
     * header.
     */
    genHoistedChecks(cUnit);
    return true;
}

/*
 * Select the target block of the backward branch.
 */
bool dvmCompilerInsertBackwardChaining(CompilationUnit *cUnit)
{
    /*
     * If we are not in self-verification or profiling mode, the backward
     * branch can go to the entryBlock->fallThrough directly. Suspend polling
     * code will be generated along the backward branch to honor the suspend
     * requests.
     */
#ifndef ARCH_IA32
#if !defined(WITH_SELF_VERIFICATION)
    if (gDvmJit.profileMode != kTraceProfilingContinuous &&
        gDvmJit.profileMode != kTraceProfilingPeriodicOn) {
        return false;
    }
#endif
#endif

    /*
     * In self-verification or profiling mode, the backward branch is altered
     * to go to the backward chaining cell. Without using the backward chaining
     * cell we won't be able to do check-pointing on the target PC, or count the
     * number of iterations accurately.
     */
    BasicBlock *firstBB = cUnit->entryBlock->fallThrough;
    BasicBlock *backBranchBB = findPredecessorBlock(cUnit, firstBB);

    //Backward branching can fail if findPredecessorBlock returns 0, if it does report the failure
    if (backBranchBB == NULL)
    {
        return false;
    }

    if (backBranchBB->taken == firstBB) {
        backBranchBB->taken = cUnit->backChainBlock;
    } else {
        //Paranoid: if fallThrough is not firstBB, we have an issue: neither taken or fallthrough went to firstBB...
        if (backBranchBB->fallThrough != firstBB)
        {
            //Report it as a failure
            return false;
        }
        backBranchBB->fallThrough = cUnit->backChainBlock;
    }
    cUnit->backChainBlock->startOffset = firstBB->startOffset;

    //Report success
    return true;
}
#endif

/**
 * @brief Recursive function to find the minimum offset of a loop: it is located in the BasicBlock with the smallest startOffset
 * @param cUnit the CompilationUnit
 * @param bb the current BasicBlock
 * @return the minimum offset BasicBlock
 */
static BasicBlock *findMinimumHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    //If null, not dalvik byte code, or visited, return 0
    if (bb == 0 || (bb->blockType != kDalvikByteCode) || (bb->visited == true))
    {
        return 0;
    }

    //Mark it
    bb->visited = true;

    //Paranoid
    if (bb->predecessors == 0)
    {
        return 0;
    }

    //Suppose the minimum is bb
    BasicBlock *min = bb;

    //Go through the predecessors
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    while (true) {
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

        if (blockIdx == -1)
        {
            break;
        }

        BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, blockIdx);

        BasicBlock *curMin = findMinimumHelper (cUnit, predBB);

        if (curMin != 0 && curMin->startOffset < min->startOffset)
        {
            min = curMin;
        }
    }

    //Return minium
    return min;
}

/**
 * @brief Function to the minimum offset of a loop
 * @param cUnit the CompilationUnit
 * @return the minimum offset BasicBlock of cUnit
 */
static BasicBlock *findMinimum (CompilationUnit *cUnit)
{
    //Reset flags
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, dvmCompilerClearVisitedFlag, kAllNodes, false);

    //Call recursive function
    return findMinimumHelper (cUnit, cUnit->entryBlock->fallThrough);
}

/**
 * @brief Mark the BasicBlock in the loop cache
 *  The loop cache is used to know if an offset is a loop head or not. It helps reduce compilation time.
 *  The loop cache contains all the BasicBlocks that are NOT loop heads
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns false, the function does not change the BasicBlock
 */
static bool markBasicBlocksInLoopCache (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Only care about dalvik byte code
    if (bb->blockType == kDalvikByteCode)
    {
        gDvmJit.knownNonLoopHeaderCache[cUnit->method->insns + bb->startOffset];
    }
    //We did not change anything to bb
    return false;
}

/**
 * @brief Mark off any BasicBlock, which is not a loop header
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return always return false, we don't change the BasicBlock
 */
static bool markOffNonHeadersHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    BitVectorIterator bvIterator;

    //Paranoid
    assert (bb->predecessors != 0);

    //Get predecessors
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);

    //Only mark off BasicBlocks that are dalvik code
    if (bb->blockType != kDalvikByteCode)
    {
        return false;
    }

    //Did we find a BasicBlock being a backward branch
    while (true) {
        //Get the next iterator
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

        //If it is finished, exit
        if (blockIdx == -1)
        {
            break;
        }

        BasicBlock *predBB = (BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, blockIdx));

        //Paranoid
        if (predBB == 0)
        {
            break;
        }

        //If no dominator information, skip it
        if (predBB->dominators == 0)
        {
            continue;
        }

        //If the predecessor is dominated by this one, it is a backward branch
        if (dvmIsBitSet (predBB->dominators, bb->id) == true)
        {
            unsigned int entryOffset = cUnit->entryBlock->startOffset;

            //Now here are some assumptions:
            // If bb is the startOffset of cUnit->entryBlock, it is the original head
            if (entryOffset == bb->startOffset)
            {
                predBB->loopTraversalType.walkBackward = true;
                predBB->loopTraversalType.relativeTo = bb;
            }
            else
            {
                //Now the if handled top loop cases where the head of the loop is
                //actually the head of the trace. Sometimes it happens that the branch
                //into the loop is the head. Check this here

                //First do we have only one branch towards it:
                if (bb->taken != 0 && bb->fallThrough == 0 && bb->taken->startOffset == entryOffset)
                {
                    bb->loopTraversalType.walkForward = true;
                    bb->loopTraversalType.relativeTo = bb->taken;
                }
                else
                {
                    //Same but the other side
                    if (bb->fallThrough != 0 && bb->taken == 0 && bb->fallThrough->startOffset == entryOffset)
                    {
                        bb->loopTraversalType.walkForward = true;
                        bb->loopTraversalType.relativeTo = bb->fallThrough;
                    }
                    else
                    {
                        //Otherwise, we have two children and that means this is exiting the loop
                        bb->loopTraversalType.walkBackward = true;
                        bb->loopTraversalType.relativeTo = predBB;
                    }
                }
            }

            //Now mark it as a potential loop head and its children
            gDvmJit.knownNonLoopHeaderCache.erase (cUnit->method->insns + bb->startOffset);

            //Now we mark both children because we don't know which one is towards a loop
            //A subsequent call will handle it
            if (bb->taken != 0)
            {
                if (dvmIsBitSet (predBB->dominators, bb->taken->id) == true)
                {
                    gDvmJit.knownNonLoopHeaderCache.erase (cUnit->method->insns + bb->taken->startOffset);
                }
            }

            if (bb->fallThrough != 0)
            {
                if (dvmIsBitSet (predBB->dominators, bb->fallThrough->id) == true)
                {
                    gDvmJit.knownNonLoopHeaderCache.erase (cUnit->method->insns + bb->fallThrough->startOffset);
                }
            }
        }
    }

    //We did not change the BasicBlock
    return false;
}


/**
 * @brief Clear predecessor information
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns whether we changed something in the BasicBlock or not
 */
static bool clearPredecessors (CompilationUnit *cUnit, BasicBlock *bb)
{
    //We only need to set it if there is a bit set,
    //normally we wouldn't care about this test but the dispatcher might care
    if (dvmCountSetBits (bb->predecessors) != 0)
    {
        dvmClearAllBits (bb->predecessors);
        return true;
    }
    return false;
}

/**
 * @brief Calculate Predecessor Information Helper
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns false, the BasicBlock is not changed
 */
static bool calculatePredecessorsHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    //We only care about non hidden blocks
    if (bb->hidden == true)
    {
        return false;
    }

    //Create iterator for visiting children
    ChildBlockIterator childIter (bb);

    //Now iterate through the children to set the predecessor bits
    for (BasicBlock **childPtr = childIter.getNextChildPtr (); childPtr != 0; childPtr = childIter.getNextChildPtr ())
    {
        BasicBlock *child = *childPtr;

        assert (child != 0);

        dvmCompilerSetBit (child->predecessors, bb->id);
    }

    //We did change something but not our own basic block
    return false;
}

/**
 * @brief Calculate Predecessor Information
 * @param cUnit the CompilationUnit
 */
void dvmCompilerCalculatePredecessors (CompilationUnit *cUnit)
{
    //First job is to clear the predecessors
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, clearPredecessors, kAllNodes, false);

    //Second part is to calculate them again
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, calculatePredecessorsHelper, kAllNodes, false);
}

/**
 * @brief Mark off BasicBlocks from the loop cache
 * @param cUnit the CompilationUnit
 */
void dvmCompilerLoopMarkOffNonHeaderBlocks (CompilationUnit *cUnit)
{
    //Recalculate the predecessors with this new formation
    dvmCompilerCalculatePredecessors (cUnit);

    //Find the minimum offset
    BasicBlock *minimum = findMinimum (cUnit);

    //Now entry should temporarily go to the minimum
    BasicBlock *tmpEntry = cUnit->entryBlock->fallThrough;
    cUnit->entryBlock->fallThrough = minimum;

    //Recalculate the predecessors with this new formation
    dvmCompilerCalculatePredecessors (cUnit);

    //Ok, now we can calculate dominators
    dvmCompilerBuildDomination (cUnit);

    //Clear the temporary bits
    dvmClearAllBits (cUnit->tempBlockV);

    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
            markBasicBlocksInLoopCache,
            kAllNodes,
            false /* isIterative */);

    //Now we can go through the BasicBlocks and mark off those that are not loops
    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
            markOffNonHeadersHelper,
            kAllNodes,
            false /* isIterative */);

    //Put it back as it was, and recalculate the predecessors
    cUnit->entryBlock->fallThrough = tmpEntry;
    dvmCompilerCalculatePredecessors (cUnit);

    //Domination is done later so no need here
}

#ifdef ARCH_IA32
/**
 * @brief Looks through backward's predecessors and inserts a new block in
 * between. It also ensures that new block is the taken branch and flips
 * condition in bytecode if needed.
 * @details Creates a new block and copies relevant information from backward.
 * @param cUnit the Compilation Unit
 * @param backward the backward branch chaining cell
 */
static void insertBlockBeforeBackwardHelper (CompilationUnit *cUnit,
        BasicBlock *backward)
{
    //Checking preconditions
    assert(backward != 0);

    //Only insert prebackward if backward branch CC is involved
    if (backward->blockType != kChainingCellBackwardBranch)
    {
        return;
    }

    BitVector *predecessors = backward->predecessors;

    //Paranoid
    if (predecessors == 0)
    {
        return;
    }

    //Ok, there is currently no way a backward branch can have more than one predecessor
    //Something went terribly wrong if it did, so get out
    //Note that if we remove this check we need to revisit to code below, cosidering loop
    //over predecessors.
    if (dvmCountSetBits (predecessors) != 1)
    {
        PASS_LOG (ALOGD, cUnit, "JIT_INFO: Backward branch has more than one predecessor");

        cUnit->quitLoopMode = true;
        return;
    }

    // We have only one predecessor so take it
    int blockIdx = dvmHighestBitSet (predecessors);

    //Get the predecessor block
    BasicBlock *predecessor =
            reinterpret_cast<BasicBlock *> (dvmGrowableListGetElement (
                    &cUnit->blockList, blockIdx));

    //Paranoid
    assert (predecessor != 0);

    //Create a preBackward block
    BasicBlock *preBackward = dvmCompilerNewBBinCunit (cUnit,
            kPreBackwardBlock);

    //Paranoid
    assert(preBackward != 0);

    //Now we copy the relevant parts
    preBackward->startOffset = backward->startOffset;
    preBackward->firstMIRInsn = backward->firstMIRInsn;
    preBackward->lastMIRInsn = backward->lastMIRInsn;
    preBackward->containingMethod = backward->containingMethod;

    //We also need to make a copy of the write back requests
    preBackward->requestWriteBack = dvmCompilerAllocBitVector (1, true);
    dvmCopyBitVector (preBackward->requestWriteBack,
            backward->requestWriteBack);

    //We want the new block to be the taken branch.
    //So if backward used to be the fallthrough, make it the taken.
    if(predecessor->fallThrough == backward)
    {
        MIR *ifMir = predecessor->lastMIRInsn;

        //It is unexpected if we have a null MIR, so bail out
        if (ifMir == 0)
        {
            cUnit->quitLoopMode = true;
            return;
        }

        //Paranoid, we should have an if at the end
        assert(ifMir != 0 && ifMir->dalvikInsn.opcode >= OP_IF_EQ
                && ifMir->dalvikInsn.opcode <= OP_IF_LEZ);

        Opcode negated;

        bool canNegate = negateOpcode (ifMir->dalvikInsn.opcode, negated);

        //If we can negate the bytecode condition, then we can swap
        //the children
        if (canNegate == true)
        {
            //Update opcode
            ifMir->dalvikInsn.opcode = negated;

            //Set the fallthrough to be the old taken
            dvmCompilerReplaceChildBasicBlock (predecessor->taken, predecessor, kChildTypeFallthrough);

            //Make the backward be the new taken
            dvmCompilerReplaceChildBasicBlock (backward, predecessor, kChildTypeTaken);
        }
    }

    //Insert the preBackward block between predecessor and backward CC
    bool res = dvmCompilerInsertBasicBlockBetween (preBackward, predecessor,
            backward);

    //If we failed inserting, that's not good and we bail out
    if (res == false)
    {
        cUnit->quitLoopMode = true;
        return;
    }

    //Clear fields from backward
    backward->firstMIRInsn = 0;
    backward->lastMIRInsn = 0;

    //Update parent of the MIRs
    for (MIR *mir = preBackward->firstMIRInsn; mir != 0; mir = mir->next)
    {
        mir->bb = preBackward;
    }
}

/**
 * @brief Finds all of the backward branch chaining cells and then inserts
 * a block before each of them.
 * @param cUnit the Compilation Unit
 * @param info the information of Loop we are looking at
 * @param data required by interface (not used)
 * @return true to continue iteration over loops
 */
static bool insertBlockBeforeBackward (CompilationUnit *cUnit,
        LoopInformation *info, void *data = 0)
{
    //We want to look through all of the backward chaining cells
    const BitVector *backwards = info->getBackwardBranches ();

    //Const cast due to incompatibility here
    BitVector *tmp = const_cast<BitVector *> (backwards);

    //Initialize iterator
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (tmp, &bvIterator);

    while (true)
    {
        //Get the block index
        int blockIdx = dvmBitVectorIteratorNext (&bvIterator);

        //Break if we are done
        if (blockIdx == -1)
        {
            break;
        }

        //Get the backward block
        BasicBlock *backward =
                reinterpret_cast<BasicBlock *> (dvmGrowableListGetElement (
                        &cUnit->blockList, blockIdx));

        //Paranoid
        if (backward == 0)
        {
            continue;
        }

        insertBlockBeforeBackwardHelper (cUnit, backward);
    }
    return true;
}

/**
 * @brief Add a block before the preheader of type kFromInterpreter
 * @param cUnit the Compilation Unit
 * @param info the information of Loop we are looking at
 * @param data required by interface (not used)
 * @return true to continue iteration over loops
 */
static bool insertBlockFromInterpreter (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    //Get the preheader
    BasicBlock *preHeader = info->getPreHeader ();

    //Get one of the backward blocks since we want to get offset from it
    int backwardIdx = dvmHighestBitSet (info->getBackwardBranches ());
    BasicBlock *backward = reinterpret_cast<BasicBlock *> (dvmGrowableListGetElement (&cUnit->blockList, backwardIdx));
    assert (backward != 0);

    if (backward == 0)
    {
        PASS_LOG (ALOGD, cUnit, "Insert_LoopHelper_Blocks: FromInterpreter cannot be properly inserting "
                "without offset from backward CC.");
        cUnit->quitLoopMode = true;
        return false;
    }

    if (preHeader != 0)
    {
        //Also add a from interpreter node
        BasicBlock *fromInterpreter = dvmCompilerNewBBinCunit (cUnit, kFromInterpreter);

        //Set the correct offset
        fromInterpreter->startOffset = backward->startOffset;

        //Link fromInterpreter to preHeader
        dvmCompilerReplaceChildBasicBlock (preHeader, fromInterpreter, kChildTypeFallthrough);
    }

    //Unused parameter
    (void) data;

    //Continue iterating
    return true;
}

/**
 * @brief Inserts a basic block before Backward Chaining Cell and one before the preheader.
 * @details The newly inserted basic blocks takes the write back requests and
 * MIRs from chaining cell in order to help backend which cannot handle
 * Backward Chaining Cell like a bytecode block. It also ensures that the
 * newly inserted block is the taken branch, so if the backward was fallthrough
 * it flips the condition.
 * @param cUnit the CompilationUnit
 * @param currentPass the Pass
 */
void dvmCompilerInsertLoopHelperBlocks (CompilationUnit *cUnit, Pass *currentPass)
{
    //Now let's go through the loop information
    LoopInformation *info = cUnit->loopInformation;

    //If info is 0, there is nothing to do
    if (info == 0)
    {
        return;
    }

    //Actually do the work
    info->iterate (cUnit, insertBlockBeforeBackward);

    //Now do it for the from interpreter
    info->iterate (cUnit, insertBlockFromInterpreter);

    //Unused argument
    (void) currentPass;
}
#endif
