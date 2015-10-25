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

#ifndef DALVIK_VM_METHODCONTEXTHANDLER
#define DALVIK_VM_METHODCONTEXTHANDLER

#include "MethodContext.h"

/**
 * @class MethodContextHandler
 * @brief MethodContextHandler Handles the MethodContexts of the system
 */
class MethodContextHandler
{
private:
    /* --------- Constructors, Destructor --------- */

    /**
     * @brief Regular constructors
     * @details unused
     */
    MethodContextHandler (void);

    /**
     * @brief Copy constructor
     * @details unused
     */
    MethodContextHandler (const MethodContext&);

    /**
     * @brief Assignment operator
     * @details unused
     * @return Reference to the assigned MethodContext
     */
    MethodContextHandler& operator= (const MethodContextHandler&);

    /** @brief Destructor */
    ~MethodContextHandler (void) {};

    /* -------- Elements for management of the context --------- */

    /** @brief The method context map */
    static std::map<const Method *, const MethodContext *> methodMap;

    /** @brief The value here is arbitrary, just kept for sanity
     *  @details Serves as a bound for the maxContexts value, since
     *  that can set by user
     */
    static const unsigned int MAX_POSSIBLE_CONTEXTS = 1000;

    /** @brief Default value, based on the average number of methods in many common apps */
    static unsigned int maxContexts;

    /**
     * @brief Cleanup the context map
     * @details Create an empty entry in the context map so that
     * new context information can be added
     * @return Whether we could successfully create an entry
     */
    static bool cleanUpMethodMap (void);

public:

    /**
     * @brief Instance function
     * @param method The Method for which we need the context
     * @return the generated MethodContext, or null
     */
    static const MethodContext *getMethodContext (const Method *method);

    /**
     * @brief Set the maximum number of contexts allowed in the system
     * @param numContexts The maximum allowed contexts
     * @return Whether we could successfully set the value
     */
    static bool setMaxContexts (unsigned long numContexts);

    /**
     * @brief Set the maximum number of constants to be allowed per context
     * @param numConstants The maximum number of constants requested
     * @return Whether we could successfully set the value
     */
    static bool setMaxConstantsPerContext (unsigned long numConstants);

    /**
     * @brief Set the maximum number of basic blocks allowed in a method for context creation
     * @param numBasicBlocks The number of basic blocks requested
     * @return Whether we could successfully set the value
     */
    static bool setMaxBasicBlocksPerContext (unsigned long numBasicBlocks);

    /** @brief Erase the entire map */
    static void eraseMethodMap (void);

};

/**
 * @brief Check whether a VR is constant at the mir
 * @param mirForVR The MIR in which the vR is used
 * @param[out] vR The vR for which we need const information
 * @param value The value of the vR, if found
 * @return The type of the constant, if found
 */
ConstVRType dvmCompilerGetConstValueOfVR (const MIR *mirForVR, u4 vR, u8 &value);

/**
 * @brief See if the MIR is the last use of the current define of the VR
 * @details This is not the last use of the VR, but this still returns true
 * if the VR is redefined after this MIR
 * @param mir The MIR to check
 * @param VR The VR we are interested in
 * @return true if the VR is redefined / not used after this MIR
 */
bool dvmCompilerIsMirEndOfUDChain (const MIR *mir, u4 *VR);

#endif
