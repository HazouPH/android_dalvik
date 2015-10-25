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

#include "CompilerIR.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "LoopInformation.h"
#include "Utility.h"

#include <map>
#include <stack>

bool LoopInformation::iterateWithConst (const CompilationUnit *cUnit, bool (*func)(const CompilationUnit *, LoopInformation *, void *), void *data)
{
    LoopInformation *item = this;
    while (item != 0)
    {
        if (func (cUnit, item, data) == false)
        {
            return false;
        }
        if (item->nested != 0)
        {
            if (item->nested->iterateWithConst (cUnit, func, data) == false)
            {
                return false;
            }
        }
        item = item->siblingNext;
    }
    return true;
}

bool LoopInformation::iterate (CompilationUnit *cUnit, bool (*func)(CompilationUnit *, LoopInformation *, void *), void *data)
{
    LoopInformation *item = this;
    while (item != 0)
    {
        if (func (cUnit, item, data) == false)
        {
            return false;
        }
        if (item->nested != 0)
        {
            if (item->nested->iterate (cUnit, func, data) == false)
            {
                return false;
            }
        }
        item = item->siblingNext;
    }
    return true;
}

bool LoopInformation::iterateThroughLoopBasicBlocks (CompilationUnit *cUnit, bool (*func) (CompilationUnit *cUnit, BasicBlock *, void *), void *data)
{
    BitVector *basicBlocks = getBasicBlocks ();
    if (basicBlocks == 0)
    {
        return true;
    }

    //Create iterator to go through basic blocks
    BitVectorIterator blockIter;
    dvmBitVectorIteratorInit (basicBlocks, &blockIter);

    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (blockIter, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (blockIter, cUnit->blockList))
    {
        //Call the worker function
        if (func (cUnit, bb, data) == false)
        {
            return false;
        }
    }

    return true;
}

bool LoopInformation::iterateThroughLoopExitBlocks (CompilationUnit *cUnit, bool (*func) (CompilationUnit *cUnit, BasicBlock *, void *), void *data)
{
    BitVector *loopExits = getExitLoops ();
    if (loopExits == 0)
    {
        return true;
    }

    //Create iterator to go through exit blocks
    BitVectorIterator blockIter;
    dvmBitVectorIteratorInit (loopExits, &blockIter);

    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (blockIter, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (blockIter, cUnit->blockList))
    {
        //Now call the worker function
        if (func (cUnit, bb, data) == false)
        {
            return false;
        }
    }

    return true;
}

bool LoopInformation::iterate (bool (*func)(LoopInformation *, void *), void *data)
{
    LoopInformation *item = this;
    while (item != 0)
    {
        if (func (item, data) == false)
        {
            return false;
        }
        if (item->nested != 0)
        {
            if (item->nested->iterate (func, data) == false)
            {
                return false;
            }
        }
        item = item->siblingNext;
    }
    return true;
}

/**
 * @brief helper function to collect all loop informations into map BB->LoopInformation
 * @param cUnit the CompilationUnit
 * @param info current Loop Information
 * @param data map to fill
 * @return always true, required by interface
 */
static bool collectAllNested (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    std::map<BasicBlock *, LoopInformation *> *container =
        static_cast<std::map<BasicBlock *, LoopInformation *> *>(data);

    (*container)[info->getEntryBlock()] = info;

    return true;
};

/**
 * @brief Find all tail blocks to specified basic block
 * @param cUnit the CompilationUnit
 * @param bb the basic block which is suggested to be a loop head
 * @return tail blocks or 0 if there is no them
 */
static BitVector* getLoopTailBlocks (CompilationUnit *cUnit, BasicBlock *bb)
{
    BitVectorIterator bvIterator;
    BitVector *tailblocks = 0;

    assert (bb->predecessors != 0);

    // If the predecessor is dominated by this entry, it is a backward branch
    dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);
    for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); predBB != 0;
                     predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        //If we have no dominator information, we can skip it
        if (predBB->dominators == 0)
        {
            continue;
        }

        if (dvmIsBitSet (predBB->dominators, bb->id) != 0)
        {
            if (tailblocks == 0)
            {
                tailblocks = dvmCompilerAllocBitVector (1, true);
                dvmClearAllBits (tailblocks);
            }
            dvmSetBit (tailblocks, predBB->id);
        }
    }
    return tailblocks;
}

/**
 * @brief Find All BB in a loop
 * @param cUnit the CompilationUnit
 * @param entry loop entry
 * @param tailblocks tail blocks to loop entry
 * @param basicBlocks bit vector to fill with blocks in a loop
 * @return false if it is not a loop, namely there is a BB which entry does not dominate
 */
static bool getAllBBInLoop (CompilationUnit *cUnit, BasicBlock *entry, BitVector *tailblocks, BitVector *basicBlocks)
{
    assert (tailblocks != 0);
    assert (entry != 0);
    assert (basicBlocks != 0);

    dvmClearAllBits (basicBlocks);

    // loop entry is in a loop
    dvmSetBit (basicBlocks, entry->id);

    std::stack<BasicBlock*> workStack;

    // Start from tail blocks except entry loop if it is a tail block at the same time
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(tailblocks, &bvIterator);
    for (BasicBlock *tailblock = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);
                     tailblock != 0;
                     tailblock = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        if (tailblock != entry)
        {
            workStack.push (tailblock);
        }
    }

    // Loop entry dominates us, so we are safe walking by predecessors stopping by loop entry
    while (workStack.empty () != true)
    {
        BasicBlock *cur = workStack.top ();
        workStack.pop ();

        //Check if we have domination information, we might not because domination is only created if the block is reachable.
        //For example, From Interpreter blocks are not reachable from the entry block, and thus don't get domination information
        if (cur->dominators == 0)
        {
            continue;
        }

        if (dvmIsBitSet (cur->dominators, entry->id) == 0)
        {
            // it is not a normal loop
            return false;
        }

        dvmSetBit (basicBlocks, cur->id);

        dvmBitVectorIteratorInit(cur->predecessors, &bvIterator);
        for (BasicBlock *pred = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); pred != 0;
                         pred = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
        {
            if (dvmIsBitSet (basicBlocks, pred->id) == false)
            {
                workStack.push (pred);
            }
        }
    }

    return true;
}

/**
 * @brief Determine not in a loop's BB with link from loop body
 * @param cUnit the CompilationUnit
 * @param basicBlocks the basic block forming a loop
 * @param exitBlocks not in a loop's BBs with link from loop body (to fill)
 */
static void getOutsFromLoop (CompilationUnit *cUnit, BitVector *basicBlocks, BitVector *exitBlocks)
{
    assert (basicBlocks != 0);
    assert (exitBlocks != 0);

    dvmClearAllBits (exitBlocks);

    // Iterate over BB in a loop and if its edge comes out of a loop => add it to bit vector
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(basicBlocks, &bvIterator);
    for (BasicBlock *cur = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); cur != 0;
                     cur = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        if ((cur->taken != 0) && (dvmIsBitSet (basicBlocks, cur->taken->id) == false))
        {
            //We skip invoke chaining cells because those don't have control flow semantics
            if (cur->taken->blockType != kChainingCellInvokeSingleton
                    && cur->taken->blockType != kChainingCellInvokePredicted)
            {
                dvmSetBit (exitBlocks, cur->taken->id);
            }
        }
        if ((cur->fallThrough != 0) && (dvmIsBitSet (basicBlocks, cur->fallThrough->id) == false))
        {
            //We skip invoke chaining cells because those don't have control flow semantics
            if (cur->fallThrough->blockType != kChainingCellInvokeSingleton
                    && cur->fallThrough->blockType != kChainingCellInvokePredicted)
            {
                dvmSetBit (exitBlocks, cur->fallThrough->id);
            }
        }

        if (cur->successorBlockList.blockListType != kNotUsed) {
            GrowableListIterator iterator;
            dvmGrowableListIteratorInit(&cur->successorBlockList.blocks,
                                    &iterator);
            while (true) {
                SuccessorBlockInfo *successorBlockInfo = (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);
                if (successorBlockInfo == 0)
                {
                    break;
                }

                BasicBlock *succBB = successorBlockInfo->block;
                if (succBB == 0)
                {
                    continue;
                }
                if (dvmIsBitSet (basicBlocks, succBB->id) == false)
                {
                    dvmSetBit (exitBlocks, succBB->id);
                }
            }
        }
    }
}

LoopInformation * LoopInformation::getLoopInformation (CompilationUnit *cUnit, LoopInformation *current)
{
    if (cUnit->quitLoopMode == true)
    {
        return 0;
    }

    std::map<BasicBlock *, LoopInformation *> curBB2LI;
    LoopInformation *result = 0;
    if (current != 0)
    {
        current->iterate (cUnit, collectAllNested, &curBB2LI);
    }

    // iterate over all BB
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    while (true)
    {
        //Get next BasicBlock
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);

        //Have we finished
        if (bb == 0)
        {
            break;
        }

        if (bb->hidden == true)
        {
            continue;
        }

        // First find all tail blocks
        BitVector *tailblocks = getLoopTailBlocks(cUnit, bb);

        if (tailblocks == 0)
        {
            // bb is not a loop entry
            continue;
        }

        // Do we have an old loop information for bb loop entry?
        LoopInformation *info = curBB2LI[bb];
        if (info == 0)
        {
            info = static_cast<LoopInformation *> (dvmCompilerNew (sizeof (*info), true));
            info->init ();
            info->setEntryBlock (bb);
        }
        else
        {
            info->parent = 0;
            info->nested = 0;
            info->siblingNext = 0;
            info->siblingPrev = 0;
            assert (info->getEntryBlock () == bb);
        }

        // Set backwards
        info->backward = tailblocks;

        // Now, Find all BB in a loop
        if (getAllBBInLoop (cUnit, bb, tailblocks, info->basicBlocks) == false)
        {
            // It is not a normal loop
            continue;
        }

        // Now, Find out from a loop
        getOutsFromLoop (cUnit, info->basicBlocks, info->exitLoop);

        // Now, check for pre-header
        // We need to find a predecessor dominating us
        // In correctly formed loop is will be alone
        BitVector *tmp = dvmCompilerAllocBitVector (1, true);
        dvmIntersectBitVectors (tmp, bb->predecessors, bb->dominators);
        int idx = dvmHighestBitSet (tmp);
        if (idx >= 0)
        {
            info->preHeader = ((BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, idx)));
        }
        else
        {
            info->preHeader = 0;
        }

        //Also get its from interpreter node
        if (info->preHeader != 0)
        {
            BitVector *predecessors = info->preHeader->predecessors;

            //Set to 0 before iterating
            info->fromInterpreter = 0;

            //Go through predecessors
            BitVectorIterator bvIterator;
            dvmBitVectorIteratorInit (predecessors, &bvIterator);

            for (BasicBlock *current = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); current != 0;
                             current = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
            {
                if (current->blockType == kFromInterpreter)
                {
                    //Set it for info
                    info->fromInterpreter = current;

                    //Set it as non hidden
                    current->hidden = false;

                    //We are done
                    break;
                }
            }
        }

        // Last thing, we do not want kPreBackwardBlock and kChainingCellBackwardBranch to be in our loop
        BitVectorIterator bvIterator;
        dvmBitVectorIteratorInit (info->backward, &bvIterator);
        for (BasicBlock *current = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);
                         current != 0;
                         current = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
        {
            if (current->blockType == kChainingCellBackwardBranch)
            {
                dvmClearBit (info->basicBlocks, current->id);

                // Also, let's check if there is a kPreBackwardBlock predecessor
                BitVectorIterator bvIterator2;
                dvmBitVectorIteratorInit (current->predecessors, &bvIterator2);
                for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator2, cUnit->blockList);
                                 predBB != 0;
                                 predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator2, cUnit->blockList))
                {
                    if (predBB->blockType == kPreBackwardBlock)
                    {
                        dvmClearBit (info->basicBlocks, predBB->id);
                    }
                }
            }
        }

        // Nest loop information
        result = (result == 0) ? info : result->add (info);

        // We are done
    }

    return result;
}

LoopInformation::LoopInformation (void)
{
    //Set BitVector to 0
    interIterationVariables = 0;

    //Call initialization
    init ();
}

LoopInformation::~LoopInformation (void)
{
}

void LoopInformation::init (void)
{
    variants = 0;
    parent = 0;
    siblingNext = 0;
    siblingPrev = 0;
    nested = 0;
    depth = 0;
    basicBlocks = 0;
    backward = 0;
    entry = 0;
    preHeader = 0;
    scratchRegisters = 0;
    peeledBlocks = 0;
    countUpLoop = false;
    ssaBIV = 0;
    endConditionReg = -1; // -1 for validation reasons
    fromInterpreter = 0;

    if (interIterationVariables == 0)
    {
        interIterationVariables = dvmCompilerAllocBitVector (1, true);
    }
    else
    {
        dvmClearAllBits (interIterationVariables);
    }

    // Initialize growable lists
    dvmInitGrowableList (&inductionVariableList, 1);
    arrayAccessInfo = (GrowableList *)dvmCompilerNew(sizeof(GrowableList), true);
    dvmInitGrowableList (arrayAccessInfo, 4);

    //Initialize the BitVectors:
    exitLoop = dvmCompilerAllocBitVector (1, true);
    basicBlocks = dvmCompilerAllocBitVector (1, true);
}

// update depth for loop and nested loops
void LoopInformation::setDepth (int depth)
{
    LoopInformation *info = this;
    while (info != 0)
    {
        info->depth = depth;
        if (info->nested != 0)
        {
            info->nested->setDepth (depth + 1);
        }
        info = info->siblingNext;
    }
}

/**
 * Add takes a new LoopInformation and determines if info is nested with this instance or not.
 * If it is nested in this instance, we fill our nested information with it
 * Otherwise, we are nested in it and we request it to nest us
 * The function returns the outer nested loop, it can nest any level of a nested loop
 */
LoopInformation *LoopInformation::add (LoopInformation *info)
{
    // Simple case
    if (info == this)
    {
        return this;
    }

    //Do we include the current loop ?
    if (contains (info->getEntryBlock ()) == true)
    {
        //We contain them, so they should not contain us
        assert (info->contains (getEntryBlock ()) == false);

        //Search in the children if anybody includes them
        if (nested == 0)
        {
            nested = info;
        }
        else
        {
            nested = nested->add (info);
        }
        nested->parent = this;
        nested -> setDepth (getDepth () + 1);
        return this;
    }
    else
    {
        //Otherwise, info contains us
        if (info->contains (getEntryBlock ()) == true)
        {
            return info->add (this);
        }
        else
        {
            // it is sibling
            info->depth = getDepth ();
            info->parent = this->getParent ();
            info->siblingNext = this;
            this->siblingPrev = info;
            return info;
        }
    }
}

/**
 * @brief Utility function to check whether the current loop corresponds to specified entry
 * @param info loop information
 * @param data std::pair where the first is key bb, the second is a place for result
 * @return false is specified bb is an entry of this loop
 */
static bool getLoopInformationByEntryHelper (LoopInformation *info, void *data)
{
    std::pair<BasicBlock*, LoopInformation*> *pair =
        static_cast<std::pair<BasicBlock*, LoopInformation*> *>(data);

    if (info->getEntryBlock () == pair->first)
    {
        pair->second = info;
        return false;
    }
    return true;
}

/**
 * Return the LoopInformation that has entry as the entry BasicBlock
 */
LoopInformation *LoopInformation::getLoopInformationByEntry (const BasicBlock *entry)
{
    // Fast check
    if (this->entry == entry)
    {
        return this;
    }

    // Iterate over all loops
    std::pair<const BasicBlock*, LoopInformation*> pair(entry, 0);

    if (iterate (getLoopInformationByEntryHelper, &pair) == false)
    {
        return pair.second;
    }

    return 0;
}

bool LoopInformation::isBasicBlockALoopHelper (const BasicBlock *bb)
{
    if (bb == 0)
    {
        return false;
    }

    return preHeader == bb
        || (exitLoop != 0 && dvmIsBitSet (exitLoop, bb->id) == true)
        || (backward != 0 && dvmIsBitSet (backward, bb->id) == true);
}

BasicBlock *LoopInformation::getExitBlock (const CompilationUnit *cUnit)
{
    //Check if we have the exitLoop BitVector
    if (exitLoop == 0)
    {
        return 0;
    }

    //Check if we have only one exit block
    int numBlocks = dvmCountSetBits (exitLoop);

    if (numBlocks != 1)
    {
        return 0;
    }

    //It is alone, so just take it
    int idx = dvmHighestBitSet (exitLoop);

    return (BasicBlock *) dvmGrowableListGetElement (&cUnit->blockList, idx);
}

BitVector *LoopInformation::getPostExitLoops (const CompilationUnit *cUnit)
{
    BitVector *postExitLoop = dvmCompilerAllocBitVector (1, true);
    dvmClearAllBits (postExitLoop);

    // Iterate over all exit loops
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(exitLoop, &bvIterator);
    BasicBlock *bb;
    while ((bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList)) != 0)
    {
        assert (bb->taken == 0);
        assert (bb->fallThrough != 0);

        if (bb->fallThrough != 0)
        {
            dvmSetBit (postExitLoop, bb->fallThrough->id);
        }
    }
    return postExitLoop;
}

BasicBlock *LoopInformation::getPostExitBlock (const CompilationUnit *cUnit)
{
    //Get the post exit BasicBlocks BitVector
    BitVector *postExitBlocks = getPostExitLoops (cUnit);

    if (postExitBlocks == 0)
    {
        return 0;
    }

    //Check if we have only one post exit block
    int numBlocks = dvmCountSetBits (postExitBlocks);

    if (numBlocks != 1)
    {
        return 0;
    }

    //It is alone, so just take it
    int idx = dvmHighestBitSet (postExitBlocks);
    return (BasicBlock *) dvmGrowableListGetElement (&cUnit->blockList, idx);
}

BasicBlock *LoopInformation::getBackwardBranchBlock (const CompilationUnit *cUnit)
{
    //Check if we have a backward blocks bitvector (Paranoid)
    if (backward == 0)
    {
        return 0;
    }

    //Check if we have only one post exit block
    int numBlocks = dvmCountSetBits (backward);

    if (numBlocks != 1)
    {
        return 0;
    }

    //It is alone, so just take it
    int idx = dvmHighestBitSet (backward);
    BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement (&cUnit->blockList, idx);

    //Make sure it is a backward branch (Paranoid)
    if (bb == 0 || bb->blockType != kChainingCellBackwardBranch)
    {
        return 0;
    }

    return bb;
}

bool LoopInformation::contains (const BasicBlock *bb) const
{
    //If we don't have any basic blocks or if bb is nil, return false
    if (basicBlocks == 0 || bb == 0)
    {
        return false;
    }

    //Otherwise check the bit
    return dvmIsBitSet (basicBlocks, bb->id);
}

static bool dumpInformationHelper (const CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    unsigned int tab = *(static_cast<int*>(data)) + info->getDepth ();

    char buffer[256];
    char tabs[10];
    unsigned int i;

    //Set tabs to 0
    memset (tabs, 0, sizeof (tabs));

    //Set up tab array
    for (i = 0; i < tab && i < sizeof(tabs) - 1; i++)
        tabs[i] = ' ';
    tabs[i] = '\0';

    //Print out base information
    snprintf (buffer, sizeof (buffer), "%sThis: %p", tabs, info);
    ALOGD ("%s", buffer);
    snprintf (buffer, sizeof (buffer), "%sDepth: %d", tabs, info->getDepth ());
    ALOGD ("%s", buffer);
    snprintf (buffer, sizeof (buffer), "%sEntry: %d", tabs, info->getEntryBlock () != 0 ? info->getEntryBlock ()->id : -1);
    ALOGD ("%s", buffer);
    snprintf (buffer, sizeof (buffer), "%sPreHeader: %d", tabs, info->getPreHeader () != 0 ? info->getPreHeader ()->id : -1);
    ALOGD ("%s", buffer);

    snprintf (buffer, sizeof (buffer), "%sPost Exit: ", tabs);
    dvmDumpBitVector (buffer, info->getPostExitLoops (cUnit), true);

    //Print the backward chaining blocks
    snprintf (buffer, sizeof (buffer), "%sPost Loop: ", tabs);
    dvmDumpBitVector (buffer, info->getExitLoops (), true);

    //Print the backward chaining blocks
    snprintf (buffer, sizeof (buffer), "%sBackward: ", tabs);
    dvmDumpBitVector (buffer, info->getBackwardBranches (), true);

    //Print the BitVector
    snprintf (buffer, sizeof (buffer), "%sBasicBlocks: ", tabs);
    dvmDumpBitVector (buffer, info->getBasicBlocks (), true);

    return true;
}

void LoopInformation::dumpInformation (const CompilationUnit *cUnit, unsigned int tab)
{
    iterateWithConst (cUnit, dumpInformationHelper, &tab);
}

static bool dumpInformationDotFormatHelper (const CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    FILE *file = static_cast<FILE*>(data);

    char buffer[256];

    //Create a node
    unsigned long uid = (unsigned long) (info);

    fprintf (file, "%lu [shape=record, label =\"{ \\\n", uid);

    //Print out base information
    snprintf (buffer, sizeof (buffer), "{Loop:} | \\\n");
    fprintf (file, "%s", buffer);
    snprintf (buffer, sizeof (buffer), "{Depth: %d} | \\\n", info->getDepth ());
    fprintf (file, "%s", buffer);
    snprintf (buffer, sizeof (buffer), "{Entry: %d} | \\\n", info->getEntryBlock () != 0 ? info->getEntryBlock ()->id : -1);
    fprintf (file, "%s", buffer);
    snprintf (buffer, sizeof (buffer), "{PreHeader: %d} | \\\n", info->getPreHeader () ? info->getPreHeader ()->id : -1);
    fprintf (file, "%s", buffer);

    dvmDumpBitVectorDotFormat (file, "Post Exit: ", info->getPostExitLoops (cUnit), true);

    //Print the backward chaining blocks
    dvmDumpBitVectorDotFormat (file, "Post Loop: ", info->getExitLoops (), true);

    //Print the backward chaining blocks
    dvmDumpBitVectorDotFormat (file, "Backward: ", info->getBackwardBranches (), true);

    //Print the BasicBlocks BitVector
    dvmDumpBitVectorDotFormat (file, "BasicBlocks: ", info->getBasicBlocks (), true, true);

    //End the block
    fprintf (file, "}\"];\n\n");

    //Now make the link
    unsigned long childUID = (unsigned long) (info->getNested ());
    if (childUID != 0)
    {
        fprintf (file, "%lu:s -> %lu:n\n", uid, childUID);
    }

    return true;
}

void LoopInformation::dumpInformationDotFormat (const CompilationUnit *cUnit, FILE *file)
{
    iterateWithConst (cUnit, dumpInformationDotFormatHelper, file);
}

bool LoopInformation::executedPerIteration (const CompilationUnit *cUnit, const BasicBlock *bb) const
{
    //Paranoid
    if (bb == 0)
    {
        return false;
    }

    //Go through the backward chaining cells
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (backward, &bvIterator);
    for (BasicBlock *bwcc = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bwcc != 0;
                     bwcc = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        //To prove that the mir is executed per iteration, it's block must dominate each backward chaining cell
        if (dvmIsBitSet (bwcc->dominators, bb->id) == false)
        {
            return false;
        }
    }

    //The BasicBlock is always executed
    return true;
}

bool LoopInformation::executedPerIteration (const CompilationUnit *cUnit, const MIR *mir) const
{
    //Paranoid
    assert (mir != 0);

    //Get the mir's BasicBlock
    BasicBlock *current = mir->bb;

    //Call the function using the BasicBlock
    return executedPerIteration (cUnit, current);
}

InductionVariableInfo *LoopInformation::getInductionVariableInfo (const CompilationUnit *cUnit, int reg, bool isSSA)
{
    //Go through the induction variable list
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit (&inductionVariableList, &iterator);

    while (true)
    {
        InductionVariableInfo *info = (InductionVariableInfo *) (dvmGrowableListIteratorNext (&iterator));

        //Bail at the end
        if (info == 0)
        {
            break;
        }

        //Check if we have a basic IV. If we do, then we can simply look up by dalvik register.
        if (info->isBasicIV () == true)
        {
            //Convert them both to dalvik register.
            int ivDalvikReg = dvmExtractSSARegister (cUnit, info->ssaReg);
            int regToCheck = (isSSA == true) ? dvmExtractSSARegister (cUnit, reg) : reg;

            //Return true if they match, otherwise continue to the next
            if (regToCheck == ivDalvikReg)
            {
                return info;
            }
        }
        else
        {
            //We have a dependent IV. It does not make sense to look up by dalvik register
            //and thus we first check that the provided reg is in ssa form and only if it
            //we check whether we found a matching entry in the IV list
            if (isSSA == true && reg == info->ssaReg)
            {
                return info;
            }
        }
    }

    //Did not find it
    return 0;
}

int LoopInformation::getInductionIncrement (const CompilationUnit *cUnit, unsigned int reg, bool isSSA)
{
    InductionVariableInfo *info = getInductionVariableInfo (cUnit, reg, isSSA);

    if (info == 0)
    {
        return 0;
    }

    return info->loopIncrement;
}

bool LoopInformation::isBasicInductionVariable (const CompilationUnit *cUnit, unsigned int reg, bool isSSA)
{
    InductionVariableInfo *info = getInductionVariableInfo (cUnit, reg, isSSA);

    if (info == 0)
    {
        return false;
    }

    return info->isBasicIV ();
}

bool LoopInformation::isAnInductionVariable (const CompilationUnit *cUnit, unsigned int reg, bool isSSA)
{
    InductionVariableInfo *info = getInductionVariableInfo (cUnit, reg, isSSA);

    if (info == 0)
    {
        return false;
    }

    return true;
}

MIR *LoopInformation::getPhiInstruction (const CompilationUnit *cUnit, unsigned int vr) const
{
    //Get the BasicBlock vector for this loop
    BitVector *blocks = getBasicBlocks ();

    //Iterate through them
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (blocks, &bvIterator);
    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        //Go through its instructions
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Get dalvik instruction
            DecodedInstruction &insn = mir->dalvikInsn;

            //Is it a phi node?
            if (insn.opcode == static_cast<Opcode> (kMirOpPhi))
            {
                //Get ssa representation
                SSARepresentation *ssa = mir->ssaRep;

                //Paranoid
                assert (ssa != 0 && ssa->numDefs == 1);

                //Does it define our vr?
                int ssaReg = ssa->defs[0];

                //Is it a match?
                if (dvmExtractSSARegister (cUnit, ssaReg) == vr)
                {
                    //In a complex CFG we can have several Phi nodes for the same VR
                    //We'd like to find the first one, namely Phi node where the one of
                    //uses comes from outside of the loop
                    if (ssa->defWhere != 0)
                    {
                        for (int i = 0; i < ssa->numUses; i++)
                        {
                            MIR *defMir = ssa->defWhere[i];
                            //If defMir is 0 then it is come from outside of trace
                            if (defMir == 0 || contains (defMir->bb) == false)
                            {
                                return mir;
                            }
                        }
                    }
                }
            }
        }
    }

    //Did not find it
    return 0;
}

bool LoopInformation::isInterIterationVariable (unsigned int vr) const
{
    return dvmIsBitSet (interIterationVariables, vr);
}

void LoopInformation::addInterIterationVariable (unsigned int vr)
{
    dvmSetBit (interIterationVariables, vr);
}

void LoopInformation::clearInterIterationVariables (void)
{
    dvmClearAllBits (interIterationVariables);
}

/**
 * @brief Determines whether the loop contains certain opcodes that would block memory aliasing
 * @param cUnit the CompilationUnit
 * @param bv the BitVector representing the BasicBlocks of the loop
 * @return whether or not the loop contains any non supported opcodes in its BasicBlocks
 */
static bool containsUnsupportedOpcodes (const CompilationUnit *cUnit, BitVector *bv)
{
    //Get highest bit, it's also the only one set (checked by the gate)
    int highest = dvmHighestBitSet (bv);

    //Get the BasicBlock
    BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(& (cUnit->blockList), highest);

    //Paranoid
    assert (bb != 0);

    //Currently we refuse:
    //  - returns, calls, move results, throw, switch
    //  - execute inline, new array, monitor
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get Dalvik instruction
        DecodedInstruction &insn = mir->dalvikInsn;

        switch (insn.opcode)
        {
            //These are more paranoid than anything because they should not appear without an invoke
            //And we ignore invokes already
            case OP_MOVE_RESULT:
            case OP_MOVE_RESULT_WIDE:
            case OP_MOVE_RESULT_OBJECT:
            case OP_MOVE_EXCEPTION:

            //Refuse returns
            case OP_RETURN_VOID:
            case OP_RETURN:
            case OP_RETURN_WIDE:
            case OP_RETURN_OBJECT:
            case OP_RETURN_VOID_BARRIER:

            //Refuse new instance, new array, and array length
            case OP_NEW_INSTANCE:
            case OP_NEW_ARRAY:
            case OP_ARRAY_LENGTH:

            //Refuse throw
            case OP_THROW:

            //Refuse switches
            case OP_PACKED_SWITCH:
            case OP_SPARSE_SWITCH:

            //Refuse invokes
            case OP_INVOKE_VIRTUAL:
            case OP_INVOKE_VIRTUAL_RANGE:
            case OP_INVOKE_INTERFACE:
            case OP_INVOKE_INTERFACE_RANGE:
            case OP_INVOKE_OBJECT_INIT_RANGE:
            case OP_INVOKE_VIRTUAL_QUICK:
            case OP_INVOKE_VIRTUAL_QUICK_RANGE:
            case OP_INVOKE_SUPER_RANGE:
            case OP_INVOKE_DIRECT_RANGE:
            case OP_INVOKE_STATIC_RANGE:
            case OP_INVOKE_SUPER_QUICK_RANGE:
            case OP_INVOKE_SUPER:
            case OP_INVOKE_DIRECT:
            case OP_INVOKE_STATIC:
            case OP_INVOKE_SUPER_QUICK:

            //Refuse breakpoints, throw, execute inline
            case OP_BREAKPOINT:
            case OP_THROW_VERIFICATION_ERROR:
            case OP_EXECUTE_INLINE:
            case OP_EXECUTE_INLINE_RANGE:
                //It does contain an unsupported opcode
                return true;

            default:
                break;
        }
    }

    //All good
    return false;
}

/**
 * @brief Used to determine if the loop is a very simple one: not nested, one basic block, no unsupported instructions
 * @param cUnit the Compilation Unit
 * @param info The loop to test
 * @param data Data to pass to the testing function (unused)
 * @return Returns whether the loop is very simple
 */
static bool isVerySimpleLoop (const CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    if (info->getNested () != 0)
    {
        return false;
    }

    //Right now, we will refuse anything that has more than one block
    BitVector *bv = const_cast<BitVector *> (info->getBasicBlocks ());

    if (dvmCountSetBits (bv) > 1)
    {
        return false;
    }

    //Finally we don't support every type of opcodes here, there are some that would make aliasing impossible
    if (containsUnsupportedOpcodes (cUnit, bv) == true)
    {
        return false;
    }

    return true;
}


//The following gate accepts only very simple loops: one basicblock, no nesting
bool dvmCompilerVerySimpleLoopGateWithLoopInfo (const CompilationUnit *cUnit, LoopInformation * loopInfo)
{
    //We don't have enough information to determine if we have a simple loop
    if (loopInfo == 0)
    {
        return false;
    }

    //Check solely if the given loop is very simple
    return isVerySimpleLoop (cUnit, loopInfo, 0);
}

//The following gate accepts only very simple loops: one basicblock, no nesting
bool dvmCompilerVerySimpleLoopGate (const CompilationUnit *cUnit, Pass *curPass)
{
    //Only do something if we have the loop information
    LoopInformation *info = cUnit->loopInformation;

    //Unused parameter
    (void) curPass;

    if (info != 0)
    {
        if (info->iterateWithConst (cUnit, isVerySimpleLoop) == true)
        {
            return true;
        }
    }
    return false;
}

bool LoopInformation::isInvariant (unsigned int ssa) const
{
    //If we don't have the bitvector, we don't know. So be conservative
    if (variants == 0)
    {
        return false;
    }

    return dvmIsBitSet (variants, ssa) == false;
}

bool LoopInformation::canThrow (const CompilationUnit *cUnit) const
{
    //Get the BasicBlock vector for this loop
    BitVector *blocks = getBasicBlocks ();

    //Iterate through them
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit (blocks, &bvIterator);
    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        //Go through its instructions
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Get dalvik instruction
            DecodedInstruction &insn = mir->dalvikInsn;

            int flags = dvmCompilerGetOpcodeFlags (insn.opcode);

            if ( (flags & kInstrCanThrow) != 0)
            {
                return true;
            }
        }
    }

    //It is fine, no instructions can throw
    return false;
}

bool LoopInformation::guaranteedToThrowFirstIteration (const CompilationUnit *cUnit) const
{
    //Get the BasicBlock vector for this loop
    BitVector *blocks = getBasicBlocks ();

    //Iterate through them
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit (blocks, &bvIterator);

    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Get Dalvik instruction
            DecodedInstruction &insn = mir->dalvikInsn;

            int dexFlags = dvmCompilerGetOpcodeFlags (insn.opcode);

            //If instruction cannot throw, then we continue
            if ((dexFlags & kInstrCanThrow) == 0)
            {
                continue;
            }

            //If this MIR is invariant but can throw, then let's see if it is guaranteed to throw
            //in the first iteration (peeled iteration)
            if (mir->invariant == true)
            {
                continue;
            }

            //If we have a divide with literal, then it is guaranteed to throw first iteration if
            //divisor is 0 and not changing. So continue.
            if (insn.opcode == OP_DIV_INT_LIT16
                    || insn.opcode == OP_REM_INT_LIT16
                    || insn.opcode == OP_DIV_INT_LIT8
                    || insn.opcode == OP_REM_INT_LIT8)
            {
                continue;
            }

            //Get Dataflow flags
            long long dfFlags = dvmCompilerDataFlowAttributes[insn.opcode];

            //If the bytecode contains null and range checks
            if ( (dfFlags & DF_HAS_NR_CHECKS) != 0)
            {
                //If we have marked it to not get a null check or bound check, we can ignore it
                int mask = (MIR_IGNORE_NULL_CHECK | MIR_IGNORE_RANGE_CHECK);

                //We need both ignores to be turned on in order to continue
                if ( (mir->OptimizationFlags & mask) == mask)
                {
                    continue;
                }
            }

            //If the bytecode contains a null check
            if ( (dfFlags & DF_HAS_OBJECT_CHECKS) != 0)
            {
                //If we have marked it to not get a null check, we can ignore it
                if ( (mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) != 0)
                {
                    continue;
                }
            }

            //Is it a getter or a setter
            bool isGetterSetter = ( (dfFlags & (DF_IS_SETTER | DF_IS_GETTER) ) != 0);

            //If we have an instance getter/setter we can prove that if memory location is invariant,
            //we are guaranteed to throw in the peeled iteration
            if (isGetterSetter == true)
            {
                bool variant = dvmCompilerCheckVariant (mir, getVariants (), dvmCompilerGetStartUseIndex (insn.opcode));

                if (variant == false)
                {
                    continue;
                }
            }

            return false;
        }
    }
    return true;
}

bool LoopInformation::hasInvoke (const CompilationUnit *cUnit) const
{
    //Get the BasicBlock vector for this loop
    BitVector *blocks = getBasicBlocks ();

    //Iterate through them
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

        BasicBlock *bb = (BasicBlock*) (dvmGrowableListGetElement(&cUnit->blockList, blockIdx));

        //Paranoid
        if (bb == 0)
        {
            break;
        }

        //Go through its instructions
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Get dalvik instruction
            DecodedInstruction &insn = mir->dalvikInsn;

            int flags = dvmCompilerGetOpcodeFlags (insn.opcode);

            //Check whether it is an invoke
            if ( (flags & kInstrInvoke) != 0)
            {
                return true;
            }
        }
    }

    //It is fine, no invoke instructions seen
    return false;
}

/* Get number of basic IVs */
int LoopInformation::getNumBasicIV (const CompilationUnit* cUnit)
{
    InductionVariableInfo *ivInfo;
    //Get the IV list
    GrowableList* ivList = & (getInductionVariableList ());
    unsigned int i;
    unsigned int number = 0;
    for (i = 0; i < ivList->numUsed; i++) {
        ivInfo = GET_ELEM_N(ivList, InductionVariableInfo*, i);
        if (ivInfo->isBasicIV ()) {
            number++;
        }
    }

    return number;
}

/**
  * @brief set the countUpLoop info for this loop and return countUpLoop
  * @return whether this loop is a count up loop
  */
bool LoopInformation::getCountUpLoop(void)
{
    unsigned int number = 0;

    GrowableListIterator ivIter;
    dvmGrowableListIteratorInit (&inductionVariableList, &ivIter);

    while (true)
    {
        InductionVariableInfo *ivInfo =
                reinterpret_cast<InductionVariableInfo *> (dvmGrowableListIteratorNext (&ivIter));

        //Break when we reach the end
        if (ivInfo == 0)
        {
            break;
        }

        //Only look at basic induction variables
        if (ivInfo->isBasicIV () == true)
        {
            number++;

            //If we have a BIV with 0 increment, then assume we don't have a count up loop.
            //We possibly have an infinite loop.
            if (ivInfo->loopIncrement == 0)
            {
               countUpLoop = false;
               break;
            }

            //If this is the second BIV we found, then we need to do more work to figure
            //what kind of loop we have. Until this is extended, return false for now.
            if (number > 1)
            {
                countUpLoop = false;
                break;
            }

            //We have a count up loop if the increment is greater than 0
            countUpLoop = (ivInfo->loopIncrement > 0);
        }
    }

    return countUpLoop;
}


//Sink a vector of instructions
void LoopInformation::sinkInstructions (CompilationUnit *cUnit, std::vector<MIR *> &insns) const
{
    //We need to sink to the exit loops and the backward chaining cell

    //First, handle the exit loops
    BitVector *bv = getExitLoops ();

    //Add the instructions to the basic blocks defined by the vector
    dvmCompilerPrependInstructionsToBasicBlocks (cUnit, bv, insns);

    //Now get the backward chaining cells
    bv = getBackwardBranches ();

    //Add the instructions to the basic blocks defined by the vector
    dvmCompilerPrependInstructionsToBasicBlocks (cUnit, bv, insns);
}

//Sink an instruction
void LoopInformation::sinkInstruction (CompilationUnit *cUnit, MIR *insn) const
{
    //Create a vector
    std::vector<MIR *> insns;

    //Push the unique instruction
    insns.push_back (insn);

    //Call generic function
    sinkInstructions (cUnit, insns);
}

// Handle the new copies: link any block to preheader to the entry's copy
void LoopInformation::handlePredecessor (CompilationUnit *cUnit, std::map<BasicBlock*, BasicBlock *> &associations)
{
    //Get the entry block
    BasicBlock *entry = getEntryBlock ();

    //Get the preheader
    BasicBlock *preheader = getPreHeader ();

    //Paranoid
    assert (entry != 0 && preheader != 0);

    //Get entry's copy
    BasicBlock *copyEntry = associations[entry];

    //Paranoid
    assert (copyEntry != 0);

    //Now go through all the predecessors
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit (preheader->predecessors, &bvIterator);
    while (true)
    {
        BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);

        //Check if done
        if (bb == 0)
        {
            break;
        }

        //Fix the links
        if (bb->taken == preheader)
        {
            bb->taken = copyEntry;
        }

        if (bb->fallThrough == preheader)
        {
            bb->fallThrough = copyEntry;
        }
    }

}

/**
 * @brief Peel a loop
 */
void LoopInformation::peelLoopHelper (CompilationUnit *cUnit)
{
    //Get the BasicBlocks
    BitVector *blocks = getBasicBlocks ();

    //Go through each block
    BitVectorIterator bvIterator;

    //The easiest way to do this is to simply copy them all
    //  - Copy all BasicBlock and mark in the maps
    std::map<BasicBlock *, BasicBlock *> associations;

    dvmBitVectorIteratorInit ( (BitVector *) blocks, &bvIterator);
    while (true)
    {
        BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);

        //Check if done
        if (bb == 0)
        {
            break;
        }

        //Copy the BasicBlock
        BasicBlock *copy = dvmCompilerCopyBasicBlock (cUnit, bb);

        //Mark it as peeled
        copy->peeled = true;

        //Set it as a peeled block of the loop
        dvmSetBit(peeledBlocks, copy->id);

        //Mark it in the association
        associations[bb] = copy;

        //Reset Null and Bound checks flags in a copy
        int resetFlags = MIR_IGNORE_NULL_CHECK | MIR_IGNORE_RANGE_CHECK;
        dvmCompilerResetOptimizationFlags (copy, resetFlags);
    }

    //Ok we copied everything, now through them again and update child links
    for (std::map<BasicBlock *, BasicBlock *>::iterator it = associations.begin (); it != associations.end (); it++)
    {
        BasicBlock *copy = it->second;

        //Because we might have 0, 0 in the map, check here but we should never have (!0), 0 in the map
        assert (it->first == 0 || (it->first != 0 && copy != 0));

        //Now we should update all out links, rules are as follows:
        //    if child is backward - copied bb should point to pre-header
        //    if child is exit     - copied bb should point to newly created block (copy of that exit)
        //    if child is in loop  - copied bb should point to copied child
        //    otherwise something went wrong
        ChildBlockIterator childIter (copy);
        for (BasicBlock **childPtr = childIter.getNextChildPtr ();
                childPtr != 0;
                childPtr = childIter.getNextChildPtr ())
        {
            //Get the child
            BasicBlock *child = *childPtr;

            //ChildBlockIterator should not return 0
            assert (child != 0);

            if (associations.count (child) > 0)
            {
                *childPtr = associations[child];
                continue;
            }

            if (dvmIsBitSet (getExitLoops (), child->id) == true)
            {
                BasicBlock *loopExitCopy = dvmCompilerNewBBinCunit(cUnit, child->blockType);
                loopExitCopy->taken = child->taken;
                loopExitCopy->fallThrough = child->fallThrough;
                *childPtr = loopExitCopy;
                continue;
            }

            if (dvmIsBitSet (getBackwardBranches (), child->id) == true)
            {
                *childPtr = getPreHeader ();
                continue;
            }
        }
    }

    //All children are updated
    //Now we need to attach the copied entry block on top of the preheader of the loop
    handlePredecessor (cUnit, associations);
}

//Count the instructions
unsigned int LoopInformation::countInstructions (CompilationUnit *cUnit)
{
    unsigned int res = 0;

    //Get the BasicBlocks
    BitVector *blocks = getBasicBlocks ();

    //Iterate through the basic blocks
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit ( (BitVector *) blocks, &bvIterator);
    while (true)
    {
        BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);

        //Check if done
        if (bb == 0)
        {
            break;
        }

        //Count the instructions in the BasicBlock
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            res++;
        }
    }

    //Return accumulator
    return res;
}

//Peel the loop
bool LoopInformation::peelLoop (CompilationUnit *cUnit)
{
    if (peeledBlocks != 0)
    {
        //We have already peeled this loop
        return true;
    }

    //Only peel if innermost
    if (getNested () != 0)
    {
        return false;
    }

    //Also check the size
    unsigned int count = countInstructions (cUnit);

    if (count >= JIT_MAX_TRACE_LEN / 2)
    {
        return false;
    }

    //Initialize the peeled blocks bitvector
    peeledBlocks = dvmCompilerAllocBitVector (1, true);

    //Actually peel the loop
    peelLoopHelper (cUnit);

    return true;

}

static bool invalidatePeelingHelper (LoopInformation *info, void *data)
{
    BitVector *bv = info->getPeeledBlocks ();
    if (bv != 0)
    {
        dvmClearAllBits (bv);
    }
    return true;
}

//Invalidate all the peeling
void LoopInformation::invalidatePeeling (void)
{
    iterate (invalidatePeelingHelper);
}

bool LoopInformation::isUniqueIVIncrementingBy1 ()
{
    GrowableList* ivList = & (getInductionVariableList ());
    unsigned int number = 0;

    for (unsigned int i = 0; i < ivList->numUsed; i++)
    {
        InductionVariableInfo *ivInfo;

        ivInfo = GET_ELEM_N (ivList, InductionVariableInfo*, i);

        //Is it a basic IV?
        if (ivInfo->isBasicIV () == true)
        {
            //Increment count
            number++;

            //Is the increment not 1?
            if (ivInfo->loopIncrement != 1)
            {
                return false;
            }

            //If we already had one, bail
            if (number > 1)
            {
                return false;
            }
        }
    }

    //Result is: we did get only one right?
    return (number == 1);
}

void LoopInformation::addInstructionsToExits (CompilationUnit *cUnit, const std::vector<MIR *> &insns)
{
    //We need to sink to the exit loops and the backward chaining cell

    //First, handle the exit loops
    BitVector *bv = getExitLoops ();

    //Add the instructions to the basic blocks defined by the vector
    dvmCompilerPrependInstructionsToBasicBlocks (cUnit, bv, insns);

    //Now get the backward chaining cells
    bv = getBackwardBranches ();

    //Add the instructions to the basic blocks defined by the vector
    dvmCompilerPrependInstructionsToBasicBlocks (cUnit, bv, insns);
}

void LoopInformation::addInstructionToExits (CompilationUnit *cUnit, MIR *mir)
{
    //Create a vector
    std::vector<MIR *> insns;

    //Push the unique one
    insns.push_back (mir);

    //Call generic function
    addInstructionsToExits (cUnit, insns);
}

bool LoopInformation::isSSARegLeavesLoop (const CompilationUnit *cUnit, const int ssaReg) const
{
    int dalvikReg = dvmExtractSSARegister (cUnit, ssaReg);
    int regVersion = dvmExtractSSASubscript (cUnit, ssaReg);

    //Get the exits vector for this loop
    BitVector *blocks = getExitLoops ();

    // No exits => infinte loop
    if (blocks == 0)
    {
        return false;
    }

    //Iterate through them
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit (blocks, &bvIterator);

    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        int exitRegVersion = DECODE_SUB (bb->dataFlowInfo->dalvikToSSAMapEntrance[dalvikReg]);
        if (regVersion == exitRegVersion)
        {
            return true;
        }
    }
    return false;
}
