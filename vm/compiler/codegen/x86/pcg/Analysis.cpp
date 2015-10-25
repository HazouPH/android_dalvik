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

#include "Analysis.h"
#include "CompilationErrorPCG.h"
#include "BasicBlockPCG.h"
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "Utility.h"
#include "UtilityPCG.h"

/**
 * @brief Merge pcgDtype variables
 * @param dtype1 the first pcgDtype
 * @param dtype2 the second pcgDtype
 * @return the merged pcgDtype
 */
static pcgDtype pcgMergeDtypes (pcgDtype dtype1, pcgDtype dtype2)
{
    if (dtype1 == dtype2)
    {
        return dtype1;
    }
    if (dtype1 == Any)
    {
        return dtype2;
    }
    if (dtype2 == Any)
    {
        return dtype1;
    }
    if (dtype1 == Any4 && (dtype2 == VXreg32 || dtype2 == INTreg))
    {
        return dtype2;
    }
    if (dtype2 == Any4 && (dtype1 == VXreg32 || dtype1 == INTreg))
    {
        return dtype1;
    }
    if (dtype1 == Any8 && (dtype2 == DPVXreg64 || dtype2 == LLreg))
    {
        return dtype2;
    }
    if (dtype2 == Any8 && (dtype1 == DPVXreg64 || dtype1 == LLreg))
    {
        return dtype1;
    }
    if (dtype1 == Any8Hi && (dtype2 == DPVXreg64Hi || dtype2 == LLregHi))
    {
        return dtype2;
    }
    if (dtype2 == Any8Hi && (dtype1 == DPVXreg64Hi || dtype1 == LLregHi))
    {
        return dtype1;
    }

    return NOreg;
}

/**
 * @brief Recursively disable registerization for ssaNum and any dependent ssa numbers
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the ssa we care about
 */
static void pcgDisableRegisterizationForSSANum (CompilationUnitPCG *cUnit, int ssaNum)
{
    SSANumInfo &pcgSSAInfo = cUnit->getRootSSANumInformation (ssaNum);

    // Set the dtype to NOreg to make sure we don't mistakenly read it.  If we
    // are disabling registerization, a likely reason is that we could not
    // determine ssaNum's dtype.
    pcgSSAInfo.dtype = NOreg;

    // Only process ssaNum if registerization hasn't already been disabled.
    // That avoids infinite recursion.
    if (pcgSSAInfo.registerize) {
        int pairSSANum = pcgSSAInfo.pairSSANum;
        pcgSSAInfo.registerize = false;

        // Disable registerization for its pair ssa number, if applicable
        if (pairSSANum != 0) {
            pcgDisableRegisterizationForSSANum(cUnit, pairSSANum);
        }
    }
}

/**
 * @brief Handle the define for the SSA information
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the ssa we care about
 * @param dtype the type of the SSA register
 */
static void pcgDefRef (CompilationUnitPCG *cUnit, int ssaNum, pcgDtype dtype)
{
    bool newElement = false;

    //Get the entry
    SSANumInfo &pcgSSAInfo = cUnit->getSSANumInformation (ssaNum, newElement);

    if (newElement == true)
    {
        // Then we have to fill it up a bit
        // TODO: These initializations are redundant with the memset that
        //       happens in cUnit->getSSANumInformation.  This should be
        //       refactored, possibly by making SSANumInfo into a class with
        //       a constructor.
        pcgSSAInfo.dtype = dtype;
        pcgSSAInfo.parentSSANum = ssaNum;
        pcgSSAInfo.pairSSANum = 0;
        pcgSSAInfo.numUses = 0;
        pcgSSAInfo.mir = 0;
        pcgSSAInfo.registerize = true;
        pcgSSAInfo.needsNullCheck = false;
        pcgSSAInfo.checkedForNull = false;
        pcgSSAInfo.deferWriteback = true;
    }
    else
    {
        // Get the information for the root of the SSANum tree.
        SSANumInfo &rootSSAInfo = cUnit->getRootSSANumInformation (ssaNum);

        // Merge the types between child and this parent
        rootSSAInfo.dtype = pcgMergeDtypes (dtype, rootSSAInfo.dtype);

        if (rootSSAInfo.dtype == NOreg)
        {
            pcgDisableRegisterizationForSSANum(cUnit, rootSSAInfo.parentSSANum);
        }
    }
}

/**
 * @brief Handle the reference for the SSA information
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the ssa we care about
 * @param dtype the type of the SSA register
 * @param needsNullCheck do we need a null check (default: false)
 */
static void pcgRef (CompilationUnitPCG *cUnit, int ssaNum, pcgDtype dtype, bool needsNullCheck = false)
{
    //For every use we must ensure that we a define for the specific type we care about
    pcgDefRef (cUnit, ssaNum, dtype);

    //Get information about it
    SSANumInfo &info = cUnit->getSSANumInformation (ssaNum);

    //Augment uses
    info.numUses++;

    //Add null check information if we care about speculative
    if (cUnit->checkOptimizationMask (OptimizationMaskSpeculativeNullChecks) == true)
    {
        info.needsNullCheck = needsNullCheck;
    }
}

/**
 * @brief Merge SSA numbers
 * @details This routine gets called when two SSA numbers must be merged into
 *  the same CGTemp.  That happens when we see a PHI, e.g.
 *      kMirOpPhi v0_1 = (v0_0, v0_2)
 *  In this case, v0_0, v0_1, and v0_2 must all use the same CGTemp, must all
 *  share the same dtype, must have the same pair, if applicable, and must all
 *  be registerized or not registerized together.  To implement that, v0_1 is
 *  used as the "parent" ssa number.  All the information associated with the
 *  unified CGTemp are stored in v0_1's SSANumInfo record.  v0_0 and v0_2 refer
 *  to their parent, v0_1, via the parentSSANum field.  This routine creates
 *  the child-parent tree structure and merges the information from the child
 *  ssa number into the parent.  If any conflicts are detected, registerization
 *  is disabled for the parent.
 * @param cUnit the CompilationUnitPCG
 * @param childNum the child SSA Number
 * @param parentNum the parent SSA Number
 */
static void pcgMergeSSANums (CompilationUnitPCG *cUnit, int childNum, int parentNum)
{
    //Get local child and parent information
    SSANumInfo &child = cUnit->getRootSSANumInformation (childNum);
    SSANumInfo &parent = cUnit->getRootSSANumInformation (parentNum);

    // Compute the combined dtype.  It will be NOreg for inconsistent types.
    parent.dtype = pcgMergeDtypes (parent.dtype, child.dtype);

    // If we detected a type inconsistency or if one of the SSA numbers already
    // has registerization disabled, disable registerization now.  There is no
    // need to physically do the merge in this case.
    if (parent.dtype == NOreg || parent.registerize == false ||
        child.registerize == false)
    {
        pcgDisableRegisterizationForSSANum(cUnit, parent.parentSSANum);
        pcgDisableRegisterizationForSSANum(cUnit, child.parentSSANum);
        return;
    }

    // Physically merge them.
    child.parentSSANum = parent.parentSSANum;

    // Merge the child pair with the parent pair, if necessary.
    if (child.pairSSANum != 0)
    {
        if (parent.pairSSANum == 0)
        {
            parent.pairSSANum = child.pairSSANum;
        }
        else if (parent.pairSSANum != child.pairSSANum)
        {
            pcgMergeSSANums (cUnit, child.pairSSANum, parent.pairSSANum);
        }
    }
}

/**
 * @brief Handle the pairing of two SSA numbers.  An SSA number pair is used to associate the two halves of an 8-byte value.
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum1 the lower half of the 8-byte value
 * @param ssaNum2 the upper half of the 8-byte value
 */
static void pcgCreatePair (CompilationUnitPCG *cUnit, int ssaNum1, int ssaNum2)
{
    SSANumInfo &info1 = cUnit->getRootSSANumInformation (ssaNum1);
    SSANumInfo &info2 = cUnit->getRootSSANumInformation (ssaNum2);
    int pairSSANum1 = info1.pairSSANum;
    int pairSSANum2 = info2.pairSSANum;

    // In order to create a valid pair, both members of the pair must be
    // registerized.
    if (info1.registerize == false || info2.registerize == false) {
        pcgDisableRegisterizationForSSANum(cUnit, info1.parentSSANum);
        pcgDisableRegisterizationForSSANum(cUnit, info2.parentSSANum);
        return;
    }

    // If either SSANum has an existing pair, it needs to be merged with the
    // new one, because each SSANum can only have one pair.
    if (pairSSANum1 != 0) {
        SSANumInfo &currPairInfo = cUnit->getRootSSANumInformation (pairSSANum1);
        int currPairSSANum = currPairInfo.parentSSANum;

        if (currPairSSANum != info2.parentSSANum) {
            pcgMergeSSANums (cUnit, info2.parentSSANum, currPairSSANum);
        }
    }
    if (pairSSANum2 != 0) {
        SSANumInfo &currPairInfo = cUnit->getRootSSANumInformation (pairSSANum2);
        int currPairSSANum = currPairInfo.parentSSANum;

        if (currPairSSANum != info1.parentSSANum) {
            pcgMergeSSANums (cUnit, info1.parentSSANum, currPairSSANum);
        }
    }

    // Physically create the pair.
    info1.pairSSANum = info2.parentSSANum;
    info2.pairSSANum = info1.parentSSANum;
}

/**
 * @brief Analyze the execute inline opcode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @return whether we succeeded or not
 */
static bool pcgAnalyzeExecuteInline (CompilationUnitPCG *cUnit, MIR *mir)
{
    //vB holds the index for intrinsic
    NativeInlineOps op = (NativeInlineOps) (mir->dalvikInsn.vB);

    //Get SSARepresentation
    SSARepresentation *ssa = mir->ssaRep;

    //Paranoid
    assert (ssa != 0);

    // For OP_EXECUTE_INLINE[_RANGE], the UD information depends on which
    // operation is being executed.
    switch (op)
    {
        case INLINE_STRING_LENGTH:
        case INLINE_STRING_IS_EMPTY:
        case INLINE_MATH_ABS_INT:
        case INLINE_STRICT_MATH_ABS_INT:
            //Paranoid
            assert (ssa->numUses > 0 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], INTreg);
            break;

        case INLINE_STRING_CHARAT:
        case INLINE_MATH_MAX_INT:
        case INLINE_MATH_MIN_INT:
        case INLINE_STRICT_MATH_MAX_INT:
        case INLINE_STRICT_MATH_MIN_INT:
            //Paranoid
            assert (ssa->numUses > 1 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], INTreg);
            pcgRef (cUnit, ssa->uses[1], INTreg);
            break;

        case INLINE_MATH_ABS_LONG:
        case INLINE_STRICT_MATH_ABS_LONG:
            //Paranoid
            assert (ssa->numUses > 1 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], LLreg);
            pcgRef (cUnit, ssa->uses[1], LLregHi);
            pcgCreatePair (cUnit, ssa->uses[0], ssa->uses[1]);
            break;

        case INLINE_STRING_EQUALS:
        case INLINE_STRING_COMPARETO:
            //Paranoid
            assert (ssa->numUses > 1 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], Any4);
            pcgRef (cUnit, ssa->uses[1], Any4);
            break;

        case INLINE_STRING_FASTINDEXOF_II:
            assert (ssa->numUses > 2 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], Any4);
            pcgRef (cUnit, ssa->uses[1], Any4);
            pcgRef (cUnit, ssa->uses[2], Any4);
            break;

        case INLINE_DOUBLE_TO_RAW_LONG_BITS:
        case INLINE_LONG_BITS_TO_DOUBLE:
        case INLINE_DOUBLE_TO_LONG_BITS:
        case INLINE_MATH_SIN:
        case INLINE_MATH_SQRT:
        case INLINE_MATH_COS:
        case INLINE_STRICT_MATH_SQRT:
        case INLINE_STRICT_MATH_ABS_DOUBLE:

        case INLINE_MATH_ACOS:
        case INLINE_MATH_ASIN:
        case INLINE_MATH_ATAN:
        case INLINE_MATH_CBRT:
        case INLINE_MATH_CEIL:
        case INLINE_MATH_COSH:
        case INLINE_MATH_EXP:
        case INLINE_MATH_EXPM1:
        case INLINE_MATH_FLOOR:
        case INLINE_MATH_LOG:
        case INLINE_MATH_LOG10:
        case INLINE_MATH_LOG1P:
        case INLINE_MATH_RINT:
        case INLINE_MATH_SINH:
        case INLINE_MATH_TAN:
        case INLINE_MATH_TANH:
            //Paranoid
            assert (ssa->numUses > 1 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], Any8);
            pcgRef (cUnit, ssa->uses[1], Any8Hi);
            pcgCreatePair (cUnit, ssa->uses[0], ssa->uses[1]);
            break;

        case INLINE_MATH_ATAN2:
        case INLINE_MATH_HYPOT:
        case INLINE_MATH_NEXTAFTER:
        case INLINE_MATH_POW:
            //Paranoid
            assert (ssa->numUses > 3 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], Any8);
            pcgRef (cUnit, ssa->uses[1], Any8Hi);
            pcgCreatePair (cUnit, ssa->uses[0], ssa->uses[1]);
            pcgRef (cUnit, ssa->uses[2], Any8);
            pcgRef (cUnit, ssa->uses[3], Any8Hi);
            pcgCreatePair (cUnit, ssa->uses[2], ssa->uses[3]);
            break;

        case INLINE_MATH_ABS_FLOAT:
            //Paranoid
            assert (ssa->numUses > 0 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], VXreg32);
            break;

        case INLINE_MATH_ABS_DOUBLE:
            //Paranoid
            assert (ssa->numUses > 1 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], DPVXreg64);
            pcgRef (cUnit, ssa->uses[1], DPVXreg64Hi);
            pcgCreatePair (cUnit, ssa->uses[0], ssa->uses[1]);
            break;

        case INLINE_INT_BITS_TO_FLOAT:
        case INLINE_FLOAT_TO_INT_BITS:
        case INLINE_FLOAT_TO_RAW_INT_BITS:
        case INLINE_STRICT_MATH_ABS_FLOAT:
            //Paranoid
            assert (ssa->numUses > 0 && ssa->uses != 0);
            pcgRef (cUnit, ssa->uses[0], Any4);
            break;

        default:
            //TODO: use the error framework
            ALOGI ("+++ PCG Error +++ Unsupported execute inline routine\n");
            return false;
    }

    return true;
}

/**
 * @brief Analyze the invoke arguments
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param firstArgUse which use is the first argument in the use array
 */
static void pcgAnalyzeInvokeArgs (CompilationUnitPCG *cUnit, MIR *mir, int firstArgUse)
{
    //Get SSARepresentation
    SSARepresentation *ssa = mir->ssaRep;

    //Paranoid: we have an SSA representation and, if we have an iteration,, we have an array
    assert (ssa != 0 && (firstArgUse >= ssa->numUses || ssa->uses != 0));

    // The invoke itself has no information about the types of its arguments
    // So just allow references of any type.
    int max = mir->ssaRep->numUses;
    for (int i = firstArgUse; i < max; i++)
    {
        pcgRef (cUnit, ssa->uses[i], Any);
    }
}

/**
 * @brief Analyze the arguments to OP_FILLED_NEW_ARRAY[_RANGE]
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
static void pcgAnalyzeFilledNewArray (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get SSARepresentation
    SSARepresentation *ssa = mir->ssaRep;

    assert (ssa != 0);

    int max = mir->ssaRep->numUses;
    for (int i = 0; i < max; i++)
    {
        pcgRef (cUnit, ssa->uses[i], Any4);
    }
}

/**
 * @brief Kill VRs from inlined methods after the method is done being executed
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlockPCG in which to kill VRs, to stop them from being dirty out
 */
static void killInlinedVRs (CompilationUnitPCG * cUnit, BasicBlockPCG * bb)
{
    // We need to kill the VRs defined in the inlined method
    BitVector *inlinedVRs = cUnit->getTemporaryBitVector ();
    dvmClearAllBits (inlinedVRs);

    for (int j = 0; j < cUnit->registerWindowShift; j++)
    {
        //Get bitvector associated to it
        BitVector *vrDefsBv = cUnit->getSSANumSet (j);

        if (vrDefsBv != 0)
        {
            // Add all the defs of the inlined VR to the inlinedVRs set
            dvmUnifyBitVectors (inlinedVRs, inlinedVRs, vrDefsBv);
        }
    }

    // Add all defs all inlined VRs to the kills set.
    // Remove all defs of all inlined VRs from the gens set.
    dvmUnifyBitVectors (bb->kills, bb->kills, inlinedVRs);
    dvmSubtractBitVectors (bb->dirtyGens, bb->dirtyGens, inlinedVRs);

    //Free temporary bitvector
    cUnit->freeTemporaryBitVector (inlinedVRs);
}

/**
 * @brief Init Generation and Kills
 * @param cUnit the CompilationUnitPCG
 * @details We have added some complexity here, to track inlined MIRs, and to kill VRs from inlined MIRs after the inlined method is finished.
 */
static void initGensAndKills (CompilationUnitPCG *cUnit)
{
    int bvSize = cUnit->numSSARegs;

    //Get the block list
    GrowableList *blockList = &cUnit->blockList;

    // We're going to track the inlined method (if any)
    // because VRs associated with inlined methods are dead
    // after the inlined method is finished
    const Method * currentInlinedMethod = 0;

    // We define this here, because we want to check the last BB
    // for an inlined MIR -> non inlined MIR transition, too
    BasicBlockPCG * bb = 0;

    //Go through the blocks
    for (unsigned int i = 0; i < blockList->numUsed; i++)
    {
        //Get the current block
        //TODO: change loop to use iterator instead
        bb = (BasicBlockPCG *) blockList->elemList[i];

        //Paranoid
        assert (bb != 0);

        // Allocate and initialize the bit vectors
        bb->availIns = dvmCompilerAllocBitVector (bvSize, false);
        bb->availGens = dvmCompilerAllocBitVector (bvSize, false);
        bb->availOuts = dvmCompilerAllocBitVector (bvSize, false);
        bb->dirtyIns = dvmCompilerAllocBitVector (bvSize, false);
        bb->dirtyGens = dvmCompilerAllocBitVector (bvSize, false);
        bb->dirtyOuts = dvmCompilerAllocBitVector (bvSize, false);
        bb->kills = dvmCompilerAllocBitVector (bvSize, false);

        // Initialize the bitvectors
        dvmClearAllBits (bb->availIns);
        dvmClearAllBits (bb->availGens);
        dvmClearAllBits (bb->dirtyIns);
        dvmClearAllBits (bb->dirtyGens);
        dvmClearAllBits (bb->dirtyOuts);
        dvmClearAllBits (bb->kills);
        dvmSetInitialBits (bb->availOuts, bvSize);

        // The avail outs bit vectors are initialized differently.
        // For entry blocks (including "from interpreter" blocks, which are
        // just alternate entries), the available outs are equal to the live
        // ins.  They are the values that we will load into registers on entry
        // to the trace.  For all other blocks, we mark every SSANum available.
        // (The available SSANum data flow is forward flowing intersection.)
        if (bb->blockType == kEntryBlock || bb->blockType == kFromInterpreter)
        {
            // The live out information needs to be translated from VR number
            // into SSA numbers.
            BitVectorIterator bvIterator;
            BasicBlockPCG *liveInBlock;
            BasicBlockDataFlow *info;

            // The liveness information isn't accurate on the kFromInterpreter
            // block, so we need to read it from the its fallthrough block, i.e.
            // the loop preheader block.
            if (bb->blockType == kEntryBlock)
            {
                liveInBlock = bb;
            }
            else
            {
                liveInBlock = static_cast<BasicBlockPCG *> (bb->fallThrough);
            }
            info = liveInBlock->dataFlowInfo;

            dvmBitVectorIteratorInit (info->liveInV, &bvIterator);
            for (int dalvikVR = dvmBitVectorIteratorNext (&bvIterator);
                    dalvikVR != -1;
                    dalvikVR = dvmBitVectorIteratorNext (&bvIterator))
            {
                int comboSSANum = info->dalvikToSSAMapEntrance[dalvikVR];
                int ssaNum = DECODE_REG (comboSSANum);

                //The live-in vector is over conservative so we only handle initial load if it really is referenced
                if (dvmIsBitSet (cUnit->getReferencedSSARegBV (), ssaNum) == false)
                {
                    continue;
                }

                SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

                // We only generate a load for registerized temps.
                if (info.registerize == true)
                {
                    dvmSetBit (bb->availGens, info.parentSSANum);
                }
            }
        }

        //Go through each instruction
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Get opcode
            Opcode opcode = mir->dalvikInsn.opcode;

            // Ignore PHI's.  They do not define a new value, so they do not
            // "dirty" a VR.
            if ( static_cast<ExtendedMIROpcode> (opcode) == kMirOpPhi)
            {
                continue;
            }

            //Go through each define
            for (int i = 0; i < mir->ssaRep->numDefs; i++)
            {

                // Get SSA and its information
                int ssaNum = mir->ssaRep->defs[i];

                SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

                //Get virtual number
                u2 vrNum = dvmExtractSSARegister (cUnit, ssaNum);

                //Get bitvector associated to it
                BitVector *vrDefsBv = cUnit->getSSANumSet (vrNum);

                //Paranoid
                if (vrDefsBv == 0)
                {
                    //For the moment just make it fail with the generic error
                    cUnit->errorHandler->setError (kJitErrorPcgCodegen);

                    //Just return because this is already a bad enough situation
                    return;
                }

                // Add all defs of this VR to the kills set.
                // Remove all defs of this VR from the gens set.
                dvmUnifyBitVectors (bb->kills, bb->kills, vrDefsBv);
                dvmSubtractBitVectors (bb->dirtyGens, bb->dirtyGens, vrDefsBv);

                // Add this def to both the "avail" and "dirty" gens sets
                if (info.registerize == true)
                {
                    dvmSetBit (bb->dirtyGens, info.parentSSANum);
                    dvmClearBit(bb->kills, info.parentSSANum);
                    dvmSetBit (bb->availGens, info.parentSSANum);
                }

                //If not extended
                if (opcode < static_cast<Opcode> (kMirOpFirst))
                {
                    //Get the flags
                    int flags = dexGetFlagsFromOpcode (opcode);

                    //Return whether the invoke mask is non null
                    if ((flags & kInstrInvoke) != 0)
                    {
                        dvmSetInitialBits (bb->kills, bvSize);
                        dvmClearAllBits (bb->dirtyGens);
                    }
                }
            }

            // There are three cases for inlined VRs we track here:
            // 1) we move from non-inlined MIRs to inlined MIRs
            // 2) we move from inlined MIRs to non-inlined MIRs
            // 3) we move from inlined MIRs to inlined MIRs from different methods
            if (mir->nesting.parent != 0 && currentInlinedMethod == 0)
            {
                // 1) just set the currently tracked method
                currentInlinedMethod = mir->nesting.sourceMethod;
            }
            else if (mir->nesting.parent == 0 && currentInlinedMethod != 0)
            {
                killInlinedVRs (cUnit, bb);

                // Future MIRs aren't from an inlined method any more
                currentInlinedMethod = 0;
            }
            else if (mir->nesting.parent != 0 &&
                     currentInlinedMethod != mir->nesting.sourceMethod)
            {
                killInlinedVRs (cUnit, bb);

                // Future MIRs are from a different inlined method
                currentInlinedMethod = mir->nesting.sourceMethod;
            }

        }
    }

    // We need to check the end of the cUnit, too, because
    // we may have dirty out VRs from the inlined method
    // which doesn't make sense
    if (currentInlinedMethod != 0)
    {
        killInlinedVRs (cUnit, bb);
    }
}

/**
 * @brief Propagate out registers
 * @param cUnit the CompilationUnitPCG
 */
static void propagateOuts (CompilationUnitPCG *cUnit)
{
    GrowableList *blockList = &cUnit->blockList;
    bool changed = true;

    // Allocate the temporary bit vectors that will be available for the
    // duration of routine compilation.  tempBV is for small local uses.
    BitVector *tempBV = cUnit->getTemporaryBitVector ();

    while (changed == true)
    {
        changed = false;

        for (unsigned int i = 0; i < blockList->numUsed; i++)
        {
            BasicBlockPCG *bb = (BasicBlockPCG *)blockList->elemList[i];

            BitVector *availIns = bb->availIns;
            BitVector *availOuts = bb->availOuts;
            BitVector *availGens = bb->availGens;
            BitVector *dirtyIns = bb->dirtyIns;
            BitVector *dirtyGens = bb->dirtyGens;
            BitVector *dirtyOuts = bb->dirtyOuts;
            BitVector *kills = bb->kills;
            BitVectorIterator it;
            int predId;
            bool first = true;

            // The dataflow equations:
            // AvailIns = Intersection of AvailOuts of preds
            // AvailOuts = AvailIns | AvailGens
            // DirtyIns = (Union of DirtyOuts of preds) & AvailIns
            // DirtyOuts = DirtyIns - kills + DirtyGens
            dvmBitVectorIteratorInit (bb->predecessors, &it);
            while ( (predId = dvmBitVectorIteratorNext (&it)) != -1)
            {
                BasicBlockPCG *bb = (BasicBlockPCG *) blockList->elemList[predId];
                BitVector *predAvailOuts = bb->availOuts;
                BitVector *predOuts = bb->dirtyOuts;

                if (first == true)
                {
                    dvmCopyBitVector (availIns, predAvailOuts);
                    dvmCopyBitVector (dirtyIns, predOuts);
                    first = false;
                }
                else
                {
                    dvmIntersectBitVectors (availIns, availIns, predAvailOuts);
                    dvmUnifyBitVectors (dirtyIns, dirtyIns, predOuts);
                }
            }
            dvmIntersectBitVectors(dirtyIns, dirtyIns, availIns);

            // dirtyOuts = dirtyIns - kills + dirtyGens
            dvmCopyBitVector (tempBV, dirtyIns);
            dvmSubtractBitVectors (tempBV, tempBV, kills);
            dvmUnifyBitVectors (tempBV, tempBV, dirtyGens);
            if (dvmCheckCopyBitVector (dirtyOuts, tempBV) == true)
            {
                changed = true;
            }

            // availOuts = availIns + availGens
            dvmCopyBitVector (tempBV, availIns);
            dvmUnifyBitVectors (tempBV, tempBV, availGens);
            if (dvmCheckCopyBitVector (availOuts, tempBV))
            {
                changed = true;
            }
        }
    }

    //Free temporary bitvector
    cUnit->freeTemporaryBitVector (tempBV);
}

/**
 * @brief Go through the information to avoid deferred write backs
 * @details Avoid putting identical writeback sequences at every trace exit
 *  for no useful purpose. Hence, if you dirty a VR with an instruction that
 *  will only be executed once per trace and is never overwritten by a later
 *  update to that same VR, you might as well write it back right away.
 * @param cUnit the CompilationUnitPCG
 */
static void pcgAvoidDeferredWritebacks (CompilationUnitPCG *cUnit)
{
    GrowableList *blockList = &cUnit->blockList;
    BitVectorIterator it;
    int ssaNum;
    int alwaysDeferWritebacks;

    if (dvmExtractBackendOption ("AlwaysDeferWB", &alwaysDeferWritebacks) == true && alwaysDeferWritebacks == true)
    {
        return;
    }

    // Compute a union of all the writebacks that must occur at chaining cell trace exits.
    BitVector *tempBV = cUnit->getTemporaryBitVector ();

    //Go through each BB
    //TODO: use iterator instead
    for (unsigned int i = 0; i < blockList->numUsed; i++)
    {
        BasicBlockPCG *bb = (BasicBlockPCG *)blockList->elemList[i];

        if (bb->blockType < kChainingCellLast)
        {
            dvmUnifyBitVectors (tempBV, tempBV, bb->dirtyIns);
        }
    }

    dvmBitVectorIteratorInit (tempBV, &it);
    ssaNum = dvmBitVectorIteratorNext (&it);

    while (ssaNum != -1)
    {
        //Get information
        SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

        //Purely local registers do not have a home location and thus they must be registerized.
        //Thus the deferred writeback should only be disabled for regular virtual registers.
        if (dvmCompilerIsPureLocalScratch (cUnit, ssaNum, true) == false)
        {
            //Update deferment
            info.deferWriteback = false;
        }

        //Get next number
        ssaNum = dvmBitVectorIteratorNext (&it);
    }

    cUnit->freeTemporaryBitVector (tempBV);
}

void dvmCompilerPcgApplyRegisterizationHeuristics (CompilationUnitPCG *cUnit, int ssaNum, SSANumInfo &info)
{
    int zheur;
    u2 vrSub = 0;  // These initializations are unnecessary, but they avoid
    u2 vrNum = 0;  // a compiler warning about "may be used uninitialized".

    if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        CompilationUnit *simpleCUnit = dynamic_cast<CompilationUnit *> (cUnit);
        int dalvikReg = dvmConvertSSARegToDalvik (simpleCUnit, ssaNum);
        vrNum = DECODE_REG (dalvikReg);
        vrSub = DECODE_SUB (dalvikReg);
    }

    if (dvmExtractBackendOption ("zheur", &zheur) == false)
    {
        zheur = 0;
    }

    if (zheur > 0)
    {
        //Check if we should be disabling registerization for define
        if (info.numUses <= zheur
                && info.parentSSANum == ssaNum
                && (info.mir == 0 || (ExtendedMIROpcode) (info.mir->dalvikInsn.opcode) != kMirOpPhi)
                && info.needsNullCheck == false
                && dvmCompilerIsPureLocalScratch (cUnit, ssaNum, true) == false)
        {
            cUnit->disableRegisterizationForDef (ssaNum);
            if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)

            {
                ALOGI ("    Register v%d_%d [ssanum : %d]. NOT registerized globally. Z-heuristics (1).\n", vrNum, vrSub, ssaNum);
            }
        }
        else
        {
            if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)

            {
                ALOGI ("    Register v%d_%d [ssanum : %d]. Registerized globally. Escaped Z-heuristics (1).\n", vrNum, vrSub, ssaNum);
            }
        }
    }

    if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        //Get information
        SSANumInfo &info = cUnit->getSSANumInformation (ssaNum);

        if (info.parentSSANum == ssaNum)
        {
            const int cBufSize = 128;
            char buffer[cBufSize];
            std::string s;

            snprintf(buffer, cBufSize,  "v%d_%d type %s ",
                     vrNum, vrSub, dvmCompilerPcgGetDtypeName (info.dtype));
            s += buffer;

            if (info.registerize == false) {
               s += "(no reg) ";
            }
            if (info.pairSSANum != 0) {
                // Use the root SSA number for the pair
                CompilationUnit *simpleCUnit = dynamic_cast<CompilationUnit *> (cUnit);
                SSANumInfo &pairInfo = cUnit->getSSANumInformation (info.pairSSANum);
                int pairReg = dvmConvertSSARegToDalvik (simpleCUnit, pairInfo.parentSSANum);
                int pairNum = DECODE_REG (pairReg);
                int pairSub = DECODE_SUB (pairReg);
                snprintf(buffer, cBufSize,  "(pair of v%d_%d) ", pairNum, pairSub);
                s += buffer;
            }
            snprintf(buffer, cBufSize, "[ssanum : %d]", ssaNum);
            s += buffer;
            ALOGI("%s", s.c_str());
        }
    }
}

bool dvmCompilerPcgNewRegisterizeVRAnalysis (CompilationUnitPCG *cUnit)
{
    GrowableList *blockList = &cUnit->blockList;

    for (unsigned int i = 0; i < blockList->numUsed; i++)
    {
        BasicBlock *bb = (BasicBlock *)blockList->elemList[i];
        for (MIR *mir = bb->firstMIRInsn; mir; mir = mir->next)
        {
            //Get the SSARepresentation
            SSARepresentation *ssaRep = mir->ssaRep;

            //Paranoid
            assert (ssaRep != 0);

            int dalvikOpCode = mir->dalvikInsn.opcode;

            switch (dalvikOpCode)
            {
                case OP_NOP:
                case OP_GOTO:
                case OP_GOTO_16:
                case OP_GOTO_32:
                    // Nothing to do.
                    break;

                case OP_MOVE:
                case OP_MOVE_OBJECT:
                case OP_MOVE_FROM16:
                case OP_MOVE_OBJECT_FROM16:
                case OP_MOVE_16:
                case OP_MOVE_OBJECT_16:

                    //Paranoid
                    assert (ssaRep->numUses > 0 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], Any4);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_ADD_INT_LIT16:
                case OP_AND_INT_LIT16:
                case OP_OR_INT_LIT16:
                case OP_XOR_INT_LIT16:
                case OP_ADD_INT_LIT8:
                case OP_RSUB_INT_LIT8:
                case OP_RSUB_INT:
                case OP_AND_INT_LIT8:
                case OP_SHL_INT_LIT8:
                case OP_SHR_INT_LIT8:
                case OP_USHR_INT_LIT8:
                case OP_OR_INT_LIT8:
                case OP_XOR_INT_LIT8:
                case OP_MUL_INT_LIT8:
                case OP_MUL_INT_LIT16:
                case OP_INT_TO_CHAR:
                case OP_INT_TO_SHORT:
                case OP_INT_TO_BYTE:
                case OP_NEG_INT:
                case OP_NOT_INT:
                case OP_INSTANCE_OF:
                case OP_DIV_INT_LIT8:
                case OP_REM_INT_LIT8:
                case OP_DIV_INT_LIT16:
                case OP_REM_INT_LIT16:
                case OP_ARRAY_LENGTH:
                    //Paranoid
                    assert (ssaRep->numUses > 0 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_IGET:
                case OP_IGET_BOOLEAN:
                case OP_IGET_BYTE:
                case OP_IGET_CHAR:
                case OP_IGET_SHORT:
                case OP_IGET_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 0 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_IGET_OBJECT:
                case OP_IGET_OBJECT_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 0 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_INT_TO_LONG:
                    //Paranoid
                    assert (ssaRep->numUses > 0 && ssaRep->numDefs > 1);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], LLreg);
                    pcgDefRef (cUnit, ssaRep->defs[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_IGET_WIDE:
                case OP_IGET_WIDE_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 0 && ssaRep->numDefs > 1);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_LONG_TO_INT:
                    //Paranoid
                    assert (ssaRep->numUses > 1 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_CMP_LONG:
                    //Paranoid
                    assert (ssaRep->numUses > 3 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], LLreg);
                    pcgRef (cUnit, ssaRep->uses[3], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[2], ssaRep->uses[3]);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_MOVE_WIDE:
                case OP_MOVE_WIDE_FROM16:
                case OP_MOVE_WIDE_16:
                    //Paranoid
                    assert (ssaRep->numUses > 1 && ssaRep->numDefs > 1);

                    pcgRef (cUnit, ssaRep->uses[0], Any8);
                    pcgRef (cUnit, ssaRep->uses[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_CONST:
                case OP_CONST_4:
                case OP_CONST_16:
                case OP_CONST_HIGH16:
                    //Paranoid
                    assert (ssaRep->numDefs > 0);

                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_CONST_STRING:
                case OP_CONST_STRING_JUMBO:
                    //Paranoid
                    assert (ssaRep->numDefs > 0);

                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_CONST_WIDE:
                case OP_CONST_WIDE_16:
                case OP_CONST_WIDE_HIGH16:
                case OP_CONST_WIDE_32:
                    //Paranoid
                    assert (ssaRep->numDefs > 1);

                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_IF_EQ:
                case OP_IF_NE:
                case OP_IF_LT:
                case OP_IF_GE:
                case OP_IF_GT:
                case OP_IF_LE:
                    //Paranoid
                    assert (ssaRep->numUses > 1);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    break;

                case OP_IPUT:
                case OP_IPUT_BOOLEAN:
                case OP_IPUT_BYTE:
                case OP_IPUT_CHAR:
                case OP_IPUT_SHORT:
                case OP_IPUT_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 1);

                    pcgRef (cUnit, ssaRep->uses[0], Any4);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg, true);
                    break;

                case OP_IPUT_OBJECT:
                case OP_IPUT_OBJECT_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 1);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg, true);
                    break;

                case OP_IF_GEZ:
                case OP_IF_NEZ:
                case OP_IF_EQZ:
                case OP_IF_LTZ:
                case OP_IF_GTZ:
                case OP_IF_LEZ:
                    //Paranoid
                    assert (ssaRep->numUses > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    break;

                case OP_SPUT:
                case OP_SPUT_BYTE:
                case OP_SPUT_CHAR:
                case OP_SPUT_SHORT:
                case OP_SPUT_BOOLEAN:
                case OP_SPUT_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 0);

                    pcgRef (cUnit, ssaRep->uses[0], Any4);
                    break;

                case OP_SPUT_OBJECT:
                case OP_SPUT_OBJECT_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    break;

                case OP_SPUT_WIDE:
                case OP_SPUT_WIDE_VOLATILE:
                    //Paranoid
                    assert (ssaRep->numUses > 1);

                    pcgRef (cUnit, ssaRep->uses[0], Any8);
                    pcgRef (cUnit, ssaRep->uses[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    break;

                case OP_CHECK_CAST:
                case OP_PACKED_SWITCH:
                case OP_SPARSE_SWITCH:
                    //Paranoid
                    assert (ssaRep->numUses > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    break;

                case OP_MUL_INT:
                case OP_SUB_INT:
                case OP_ADD_INT:
                case OP_OR_INT:
                case OP_AND_INT:
                case OP_XOR_INT:
                case OP_SHL_INT:
                case OP_SHR_INT:
                case OP_USHR_INT:
                case OP_DIV_INT:
                case OP_REM_INT:
                    //Paranoid
                    assert (ssaRep->numUses > 1 && ssaRep->numDefs > 0);

                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_MUL_LONG:
                case OP_ADD_LONG:
                case OP_SUB_LONG:
                case OP_AND_LONG:
                case OP_OR_LONG:
                case OP_XOR_LONG:
                case OP_DIV_LONG:
                case OP_REM_LONG:
                case OP_ADD_LONG_2ADDR:
                case OP_SUB_LONG_2ADDR:
                case OP_MUL_LONG_2ADDR:
                case OP_DIV_LONG_2ADDR:
                case OP_REM_LONG_2ADDR:
                case OP_OR_LONG_2ADDR:
                case OP_AND_LONG_2ADDR:
                case OP_XOR_LONG_2ADDR:
                    //Paranoid
                    assert (ssaRep->numUses > 3 && ssaRep->numDefs > 1);

                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], LLreg);
                    pcgRef (cUnit, ssaRep->uses[3], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[2], ssaRep->uses[3]);
                    pcgDefRef (cUnit, ssaRep->defs[0], LLreg);
                    pcgDefRef (cUnit, ssaRep->defs[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_NEG_LONG:
                case OP_NOT_LONG:
                    //Paranoid
                    assert (ssaRep->numUses > 1 && ssaRep->numDefs > 1);

                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], LLreg);
                    pcgDefRef (cUnit, ssaRep->defs[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_SHL_LONG:
                case OP_SHR_LONG:
                case OP_USHR_LONG:
                case OP_SHL_LONG_2ADDR:
                case OP_SHR_LONG_2ADDR:
                case OP_USHR_LONG_2ADDR:
                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], LLreg);
                    pcgDefRef (cUnit, ssaRep->defs[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_MONITOR_ENTER:
                case OP_MONITOR_EXIT:
                case OP_FILL_ARRAY_DATA:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    break;

                case OP_FILLED_NEW_ARRAY:
                case OP_FILLED_NEW_ARRAY_RANGE:
                    pcgAnalyzeFilledNewArray (cUnit, mir);
                    break;

                case OP_IPUT_QUICK:
                    pcgRef (cUnit, ssaRep->uses[0], Any4);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg, true);
                    break;

                case OP_IPUT_OBJECT_QUICK:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg, true);
                    break;

                case OP_IPUT_WIDE:
                case OP_IPUT_WIDE_QUICK:
                case OP_IPUT_WIDE_VOLATILE:
                    pcgRef (cUnit, ssaRep->uses[0], Any8);
                    pcgRef (cUnit, ssaRep->uses[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], INTreg, true);
                    break;

                case OP_AGET:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_AGET_OBJECT:
                case OP_AGET_BYTE:
                case OP_AGET_BOOLEAN:
                case OP_AGET_CHAR:
                case OP_AGET_SHORT:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_AGET_WIDE:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_SGET:
                case OP_SGET_VOLATILE:
                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_SGET_OBJECT:
                case OP_SGET_OBJECT_VOLATILE:
                case OP_SGET_BOOLEAN:
                case OP_SGET_CHAR:
                case OP_SGET_BYTE:
                case OP_SGET_SHORT:
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_CONST_CLASS:
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_SGET_WIDE:
                case OP_SGET_WIDE_VOLATILE:
                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_ADD_INT_2ADDR:
                case OP_SUB_INT_2ADDR:
                case OP_AND_INT_2ADDR:
                case OP_XOR_INT_2ADDR:
                case OP_OR_INT_2ADDR:
                case OP_MUL_INT_2ADDR:
                case OP_SHL_INT_2ADDR:
                case OP_SHR_INT_2ADDR:
                case OP_USHR_INT_2ADDR:
                case OP_DIV_INT_2ADDR:
                case OP_REM_INT_2ADDR:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_APUT:
                    pcgRef (cUnit, ssaRep->uses[0], Any4);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg, true);
                    pcgRef (cUnit, ssaRep->uses[2], INTreg);
                    break;

                case OP_APUT_OBJECT:
                case OP_APUT_CHAR:
                case OP_APUT_BYTE:
                case OP_APUT_BOOLEAN:
                case OP_APUT_SHORT:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg, true);
                    pcgRef (cUnit, ssaRep->uses[2], INTreg);
                    break;

                case OP_APUT_WIDE:
                    pcgRef (cUnit, ssaRep->uses[0], Any8);
                    pcgRef (cUnit, ssaRep->uses[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], INTreg, true);
                    pcgRef (cUnit, ssaRep->uses[3], INTreg);
                    break;

                case OP_ADD_FLOAT:
                case OP_SUB_FLOAT:
                case OP_MUL_FLOAT:
                case OP_DIV_FLOAT:
                case OP_REM_FLOAT:
                case OP_ADD_FLOAT_2ADDR:
                case OP_SUB_FLOAT_2ADDR:
                case OP_MUL_FLOAT_2ADDR:
                case OP_DIV_FLOAT_2ADDR:
                case OP_REM_FLOAT_2ADDR:
                    pcgRef (cUnit, ssaRep->uses[0], VXreg32);
                    pcgRef (cUnit, ssaRep->uses[1], VXreg32);
                    pcgDefRef (cUnit, ssaRep->defs[0], VXreg32);
                    break;

                case OP_ADD_DOUBLE:
                case OP_SUB_DOUBLE:
                case OP_MUL_DOUBLE:
                case OP_DIV_DOUBLE:
                case OP_REM_DOUBLE:
                case OP_ADD_DOUBLE_2ADDR:
                case OP_SUB_DOUBLE_2ADDR:
                case OP_MUL_DOUBLE_2ADDR:
                case OP_DIV_DOUBLE_2ADDR:
                case OP_REM_DOUBLE_2ADDR:
                    pcgRef (cUnit, ssaRep->uses[0], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[3], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[2], ssaRep->uses[3]);
                    pcgDefRef (cUnit, ssaRep->defs[0], DPVXreg64);
                    pcgDefRef (cUnit, ssaRep->defs[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_CMPG_FLOAT:
                case OP_CMPL_FLOAT:
                    pcgRef (cUnit, ssaRep->uses[0], VXreg32);
                    pcgRef (cUnit, ssaRep->uses[1], VXreg32);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_CMPG_DOUBLE:
                case OP_CMPL_DOUBLE:
                    pcgRef (cUnit, ssaRep->uses[0], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgRef (cUnit, ssaRep->uses[2], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[3], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[2], ssaRep->uses[3]);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_INT_TO_DOUBLE:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], DPVXreg64);
                    pcgDefRef (cUnit, ssaRep->defs[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_LONG_TO_DOUBLE:
                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], DPVXreg64);
                    pcgDefRef (cUnit, ssaRep->defs[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_INT_TO_FLOAT:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], VXreg32);
                    break;

                case OP_LONG_TO_FLOAT:
                    pcgRef (cUnit, ssaRep->uses[0], LLreg);
                    pcgRef (cUnit, ssaRep->uses[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], VXreg32);
                    break;

                case OP_DOUBLE_TO_INT:
                    pcgRef (cUnit, ssaRep->uses[0], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_FLOAT_TO_INT:
                    pcgRef (cUnit, ssaRep->uses[0], VXreg32);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_FLOAT_TO_LONG:
                    pcgRef (cUnit, ssaRep->uses[0], VXreg32);
                    pcgDefRef (cUnit, ssaRep->defs[0], LLreg);
                    pcgDefRef (cUnit, ssaRep->defs[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_DOUBLE_TO_LONG:
                    pcgRef (cUnit, ssaRep->uses[0], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], LLreg);
                    pcgDefRef (cUnit, ssaRep->defs[1], LLregHi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_FLOAT_TO_DOUBLE:
                    pcgRef (cUnit, ssaRep->uses[0], VXreg32);
                    pcgDefRef (cUnit, ssaRep->defs[0], DPVXreg64);
                    pcgDefRef (cUnit, ssaRep->defs[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_DOUBLE_TO_FLOAT:
                    pcgRef (cUnit, ssaRep->uses[0], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], VXreg32);
                    break;

                case OP_NEW_ARRAY:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_IGET_OBJECT_QUICK:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_IGET_QUICK:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_IGET_WIDE_QUICK:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_MOVE_RESULT:
                case OP_MOVE_RESULT_OBJECT:
                    pcgDefRef (cUnit, ssaRep->defs[0], Any4);
                    break;

                case OP_NEG_FLOAT:
                    pcgRef (cUnit, ssaRep->uses[0], VXreg32);
                    pcgDefRef (cUnit, ssaRep->defs[0], VXreg32);
                    break;

                case OP_NEG_DOUBLE:
                    pcgRef (cUnit, ssaRep->uses[0], DPVXreg64);
                    pcgRef (cUnit, ssaRep->uses[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    pcgDefRef (cUnit, ssaRep->defs[0], DPVXreg64);
                    pcgDefRef (cUnit, ssaRep->defs[1], DPVXreg64Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_RETURN_VOID:
                case OP_RETURN_VOID_BARRIER:
                    break;

                case OP_RETURN:
                case OP_RETURN_OBJECT:
                    pcgRef (cUnit, ssaRep->uses[0], Any4);
                    break;

                case OP_RETURN_WIDE:
                    pcgRef (cUnit, ssaRep->uses[0], Any8);
                    pcgRef (cUnit, ssaRep->uses[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->uses[0], ssaRep->uses[1]);
                    break;

                case OP_MOVE_RESULT_WIDE:
                    pcgDefRef (cUnit, ssaRep->defs[0], Any8);
                    pcgDefRef (cUnit, ssaRep->defs[1], Any8Hi);
                    pcgCreatePair (cUnit, ssaRep->defs[0], ssaRep->defs[1]);
                    break;

                case OP_NEW_INSTANCE:
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case OP_EXECUTE_INLINE:
                case OP_EXECUTE_INLINE_RANGE:
                    //If it fails, we fail
                    if (pcgAnalyzeExecuteInline (cUnit, mir) == false)
                    {
                        return false;
                    }
                    break;

                case OP_INVOKE_VIRTUAL:
                case OP_INVOKE_VIRTUAL_RANGE:
                case OP_INVOKE_DIRECT:
                case OP_INVOKE_DIRECT_RANGE:
                case OP_INVOKE_VIRTUAL_QUICK:
                case OP_INVOKE_VIRTUAL_QUICK_RANGE:
                case OP_INVOKE_INTERFACE:
                case OP_INVOKE_INTERFACE_RANGE:
                    // The first arg must be treated as INTreg, because it
                    // gets 0 checked.
                    //
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);  // "this" pointer
                    pcgAnalyzeInvokeArgs (cUnit, mir, 1);
                    break;

                case OP_INVOKE_STATIC:
                case OP_INVOKE_STATIC_RANGE:
                case OP_INVOKE_SUPER:
                case OP_INVOKE_SUPER_RANGE:
                case OP_INVOKE_SUPER_QUICK:
                case OP_INVOKE_SUPER_QUICK_RANGE:
                    pcgAnalyzeInvokeArgs (cUnit, mir, 0);
                    break;

                case kMirOpRegisterize:
                    for (int i = 0; i < mir->ssaRep->numUses; i++)
                    {
                        pcgRef (cUnit, ssaRep->uses[i], Any);
                    }
                    break;

                case kMirOpPhi:
                    {
                        // Use the result's SSA Num for all operands of the phi.
                        int ssaNum = ssaRep->defs[0];
                        pcgDefRef (cUnit, ssaNum, Any);

                        int i;
                        for (i = 0; i < mir->ssaRep->numUses; i++)
                        {
                            pcgRef (cUnit, ssaRep->uses[i], Any);
                            pcgMergeSSANums (cUnit, ssaRep->uses[i], ssaNum);
                        }
                    }
                    break;

                case kMirOpCheckInlinePrediction:
                    // Reference the "this" pointer
                    pcgRef (cUnit, ssaRep->uses[0], INTreg, true);
                    break;

                case kMirOpLowerBound:
                case kMirOpNullCheck:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    break;

                case kMirOpNullNRangeUpCheck:
                case kMirOpNullNRangeDownCheck:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    break;

                case kMirOpBoundCheck:
                    // kMirOpBoundCheck is special, because the index being
                    // checked might either be specified as a constant or a VR.
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    if (ssaRep->numUses > 1) {
                        pcgRef (cUnit, ssaRep->uses[1], INTreg);
                    }
                    break;

                case kMirOpCheckStackOverflow:
                    //No virtual registers are being used
                    break;

                // All the packed opcodes have references to XMM physical
                // registers.  We do not need to do any analysis for them.  We
                // already know their data types, they are always registerized,
                // and they are never written back.  We only need to record the
                // uses and definitions of the VRs.
                case kMirOpPackedSet:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    break;

                case kMirOpPackedAddReduce:
                    pcgRef (cUnit, ssaRep->uses[0], INTreg);
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case kMirOpPackedReduce:
                    pcgDefRef (cUnit, ssaRep->defs[0], INTreg);
                    break;

                case kMirOpConst128b:
                case kMirOpMove128b:
                case kMirOpPackedAddition:
                case kMirOpPackedMultiply:
                case kMirOpPackedSubtract:
                case kMirOpPackedShiftLeft:
                case kMirOpPackedSignedShiftRight:
                case kMirOpPackedUnsignedShiftRight:
                case kMirOpPackedAnd:
                case kMirOpPackedOr:
                case kMirOpPackedXor:
                    break;

                default:
                    ALOGI ("Unsupported instruction in trace for new registerization:");
                    char mybuf[2048];
                    dvmCompilerExtendedDisassembler (cUnit, mir, &mir->dalvikInsn, mybuf, sizeof (mybuf));
                    ALOGI ("%s\n", mybuf);

                    //TODO: use the error framework
                    return false;
            }

            // Any uses of "version 0" of a virtual register indicates a value
            // that is live on entry to the trace.  Currently, we generate a
            // load in the entry block for this.  Inserting the ssaNum into
            // the pcgReferencedVRs triggers this.
            //
            // TODO: This isn't quite right for wide uses.  We only want to
            //       generate a load of the first use in that case.
            for (int j = 0; j < mir->ssaRep->numUses; j++)
            {
                int ssaNum = mir->ssaRep->uses[j];
                int dalvikReg = dvmConvertSSARegToDalvik (cUnit, ssaNum);
                if (DECODE_SUB (dalvikReg) == 0)
                {
                    SSANumInfo &info = cUnit->getSSANumInformation (ssaNum);
                    int parentSSANum = info.parentSSANum;
                    cUnit->insertReferencedVR (parentSSANum);
                }
            }

            // initialize the mir field of pcgSSANumInfo
            for (int i = 0; i < mir->ssaRep->numDefs; i++)
            {
                int ssaNum = mir->ssaRep->defs[i];
                SSANumInfo &info = cUnit->getSSANumInformation (ssaNum);
                info.mir = mir;
            }
        }
    }

    // Walk through the MIR again looking for copies.  This will avoid type
    // mismatches that may be costly in runtime performance.  This isn't
    // perfect.  In theory, we might need to iterate multiple times until
    // there are no more changes.  If we decide to do that, we should probably
    // keep track of the moves and then iterate over only the moves.
    for (unsigned int i = 0; i < blockList->numUsed; i++)
    {
        BasicBlock *bb = (BasicBlock *)blockList->elemList[i];
        for (MIR *mir = bb->firstMIRInsn; mir; mir = mir->next)
        {
            pcgDtype srcDtype, dstDtype, mergedDtype;
            pcgDtype src2Dtype, dst2Dtype, merged2Dtype;
            Opcode dalvikOpCode = mir->dalvikInsn.opcode;

            //Get SSARepresentation
            SSARepresentation *ssaRep = mir->ssaRep;

            //Paranoid
            assert (ssaRep != 0);

            switch (dalvikOpCode)
            {
                case OP_MOVE:
                case OP_MOVE_OBJECT:
                case OP_MOVE_FROM16:
                case OP_MOVE_OBJECT_FROM16:
                case OP_MOVE_16:
                case OP_MOVE_OBJECT_16:
                    srcDtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[0]);
                    dstDtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]);
                    mergedDtype = pcgMergeDtypes (srcDtype, dstDtype);
                    if (mergedDtype != NOreg)
                    {
                        dvmCompilerPcgSetDtypeForSSANum (cUnit, ssaRep->uses[0], mergedDtype);
                        dvmCompilerPcgSetDtypeForSSANum (cUnit, ssaRep->defs[0], mergedDtype);
                    }
                    break;

                case OP_MOVE_WIDE:
                case OP_MOVE_WIDE_FROM16:
                case OP_MOVE_WIDE_16:
                    srcDtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[0]);
                    dstDtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]);
                    src2Dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[1]);
                    dst2Dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[1]);
                    mergedDtype = pcgMergeDtypes (srcDtype, dstDtype);
                    merged2Dtype = pcgMergeDtypes (src2Dtype, dst2Dtype);
                    if (mergedDtype != NOreg && merged2Dtype != NOreg)
                    {
                        dvmCompilerPcgSetDtypeForSSANum (cUnit, ssaRep->uses[0], mergedDtype);
                        dvmCompilerPcgSetDtypeForSSANum (cUnit, ssaRep->defs[0], mergedDtype);
                        dvmCompilerPcgSetDtypeForSSANum (cUnit, ssaRep->uses[1], merged2Dtype);
                        dvmCompilerPcgSetDtypeForSSANum (cUnit, ssaRep->defs[1], merged2Dtype);
                    }
                    break;

                default:
                    break;
            }
        }
    }

    //Return success
    return true;
}

void dvmCompilerPcgModSSANum (CompilationUnitPCG *cUnit)
{
    initGensAndKills (cUnit);
    propagateOuts (cUnit);

    if (!cUnit->loopInformation)
    {
        pcgAvoidDeferredWritebacks (cUnit);
    }

    if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        dvmCompilerPcgDumpModRegInfo (cUnit);
    }
}

void dvmCompilerPcgMarkPossiblyReferenced (BasicBlockPCG *bb)
{
    bool isChainingCell;

    if (bb == 0 || bb->possiblyReferenced == true)
    {
        return;
    }

    bb->possiblyReferenced = true;

    // Any reference to a normal code block should cause us to bind the
    // label for it, even if it is only referenced by an invoke.
    // Chaining cells are different, because we use "possibly referenced"
    // to mean "saw a branch to the exit trampoline for the cell".
    // References to chaining cells at invokes refer to the chaining cell
    // itself and not to the exit trampoline.  So do not set
    // possiblyReferenced for chaining cell blocks referenced by invokes.

    //Now handle the fallThrough
    if (bb->fallThrough != 0)
    {
        isChainingCell = bb->fallThrough->blockType < kChainingCellLast;

        if (isChainingCell == false || dvmCompilerPcgBlockEndsInInvoke (bb) == false)
        {
            BasicBlockPCG *ft = (BasicBlockPCG *) (bb->fallThrough);
            dvmCompilerPcgMarkPossiblyReferenced (ft);
        }
    }

    //Now handle the taken
    if (bb->taken != 0)
    {
        isChainingCell = bb->taken->blockType < kChainingCellLast;

        if (isChainingCell == false || dvmCompilerPcgBlockEndsInInvoke (bb) == false)
        {
            BasicBlockPCG *taken = (BasicBlockPCG *) (bb->taken);
            dvmCompilerPcgMarkPossiblyReferenced (taken);
        }
    }
}

bool dvmCompilerPcgFillReferencedSsaVector (CompilationUnit *cUnitPCG, BasicBlock *bb)
{
    //It must be the case that the PCG specific cUnit is being used here.
    CompilationUnitPCG *cUnit = reinterpret_cast<CompilationUnitPCG *> (cUnitPCG);

    //Walk the mirs to find all of the referenced ssa registers.
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        SSARepresentation *ssaRep = mir->ssaRep;

        if (ssaRep == 0)
        {
            continue;
        }

        for (int i = 0; i < ssaRep->numUses; i++)
        {
            dvmSetBit (cUnit->getReferencedSSARegBV (), ssaRep->uses[i]);
        }

        for (int i = 0; i < ssaRep->numDefs; i++)
        {
            dvmSetBit (cUnit->getReferencedSSARegBV (), ssaRep->defs[i]);
        }
    }

    //There were no updates done to the basic block.
    return false;
}
