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

#ifndef DALVIK_VM_METHODCONTEXT
#define DALVIK_VM_METHODCONTEXT

#include "CompilerUtility.h"
#include "CompilerIR.h"
#include "Dalvik.h"

#include <set>

//Forward declarations
struct BasicBlock;
struct BitVector;
class CompilationUnit;
class Pass;

/**
 * @brief Specifies the type of a const VR
 */
enum ConstVRType
{
    /** @brief A 32-bit const VR */
    kVRNonWideConst,
    /** @brief A 64-bit const VR */
    kVRWideConst,
    /** @brief A definitively non const VR */
    kVRNotConst,
    /** @brief The const-ness of the VR is unknown */
    kVRUnknown
};

/**
 * @class ConstOffset
 * @brief Struct to store a basic const + offset set
 */
struct ConstOffset
{
    /** @brief The index in the constant table */
    unsigned char constIndex;

    /** @brief The end offset as an offset from the offsetStart */
    unsigned char offsetEnd;

    /** @brief The beginning offset of this constant*/
    unsigned short offsetStart;
};

/**
 * @class MethodContext
 * @brief MethodContext Keeps a method level context and information to be used by traces
 */
class MethodContext
{
private:
    /** @brief The Method */
    const Method *method;

    /* --------- Constructors, Destructor --------- */

    /**
     * @brief Regular constructors
     * @details unused
     */
    MethodContext (void);

    /**
     * @brief Copy constructor
     * @details unused
     */
    MethodContext (const MethodContext&);

    /**
     * @brief Assignment operator
     * @details unused
     * @return Reference to the assigned MethodContext
     */
    MethodContext& operator= (const MethodContext&);

    /**
     * @brief Constructor
     * @param method the method for which the context has the data
     * @return A new context for the method
     */
    MethodContext (const Method *method);

    /* -------- Elements needed for constant information ------ */

    /** @brief Map to keep information about constant VRs */
    std::map<int, std::vector<ConstOffset> > vRConstMap;

    /** @brief Map of VRs which are constant for the whole method */
    std::map<int, unsigned int> methodWideConstVRs;

    /** @brief The maximum number of constants we can have in each context */
    static unsigned int maxConstants;

    /** @brief The maximum constants that are possible, due to data structure limitations
     *  @details This value serves as the limit for the maxConstants value
     */
    const static unsigned int MAX_POSSIBLE_CONSTANTS = 255;

    /** @brief The maximum number of BasicBlocks we can have in a method
     * @details If the method for which we are constructing the context
     * has more than these many basic blocks, context will not be created.
     */
    static unsigned int maxBasicBlocks;

    /** @brief The hardcoded upper limit on basic blocks.
     *  @details Creating method context for very large methods will hurt
     *  compilation time, so we will avoid it.
     */
    const static unsigned int MAX_POSSIBLE_BASICBLOCKS = 3000;

    /** @brief The list of constants */
    std::vector<unsigned int> constants;

    /**
     * @brief Given a cUnit, collect information about the constants in the method
     * @param cUnit The CompilationUnit for the method
     * @return true if everything was fine
     */
    bool handleConstants (CompilationUnit *cUnit);

    /**
     * @brief Given a cUnit, collect the bytecodes which are the last defines of a VR
     * @details For now, we only look at the last MIR of the basic block and see if
     * that is the last use of a VR. This takes much less space and is faster to
     * collect. TODO: Generalize.
     * @param cUnit The CompilationUnit for the method
     */
    void handleEndOfUDChains (CompilationUnit *cUnit);

    /** @brief A map of the offset of last MIR of basic block to the VRs having last use there */
    std::map<unsigned int, std::set<unsigned int> > lastOffsetOfBBToLastUseVRMap;


    /**
     * @brief Get the index for a constant value
     * @details If the value is not found in the table,
     * and we have not exceeded the maxConstants limit, the value will
     * be added to the table
     * @param value The value for which we need the index
     * @return The index in the const table, or -1 if not found
     */
    int getIndexForConst (unsigned int value);

    /**
     * @brief Get the const at a given index
     * @details The index is guarded by the maxConstants value
     * @param index for the constant
     * @param[out] value The value at that index
     * @return whether a value was found at the index
     */
    bool getConstAtIndex (unsigned int index, unsigned int &value) const;

public:
    /**
     * @brief Returns the Method for which this context is created
     * @return The Method for this context
     */
    const Method *getMethod (void) const { return method; }

    /**
     * @brief Instance function
     * @param method The Method for which we need the context
     * @return the generated MethodContext, or null
     */
    static const MethodContext *createNewInstance (const Method *method);

    /**
     * @brief Get the const value
     * @param offset the offset where the const value is needed
     * @param vR the VR whose const value is needed
     * @param[out] value the value of the const
     * @return Whether a const value of the VR at that offset is found
     */
    bool getConstValueOfVR (unsigned int offset, int vR, unsigned int &value) const;

    /**
     * @brief Add the VR information to our temporary structures
     * @param vR The VR for which we need to add const information
     * @param value the 32-bit value as the const
     * @param offsetStart the beginning offset of the VRs constness
     * @param offsetEnd the end offset of the VRs constness
     * @return whether the VR could be updated successfully
     */
    bool updateVRConsts (int vR, unsigned int value, unsigned int offsetStart, unsigned int offsetEnd);

    /**
     * @brief Sets the maximum number of constants in the constant table
     * @param numConstants The number of constants requested
     * @return Whether we could set the value
     */
    static bool setMaxConstants (unsigned long numConstants);

    /**
     * @brief Sets the maximum number of basic blocks allowed in a method
     * @param numBasicBlocks Context will not be created if method has more basic blocks than this
     * @return Whether we could set the value
     */
    static bool setMaxBasicBlocks (unsigned long numBasicBlocks);

    /**
     * @brief Check if the offset matches the end of any live range for the VR
     * @param offset The offset to check as the end of live range
     * @param VR The VR to check
     * @return true if the offset is the end offset of a live range
     */
    bool isOffsetEndOfUDChain (unsigned int offset, u4 VR) const;

    /** @brief Print statistics about the MethodContext */
    void printStatistics (void) const;
};

#endif
