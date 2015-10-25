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

#include <map>

#include "Dalvik.h"
#include "CompilerInternals.h"
#include "Dataflow.h"
#include "Utility.h"

/**
 * @brief Used to replace child of basic block and update predecessors.
 * @param newChild The new child for this basic block.
 * @param parent The basic block whose child should be replaced.
 * @param childPtr The pointer to old child block which needs to be updated with new child.
 */
static void replaceChild (BasicBlock *newChild, BasicBlock *parent, BasicBlock **childPtr)
{
    assert (childPtr != 0);

    //Get the old child
    BasicBlock *oldChild = *childPtr;

    //Set the new child
    *childPtr = newChild;

    //Update the predecessors
    dvmCompilerUpdatePredecessors (parent, oldChild, newChild);
}

/**
 * @brief Used to create a new basic block.
 * @param blockType The block type to create.
 * @param blockId The block id for this new block.
 * @return Returns the newly created block.
 * @warning If using this function to create a BB, you must make absolutely sure that there are no
 * clashes for the blockId if putting the blocks in the same list.
 */
static BasicBlock *createNewBB (BBType blockType, int blockId)
{
    //Call the backend, it might want to allocate the BasicBlock itself
    BasicBlock *bb = dvmCompilerArchSpecificNewBB();

    // If architecture specific BB creator returns null, we need to actually
    // create a BasicBlock.
    if (bb == 0)
    {
        bb = static_cast<BasicBlock *> (dvmCompilerNew(sizeof(BasicBlock), true));
    }

    bb->blockType = blockType;
    bb->id = blockId;
    bb->predecessors = dvmCompilerAllocBitVector(blockId > 32 ? blockId : 32,
                                                 true /* expandable */);
    bb->requestWriteBack = dvmCompilerAllocBitVector(1, true);
    return bb;
}

/* Allocates a new basic block and adds it to the block list. Does not update cUnit's numBlocks. */
BasicBlock *dvmCompilerNewBBinList (GrowableList &blockList, BBType blockType)
{
    //To create a unique id we get the size of the block list
    int blockId = dvmGrowableListSize (&blockList);

    //Create the new block
    BasicBlock *newBlock = createNewBB (blockType, blockId);

    //Add it to the block list
    dvmInsertGrowableList (&blockList, (intptr_t) newBlock);

    return newBlock;
}

/* Allocates a new basic block and adds it to the compilation unit */
BasicBlock *dvmCompilerNewBBinCunit (CompilationUnit *cUnit, BBType blockType)
{
    // Create a new BB in the cUnit's block list
    BasicBlock *newBlock = dvmCompilerNewBBinList (cUnit->blockList, blockType);

    //Update the number of blocks in cUnit
    cUnit->numBlocks = dvmGrowableListSize (&(cUnit->blockList));

    return newBlock;
}

/* Used to hide a basic block from block list. It ensures that all CFG links to this block are severed */
void dvmCompilerHideBasicBlock (GrowableList &blockList, BasicBlock *bbToHide)
{
    //Paranoid
    if (bbToHide == 0)
    {
        return;
    }

    //First lets make it a dalvik bytecode block so it doesn't have any special meaning
    bbToHide->blockType = kDalvikByteCode;

    //Mark it as hidden
    bbToHide->hidden = true;

    //Detach it from its MIRs so we don't generate code for them. Also detached MIRs
    //are updated to know that they no longer have a parent.
    for (MIR *mir = bbToHide->firstMIRInsn; mir != 0; mir = mir->next)
    {
        mir->bb = 0;
    }
    bbToHide->firstMIRInsn = 0;
    bbToHide->lastMIRInsn = 0;

    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bbToHide->predecessors, &bvIterator);

    //Now go through its predecessors to detach it
    for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, blockList);
            predBB != 0;
            predBB = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, blockList))
    {
        //Iterate through children of predecessor
        ChildBlockIterator childIter (predBB);

        for (BasicBlock **childPtr = childIter.getNextChildPtr ();
                childPtr != 0;
                childPtr = childIter.getNextChildPtr ())
        {
            //Get the child
            BasicBlock *child = *childPtr;

            //Paranoid
            assert (child != 0);

            //Is the child equal to the BB we are hiding
            if (child == bbToHide)
            {
                //Replace child with null child
                replaceChild (0, predBB, childPtr);
            }
        }
    }

    //Iterate through children of bb we are hiding
    ChildBlockIterator successorChildIter (bbToHide);

    for (BasicBlock **childPtr = successorChildIter.getNextChildPtr ();
            childPtr != 0;
            childPtr = successorChildIter.getNextChildPtr ())
    {
        //Replace child with null child
        replaceChild (0, bbToHide, childPtr);
    }
}

/* Allocate a new MIR */
MIR *dvmCompilerNewMIR (void)
{
    //Allocate the new MIR on the arena
    MIR *newMir = static_cast<MIR *> (dvmCompilerNew (sizeof (*newMir), true));

    //Since we zero'ed out all fields, there's nothing else we can or
    //need to initialize. So simply return the new MIR.
    return newMir;
}

/* Insert an MIR instruction to the end of a basic block */
void dvmCompilerAppendMIR(BasicBlock *bb, MIR *mir)
{
    //If this mir has a parent BB already, we need to remove it from there
    if (mir->bb != 0)
    {
        bool removed = dvmCompilerRemoveMIR (mir);
        (void) removed;
        assert (removed == true);
    }

    if (bb->firstMIRInsn == NULL) {
        assert(bb->lastMIRInsn == NULL);
        bb->lastMIRInsn = bb->firstMIRInsn = mir;
        mir->prev = mir->next = NULL;
    } else {
        bb->lastMIRInsn->next = mir;
        mir->prev = bb->lastMIRInsn;
        mir->next = NULL;
        bb->lastMIRInsn = mir;
    }

    //Set BB for mir
    mir->bb = bb;
}

/* Insert an MIR instruction to the head of a basic block */
void dvmCompilerPrependMIR(BasicBlock *bb, MIR *mir)
{
    //If this mir has a parent BB already, we need to remove it from there
    if (mir->bb != 0)
    {
        bool removed = dvmCompilerRemoveMIR (mir);
        (void) removed;
        assert (removed == true);
    }

    if (bb->firstMIRInsn == NULL) {
        assert(bb->lastMIRInsn == NULL);
        bb->lastMIRInsn = bb->firstMIRInsn = mir;
        mir->prev = mir->next = NULL;
    } else {
        bb->firstMIRInsn->prev = mir;
        mir->next = bb->firstMIRInsn;
        mir->prev = NULL;
        bb->firstMIRInsn = mir;
    }

    //Set BB for mir
    mir->bb = bb;
}

/* Insert an MIR instruction before the specified MIR */
void dvmCompilerInsertMIRBefore (BasicBlock *bb, MIR *mirToInsertBefore, MIR *newMIR)
{
    //Paranoid
    if (newMIR == 0)
    {
        //We have no work to do
        return;
    }

    //If this mir has a parent BB already, we need to remove it from there
    if (newMIR->bb != 0)
    {
        bool removed = dvmCompilerRemoveMIR (newMIR);
        (void) removed;
        assert (removed == true);
    }

    //If the mir to insert before is null, we insert at beginning of block
    if (mirToInsertBefore == 0)
    {
        dvmCompilerPrependMIR (bb, newMIR);

        //We have finished the work
        return;
    }

    MIR *prevMIR = mirToInsertBefore->prev;

    if (prevMIR != 0)
    {
        //We are inserting a new MIR after the previous one
        prevMIR->next = newMIR;
    }
    else
    {
        //Current MIR was first in block but now we are inserting before it
        bb->firstMIRInsn = newMIR;
    }

    newMIR->prev = prevMIR;
    newMIR->next = mirToInsertBefore;
    mirToInsertBefore->prev = newMIR;

    //Set BB for mir
    newMIR->bb = bb;
}

/* Insert an MIR instruction after the specified MIR */
void dvmCompilerInsertMIRAfter (BasicBlock *bb, MIR *mirToInsertAfter, MIR *newMIR)
{
    //Paranoid
    if (newMIR == 0)
    {
        //We have no work to do
        return;
    }

    //If this mir has a parent BB already, we need to remove it from there
    if (newMIR->bb != 0)
    {
        bool removed = dvmCompilerRemoveMIR (newMIR);
        (void) removed;
        assert (removed == true);
    }

    //If the mir to insert before is null, we insert at end of block
    if (mirToInsertAfter == 0)
    {
        dvmCompilerAppendMIR (bb, newMIR);

        //We have finished the work
        return;
    }

    newMIR->next = mirToInsertAfter->next;
    mirToInsertAfter->next = newMIR;
    newMIR->prev = mirToInsertAfter;

    if (newMIR->next) {
        /* Is not the last MIR in the block */
        newMIR->next->prev = newMIR;
    } else {
        /* Is the last MIR in the block */
        bb->lastMIRInsn = newMIR;
    }

    //Set BB for mir
    newMIR->bb = bb;
}

/*
 * @brief Detach MIRs starting from mirChainStart from BB mirChainStart belongs to.
 * @param mir MIR starting the chain to be removed.
 */
static void detachLinkedMIRsFromBB(MIR *mirChainStart)
{
    assert (mirChainStart != 0);
    BasicBlock *mirChainBlock = mirChainStart->bb;

    if (mirChainBlock != 0)
    {
        if (mirChainBlock->firstMIRInsn == mirChainStart)
        {
            mirChainBlock->firstMIRInsn = 0;
            mirChainBlock->lastMIRInsn = 0;
        }
        else
        {
            mirChainBlock->lastMIRInsn = mirChainStart->prev;
            mirChainBlock->lastMIRInsn->next = 0;
            mirChainStart->prev = 0;
        }
    }
}

/*
 * @brief For each MIR in chained MIRs starting from mirChainStart sets its BB to be bb.
 * @param bb target BB.
 * @param mir MIR starting the chain.
 * @return the last MIR in chain.
 */
static MIR* assignLinkedMIRsToBB(BasicBlock *bb, MIR *mirChainStart)
{
    assert (mirChainStart != 0);
    assert (bb != 0);

    MIR *mirChainEnd = mirChainStart;
    while (mirChainEnd->next != 0)
    {
        //Set up the new BB of this mir
        mirChainEnd->bb = bb;

        mirChainEnd = mirChainEnd->next;
    }
    //Set up the new BB for the last MIR and return it
    mirChainEnd->bb = bb;
    return mirChainEnd;
}

void dvmCompilerMoveLinkedMIRsAfter (BasicBlock *bb, MIR *mirToInsertAfter, MIR *mirChainStart)
{
    //Paranoid
    if (mirChainStart == 0)
    {
        //We have no work to do
        return;
    }

    //If mirChainStart belongs to some BB we should detach it and tail
    detachLinkedMIRsFromBB (mirChainStart);

    //Now go through each MIR to make it part of this new block and also to find lastMIR.
    MIR *mirChainEnd =  assignLinkedMIRsToBB(bb, mirChainStart);

    //Now attach a chain to new BB
    //Check if we have a first MIR because if we don't we append to front
    if (bb->firstMIRInsn == 0)
    {
        assert(bb->lastMIRInsn == 0);
        bb->firstMIRInsn = mirChainStart;
        bb->lastMIRInsn = mirChainEnd;
        assert (mirChainEnd->next == 0);
    }
    else
    {
        //If we don't have a MIR to insert after, then it must be the case we want to insert to end of BB
        if (mirToInsertAfter == 0)
        {
            mirToInsertAfter = bb->lastMIRInsn;
        }

        //If we are here mirToInsertAfter is a parameter or end of BB which is not empty
        assert (mirToInsertAfter != 0);

        //Keep track of the next for the MIR we need to insert after
        MIR *mirToInsertAfterNext = mirToInsertAfter->next;

        //Chain start
        mirToInsertAfter->next = mirChainStart;
        mirChainStart->prev = mirToInsertAfter;

        //Chain end
        mirChainEnd->next = mirToInsertAfterNext;
        if (mirToInsertAfterNext != 0)
        {
            mirToInsertAfterNext->prev = mirChainEnd;
        }
        else
        {
            //The last instruction now is the chain end
            bb->lastMIRInsn = mirChainEnd;
        }
    }
}

void dvmCompilerMoveLinkedMIRsBefore (BasicBlock *bb, MIR *mirToInsertBefore, MIR *mirChainStart)
{
    //Paranoid
    if (mirChainStart == 0)
    {
        //We have no work to do
        return;
    }

    //If mirChainStart belongs to some BB we should detach it and tail
    detachLinkedMIRsFromBB (mirChainStart);

    //Now go through each MIR to make it part of this new block and also to find lastMIR.
    MIR *mirChainEnd =  assignLinkedMIRsToBB(bb, mirChainStart);

    //Now attach a chain to new BB
    //Check if we have a first MIR because if we don't we append to front
    if (bb->firstMIRInsn == 0)
    {
        assert(bb->lastMIRInsn == 0);
        bb->firstMIRInsn = mirChainStart;
        bb->lastMIRInsn = mirChainEnd;
        assert (mirChainEnd->next == 0);
    }
    else
    {
        //If we don't have a MIR to insert before, then it must be the case we want to insert to beginning of BB
        if (mirToInsertBefore == 0)
        {
            mirToInsertBefore = bb->firstMIRInsn;
        }

        //If we are here mirToInsertBefore is a parameter or beginning of BB which is not empty
        assert (mirToInsertBefore != 0);

        //Keep track of the next for the MIR we need to insert after
        MIR *mirToInsertBeforePrev = mirToInsertBefore->prev;

        //Chain end first
        mirToInsertBefore->prev = mirChainEnd;
        mirChainEnd->next = mirToInsertBefore;

        //Chain start now
        mirChainStart->prev = mirToInsertBeforePrev;
        if (mirToInsertBeforePrev != 0)
        {
            mirToInsertBeforePrev->next = mirChainStart;
        }
        else
        {
            //The last instruction now is the chain end
            bb->firstMIRInsn = mirChainStart;
        }
    }
}
/*
 * Append an LIR instruction to the LIR list maintained by a compilation
 * unit
 */
void dvmCompilerAppendLIR(CompilationUnit *cUnit, LIR *lir)
{
    if (cUnit->firstLIRInsn == NULL) {
        assert(cUnit->lastLIRInsn == NULL);
        cUnit->lastLIRInsn = cUnit->firstLIRInsn = lir;
        lir->prev = lir->next = NULL;
    } else {
        cUnit->lastLIRInsn->next = lir;
        lir->prev = cUnit->lastLIRInsn;
        lir->next = NULL;
        cUnit->lastLIRInsn = lir;
    }
}

/*
 * Insert an LIR instruction before the current instruction, which cannot be the
 * first instruction.
 *
 * prevLIR <-> newLIR <-> currentLIR
 */
void dvmCompilerInsertLIRBefore(LIR *currentLIR, LIR *newLIR)
{
    assert(currentLIR->prev != NULL);
    LIR *prevLIR = currentLIR->prev;

    prevLIR->next = newLIR;
    newLIR->prev = prevLIR;
    newLIR->next = currentLIR;
    currentLIR->prev = newLIR;
}

/*
 * Insert an LIR instruction after the current instruction, which cannot be the
 * first instruction.
 *
 * currentLIR -> newLIR -> oldNext
 */
void dvmCompilerInsertLIRAfter(LIR *currentLIR, LIR *newLIR)
{
    newLIR->prev = currentLIR;
    newLIR->next = currentLIR->next;
    currentLIR->next = newLIR;
    newLIR->next->prev = newLIR;
}

/*
 * @brief Remove a MIR using its internal BasicBlock pointer
 * @param mir MIR to be removed (null allowed)
 * @return Returns true if successfully removed.
 */
bool dvmCompilerRemoveMIR (MIR *mir)
{
    //Paranoid
    if (mir == 0)
    {
        return false;
    }

    //Call generic function
    return dvmCompilerRemoveMIR (mir->bb, mir);
}

/*
 * @brief Remove an MIR from a BasicBlock
 * @param bb the BasicBlock
 * @param mir the MIR
 * @return whether it succeeded
 */
bool dvmCompilerRemoveMIR (BasicBlock *bb, MIR *mir)
{
    //Paranoid
    if (bb == 0 || mir == 0)
    {
        return false;
    }

    //Find the MIR: this makes sure we are in the right BB
    MIR *current = 0;

    for (current = bb->firstMIRInsn; current != 0; current = current->next)
    {
        //If found: break
        if (current == mir)
        {
            break;
        }
    }

    //Did we find it?
    if (current != 0)
    {
        MIR *prev = current->prev;
        MIR *next = current->next;

        //Just update the links of prev and next and current is almost gone
        if (prev != 0)
        {
            prev->next = next;
        }

        if (next != 0)
        {
            next->prev = prev;
        }

        //Exceptions are if first or last mirs are invoke
        if (bb->firstMIRInsn == current)
        {
            bb->firstMIRInsn = next;
        }

        if (bb->lastMIRInsn == current)
        {
            bb->lastMIRInsn = prev;
        }

        //Remove bb
        mir->bb = 0;

        //Found it and removed it
        return true;
    }

    //We did not find it
    return false;
}

/**
 * @brief Add instructions to the end of BasicBlock
 * @param bb the BasicBlock
 * @param toAdd a vector of Instructions to add to bb
 */
void dvmCompilerAddInstructionsToBasicBlock (BasicBlock *bb, const std::vector<MIR *> &toAdd)
{
    //If it is nil, we don't do anything
    if (bb == 0)
    {
        return;
    }

    for (std::vector<MIR *>::const_iterator it = toAdd.begin (); it != toAdd.end (); it++)
    {
        MIR *newMIR = dvmCompilerCopyMIR (*it);

        //Add a copy of each MIR
        dvmCompilerAppendMIR (bb, newMIR);
    }
}

/**
 * @brief Insert instructions at the start of BasicBlock
 * @param bb the BasicBlock
 * @param toAdd a vector of Instructions to add to bb
 */
void dvmCompilerPrependInstructionsToBasicBlock (BasicBlock *bb, const std::vector<MIR *> &toAdd)
{
    //If it is nil, we don't do anything
    if (bb == 0)
    {
        return;
    }

    for (std::vector<MIR *>::const_reverse_iterator it = toAdd.rbegin (); it != toAdd.rend (); it++)
    {
        MIR *newMIR = dvmCompilerCopyMIR (*it);

        //Add a copy of each MIR
        dvmCompilerPrependMIR (bb, newMIR);
    }
}

/**
 * @brief Add instructions to the end of BasicBlock in a BitVector
 * @param cUnit the CompilationUnit
 * @param basicBlocks a BitVector containing the BasicBlocks
 * @param toAdd a vector of Instructions to add to bb
 */
void dvmCompilerAddInstructionsToBasicBlocks (CompilationUnit *cUnit, BitVector *basicBlocks,
        const std::vector<MIR *> &toAdd)
{
    //Iterate on the exit blocks
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (basicBlocks, &bvIterator);

    while (true)
    {
        //Get the block index
        int blockIdx = dvmBitVectorIteratorNext (&bvIterator);

        //If we are done
        if (blockIdx == -1)
        {
            break;
        }

        BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, blockIdx);

        //Now add to the basic block
        dvmCompilerAddInstructionsToBasicBlock (bb, toAdd);
    }
}

/**
 * @brief Insert instructions at the start of BasicBlock in a BitVector
 * @param cUnit the CompilationUnit
 * @param basicBlocks a BitVector containing the BasicBlocks
 * @param toAdd a vector of Instructions to add to bb
 */
void dvmCompilerPrependInstructionsToBasicBlocks (CompilationUnit *cUnit, BitVector *basicBlocks,
        const std::vector<MIR *> &toAdd)
{
    //Iterate on the exit blocks
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (basicBlocks, &bvIterator);

    while (true)
    {
        //Get the block index
        int blockIdx = dvmBitVectorIteratorNext (&bvIterator);

        //If we are done
        if (blockIdx == -1)
        {
            break;
        }

        BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, blockIdx);

        //Now add to the basic block
        dvmCompilerPrependInstructionsToBasicBlock (bb, toAdd);
    }
}

bool dvmCompilerReplaceChildBasicBlock (BasicBlock *newChild, BasicBlock *parent, BasicBlock *oldChild)
{
    //Parent must always be set
    assert (parent != 0);

    //If we don't have a child to replace then we cannot do anything
    if (oldChild == 0)
    {
        return false;
    }

    //Assume we don't successfully replace child
    bool replacedChild = false;

    //Iterate through block's children
    ChildBlockIterator childIter (parent);
    for (BasicBlock **childPtr = childIter.getNextChildPtr(); childPtr != 0; childPtr = childIter.getNextChildPtr())
    {
        //Get the child
        BasicBlock *child = *childPtr;

        //If we have a match for replacement
        if (child == oldChild)
        {
            replaceChild (newChild, parent, childPtr);

            //We successfully replaced child
            replacedChild = true;
        }
    }

    return replacedChild;
}

bool dvmCompilerReplaceChildBasicBlock (BasicBlock *newChild, BasicBlock *parent, ChildBlockType childType)
{
    //Parent must always be set
    assert (parent != 0);

    BasicBlock **childPtr = 0;

    //Decide which child we need to update
    switch (childType)
    {
        case kChildTypeFallthrough:
            childPtr = &(parent->fallThrough);
            break;
        case kChildTypeTaken:
            childPtr = &(parent->taken);
            break;
        default:
            ALOGD ("JIT_INFO: Unsupported child type %d in replacement of basic block children.", childType);
            return false;
    }

    //Do the actual work
    replaceChild (newChild, parent, childPtr);

    //We have replaced child
    return true;
}

bool dvmCompilerInsertBasicBlockBetween (BasicBlock *newBlock, BasicBlock *parent, BasicBlock *child,
        bool updateChildPredecessors)
{
    //Paranoid
    if (parent == 0 || child == 0)
    {
        return false;
    }

    //Find the link
    if (parent->taken == child)
    {
        parent->taken = newBlock;
        dvmCompilerUpdatePredecessors (parent, child, newBlock);
    }
    else
    {
        if (parent->fallThrough == child)
        {
            parent->fallThrough = newBlock;
            dvmCompilerUpdatePredecessors (parent, child, newBlock);
        } else {
            // We probably have a switch, so we should find successor to child and update it
            bool found = false;
            if (parent->successorBlockList.blockListType != kNotUsed) {
                GrowableListIterator iterator;
                dvmGrowableListIteratorInit(&parent->successorBlockList.blocks,
                                        &iterator);
                while (true) {
                    SuccessorBlockInfo *successorBlockInfo = (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);
                    if (successorBlockInfo == 0)
                    {
                        break;
                    }

                    BasicBlock *succBB = successorBlockInfo->block;
                    if (succBB == child)
                    {
                        successorBlockInfo->block = newBlock;
                        dvmCompilerUpdatePredecessors (parent, child, newBlock);
                        found = true;
                        break;
                    }
                }
            }

            // We did not find successor, so nothing to insert
            if (found == false)
            {
                return false;
            }
        }
    }

    //Link to child
    newBlock->fallThrough = child;
    newBlock->taken = 0;
    if (updateChildPredecessors == true)
    {
        dvmCompilerUpdatePredecessors (newBlock, newBlock->fallThrough, child);
    }

    //Success
    return true;
}

/**
 * @brief Reset MIR optimization flags in BasicBlock
 * @param bb the BasicBlock
 * @param resetFlags the flags to reset
 */
void dvmCompilerResetOptimizationFlags (const BasicBlock *bb, int resetFlags)
{
    // reset flags for all MIRs in bb
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        mir->OptimizationFlags &= (~resetFlags);
    }
}


/**
 * @brief Copy a BasicBlock
 * @param cUnit the CompilationUnit
 * @param old the old BasicBlock
 * @return the new BasicBlock
 */
BasicBlock *dvmCompilerCopyBasicBlock (CompilationUnit *cUnit, const BasicBlock *old)
{
    BasicBlock *resultBB = dvmCompilerNewBBinCunit (cUnit, old->blockType);

    //We don't do a superficial copy here because it would lead to a lot of things
    //To clean up. Let us do it by hand instead

    //Copy in taken and fallthrough
    resultBB->fallThrough = old->fallThrough;
    resultBB->taken = old->taken;

    //Copy successor links if needed
    resultBB->successorBlockList.blockListType = old->successorBlockList.blockListType;
    if (resultBB->successorBlockList.blockListType != kNotUsed) {
        dvmInitGrowableList(&resultBB->successorBlockList.blocks,
                dvmGrowableListSize (&old->successorBlockList.blocks));

        GrowableListIterator iterator;
        dvmGrowableListIteratorInit(&(((BasicBlock*) old)->successorBlockList.blocks), &iterator);
        while (true) {
            SuccessorBlockInfo *successorBlockInfoOld = (SuccessorBlockInfo *) dvmGrowableListIteratorNext(&iterator);
            if (successorBlockInfoOld == 0)
            {
                break;
            }

            SuccessorBlockInfo *successorBlockInfoNew = (SuccessorBlockInfo *) dvmCompilerNew(sizeof(SuccessorBlockInfo), false);
            memcpy (successorBlockInfoNew, successorBlockInfoOld, sizeof(SuccessorBlockInfo));
            dvmInsertGrowableList(&resultBB->successorBlockList.blocks, (intptr_t) successorBlockInfoNew);
        }
    }

    //Copy offset, method
    resultBB->startOffset = old->startOffset;
    resultBB->containingMethod = old->containingMethod;

    //Now copy instructions
    for (MIR *mir = old->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get a copy first
        MIR *copy = dvmCompilerCopyMIR (mir);

        //Append it
        dvmCompilerAppendMIR (resultBB, copy);
    }

    //Copy the writebacks as well
    if (old->requestWriteBack != 0 && resultBB->requestWriteBack != 0)
    {
        dvmCopyBitVector (resultBB->requestWriteBack, old->requestWriteBack);
    }

    return resultBB;
}

/**
 * @brief Copy a MIR instruction
 * @param orig the original MIR we wish to copy
 * @return the copy of the MIR, without a next, prev, ssa representation or a BasicBlock
 */
MIR *dvmCompilerCopyMIR (MIR *orig)
{
    MIR *copy = static_cast<MIR *> (dvmCompilerNew (sizeof (*copy), true));

    //Superficial copy because it's easier in this case
    *copy = *orig;

    //Now remove prev, next, and ssaRep
    copy->next = 0;
    copy->prev = 0;
    copy->ssaRep = 0;

    //No bb for this copy
    copy->bb = 0;
    //But we do have a copyFrom
    copy->copiedFrom = orig;

    return copy;
}

u2 * dvmCompilerGetDalvikPC (CompilationUnit* cUnit, const MIR *mir)
{
    //If we are not provided a MIR, then we cannot figure out a PC
    if (mir == 0)
    {
        return 0;
    }

    //The MIR knows its own method
    const Method *sourceMethod = mir->nesting.sourceMethod;

    //If the MIR had no source method, then we cannot figure out a valid PC for it
    if (sourceMethod == 0)
    {
        /* We should guarantee mir has valid
         * sourceMethod if mir is inlined
         */
        int flags = mir->OptimizationFlags;
        if ((flags & MIR_INLINED_PRED) != 0 || (flags & MIR_INLINED) != 0)
        {
            ALOGD("JIT_INFO: No sourceMethod for an inlined mir");
            assert(false);
            return 0;
        }
        /* Consider sourceMethod = cUnit->method for non inlined mirs
         * untill we support exceptions for non-simple inlining
         * TODO: fix this as soon as inlining scope gets expanded
         */
        sourceMethod = cUnit->method;
    }

    //The dalvik PC is the pointer to instructions of method plus the offset
    u2 *dalvikPC = const_cast<u2 *> (sourceMethod->insns) + mir->offset;

    return dalvikPC;
}

/**
 * @brief Find the highest MIR in the color
 * @param elem an instruction
 * @return the first instruction in the color following the color link list
 */
MIR *dvmCompilerFindHighestMIRInColor (MIR *elem)
{
    //Paranoid
    assert (elem != 0);

    while (elem->color.prev != 0)
    {
        //Go to the previous one
        elem = elem->color.prev;
    }

    return elem;
}

/**
 * @brief Find the lowest MIR in the color
 * @param elem an instruction
 * @return the last instruction in the color following the color link list
 */
MIR *dvmCompilerFindLowestMIRInColor (MIR *elem)
{
    //Paranoid
    assert (elem != 0);

    while (elem->color.next != 0)
    {
        //Go to the next one
        elem = elem->color.next;
    }

    return elem;
}

/* Determines if ssa reg define is live out of current basic block. */
bool dvmCompilerIsSsaLiveOutOfBB (CompilationUnit *cUnit, BasicBlock *bb, int ssaReg)
{
    //Paranoid
    if (bb == 0)
    {
        //Since we don't have a BB, be safe and say that the ssa is live out
        return true;
    }

    //In order to determine if the ssa reg is live out, we scan all MIRs in backward order. If we find a def
    //that has a different ssa number but same dalvik register, then the ssa reg user asked about cannot
    //be the one that is live out of this BB.

    //Get the dalvik register
    int dalvikReg = dvmExtractSSARegister (cUnit, ssaReg);

    //Walk through the MIRs backwards
    for (MIR *mir = bb->lastMIRInsn; mir != 0; mir = mir->prev)
    {
        //Get ssa rep
        SSARepresentation *ssaRep = mir->ssaRep;

        //Go through the defines for this MIR
        for (int i = 0; i < ssaRep->numDefs; i++)
        {
            //Paranoid
            assert(ssaRep->defs != 0);

            //Get the ssa reg
            int defSsaReg = ssaRep->defs[i];

            //Get dalvik reg
            int defDalvikReg = dvmExtractSSARegister (cUnit, defSsaReg);

            //Compare dalvik regs
            if (dalvikReg == defDalvikReg)
            {
                //We found a def of the register that we are being asked about

                //If ssa regs don't match, then the one user asked about is not
                //live out. If they do match, then it is live out.
                if (ssaReg == defSsaReg)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
    }

    //If we get to this point we couldn't find a define of register user asked about.
    //Let's assume the user knows what he's doing so we can be safe and say that if we
    //couldn't find a def, it is live out.
    return true;
}

/**
 * @brief Generates a move MIR.
 * @param sourceVR The virtual register to move from.
 * @param destVR The virtual register to move to.
 * @param wide Flag if we want a wide move. Both registers must be wide if
 * this flag is set.
 * @return Returns the mir that represents the desired move.
 */
MIR *dvmCompilerNewMoveMir (int sourceVR, int destVR, bool wide)
{
    MIR *mir = dvmCompilerNewMIR ();

    //Decide on the move opcode based on wideness
    mir->dalvikInsn.opcode = wide ? OP_MOVE_WIDE : OP_MOVE;

    //Update operands
    mir->dalvikInsn.vA = static_cast<u4> (destVR);
    mir->dalvikInsn.vB = static_cast<u4> (sourceVR);

    //Return the newly generated mir
    return mir;
}

bool dvmCompilerUsesAreInvariant (const MIR *mir, const BitVector *variants, int skipUses)
{
    //Ok now we care about the uses: if they are invariant, the result is as well
    SSARepresentation *ssaRep = mir->ssaRep;

    //If no ssaRep, we know nothing
    if (ssaRep == 0)
    {
        return false;
    }

    //Get uses
    int numUses = ssaRep->numUses;

    //In some scenarios we want to skip some of the uses when making the decision
    //for whether uses are invariant. So let's do some sanity checking here.
    if (skipUses < 0)
    {
        skipUses = 0;
    }

    for (int i = skipUses; i < numUses; i++)
    {
        //Get local use
        int use = ssaRep->uses[i];

        //Is it a variant already?
        if (dvmIsBitSet (variants, use) == true)
        {
            return false;
        }
    }

    //If we got to here, then it is invariant, just return true
    return true;
}

bool dvmCompilerCheckVariant (MIR *elem, BitVector *variants, int skipUses)
{
    //If variants is 0, then we consider elem as being variant
    if (variants == 0)
    {
        return true;
    }

    //In the general case, we only care about the uses
    return (dvmCompilerUsesAreInvariant (elem, variants, skipUses) == false);
}

/**
 * @brief Used to rewrite instructions in 3rc format.
 * @param dalvikInsn The dalvik instruction to update. Guaranteed to not be updated if function returns false.
 * @param oldToNew The mapping of old VRs to the new ones
 * @param foundOperand Updated by function if operand is found
 * @return Returns false if problem is encountered.
 */
static bool rewrite3rc (DecodedInstruction &dalvikInsn, const std::map<int, int> &oldToNew, bool &foundOperand)
{
    //The number of arguments is guaranteed to be in vA for this format
    u4 count = dalvikInsn.vA;

    //vC holds the start register. The range uses registers from vC to (vC + vA - 1)
    u4 vC = dalvikInsn.vC;

    u4 newVC = 0;
    {
        std::map<int, int>::const_iterator iter = oldToNew.find (vC);

        if (iter == oldToNew.end ())
        {
            //We have no new mapping for vC so we cannot rewrite anything
            return false;
        }
        else
        {
            newVC = iter->second;
        }
    }

    //Now check that all VRs have a consistent old to new mapping
    for (unsigned int vR = vC + 1; vR < vC + count; vR++)
    {
        std::map<int, int>::const_iterator iter = oldToNew.find (vR);

        if (iter == oldToNew.end ())
        {
            //We don't have an entry and therefore we don't know the new
            return false;
        }
        else
        {
            u4 newVR = iter->second;

            //Now check that the range difference is the same
            if ((vR - vC) != (newVR - newVC))
            {
                //Range is not the same
                return false;
            }
        }
    }

    //If we made it to this point, all the checks passed and therefore we can update vC
    dalvikInsn.vC = newVC;
    foundOperand = true;

    return true;
}

/**
 * @brief Used to rewrite instructions in 35c format.
 * @param dalvikInsn The dalvik instruction to update.
 * @param oldToNew The mapping of old VRs to the new ones
 * @param foundOperand Updated by function if operand is found
 */
static void rewrite35c (DecodedInstruction &dalvikInsn, const std::map<int, int> &oldToNew, bool &foundOperand)
{
    //The number of arguments is guaranteed to be in vA for this format
    u4 count = dalvikInsn.vA;

    //Go through each of the operands to look for a match for the old VR.
    for (u4 operand = 0; operand < count; operand++)
    {
        //Iterate through the mapping of old to new VRs
        for (std::map<int, int>::const_iterator iter = oldToNew.begin (); iter != oldToNew.end (); iter++)
        {
            u4 oldVR = iter->first;
            u4 newVR = iter->second;

            //If we found a match, update it now
            if (dalvikInsn.arg[operand] == oldVR)
            {
                //Update the operand and mark it as found
                dalvikInsn.arg[operand] = newVR;
                foundOperand = true;
                break;
            }
        }
    }
}

/**
 * @brief Used to rename a single virtual register
 * @details The user passes a reference to the register to update and this function looks
 * in the oldToNew mapping for entry with key that has old register
 * @param oldToNew The mapping of old VRs to the new ones
 * @param regToRewrite Reference to the register name which needs updated
 * @param foundOperand Updated by function if operand is found
 */
static void rewriteVR (const std::map<int, int> &oldToNew, u4 &regToRewrite, bool &foundOperand)
{
    //Iterate through the mapping of old to new VRs
    for (std::map<int, int>::const_iterator iter = oldToNew.begin (); iter != oldToNew.end (); iter++)
    {
        u4 oldVR = iter->first;
        u4 newVR = iter->second;

        if (regToRewrite == oldVR)
        {
            regToRewrite = newVR;
            foundOperand = true;
            break;
        }
    }
}

bool dvmCompilerRewriteMirVRs (DecodedInstruction &dalvikInsn, const std::map<int, int> &oldToNew, bool onlyUses)
{
    //Phi nodes get recomputed automatically and thus we don't need to rewrite the uses.
    if (static_cast<ExtendedMIROpcode> (dalvikInsn.opcode) == kMirOpPhi)
    {
        //We return true because there's nothing we need to in terms of rewriting
        return true;
    }

    //Get dataflow flags
    long long dfAttributes = dvmCompilerDataFlowAttributes[dalvikInsn.opcode];

    //If we have an extended MIR then reject because rewriting support has not been added
    if ((dfAttributes & DF_FORMAT_EXT_OP) != 0)
    {
        return false;
    }

    //If we are rewriting only uses, then we cannot have overlapping use and def because we are updating the use and not the def
    if (onlyUses == true)
    {
        //Check if we have overlap
        if ((dfAttributes & DF_A_IS_DEFINED_REG) != 0 && (dfAttributes & DF_A_IS_USED_REG) != 0)
        {
            return false;
        }
    }

    //Be pessimistic and assume we won't find operand
    bool foundOperand = false;

    //Check if we have instruction that is in range form
    if ((dfAttributes & DF_FORMAT_3RC) != 0)
    {
        if (rewrite3rc (dalvikInsn, oldToNew, foundOperand) == false)
        {
            //Something went wrong so we just pass along the error
            return false;
        }
    }

    //Now check if it is format35c which may have multiple uses.
    bool format35c = ((dfAttributes & DF_FORMAT_35C) != 0);

    if (format35c == true)
    {
        rewrite35c (dalvikInsn, oldToNew, foundOperand);
    }

    //We write to vA if it is a register and we're not just rewriting uses, or if vA is used and not defined
    bool writevA = (onlyUses == false && (dfAttributes & DF_A_IS_REG) != 0)
            || ((dfAttributes & DF_A_IS_USED_REG) != 0 && (dfAttributes & DF_A_IS_DEFINED_REG) == 0);

    //Check if vA matches operand we are looking for
    if (writevA == true)
    {
        rewriteVR (oldToNew, dalvikInsn.vA, foundOperand);
    }

    //Check if vB matches operand we are looking for.
    if ((dfAttributes & DF_B_IS_REG) != 0)
    {
        rewriteVR (oldToNew, dalvikInsn.vB, foundOperand);
    }

    //Check if vC matches operand we are looking for. If we have 35c form,
    //the "this" pointer might be in vC and also in arg[0]. Since we don't
    //know who will use this decoded instruction, we try to update vC as well.
    if ((dfAttributes & DF_C_IS_REG) != 0 || format35c == true)
    {
        rewriteVR (oldToNew, dalvikInsn.vC, foundOperand);
    }

    bool hasUses = ((dfAttributes & DF_HAS_USES) != 0);

    //If we are rewriting only uses and the instruction has them, then we expect to find the operand
    if (onlyUses == true && hasUses == true)
    {
        //If we did not find operand we failed the rewrite
        return foundOperand;
    }
    else
    {
        //If we aren't rewriting only uses, then we are generically doing VR rewriting.
        //Thus if we make it to this point all rewriting went fine. We don't check foundOperand because
        //some bytecodes (like gotos) have no virtual register usages/defines.
        return true;
    }
}

/**
 * @brief Rewrite the uses of a Dalvik instruction structure.
 * @param dalvikInsn The dalvik instruction to update.
 * @param oldVR the old VR that we want to rewrite
 * @param newVR the new VR we want to use
 * @return whether the rewrite was successful
 */
static bool rewriteUses (DecodedInstruction &dalvikInsn, int oldVR, int newVR)
{
    //Create a map of old to new VR
    std::map<int, int> oldToNew;
    oldToNew[oldVR] = newVR;

    //Ask for MIR rewriting for just the uses
    return dvmCompilerRewriteMirVRs (dalvikInsn, oldToNew, true);
}

/**
 * @brief Fixes the uses when the VR previously defined changed.
 * @param oldVR the old VR that we want to rewrite
 * @param newVR the new VR we want to use
 * @param chain The first link in chain of uses.
 * @param newDecodedInst Updated by function to contain the rewritten * instruction for each mir.
 * @param constrainToThisBB do we constrain the algorithm to a given BasicBlock
 * @return Returns true if all uses were rewritten correctly.
 */
static bool rewriteUses (int oldVR, int newVR, SUsedChain *chain, std::map<MIR *, DecodedInstruction> &newDecodedInst, BasicBlock *constrainToThisBB)
{
    //Walk the chain
    for (; chain != 0; chain = chain->nextUse)
    {
        MIR *mir = chain->mir;

        //Paranoid
        if (mir == 0)
        {
            //This should never happen but we skip it if we have no MIR to rewrite
            continue;
        }

        //If we are only doing rewrites of MIRs in same BB, then we need to skip other MIRs now
        if (constrainToThisBB != 0 && mir->bb != constrainToThisBB)
        {
            continue;
        }

        //Make a copy of the dalvik instruction that can be updated
        DecodedInstruction dalvikInsn = mir->dalvikInsn;

        //Call the rewrite helper function
        bool res = rewriteUses (dalvikInsn, oldVR, newVR);

        //If we failed, fail everything
        if (res == false)
        {
            return false;
        }

        //Keep track of this rewrite in order to commit it later
        newDecodedInst[mir] = dalvikInsn;
    }

    //If we make it here, we succeeded
    return true;
}

//Rewrite uses of the def specified by mir
bool dvmCompilerRewriteMirUses (MIR *mir, int oldVR, int newVR, bool shouldRemainInSameBB)
{
    //Used to keep track of the rewritten instruction before we commit it
    std::map<MIR *, DecodedInstruction> newDecodedInst;

    if (mir == 0)
    {
        return false;
    }

    //Get local copy of ssa representation
    SSARepresentation *ssaRep = mir->ssaRep;

    //We want to be able to find the uses
    if (ssaRep == 0 || ssaRep->numDefs == 0 || ssaRep->usedNext == 0)
    {
        return false;
    }

    //Determine if we need to contain the rewrites to the same BB
    BasicBlock *constrainToThisBB = (shouldRemainInSameBB == true) ? mir->bb : 0;

    //Follow the uses of the first def. When we get here we know we have
    //at least one def.
    SUsedChain *chain = ssaRep->usedNext[0];

    //Now fix the uses of this updated define
    bool success = rewriteUses (oldVR, newVR, chain, newDecodedInst, constrainToThisBB);

    //No use to keep going if we failed
    if (success == false)
    {
        return false;
    }

    //If we have a wide VR, check whether we can rewrite the uses.
    //Technically we need to fail rewriting the uses because the
    //wide part of the VR should never appear as an operand. But we can
    //be nice and even if this scenario happens, we can fix the uses.
    if (ssaRep->numDefs >= 2)
    {
        //Prepare the wide part of the old and new VRs
        int oldVRWide = oldVR + 1;
        int newVRWide = newVR + 1;

        //Get the chain for the wide define
        chain = ssaRep->usedNext[1];

        //Now try to rewrite the uses of the wide part.
        //Don't affect success whether or not we fail here.
        rewriteUses (oldVRWide, newVRWide, chain, newDecodedInst, constrainToThisBB);
    }

    //We can commit our rewrites if we succeed all of them. We only make it
    //to this point if we have succeeded.
    std::map<MIR *, DecodedInstruction>::const_iterator updateIter;
    for (updateIter = newDecodedInst.begin ();
            updateIter != newDecodedInst.end (); updateIter++)
    {
        MIR *mir = updateIter->first;
        const DecodedInstruction &dalvikInsn = updateIter->second;

        //Rewrite the decoded instruction for the mir
        mir->dalvikInsn = dalvikInsn;
    }

    //If we make it here, we succeeded
    return true;
}

//Rewrite Mir for one instruction
bool dvmCompilerRewriteMirDef (MIR *mir, int oldVR, int newVR, bool shouldRewriteUses, bool shouldRemainInSameBB)
{
    //Paranoid
    assert (mir != 0);

    //Get a local copy of the decoded instruction
    DecodedInstruction dalvikInsn = mir->dalvikInsn;

    //Get dataflow flags
    long long dfAttributes = dvmCompilerDataFlowAttributes[dalvikInsn.opcode];

    //Check to see if we have defs for this MIR
    if ((dfAttributes & DF_HAS_DEFS) == 0)
    {
        //This function can only rewrite the defs
        return false;
    }

    //Check to make sure that the define matches desired VR to replace
    if (mir->dalvikInsn.vA != static_cast<u4> (oldVR))
    {
        return false;
    }

    //Check if use overlaps the define
    if ((dfAttributes & (DF_UA | DF_UA_WIDE)) != 0)
    {
        //We should be able to fix this scenario but for now reject it
        return false;
    }

    //Now that we know we are fine to do the replacement, let's do it
    dalvikInsn.vA = static_cast<u4> (newVR);

    //Check if we need to rewrite the uses now that use the new def
    if (shouldRewriteUses == true)
    {
        if (dvmCompilerRewriteMirUses (mir, oldVR, newVR, shouldRemainInSameBB) == false)
        {
            return false;
        }
    }

    //Now we can commit update of def
    mir->dalvikInsn = dalvikInsn;

    //If we make it here, we succeeded
    return true;
}

OpcodeFlags dvmCompilerGetOpcodeFlags (int opcode)
{
    OpcodeFlags flags = 0;

    //First check if this is an extended opcode
    if (opcode >= static_cast<int> (kNumPackedOpcodes))
    {
        ExtendedMIROpcode extOp = static_cast<ExtendedMIROpcode> (opcode);

        switch (extOp)
        {
            case kMirOpPhi:
            case kMirOpRegisterize:
                //No branching semantics, always goes to next instruction
                flags = kInstrCanContinue;
                break;
            case kMirOpPunt:
                //Only branching semantics because it unconditionally punts to interpreter
                flags = kInstrCanBranch;
                break;
            case kMirOpNullNRangeUpCheck:
            case kMirOpNullNRangeDownCheck:
            case kMirOpLowerBound:
            case kMirOpNullCheck:
            case kMirOpBoundCheck:
            case kMirOpCheckStackOverflow:
                //Instruction can continue or it may throw
                flags = kInstrCanContinue | kInstrCanThrow;
                break;
            case kMirOpCheckInlinePrediction:
                //This extended MIR has conditional branching semantics
                flags = kInstrCanContinue | kInstrCanBranch;
                break;
            default:
                break;
        }
    }
    else
    {
        //Just get the flags provided by the dex helper
        flags = dexGetFlagsFromOpcode (static_cast<Opcode> (opcode));
    }

    return flags;
}

const char* dvmCompilerGetOpcodeName (int opcode)
{
    if (opcode < static_cast<int> (kNumPackedOpcodes))
    {
        return dexGetOpcodeName (static_cast<Opcode> (opcode));
    }
    else
    {
        ExtendedMIROpcode extOp = static_cast<ExtendedMIROpcode> (opcode);

        switch (extOp)
        {
            case kMirOpPhi:
                return "kMirOpPhi";
            case kMirOpNullNRangeUpCheck:
                return "kMirOpNullNRangeUpCheck";
            case kMirOpNullNRangeDownCheck:
                return "kMirOpNullNRangeDownCheck";
            case kMirOpLowerBound:
                return "kMirOpLowerBound";
            case kMirOpPunt:
                return "kMirOpPunt";
            case kMirOpCheckInlinePrediction:
                return "kMirOpCheckInlinePrediction";
            case kMirOpNullCheck:
                return "kMirOpNullCheck";
            case kMirOpBoundCheck:
                return "kMirOpBoundCheck";
            case kMirOpRegisterize:
                return "kMirOpRegisterize";
            case kMirOpConst128b:
                return "kMirOpConst128b";
            case kMirOpMove128b:
                return "kMirOpMove128b";
            case kMirOpPackedMultiply:
                return "kMirOpPackedMultiply";
            case kMirOpPackedAddition:
                return "kMirOpPackedAddition";
            case kMirOpPackedAddReduce:
                return "kMirOpPackedAddReduce";
            case kMirOpPackedSet:
                return "kMirOpPackedSet";
            case kMirOpCheckStackOverflow:
                return "kMirOpCheckStackOverflow";
            case kMirOpPackedSubtract:
                return "kMirOpPackedSubtract";
            case kMirOpPackedShiftLeft:
                return "kMirOpPackedShiftLeft";
            case kMirOpPackedSignedShiftRight:
                return "kMirOpPackedSignedShiftRight";
            case kMirOpPackedUnsignedShiftRight:
                return "kMirOpPackedUnsignedShiftRight";
            case kMirOpPackedAnd:
                return "kMirOpPackedAnd";
            case kMirOpPackedOr:
                return "kMirOpPackedOr";
            case kMirOpPackedXor:
                return "kMirOpPackedXor";
            case kMirOpPackedReduce:
                return "kMirOpPackedReduce";
            default:
                return "KMirUnknown";
        }
    }
}

ChildBlockIterator::ChildBlockIterator (BasicBlock *bb)
{
    //Initialize basic block
    basicBlock = bb;

    //We have not yet visited any of the children
    visitedFallthrough = false;
    visitedTaken = false;

    //Check if we have successors
    if (basicBlock != 0 && basicBlock->successorBlockList.blockListType != kNotUsed)
    {
        haveSuccessors = true;
        dvmGrowableListIteratorInit (&(basicBlock->successorBlockList.blocks), &successorIter);
    }
    else
    {
        //Static analyzers believe we fail to initialize successorIter so we set up its fields now
        successorIter.idx = 0;
        successorIter.size = 0;
        successorIter.list = 0;

        //We have no successors if the block list is unused
        haveSuccessors = false;
    }
}

BasicBlock **ChildBlockIterator::getNextChildPtr (void)
{
    //We check if we have a basic block. If we don't we cannot get next child
    if (basicBlock == 0)
    {
        return 0;
    }

    //If we haven't visited fallthrough, return that
    if (visitedFallthrough == false)
    {
        visitedFallthrough = true;

        if (basicBlock->fallThrough != 0)
        {
            return &(basicBlock->fallThrough);
        }
    }

    //If we haven't visited taken, return that
    if (visitedTaken == false)
    {
        visitedTaken = true;

        if (basicBlock->taken != 0)
        {
            return &(basicBlock->taken);
        }
    }

    //We visited both taken and fallthrough. Now check if we have successors we need to visit
    if (haveSuccessors == true)
    {
        //Get information about next successor block
        for (SuccessorBlockInfo *successorBlockInfo = (SuccessorBlockInfo *) (dvmGrowableListIteratorNext(&successorIter));
            successorBlockInfo != NULL;
            successorBlockInfo = (SuccessorBlockInfo *) (dvmGrowableListIteratorNext(&successorIter)))
        {
            // if block was replaced by zero block, take next one
            if (successorBlockInfo->block != 0)
            {
                return &(successorBlockInfo->block);
            }
        }
    }

    //If we make it here we found no remaining children
    return 0;
}

/**
 * @brief Update the predecessor information of old and new BB
 * @details This only switches predecessors, unlike dvmCompilerCalculatePredecessors()
 * which calculates predecessor information for the whole cUnit.
 * @param parent The basic block whose children are changing
 * @param oldChild The basic block which is losing the parent
 * @param newChild The basic block which is gaining the parent
 */
void dvmCompilerUpdatePredecessors (BasicBlock *parent, BasicBlock *oldChild, BasicBlock *newChild) {
    assert(parent != 0);

    //Remove association from old child
    if ((oldChild != 0) && (dvmIsBitSet(oldChild->predecessors, parent->id) == true))
    {
        dvmCompilerClearBit (oldChild->predecessors, parent->id);
    }

    //Add information for newChild
    if (newChild != 0)
    {
        dvmCompilerSetBit (newChild->predecessors, parent->id);
    }
}
