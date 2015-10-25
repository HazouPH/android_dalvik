/*
 * Copyright (C); 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");;
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

#include "BasicBlockPCG.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "LowerJump.h"
#include "LowerMemory.h"
#include "Utility.h"
#include "UtilityPCG.h"

void dvmCompilerPcgTranslateDirectJumpToBlock (BasicBlockPCG *bb)
{
    CGCreateNewInst ("jmp", "b", bb->cgLabel);
}

/**
 * @brief Create a JSR
 * @param symbol the CGsymbol to jump to
 * @param parms a pointer to an array of CGInst parameters, ending in a CGInstInvalid (0 is fine)
 * @param reg a CGInst with a register containing an address to jump to (CGInstInvalid is fine)
 * @return the JSR CGInst
 */
static CGInst createJsr (CGSymbol symbol, CGInst *parms, CGInst reg)
{
    if (parms != 0)
    {
        if (reg != CGInstInvalid)
        {
            return CGCreateNewInst ("jsr", "rl", reg, parms);
        }
        else
        {
            return CGCreateNewInst ("jsr", "nl", symbol, parms);
        }
    }
    else
    {
        if (reg != CGInstInvalid)
        {
            return CGCreateNewInst ("jsr", "r", reg);
        }
        else
        {
            return CGCreateNewInst ("jsr", "n", symbol);
        }
    }
}


/**
 * @details Create a JSR and add an spaddi
 */
CGInst dvmCompilerPcgCreateJsr (CompilationUnitPCG *cUnit, CGSymbol symbol, CGInst *parms, CGInst reg)
{
    if (symbol != CGSymbolInvalid && reg != CGInstInvalid)
    {
        cUnit->errorHandler->setError (kJitErrorPcgJsrCreation);
        return CGInstInvalid;
    }

    CGInst spadd = CGCreateNewInst ("spaddi", "ri", CGGetStackPointerDef (), 0);
    CGSetRreg (spadd, "esp");

    return createJsr (symbol, parms, reg);
}

#if defined(WITH_JIT_TUNING)
/**
 * @details Create a JSR, add an spaddi, and store the kSwitchOverflow value on the stack
 */
CGInst dvmCompilerPcgCreateJsrWithKSwitchOverflow (CompilationUnitPCG *cUnit, CGSymbol symbol, CGInst *parms, CGInst reg)
{
    if (symbol != CGSymbolInvalid && reg != CGInstInvalid)
    {
        cUnit->errorHandler->setError (kJitErrorPcgJsrCreation);
        return CGInstInvalid;
    }

    CGInst spadd = CGCreateNewInst ("spaddi", "ri", CGGetStackPointerDef (), 0);

    /* Fall back to interpreter after resolving address of switch target.
     * Indicate a kSwitchOverflow. Note: This is not an "overflow". But it helps
     * count the times we return from a Switch
     */
    CGInst switchOverflowFlag = CGCreateNewInst ("mov", "i", kSwitchOverflow);
    CGInst store = dvmCompilerPcgCreateSimpleStore (CGGetStackPointerDef (),
        0, switchOverflowFlag);

    CGInst stackPtrInEsp = CGCreateNewInst ("mov", "r", CGGetStackPointerDef ());
    CGSetRreg (stackPtrInEsp, "esp");

    return createJsr (symbol, parms, reg);
}
#endif

void dvmCompilerPcgTranslateConditionalJump (BasicBlockPCG *bb, CGInst a, const char *cond, CGInst b)
{
    CGLabel targetLabel = bb->takenLabel;
    //TODO: check values with tuning
    int branchProb = 50;

    BasicBlockPCG *taken = (BasicBlockPCG *) (bb->taken);
    BasicBlockPCG *fallThrough = (BasicBlockPCG *) (bb->fallThrough);

    //TODO: look if these values should get changed
    if (taken->blockType == kChainingCellBackwardBranch)
    {
        branchProb = 90;
    }
    else
    {
        if (fallThrough->blockType == kChainingCellBackwardBranch)
        {
            branchProb = 10;
        }
    }

    CGCreateNewInst ("cjcc", "rcrbp", a, cond, b, targetLabel, branchProb);
}

void dvmCompilerPcgTranslateIf (CompilationUnitPCG *cUnit, MIR *mir, const char *cond)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);
    dvmCompilerPcgTranslateConditionalJump (bb, A, cond, B);
}

void dvmCompilerPcgTranslateIfZero (CompilationUnitPCG *cUnit, MIR *mir, const char *cond)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    // HACK! We currently set this flag when we combine an FP compare with a
    // subsequent ifz.  We need a real flag or some other better mechanism.
    if (mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK)
    {
        return;
    }

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    dvmCompilerPcgTranslateConditionalJump (bb, A, cond, zero);
}

void dvmCompilerPcgTranslateIfFp (CompilationUnitPCG *cUnit, MIR *mir, uint32_t opSize, int nanVal)
{
    const char *opcode;
    const char *cond1, *cond2;

    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    if (opSize == 4)
    {
        opcode = "movss1";
    }
    else
    {
        assert (opSize == 8);
        opcode = "movsd1";
    }

    switch (nanVal)
    {
        case 1:
            cond1 = "flt";
            cond2 = "fle";
            break;
        case -1:
            cond1 = "fnge";
            cond2 = "fngt";
            break;
        default:
            // Currently, this case is never hit.  I am not sure whether there is
            // an opcode that gives a 0 result for an unordered compare.
            assert (nanVal == 0);
            cond1 = "flt";
            cond2 = "fequ";
            break;
    }

    // Try to find the branch target.  If the next MIR is an integer compare
    // and jump, we can short circuit it.
    MIR *nextMir = mir->next;
    CGLabel negOneLabel = CGCreateLabel ();
    CGLabel endLabel;
    CGLabel zeroAndOneTarget;
    bool shortCircuit = false;

    if (nextMir && nextMir->dalvikInsn.opcode == OP_IF_GEZ)
    {
        zeroAndOneTarget = bb->takenLabel;
        shortCircuit = true;
        // HACK!  We need a real flag for this.
        nextMir->OptimizationFlags |= MIR_IGNORE_NULL_CHECK;
    }
    else
    {
        endLabel = CGCreateLabel ();
        zeroAndOneTarget = endLabel;
    }

    int cn = (opSize == 8) ? 2 : 1;
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], opcode, opSize);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[cn], opcode, opSize);
    CGTemp resultTemp = cUnit->getCurrentTemporaryVR (true);

    CGCreateNewInst ("cjcc", "rcrbp", B, cond1, C, negOneLabel, 95);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGInst one = CGCreateNewInst ("mov", "i", 1);
    CGInst slcc = CGCreateNewInst ("islcc", "rcrrr", B, cond2, C,
            zero, one);
    CGAddTempDef (resultTemp, slcc);

    if (shortCircuit == true)
    {
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));
    }
    CGCreateNewInst ("jmp", "b", zeroAndOneTarget);

    CGBindLabel (negOneLabel);
    CGInst negOne = CGCreateNewInst ("mov", "i", -1);
    CGAddTempDef (resultTemp, negOne);

    if (shortCircuit == true)
    {
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));

        // At this point, we need a control flow transfer to the fallThrough
        // block.  That will get added automatically in dvmCompilerPcgTranslateBB.
    }
    else
    {
        CGBindLabel (endLabel);
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));
    }
}

void dvmCompilerPcgTranslateGoto (BasicBlockPCG *bb)
{
    CGCreateNewInst ("jmp", "b", bb->takenLabel);
}

void dvmCompilerPcgDoWritebacksOnEdge (CompilationUnitPCG *cUnit, BasicBlockPCG *from, BasicBlockPCG *to)
{
    BitVector *tempBV = cUnit->getTemporaryBitVector ();
    dvmCopyBitVector (tempBV, from->dirtyOuts);

    // We have to write back any register that is live out of "from" but not
    // live into "to".  If "to" is a pre backward block, the edge is really to
    // the loop header.
    if (to->blockType == kPreBackwardBlock)
    {
        LoopInformation *loopInfo = cUnit->loopInformation;
        to = (BasicBlockPCG *) loopInfo->getEntryBlock ();

        // We only need to write back phi nodes inside the loop
        dvmCompilerPcgRemoveNonPhiNodes (cUnit, tempBV, to);
    }

    dvmSubtractBitVectors (tempBV, tempBV, to->dirtyIns);
    dvmCompilerPcgGenerateWritebacks (cUnit, tempBV);
    cUnit->freeTemporaryBitVector (tempBV);
}

void dvmCompilerPcgGenerateWritebacks (CompilationUnitPCG *cUnit, BitVector *bv)
{
    BitVectorIterator it;
    int ssaNum;

    BitVector *tempBV = cUnit->getTemporaryBitVector ();

    dvmCopyBitVector (tempBV, bv);

    dvmBitVectorIteratorInit (bv, &it);
    while ( (ssaNum = dvmBitVectorIteratorNext (&it)) != -1)
    {
        //TODO: Change while to not have an assign at the same time

        //Get information
        SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

        if (dvmIsBitSet (tempBV, ssaNum) == true && info.deferWriteback == true)
        {
            int storeMask = 1;
            int pairSSANum = info.pairSSANum;

            // For 8-byte stores, we have to distinguish between cases where
            // we need to write back all 8 bytes vs. just the upper or lower
            // halves.  This may be improved somewhat.  It is inefficient to
            // store JUST the upper half of a DPVXreg64, because we need a
            // shufps to do it.  In many cases, we could write the full
            // 8-bytes as long as we order the writebacks properly.  For
            // example, if we have an INTreg writeback to v8, and an upper
            // 4-bytes of DPVXreg64 writeback to v9, we can do a DPVXreg64
            // store to v8-v9 followed by an INTreg store to v8.
            if (pairSSANum != 0)
            {
                CGTemp temp = cUnit->getCGTempForSSANum(pairSSANum);

                // We model the 8-byte instructions in PCG IL as defining the
                // CGTemp of the low half, so we need to convert ssaNum here
                // if it defines the upper half of an 8-byte value.
                if (dvmCompilerPcgIsHighDtype(info.dtype) == true)
                {
                    ssaNum = temp;
                    storeMask = 2;
                }

                // Now see if we can write back an entire 8-byte value at once.
                if (dvmIsBitSet (tempBV, temp) == true)
                {
                    storeMask = 3;
                    dvmClearBit (tempBV, temp);
                }
            }

            //Purely local scratch registers do not have a home location.
            //So therefore we skip the actual store at exit because the way ME
            //generated code, these are not live out
            if (dvmCompilerIsPureLocalScratch (cUnit, ssaNum, true) == false)
            {
                dvmCompilerPcgStoreVirtualReg (cUnit, ssaNum, storeMask);
            }
        }
    }

    cUnit->freeTemporaryBitVector (tempBV);
}
