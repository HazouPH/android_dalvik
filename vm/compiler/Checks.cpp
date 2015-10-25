/*
 * Copyright (C) 2012 Intel Corporation
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

#include "Checks.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "Loop.h"
#include "LoopInformation.h"
#include "Pass.h"
#include "PassDriver.h"
#include "Utility.h"

#include <map>

#define CHECK_LOG(...)

/**
 * Note:
 *  The current file contains the hoisting pass framework, it uses two structures:
 *      - SRemoveData is the general pass information holder for the whole pass
 *      - STrackers is for a given BasicBlock, it is used to hold information passed through the pass' call chain
 *
 *  To add a new hoisting algorithm, follow the STEP keyword throughout the file, they
 *  show where to add your information (not including the two data structures)
 */

//STEP 0: Add any information required in the data structures
/**
 * @brief sRemoveData is used by the removal algorithm to remember which registers are live for each BasicBlock
 */
typedef struct sRemoveData
{
    std::map<int, BitVector*> nullChecks;                            /**< @brief map between BasicBlock id and a BitVector */
    std::map<int, std::map<int, BitVector *> > indexToArrayChecks;   /**< @brief map between BasicBlock id and a map of indices with arrays */

    bool hoistChecks;                                                /**< @brief Do we hoist the checks out or do we leave the first in place? */
    BitVector *hoistedNullChecks;                                    /**< @brief Remember which arrays we have hoisted already */
    std::map<int, BitVector *> hoistedArrayToIndexChecks;            /**< @brief Remember which array[index] we've hoisted already */
}SRemoveData;

/**
 * @brief sTrackers is used to track progress through a BasicBlock parsing
 */
typedef struct sTrackers
{
    BitVector *tempNullChecks;                          /**< @brief bit vector for the null checks guaranteed to have been done */
    std::map<int, BitVector *> *indexToArrayChecks;     /**< @brief map from index to Bitvector representing which checks have been done */

    std::map<int, std::vector<std::pair<int, int> > > replacementRegs; /**< @brief map of replacement registers, for each entry a pair (replacement, color) is provided */
    int currentColor;                                   /**< @brief Current color regarding writes to memory */
    std::map<int, std::vector<MIR *> > opcodeMap;       /**< @brief opcodeMap, keeps each MIR via a opcodeMap */

    sTrackers (void);                                   /**< @brief Constructor */
    ~sTrackers (void);                                  /**< @brief Destructor */
}STrackers;

STrackers::sTrackers (void)
{
    currentColor = 0;
    tempNullChecks = 0;
}

STrackers::~sTrackers (void)
{
    //Clear maps (not indexToArrayChecks, it isn't ours to clear)
    for (std::map<int, std::vector<std::pair<int, int> > >::iterator it = replacementRegs.begin ();
                                                                   it != replacementRegs.end ();
                                                                   it++)
    {
        std::vector<std::pair<int, int> > &v = it->second;
        v.clear ();
    }
    replacementRegs.clear ();

    //Same for opcodeMap
    for (std::map<int, std::vector<MIR *> >::iterator it = opcodeMap.begin ();
                                                      it != opcodeMap.end ();
                                                      it++)
    {
        std::vector<MIR *> &v = it->second;
        v.clear ();
    }
    opcodeMap.clear ();
}

//Forward declarations of static functions

/**
 * @brief Initialize the tempNullCheck bit vector with first predecessor
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param tempNullCheck bit vector representing which registers have been null checked
 * @param predBB predecessor block
 */
static void initializeNullChecks (CompilationUnit *cUnit, SRemoveData *removeData, BitVector *tempNullCheck, BasicBlock *predBB);

/**
 * @brief Handle new predecessor block
 * @param removeData the current SRemoveData information
 * @param tempNullCheck bit vector representing which registers have been null checked
 * @param predBB predecessor block
 */
static void handlePredecessorNull (SRemoveData *removeData, BitVector *tempNullCheck, BasicBlock *predBB);

/**
 * @brief Initialize the indexToArray map with first predecessor
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param indexToArrayChecks map between index and which array has been checked
 * @param predBB predecessor block
 */
static void initializeIndexToArray (CompilationUnit *cUnit, SRemoveData *removeData, std::map<int, BitVector *> *indexToArrayChecks, BasicBlock *predBB);

/**
 * @brief Initialize the indexToArray map with first predecessor
 * @param removeData the current SRemoveData information
 * @param indexToArrayChecks map between index and which array has been checked
 * @param predBB predecessor block
 */
static void handlePredecessorIndexToArray (SRemoveData *removeData, std::map<int, BitVector *> *indexToArrayChecks, BasicBlock *predBB);

/**
 * @brief Initialize data for the current BasicBlock
 * @param cUnit the CompilationUnit
 * @param ptrRemoveData the current SRemoveData information
 * @param bb the current BasicBlock
 * @param tracker the tracker structure for the basic block
 */
static void initializeData (CompilationUnit *cUnit, SRemoveData **ptrRemoveData, BasicBlock *bb, STrackers &tracker);

/**
 * @brief Handle the null check regarding a new MIR instruction
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param nullCheck the array index considered for nullCheck for the given mir
 * @param mir the MIR instruction
 * @param tracker the tracker structure for the basic block
 */
static void handleNullCheck (CompilationUnit *cUnit, SRemoveData *removeData, int nullCheck, MIR *mir, STrackers &tracker);


/**
 * @brief Handle the bound check regarding a new MIR instruction
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param arrayIndex the array index for the given mir
 * @param boundCheck the boundcheck index for the given mir
 * @param mir the MIR instruction
 * @param tracker the tracker structure for the basic block
 */
static void handleBoundCheck (CompilationUnit *cUnit, SRemoveData *removeData, int arrayIndex, int boundCheck, MIR *mir, STrackers &tracker);

/**
 * @brief Walk a BasicBlock, going through each instruction
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param first the first MIR instruction of the current BasicBlock
 * @param tracker the tracker structure for the basic block
 */
static void walkBasicBlock (CompilationUnit *cUnit, SRemoveData *removeData, MIR *first, STrackers &tracker);


/**
 * @brief Handle the hoisting of an array access via an index
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param mir the MIR instruction
 * @param array the array SSA register
 * @param index the array's index access SSA register
 */
static void handleIndexHoist (CompilationUnit *cUnit, SRemoveData *removeData, MIR *mir, int array, int index);

/**
 * @brief Handle the hoisting of a null check for an object access
 * @param cUnit the CompilationUnit
 * @param removeData the current SRemoveData information
 * @param mir the MIR instruction
 * @param array the array SSA register
 */
static void handleNullCheckHoist (CompilationUnit *cUnit, SRemoveData *removeData, MIR *mir, int array);

/**
 * @brief Recursive helper function for usesEqual, compares idx use and then calls itself with +1 if equal, terminates when idx >= number of uses or a difference is seen
 * @param mir the MIR instruction
 * @param other the other MIR instruction compared to
 * @param replacementRegs the replacement register map
 * @param currentColor the current color for memory writes
 * @param directMatch is the equality a direct match?
 * @param idx the index for the use array we are comparing to (default 0)
 * @return true if idx >= number of uses
 */
static bool usesEqualHelper (const MIR *mir, const MIR *other, std::map<int, std::vector <std::pair <int, int> > > &replacementRegs, int currentColor, bool &directMatch, int idx = 0);


/**
 * @brief Are two MIR using the same registers?
 * @param mir the MIR instruction
 * @param other the other MIR instruction compared to
 * @param replacementRegs the replacement register map to test different possible uses
 * @param currentColor the current color for memory writes
 * @param directMatch is the equality a direct match?
 * @return whether or not mir and other are using the same registers via a replacementReg or not
 */
static bool usesEqual (const MIR *mir, const MIR *other, std::map<int, std::vector <std::pair <int, int> > > &replacementRegs, int currentColor, bool &directMatch);


//IMPLEMENTATION

/**
 * @brief Start the removal pass, allocate the map
 * @param cUnit the CompilationUnit
 * @param curPass the Pass
 */
void dvmCompilerStartCheckRemoval (CompilationUnit *cUnit, Pass *curPass)
{
    //We only care about this, if it is a loop
    if (dvmCompilerTraceIsLoop (cUnit, curPass) == false)
    {
        return;
    }

    //STEP 1: Initialize any thing for the pass data here
    SRemoveData *data = 0;
    void *space = dvmCompilerNew (sizeof (*data), false);
    data = new (space) SRemoveData;
    curPass->setData (space);

    //By default, we don't hoist the checks yet because the entry is not really the entry of the loop
    data->hoistChecks = false;
    data->hoistedNullChecks = dvmCompilerAllocBitVector (cUnit->numSSARegs, false);
    dvmClearAllBits(data->hoistedNullChecks);
}

/**
 * @brief End the removal pass, free the map
 * @param cUnit the CompilationUnit
 * @param curPass the Pass
 */
void dvmCompilerEndCheckRemoval (CompilationUnit *cUnit, Pass *curPass)
{
    SRemoveData *data = static_cast<SRemoveData *> (curPass->getData ());

    //STEP 2: Add any free code here

    /* We don't need to free the BitVectors: they are using the compiler heap
     * Therefore, just clear the map and free the SRemoveData structure
     */
    if (data != NULL)
    {
        data->nullChecks.clear ();

        /** Clear index to Array map
         *  Same as before, no need to free the BitVectors
         */
        for (std::map<int, std::map<int, BitVector *> >::iterator it = data->indexToArrayChecks.begin ();
                it != data->indexToArrayChecks.end ();
                it++)
        {
            std::map<int, BitVector *> &myMap = it->second;
            myMap.clear ();
        }

        data->indexToArrayChecks.clear ();

        // Clear the hoisted information too
        data->hoistedArrayToIndexChecks.clear ();

        //Now clear up curPass->data
        curPass->setData (0);
    }
}

//NEXT COUPLE OF FUNCTIONS HANDLE DATA INITIALIZATION WHEN CONSIDERING A NEW BASIC BLOCK

void initializeIndexToArray (CompilationUnit *cUnit, SRemoveData *removeData,
                                    std::map<int, BitVector *> *indexToArrayChecks, BasicBlock *predBB)
{
    //Our current index to array map (in the indexToArrayChecks variable) is going to be the copy of the first one
    std::map<int, BitVector *> &predMap = removeData->indexToArrayChecks[predBB->id];

    for (std::map<int, BitVector *>::const_iterator it = predMap.begin ();
            it != predMap.end ();
            it++)
    {
        int key = it->first;
        BitVector *bv = it->second;
        //Create a new one for our new BasicBlock
        BitVector *newBV = dvmCompilerAllocBitVector (cUnit->numSSARegs, false);
        dvmCopyBitVector (newBV, bv);

        //Add it to our map
        (*indexToArrayChecks)[key] = newBV;
    }
}


void initializeNullChecks (CompilationUnit *cUnit, SRemoveData *removeData, BitVector *tempNullCheck, BasicBlock *predBB)
{
    //First handle the NULL check bitvector
    BitVector *elem = removeData->nullChecks[predBB->id];

    //Ok if we don't have this, it is because we have a backward branch, ignore it
    if (elem != NULL)
    {
        //Paranoid
        assert (cUnit->numSSARegs > 0);

        //In this case, for the null handling, we are going to be setting every bit to a potential handled
        //The intersection code will remove all the false positives
        dvmSetInitialBits (tempNullCheck, cUnit->numSSARegs - 1);

        /* tempNullCheck = tempNullCheck ^ constants already checked */
        dvmIntersectBitVectors(tempNullCheck, tempNullCheck, elem);
    }
}

void handlePredecessorNull (SRemoveData *removeData, BitVector *tempNullCheck, BasicBlock *predBB)
{
    //First handle the NULL check bitvector
    BitVector *elem = removeData->nullChecks[predBB->id];

    //Ok if we don't have this, it is because we have a backward branch, ignore it
    if (elem != NULL)
    {
        /* tempNullCheck = tempNullCheck ^ constants already checked */
        dvmIntersectBitVectors(tempNullCheck, tempNullCheck, elem);
    }
}

void handlePredecessorIndexToArray (SRemoveData *removeData, std::map<int, BitVector *> *indexToArrayChecks,
                                           BasicBlock *predBB)
{
    //Second intersection is the array access check, a little bit more tricky because it is a map first
    std::map<int, BitVector *> &predIndexToArrayChecks = removeData->indexToArrayChecks[predBB->id];

    //Now we go through ours, if we don't see it in predIndexToArray, we can remove it from ours entirely
    //We can't do it as we are iterating, so let's put it in a vector
    std::vector<int> toBeRemoved;

    for (std::map<int, BitVector *>::iterator it = predIndexToArrayChecks.begin ();
            it != predIndexToArrayChecks.end ();
            it++)
    {
        int index = it->first;
        BitVector *bv = it->second;

        //Let's see if we have it
        std::map<int, BitVector *>::iterator gotIt = indexToArrayChecks->find (index);

        if (gotIt != indexToArrayChecks->end ())
        {
            //We have more work to do then: we must do an intersection
            BitVector *ourBV = gotIt->second;
            dvmIntersectBitVectors(ourBV, ourBV, bv);

            //See if we can remove it
            if (dvmCountSetBits (ourBV) == 0)
            {
                toBeRemoved.push_back (index);
            }
        }
        else
        {
            //predIndexToArray does not have it, we can remove it entirely
            toBeRemoved.push_back (index);
        }
    }

    //Now remove the ones we don't need anymore
    for (std::vector<int>::const_iterator it = toBeRemoved.begin ();
            it != toBeRemoved.end ();
            it++)
    {
        //No need to free, the compiler heap will do it
        indexToArrayChecks->erase (*it);
    }
}

void initializeData (CompilationUnit *cUnit, SRemoveData **ptrRemoveData, BasicBlock *bb,
                            STrackers &tracker)
{
    SRemoveData *removeData = *ptrRemoveData;

    //Get the block list
    GrowableList *blockList = &cUnit->blockList;

    //Get data if in loop mode
    if (cUnit->passData != NULL)
    {
        //Get the removeData
        removeData = static_cast<SRemoveData *> (cUnit->passData);
        *ptrRemoveData = removeData;

        if (removeData != NULL)
        {
            //STEP 3: Initialize the vectors and update the pointers, update removeData if needed

            //Retrieve our indexToArrayChecks map
            tracker.indexToArrayChecks = &(removeData->indexToArrayChecks[bb->id]);

            //Set back tempNullCheck to removeData
            removeData->nullChecks[bb->id] = tracker.tempNullChecks;

            //Get pointers from tracker, more efficient than passing tracker down stream
            std::map<int, BitVector *> *indexToArrayChecks = tracker.indexToArrayChecks;
            BitVector *tempNullChecks = tracker.tempNullChecks;

            //Next step is to get the constants that have already been tested from our predecessors
            BitVectorIterator bvIterator;

            /* Iterate through the predecessors */
            dvmBitVectorIteratorInit(bb->predecessors, &bvIterator);

            int predIdx = dvmBitVectorIteratorNext(&bvIterator);

            //If we have at least one predecessor
            if (predIdx != -1)
            {
                //Get the basic block
                BasicBlock *predBB = (BasicBlock *) dvmGrowableListGetElement(blockList, predIdx);

                //STEP 4: handle first predecessor if needed

                //Call for Null checks
                initializeNullChecks (cUnit, removeData, tempNullChecks, predBB);

                //Call for IndexToArray
                initializeIndexToArray (cUnit, removeData, indexToArrayChecks, predBB);

                //Get next predecessor
                predIdx = dvmBitVectorIteratorNext(&bvIterator);

                while (predIdx != -1) {
                    //Get the basic block
                    predBB = (BasicBlock *) dvmGrowableListGetElement(blockList, predIdx);

                    //STEP 5: handle new predecessor

                    //Call for null check
                    handlePredecessorNull (removeData, tempNullChecks, predBB);

                    //Call for IndexToArray
                    handlePredecessorIndexToArray (removeData, indexToArrayChecks, predBB);

                    //Next predecessor
                    predIdx = dvmBitVectorIteratorNext(&bvIterator);
                }
            }
        }
    }
}

//NEXT COUPLE OF FUNCTIONS HANDLE THE WALKING THROUGH A BASIC BLOCK
void handleNullCheck (CompilationUnit *cUnit, SRemoveData *removeData, int nullCheck, MIR *mir, STrackers &tracker)
{
    // Do we have an index? Skip this MIR and log error if arguments are invalid
    if (nullCheck >= 0 && mir->ssaRep != 0 && mir->ssaRep->uses != 0 && nullCheck < mir->ssaRep->numUses)
    {
        int reg = mir->ssaRep->uses[nullCheck];

        //Get local variables
        BitVector *tempNullCheck = tracker.tempNullChecks;
        std::map<int, std::vector<std::pair<int, int> > > &replacementRegs = tracker.replacementRegs;
        int &currentColor = tracker.currentColor;
        //We have another possibility, is it equal to another register null checked ?
        std::vector<std::pair<int, int> > &ourRepl = replacementRegs[reg];

        bool foundOne = false;

        //Has it already been tested
        if (dvmIsBitSet (tempNullCheck, reg) != 0)
        {
            CHECK_LOG ( "Register already null checked\n");
            foundOne = true;
        }
        else
        {
            for (std::vector<std::pair<int, int> >::const_iterator it = ourRepl.begin ();
                                                                   it != ourRepl.end ();
                                                                   it++)
            {
                const std::pair<int, int> &pair = *it;
                int other = pair.first;
                int color = pair.second;

                //We care about color, can we trust this register ?
                if (color >= currentColor)
                {
                    //And if so is null checked?
                    if (dvmIsBitSet (tempNullCheck, other) != 0)
                    {
                        CHECK_LOG ( "Replacement %d already null checked\n", other);
                        foundOne = true;
                        break;
                    }
                }
            }
        }

        //Set that it is null checked
        CHECK_LOG ( "Now %d null checked\n", reg);
        dvmSetBit (tempNullCheck, reg);

        //Have we found a replacement that already did the null check
        if (foundOne == true)
        {
            //We can remove the null check then
            mir->OptimizationFlags |= MIR_IGNORE_NULL_CHECK;
        }
        else
        {
            //Hoist if requested
            handleNullCheckHoist (cUnit, removeData, mir, reg);
        }

        //Now we can go through each of our replacements and let them know
        for (std::vector<std::pair<int, int> >::const_iterator it = ourRepl.begin ();
                it != ourRepl.end ();
                it++)
        {
            const std::pair<int, int> &pair = *it;

            int other = pair.first;
            int color = pair.second;

            //If right color
            if (color >= currentColor)
            {
                CHECK_LOG ( "Also setting %d as null checked\n", other);
                dvmSetBit (tempNullCheck, other);
            }
        }
    } else {
        if (nullCheck >= 0)
        {
            // Report about invalid agruments
            if (mir->ssaRep == 0)
            {
                ALOGD ("JIT_INFO: handleNullCheck (0x%x): ssaRep is null",
                    mir->dalvikInsn.opcode);
            }
            else
            {
                if (mir->ssaRep->uses == 0)
                    ALOGD ("JIT_INFO: handleNullCheck (0x%x): ssaRep->uses is null",
                        mir->dalvikInsn.opcode);

                if (nullCheck >= mir->ssaRep->numUses)
                    ALOGD ("JIT_INFO: handleNullCheck (0x%x): nullCheck (%i) >= numUses (%i)",
                        mir->dalvikInsn.opcode, nullCheck, mir->ssaRep->numUses);
            }

            assert(mir->ssaRep != 0);
            assert(mir->ssaRep->uses != 0);
            assert(nullCheck < mir->ssaRep->numUses);
        }
    }
}

void handleBoundCheck (CompilationUnit *cUnit, SRemoveData *removeData, int nullCheck, int boundCheck,
                              MIR *mir, STrackers &tracker)
{
    // Check the bound check via indexToArrayChecks, skip this MIR and log error if agruments are invalid
    if (boundCheck >= 0
        && mir->ssaRep != 0
        && mir->ssaRep->uses != 0
        && boundCheck < mir->ssaRep->numUses
        && nullCheck < mir->ssaRep->numUses && nullCheck >= 0)
    {
        //Get local values
        int array = mir->ssaRep->uses[nullCheck];
        int index = mir->ssaRep->uses[boundCheck];

        std::vector<std::pair<int, int> > &arrayRegs = tracker.replacementRegs[array];
        int &currentColor = tracker.currentColor;

        std::map<int, BitVector *> *indexToArrayChecks = tracker.indexToArrayChecks;

        //Check the bounds: first get the right BitVector
        BitVector *bv = (*indexToArrayChecks)[index];

        if (bv == NULL)
        {
            //Create it then
            bv = dvmCompilerAllocBitVector (cUnit->numSSARegs, false);
            dvmClearAllBits(bv);

            //Set it because no one ever has here
            dvmSetBit (bv, array);
            //Finally store it
            (*indexToArrayChecks)[index] = bv;

            //Hoist if requested
            handleIndexHoist (cUnit, removeData, mir, array, index);
        }
        else
        {
            bool foundOne = false;

            //Otherwise is the bit-vector set?
            if (dvmIsBitSet (bv, array) != 0)
            {
                foundOne = true;
            }
            else
            {
                //One last test, can we find a replacement that is already bound check with this index?
                for (std::vector<std::pair<int, int> >::const_iterator it = arrayRegs.begin ();
                        it != arrayRegs.end ();
                        it++)
                {
                    const std::pair<int, int> &pair = *it;
                    int other = pair.first;
                    int color = pair.second;

                    //If color is good
                    if (color >= currentColor)
                    {
                        //If we actually have seen this one before
                        if (dvmIsBitSet (bv, other) == true)
                        {
                            foundOne = true;
                            break;
                        }
                    }
                }

                //If still not found
                if (foundOne == false)
                {
                    //Otherwise, we'll leave this one and set it here
                    dvmSetBit (bv, array);

                    //Hoist if requested
                    handleIndexHoist (cUnit, removeData, mir, array, index);
                }
            }

            if (foundOne == true)
            {
                //We can remove the null check then
                mir->OptimizationFlags |= MIR_IGNORE_RANGE_CHECK;
            }

        }

        //Ok, now go through each array equivalent register and let them know they don't need to check this register either

        for (std::vector<std::pair<int, int> >::const_iterator it = arrayRegs.begin ();
                                                               it != arrayRegs.end ();
                                                               it++)
        {
            const std::pair<int, int> &pair = *it;
            int other = pair.first;
            int color = pair.second;

            //If color is good
            if (color >= currentColor)
            {
                //Mark it
                dvmSetBit (bv, other);
            }
        }
    } else {
        if (boundCheck >= 0)
        {
            // Report about invalid arguments
            if (mir->ssaRep == 0)
            {
                ALOGD ("JIT_INFO: handleBoundCheck (0x%x): ssaRep is null",
                    mir->dalvikInsn.opcode);
            }
            else
            {
                if (mir->ssaRep->uses == 0)
                    ALOGD ("JIT_INFO: handleBoundCheck (0x%x): ssaRep->uses is null",
                        mir->dalvikInsn.opcode);

                if (boundCheck >= mir->ssaRep->numUses)
                    ALOGD ("JIT_INFO: handleBoundCheck (0x%x): boundCheck (%i) >= ssaRep->numUses (%i)",
                        mir->dalvikInsn.opcode, boundCheck, mir->ssaRep->numUses);

                if (nullCheck >= 0 && nullCheck >= mir->ssaRep->numUses)
                    ALOGD ("JIT_INFO: handleBoundCheck (0x%x): nullCheck (%i) >= ssaRep->numUses (%i)",
                        mir->dalvikInsn.opcode, nullCheck, mir->ssaRep->numUses);
            }

            if (nullCheck < 0)
                ALOGD ("JIT_INFO: handleBoundCheck (0x%x): nullCheck (%i) < 0, but boundCheck is %i",
                    mir->dalvikInsn.opcode, nullCheck, boundCheck);

            assert(mir->ssaRep != 0);
            assert(mir->ssaRep->uses != 0);
            assert(boundCheck < mir->ssaRep->numUses);
            assert(nullCheck >= 0 && nullCheck < mir->ssaRep->numUses);
        }
    }
}

void walkBasicBlock (CompilationUnit *cUnit, SRemoveData *removeData, MIR *first, STrackers &tracker)
{
    //Get local version
    int &currentColor = tracker.currentColor;
    std::map<int, std::vector<MIR *> > &opcodeMap = tracker.opcodeMap;
    std::map<int, std::vector<std::pair<int, int> > > &replacementRegs = tracker.replacementRegs;

    for (MIR *mir = first; mir != NULL; mir = mir->next)
    {
        DecodedInstruction *dInsn = &mir->dalvikInsn;
        long long dfAttributes =
            dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        CHECK_LOG ( "\nHandling %s\n",
                    dvmCompilerFullDisassembler (cUnit, mir));

        //First check if this a new memory related instruction
        if ( ( (dfAttributes & DF_IS_CALL) != 0) ||
             ( (dfAttributes & DF_CLOBBERS_MEMORY) != 0) ||
             ( (dfAttributes & DF_IS_SETTER) != 0))
        {
            currentColor++;
        }

        //Now before we start throwing things away, let's do a comparison in uses
        //First get old instructions of same type
        if (mir->ssaRep != 0)
        {
            int opcode = dInsn->opcode;
            std::vector<MIR *> &list = opcodeMap[opcode];

            //For each check if we have an equality
            for (std::vector<MIR *>::const_iterator it = list.begin ();
                    it != list.end ();
                    it++)
            {
                const MIR *other = *it;
                bool directMatch;

                CHECK_LOG ( "\nComparing it to %s\n",
                        dvmCompilerFullDisassembler (cUnit, other));

                if (usesEqual (mir, other, replacementRegs, currentColor, directMatch) == true)
                {
                    //We have a equality, then we should copy the definitions
                    int numDefs = mir->ssaRep->numDefs;
                    int *ourDefs = mir->ssaRep->defs;
                    int *theirDefs = other->ssaRep->defs;

                    for (int i = 0; i < numDefs; i++)
                    {
                        int us = ourDefs[i];
                        int them = theirDefs[i];

                        //Get vector
                        std::vector<std::pair<int, int> > &ourRepl = replacementRegs[us];

                        //If it is a direct match, put an absurdly large color value
                        //This is true because whatever happens to one will be true for the other no matter what regarding null/bound checks
                        //Otherwise, they'd have to be fetched again and would no longer be the same register anyway
                        std::pair<int, int> newElem (them,
                            (directMatch) ? 1000000 : currentColor);
                        ourRepl.push_back (newElem);

                        CHECK_LOG ( "%d is now equivalent to %d with a color of %d\n", us, them, newElem.second);
                    }
                }
            }

            //Now push it in for future same opcodes
            CHECK_LOG ( "Pushing into opcode map: %s\n", dvmCompilerFullDisassembler (cUnit, mir));
            list.push_back (mir);
        }

        int instrFlags = dvmCompilerGetOpcodeFlags (dInsn->opcode);

        /* Instruction is clean */
        if ((instrFlags & kInstrCanThrow) == 0) continue;

        /*
         * Currently we can only optimize away null and range checks. Punt on
         * instructions that can throw due to other exceptions.
         */
        if ( (dfAttributes & (DF_HAS_NR_CHECKS | DF_HAS_OBJECT_CHECKS)) == 0)
        {
            continue;
        }

        //STEP 6: add any value dependent on the DataFlow attribute
        //Set to -1 both
        int nullCheck = -1;
        int boundCheck = -1;

        //Depending on the instruction, the array and index are in different registers
        switch (dfAttributes & DF_HAS_NR_CHECKS) {
            case DF_NULL_N_RANGE_CHECK_0:
                nullCheck = 0;
                boundCheck = nullCheck + 1;
                break;
            case DF_NULL_N_RANGE_CHECK_1:
                nullCheck = 1;
                boundCheck = nullCheck + 1;
                break;
            case DF_NULL_N_RANGE_CHECK_2:
                nullCheck = 2;
                boundCheck = nullCheck + 1;
                break;
            default:
                break;
        }

        switch (dfAttributes & DF_HAS_OBJECT_CHECKS)
        {
            case DF_NULL_OBJECT_CHECK_0:
                nullCheck = 0;
                break;
            case DF_NULL_OBJECT_CHECK_1:
                nullCheck = 1;
                break;
            case DF_NULL_OBJECT_CHECK_2:
                nullCheck = 2;
                break;
            default:
                break;
        }

        //STEP 7: actually decide what to do

        //First check the null register
        //Little warning: if we are hoisting, we do want this test first before any bound checking
        //because we do not test null for arrays afterwards, we suppose it has already been done by this code
        handleNullCheck (cUnit, removeData, nullCheck, mir, tracker);

        //Now handle bound check
        handleBoundCheck (cUnit, removeData, nullCheck, boundCheck, mir, tracker);
    }
}

/**
 * @brief The removal pass: point of entry remove any redundant checks
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 */
bool dvmCompilerCheckRemoval (CompilationUnit *cUnit, BasicBlock *bb)
{
    SRemoveData *removeData = NULL;

    //Create a tracker
    STrackers tracker;

    //STEP 8: initialize tracker structure

    //Create a bit vector for this one
    tracker.tempNullChecks = dvmCompilerAllocBitVector (cUnit->numSSARegs, false);
    dvmClearAllBits(tracker.tempNullChecks);

    //There is a temporary index to array check map in case we are in trace mode
    std::map<int, BitVector *> temporaryIndexToArrayChecks;
    tracker.indexToArrayChecks = &temporaryIndexToArrayChecks;

    /* Initialize the data */
    initializeData (cUnit, &removeData, bb, tracker);

   /* Now tempNullCheck contains exactly which registers have already been tested as Null and the
    * indexToArrayChecks is up to date for bound checks
    * Let us go through the basic block and see if we can remove tests
    */
    walkBasicBlock (cUnit, removeData, bb->firstMIRInsn, tracker);

    //Clear the map before leaving
    temporaryIndexToArrayChecks.clear ();

    //We have not changed the basic block
    return false;
}

void handleIndexHoist (CompilationUnit *cUnit, SRemoveData *removeData, MIR *mir, int arrayReg, int indexReg)
{
    //We only hoist if removeData is set, we know what has been hoisted, and we do want to hoist
    if (removeData != NULL && removeData->hoistedNullChecks != NULL && removeData->hoistChecks == true)
    {
        //Ok before going anywhere, we must first check if we hoisted the arrayReg already?
        //Normally we should have but let us be paranoid
        if (dvmIsBitSet (removeData->hoistedNullChecks, arrayReg) == true)
        {
            //Actually, we only hoist this check if it is a constant (sub == 0)
            //Technically we could also hoist induction variables but this is currently done
            //in a previous pass, we could merge them however at some point
            //We do test index and array because we can only hoist it if both are constants
            int subNRegArray = dvmConvertSSARegToDalvik(cUnit, arrayReg);
            int arraySub = DECODE_SUB(subNRegArray);
            int subNRegIndex = dvmConvertSSARegToDalvik(cUnit, indexReg);
            int indexSub = DECODE_SUB(subNRegIndex);

            //Now whether we can hoist depends first on the index: is it a constant or an invariant
            bool canHoist = ( (indexSub == 0) || (dvmCompilerIsRegConstant (cUnit, indexReg) == true));

            //Then the array must be invariant
            canHoist = (canHoist && arraySub == 0);

            if (canHoist == true)
            {
                //Get entry block
                BasicBlock *entry = cUnit->entryBlock;

                if (entry != NULL)
                {
                    //We can remove the null check then
                    // Either we already have hoisted it and we are safe
                    // Or we are going to
                    mir->OptimizationFlags |= MIR_IGNORE_RANGE_CHECK;

                    //Get bitvector for the array, we use arrays here because we assume there are less arrays than indices..
                    BitVector *bv = removeData->hoistedArrayToIndexChecks[arrayReg];

                    //For readability, let's reset canHoist to true though it is already
                    canHoist = true;

                    //If not found, we can try to hoist
                    if (bv == NULL)
                    {
                        //But first create it
                        bv = dvmCompilerAllocBitVector (cUnit->numSSARegs, false);
                        dvmClearAllBits (bv);
                        dvmSetBit (bv, indexReg);
                    }
                    else
                    {
                        //Then have we already bound checked with indexReg?
                        // If yes, we don't hoist here
                        // If no, we hoist
                        if (dvmIsBitSet (bv, indexReg) == true)
                        {
                            canHoist = false;
                        }
                        else
                        {
                            //Technically we have not hoisted yet but the following tests will always have
                            //the same outcome, so bail before finding that out next time
                            dvmSetBit (bv, indexReg);
                        }
                    }

                    if (canHoist == true)
                    {
                        //Actually generate the hoisting code
                        MIR *boundCheck = static_cast<MIR *> (dvmCompilerNew (sizeof (*boundCheck), true));
                        boundCheck->dalvikInsn.opcode = static_cast<Opcode> (kMirOpBoundCheck);
                        //We only care about the object register and index register
                        boundCheck->dalvikInsn.vA = arrayReg;

                        //The rest of the bound check depends on whether it is a register or constant
                        if (indexSub == 0)
                        {
                            boundCheck->dalvikInsn.arg[0] = MIR_BOUND_CHECK_REG;
                            boundCheck->dalvikInsn.arg[1] = indexReg;
                        }
                        else
                        {
                            boundCheck->dalvikInsn.arg[0] = MIR_BOUND_CHECK_CST;
                            boundCheck->dalvikInsn.arg[1] = (*cUnit->constantValues)[indexReg];
                        }

                        boundCheck->dalvikInsn.vC = 0;
                        boundCheck->ssaRep = mir->ssaRep;
                        dvmCompilerAppendMIR(entry, boundCheck);
                    }
                }
            }
        }
    }

}

bool dvmCompilerGenerateNullCheckHoist (BasicBlock *hoistToBB, int objectReg)
{
    //Assume we will not hoist
    bool hoisted = false;

    //Check if we have a BB to hoist to
    if (hoistToBB != 0)
    {
        //Now check if we can determine PC in case of exception
        if (hoistToBB->fallThrough != 0 && hoistToBB->fallThrough->firstMIRInsn != 0)
        {
            const MIR *firstMir = hoistToBB->fallThrough->firstMIRInsn;

            //Do a sanity check on the block offset before we hoist. It should match the offset of first instruction
            if (hoistToBB->fallThrough->startOffset == firstMir->offset)
            {
                MIR *nullCheck = dvmCompilerNewMIR ();

                //Create null check
                nullCheck->dalvikInsn.opcode = static_cast<Opcode> (kMirOpNullCheck);
                nullCheck->dalvikInsn.vA = objectReg;

                //For exception purpose, we set the offset to match the offset in the block following entry
                nullCheck->offset = hoistToBB->fallThrough->startOffset;

                //We also make sure that it has the same nesting information
                nullCheck->nesting = firstMir->nesting;

                //Now append the MIR to the BB
                dvmCompilerAppendMIR (hoistToBB, nullCheck);

                //Mark that hoisting was successful
                hoisted = true;
            }
        }
    }

    return hoisted;
}

void handleNullCheckHoist (CompilationUnit *cUnit, SRemoveData *removeData, MIR *mir, int objectReg)
{
    //Do we hoist ?
    if (removeData != NULL && removeData->hoistChecks == true)
    {
        int subNRegObject = dvmConvertSSARegToDalvik(cUnit, objectReg);
        int objectSub = DECODE_SUB(subNRegObject);

        //If so, we can actually only hoist it if the sub is 0
        //0 meaning that it has never been assigned (and thus never changed in the loop)
        if (objectSub == 0)
        {
            //Get entry block
            BasicBlock *entry = cUnit->entryBlock;

            bool hoisted = dvmIsBitSet (removeData->hoistedNullChecks, objectReg);

            //Have we hoisted it already?
            if (hoisted == false)
            {
                //Though we have not hoisted yet, the reasons below will be true for any subsequent request
                dvmSetBit (removeData->hoistedNullChecks, objectReg);

                //Actually generate the hoisting code
                hoisted = dvmCompilerGenerateNullCheckHoist (entry, objectReg);
            }

            if (hoisted == true)
            {
                //If we hoisted the null check the we can eliminate it
                mir->OptimizationFlags |= MIR_IGNORE_NULL_CHECK;
            }
        }
    }
}

bool usesEqualHelper (const MIR *mir, const MIR *other, std::map<int, std::vector <std::pair <int, int> > > &replacementRegs, int currentColor, bool &directMatch, int idx)
{
    //Get both uses array
    int *ours = mir->ssaRep->uses;
    int *theirs = other->ssaRep->uses;

    CHECK_LOG ( "Helper compare %d out of %d\n", idx, mir->ssaRep->numUses);

    if (idx == mir->ssaRep->numUses)
    {
        //Found it, we finished and we found an equal MIR
        CHECK_LOG ( "They are the same!\n");
        return true;
    }

    int us = ours[idx];
    int them = theirs[idx];

    //Direct Match ?
    if (us == them)
    {
        directMatch = true;
        CHECK_LOG ( "They are the same: direct match!\n");
        return true;
    }

    //Now find the replacements
    std::vector<std::pair <int, int> > &usRepl = replacementRegs[us];
    std::vector<std::pair <int, int> > &themRepl = replacementRegs[them];

    //If no replacement, we are done
    if (usRepl.size () == 0 && themRepl.size () == 0)
    {
        CHECK_LOG ( "Use size is not the same\n");
        return false;
    }

    //Otherwise, to simplify the code, if usRepl is empty switch
    if (usRepl.size () == 0)
    {
        return usesEqualHelper (other, mir, replacementRegs, currentColor, directMatch, idx);
    }

    //Same size: compare each use, we require same order
    for (std::vector<std::pair<int, int> >::const_iterator it = usRepl.begin ();
                                                         it != usRepl.end ();
                                                         it++)
    {
        //Get element
        const std::pair<int, int> &up = *it;
        int currentUs = up.first;
        int colorUs = up.second;

        //If color is right, we can compare ourselves
        CHECK_LOG ( "Our color %d, currentColor %d\n", colorUs, currentColor);
        if (colorUs >= currentColor)
        {
            bool foundOne = false;

            //Compare to real them
            CHECK_LOG ( "Us %d, them %d\n", currentUs, them);
            if (currentUs == them)
            {
                foundOne = true;
            }
            else
            {
                //Otherwise go for the replacement
                for (std::vector<std::pair<int, int> >::const_iterator inner = themRepl.begin ();
                        inner != themRepl.end ();
                        inner++)
                {
                    const std::pair<int, int> &tp = *inner;
                    int currentThem = tp.first;
                    int colorThem = tp.second;

                    CHECK_LOG ( "Us %d, colorThem %d, currentcolor %d\n", currentUs, colorThem, currentColor);
                    //If color is good, compare them
                    if (colorThem >= currentColor)
                    {
                        CHECK_LOG ( "current Us %d, current them %d\n", currentUs, currentThem);
                        if (currentUs == currentThem)
                        {
                            //We found one, we can break and go to next index
                            foundOne = true;
                            break;
                        }
                    }
                }
            }

            //We found equivalent registers, next index !
            if (foundOne == true)
            {
                //Next index
                return usesEqualHelper (mir, other, replacementRegs, currentColor, directMatch, idx + 1);
            }
            else
            {
                //The we are done, we have no match for this one
                break;
            }
        }
    }

    //We don't have a match
    CHECK_LOG ( "No match\n");
    return false;
}

bool usesEqual (const MIR *mir, const MIR *other, std::map<int, std::vector <std::pair <int, int> > > &replacementRegs, int currentColor, bool &directMatch)
{
    //Paranoid
    assert (other != 0 && other->ssaRep != 0);

    CHECK_LOG ( "Comparing two MIRS\n");

    //Get other's number of uses
    int num = other->ssaRep->numUses;

    //Is it the same size?
    if (mir->ssaRep->numUses != num)
    {
        CHECK_LOG ( "Not the same uses! (%d, %d)\n", mir->ssaRep->numUses, num);
        return false;
    }

    //Call helper
    //Suppose for the best
    directMatch = true;
    bool res = usesEqualHelper (mir, other, replacementRegs, currentColor, directMatch);

    //No need to check the rest
    if (res == false)
    {
        CHECK_LOG ( "Helper says not the same uses\n");
        return false;
    }

    //We haven't finished yet actually, we must look at the vA,vB,vC in case there is a constant
    const DecodedInstruction &us = mir->dalvikInsn;
    const DecodedInstruction &it = other->dalvikInsn;

    //We need the attributes to check the right virtual register
    long long dfAttributes =
        dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

    //We only care about the virtual register if it isn't a defined
    if ( (dfAttributes & DF_DA) == 0 && (dfAttributes & DF_DA_WIDE) == 0)
    {
        if (us.vA != it.vA)
        {
            CHECK_LOG ("vA is not the same (%d, %d)\n", us.vA, it.vA);
            return false;
        }
    }

    //If they are not used, they must be the same
    if ( (dfAttributes & DF_UB) == 0 && (dfAttributes & DF_UB_WIDE) == 0)
    {
        if (us.vB != it.vB)
        {
            CHECK_LOG ( "vB is not the same (%d, %d)\n", us.vB, it.vB);
            return false;
        }
    }

    if ( (dfAttributes & DF_UC) == 0 || (dfAttributes & DF_UC_WIDE) == 0)
    {
        if (us.vC != it.vC)
        {
            CHECK_LOG ( "vC is not the same (%d, %d)\n", us.vC, it.vC);
            return false;
        }
    }

    //If we got here, the arrays are the same
    CHECK_LOG ( "They are the same!\n");
    return true;
}

