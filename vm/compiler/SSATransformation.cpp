/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include "BBOptimization.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "Loop.h"
#include "LoopRegisterUsage.h"
#include "libdex/DexOpcodes.h"
#include "SSAWalkData.h"
#include "Utility.h"

/* Enter the node to the dfsOrder list then visit its successors */
static void recordDFSPreOrder(CompilationUnit *cUnit, BasicBlock *block)
{

    if (block->visited || block->hidden) return;
    block->visited = true;

    /* Enqueue the block id */
    dvmInsertGrowableList(&cUnit->dfsOrder, block->id);

    //Create iterator for visiting children
    ChildBlockIterator childIter (block);

    //Now iterate through the children to record pre-order
    for (BasicBlock **childPtr = childIter.getNextChildPtr (); childPtr != 0; childPtr = childIter.getNextChildPtr ())
    {
        BasicBlock *child = *childPtr;

        assert (child != 0);

        recordDFSPreOrder (cUnit, child);
    }

    return;
}

/* Sort the blocks by the Depth-First-Search pre-order */
static void computeDFSOrder(CompilationUnit *cUnit)
{
    /* Initialize or reset the DFS order list */
    if (cUnit->dfsOrder.elemList == NULL) {
        dvmInitGrowableList(&cUnit->dfsOrder, cUnit->numBlocks);
    } else {
        /* Just reset the used length on the counter */
        cUnit->dfsOrder.numUsed = 0;
    }

    dvmCompilerDataFlowAnalysisDispatcher(cUnit, dvmCompilerClearVisitedFlag,
                                          kAllNodes,
                                          false /* isIterative */);

    recordDFSPreOrder(cUnit, cUnit->entryBlock);
    cUnit->numReachableBlocks = cUnit->dfsOrder.numUsed;
}

/*
 * Mark block bit on the per-Dalvik register vector to denote that Dalvik
 * register idx is defined in BasicBlock bb.
 */
static bool fillDefBlockMatrix(CompilationUnit *cUnit, BasicBlock *bb)
{
    if (bb->dataFlowInfo == NULL) return false;

    BitVectorIterator iterator;

    dvmBitVectorIteratorInit(bb->dataFlowInfo->defV, &iterator);
    while (true) {
        int idx = dvmBitVectorIteratorNext(&iterator);
        if (idx == -1) break;
        /* Block bb defines register idx */
        dvmCompilerSetBit(cUnit->defBlockMatrix[idx], bb->id);
    }
    return true;
}

static void computeDefBlockMatrix(CompilationUnit *cUnit)
{
    //At this point we need to determine if we need to allocate the defBlockMatrix.
    //Since the size of it is always set to numRegisters + 1, in our comparison we subtract one
    //in order to determine if it actually needs resized.
    if (cUnit->defBlockMatrixSize - 1 < cUnit->numDalvikRegisters)
    {
        cUnit->defBlockMatrixSize = cUnit->numDalvikRegisters + 1;
        cUnit->defBlockMatrix = static_cast<BitVector **> (dvmCompilerNew(sizeof(BitVector *) * cUnit->defBlockMatrixSize, true));
    }

    /* Initialize numRegister vectors with numBlocks bits each */
    for (int i = 0; i < cUnit->defBlockMatrixSize; i++) {
        cUnit->defBlockMatrix[i] = dvmCompilerAllocBitVector(cUnit->numBlocks,
                                                             false);
    }

    dvmCompilerDataFlowAnalysisDispatcher(cUnit, dvmCompilerFindLocalLiveIn,
                                          kAllNodes,
                                          false /* isIterative */);

    dvmCompilerDataFlowAnalysisDispatcher (cUnit, dvmCompilerInitializeExitUses,
                                          kAllNodes,
                                          false /* isIterative */);

    dvmCompilerDataFlowAnalysisDispatcher(cUnit, fillDefBlockMatrix,
                                          kAllNodes,
                                          false /* isIterative */);

    if (cUnit->jitMode == kJitMethod) {
        /*
         * Also set the incoming parameters as defs in the entry block.
         * Only need to handle the parameters for the outer method.
         */
        int inReg = cUnit->method->registersSize - cUnit->method->insSize;
        for (; inReg < cUnit->method->registersSize; inReg++) {
            dvmCompilerSetBit(cUnit->defBlockMatrix[inReg],
                              cUnit->entryBlock->id);
        }
    }
}

/* Compute the post-order traversal of the CFG */
static void computeDomPostOrderTraversal(CompilationUnit *cUnit, BasicBlock *bb)
{
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bb->iDominated, &bvIterator);
    GrowableList *blockList = &cUnit->blockList;

    /* Iterate through the dominated blocks first */
    while (true) {
        int bbIdx = dvmBitVectorIteratorNext(&bvIterator);
        if (bbIdx == -1) break;
        BasicBlock *dominatedBB =
            (BasicBlock *) dvmGrowableListGetElement(blockList, bbIdx);
        computeDomPostOrderTraversal(cUnit, dominatedBB);
    }

    /* Enter the current block id */
    dvmInsertGrowableList(&cUnit->domPostOrderTraversal, bb->id);

    /* hacky loop detection */
    if (bb->taken && dvmIsBitSet(bb->dominators, bb->taken->id)) {
        cUnit->hasLoop = true;
    }
}

static void checkForDominanceFrontier(BasicBlock *domBB,
                                      const BasicBlock *succBB)
{
    /*
     * TODO - evaluate whether phi will ever need to be inserted into exit
     * blocks.
     */
    if (succBB->iDom != domBB &&
        succBB->blockType == kDalvikByteCode &&
        succBB->hidden == false) {
        dvmSetBit(domBB->domFrontier, succBB->id);
    }
}

/* Worker function to compute the dominance frontier */
static bool computeDominanceFrontier(CompilationUnit *cUnit, BasicBlock *bb)
{
    GrowableList *blockList = &cUnit->blockList;

    //Create iterator for visiting children
    ChildBlockIterator childIter (bb);

    //Now iterate through the children to check for dominance
    for (BasicBlock **childPtr = childIter.getNextChildPtr (); childPtr != 0; childPtr = childIter.getNextChildPtr ())
    {
        BasicBlock *child = *childPtr;

        assert (child != 0);

        checkForDominanceFrontier (bb, child);
    }

    /* Calculate DF_up */
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bb->iDominated, &bvIterator);
    while (true) {
        int dominatedIdx = dvmBitVectorIteratorNext(&bvIterator);
        if (dominatedIdx == -1) break;
        BasicBlock *dominatedBB = (BasicBlock *)
            dvmGrowableListGetElement(blockList, dominatedIdx);
        BitVectorIterator dfIterator;
        dvmBitVectorIteratorInit(dominatedBB->domFrontier, &dfIterator);
        while (true) {
            int dfUpIdx = dvmBitVectorIteratorNext(&dfIterator);
            if (dfUpIdx == -1) break;
            BasicBlock *dfUpBlock = (BasicBlock *)
                dvmGrowableListGetElement(blockList, dfUpIdx);
            checkForDominanceFrontier(bb, dfUpBlock);
        }
    }

    return true;
}

/* Worker function for initializing domination-related data structures */
static bool initializeDominationInfo(CompilationUnit *cUnit, BasicBlock *bb)
{
    int numTotalBlocks = cUnit->blockList.numUsed;

    //In case numTotalBlocks is 0, put it to 1
    if (numTotalBlocks == 0)
    {
        numTotalBlocks = 1;
    }

    if (bb->dominators == NULL ) {
        bb->dominators = dvmCompilerAllocBitVector(numTotalBlocks,
                                                   true /* expandable */);
        bb->iDominated = dvmCompilerAllocBitVector(numTotalBlocks,
                                                   true /* expandable */);
        bb->domFrontier = dvmCompilerAllocBitVector(numTotalBlocks,
                                                   true /* expandable */);
    } else {
        dvmEnsureSizeAndClear (bb->dominators, numTotalBlocks);
        dvmEnsureSizeAndClear (bb->iDominated, numTotalBlocks);
        dvmEnsureSizeAndClear (bb->domFrontier, numTotalBlocks);
    }
    /* Set all bits in the dominator vector */
    dvmSetInitialBits(bb->dominators, numTotalBlocks);

    return true;
}

/* Worker function to compute each block's dominators */
static bool computeBlockDominators(CompilationUnit *cUnit, BasicBlock *bb)
{
    GrowableList *blockList = &cUnit->blockList;
    int numTotalBlocks = blockList->numUsed;
    BitVector *tempBlockV = cUnit->tempBlockV;
    BitVectorIterator bvIterator;

    /*
     * The dominator of the entry block has been preset to itself and we need
     * to skip the calculation here.
     */
    if (bb == cUnit->entryBlock) return false;

    dvmSetInitialBits(tempBlockV, numTotalBlocks);

    /* Iterate through the predecessors */
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    while (true) {
        int predIdx = dvmBitVectorIteratorNext(&bvIterator);
        if (predIdx == -1) break;
        BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement(
                                 blockList, predIdx);

        //If we don't have dominator information, skip it
        if (predBB->dominators == 0)
        {
            continue;
        }
        /* tempBlockV = tempBlockV ^ dominators */
        dvmIntersectBitVectors(tempBlockV, tempBlockV, predBB->dominators);
    }
    dvmSetBit(tempBlockV, bb->id);
    if (dvmCompareBitVectors(tempBlockV, bb->dominators)) {
        dvmCopyBitVector(bb->dominators, tempBlockV);
        return true;
    }
    return false;
}

/* Worker function to compute the idom */
static bool computeImmediateDominator(CompilationUnit *cUnit, BasicBlock *bb)
{
    GrowableList *blockList = &cUnit->blockList;
    BitVector *tempBlockV = cUnit->tempBlockV;
    BitVectorIterator bvIterator;
    BasicBlock *iDom;

    if (bb == cUnit->entryBlock) return false;

    dvmCopyBitVector(tempBlockV, bb->dominators);
    dvmClearBit(tempBlockV, bb->id);
    dvmBitVectorIteratorInit(tempBlockV, &bvIterator);

    /* Should not see any dead block */
    assert(dvmCountSetBits(tempBlockV) != 0);
    if (dvmCountSetBits(tempBlockV) == 1) {
        iDom = (BasicBlock *) dvmGrowableListGetElement(
                       blockList, dvmBitVectorIteratorNext(&bvIterator));
        bb->iDom = iDom;
    } else {
        int iDomIdx = dvmBitVectorIteratorNext(&bvIterator);
        assert(iDomIdx != -1);
        while (true) {
            int nextDom = dvmBitVectorIteratorNext(&bvIterator);
            if (nextDom == -1) break;
            BasicBlock *nextDomBB = (BasicBlock *)
                dvmGrowableListGetElement(blockList, nextDom);

            //If we don't have dominator information, skip it
            if (nextDomBB->dominators == 0)
            {
                continue;
            }
            /* iDom dominates nextDom - set new iDom */
            if (dvmIsBitSet(nextDomBB->dominators, iDomIdx)) {
                iDomIdx = nextDom;
            }

        }
        iDom = (BasicBlock *) dvmGrowableListGetElement(blockList, iDomIdx);
        /* Set the immediate dominator block for bb */
        bb->iDom = iDom;
    }
    /* Add bb to the iDominated set of the immediate dominator block */
    dvmCompilerSetBit(iDom->iDominated, bb->id);
    return true;
}

/* Compute dominators, immediate dominator, and dominance fronter */
static void computeDominators(CompilationUnit *cUnit)
{
    int numReachableBlocks = cUnit->numReachableBlocks;
    int numTotalBlocks = cUnit->blockList.numUsed;

    /* Initialize domination-related data structures */
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, initializeDominationInfo,
                                          kReachableNodes,
                                          false /* isIterative */);

    /* Set the dominator for the root node */
    dvmClearAllBits(cUnit->entryBlock->dominators);
    dvmSetBit(cUnit->entryBlock->dominators, cUnit->entryBlock->id);

    if (cUnit->tempBlockV == NULL) {
        cUnit->tempBlockV = dvmCompilerAllocBitVector(numTotalBlocks,
                                                  true /* expandable */);
    } else {
        dvmEnsureSizeAndClear (cUnit->tempBlockV, numTotalBlocks);
    }
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, computeBlockDominators,
                                          kPreOrderDFSTraversal,
                                          true /* isIterative */);

    cUnit->entryBlock->iDom = NULL;
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, computeImmediateDominator,
                                          kReachableNodes,
                                          false /* isIterative */);

    /*
     * Now go ahead and compute the post order traversal based on the
     * iDominated sets.
     */
    if (cUnit->domPostOrderTraversal.elemList == NULL) {
        dvmInitGrowableList(&cUnit->domPostOrderTraversal, numReachableBlocks);
    } else {
        cUnit->domPostOrderTraversal.numUsed = 0;
    }

    computeDomPostOrderTraversal(cUnit, cUnit->entryBlock);
    assert(cUnit->domPostOrderTraversal.numUsed ==
           (unsigned) cUnit->numReachableBlocks);

    /* Now compute the dominance frontier for each block */
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, computeDominanceFrontier,
                                          kPostOrderDOMTraversal,
                                          false /* isIterative */);
}

/*
 * Perform dest U= src1 ^ ~src2
 * This is probably not general enough to be placed in BitVector.[ch].
 */
static void computeSuccLiveIn(BitVector *dest,
                              const BitVector *src1,
                              const BitVector *src2)
{
    if (dest->storageSize != src1->storageSize ||
        dest->storageSize != src2->storageSize ||
        dest->expandable != src1->expandable ||
        dest->expandable != src2->expandable) {
        ALOGE("Incompatible set properties");
        dvmAbort();
    }

    unsigned int idx;
    for (idx = 0; idx < dest->storageSize; idx++) {
        dest->storage[idx] |= src1->storage[idx] & ~src2->storage[idx];
    }
}

/*
 * Iterate through all successor blocks and propagate up the live-in sets.
 * The calculated result is used for phi-node pruning - where we only need to
 * insert a phi node if the variable is live-in to the block.
 */
static bool computeBlockLiveIns(CompilationUnit *cUnit, BasicBlock *bb)
{
    //Paranoid
    if (bb->dataFlowInfo == NULL)
    {
        return false;
    }

    //Suppose no change
    bool change = false;

    //First handle LiveIns
    BitVector *tempDalvikRegisterV = cUnit->tempDalvikRegisterV;

    //Copy the current one for reference
    dvmCopyBitVector(tempDalvikRegisterV, bb->dataFlowInfo->useV);

    //Now update it: in = use U (out - defs)
    computeSuccLiveIn (tempDalvikRegisterV, bb->dataFlowInfo->liveOutV, bb->dataFlowInfo->defV);

    //Now we have the new live in, compare it
    if (dvmCompareBitVectors(tempDalvikRegisterV, bb->dataFlowInfo->liveInV) == true) {
        //Copy it in
        dvmCopyBitVector(bb->dataFlowInfo->liveInV, tempDalvikRegisterV);
        //We have changed something
        change = true;
    }

    //Now we can handle the outs: new out = U ins for each successor of bb
    //Clear the temp
    dvmClearAllBits (tempDalvikRegisterV);

    //Create iterator for visiting children
    ChildBlockIterator childIter (bb);

    //Now iterate through the children to include their live-ins
    for (BasicBlock **childPtr = childIter.getNextChildPtr (); childPtr != 0; childPtr = childIter.getNextChildPtr ())
    {
        BasicBlock *child = *childPtr;

        assert (child != 0);

        if (child->dataFlowInfo != 0)
        {
            dvmUnifyBitVectors (tempDalvikRegisterV, tempDalvikRegisterV, child->dataFlowInfo->liveInV);
        }
    }

    //Check for a difference
    if (dvmCompareBitVectors(tempDalvikRegisterV, bb->dataFlowInfo->liveOutV) == true) {
        //Copy it in
        dvmCopyBitVector(bb->dataFlowInfo->liveOutV, tempDalvikRegisterV);
        //We have changed something
        change = true;
    }

    return change;
}

/* Insert phi nodes to for each variable to the dominance frontiers */
static void insertPhiNodes(CompilationUnit *cUnit)
{
    int dalvikReg;
    const GrowableList *blockList = &cUnit->blockList;

    //Do we need to allocate the bit vectors again ?
    if (cUnit->numBlocks > cUnit->phi.size)
    {
        //Have we ever allocated them?
        if (cUnit->phi.phiBlocks != 0)
        {
            dvmEnsureSizeAndClear (cUnit->phi.phiBlocks, cUnit->numBlocks);
            dvmEnsureSizeAndClear (cUnit->phi.tmpBlocks, cUnit->numBlocks);
            dvmEnsureSizeAndClear (cUnit->phi.inputBlocks, cUnit->numBlocks);
        }
        else
        {
            cUnit->phi.phiBlocks = dvmCompilerAllocBitVector(cUnit->numBlocks, true);
            cUnit->phi.tmpBlocks = dvmCompilerAllocBitVector(cUnit->numBlocks, true);
            cUnit->phi.inputBlocks = dvmCompilerAllocBitVector(cUnit->numBlocks, true);
        }
        cUnit->phi.size = cUnit->numBlocks;
    }

    //Get local versions of the bitvectors
    BitVector *phiBlocks = cUnit->phi.phiBlocks;
    BitVector *tmpBlocks = cUnit->phi.tmpBlocks;
    BitVector *inputBlocks = cUnit->phi.inputBlocks;

    if (cUnit->tempDalvikRegisterV == 0)
    {
        cUnit->tempDalvikRegisterV = dvmCompilerAllocBitVector (cUnit->numDalvikRegisters, true);
    }
    else
    {
        dvmEnsureSizeAndClear (cUnit->tempDalvikRegisterV, cUnit->numDalvikRegisters);
    }

    dvmCompilerDataFlowAnalysisDispatcher(cUnit, computeBlockLiveIns,
                                          kPostOrderDFSTraversal,
                                          true /* isIterative */);

    /* Iterate through each Dalvik register */
    for (dalvikReg = 0; dalvikReg < cUnit->numDalvikRegisters; dalvikReg++) {
        bool change;
        BitVectorIterator iterator;

        dvmCopyBitVector(inputBlocks, cUnit->defBlockMatrix[dalvikReg]);
        dvmClearAllBits(phiBlocks);

        /* Calculate the phi blocks for each Dalvik register */
        do {
            change = false;

            dvmClearAllBits(tmpBlocks);
            dvmBitVectorIteratorInit(inputBlocks, &iterator);

            while (true) {
                int idx = dvmBitVectorIteratorNext(&iterator);
                if (idx == -1) break;
                BasicBlock *defBB =
                    (BasicBlock *) dvmGrowableListGetElement(blockList, idx);

                /* Merge the dominance frontier to tmpBlocks */
                dvmUnifyBitVectors(tmpBlocks, tmpBlocks, defBB->domFrontier);
            }

            if (dvmCompareBitVectors(phiBlocks, tmpBlocks)) {
                change = true;
                dvmCopyBitVector(phiBlocks, tmpBlocks);

                /*
                 * Iterate through the original blocks plus the new ones in
                 * the dominance frontier.
                 */
                dvmCopyBitVector(inputBlocks, phiBlocks);
                dvmUnifyBitVectors(inputBlocks, inputBlocks,
                                   cUnit->defBlockMatrix[dalvikReg]);
            }
        } while (change);

        /*
         * Insert a phi node for dalvikReg in the phiBlocks if the Dalvik
         * register is in the live-in set.
         */
        dvmBitVectorIteratorInit(phiBlocks, &iterator);
        while (true) {
            int idx = dvmBitVectorIteratorNext(&iterator);
            if (idx == -1) break;
            BasicBlock *phiBB =
                (BasicBlock *) dvmGrowableListGetElement(blockList, idx);
            /* Variable will be clobbered before being used - no need for phi */
            if (!dvmIsBitSet(phiBB->dataFlowInfo->liveInV, dalvikReg)) continue;
            MIR *phi = dvmCompilerNewMIR ();
            phi->dalvikInsn.opcode = (Opcode)kMirOpPhi;
            phi->dalvikInsn.vA = dalvikReg;
            phi->offset = phiBB->startOffset;
            dvmCompilerPrependMIR(phiBB, phi);
        }
    }
}

/**
 * @brief Walk up the predecessors to get to a kDalvikByteCode block
 * @param blockList the GrowableList representing the block list
 * @param bb the BasicBlock we want to walk upwards from
 * @return the first kDalvikByteCode basicblock ancestor of bb, 0 otherwise and 0 if a given bb has more than one ancestor on the walk upwards
 */
static BasicBlock *walkUpPredecessorsToByteCode (GrowableList *blockList, BasicBlock *bb)
{
    //Get the predecessors
    BitVector *predecessors = bb->predecessors;

    //If predecessors is 0, we are done
    if (predecessors == 0)
    {
        return 0;
    }

    //If the BB has more than one predecessor we are done
    //  - This is actually an error because normally we started with the BWCC
    //  - Then, potentially it could have a single parent which is either the last loop's BB (the one we want)
    //      - Or before hitting that, we might have the pre-BWCC
    //  - But all should have only one parent...
    if (dvmCountSetBits (predecessors) != 1)
    {
        return 0;
    }

    //Get its index
    int idx = dvmHighestBitSet (predecessors);

    //Get the BB
    BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement (blockList, idx);

    //Paranoid
    if (predBB == 0)
    {
        return 0;
    }

    //Now technically this might not be a PBWCC type yet, it could be our last iteration BB
    if (predBB->blockType != kDalvikByteCode)
    {
        //Recursive call
        return walkUpPredecessorsToByteCode (blockList, predBB);
    }

    //Otherwise we are done
    return predBB;
}

/*
 * Worker function to insert phi-operands with latest SSA names from
 * predecessor blocks
 */
static bool insertPhiNodeOperands(CompilationUnit *cUnit, BasicBlock *bb)
{
    BitVector *ssaRegV = cUnit->tempSSARegisterV;
    BitVectorIterator bvIterator;
    GrowableList *blockList = &cUnit->blockList;

    //Walk the basic block
    MIR *mir = bb->firstMIRInsn;

    while (mir != 0)
    {
        //If it isn't a PHI node, we are done
        if (mir->dalvikInsn.opcode != (Opcode)kMirOpPhi)
        {
            return true;
        }

        int ssaReg = mir->ssaRep->defs[0];
        int encodedDalvikValue =
            (int) dvmGrowableListGetElement(cUnit->ssaToDalvikMap, ssaReg);
        int dalvikReg = DECODE_REG(encodedDalvikValue);

        //Clear the bitvector, this bitvector will hold all the SSA registers that should be in the PHI node's operands
        dvmClearAllBits(ssaRegV);

        /* Iterate through the predecessors */
        dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
        while (true) {
            int predIdx = dvmBitVectorIteratorNext(&bvIterator);
            if (predIdx == -1) break;
            BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement(
                                     blockList, predIdx);

            if (predBB->blockType == kChainingCellBackwardBranch)
            {
                //Ok there is an exception: if this is a BWCC, actually we want the BB from the loop leading to this
                predBB = walkUpPredecessorsToByteCode (blockList, predBB);
            }

            //Paranoid
            assert (predBB != 0);

            //If dalvikToSSAMapExit is not created than we did not traverse this bb in SSA conversion
            //So it cannot give us anything interesting then ingore it
            if (predBB->dataFlowInfo->dalvikToSSAMapExit == 0)
            {
                continue;
            }

            //Now get the SSA combination (SSA register, SSA Subscript) via the SSA map at the exit of the predecessor
            int encodedSSAValue =
                predBB->dataFlowInfo->dalvikToSSAMapExit[dalvikReg];

            //Get the SSA register itself
            int ssaReg = DECODE_REG(encodedSSAValue);

            //Add this SSA register to the bitvector
            dvmSetBit(ssaRegV, ssaReg);
        }

        //Ok, we know the PHI node has a def, what is it?
        int tmp = mir->ssaRep->defs[0];

        //In a particular case it is possible to have set the bit of the def here:
        //  - If ever we sunk something, the whole SSA algorithm believes there to be a PHI node for that VR (which is correct if you follow the CFG)
        //  - Problem is that it is wrong: the PBWCC, where that sunk code lies actually should not be an operand, from PBWCC we go back to the interpreter not the loop head...
        //  - Thus the walkUpPredecessorsToByteCode gives us the SSA number at the exit of the loop
        //  - However, if the SSA number has not changed in the loop, this whole PHI node should never have existed
        //     -> All of this is a huge TODO: getting an actual CFG that really shows what is happening and not pretending certain things
        dvmClearBit (ssaRegV, tmp);

        /* Count the number of SSA registers for a Dalvik register */
        int numUses = dvmCountSetBits(ssaRegV);

        //Only need to allocate if we don't have enough in size
        if (mir->ssaRep->numUses < numUses)
        {
            mir->ssaRep->uses = (int *) dvmCompilerNew(sizeof(int) * numUses, false);
            mir->ssaRep->fpUse = (bool *) dvmCompilerNew(sizeof(bool) * numUses, true);
            mir->ssaRep->defWhere = static_cast<MIR **> (dvmCompilerNew (sizeof (* (mir->ssaRep->defWhere)) * numUses, true));
        }

        //Set size
        mir->ssaRep->numUses = numUses;

        BitVectorIterator phiIterator;

        dvmBitVectorIteratorInit(ssaRegV, &phiIterator);
        int *usePtr = mir->ssaRep->uses;

        /* Set the uses array for the phi node */
        while (true) {
            int ssaRegIdx = dvmBitVectorIteratorNext(&phiIterator);
            if (ssaRegIdx == -1) break;
            *usePtr++ = ssaRegIdx;
        }

        //If ever this MIR only has now one numUses due to the previous
        //statement, than it isn't even a PHI node.
        //Remove it now, but remember the SSA number of its one operand so that
        //we can fix up any references to this PHI.
        if (numUses == 1)
        {
            //First get the next one
            MIR *old = mir;
            mir = mir->next;
            (*cUnit->degeneratePhiMap)[old->ssaRep->defs[0]] = old->ssaRep->uses[0];
            dvmCompilerRemoveMIR (old);

            //On with the next MIR
            continue;
        }

        //Next MIR
        mir = mir->next;
    }

    return true;
}

/**
 * @brief Update any references to degenerate PHIs within a basic block
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return unconditionally return true indicating that we might have changed
 * bb. This function is only used in a non-iterative data flow, so the return
 * value is really a "don't care".
 */
static bool fixDegeneratePhiUses(CompilationUnit *cUnit, BasicBlock *bb)
{
    // Walk the basic block
    MIR *mir = bb->firstMIRInsn;

    while (mir != 0)
    {
        // Foreach use SSANum
        for (int i = 0; i < mir->ssaRep->numUses; i++)
        {
            int useSSANum = mir->ssaRep->uses[i];
            std::map<int, int>::iterator it;

            // Was the use SSANum defined by a deleted degenerate PHI?
            it = cUnit->degeneratePhiMap->find (useSSANum);
            if (it != cUnit->degeneratePhiMap->end ())
            {
                // If so, reference the degenerate PHI's operand instead.
                mir->ssaRep->uses[i] = it->second;
            }
        }
        mir = mir->next;
    }

    return true;
}

/* Perform SSA transformation for the whole method */
void dvmCompilerMethodSSATransformation(CompilationUnit *cUnit)
{
    /* Compute the DFS order */
    computeDFSOrder(cUnit);

    /* Compute the dominator info */
    computeDominators(cUnit);

    /* Allocate data structures in preparation for SSA conversion */
    dvmInitializeSSAConversion(cUnit);

    /* Find out the "Dalvik reg def x block" relation */
    computeDefBlockMatrix(cUnit);

    /* Insert phi nodes to dominance frontiers for all variables */
    insertPhiNodes(cUnit);

    //Clear the nodes
    dvmCompilerDataFlowAnalysisDispatcher (cUnit,
            dvmCompilerClearVisitedFlag, kAllNodes, false);

    /* Rename register names by local defs and phi nodes */
    dvmCompilerDoSSAConversion (cUnit, cUnit->entryBlock);

    /*
     * Shared temp bit vector used by each block to count the number of defs
     * from all the predecessor blocks.
     */
    cUnit->tempSSARegisterV = dvmCompilerAllocBitVector(cUnit->numSSARegs,
                                                        false);

    /* Insert phi-operands with latest SSA names from predecessor blocks */
    cUnit->degeneratePhiMap->clear ();
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, insertPhiNodeOperands,
                                          kReachableNodes,
                                          false /* isIterative */);

    if (cUnit->degeneratePhiMap->empty () == false) {
        dvmCompilerDataFlowAnalysisDispatcher(cUnit, fixDegeneratePhiUses,
                                              kReachableNodes,
                                              false /* isIterative */);
    }
}

/**
 * @brief Build the domination information
 * @param cUnit the CompilationUnit
 */
void dvmCompilerBuildDomination (CompilationUnit *cUnit)
{
    /* Compute the DFS order */
    computeDFSOrder (cUnit);

    /* Compute the dominator info */
    computeDominators (cUnit);
}

/**
 * @brief Build the def use chains
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns whether it changed the BasicBlock bb
 */
bool dvmCompilerBuildDefUseChain (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Suppose we did not change anything
    bool res = false;

    //Get walker data
    SSAWalkData *data = static_cast<SSAWalkData *> (cUnit->walkData);

    //By default the order is the min accepted
    unsigned int currentOrder = 0;

    //Get minimum topological order from predecessors
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); predBB != 0;
                     predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList)) {

        //If the pred was not handled yet (backward?) then skip it
        if (predBB->visited == false)
        {
            continue;
        }

        //Get order from the entrance of the BB
        unsigned int order = predBB->topologicalOrder;

        //Get the last instruction's order
        MIR *lastInsn = predBB->lastMIRInsn;

        //If no instruction, just use the BB order, so skip this
        if (lastInsn != 0)
        {
            //Get order
            order = lastInsn->topologicalOrder;
        }

        //Compare to actual minimum
        if (currentOrder < order)
        {
            currentOrder = order;
        }
    }

    if (bb->topologicalOrder != currentOrder)
    {
        //Set the basic block's order now
        bb->topologicalOrder = currentOrder;
        //A change occured
        res = true;
    }

    //We now have the minimum topological order: go through the instructions
    for (MIR *insn = bb->firstMIRInsn; insn != 0; insn = insn->next)
    {
        //Augment the current topological order and then set
        currentOrder++;
        insn->topologicalOrder = currentOrder;

        //Now handle use and def chains
        SSARepresentation *ssaRep = insn->ssaRep;

        //If we don't have a ssaRep, there is nothing we can do here
        if (ssaRep != 0)
        {
            //First add to the use chains
            int nbr = ssaRep->numUses;
            for (int i = 0; i < nbr; i++)
            {
                //Get use value
                int value = ssaRep->uses[i];

                //Set defWhere for this instruction
                MIR *defined = data->getDefinition (value);

                //In case there is no define yet, let's remember it to handle it afterwards
                if (defined == 0)
                {
                    data->addNoDefine (insn, i);
                }
                else
                {
                    data->addUseToDefChain (i, insn, defined);
                }
            }

            //Now handle defs
            nbr = ssaRep->numDefs;
            for (int i = 0; i < nbr; i++)
            {
                //Before anything clean up the usedNext
                ssaRep->usedNext[i] = 0;

                //Get def value
                int value = ssaRep->defs[i];

                //Register definition
                data->setDefinition (insn, value);
            }
        }

        //Something changed if we got here: at least one instruction was touched
        res = true;
    }

    return res;
}

/**
 * @brief Helper to remove all PHI nodes from BasicBlocks
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return whether we changed something to the BasicBlock
 */
static bool clearPHIInformationHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    bool res = false;

    //Go through each MIR
    MIR *mir = bb->firstMIRInsn;

    while (mir != 0)
    {
        int opcode = mir->dalvikInsn.opcode;

        if (opcode == kMirOpPhi)
        {
            //Going to change something
            res = true;

            //First thing is to detach it
            MIR *prev = mir->prev;
            MIR *next = mir->next;

            //Attach previous to next if it exists
            if (prev != 0)
            {
                prev->next = next;
            }

            //Same for next
            if (next != 0)
            {
                next->prev = prev;
            }

            //Instruction is now removed but we must handle first and last for the basic block
            if (mir == bb->firstMIRInsn)
            {
                bb->firstMIRInsn = next;
            }

            //Remove the instruction for last
            if (mir == bb->lastMIRInsn)
            {
                bb->lastMIRInsn = prev;
            }
        }

        //Go to the next instruction
        mir = mir->next;
    }

    return res;
}

/**
 * @brief Clear the PHI nodes
 * @param cUnit the CompilationUnit
 */
void clearPHIInformation (CompilationUnit *cUnit)
{
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, clearPHIInformationHelper, kAllNodes, false);
}

/**
 * @brief Calculate the BasicBlock information
 * @see Compiler.h
 */
bool dvmCompilerCalculateBasicBlockInformation (CompilationUnit *cUnit, bool filter, bool buildLoopInfo)
{
    //New blocks may have been inserted so the first thing we do is ensure that the cUnit's number of blocks
    //matches the actual count of basic blocks.
    cUnit->numBlocks = dvmGrowableListSize (&(cUnit->blockList));

    //We might have used scratch registers which we need to commit now to include them in
    //total count of dalvik registers
    dvmCompilerCommitPendingScratch (cUnit);

    //Clear SSA information
    clearPHIInformation (cUnit);

    //Calculate Predecessors
    dvmCompilerCalculatePredecessors (cUnit);

    //Several algorithms for calculating basic block information assume that
    //the compilation unit contains correct DFS ordering.
    computeDFSOrder (cUnit);

    // Hide all unreachable blocks
    dvmCompilerRemoveUnreachableBlocks (cUnit);

    //If we want to filter the loop
    if (filter == true)
    {
        //In order to filter, we need to build domination first.
        //After we filter, we need to re-compute it
        computeDominators (cUnit);

        /* Loop structure not recognized/supported - return false */
        if (dvmCompilerFilterLoopBlocks (cUnit) == false)
        {
            return false;
        }
    }

    /* Recompute the DFS order and the domination information */
    dvmCompilerBuildDomination (cUnit);

    /* Allocate data structures in preparation for SSA conversion */
    dvmInitializeSSAConversion(cUnit);

    /* Find out the "Dalvik reg def x block" relation */
    computeDefBlockMatrix(cUnit);

    /* Insert phi nodes to dominance frontiers for all variables */
    insertPhiNodes(cUnit);

    //Clear the nodes
    dvmCompilerDataFlowAnalysisDispatcher (cUnit,
            dvmCompilerClearVisitedFlag, kAllNodes, false);

    /* Rename register names by local defs and phi nodes */
    dvmCompilerDoSSAConversion (cUnit, cUnit->entryBlock);

    /*
     * Shared temp bit vector used by each block to count the number of defs
     * from all the predecessor blocks.
     */
    if (cUnit->tempSSARegisterV == 0)
    {
        cUnit->tempSSARegisterV = dvmCompilerAllocBitVector (cUnit->numSSARegs, true);
    }
    else
    {
        dvmEnsureSizeAndClear (cUnit->tempSSARegisterV, cUnit->numSSARegs);
    }

    /* Insert phi-operands with latest SSA names from predecessor blocks */
    cUnit->degeneratePhiMap->clear ();
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, insertPhiNodeOperands,
                                          kReachableNodes,
                                          false /* isIterative */);
    if (cUnit->degeneratePhiMap->empty () == false) {
        dvmCompilerDataFlowAnalysisDispatcher(cUnit, fixDegeneratePhiUses,
                                              kReachableNodes,
                                              false /* isIterative */);
    }

    //Set walk data: create the data on the stack, will get destroyed automatically at the end of the function
    SSAWalkData data (cUnit);
    void *walkData = static_cast<void *> (&data);

    //Once this is done, we fill in the def/use chain and topological order for the MIRs
    //We suppose here that SSA has been done already
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, dvmCompilerBuildDefUseChain, kPredecessorsFirstTraversal, false, walkData);

    //Before anything else: any value without a definition might have one now, handle them now
    data.handleNoDefinitions ();

#ifdef ARCH_IA32
    //Call loop information fill if needed
    if (buildLoopInfo == true)
    {
        cUnit->loopInformation = LoopInformation::getLoopInformation (cUnit, cUnit->loopInformation);
    }
#endif

    //Clear the constants
    cUnit->constantValues->clear ();
    dvmClearAllBits (cUnit->isConstantV);

    //Finally, get the constant information and set the cUnit correctly
    dvmCompilerDataFlowAnalysisDispatcher(cUnit,
            dvmCompilerDoConstantPropagation,
            kAllNodes,
            false /* isIterative */);

    if (cUnit->loopInformation != 0)
    {
        //Finally, find the induction variables
        dvmCompilerFindInductionVariables (cUnit, cUnit->loopInformation);

        //Do memory aliasing
        dvmCompilerMemoryAliasing (cUnit);

        //Do Local Value Numbering
        dvmCompilerLocalValueNumbering (cUnit);

        //Do invariant detection in a loop
        dvmCompilerVariant (cUnit);
    }

    return true;
}
