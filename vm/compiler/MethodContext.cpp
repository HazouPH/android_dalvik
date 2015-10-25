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

/**
 * MethodContext.cpp
 *
 * This file contains the implementation of the MethodContext class. Some details are noteworthy.
 *
 * DETAILS:
 *
 * 1. Essentially, this class is a data store. The functions here assist in collecting the data,
 *    but at the end we are left with maps and queues containing data. Note that the only public
 *    function which creates the context is the createNewInstance, which returns a const. So the
 *    context is immutable to the outside world, and can only be queried.
 *
 * 2. Moreover, we can perform optimizations on the data store such as sorting the queues, without
 *    worrying about the sorting ever being invalid.
 *
 * 3. Due to that, any expansion of the class which adds APIs, should make the API functions const.
 *
 * DEBUGGING:
 *
 * Do a (hash) define of DEBUG_METHOD_CONTEXT in MethodContex.h to get debugging information, including statistics
 * about the Method Context. Everybody likes statistics.
 *
 * LIMITATIONS:
 *
 * 1. The maximum number of constants a context can contain is defined by MAX_POSSIBLE_CONSTANTS in MethodContext.h.
 *    The limitation exists since we store the constant information for each VR in @see ConstOffset struct,
 *    which contains the index into a table of all constants. By limiting the number of constants to 256,
 *    the index is just one byte. The start offset of the VR's constant range is two bytes, the end offset is
 *    one more, for a total of 4 bytes, which fits snugly in 32-bits. We like snug structures.
 *
 *    => Note that the actual value guarding the number of constants is maxConstants, which can be set by the user.
 *       Use the -Xjitmaxconstantspercontext:<value> flag to do so.
 *       However, maxConstants cannot go over MAX_POSSIBLE_CONSTANTS, which is fixed.
 *
 * TODOs:
 *
 * 1. The findConstantVRs function only looks at constants defined by virtue of coming from a const bytecode. It
 *    should also look at constant VRs which are constants because we did a MOVE of another constant VR to it.
 */

#include "MethodContext.h"
#include "BBOptimization.h"
#include "SSAWalkData.h"
#include "Utility.h"

#ifdef DEBUG_METHOD_CONTEXT
#define METHOD_CONTEXT_LOG(X) X
#else
#define METHOD_CONTEXT_LOG(X)
#endif

#define DEFAULT_BASIC_BLOCK_LIMIT 500

#ifdef DEBUG_METHOD_CONTEXT
static void contextLog (const MethodContext *const context, const char *message)
{
    ALOGE ("METHOD_CONTEXT: %s%s - %s", context->getMethod ()->clazz->descriptor, context->getMethod ()->name, message);
}
#endif

//Initialize the static elements
unsigned int MethodContext::maxConstants = MAX_POSSIBLE_CONSTANTS;
unsigned int MethodContext::maxBasicBlocks = DEFAULT_BASIC_BLOCK_LIMIT;

MethodContext::MethodContext (const Method *method)
{
    this->method = method;
}

int MethodContext::getIndexForConst (unsigned int value)
{
    std::vector<unsigned int>::iterator it;

    unsigned int position = 0;

    //Find the index of the constant if present
    for (it = constants.begin (); it != constants.end (); it++)
    {
        if ((*it) == value)
        {
            return position;
        }
        position++;

        //Paranoid
        assert (position <= maxConstants);
    }

    //Not found. Add it to the store if possible
    if (constants.size () < maxConstants)
    {
        constants.push_back (value);
        return position;
    }
    else
    {
        METHOD_CONTEXT_LOG (contextLog (this, "Reached constant table size limit"));
    }

    //Position cannot be determined for this constant
    return -1;
}

bool MethodContext::getConstAtIndex (unsigned int index, unsigned int &value) const
{
    //The index should be within bounds
    if (index >= constants.size ())
    {
        METHOD_CONTEXT_LOG (contextLog (this, "Illegal const table access"));
        return false;
    }

    value = constants[index];

    return true;
}

/**
 * @brief Go through all the MIRs and record the constants being set
 * @details Record wide and non-wide constants, and those set through a move
 * on a constant VR.
 * @param cUnit The CompilationUnit
 * @param bb The BasicBlock for which we want to record the constant
 * @return false if an error occured while collecting constants, true otherwise
 */
static bool findConstantVRs (CompilationUnit *cUnit, BasicBlock *bb)
{
    // Skip if we have already seen this BasicBlock
    if (bb->visited == true)
    {
        return false;
    }

    bb->visited = true;

    //Get the pass data, which contains the Method Context and a map for VRs
    std::pair<MethodContext *, std::map<int, int> *> *passData =
            static_cast<std::pair<MethodContext *, std::map<int, int> *> *> (cUnit->walkData);

    std::map<int, int> *uniqueDefVR = passData->second;
    MethodContext *context = passData->first;

    //Go through all the MIRs and see if they set or move a constant
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        unsigned short beginOffset = 0;
        unsigned short endOffset = 0;

        int dfAttributes =
                dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if ( (dfAttributes & DF_HAS_DEFS) == 0)
        {
            continue;
        }

        /* Handle instructions that set up constants directly */
        if ( (dfAttributes & DF_SETS_CONST) != 0)
        {
            int lowConst = 0, highConst = 0;
            bool isWideConstDef = false;

            //Get the actual value of the VR
            bool setsConst = dexGetConstant (mir->dalvikInsn, lowConst, highConst, isWideConstDef);

            //Fake use for non-assert build
            (void) setsConst;

            //Paranoid
            assert (setsConst == true);

            endOffset = beginOffset = mir->offset;

            //Now find the offset till which the VR is a constant
            SSARepresentation *ssaRep = mir->ssaRep;

            //Cannot continue without SSA
            if (ssaRep == 0)
            {
                METHOD_CONTEXT_LOG (contextLog (context, "No SSA available while finding constants"));
                return false;
            }

            //Get the VRs
            u4 lowVR = dvmExtractSSARegister(cUnit, mir->ssaRep->defs[0]);
            u4 highVR = -1;

            if (isWideConstDef == true)
            {
                if (mir->ssaRep->numDefs != 2)
                {
                    METHOD_CONTEXT_LOG (contextLog (context, "High Definition not found for wide VR"));
                    return false;
                }

                highVR = dvmExtractSSARegister(cUnit, mir->ssaRep->defs[1]);
            }

            /* Check if the constant define is the first define of the VR. If so, add it.
             * If not, put a -1 so that it is not considered.
             */

            //First look at low VR
            std::map<int, int>::iterator it = uniqueDefVR->find(lowVR);

            if (it == uniqueDefVR->end())
            {
                //This is the first
                (*uniqueDefVR)[lowVR] = 1;
            }
            else
            {
                //Redefinition. Put -1 so that it is ignored later
                (*uniqueDefVR)[lowVR] = -1;
            }

            //Do the same for High VR
            if (isWideConstDef == true)
            {
                it = uniqueDefVR->find(highVR);

                if (it == uniqueDefVR->end())
                {
                    //This is the first
                    (*uniqueDefVR)[highVR] = 1;
                }
                else
                {
                    //Redefinition. Put -1 so that it is ignored later
                    (*uniqueDefVR)[highVR] = -1;
                }
            }

            //Get the uses of the low and, if available, high part
            SUsedChain *usedChainLow = ssaRep->usedNext[0];
            SUsedChain *usedChainHigh = ssaRep->numDefs == 2 ? ssaRep->usedNext[1] : 0;

            //Remember the previous MIR in the used chain, to maintain a continuous chain
            MIR *lastMIRInUseChain = mir;

            /* Go through the useNext chain. We want to go as far as we can in the chain
             * while maintaining consistency if both low and high parts are present
             */
            while (usedChainLow != 0)
            {
                MIR *currentMIRInUseChain = usedChainLow->mir;

                //Paranoid
                if (currentMIRInUseChain == 0)
                {
                    break;
                }

                //Quit if it is a Phi node
                if (currentMIRInUseChain->dalvikInsn.opcode == static_cast<Opcode>(kMirOpPhi))
                {
                    break;
                }

                //The const defined VR should not be redefining itself. If it does, the SSA version
                //changes, but the old version might still live on. When queried from the trace world
                //the SSA information is lost, and the multiple VR versions will cause confusion (bugs)
                //about const-ness. So bail when this happens.
                unsigned int currentDFA = dvmCompilerDataFlowAttributes[currentMIRInUseChain->dalvikInsn.opcode];

                if (currentMIRInUseChain->dalvikInsn.vA == lowVR )
                {
                    if ((currentDFA & DF_DA) != 0 || (currentDFA & DF_DA_WIDE) != 0)
                    {
                        break;
                    }
                }

                //Check if the wide part is consistent
                if (isWideConstDef == true)
                {

                    //If the low chain is here, but high chain does not exist
                    //We would still keep the endOffset we have updated so far
                    if (usedChainHigh == 0)
                    {
                        METHOD_CONTEXT_LOG (contextLog(context, "Missing high VR use for wide const"));
                        break;
                    }

                    //If the low chain and high chain go different ways
                    //We would still keep the endOffset we have updated so far
                    if (usedChainHigh->mir->offset != currentMIRInUseChain->offset)
                    {
                        METHOD_CONTEXT_LOG (contextLog(context, "Inconsistent high VR for wide const"));
                        break;
                    }
                }

                /*
                 * Check if the offset of the next use forms a continuous increasing chain.
                 * If the use chain, goes backwards, or jumps over a basic block, we need to
                 * handle it separately.
                 */

                //Now see if the offset is continuous
                if (currentMIRInUseChain->bb != lastMIRInUseChain->bb)
                {
                    //Make sure the BBs are consecutive
                    BasicBlock *lastBB = lastMIRInUseChain->bb;
                    BasicBlock *currentBB = currentMIRInUseChain->bb;

                    if (lastBB->lastMIRInsn->offset + lastBB->lastMIRInsn->width != currentBB->firstMIRInsn->offset)
                    {
                        //Stop the previous offset range at the last considered MIR in use chain
                        context->updateVRConsts (lowVR, lowConst, beginOffset, lastMIRInUseChain->offset);
                        if (isWideConstDef == true)
                        {
                            context->updateVRConsts (highVR, highConst, beginOffset, lastMIRInUseChain->offset);
                        }

                        //Continue with the chain with the new beginOffset
                        beginOffset = currentMIRInUseChain->offset;
                    }
                }

                //Update endOffset to at least this MIR
                endOffset = currentMIRInUseChain->offset;

                //Record this MIR
                lastMIRInUseChain = currentMIRInUseChain;

                //Go to the next MIR
                usedChainLow = usedChainLow->nextUse;

                if (isWideConstDef == true)
                {
                    usedChainHigh = usedChainHigh->nextUse;
                }

            }

            /*
             * Update the values. The endOffsets and beginOffsets are only updated in the code above
             * if the usedChains are proper. That is, they are over continuous  and increasing
             * offsets, and the wide part follows the same chain as the low part
             */
            context->updateVRConsts (lowVR, lowConst, beginOffset, endOffset);
            if (isWideConstDef == true)
            {
                context->updateVRConsts (highVR, highConst, beginOffset, endOffset);
            }
        }
        else
        {
            /*
             * We have a def but it does not set a const. Since we are tracking const defines which
             * are the only defines of the VR, let's mark this VR as not interesting for that purpose
             */
            SSARepresentation *ssaRep = mir->ssaRep;

            if (ssaRep == 0)
            {
                continue;
            }

            for (int i = 0; i < mir->ssaRep->numDefs; i++)
            {
                int vr = dvmExtractSSARegister (cUnit, mir->ssaRep->defs[i]);
                (*uniqueDefVR)[vr] = -1;
            }
        }
    }

    return true;
}

bool MethodContext::handleConstants (CompilationUnit *cUnit)
{
    SSAWalkData data (cUnit);
    void *walkData = static_cast<void *>(&data);

    //Reset flags
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, dvmCompilerClearVisitedFlag, kAllNodes, false);

    /*
     * We are also interested in VRs which are only defined once, and defined
     * as constants. For such VRs, we can put the whole method as constant range.
     * To do that, we have a map of all interesting VRs
     */
    std::map<int, int> uniqueDefVRs;

    //Pass the context and this map to the findConstantVRs function
    std::pair<MethodContext *, std::map<int, int> *> passData;

    passData.first = this;
    passData.second = &uniqueDefVRs;

    walkData = static_cast<void *>(&passData);
    dvmCompilerDataFlowAnalysisDispatcher (cUnit,
            findConstantVRs, kReachableNodes, false, walkData);

    //The map should now have been filled with uniquely defined constants.
    for (std::map<int, int>::iterator it = uniqueDefVRs.begin(); it != uniqueDefVRs.end(); it++)
    {
        if (it->second == 1)
        {
            int VR = it->first;

            //Get constant index of this VR
            const std::map<int, std::vector<ConstOffset> >::const_iterator mapIterator = vRConstMap.find (VR);

            //Fail if the VR is not found
            if (mapIterator == vRConstMap.end ())
            {
                return false;
            }

            const std::vector<ConstOffset>& offsets = mapIterator->second;

            //Paranoid: The VR should have at least one const value
            if (offsets.size() < 1)
            {
                return false;
            }

            int constIndex = offsets[0].constIndex;

            //Add this VR and its constant index to the method wide const VR map
            methodWideConstVRs[VR] = constIndex;

            //The VR is const method wide. Remove the offset level information for this VR
            vRConstMap.erase(VR);
        }
    }

    return true;
}

static bool markLastUsesAtEndOfBB (CompilationUnit *cUnit, BasicBlock *bb)
{
    std::map<unsigned int, std::set<unsigned int> > *mapVR =
            static_cast<std::map<unsigned int, std::set<unsigned int> > *>(cUnit->walkData);

    /*
     * For now, we only look at the last MIR of the basic block. This
     * is faster, and also uses lot less space. Also it covers specific cases
     * like VRs used only for comparisons in if statements.
     */

    //Get the last MIR
    MIR *lastMir = bb->lastMIRInsn;

    //Paranoid
    if (lastMir == 0 || lastMir->ssaRep == 0)
    {
        return false;
    }

    //Now go through all the uses, and see if they are the last use
    int useIndex = 0;

    while (useIndex < lastMir->ssaRep->numUses)
    {
        SUsedChain *useChain = dvmCompilerGetUseChainForUse (lastMir, useIndex);

        //If this is the end of the use chain, mark this offset
        if (useChain != 0 && useChain->nextUse == 0)
        {
            (*mapVR)[lastMir->offset].insert (dvmExtractSSARegister (cUnit, lastMir->ssaRep->uses [useIndex]));
        }

        useIndex++;
    }

    return false;
}

void MethodContext::handleEndOfUDChains (CompilationUnit *cUnit)
{
    void *walkData = static_cast<void *>(&lastOffsetOfBBToLastUseVRMap);
    dvmCompilerDataFlowAnalysisDispatcher (cUnit,
            markLastUsesAtEndOfBB, kReachableNodes, false, walkData);
}

const MethodContext *MethodContext::createNewInstance (const Method * method)
{

    /* Perform method level analysis */
    // The temporary CFG for processing
    CompilationUnit cUnit;

    //Setup the cUnit for processing
    bool success = dvmCompilerFillCUnitWithMethodData (cUnit, method, true);

    //If we fail to create CFG, we cannot continue
    if (success == false)
    {
        METHOD_CONTEXT_LOG (ALOGE ("METHOD_CONTEXT: Failed to create CFG for method %s%s", method->clazz->descriptor, method->name));
        return 0;
    }

    // Check the size of the CFG. If it exceeds our bounds, do not create a context
    if (cUnit.numBlocks > maxBasicBlocks)
    {
        METHOD_CONTEXT_LOG (ALOGE ("METHOD_CONTEXT: Rejecting context as CFG is too large for method %s%s", method->clazz->descriptor, method->name));
        return 0;
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

    dvmCompilerCalculateBasicBlockInformation (&cUnit, false, false);

    // Check the walk status of the CFG. If it too complex to walk, do not create a context
    if (cUnit.predecessorFirstTraversalOK == false)
    {
        METHOD_CONTEXT_LOG (ALOGE ("METHOD_CONTEXT: Rejecting context as CFG is too complex for method %s%s", method->clazz->descriptor, method->name));
        return 0;
    }

    //The cUnit preparation is done so now let's create the context
    MethodContext *context = new MethodContext (method);

    //Paranoid
    if (context == 0)
    {
        return 0;
    }

    /* Now let us collect each of the statistics individually */

    /* ----------- Record constant's information ---------------*/

    //If we fail during constant finding, we bail
    if (context->handleConstants (&cUnit) == false)
    {
        METHOD_CONTEXT_LOG (contextLog (context, "Constant information collection failed"));

        //Clear the data structures
        context->constants.clear();
        context->vRConstMap.clear();
        context->methodWideConstVRs.clear();
    }

    context->handleEndOfUDChains (&cUnit);

    return context;
}

bool MethodContext::getConstValueOfVR (unsigned int offset, int vR, unsigned int &value) const
{
    //First see if this VR is constant in the whole method
    if (methodWideConstVRs.find (vR) != methodWideConstVRs.end ())
    {
        //Found it. Get the value and return it
        return getConstAtIndex (methodWideConstVRs.find(vR)->second, value);
    }

    unsigned short offsetInShort = static_cast<unsigned short>(offset);

    //Get the offset list of the VR
    std::map<int, std::vector<ConstOffset> >::const_iterator mapIterator = vRConstMap.find (vR);

    //Fail if the VR is not found
    if (mapIterator == vRConstMap.end ())
    {
        return false;
    }

    const std::vector<ConstOffset>& constOffsetList = mapIterator->second;
    std::vector<ConstOffset>::const_iterator constOffsets;

    //Go through the offset list and see where this offset belongs
    for (constOffsets = constOffsetList.begin (); constOffsets != constOffsetList.end (); constOffsets++)
    {
        ConstOffset vrConstOffset = *constOffsets;

        //See if the offset falls in the range of this constant
        if (vrConstOffset.offsetStart <= offsetInShort &&
                vrConstOffset.offsetStart + vrConstOffset.offsetEnd >= offsetInShort )
        {
            //Get the constant and return it
            return getConstAtIndex (vrConstOffset.constIndex, value);
        }
    }

    return false;
}

bool MethodContext::setMaxConstants (unsigned long numConstants)
{
    if (numConstants < MAX_POSSIBLE_CONSTANTS)
    {
        maxConstants = numConstants;
        return true;
    }

    METHOD_CONTEXT_LOG (ALOGE ("Invalid value %ld for maximum constants. Must be within [0 - %d]", numConstants, MAX_POSSIBLE_CONSTANTS));
    return false;
}

bool MethodContext::setMaxBasicBlocks (unsigned long numBasicBlocks)
{
    if (numBasicBlocks < MAX_POSSIBLE_BASICBLOCKS)
    {
        maxBasicBlocks = numBasicBlocks;
        return true;
    }

    METHOD_CONTEXT_LOG (ALOGE ("Invalid value %ld for maximum basic blocks. Must be within [0 - %d]", numBasicBlocks, MAX_POSSIBLE_BASICBLOCKS));
    return false;
}

bool MethodContext::updateVRConsts (int vR, unsigned int value, unsigned int offsetStart, unsigned int offsetEnd)
{
    //Let's keep a limit on the number of offsets we create for a VR
    const int MAX_OFFSETS = 50;

    //Convenience variable for the maximum value we can fit in a byte
    const int MAX_VALUE_BYTE = 0xFF;

    int offsetDiff = offsetEnd - offsetStart;

    //Sanity check on the range
    if (offsetDiff < 0)
    {
        return false;
    }

    //If the offsets are far apart, we will break it down
    //and add each small chunk to the VR list. Each offset
    //will be kept in one byte, and we don't want too many
    //of these offsets
    if ((offsetDiff / MAX_VALUE_BYTE) > MAX_OFFSETS)
    {
        METHOD_CONTEXT_LOG (contextLog (this, "Cannot updateVRConsts because offset range is too large"));
        return false;
    }

    //Add constant to table, get the index
    int constIndex = getIndexForConst (value);

    //If we cannot get an index for our value, this VR const information
    //has to be given up
    if (constIndex < 0 || constIndex > MAX_VALUE_BYTE)
    {
        METHOD_CONTEXT_LOG (contextLog (this, "Failed to updateVRConst because could not get const index"));
        return false;
    }

    //The index has to be 8-bit
    unsigned char constIndexByte = constIndex;

    //Chop off the offset into byte size ranges
    //Because that is what we are going to store.
    do
    {

        ConstOffset newOffset;

        /*
         * This assignment of the subtraction of two unsigned ints to
         * a signed int is safe because we are checking it above as well
         * and returning for negative values.
         */
        int tempOffsetDiff = offsetEnd - offsetStart;

        if (tempOffsetDiff > MAX_VALUE_BYTE)
        {
            //Such large offset ranges are rare. Log it as interesting
            METHOD_CONTEXT_LOG (contextLog (this, "Offset range crosses 255"));
        }

        //We want the offsetDiff to be a maximum of 255
        tempOffsetDiff = tempOffsetDiff & MAX_VALUE_BYTE;

        newOffset.constIndex = constIndexByte;
        newOffset.offsetStart = offsetStart;
        newOffset.offsetEnd = static_cast<unsigned char>(tempOffsetDiff);

        vRConstMap[vR].push_back (newOffset);

        offsetStart += (tempOffsetDiff + 1);

    } while (offsetEnd > offsetStart);

    return true;
}

void MethodContext::printStatistics (void) const
{
    //Get information about the offsetRanges: How many, and max per VR
    unsigned int totalOffsetRanges = 0;
    unsigned int maxOffsetRanges = 0;
    int maxOffsetVR = 0;

    std::map<int, std::vector<ConstOffset> >::const_iterator vRConstMapIter;

    for (vRConstMapIter = vRConstMap.begin (); vRConstMapIter != vRConstMap.end (); vRConstMapIter++)
    {
        unsigned int size = vRConstMapIter->second.size ();

        totalOffsetRanges += size;

        if (size > maxOffsetRanges)
        {
            maxOffsetRanges = size;
            maxOffsetVR = vRConstMapIter->first;
        }
    }

    //Print out all the statistics in one line
    ALOGE ("JIT_INFO: For %s%s - Const VRs: %d, Max Offset Ranges: %d for VR %d, Uniq Const: %d, Const-If pairs recorded: %d",
            method->clazz->descriptor, method->name, vRConstMap.size (), maxOffsetRanges, maxOffsetVR, constants.size (), lastOffsetOfBBToLastUseVRMap.size());
}

bool MethodContext::isOffsetEndOfUDChain (unsigned int offset, u4 VR) const
{
    std::map<unsigned int, std::set<unsigned int> >::const_iterator it = lastOffsetOfBBToLastUseVRMap.find (offset);

    //See if we have an entry for this offset
    if (it != lastOffsetOfBBToLastUseVRMap.end())
    {
        //Check if the VR is the same as the one with the last use at this offset
        return (it->second.find (VR) != it->second.end ());
    }

    return false;
}
