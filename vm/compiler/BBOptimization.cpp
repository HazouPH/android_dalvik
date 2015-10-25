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

#include "BBOptimization.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "Loop.h"
#include "LoopInformation.h"
#include "PassDriver.h"
#include "Utility.h"
#include <algorithm>

/**
 * @brief Helper function to dvmCompilerMergeBasicBlocks
 *        to check whether a BB a pre-header/Backward/Exit of any loop
 * @param info loop information to check
 * @param data BB to check.
 * @return false if BB is a loop helper
 */
static bool isBBnotLoopHelper (LoopInformation *info, void *data)
{
    return info->isBasicBlockALoopHelper (static_cast<BasicBlock*>(data)) == false;
}

bool dvmCompilerMergeBasicBlocks (CompilationUnit *cUnit, BasicBlock *bb)
{
    //We only merge blocks if they are kDalvikByteCode and not hidden
    if (bb->blockType != kDalvikByteCode ||
        bb->hidden == true)
    {
        return false;
    }

    //We cannot merge blocks that have multiple targets for switch or exception
    if (bb->successorBlockList.blockListType != kNotUsed)
    {
        return false;
    }

    //We also refuse to merge if we have a taken and a fallthrough or neither
    if ( (bb->taken != 0 && bb->fallThrough != 0) ||
         (bb->taken == 0 && bb->fallThrough == 0))
    {
        return false;
    }

    //Now we actually have either a taken or a fallthrough, check if that child only has one parent
    BasicBlock *child = (bb->taken != 0) ? bb->taken : bb->fallThrough;

    //If child is hidden, we bail
    if (child->hidden == true)
    {
        //No change to the BasicBlock
        return false;
    }

    //Child must be dalvik code
    if (child->blockType != kDalvikByteCode)
    {
        return false;
    }

    unsigned int bitsSet = dvmCountSetBits (child->predecessors);

    //If not only one parent, we must bail
    if (bitsSet != 1)
    {
        //No change to the BasicBlock
        return false;
    }

    // If child or bb is a loop formation helper BB we must not merge them even if any of them are empty!
    if (cUnit->loopInformation != 0)
    {
        bool childIsNotLoopHelper = cUnit->loopInformation->iterate (isBBnotLoopHelper, child);
        if (childIsNotLoopHelper == false)
        {
            //No change to the BasicBlock
            return false;
        }
        bool bbIsNotLoopHelper = cUnit->loopInformation->iterate (isBBnotLoopHelper, bb);
        if (bbIsNotLoopHelper == false)
        {
            //No change to the BasicBlock
            return false;
        }
    }

    //We allow merge if one of the blocks has no instructions or if both have no instructions.
    //In case when both have instructions, we need to check further if we can do the merge.
    if (bb->lastMIRInsn != 0 && child->firstMIRInsn != 0)
    {
        MIR *lastInsn = bb->lastMIRInsn;

        //Get the opcode
        int opcode = lastInsn->dalvikInsn.opcode;

        //Get the opcode flags
        int flags = dvmCompilerGetOpcodeFlags (opcode);

        //Is it an unconditional jump?
        bool isUnconditionalJump = (flags == kInstrCanBranch);

        //We also check if instruction can continue but cannot do anything else, with one exception...
        //If instruction can throw we have two possible paths, one fallthrough for continue and one taken
        //for exception. But when we get here we know that the basic block doesn't have two branches.
        //This means that we have implicit jump to exception which backend will automatically take care of.
        //Thus we can still merge blocks if the instruction can continue and throw but nothing else.
        bool continues = (flags == (kInstrCanContinue | kInstrCanThrow)) || (flags == kInstrCanContinue);

        //We can do the merge if we have an unconditional jump or we have a continue.
        //Thus for rejection we check the negated statement.
        if (isUnconditionalJump == false && continues == false)
        {
            return false;
        }

        if (isUnconditionalJump == true)
        {
            //We have an unconditional jump but we can remove it since we are merging blocks
            dvmCompilerRemoveMIR (lastInsn);
        }
    }

    //If we have an instruction, take its offset
    if (bb->firstMIRInsn != 0)
    {
        child->startOffset = bb->startOffset;
    }

    //Move the MIRs from this block to the beginning of child
    dvmCompilerMoveLinkedMIRsBefore (child, 0, bb->firstMIRInsn);

    //Now we should remove our BB from all predecessors
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); predBB != 0;
                     predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        dvmCompilerReplaceChildBasicBlock (child, predBB, bb);
    }

    //Merge any spill request between what the father has and the child
    dvmUnifyBitVectors (child->requestWriteBack, child->requestWriteBack, bb->requestWriteBack);

    //Now remove the child from the cUnit
    dvmCompilerHideBasicBlock (cUnit->blockList, bb);

    //Merge completed but no sense to re-iterate because our removing does not give new opportunities
    return false;
}

/**
 * @brief Insert the pre-loop header
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation for the loop
 * @param entry the loop entry
 */
static void insertPreLoopHeader (CompilationUnit *cUnit, LoopInformation *info, BasicBlock *entry)
{
    BasicBlock *preHeader = dvmCompilerNewBBinCunit(cUnit, kDalvikByteCode);
    preHeader->startOffset = entry->startOffset;

    assert (entry->predecessors);

    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(entry->predecessors, &bvIterator);
    for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); predBB != 0;
                     predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        // If the type is a backward chaining cell or inserted pre-header, go to the next one
        if ((predBB->blockType != kChainingCellBackwardBranch) && (predBB != preHeader))
        {
            //We do not update entry predecessors because we are iterating through them
            //Namely this means that we defer setting the pre-header as predecessor of entry until end of loop
            const bool updateEntryPredecessors = false;

            dvmCompilerInsertBasicBlockBetween (preHeader, predBB, entry, updateEntryPredecessors);
        }
    }

    //Now we finished setting linking all entry predecessors to loop preheader. Thus, we finally just now
    //make the pre-header the entry's predecessor.
    dvmCompilerUpdatePredecessors (preHeader, preHeader->fallThrough, entry);
}

/**
 * Form a loop if bb is a loop head
 *  - If it is, make sure it is a bottom formed loop (or make it so), add a preloop block and an exit block
 */
void dvmCompilerFormOldLoop (CompilationUnit *cUnit, Pass *pass)
{
    //Put a backward chaining cell before every predecessor of the entry block
    BitVectorIterator bvIterator;

    //Get first BasicBlock
    BasicBlock *bb = cUnit->entryBlock->fallThrough;

    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    while (true) {
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

        //If done, bail
        if (blockIdx == -1)
        {
            break;
        }

        //Get predecessor
        BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, blockIdx);

        //Paranoid
        if (predBB == 0)
        {
            continue;
        }

        //We only care if it's a kDalvikByteCode
        if (predBB->blockType != kDalvikByteCode)
        {
            continue;
        }

        //Put a backward chaining cell between the predecessor and the entry block
        BasicBlock *backward = dvmCompilerNewBBinCunit (cUnit, kChainingCellBackwardBranch);
        backward->startOffset = bb->startOffset;

        //Backward will fall through to the current BasicBlock directly
        backward->fallThrough = bb;

        //Now link predBB
        if (predBB->taken == bb)
        {
            predBB->taken = backward;
        }
        else
        {
            //Paranoid
            assert (predBB->fallThrough == bb);

            predBB->fallThrough = backward;
        }
    }

    //In this case, we just put the
    //cUnit->quitLoopMode = true;
    (void) pass;
}

/**
 * @brief Helper to test if the loop has been formed properly
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param data required by interface (not used)
 * @return whether the loop is formed properly
 */
static bool testLoopHelper (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    //Standard message to append in case of error
    char message[256];
    snprintf (message, sizeof(message), "LOOP_INFO: loop for trace  %s%s, offset %02x at depth %d",
            cUnit->method->clazz->descriptor, cUnit->method->name,
            cUnit->traceDesc->trace[0].info.frag.startOffset, info->getDepth ());


    //Gather all the required information
    BitVector *backwardBlocks = const_cast<BitVector *>(info->getBackwardBranches());
    BitVector *exitLoopBlocks = const_cast<BitVector *>(info->getExitLoops());
    BitVector *allBlocks = const_cast<BitVector *>(info->getBasicBlocks());

    BasicBlock *preHeader = info->getPreHeader();
    BasicBlock *entry = info->getEntryBlock();

    //Loop should have at least one basic block:
    if (dvmCountSetBits (allBlocks) == 0)
    {
        ALOGE ("%s - Not even a single basic block in info", message);
        return false;
    }

    //Loop should have a pre-header
    if (preHeader == 0)
    {
        ALOGE ("%s - Loop has no pre-header", message);
        return false;
    }

    //Loop should have an entry block
    if (entry == 0)
    {
        ALOGE ("%s - Loop has no entry block", message);
        return false;
    }

    //Preheader should go to entry
    if (preHeader->fallThrough != entry)
    {
        ALOGE ("%s - PreHeader %d does not go to loop entry %d", message, preHeader->id, entry->id);
        return false;
    }

    //Now check all the basic blocks in the loop
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (allBlocks, &bvIterator);
    for (BasicBlock *loopBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); loopBB != 0;
                     loopBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        //Check if the block only connects to blocks in the loop, backward block, or loop exit block
        if ( (loopBB->taken != 0) &&
                (info->contains (loopBB->taken) == false) &&
                (dvmIsBitSet (exitLoopBlocks, loopBB->taken->id) == false) &&
                (dvmIsBitSet (backwardBlocks, loopBB->taken->id) == false) )
        {
            ALOGE ("%s - Basic block %d exits loop through taken %d", message, loopBB->id, loopBB->taken->id);
            return false;
        }

        if ( (loopBB->fallThrough != 0) &&
                (info->contains (loopBB->fallThrough) == false) &&
                (dvmIsBitSet (exitLoopBlocks, loopBB->fallThrough->id) == false) &&
                (dvmIsBitSet (backwardBlocks, loopBB->fallThrough->id) == false) )
        {
            ALOGE ("%s - Basic block %d exits loop through fallThrough %d", message, loopBB->id, loopBB->fallThrough->id);
            return false;
        }

    }

    //Check if all backward blocks go to the entry
    dvmBitVectorIteratorInit (const_cast < BitVector* > (backwardBlocks), &bvIterator);
    for (BasicBlock *backBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); backBB != 0;
                     backBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        if (backBB->fallThrough != entry)
        {
            ALOGE ("%s - Backward block %d does not go to entry %d", message, backBB->id, entry->id);
            return false;
        }
    }

    return true;
}

/**
 * @brief Test if the loop has been formed properly
 * @param cUnit the CompilationUnit
 * @param pass the current pass
 */
void dvmCompilerTestLoop (CompilationUnit *cUnit, Pass *pass)
{

    if (gDvmJit.testLoops == false)
    {
        return;
    }

    if (cUnit->loopInformation != 0)
    {
        cUnit->loopInformation = LoopInformation::getLoopInformation (cUnit, cUnit->loopInformation);
    }

    LoopInformation *info = cUnit->loopInformation;

    if (info == 0)
    {
        ALOGE ("LOOP_INFO: loop for trace  %s%s, offset %02x - info is null at testLoop",
            cUnit->method->clazz->descriptor, cUnit->method->name,
            cUnit->traceDesc->trace[0].info.frag.startOffset);
        cUnit->quitLoopMode = true;
    }
    else
    {
        if (info->iterate (cUnit, testLoopHelper) == false)
        {
            cUnit->quitLoopMode = true;
        }
    }
    (void) pass;
}

/**
 * @brief Check whether loop should be tranformed
 * @param notLoop outs of loop
 * @param entry loop entry
 * @return true if transormation is required
 */
static bool isTransformationRequired (const BitVector *notLoop, const BasicBlock *entry)
{
    // We do not want to transform complex top loop now
    // So we will work with top loop in this case
    if (entry->successorBlockList.blockListType != kNotUsed)
    {
        return false;
    }

    // Loop entry has a taken and it is not in our loop => we want to transform this top loop
    if ((entry->taken != 0) && (dvmIsBitSet (notLoop, entry->taken->id) == 1))
    {
        return true;
    }
    // Loop entry has a fallThrough and it is not in our loop => we want to transform this top loop
    if ((entry->fallThrough != 0) && (dvmIsBitSet (notLoop, entry->fallThrough->id) == 1))
    {
        return true;
    }
    // Loop entry does not lead to out of loop => so we consider this as bottom loop
    // Note in the future it might be interesting to transform the following loop
    //      BB1 (loop entry), BB2 (leads to out), BB3 (backward)
    // to bottom loop
    //      BB1, BB2 (leads to out), BB3 (new loop entry), BB1_copy, BB2_copy (new backward to BB4)
    // But it is too complex for now
    return false;
}

/**
 * @brief Attempt to transform top loop to bottom if needed
 * @param cUnit the CompilationUnit
 * @param info loop information for current loop
 * @return new loop entry
 */
static BasicBlock *handleTopLoop (CompilationUnit *cUnit, LoopInformation *info)
{
    BasicBlock *entry = info->getEntryBlock ();
    BitVector *bbInLoop = info->getBasicBlocks ();
    BitVector *tailblocks = info->getBackwardBranches ();
    BitVector *notLoop = info->getExitLoops ();

    BitVectorIterator bvIterator;

    // No outs => nothing to do
    if (dvmCountSetBits (notLoop) == 0)
    {
        return entry;
    }

    // entry is a tail block => we are not a top loop
    if (dvmIsBitSet (tailblocks, entry->id) == true)
    {
        return entry;
    }

    // entry does not link to out => we are not a top loop
    if(isTransformationRequired (notLoop, entry) == false)
    {
        return entry;
    }

    // All BB points to out => no need to transform because it would be infinite loop
    BitVector *tmp = dvmCompilerAllocBitVector (1, true);
    BitVector *tmp1 = dvmCompilerAllocBitVector (1, true);
    dvmClearAllBits (tmp);

    // Collect all out coming to all exit blocks
    dvmBitVectorIteratorInit(notLoop, &bvIterator);
    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        BitVector *swap = tmp;
        tmp = tmp1;
        tmp1 = swap;

        assert (bb->predecessors != 0);
        dvmUnifyBitVectors (tmp, tmp1, bb->predecessors);
    }

    // Find BB in our loop coming to all exit blocks
    dvmIntersectBitVectors (tmp1, tmp, bbInLoop);
    // All BB coming to out ?
    if (dvmCompareBitVectors (bbInLoop, tmp1) == false)
    {
        return entry;
    }

    // Let's transform top loop
    while (isTransformationRequired (notLoop, entry) == true)
    {
        BasicBlock *inLoop = (dvmIsBitSet (notLoop, entry->taken->id) == 1) ? entry->fallThrough : entry->taken;
        BasicBlock *notLoopBB = (dvmIsBitSet (notLoop, entry->taken->id) == 1) ? entry->taken : entry->fallThrough;
        assert (inLoop != 0);
        assert (notLoopBB != 0);

        // If inLoop is an entry of other loop we do not want to make it an entry of our loop
        // Instead of that we add empty basic block to be loop entry
        if (info->getLoopInformationByEntry (inLoop) != 0)
        {
            BasicBlock *empty = dvmCompilerNewBBinCunit (cUnit, kDalvikByteCode);
            dvmCompilerInsertBasicBlockBetween (empty, entry, inLoop);
            dvmSetBit (bbInLoop, empty->id);
            inLoop = empty;
        }

        // Copy entry to make it a tail block
        BasicBlock *newBB = dvmCompilerCopyBasicBlock (cUnit, entry);

        //Update the predecessor information
        dvmSetBit (notLoopBB->predecessors, newBB->id);
        dvmSetBit (inLoop->predecessors, newBB->id);

        // Now all tail blocks should be re-directed to new loop tail block (old loop entry)
        dvmBitVectorIteratorInit(tailblocks, &bvIterator);
        for (BasicBlock *tailblock = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);
                         tailblock != 0;
                         tailblock = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
        {
            // Attach backedge to newBB
            if (tailblock->taken == entry)
            {
                tailblock->taken = newBB;
            }
            if (tailblock->fallThrough == entry)
            {
                tailblock->fallThrough = newBB;
            }

            //Update the predecessor information
            dvmCompilerUpdatePredecessors (tailblock, entry, newBB);
        }

        // Old entry is not in a loop now, while new one it is
        dvmClearBit (bbInLoop, entry->id);
        dvmSetBit (bbInLoop, newBB->id);
        entry = inLoop;

        // Now we have only one new block tail
        dvmClearAllBits (tailblocks);
        dvmSetBit (tailblocks, newBB->id);
    }

    return entry;
}

bool dvmCompilerFormLoopWorker (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    BitVectorIterator bvIterator;
    BitVectorIterator bvIterator2;

    // First we'd like to ensure that some of back branches links to out.
    // It might require loop transformation.
    BasicBlock *entry = handleTopLoop (cUnit, info);

    // Update loop information with new entry
    info->setEntryBlock (entry);

    // For each tail block we should add a Backward Branch
    dvmBitVectorIteratorInit(info->getBackwardBranches (), &bvIterator);
    for (BasicBlock *tailBlock = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);
                     tailBlock != 0;
                     tailBlock = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        BasicBlock *backward = dvmCompilerNewBBinCunit(cUnit, kChainingCellBackwardBranch);
        // It is very important to set correct offset which will be used during unchaining
        // Backward offset corresponds to entry offset where we plan to jump because it is a
        // next instruction we will execute
        backward->startOffset = entry->startOffset;

        dvmCompilerInsertBasicBlockBetween (backward, tailBlock, entry);
    }

    // For each exit we should add an Exit BB
    BitVector *tmp = dvmCompilerAllocBitVector (1, true);
    BitVector *basicBlocks = info->getBasicBlocks ();

    dvmBitVectorIteratorInit(info->getExitLoops (), &bvIterator);
    for (BasicBlock *notLoop = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); notLoop != 0;
                     notLoop = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        dvmIntersectBitVectors (tmp, notLoop->predecessors, basicBlocks);

        // Add Exit BB for found specific exit
        dvmBitVectorIteratorInit(tmp, &bvIterator2);
        for (BasicBlock *out = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator2, cUnit->blockList); out != 0;
                         out = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator2, cUnit->blockList))
        {
            BasicBlock *exitBlock = dvmCompilerNewBBinCunit(cUnit, kDalvikByteCode);
            exitBlock->startOffset = out->startOffset;

            dvmCompilerInsertBasicBlockBetween (exitBlock, out, notLoop);
        }
    }

    // Finally add a pre-loop header
    insertPreLoopHeader (cUnit, info, entry);

    return true;
}

/**
 * @brief Form a loop
 * @details make sure it is a bottom formed
 * loop (or make it so), add a preloop block and an exit block
 * @param cUnit the CompilationUnit
 * @param pass the current pass
 * @return whether we changed anything (always false)
 */
void dvmCompilerFormLoop (CompilationUnit *cUnit, Pass *pass)
{
    if (cUnit->loopInformation != 0)
    {
        cUnit->loopInformation->iterate (cUnit, dvmCompilerFormLoopWorker);
    }
}

/**
 * @brief Reorder the BasicBlocks in a DFS order
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock currently considered
 */
static void reorderHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Has it been visited or is it 0?
    if (bb == 0 || dvmIsBitSet (cUnit->tempBlockV, bb->id) == true)
    {
        return;
    }

    //Is it hidden?
    if (bb->hidden == true)
    {
        return;
    }

    //Start by setting it in tempBlockV
    //Update the id using the size of blockList
    dvmSetBit (cUnit->tempBlockV, bb->id);
    dvmInsertGrowableList (& (cUnit->blockList), (intptr_t) bb);

    //Now handle children: get local versions for taken first, fallThrough second
    BasicBlock *taken = bb->taken;
    BasicBlock *fallThrough = bb->fallThrough;

    //We do not actually know which one is the "hot" path but we prefer the fallthrough.
    //The reason we do that is because there are two cases when we know this decision helps:
    // 1) Predicted inlining - The inlined path is always fallthrough after devirtualization check.
    // 2) Loops - The taken branch always goes to an exit point until chained by interpreter to
    // go directly to the loop head. In normal execution, the taken block is never really hit.
    BasicBlock *childFirst = fallThrough,
               *childSecond = taken;

    //If we can have a preference
    if (childFirst != 0 && childSecond != 0)
    {
        //If the first child is not code but the second is, prefer the second first
        if (childFirst->blockType != kDalvikByteCode)
        {
            if (childSecond->blockType == kDalvikByteCode)
            {
                std::swap (childFirst, childSecond);
            }
        }
        else
        {
            //Otherwise the first child is code but is it empty?
            if (childFirst->firstMIRInsn == 0)
            {
                std::swap (childFirst, childSecond);
            }
        }
    }

    //Recursive
    reorderHelper (cUnit, childFirst);
    reorderHelper (cUnit, childSecond);

    //If ever there are successor blocks, handle them now
    if (bb->successorBlockList.blockListType != kNotUsed)
    {
        //Iterator on the successor blocks
        GrowableListIterator iterator;
        dvmGrowableListIteratorInit(&bb->successorBlockList.blocks, &iterator);

        while (true)
        {
            //Get the next successor
            SuccessorBlockInfo *successorBlockInfo = (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);

            if (successorBlockInfo == NULL)
            {
                break;
            }

            //Get the destination block
            BasicBlock *destBlock = successorBlockInfo->block;

            //Recursive on this successor
            reorderHelper (cUnit, destBlock);
        }
    }
}

void dvmCompilerReorder (CompilationUnit *cUnit, Pass *pass)
{
    //Get a growable list for the entries of the cUnit
    GrowableList list;
    //Initialize it
    dvmInitGrowableList (&list, 1);

    //Find the entry points
    dvmCompilerFindEntries (cUnit, &list);

    //First let us reset the block list
    dvmClearGrowableList (& (cUnit->blockList));

    //If we don't have a tempBlockV, create it now
    if (cUnit->tempBlockV == 0)
    {
        cUnit->tempBlockV = dvmCompilerAllocBitVector (1, true);
    }

    //Clear tempBlockV
    dvmClearAllBits (cUnit->tempBlockV);

    //Iterate through the list of entries
    GrowableListIterator bbiterator;
    dvmGrowableListIteratorInit (&list, &bbiterator);

    //Now walk the list
    while (true)
    {
        //Get next element
        BasicBlock *bbscan = (BasicBlock *) dvmGrowableListIteratorNext (&bbiterator);

        //Paranoid
        if (bbscan == NULL)
        {
            break;
        }

        //Now go through the BasicBlocks DFS with a twist for the loops
        reorderHelper (cUnit, bbscan);
    }

    //Finally, if we have the puntBlock that has not been added add it
    if (cUnit->puntBlock != 0 && dvmIsBitSet (cUnit->tempBlockV, cUnit->puntBlock->id) == false)
    {
        dvmInsertGrowableList (& (cUnit->blockList), (intptr_t) (cUnit->puntBlock));
        dvmSetBit (cUnit->tempBlockV, cUnit->puntBlock->id);
    }

    //Now go through the list and update the ids
    //This is done because certain parts of the compiler suppose that the id and the
    //position in the blockList are the same...
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    int id = 0;
    while (true)
    {
        //Get next BasicBlock
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);

        //Have we finished
        if (bb == NULL)
        {
            break;
        }

        //Update id for the BasicBlock
        bb->id = id;
        id++;
    }

    //Since the blocks have been reordered, the peeling information is no longer valid
    LoopInformation *info = cUnit->loopInformation;

    if (info != 0)
    {
        info->invalidatePeeling ();
    }
}

/**
 * @brief Converts the 2addr opcodes to their normal equivalents.
 * @param opcode The instruction opcode.
 * @return Returns the normal form if 2addr instruction is found.
 * Otherwise, it returns the same opcode as function argument passed.
 */
static Opcode convertFrom2addr (Opcode opcode)
{
    Opcode result = opcode;

    switch (opcode)
    {
        case OP_ADD_INT_2ADDR:
            result = OP_ADD_INT;
            break;
        case OP_SUB_INT_2ADDR:
            result = OP_SUB_INT;
            break;
        case OP_MUL_INT_2ADDR:
            result = OP_MUL_INT;
            break;
        case OP_DIV_INT_2ADDR:
            result = OP_DIV_INT;
            break;
        case OP_REM_INT_2ADDR:
            result = OP_REM_INT;
            break;
        case OP_AND_INT_2ADDR:
            result = OP_AND_INT;
            break;
        case OP_OR_INT_2ADDR:
            result = OP_OR_INT;
            break;
        case OP_XOR_INT_2ADDR:
            result = OP_XOR_INT;
            break;
        case OP_SHL_INT_2ADDR:
            result = OP_SHL_INT;
            break;
        case OP_SHR_INT_2ADDR:
            result = OP_SHR_INT;
            break;
        case OP_USHR_INT_2ADDR:
            result = OP_USHR_INT;
            break;
        case OP_ADD_LONG_2ADDR:
            result = OP_ADD_LONG;
            break;
        case OP_SUB_LONG_2ADDR:
            result = OP_SUB_LONG;
            break;
        case OP_MUL_LONG_2ADDR:
            result = OP_MUL_LONG;
            break;
        case OP_DIV_LONG_2ADDR:
            result = OP_DIV_LONG;
            break;
        case OP_REM_LONG_2ADDR:
            result = OP_REM_LONG;
            break;
        case OP_AND_LONG_2ADDR:
            result = OP_AND_LONG;
            break;
        case OP_OR_LONG_2ADDR:
            result = OP_OR_LONG;
            break;
        case OP_XOR_LONG_2ADDR:
            result = OP_XOR_LONG;
            break;
        case OP_SHL_LONG_2ADDR:
            result = OP_SHL_LONG;
            break;
        case OP_SHR_LONG_2ADDR:
            result = OP_SHR_LONG;
            break;
        case OP_USHR_LONG_2ADDR:
            result = OP_USHR_LONG;
            break;
        case OP_ADD_FLOAT_2ADDR:
            result = OP_ADD_FLOAT;
            break;
        case OP_SUB_FLOAT_2ADDR:
            result = OP_SUB_FLOAT;
            break;
        case OP_MUL_FLOAT_2ADDR:
            result = OP_MUL_FLOAT;
            break;
        case OP_DIV_FLOAT_2ADDR:
            result = OP_DIV_FLOAT;
            break;
        case OP_REM_FLOAT_2ADDR:
            result = OP_REM_FLOAT;
            break;
        case OP_ADD_DOUBLE_2ADDR:
            result = OP_ADD_DOUBLE;
            break;
        case OP_SUB_DOUBLE_2ADDR:
            result = OP_SUB_DOUBLE;
            break;
        case OP_MUL_DOUBLE_2ADDR:
            result = OP_MUL_DOUBLE;
            break;
        case OP_DIV_DOUBLE_2ADDR:
            result = OP_DIV_DOUBLE;
            break;
        case OP_REM_DOUBLE_2ADDR:
            result = OP_REM_DOUBLE;
            break;
        default:
            break;
    }

    return result;
}

/*
 * @brief Is the opcode commutative?
 * @param opcode the instruction opcode
 * @return is the opcode representing a commutative operation?
 */
static bool isOpcodeCommutative (int opcode)
{
    switch (opcode)
    {
        case OP_ADD_INT:
        case OP_MUL_INT:
        case OP_AND_INT:
        case OP_OR_INT:
        case OP_XOR_INT:
        case OP_ADD_LONG:
        case OP_MUL_LONG:
        case OP_AND_LONG:
        case OP_OR_LONG:
        case OP_XOR_LONG:
            return true;
        default:
            break;
    }

    //Is not commutative
    return false;
}

bool dvmCompilerConvert2addr (CompilationUnit *cUnit, BasicBlock *bb)
{
    bool result = false;

    //Go through each MIR
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get local copy of current opcode
        Opcode currentOpcode = mir->dalvikInsn.opcode;

        //Check if we can convert this opcode to the non-2addr form
        Opcode newOpcode = convertFrom2addr (currentOpcode);

        //When they aren't equal, it means we found a 2addr which we converted
        if (currentOpcode != newOpcode)
        {
            //Update opcode
            mir->dalvikInsn.opcode = newOpcode;

            //vA is always the destination register. However, for 2addr form
            //vA is also the first source register while in the other case vB
            //is the first source register. Thus we want to copy the operands.
            mir->dalvikInsn.vC = mir->dalvikInsn.vB;
            mir->dalvikInsn.vB = mir->dalvikInsn.vA;

            //We have at least one update
            result = true;
        }

    }

    return result;
}

bool dvmCompilerAddInvokeSupportBlocks (CompilationUnit *cUnit, BasicBlock *bb)
{
    bool updatedCFG = false;

    //Iterate through MIRs to find invoke
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        int flags = dvmCompilerGetOpcodeFlags (mir->dalvikInsn.opcode);

        //In case we don't have an invoke we have no work to do
        if ((flags & kInstrInvoke) == 0)
        {
            continue;
        }

        //Check if invoke is last MIR in its BB
        if (mir->next != 0)
        {
            assert (mir != bb->lastMIRInsn);

            if (cUnit->printPass == true)
            {
                ALOGD ("JIT_INFO: Had to split invoke block to add chaining cell");
            }

            //We need to split the block because we are appending chaining cells to the block the invoke is in
            dvmCompilerSplitBlock(&cUnit->blockList, mir->next, bb, 0);

            updatedCFG = true;
        }

        //Now we need to add a fromInterp node but we will only do it if the fallthrough block exists
        //and has only a single predecessor (namely the bb in which the invoke lives)
        if (bb->fallThrough == 0 || dvmCountSetBits (bb->fallThrough->predecessors) != 1)
        {
            if (cUnit->printPass == true)
            {
                ALOGD ("JIT_INFO: Could not add fromInterpreter block for block post invoke because CFG is complicated");
            }

            //The CFG is too complicated and cannot easily insert a fromInterp node
            cUnit->quitLoopMode = true;
            return updatedCFG;
        }
        else
        {
            //Only add the fromInterp node if the fallthrough is not a chaining cell
            if (static_cast<int> (bb->fallThrough->blockType) > kChainingCellLast)
            {
                //Add the fromInterp node now
                BasicBlock *fromInterp = dvmCompilerNewBBinCunit (cUnit, kFromInterpreter);

                //Make the fallthrough of the fromInterp node be the block following block holding invoke
                dvmCompilerReplaceChildBasicBlock (bb->fallThrough, fromInterp, kChildTypeFallthrough);
            }
        }

        if (bb->taken != 0
                && (bb->taken->blockType == kChainingCellInvokePredicted
                        || bb->taken->blockType == kChainingCellInvokeSingleton))
        {
            //This invoke already has a chaining cell
            continue;
        }

        BasicBlock *invokeCC = 0;

        //We decide on the type of chaining cell to add based on whether we need prediction or not
        bool needsPrediction = dvmCompilerDoesInvokeNeedPrediction (mir->dalvikInsn.opcode);

        if (needsPrediction == true)
        {
            invokeCC = dvmCompilerNewBBinCunit (cUnit, kChainingCellInvokePredicted);
        }
        else
        {
            const Method *callee = dvmCompilerCheckResolvedMethod (cUnit->method, &mir->dalvikInsn);

            bool createCC = true;

            //If we know for sure that we have a Native JNI call, then we do not add a chaining cell.
            if (callee != 0)
            {
                if (dvmIsNativeMethod (callee) == true)
                {
                    createCC = false;
                }
            }

            if (createCC == true)
            {
                invokeCC = dvmCompilerNewBBinCunit (cUnit, kChainingCellInvokeSingleton);
                invokeCC->containingMethod = callee;
            }
        }

        if (invokeCC != 0)
        {
            //Add chaining cell as taken branch for invoke BB
            dvmCompilerReplaceChildBasicBlock (invokeCC, bb, kChildTypeTaken);

            updatedCFG = true;
        }
    }

    return updatedCFG;
}

bool dvmCompilerFixChainingCells (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Is this block a chaining cell?
    int type = bb->blockType;

    //If not, we are done
    if (type > kChainingCellGap)
    {
        return false;
    }

    //Walk the predecessors
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);

    //Get next index
    int nextIdx = dvmBitVectorIteratorNext (&bvIterator);

    while (nextIdx != -1) {

        //Get current index
        int blockIdx = nextIdx;

        nextIdx = dvmBitVectorIteratorNext(&bvIterator);

        //Get the predecessor basic block
        BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement (&cUnit->blockList, blockIdx);
        BasicBlock *currBB = bb;

        //We don't care about the last one, it can keep the chaining cell
        if (nextIdx != -1)
        {
            //Create a child's copy
            BasicBlock *copy = dvmCompilerCopyBasicBlock (cUnit, currBB);
            //And replace the original child with its copy
            dvmCompilerReplaceChildBasicBlock (copy, predBB, currBB);
            //Update the current bb
            currBB = copy;
        }

        //We have to insert pre-hot chaining cell before each hot chaining cell
        if (type == kChainingCellHot)
        {
            //Create an empty pre-hot bb
            BasicBlock *preHot = dvmCompilerNewBBinCunit (cUnit, kDalvikByteCode);
            //And attach it
            dvmCompilerInsertBasicBlockBetween (preHot, predBB, currBB);
        }
    }

    //Don't iterate
    return false;
}

//Peel the loop if needed
bool dvmCompilerPeel (CompilationUnit *cUnit, LoopInformation *info)
{
    //Paranoid
    if (info == 0)
    {
        return false;
    }

    //Peel the loop
    bool result = info->peelLoop (cUnit);

    if (result == false)
    {
        return result;
    }

    //We have changed the cUnit. Update information
    return dvmCompilerCalculateBasicBlockInformation (cUnit, false);
}

/**
 * @brief Integer comparison for the qsort
 * @param aptr the pointer to element a
 * @param bptr the pointer to element b
 * @return negative, 0, positive if a < 0, a == 0, a > 0
 */
int intCompare (const void *aptr, const void *bptr)
{
    const int *a = static_cast<const int *> (aptr);
    const int *b = static_cast<const int *> (bptr);

    //Get difference
    int res = (*a) - (*b);

    //Return it
    return res;
}

/**
 * @class sLocalValueNumberingAssociation
 * @brief sLocalValueNumberingAssociation provides a representation of the instruction for the LVN pass
 */
typedef struct sLocalValueNumberingAssociation
{
    /** @brief The instruction's opcode */
    int opcode;
    /** @brief A vector with the instruction's uses */
    std::vector<int> uses;
    /** @brief The constant used in the instruction */
    u8 constant;

    /**
     * @brief Redefining the < operator for use in the sorting
     * @param b the other sLocalValueNumberingAssociation
     * @return whether a < b
     */
    bool operator< (const struct sLocalValueNumberingAssociation &b) const
    {
        //Order of the tests: opcode first, uses second, constant third
        if (opcode < b.opcode)
        {
            return true;
        }

        //Only care about the rest if opcodes are identical
        if (opcode > b.opcode)
        {
            return false;
        }

        //Consider size
        unsigned int size = uses.size ();
        if (size < b.uses.size ())
        {
            return true;
        }

        //Only care about the rest if sizes are identical
        if (size > b.uses.size ())
        {
            return false;
        }

        //Consider uses
        for (unsigned int i = 0; i < size; i++)
        {
            if (uses[i] < b.uses[i])
            {
                return true;
            }

            //Only care about rest if current use is identical
            if (uses[i] > b.uses[i])
            {
                return false;
            }
        }

        //Consider the constants
        if (constant < b.constant)
        {
            return true;
        }

        return false;
    }
}SLocalValueNumberingAssociation;

/**
 * @brief Handle the local value numbering for the instruction
 * @param mir MIR instruction
 * @param associations association between a SLocalValueNumberingAssociation and its color
 * @param ssaAssociations map between the SSA register and its color
 */
static void handleLocalValueNumbering (MIR *mir,
                                       std::map<SLocalValueNumberingAssociation, unsigned int> &associations,
                                       std::map<unsigned int, unsigned int> &ssaAssociations)
{
    //Static value to give a unique number to the hash
    static int hashValue = 0;

    //What hash value are we giving to the mir instruction
    int hash = 0;

    //Get DecodedInstruction
    DecodedInstruction &insn = mir->dalvikInsn;

    //Get the opcode
    int opcode = insn.opcode;

    //Create an association entry
    SLocalValueNumberingAssociation association;
    association.opcode = opcode;

    //First, get SSA representation
    SSARepresentation *ssaRep = mir->ssaRep;

    //If no ssa rep, we have a new color
    if (ssaRep != 0)
    {
        //Get number of uses
        int numUses = ssaRep->numUses;

        //Get values
        int uses[numUses];

        //Copy them
        for (int i = 0; i < numUses; i++)
        {
            uses[i] = ssaRep->uses[i];
        }

        //Is the opcode commutative?
        if (isOpcodeCommutative (opcode) == true)
        {
            //Sort the operands
            qsort (uses, numUses, sizeof (uses[0]), intCompare);
        }

        for (int i = 0; i < numUses; i++)
        {
            //Get hash value
            unsigned int tmp = 0;

            std::map<unsigned int, unsigned int>::const_iterator it = ssaAssociations.find (uses[i]);

            //If not found, use its ssa value
            if (it == ssaAssociations.end ())
            {
                tmp = uses[i];
            }
            else
            {
                tmp = it->second;
            }

            //Add to the vector
            association.uses.push_back (tmp);
        }

        //If it's a const wide, then just grab the wide_vB
        if (opcode == OP_CONST_WIDE)
        {
            association.constant = insn.vB_wide;
        }
        else
        {
            //Add the constant if there is one (default value 0)
            association.constant = 0;

            //Get flags first
            long long flags = dvmCompilerDataFlowAttributes[opcode];

            //If vB is being used, vC might be a constant
            if ( (flags & (DF_UB | DF_UB_WIDE)) != 0)
            {
                //If vC is not being used, it's the constant, otherwise there is no constant
                if ( (flags & (DF_UC | DF_UC_WIDE)) == 0)
                {
                    association.constant = insn.vC;
                }
            }
            else
            {
                //Otherwise vB is the constant
                association.constant = insn.vB;
            }
        }

        //Can we find it in the map?
        std::map<SLocalValueNumberingAssociation, unsigned int>::const_iterator it;
        it = associations.find (association);

        //If found, get its color
        if (it != associations.end ())
        {
            hash = it->second;
        }
        else
        {
            //Otherwise the hash value is the map's size
            hash = hashValue;

            //Next hash value
            hashValue++;

            //Put it in the map
            associations[association] = hash;
        }

        //Set the hash for the defines
        int numDefs = ssaRep->numDefs;
        int *defs = ssaRep->defs;
        for (int i = 0; i < numDefs; i++)
        {
            ssaAssociations[defs[i]] = hash;
        }

        //If not a constant setting and it is found, get its color
        if (it != associations.end ())
        {
            hash = it->second;
        }
        else
        {
            //Did not find it in the association table, so get a new one
            hash = hashValue;

            //Next hash value
            hashValue++;
        }
    }
    else
    {
        //No SSA Representation, get a new hashValue
        hash = hashValue;

        //Next hash value
        hashValue++;
    }

    //Also set the instruction color
    mir->localValueNumber = hash;
}

/**
 * @brief Perform local value numbering
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns whether we changed anything in the BasicBlock
 */
bool dvmCompilerLocalValueNumbering (CompilationUnit *cUnit, BasicBlock *bb)
{
    //A map for the value numbering hash
    std::map<SLocalValueNumberingAssociation, unsigned int> localValueNumberingAssociations;

    //A map for the value numbering for SSA registers
    std::map<unsigned int, unsigned int> ssaAssociations;

    //Walk the BasicBlock
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Handle the MIR
        handleLocalValueNumbering (mir, localValueNumberingAssociations, ssaAssociations);
    }

    //Nothing changed in the BasicBlock except the localValueNumber in the instructions
    return false;
}

/* Perform local value numbering */
void dvmCompilerLocalValueNumbering (CompilationUnit *cUnit)
{
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, dvmCompilerLocalValueNumbering, kPredecessorsFirstTraversal, false);
}


/**
 * @brief This optimization removes redundant goto and conditional instruction
 * @details Goto instruction at the end of basic block can be safely removed if it leads
 * to dalvik code basic block.
 * Any conditional instruction at the end of basic block can be safely removed if both
 * taken and fallthrough leads to dalvik code basick block.
 * @param cUnit Compilation context
 * @param bb Basic block to consider
 * @return false always indicating that no need to re-traverse CFG
 */
bool dvmCompilerRemoveGoto (CompilationUnit *cUnit, BasicBlock *bb)
{
//Get last instruction
    MIR *lastMIR = bb->lastMIRInsn;

    if (lastMIR != 0)
    {
        DecodedInstruction &insn = lastMIR->dalvikInsn;

        if (insn.opcode >= OP_GOTO && insn.opcode <= OP_GOTO_32)
        {
            //Ok we do have a goto, in theory we only have a taken
            if (bb->taken != 0 && bb->fallThrough == 0)
            {
                //If the taken goes to a bytecode, then we can remove the goto
                if (bb->taken->blockType == kDalvikByteCode)
                {
                    //Remove the MIR
                    dvmCompilerRemoveMIR(bb, lastMIR);

                    //FallThrough to the taken branch
                    bb->fallThrough = bb->taken;
                    bb->taken = 0;
                }
            }
        }

        // Handle conditional instruction
        if (insn.opcode >= OP_IF_EQ && insn.opcode <= OP_IF_LEZ)
        {
            if (bb->taken != 0 && bb->fallThrough == bb->taken)
            {
                //If the taken goes to a bytecode, then we can remove the if
                if (bb->taken->blockType == kDalvikByteCode)
                {
                    //Remove the MIR
                    dvmCompilerRemoveMIR(bb, lastMIR);

                    //FallThrough clean taken branch
                    bb->taken = 0;
                }
            }
        }
    }
    return false;
}

bool dvmCompilerCopyPropagationMoveReturn (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Do copy propagation on move and return only
    MIR *mir, *defMir;
    SSARepresentation *ssaRep, *defSsaRep;
    SUsedChain *chain;

    //Check if bb exists
    if (bb == 0)
    {
        return false;
    }

    mir = bb->lastMIRInsn;
    //Check if the last insn is OP_RETURN
    if (mir == 0 || mir->dalvikInsn.opcode != OP_RETURN)
    {
        return false;
    }

    ssaRep = mir->ssaRep;
    //Check the number of uses
    if (ssaRep == 0 || ssaRep->numUses != 1)
    {
        return false;
    }

    defMir = ssaRep->defWhere[0];
    //Make sure OP_MOVE is followed by OP_RETURN immediately
    if (defMir == 0 || defMir->dalvikInsn.opcode != OP_MOVE
        || defMir->next != mir)
    {
        return false;
    }

    defSsaRep = defMir->ssaRep;
    if (defSsaRep == 0)
    {
        return false;
    }

    chain = defSsaRep->usedNext[0];
    //Check if there is only one use
    if (chain !=0 && chain->mir == mir && chain->nextUse == 0)
    {
        int oldReg = dvmExtractSSARegister (cUnit, defSsaRep->defs[0]);
        int newReg = dvmExtractSSARegister (cUnit, defSsaRep->uses[0]);
        //Update the source register of the return instruction
        if (dvmCompilerRewriteMirUses (defMir, oldReg, newReg) == true)
        {
            //Remove the move instruction
            bool removed = dvmCompilerRemoveMIR (bb, defMir);
            (void) removed;
            assert (removed == true);
            //To reduce optimization time, don't re-run this optimization
            return false;
        }
    }

    //Did not change the BasicBlock
    return false;
}
