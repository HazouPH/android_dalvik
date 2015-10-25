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
 * MethodContextHandler.cpp
 *
 * This file contains the implementation of the MethodContextHandler class. Some details are noteworthy.
 *
 * DETAILS:
 *
 * 1. The purpose of this class is to act as a handler for the MethodContext instance created, including
 *    taking care of the instances. Any piece of code which wants to use the method context needs to go
 *    through this class.
 *
 * 2. getMethodContext returns a context instance for the supplied method. If such an instance does not
 *    exist, one is created and added to a map called methodMap. Subsequent request for a context get the
 *    context saved in the map.
 *
 * 3. NOTE: We only attempt to create the context once. If we fail, we add an entry for the method to the map,
 *    but keep the context as null. Subsequent requests for a context for the method always get a null value.
 *
 * 4. Because of that, any APIs we create in this file should be prepared to handle the case when we don't find
 *    a context. For example, the API for "what is the const value of this VR at this offset of this method" will
 *    try to find the context for the method. If it fails, it can just say "I don't know", simply because we don't
 *    have the context.
 *
 * DEBUGGING:
 *
 * Do a (hash) define DEBUG_METHOD_CONTEXT is MethodContext.h to get debugging information, including statistics
 * about the Method Contexts. Everybody likes statistics.
 *
 * LIMITATIONS:
 *
 * 1. The maximum number of contexts we can add to the map is limited by the value of MAX_POSSIBLE_CONTEXTs. Note that
 *    the value maxContexts is used as a limit, and can be set by user, but maxContexts cannot go over MAX_POSSIBLE_CONTEXTS.
 *    The limitation is arbitrary in terms of its value, but is present for sanity, in case the user tries to put a huge,
 *    unmanageable value. A value of 0 for maxContexts basically shuts off the method context system.
 *
 *    => Use the -Xjitmaxmethodcontexts:<value> flag to set maxContexts
 *
 * TODOs:
 *
 * 1. Since the context information can be quite large, we need to periodically clean up the map, for example in a LRU manner.
 *    Such intelligence needs to be put in the cleanupMethodMap function.
 *
 * 2. We need to keep track of the memory usage of the map, and call cleanupMethodMap when required.
 */

#include "MethodContextHandler.h"
#include "SSAWalkData.h"


#ifdef DEBUG_METHOD_CONTEXT
#define METHOD_CONTEXT_LOG(X) X
#else
#define METHOD_CONTEXT_LOG(X)
#endif

//Initialize the static elements
std::map<const Method *, const MethodContext *> MethodContextHandler::methodMap;
unsigned int MethodContextHandler::maxContexts = 500;

bool MethodContextHandler::cleanUpMethodMap (void)
{
    //TODO: Add intelligent map cleaning

    //Return whether we could free up space
    return (methodMap.size() < maxContexts);
}

const MethodContext *MethodContextHandler::getMethodContext (const Method * method)
{
    //See if maxContexts is 0, which means the context system has to turn off.
    if (maxContexts == 0)
    {
        return 0;
    }

    //Assume we will not find the context
    const MethodContext *context = 0;

    std::map <const Method *, const MethodContext *>::iterator it;

    it = methodMap.find (method);

    //The context might not be available yet
    if (it == methodMap.end ())
    {

        //First check if we have space for the new context
        if (methodMap.size () >= maxContexts)
        {
            //Attempt to create space in the map
            bool success = cleanUpMethodMap ();

            //Bail if we cannot make space for this context
            if (success == false)
            {
                return 0;
            }
        }

        /* Since we now have space for the context, let's
         * create one
         */
        context = MethodContext::createNewInstance (method);

        /* NOTE: The context can be null. We still want to add the method
         * to our map so that we keep returning null context for the method
         * and never recalculate it
         */
        methodMap[method] = context;
    }
    else
    {
        context = it->second;
    }

    return context;
}

void MethodContextHandler::eraseMethodMap (void)
{
    //Iterate over the context map, and destroy all the contexts
    std::map<const Method *, const MethodContext *>::iterator contextIter;

    //Print out some information if we are getting debugged
    METHOD_CONTEXT_LOG (ALOGE ("----------------CLEARING UP METHOD CONTEXTS----------------------------"));
    METHOD_CONTEXT_LOG (ALOGE ("Total number of contexts in the system: %u", methodMap.size ()));

    for (contextIter = methodMap.begin (); contextIter != methodMap.end (); contextIter++)
    {
        const MethodContext *toDelete = contextIter->second;

        //Print statistics for the individual context */
        if (toDelete != 0)
        {
            METHOD_CONTEXT_LOG (toDelete->printStatistics ());
        }

        delete toDelete, toDelete = 0;
    }

    methodMap.clear ();
}

bool MethodContextHandler::setMaxContexts (unsigned long numContexts)
{
    if (numContexts <= MAX_POSSIBLE_CONTEXTS)
    {
        maxContexts = numContexts;
        return true;
    }

    METHOD_CONTEXT_LOG (ALOGE ("Could not set a value of %ld for maximum contexts", numContexts));
    return false;
}

bool MethodContextHandler::setMaxConstantsPerContext (unsigned long numConstants)
{
    return MethodContext::setMaxConstants (numConstants);
}

bool MethodContextHandler::setMaxBasicBlocksPerContext (unsigned long numBasicBlocks)
{
    return MethodContext::setMaxBasicBlocks(numBasicBlocks);
}

ConstVRType dvmCompilerGetConstValueOfVR (const MIR *mirForVR, u4 vR, u8 &value)
{
    //Get the method associated with the MIR
    const Method *method = mirForVR->nesting.sourceMethod;

    //If the MIR has no associated method, we cannot have any context information
    if (method == 0)
    {
        return kVRUnknown;
    }

    //Get the context for the method to which the mir belongs to
    const MethodContext *metCon = MethodContextHandler::getMethodContext (method);

    //If there is not context, we cannot determine if it is a constant
    if (metCon == 0)
    {
        return kVRUnknown;
    }

    //See if the VR is actually present in the MIR
    if (vR != mirForVR->dalvikInsn.vA && vR != mirForVR->dalvikInsn.vB && vR != mirForVR->dalvikInsn.vC)
    {
        return kVRUnknown;
    }

    //Check if we are dealing with a wide VR
    int dfAttributes = dvmCompilerDataFlowAttributes[mirForVR->dalvikInsn.opcode];

    //Let's ignore more complicated bytecodes for now
    if ((dfAttributes & DF_FORMAT_35C) != 0 || (dfAttributes & DF_FORMAT_3RC) != 0 )
    {
        return kVRUnknown;
    }

    bool isWide = false;

    if ((vR == mirForVR->dalvikInsn.vA && (dfAttributes & DF_UA_WIDE) != 0)
            || (vR == mirForVR->dalvikInsn.vB && (dfAttributes & DF_UB_WIDE) != 0)
            || (vR == mirForVR->dalvikInsn.vC && (dfAttributes & DF_UC_WIDE) != 0))
    {
        isWide = true;
    }

    //Now try to get the constant value
    bool success = false;
    unsigned int lowConst = 0;
    unsigned int highConst = 0;

    /*
     * There might be a difference in the context's understanding of the VR numbers of this
     * MIR and the one passed, since inlining could have renamed the VRs and gotten different
     * numbers, while the context calculation uses VRs from the raw dex file. If renamed, we
     * will have a non-zero value for virtualRegRenameOffset. Let us always subtract anyways
     * to be consistent
     */

    u4 actualVR = vR - mirForVR->virtualRegRenameOffset;

    success = metCon->getConstValueOfVR (mirForVR->offset, actualVR, lowConst);

    //For wides, additionally get the const for the higher VR
    if (isWide == true)
    {
        success = (success == true) && (metCon->getConstValueOfVR (mirForVR->offset, actualVR+1, highConst) == true);
    }

    if (success == true)
    {
        value = highConst;
        value <<= 32;
        value |= (lowConst & 0xFFFFFFFF);

        if (isWide == true)
        {
            return kVRWideConst;
        }

        return kVRNonWideConst;
    }

    return kVRNotConst;
}

bool dvmCompilerIsMirEndOfUDChain (const MIR *mir, u4 VR)
{
    //Get the method associated with the MIR
    const Method *method = mir->nesting.sourceMethod;

    //If the MIR has no associated method, we cannot have any context information
    if (method == 0)
    {
        return false;
    }

    DecodedInstruction insn = mir->dalvikInsn;

    //See if the VR is actually present in the MIR
    if (VR != mir->dalvikInsn.vA && VR != mir->dalvikInsn.vB && VR != mir->dalvikInsn.vC)
    {
        return false;
    }

    int dfAttributes = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

    //Make sure the MIR actually uses this VR
    if (((dfAttributes & DF_UA) == 0 || insn.vA != VR) &&
            ((dfAttributes & DF_UB) == 0 || insn.vB != VR) &&
            ((dfAttributes & DF_UC) == 0 || insn.vC != VR))
    {
        return false;
    }

    //Get the context for the method to which the mir belongs to
    const MethodContext *metCon = MethodContextHandler::getMethodContext (method);

    //If there is not context, we cannot determine if it is a constant
    if (metCon == 0)
    {
        return false;
    }

    //Take method inlining into account
    u4 actualVR = VR - mir->virtualRegRenameOffset;

    bool success = metCon->isOffsetEndOfUDChain(mir->offset, actualVR);

    return success;
}
