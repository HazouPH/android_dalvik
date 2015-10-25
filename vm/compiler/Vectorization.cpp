/*
* Copyright (C) 2013 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <vector>
#include <map>
#include <set>

#include "Dalvik.h"
#include "Compiler.h"
#include "CompilerIR.h"
#include "Dataflow.h"
#include "Pass.h"
#include "PassDriver.h"
#include "Vectorization.h"
#include "Utility.h"
#include "AccumulationSinking.h"
#include "Expression.h"

#define VECTORIZATION_LOG(cUnit,data,function) \
    do { \
        if (cUnit->printPass == true) { \
            function (cUnit, data); \
        } \
    } while (false)

//The elements must be ordered by size
enum VectorizedType
{
    kVectorizedNoType = 0,
    kVectorizedByte,
    kVectorizedShort,
    kVectorizedInt,
};

/**
 * @class RegisterAssociation
 * @brief RegisterAssociation is an association between Virtual Registers and vectorized registers
 */
struct RegisterAssociation
{
    /** @brief Vectorized register */
    int vectorized;

    /** @brief Is it an input? */
    bool input;

    /** @brief Is it an output? */
    bool output;
};

/**
 * @class VectorizationInfo
 * @brief VectorizationInfo contains information required for the vectorization pass
 */
struct VectorizationInfo
{
    /**
     * @brief Constructor
     */
    VectorizationInfo (void) :
            type (kVectorizedNoType), upperBound (0), scratchVrForTest (0)
    {
        registers.clear ();
        constants.clear ();
    }

    /**
     * @brief Destructor: ensures all memory is freed
     */
    ~VectorizationInfo (void)
    {
        registers.clear ();
        constants.clear ();
    }

    /** @brief Register Map of VRs requiring vectorization */
    std::map <int, RegisterAssociation> registers;

    /**
     * @brief Constant list of VRs requiring vectorization
     * @details Contains constants used in this basic block, except constant used only for the
     * bound check
     */
    std::map<int, int> constants;

    /** @brief Type for the vectorization */
    VectorizedType type;

    /** @brief Upper bound */
    int upperBound;

    /** @brief scratch register which can be used in generating main test and vectorized test */
    int scratchVrForTest;
};

/**
 * @brief Report any failure in the vectorization pass/gate
 * @param cUnit The CompilationUnit for the loop
 * @param message The message to report as a reason for failure
 */
static void reportFailure (const CompilationUnit * const cUnit, const char *message)
{
    ALOGI("JIT_INFO: Vectorization gate failed at %s%s@0x%02x: %s", cUnit->method->clazz->descriptor,
            cUnit->method->name, cUnit->entryBlock->startOffset, message);
}

/**
 * @brief Dump information about the XMM / VR / constants of vectorization
 * @param cUnit The CompilationUnit of the loop
 * @param info The VectorizationInfo containing the information
 */
static void dumpVectorRegisterUsage (const CompilationUnit * const cUnit, VectorizationInfo *info)
{
    ALOGI("\nVectorized loop info for %s%s@0x%02x:", cUnit->method->clazz->descriptor,
            cUnit->method->name, cUnit->entryBlock->startOffset);
    ALOGI("\t___Inputs___");

    for (std::map<int, RegisterAssociation>::iterator it = info->registers.begin();
            it != info->registers.end(); it++)
    {
        if (it->second.input == true)
        {
            ALOGI("\tv%d (XMM%d)", it->first, it->second.vectorized);
        }
    }

    ALOGI("\t___Outputs___");

    for (std::map<int, RegisterAssociation>::iterator it = info->registers.begin();
            it != info->registers.end(); it++)
    {
        if (it->second.output == true)
        {
            ALOGI("\tv%d (XMM%d)", it->first, it->second.vectorized);
        }
    }

    ALOGI("\t___Others___");

    for (std::map<int, RegisterAssociation>::iterator it = info->registers.begin();
            it != info->registers.end(); it++)
    {
        if (it->second.output == false && it->second.input == false)
        {
            ALOGI("\tv%d (XMM%d)", it->first, it->second.vectorized);
        }
    }

    ALOGI("\t___Constants___");

    for (std::map<int, int>::iterator it = info->constants.begin();
            it != info->constants.end(); it++)
    {
            ALOGI("\tvalue %d (XMM%d)", it->first, it->second);
    }
}

/**
* @brief a helper function to return association vectorized version for an opcode
* @param scalarOpcode the scalar version of opcode
* @return vectorized version of opcode or 0 if no match
*/
static ExtendedMIROpcode getVectorizedOpcode (Opcode scalarOpcode)
{
   ExtendedMIROpcode vectorizedOpcode = (ExtendedMIROpcode) 0;

   switch (scalarOpcode)
   {
       case OP_ADD_INT:
       case OP_ADD_INT_LIT8:
       case OP_ADD_INT_LIT16:
           vectorizedOpcode = kMirOpPackedAddition;
           break;
       case OP_MUL_INT:
       case OP_MUL_INT_LIT8:
       case OP_MUL_INT_LIT16:
           vectorizedOpcode = kMirOpPackedMultiply;
           break;
       case OP_SUB_INT:
       case OP_RSUB_INT:
       case OP_RSUB_INT_LIT8:
           vectorizedOpcode = kMirOpPackedSubtract;
           break;
       case OP_AND_INT:
       case OP_AND_INT_LIT8:
       case OP_AND_INT_LIT16:
           vectorizedOpcode = kMirOpPackedAnd;
           break;
       case OP_OR_INT:
       case OP_OR_INT_LIT8:
       case OP_OR_INT_LIT16:
           vectorizedOpcode = kMirOpPackedOr;
           break;
       case OP_XOR_INT:
       case OP_XOR_INT_LIT8:
       case OP_XOR_INT_LIT16:
           vectorizedOpcode = kMirOpPackedXor;
           break;
       default:
           // not supported yet
           break;
   }

   return vectorizedOpcode;
}

/**
 * @brief Check whether this MIR can remain in vectorized loop
 * @details The acception process is a whitelist and only allows a few select bytecodes.
 * No memory operations are accepted.
 * @param mir The MIR to check for
 * @return Whether this MIR can remain in the loop
 */
static bool isVectorizable (MIR *mir)
{
    ExtendedMIROpcode vectorizedOpcode = getVectorizedOpcode (mir->dalvikInsn.opcode);

    //First check if we can create a vectorized instruction for this opcode
    if (vectorizedOpcode == kMirOpPackedSubtract)
    {
        //Get instruction
        DecodedInstruction &insn = mir->dalvikInsn;
        // now only vA = vA - vB form is supported
        if (insn.vA == insn.vC) {
            return false;
        }
        return true;
    }
    else if (vectorizedOpcode != 0)
    {
        return true;
    }

    //We also allow conditionals in vectorized loops
    if (dvmCompilerIsOpcodeConditionalBranch (mir->dalvikInsn.opcode) == true)
    {
        return true;
    }

    //We also allow constants in the loop but not wide
    long long flags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];
    if ((flags & DF_DA_WIDE) != 0)
    {
        return false;
    }

    if ((flags & DF_SETS_CONST) != 0)
    {
        return true;
    }

    return false;
}

/**
 * @brief Does the instruction supported by isVectorizable use a constant in vC?
 * @param mir the MIR instruction
 * @return whether or not the instruction uses a constant in vC
 */
static bool isVectorizableInstructionUseConstant (MIR *mir)
{
    int opcode = mir->dalvikInsn.opcode;

    switch (opcode)
    {
        case OP_ADD_INT_LIT8:
        case OP_ADD_INT_LIT16:
        case OP_RSUB_INT_LIT8:
        case OP_RSUB_INT:
        case OP_MUL_INT_LIT8:
        case OP_MUL_INT_LIT16:
        case OP_AND_INT_LIT8:
        case OP_AND_INT_LIT16:
        case OP_OR_INT_LIT8:
        case OP_OR_INT_LIT16:
        case OP_XOR_INT_LIT8:
        case OP_XOR_INT_LIT16:
            return true;
        default:
            break;
    }
    return false;
}


/**
 * @brief Find the upper bound
 * @param info the LoopInformation
 * @param upperBound reference to where to store the upperBound
 * @return whether or not the function found the upper bound
 */
static bool findUpperBound (LoopInformation *info, int &upperBound)
{
    //Paranoid
    if (info == 0)
    {
        return false;
    }

    BasicBlock *bb = info->getEntryBlock();

    //Paranoid
    if (bb == 0)
    {
        return false;
    }

    //Make sure we have an "if" at the end
    MIR *lastMIR = bb->lastMIRInsn;
    if (lastMIR == 0)
    {
        return false;
    }

    Opcode opcode = lastMIR->dalvikInsn.opcode;
    if (opcode < OP_IF_EQ || opcode > OP_IF_LEZ)
    {
        return false;
    }

    //Get the SSA
    SSARepresentation *ssaRep = lastMIR->ssaRep;

    //Make sure we have only two uses
    if (ssaRep == 0 || ssaRep->numUses != 2)
    {
        return false;
    }

    //This will hold the const value if found
    int constValue = 0;

    //We don't care about wide constants
    int constValueIgnored = 0;

    bool isWide = false;

    //Check the MIR defining the first use
    MIR *mirUseFirst = ssaRep->defWhere[0];

    //Paranoid
    if (mirUseFirst == 0)
    {
        return false;
    }

    bool setsConst = dexGetConstant (mirUseFirst->dalvikInsn, constValue, constValueIgnored, isWide);

    if (setsConst == true && isWide == false)
    {
        upperBound = constValue;
        return true;
    }

    //Otherwise, check the other use
    MIR *mirUseSecond = ssaRep->defWhere[1];

    //Paranoid
    if (mirUseSecond == 0)
    {
        return false;
    }

    setsConst = dexGetConstant (mirUseSecond->dalvikInsn, constValue, constValueIgnored, isWide);

    if (setsConst == true && isWide == false)
    {
        upperBound = constValue;
        return true;
    }

    return false;
}

/**
 * @brief Set the input parameter of a VR to the specified value in the register association
 * @param info vectorization info structure
 * @param vr virtual register number
 * @param value The value to set the input to
 */
static void setInputRegister (VectorizationInfo *info, int vr, bool value)
{
    // First search the map to see if we have the entry for this vr
    std::map <int, RegisterAssociation>::iterator it = info->registers.find (vr);

    // If found, get the register association to update
    if (it != info->registers.end())
    {
        RegisterAssociation &association = it->second;
        association.input = true;
    }
    else
    {
        // Otherwise, insert a new entry for this vr
        RegisterAssociation newAssociation;
        newAssociation.vectorized = 0;
        newAssociation.input = true;
        newAssociation.output = false;
        info->registers[vr] = newAssociation;
    }
}

/**
 * @brief Set the output parameter of a VR to the specified value in the register association
 * @param info vectorization info structure
 * @param vr virtual register number
 * @param value The value to set the output to
 */
static void setOutputRegister (VectorizationInfo *info, int vr, bool value)
{
    // First search the map to see if we have the entry for this vr
    std::map <int, RegisterAssociation>::iterator it = info->registers.find (vr);

    // If found, get the register association to update
    if (it != info->registers.end())
    {
        RegisterAssociation &association = it->second;
        association.output = value;
    }
    else
    {
        //Otherwise, insert a new entry for this vr
        RegisterAssociation newAssociation;
        newAssociation.vectorized = 0;
        newAssociation.input = false;
        newAssociation.output = value;
        info->registers[vr] = newAssociation;
    }
}

/**
 * @brief Find type for the vectorization
 * @param cUnit the CompilationUnit
 * @param loopInfo the LoopInformation
 * @param info points to all known outputs
 * @return the type for the vectorization
 */
static VectorizedType findType (const CompilationUnit * const cUnit, LoopInformation *loopInfo, VectorizationInfo *info)
{
    BasicBlock *loopBB = loopInfo->getEntryBlock ();

    if (loopBB == 0)
    {
        return kVectorizedNoType;
    }

    //Go through all the MIR's of the BB and see if there is a cast
    //We cannot do vectorization if there is one
    for (MIR *mir = loopBB->firstMIRInsn; mir != 0; mir = mir->next)
    {
        Opcode opcode = mir->dalvikInsn.opcode;

        //Check if this is a cast
        if (opcode >= OP_INT_TO_LONG && opcode <= OP_INT_TO_SHORT)
        {
            return kVectorizedNoType;
        }
    }

    //Now we want to find a common type for all output VRs

    //Now check the loop exit blocks, and see if there are casts there
    BasicBlock *exitBlock = loopInfo->getExitBlock(cUnit);

    if (exitBlock == 0)
    {
        return kVectorizedNoType;
    }

    //Go through all the MIR's of the BB and see if there is a cast
    std::map<int, Opcode> cast4vr;
    for (MIR *mir = exitBlock->firstMIRInsn; mir != 0; mir = mir->next)
    {
        Opcode opcode = mir->dalvikInsn.opcode;

        //Check if this is a cast
        if (opcode >= OP_INT_TO_LONG && opcode <= OP_INT_TO_SHORT)
        {
            int vr = mir->dalvikInsn.vA;

            std::map <int, Opcode>::iterator it = cast4vr.find (vr);
            if (it != cast4vr.end ())
            {
                //Second cast to the same VR?
                if (it->second != opcode)
                {
                    VECTORIZATION_LOG (cUnit, "Two casts for the same VR", reportFailure);
                    return kVectorizedNoType;
                }
            }
            else
            {
                cast4vr[vr] = opcode;
            }
        }
    }

    //Now find common VectorizedType for all outputs
    VectorizedType type = kVectorizedNoType;
    std::map<int, RegisterAssociation> &registers = info->registers;
    for (std::map<int, RegisterAssociation>::const_iterator it = registers.begin (); it != registers.end (); it++)
    {
        //Get the output VR
        const RegisterAssociation &association = it->second;

        //Only care if it is an output
        if (association.output == true)
        {
            int vr = it->first;

            std::map<int, Opcode>::iterator iter = cast4vr.find (vr);
            if (iter != cast4vr.end ())
            {
                Opcode opcode = iter->second;

                //Determine type basing on cast
                VectorizedType cast2type = kVectorizedNoType;
                if (opcode == OP_INT_TO_BYTE)
                {
                    cast2type = kVectorizedByte;
                }
                else if (opcode == OP_INT_TO_SHORT)
                {
                    cast2type = kVectorizedShort;
                }
                else
                {
                    VECTORIZATION_LOG (cUnit, "Not supported cast", reportFailure);
                    return kVectorizedNoType;
                }

                //Use the highest type
                type = (cast2type > type) ? cast2type : type;
            }
            else
            {
                //No cast found so we must use kVectorizedInt as a highest type
                type = kVectorizedInt;
            }
        }
    }

    return type;
}

/**
 * @brief Convert a type to how many elements per iteration
 * @param type the type for the vectorization
 * @return the number of executions in parallel for the vectorization
 */
unsigned int convertTypeToHowManyPerIteration (VectorizedType type)
{
    switch (type)
    {
        case kVectorizedByte:
            return 8;
        case kVectorizedShort:
            return 8;
        case kVectorizedInt:
            return 4;
        default:
            return 1;
    }
}

/**
 * @brief Convert a type to size
 * @param type the type for the vectorization
 * @return the size in bytes for each type
 */
unsigned int convertTypeToSize (VectorizedType type)
{
    switch (type)
    {
        case kVectorizedByte:
        case kVectorizedShort:
            return 2;
        case kVectorizedInt:
            return 4;
        default:
            return 1;
    }
}

/**
 * @brief bb The BasicBlock of the loop
 * @param cUnit the CompilationUnit
 * @param loopInformation the LoopInformation
 * @param bb the unique BasicBlock of the loop
 * @param info the VectorizationInfo associated to the pass
 */
static bool fillVectorizationInformation (const CompilationUnit * const cUnit, LoopInformation *loopInformation, BasicBlock *bb, VectorizationInfo *info)
{
    //Paranoid
    if (bb == 0 || info == 0)
    {
        return false;
    }

    //Let's get the IV to begin with
    InductionVariableInfo *ivInfo = (InductionVariableInfo *) dvmGrowableListGetElement (& (loopInformation->getInductionVariableList()), 0);

    //Paranoid
    if (ivInfo == 0)
    {
        return false;
    }

    unsigned int vrIV = dvmExtractSSARegister (cUnit, ivInfo->basicSSAReg);
    int increment = ivInfo->loopIncrement;

    //It is a count up loop so this should not happen
    if (increment < 0)
    {
        return false;
    }

    //Since this is the IV increment, we will multiply it by the number of iterations we are able
    //to skip using vectorization
    increment *= convertTypeToHowManyPerIteration (info->type);

    //Add it as a constant
    //Add 0, the proper vectorized value will be filled up later
    info->constants[increment] = 0;

    //Go through the MIRs of the BB and fill up information
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get the SSA
        SSARepresentation *ssaRep = mir->ssaRep;

        //We cannot continue if there is no SSA information
        if (ssaRep == 0)
        {
            return false;
        }

        //For phi nodes, we have input and output registers both
        if (mir->dalvikInsn.opcode == static_cast<Opcode>(kMirOpPhi))
        {
            setInputRegister (info, static_cast<int>(mir->dalvikInsn.vA), true);
            setOutputRegister (info, static_cast<int>(mir->dalvikInsn.vA), true);
            continue;
        }

        //Now let's bail if we have any other bytecode which cannot be in a vectorized loop
        //We count on isVectorizable not allowing any memory operations.
        if (isVectorizable (mir) == false)
        {
            VECTORIZATION_LOG (cUnit, "MIR is not vectorizable", reportFailure);
            return false;
        }

        //If this the IV bytecode, ensure that this is the last use of the MIR (and do nothing else)
        if (mir->dalvikInsn.vA == vrIV && dvmCompilerIsOpcodeConditionalBranch (mir->dalvikInsn.opcode) == false)
        {
            //We should have non-wide defines
            assert (ssaRep->numDefs == 1);

            //We want to detect use of IV in the loop after its increment.
            //So we walk through the uses now.
            for (SUsedChain *uses = ssaRep->usedNext[0]; uses != 0; uses = uses->nextUse)
            {
                //We only care of uses in same loop as our IV increment.
                //The gate ensures that the loop has one basic block so this check is adequate.
                if (mir->bb == uses->mir->bb)
                {
                    //If we have a use that is not a phi node nor an if, then it becomes a bit more complicated
                    //in terms of vectorization since we increment IV with its new packed step but we actually
                    //need to use its single iteration addition form. Thus for now we reject this case.
                    if (dvmCompilerIsOpcodeConditionalBranch (uses->mir->dalvikInsn.opcode) == false
                            && uses->mir->dalvikInsn.opcode != static_cast<Opcode> (kMirOpPhi))
                    {
                        VECTORIZATION_LOG(cUnit, "Invalid use of IV after increment", reportFailure);
                        return false;
                    }
                }
            }

            //We don't need any other handling for the IV here.
            continue;
        }

        //Let's first look at constants
        bool isWide = false;
        int constValueLow;
        int constValueHigh;

        bool setsConst = dexGetConstant (mir->dalvikInsn, constValueLow, constValueHigh, isWide);

        //Fill the corresponding VRs
        if (setsConst == true && isWide == false)
        {
            //We actually don't care if this constant is the one for the IV
            SSARepresentation *ssaRep = mir->ssaRep;

            //Paranoid
            assert (ssaRep != 0);

            if (ssaRep == 0)
            {
                return false;
            }

            //Unmark its definition
            int defDalvikReg = dvmExtractSSARegister (cUnit, ssaRep->defs[0]);
            setOutputRegister (info, defDalvikReg, false);

            //Get its used chain
            SUsedChain *next = ssaRep->usedNext[0];

            //Skip it if no use
            if (next == 0)
            {
                continue;
            }

            MIR *used = next->mir;

            //If the use is the last MIR of the loop basic block, or belongs to another basic block, ignore it
            if (used->next == 0 || used->bb != mir->bb)
            {
                continue;
            }

            //If the next used is the conditional, we continue
            if (dvmCompilerIsOpcodeConditionalBranch (used->dalvikInsn.opcode) == true)
            {
                continue;
            }

            //So, it is being used by an instruction in the same BB, add a request for it
            info->constants[constValueLow] = 0;

            //Move to the next MIR, we don't handle the destination because we actually won't be using it here
            continue;
        }

        //Explicitly go through the defines to add them as outputs
        if (ssaRep->defs != 0)
        {
            for (int defIndex = 0; defIndex < ssaRep->numDefs; defIndex++)
            {
                //Get the defined dalvik reg
                int defDalvikReg = dvmExtractSSARegister (cUnit, ssaRep->defs[defIndex]);

                //Now set it as output
                setOutputRegister (info, defDalvikReg, true);
            }
        }

        //See if we have uses
        if (ssaRep->uses == 0)
        {
            return false;
        }

        //Go through all the uses
        int useIndex = ssaRep->numUses - 1;

        //For each use, check the defines
        while (useIndex >= 0)
        {
            //Get the used ssa register
            int ssaReg = ssaRep->uses[useIndex];

            int vrUsed = dvmExtractSSARegister (cUnit, ssaReg);

            //Get the defining MIR
            MIR *defMir = ssaRep->defWhere[useIndex];

            //If no define, this is an input
            if (defMir == 0)
            {
                setInputRegister (info, vrUsed, true);
            }
            else
            {
                //Otherwise, set it as output, except if the VR is a constant
                bool isNotAConst = (dvmCompilerDataFlowAttributes[defMir->dalvikInsn.opcode] & DF_SETS_CONST) == 0;
                setOutputRegister (info, vrUsed, isNotAConst);
            }

            //Go to the next use
            useIndex--;
        }

        //Handle constant case
        if (isVectorizableInstructionUseConstant (mir) == true)
        {
            //Add it to the constant map
            info->constants[mir->dalvikInsn.vC] = 0;
        }
    }

    //We have added the IV as an output due to the PHI node. Remove it now
    setOutputRegister (info, vrIV, false);

    //Find the type for the vectorization
    info->type = findType (cUnit, loopInformation, info);

    //If no type, we are done
    if (info->type == kVectorizedNoType)
    {
        return false;
    }

    //Since this is the IV increment, we will multiply it by the number of iterations we are able
    //to skip using vectorization
    increment *= convertTypeToHowManyPerIteration (info->type);

    //Add it as a constant
    //Add 0, the proper XMM value will be filled up later
    info->constants[increment] = 0;

    return true;
}


/**
 * @brief Find a vectorized temporary register
 * @param info the vectorization information
 * @return the vectorized register to be used as a temporary, -1 if not found
 */
static int findVectorTemporary (VectorizationInfo *info)
{
    int vectorRegister = -1;
    std::map<int, RegisterAssociation> &registers = info->registers;
    std::set<int> usedVectorRegisters;

    //Go through the registers to find unused vector we can use for temp
    for (std::map<int, RegisterAssociation>::const_iterator it = registers.begin (); it != registers.end (); it++)
    {
        usedVectorRegisters.insert (it->second.vectorized);
    }

    //Also go through the constants
    for (std::map<int, int>::const_iterator it = info->constants.begin (); it != info->constants.end (); it++)
    {
        usedVectorRegisters.insert (it->second);
    }

    //Now go through the available vector registers to see if we find a free one
    for (unsigned char reg = 0; reg < gDvmJit.vectorRegisters; reg++)
    {
        if (usedVectorRegisters.find (static_cast<int> (reg)) == usedVectorRegisters.end())
        {
            //We found one
            vectorRegister = static_cast<int> (reg);
            break;
        }
    }

    //We did not find a vector temporary
    return vectorRegister;
}


void handleInductionVariable (CompilationUnit *cUnit, LoopInformation *loopInfo, VectorizationInfo *info, BasicBlock *bb, int vr, MIR *vr2vectorized)
{
    //Theoretically, it would be better to not have to go through the list again
    int increment = loopInfo->getInductionIncrement (cUnit, vr);

    //Create the vectorized register we need here
    MIR *cst = dvmCompilerNewMIR ();

    //Paranoid
    assert (cst != 0);

    cst->dalvikInsn.opcode = static_cast<Opcode> (kMirOpConst128b);

    //Find a temporary
    cst->dalvikInsn.vA = findVectorTemporary (info);

    //We want to create the incremental constant
    int max = convertTypeToHowManyPerIteration (info->type);
    int size = convertTypeToSize (info->type);
    int current = 0;
    char args[16];

    //Memset args to 0
    memset (args, 0, sizeof (args));

    char *ptr = args;

    assert (size * max == sizeof (args));

    for (int i = 0; i < max; i++)
    {
        switch (size)
        {
            case 4:
                {
                    //It is 4 bytes for each type
                    int *iptr = (int *) (ptr);
                    *iptr = current;
                    //Increment ptr
                    ptr += sizeof (*iptr);
                }
                break;
            case 2:
                {
                    //It is 2 bytes for each type
                    short *sptr = (short *) (ptr);
                    *sptr = current;
                    //Increment ptr
                    ptr += sizeof (*sptr);
                }
                break;
            default:
                //Should never happen
                assert (0);
                break;
        }

        //Increment increment :)
        current += increment;
    }

    //Now copy it to the args
    memcpy (cst->dalvikInsn.arg, args, sizeof (args));

    dvmCompilerAppendMIR (bb, cst);

    //Finally we want to add to the IV this constant
    MIR *mir = dvmCompilerNewMIR ();

    //Paranoid
    assert (mir != 0);

    mir->dalvikInsn.opcode = static_cast<Opcode> (kMirOpPackedAddition);
    mir->dalvikInsn.vA = vr2vectorized->dalvikInsn.vA;
    mir->dalvikInsn.vB = cst->dalvikInsn.vA;
    mir->dalvikInsn.vC = vr2vectorized->dalvikInsn.vC;
    dvmCompilerAppendMIR (bb, mir);
}
/**
 * @brief Hoist the setup of the vectorization loop in bb
 * @param cUnit the CompilationUnit
 * @param loopInfo the LoopInformation
 * @param info the VectorizationInfo
 * @param bb the BasicBlock to hoist in
 */
static void hoistSetup (CompilationUnit *cUnit, LoopInformation *loopInfo, VectorizationInfo *info, BasicBlock *bb)
{
    //Paranoid
    if (info == 0 || info->type == kVectorizedNoType)
    {
        return;
    }

    //Get the lists
    std::map<int, RegisterAssociation> &registers = info->registers;
    std::map<int, int> &constants = info->constants;

    //Get an iterator for the list

    //First, go through the inputs, and generate the instructions to set
    for (std::map<int, RegisterAssociation>::const_iterator it = registers.begin(); it != registers.end(); it++)
    {

        //Get local association
        const RegisterAssociation &association = it->second;

        //If it isn't an input, skip it
        if (association.input == false)
        {
            continue;
        }

        MIR *mir = dvmCompilerNewMIR();

        //Paranoid
        assert (mir != 0);

        //Get the vectorized register
        mir->dalvikInsn.vA = association.vectorized;

        /*
         * If the VR is just an input, we do a PackedSet
         * Else we just load up 0s, since if it is an output
         * it has to be an accumulation, and the gate ensures that
         */
        if (association.output == true)
        {
            //We need a move data
            mir->dalvikInsn.opcode = static_cast<Opcode>(kMirOpConst128b);

            //We want to load 0 as the data
            mir->dalvikInsn.arg[0] = 0;
            mir->dalvikInsn.arg[1] = 0;
            mir->dalvikInsn.arg[2] = 0;
            mir->dalvikInsn.arg[3] = 0;

            dvmCompilerAppendMIR (bb, mir);
        }
        else
        {
            //We do a packed set of all input VRs
            mir->dalvikInsn.opcode = static_cast<Opcode>(kMirOpPackedSet);

            //Fill in the appropriate size
            mir->dalvikInsn.vC = convertTypeToSize (info->type);

            //Set the vR
            int vr = it->first;
            mir->dalvikInsn.vB = vr;

            dvmCompilerAppendMIR (bb, mir);

            //If we have an induction variable, we have a bit more work
            if (loopInfo->isBasicInductionVariable (cUnit, vr) == true)
            {
                handleInductionVariable (cUnit, loopInfo, info, bb, vr, mir);
            }
        }
    }

    //Now go through the constants and create constant extended ops
    for (std::map<int, int>::const_iterator it = constants.begin (); it != constants.end (); it++)
    {
        MIR *mir = dvmCompilerNewMIR ();

        //Create a packed data move
        mir->dalvikInsn.opcode = static_cast<Opcode>(kMirOpConst128b);

        //Get the const value
        int constValue = it->first;

        //Make the constant a 16-bit value if that is the size
        if (info->type == kVectorizedByte || info->type == kVectorizedShort)
        {
            constValue = constValue << 16;
            constValue |= ((it->first) & 0xFF);
        }

        mir->dalvikInsn.arg[0] = constValue;
        mir->dalvikInsn.arg[1] = constValue;
        mir->dalvikInsn.arg[2] = constValue;
        mir->dalvikInsn.arg[3] = constValue;

        //Get the vectorized register
        mir->dalvikInsn.vA = it->second;

        dvmCompilerAppendMIR (bb, mir);
    }
}

/**
 * @brief Sink the wrap up of the vectorization loop in bb
 * @param info the VectorizationInfo
 * @param bb the BasicBlock to sink in
 */
static void sinkWrapUp (VectorizationInfo *info, BasicBlock *bb)
{
    //Paranoid
    if (info == 0)
    {
        return;
    }

    std::map<int, RegisterAssociation> &registers = info->registers;

    //Convert all outputs to Packed Reduce
    for (std::map<int, RegisterAssociation>::const_iterator it = registers.begin (); it != registers.end (); it++)
    {
        //Get the output VR
        const RegisterAssociation &association = it->second;

        //Only care if it is an exit
        if (association.output == true)
        {
            int vr = it->first;

            MIR *mir = dvmCompilerNewMIR();

            mir->dalvikInsn.opcode = static_cast<Opcode>(kMirOpPackedAddReduce);

            mir->dalvikInsn.vA = vr;

            //Get the vectorized register
            mir->dalvikInsn.vB = association.vectorized;

            mir->dalvikInsn.vC = convertTypeToSize(info->type);

            dvmCompilerPrependMIR(bb, mir);
        }
    }
}

/**
 * @brief Check for inter-loop dependencies
 * @param cUnit the CompilationUnit containing the loop
 * @param info the LoopInformation
 * @return whether there is a loop interdependency
 */
static bool checkLoopDependency (const CompilationUnit * const cUnit, LoopInformation *info)
{
    BasicBlock *bb = info->getEntryBlock ();

    if (bb == 0)
    {
        return true;
    }

    //Get the basic IV of the loop
    unsigned int vrIV = dvmExtractSSARegister (cUnit, info->getSSABIV());

    MIR *phiIV = info->getPhiInstruction (cUnit, vrIV);
    if (phiIV == 0 || phiIV->ssaRep == 0 || phiIV->ssaRep->defs == 0)
    {
        return true;
    }
    int ssaVrIV = phiIV->ssaRep->defs[0];

    std::set<int> phiVRs;

    //Now go through all the Phi Nodes and check them
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //We only care about Phi nodes
        if (mir->dalvikInsn.opcode != static_cast<Opcode>(kMirOpPhi))
        {
            continue;
        }

        //Get the SSA representation
        SSARepresentation *ssaRep = mir->ssaRep;

        //Paranoid
        assert (ssaRep != 0 && ssaRep->defs != 0);

        phiVRs.insert(dvmExtractSSARegister (cUnit, ssaRep->defs[0]));

    }

    std::set<int> dirtyVRs;

    //Now go through the rest of the MIRs and mark them dirty
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        SSARepresentation *ssaRep = mir->ssaRep;

        if (ssaRep == 0)
        {
            return true;
        }

        //Go through all the uses of the MIR
        for (int i = 0 ; i < ssaRep->numUses ; i++)
        {
            unsigned int vrUse = dvmExtractSSARegister (cUnit, ssaRep->uses[i]);

            //Check if the use is dirty
            if (dirtyVRs.find (vrUse) != dirtyVRs.end ())
            {
                //Go through all the defs and mark them dirty
                for (int j = 0 ; j < ssaRep->numDefs ; j++)
                {
                    dirtyVRs.insert(dvmExtractSSARegister (cUnit, ssaRep->defs[j]));
                }
                continue;
            }

            //Otherwise, if the use is a phi node VR...
            if (phiVRs.find(vrUse) != phiVRs.end ())
            {
                //If it is not an IV
                if (vrUse != vrIV)
                {
                    //if all the defs are not the same VR
                    for (int j = 0 ; j < ssaRep->numDefs ; j++)
                    {
                        if (vrUse != dvmExtractSSARegister(cUnit, ssaRep->defs[j]))
                        {
                            //Mark it as dirty
                            dirtyVRs.insert(dvmExtractSSARegister (cUnit, ssaRep->defs[j]));
                        }
                    }
                }
                else
                {
                    //If it is IV we should mark all defs as dirty if use is not original phi node
                    if (ssaRep->uses[i] != ssaVrIV)
                    {
                        for (int j = 0 ; j < ssaRep->numDefs ; j++)
                        {
                            if (vrIV != dvmExtractSSARegister(cUnit, ssaRep->defs[j]))
                            {
                                //Mark it as dirty
                                dirtyVRs.insert(dvmExtractSSARegister (cUnit, ssaRep->defs[j]));
                            }
                        }
                    }
                }
            }
        }
    }

    //Now go through the two lists and see if there is anything common
    std::set<int>::iterator it;

    for (it = phiVRs.begin (); it != phiVRs.end (); it++)
    {
        if (dirtyVRs.find (*it) != dirtyVRs.end ())
        {
            PASS_LOG (ALOGI, cUnit, "JIT_INFO: We have a phi VR%u which is dirty", *it);
            return true;
        }
    }

    return false;
}

/**
 * @brief Does the loop have a safe accumulation or would it be unsafe to vectorize?
 * @details An unsafe accumulation is one whose result is used by another expression.
 * For example: a = a + b; c = c + a; the second accumulation is "unsafe" because
 * vectorization accumulates into broken up vectorized a and then reduces to the actual a.
 * Thus we would need to ensure to keep c = c + a around at the exit after the a reduction
 * but before the c reduction.
 * @param cUnit the CompilationUnit
 * @param loopInfo the LoopInformation
 * @param outputVR the output virtual register
 * @return whether or not the loop has a safe accumulation when considering a given outputVR
 */
static bool haveSafeAccumulation (CompilationUnit *cUnit, LoopInformation *loopInfo, unsigned int outputVR)
{
    //Get the phi associated with the output VR
    MIR *phi = loopInfo->getPhiInstruction (cUnit, outputVR);

    if (phi == 0)
    {
        //The output VR must be a phi node
        return false;
    }

    //Check that our only use is accumulation
    SSARepresentation *ssaRep = phi->ssaRep;

    if (ssaRep->numDefs != 1)
    {
        //We do not have logic to handle wide defines
        return false;
    }

    //Get the expressions for the Phi nodes and see if they are accumulations
    std::vector<Expression *> ivExpressions;
    dvmCompilerGetLoopExpressions (cUnit, loopInfo, ivExpressions);

    // if no expressions nothing to process
    if (ivExpressions.size() == 0) {
        return false;
    }

    bool outputVRAccumulationFound = false;
    for (std::vector<Expression *>::iterator it = ivExpressions.begin(); it != ivExpressions.end(); it++)
    {
        BytecodeExpression *bcExpression = static_cast<BytecodeExpression *>(*it);

        if (bcExpression == 0)
        {
            continue;
        }

        //Check whether this expression for our outputVR
        VirtualRegister *assignTo = bcExpression->getAssignmentTo ();
        if (assignTo == 0 || assignTo->isLinearAccumulation (cUnit, outputVR) != LinAccResVRSeen)
        {
            continue;
        }
        outputVRAccumulationFound = true;

        bool failure = bcExpression->isLinearAccumulation (cUnit, outputVR) == LinAccResError;

        if (failure == true)
        {
            //Our expression tree does not have a safe accumulation
            VECTORIZATION_LOG (cUnit, "Multiple uses of the accumulation VR", reportFailure);
            return false;
        }
    }

    if (outputVRAccumulationFound == false)
    {
        VECTORIZATION_LOG (cUnit, "Did not see the accumulation for output VR", reportFailure);
        return false;
    }

    //Everything is good
    return true;
}

static bool vectorizationGate (const CompilationUnit * const cUnit, LoopInformation *loopInfo, VectorizationInfo *info)
{
    //Check if we are looking at simple loop
    if (dvmCompilerVerySimpleLoopGateWithLoopInfo (cUnit, loopInfo) == false)
    {
        VECTORIZATION_LOG (cUnit, "The loop we have analyzed is not very simple.", reportFailure);
        return false;
    }

    //We can only handle loops with an increment of 1 to the IV
    if (loopInfo->isUniqueIVIncrementingBy1 () == false)
    {
        VECTORIZATION_LOG (cUnit, "Not an increment by 1 loop", reportFailure);
        return false;
    }

    //Get entry block
    BasicBlock *bb = loopInfo->getEntryBlock ();

    //Paranoid
    assert (bb != 0);

    // Request a scratch virtual register to use for generating test in vectorization
    int scratchVrForTest = dvmCompilerGetFreeScratchRegister (const_cast<CompilationUnit*>(cUnit), 1);

    // bail if no free scratch to use
    if (scratchVrForTest == -1)
    {
        VECTORIZATION_LOG (cUnit, "No scratch VR left to generate test", reportFailure);
        return false;
    }
    else
    {
        PASS_LOG (ALOGI, cUnit, "Obtained scratch register v%u for vectorization test", scratchVrForTest);
    }

    // Now we have a free scratch register to use, remember it in VectorizationInfo
    info->scratchVrForTest = scratchVrForTest;

    //For the moment we only handle count-up loops
    if (loopInfo->getCountUpLoop () == false)
    {
        VECTORIZATION_LOG (cUnit, "Is not a count up loop", reportFailure);
        return false;
    }

    //Find the upper bound
    bool foundUpperBound = findUpperBound (loopInfo, info->upperBound);

    //If problem, bail
    if (foundUpperBound == false)
    {
        VECTORIZATION_LOG (cUnit, "Cannot find loop upper bound", reportFailure);
        return false;
    }

    if (info->upperBound < gDvmJit.minVectorizedIterations)
    {
        VECTORIZATION_LOG (cUnit, "Not enough iterations in the vectorized loop", reportFailure);
        return false;
    }

    //Last check: interdependency
    if (checkLoopDependency (cUnit, loopInfo) == true)
    {
        VECTORIZATION_LOG (cUnit, "Inter loop dependency", reportFailure);
        return false;
    }

    //First job: gather input, output, constants
    if (fillVectorizationInformation (cUnit, loopInfo, bb, info) == false)
    {
        VECTORIZATION_LOG (cUnit, "Could not fill vectorization info", reportFailure);
        return false;
    }

    //Does the vectorization in the BE support the size
    if (dvmCompilerArchSupportsVectorizedPackedSize (convertTypeToSize (info->type)) == false )
    {
        VECTORIZATION_LOG (cUnit, "No architecture support for Vectorization type", reportFailure);
        return false;
    }

    //Now check if we have enough vectorized registers for this
    unsigned int count = info->registers.size () + info->constants.size ();
    if (count >= gDvmJit.vectorRegisters)
    {
        VECTORIZATION_LOG (cUnit, "Not enough vector registers", reportFailure);
        return false;
    }

    //We also need to check if it is safe to sink the accumulation on the output registers
    std::map <int, RegisterAssociation>::iterator it;
    for (it = info->registers.begin(); it != info->registers.end(); it++)
    {
        //For each output VR check if it is safe to accumulate (if accumulating)
        if (it->second.output == true)
        {
            if (haveSafeAccumulation ((CompilationUnit *) cUnit, loopInfo, static_cast<unsigned int>(it->first)) == false)
            {
                VECTORIZATION_LOG (cUnit, "Unsafe accumulation for vectorization", reportFailure);
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Create the main test: the test performed before entering the non vectorized loop
 * @param cUnit The CompilationUnit
 * @param loopInfo the LoopInformation
 * @param info the VectorizationInfo
 * @return Returns a BasicBlock containing the test to know if the code should enter the non vectorized loop
 */
static BasicBlock *createMainTest (CompilationUnit *cUnit, LoopInformation *loopInfo, VectorizationInfo *info)
{
    if (loopInfo == 0 || info == 0)
    {
        //bail
        return 0;
    }

    //Get the basic block of the loop
    BasicBlock *bb = loopInfo->getEntryBlock ();

    //Get the if bytecode
    MIR *ifMIR = bb->lastMIRInsn;

    //Duplicate it
    MIR *copyIF = dvmCompilerCopyMIR (ifMIR);

    //Get the ssa representation
    SSARepresentation *ssaRep = ifMIR->ssaRep;

    //Paranoid
    assert (ssaRep != 0);

    //We also need the constant: again the gate should check if it's here
    MIR *constMIR = 0;

    //Paranoid
    assert (ssaRep->numUses > 0);

    //Stubs for 'dexGetConstant'
    bool isWideIgnored;
    int constValueLowIgnored;
    int constValueHighIgnored;

    //We ignore everything except the fact whether the instruction defines constant or not
    bool constIsFirst = dexGetConstant (ssaRep->defWhere[0]->dalvikInsn, constValueLowIgnored,
                                        constValueHighIgnored, isWideIgnored);

    if (constIsFirst == true)
    {
        //Const is the first use of MIR
        constMIR = ssaRep->defWhere[0];

        // replace the use which is not induction variable in copyIF mir with scratch register
        copyIF->dalvikInsn.vA = info->scratchVrForTest;
    }
    else
    {
        //Paranoid
        assert (ssaRep->numUses > 1);

        //Const is the second use of MIR
        constMIR = ssaRep->defWhere[1];

        // replace the use which is not induction variable in copyIF mir with scratch register
        copyIF->dalvikInsn.vB = info->scratchVrForTest;
    }

    //Paranoid
    assert (constMIR != 0);

    //Duplicate it
    MIR *copyConstMIR = dvmCompilerCopyMIR (constMIR);

    // replace def in constMIR with scratch register
    copyConstMIR->dalvikInsn.vA = info->scratchVrForTest;

    //Now create a BasicBlock with these instructions
    BasicBlock *res = dvmCompilerNewBBinCunit (cUnit, kDalvikByteCode);

    //Append the MIRs
    dvmCompilerAppendMIR (res, copyConstMIR);
    dvmCompilerAppendMIR (res, copyIF);

    return res;
}

/**
 * @brief Create a vectorized test: the test performed before entering the vectorized loop
 * @param cUnit The CompilationUnit
 * @param loopInfo the LoopInformation
 * @param info the VectorizationInfo
 * @return Returns a BasicBlock containing the test to know if the code should enter the vectorized loop
 */
static BasicBlock *createVectorizedTest (CompilationUnit *cUnit, LoopInformation *loopInfo, VectorizationInfo *info)
{
    BasicBlock *bb = createMainTest (cUnit, loopInfo, info);

    //Paranoid
    if (bb == 0)
    {
        return 0;
    }

    //Now just get the opcode for the const
    MIR *first = bb->firstMIRInsn;

    //Paranoid
    assert (first != 0 && (dvmCompilerDataFlowAttributes[first->dalvikInsn.opcode] & DF_SETS_CONST) != 0);

    //Now just get the vectorized upper bound
    unsigned int howManyPerIteration = convertTypeToHowManyPerIteration (info->type);

    //Update upper bound
    first->dalvikInsn.vB -= howManyPerIteration;

    return bb;
}

/**
 * @brief Form the vectorized loop
 * @param vectorizedTest the test before the vectorized loop
 * @param vectorizedPreHeader the preheader block for the vectorized loop
 * @param vectorizedBB the vectorized BB
 * @param vectorizedExit the exit basic block for the vectorized loop
 * @param bwcc the Backward Chaining Cell block
 */
static void formVectorizedLoop (BasicBlock *vectorizedTest,
                                BasicBlock *vectorizedPreHeader,
                                BasicBlock *vectorizedBB,
                                BasicBlock *vectorizedExit,
                                BasicBlock *bwcc)
{
    //The preheader goes to the loop
    vectorizedPreHeader->fallThrough = vectorizedBB;

    //The loop either goes to the exit or the bwcc, which depends on where the bwcc is
    if (vectorizedBB->fallThrough->blockType == kChainingCellBackwardBranch)
    {
        vectorizedBB->fallThrough = bwcc;
        vectorizedBB->taken = vectorizedExit;

        vectorizedTest->fallThrough = vectorizedPreHeader;
        vectorizedTest->taken = 0;
    }
    else
    {
        vectorizedBB->taken = bwcc;
        vectorizedBB->fallThrough = vectorizedExit;

        vectorizedTest->fallThrough = 0;
        vectorizedTest->taken = vectorizedPreHeader;
    }

    //Link bwcc to vectorized loop
    bwcc->fallThrough = vectorizedBB;
}

/**
 * @brief Form the vectorized loop
 * @param normalTest the test before the normal loop
 * @param preHeader the preheader block for the normal loop
 * @param bb the BasicBlock
 * @param exit the exit basic block for the normal loop
 */
static void formNormalLoop (BasicBlock *normalTest,
                       BasicBlock *preHeader,
                       BasicBlock *bb,
                       BasicBlock *exit)
{
    if (bb->fallThrough->blockType == kDalvikByteCode)
    {
        normalTest->fallThrough = exit;
        normalTest->taken = preHeader;
    }
    else
    {
        normalTest->taken = exit;
        normalTest->fallThrough = preHeader;
    }

    //Preheader goes to the loop
    preHeader->fallThrough = bb;
}

/**
 * @brief Link the loops together
 * @param vectorizedTest the test before the vectorized loop
 * @param vectorizedExit the exit basic block for the vectorized loop
 * @param normalTest the test before the normal loop
 * @param postExit the post exit basic block
 */
static void linkBlocks (BasicBlock *vectorizedTest,
                        BasicBlock *vectorizedExit,
                        BasicBlock *normalTest,
                        BasicBlock *postExit)
{
    //Link the vectorized test's uninitialized branch to normalTest
    if (vectorizedTest->taken == 0)
    {
        vectorizedTest->taken = normalTest;
    }
    else
    {
        vectorizedTest->fallThrough = normalTest;
    }
    //So will the vectorized exit
    vectorizedExit->fallThrough = normalTest;
}

/**
 * @brief Handle the generation of vectorization instruction for alu operation with literal
 * @details Generates a vectorized extended MIR equivalent to the int alu operation
 * @param cUnit the CompilationUnit
 * @param loopInformation the LoopInformation
 * @param info the VectorizationInfo
 * @param vectorizedBB the BasicBlock which is getting vectorized
 * @param mir the MIR instruction
 * @param size of the vectorization in bytes
 */
static void handleAluLiteral (CompilationUnit *cUnit, LoopInformation *loopInformation, VectorizationInfo *info,
        BasicBlock *vectorizedBB, MIR *mir, unsigned int size)
{
    //Get the opcode
    Opcode opcode = mir->dalvikInsn.opcode;

    //Let's see if this is a basic IV
    if (loopInformation->isBasicInductionVariable (cUnit, mir->dalvikInsn.vA) == true)
    {
        MIR *vectorizedIV = dvmCompilerNewMIR ();

        //We need to generate an IV increment adjusted to the vectorization.
        //Basically look for the vectorized register containing the constant increment adjusted
        //to the number of iterations skipped due to vectorization.
        int increment = mir->dalvikInsn.vC;
        increment *= convertTypeToHowManyPerIteration (info->type);

        std::map <int, int>::iterator itConst = info->constants.find (increment);

        //Convert the IV increment to a regular vectorized addition
        DecodedInstruction &insn = vectorizedIV->dalvikInsn;

        insn.opcode = static_cast<Opcode> (getVectorizedOpcode (opcode));

        std::map <int, RegisterAssociation>::iterator itA = info->registers.find (mir->dalvikInsn.vA);
        RegisterAssociation &associationA = itA->second;
        insn.vA = associationA.vectorized;
        insn.vB = itConst->second;

        insn.vC = size;

        //Now add it before
        dvmCompilerInsertMIRBefore (mir->bb, mir, vectorizedIV);

        //Our non-vectorized increment now skips iterations
        mir->dalvikInsn.vC = convertTypeToHowManyPerIteration(info->type);

        return;
    }
    else
    {
        //This is not an BIV. We need to transform it into one vector alu operation using the constant vectorized register
        std::map <int, int>::iterator itConst = info->constants.find (mir->dalvikInsn.vC);

        //Get instruction
        DecodedInstruction &insn = mir->dalvikInsn;

        // source are not equal to destination, we need to generate a kMirOpMove128b
        if (insn.vA != insn.vB) {

            // create a new mir
            MIR* newMir = dvmCompilerNewMIR();
            DecodedInstruction &newInsn = newMir->dalvikInsn;

            // set the opcode to be kMirOpMove128b
            newMir->dalvikInsn.opcode = static_cast<Opcode> (kMirOpMove128b);

            // set the vA for new mir
            std::map <int, RegisterAssociation>::iterator itA = info->registers.find (insn.vA);
            RegisterAssociation &associationA = itA->second;
            newInsn.vA = associationA.vectorized;

            // set the vB for new mir
            if (opcode == OP_RSUB_INT || opcode == OP_RSUB_INT_LIT8)
            {
                newInsn.vB = itConst->second;
            }
            else
            {
                std::map<int, RegisterAssociation>::iterator itB = info->registers.find (insn.vB);
                RegisterAssociation &associationB = itB->second;
                newInsn.vB = associationB.vectorized;
            }

            // insert it before current mir
            dvmCompilerInsertMIRBefore(vectorizedBB, mir, newMir);
        }

        //Now change this MIR
        mir->dalvikInsn.vA = (info->registers.find (mir->dalvikInsn.vA)->second).vectorized;
        if (opcode == OP_RSUB_INT || opcode == OP_RSUB_INT_LIT8)
        {
            mir->dalvikInsn.vB = (info->registers.find (mir->dalvikInsn.vB)->second).vectorized;
        }
        else
        {
            mir->dalvikInsn.vB = itConst->second;
        }
        mir->dalvikInsn.vC = convertTypeToSize(info->type);
        mir->dalvikInsn.opcode = static_cast<Opcode>(getVectorizedOpcode (mir->dalvikInsn.opcode));
    }
}

/**
 * @brief Handle the generation of vectorization instruction for alu operation
 * @details Generates a vectorized extended MIR equivalent to the int alu operation
 * @param cUnit the CompilationUnit
 * @param info the VectorizationInfo
 * @param vectorizedBB the BasicBlock which is getting vectorized
 * @param mir the MIR instruction
 * @param size of the vectorization in bytes
 */
static void handleAlu (CompilationUnit *cUnit, VectorizationInfo *info, BasicBlock *vectorizedBB, MIR *mir,
        unsigned int size)
{
    //Get opcode
    Opcode opcode = mir->dalvikInsn.opcode;

    //Get vectorized version of the opcode
    ExtendedMIROpcode vectorizedOpcode = getVectorizedOpcode (opcode);

    //If we found a matched vectorized version, update current mir to vectorized version
    if (vectorizedOpcode != 0) {

        //Get instruction
        DecodedInstruction &insn = mir->dalvikInsn;

        insn.opcode = static_cast<Opcode> (vectorizedOpcode);
        int srcVr2 = insn.vC;

        if (insn.vA == insn.vC) {
            srcVr2 = insn.vB;
        }

        //Both sources are not equal to destination, we need to generate a kMirOpMove128b
        else if (insn.vA != insn.vB) {

            int srcVr1 = insn.vB;

            // create a new mir
            MIR* newMir = dvmCompilerNewMIR();
            DecodedInstruction &newInsn = newMir->dalvikInsn;

            // set the opcode to be kMirOpMove128b
            newMir->dalvikInsn.opcode = static_cast<Opcode> (kMirOpMove128b);

            // set the vA for new mir
            std::map <int, RegisterAssociation>::iterator itA = info->registers.find (insn.vA);
            RegisterAssociation &associationA = itA->second;
            newInsn.vA = associationA.vectorized;

            // set the vB for new mir
            std::map <int, RegisterAssociation>::iterator itB = info->registers.find (srcVr1);
            RegisterAssociation &associationB = itB->second;
            newInsn.vB = associationB.vectorized;

            // insert it before current mir
            dvmCompilerInsertMIRBefore(vectorizedBB, mir, newMir);
        }

        // find destination vectorized Reg
        std::map <int, RegisterAssociation>::iterator itA = info->registers.find (insn.vA);
        RegisterAssociation &associationA = itA->second;
        insn.vA = associationA.vectorized;

        // find source vectorized Reg
        std::map <int, RegisterAssociation>::iterator itC = info->registers.find (srcVr2);
        RegisterAssociation &associationC = itC->second;
        insn.vB = associationC.vectorized;

        // fill the size info
        mir->dalvikInsn.vC = size;
    }
}

/**
 * @brief Find a temporary VR that is used for vectorization constant placeholder
 * @param cUnit The compilation unit
 * @param info The vectorization information
 * @return the temporary VR that will never live by end of pass
 */
static unsigned int findTemporaryVRForConstant (const CompilationUnit * const cUnit, VectorizationInfo *info)
{
    //We are creating a temporary that will die in the vectorized loop.
    //But we want a number that won't clash with any of the virtual registers numbers.
    //So we begin the temporary search from the end of the cUnit VR numbers.
    unsigned int temporary = cUnit->numDalvikRegisters + 1;

    //Get an iterator
    std::map<int, RegisterAssociation>::const_iterator found;

    //Get the register map
    std::map<int, RegisterAssociation> &registers = info->registers;

    do
    {
        //Augment the counter
        temporary++;

        //Did we find it or not?
        found = registers.find (temporary);

        //Start again if we did
    } while (found != registers.end ());

    //Return the register we can use
    return temporary;
}

/**
 * @brief Handle the vectorization of a constant
 * @param cUnit the CompilationUnit
 * @param loopInformation the LoopInformation
 * @param info the VectorizationInfo
 * @param mir the MIR instruction
 * @return did we remove the instruction?
 */
static void handleConstant (CompilationUnit *cUnit, LoopInformation *loopInformation, VectorizationInfo *info, MIR *mir)
{
    //If this is a const, check if this is the loop bound. If it is, this needs to be adjusted
    SSARepresentation *ssaRep = mir->ssaRep;

    //Get the IV
    InductionVariableInfo *ivInfo = (InductionVariableInfo *) dvmGrowableListGetElement (& (loopInformation->getInductionVariableList()), 0);

    unsigned int vrIV = dvmExtractSSARegister (cUnit, ivInfo->basicSSAReg);

    if (ssaRep == 0)
    {
        if (mir->copiedFrom != 0)
        {
            ssaRep = mir->copiedFrom->ssaRep;
        }

        if (ssaRep == 0)
        {
            VECTORIZATION_LOG (cUnit, "Cannot find ssa representation", reportFailure);
            return;
        }
    }

    //Find the MIR where this is used
    if (ssaRep->usedNext != 0 && ssaRep->usedNext[0] != 0)
    {
        //Ok it is being used, let's find out where
        MIR *useMIR = ssaRep->usedNext[0]->mir;

        //Paranoid
        assert (useMIR != 0);

        //Ok, is it being used by the if?
        if (dvmCompilerIsOpcodeConditionalBranch (useMIR->dalvikInsn.opcode) == true)
        {
            if (useMIR->dalvikInsn.vA == vrIV || useMIR->dalvikInsn.vB == vrIV)
            {
                //We are sure this const is setting the loop bounds. Adjust it.
                mir->dalvikInsn.vB -= convertTypeToHowManyPerIteration(info->type);
            }
        }
        else
        {
            //We are dealing with a constant that is not used by the loop's if.
            //However, it is used by instructions that will become vectorized so we need to ensure that at
            //some point we fill a vector register with this constant. So what we do first is we rewrite
            //all users of the constant to actually use a non-existent virtual register that we track. This
            //register we track is the one associated with the vectorized register that holds the constant
            //that vectorized operations needing the constant will themselves use.

            //So first we get a temporary VR we can use that does not exist in cUnit
            unsigned int tempVR = findTemporaryVRForConstant (cUnit, info);

            //Then we remember the old VR that the const bytecode was writing to
            unsigned int oldVR = mir->dalvikInsn.vA;

            //Now we rewrite all of the MIRs in the loop's basic block to use this new VR that we are using
            //solely for tracking the constant. All the instructions that we rewrite will be transformed
            //into vectorized instructions.
            bool result = dvmCompilerRewriteMirDef (mir, oldVR, tempVR, true, true);

            //The const bytecode was writing to a virtual register that will still be live out
            //So now we restore the const define solely because it is the only one that doesn't
            //need a reduce, since we know the constant already.
            mir->dalvikInsn.vA = oldVR;

            //Paranoid
            assert (result == true);

            //Finally, assign to the register world the tempVR and assign to it the right constant vectorized register
            bool isWide = false;
            int constValueLow;
            int constValueHigh;
            bool setsConst = dexGetConstant (mir->dalvikInsn, constValueLow, constValueHigh, isWide);

            //Paranoid: the gate should have said no to wide constants
            assert (setsConst == true && isWide == false);

            //Unused in non assert world
            (void) setsConst;
            (void) isWide;

            //Since we preserved the const bytecode, we do not set the temporary VR as an output one.
            setOutputRegister (info, tempVR, false);

            //Now get the vectorized register and track it
            int vectorizedRegister = info->constants[constValueLow];
            info->registers[tempVR].vectorized = vectorizedRegister;

            //Unused in non assert world
            (void) result;
        }
    }
}

/**
  * @brief Transform mir in the vectorized loop
  * @param cUnit CompilationUnit
  * @param loopInformation the LoopInformation
  * @param info vectorization information
  * @param vectorizedBB the vectorized BB
  * @param mir current mir need to transformed
  */
static void transformMirVectorized (CompilationUnit *cUnit,
                                    LoopInformation *loopInformation,
                                    VectorizationInfo *info,
                                    BasicBlock *vectorizedBB,
                                    MIR* mir)
{
    //Get size
    int size = convertTypeToSize (info->type);

    //Get the opcode
    Opcode opcode = mir->dalvikInsn.opcode;

    switch (opcode) {
        case OP_ADD_INT_LIT8:
        case OP_ADD_INT_LIT16:
        case OP_RSUB_INT:
        case OP_RSUB_INT_LIT8:
        case OP_MUL_INT_LIT8:
        case OP_MUL_INT_LIT16:
        case OP_AND_INT_LIT8:
        case OP_AND_INT_LIT16:
        case OP_OR_INT_LIT8:
        case OP_OR_INT_LIT16:
        case OP_XOR_INT_LIT8:
        case OP_XOR_INT_LIT16:
            handleAluLiteral (cUnit, loopInformation, info, vectorizedBB, mir, size);
            break;

        case OP_ADD_INT:
        case OP_SUB_INT:
        case OP_MUL_INT:
        case OP_AND_INT:
        case OP_OR_INT:
        case OP_XOR_INT:
            handleAlu (cUnit, info, vectorizedBB, mir, size);
            break;

        case OP_CONST:
        case OP_CONST_4:
        case OP_CONST_16:
            handleConstant (cUnit, loopInformation, info, mir);
            break;
        //For all the other opcodes, skipped it for now for vectorization transformation
        default:
            return;
    }
    return;
}

/**
  * @brief Transform Form the vectorized loop
  * @param cUnit CompilationUnit
  * @param loopInformation the LoopInformation
  * @param info vectorization information
  * @param vectorizedPreHeader the preheader for the vectorized loop
  * @param vectorizedBB the vectorized BB
  * @param vectorizedExit the vectorized Exit
  * @param bwcc The backward chaining cell block of the loop
  * @return whether the transformation was successful
  */
static bool transformVectorized (CompilationUnit *cUnit,
                                 LoopInformation *loopInformation,
                                 VectorizationInfo *info,
                                 BasicBlock *vectorizedPreHeader,
                                 BasicBlock *vectorizedBB,
                                 BasicBlock *vectorizedExit,
                                 BasicBlock *bwcc)
{
    //Hoist set up before loop
    hoistSetup (cUnit, loopInformation, info, vectorizedPreHeader);

    //Sink wrap up before loop
    sinkWrapUp (info, vectorizedExit);
    sinkWrapUp (info, bwcc);

    if (vectorizedBB == 0)
        return false;

    // Scan each instructions
    for (MIR *mir = vectorizedBB->firstMIRInsn; mir != 0; mir = mir->next)
    {
        transformMirVectorized(cUnit, loopInformation, info, vectorizedBB, mir);
    }

    return true;
}

/**
 * @brief Assign vectorized registers for each VR in the Vectorized information
 * @param info the Vectorized information
 */
static void assignVectorizedRegisters (VectorizationInfo *info)
{
    int cnt = 0;

    for (std::map<int, RegisterAssociation>::iterator it = info->registers.begin (); it != info->registers.end (); it++)
    {
        //Get a local version
        RegisterAssociation &association = it->second;

        association.vectorized = cnt;
        cnt++;
    }

    for (std::map<int, int>::iterator it = info->constants.begin (); it != info->constants.end (); it++)
    {
        it->second = cnt;
        cnt++;
    }

    //Assert because normally the gate ensured this was fine
    assert (cnt < gDvmJit.vectorRegisters);
}

/**
 * @brief Update the dalvik bytecode predecessors of a BasicBlock to now go to another
 * @param cUnit The CompilationUnit
 * @param orig the original destination of the predecessors
 * @param newDest the new destination of the predecessors
 */
static void updatePredecessors (CompilationUnit *cUnit, BasicBlock *orig, BasicBlock *newDest)
{
    //Get the predecessors
    BitVector *predecessors = orig->predecessors;

    //Now walk them
    BitVectorIterator iterator;
    dvmBitVectorIteratorInit (predecessors, &iterator);
    BasicBlock *predecessor = dvmCompilerGetNextBasicBlockViaBitVector (iterator, cUnit->blockList);

    while (predecessor != 0)
    {

        //The we want to change the link
        if (predecessor->taken == orig)
        {
            predecessor->taken = newDest;
        }
        else
        {
            predecessor->fallThrough = newDest;
        }


        //Get next predecessor
        predecessor = dvmCompilerGetNextBasicBlockViaBitVector (iterator, cUnit->blockList);
    }
}

static bool vectorizeHelper (CompilationUnit *cUnit, LoopInformation *loopInformation, void *data)
{
    //The gate will be filling vectorization info so we set it up on stack
    VectorizationInfo info;

    //Now check with the gate if we can actually vectorize this loop
    if (vectorizationGate (cUnit, loopInformation, &info) == false)
    {
        //We cannot vectorize but we return true so we can continue looking through rest of loops
        return true;
    }

    //First: get the Loop's BB
    BasicBlock *bb = loopInformation->getEntryBlock ();
    //Second: get the loop's preheader
    BasicBlock *preheader = loopInformation->getPreHeader ();
    //Third: get the loop's exit
    BasicBlock *exit = loopInformation->getExitBlock (cUnit);
    //Third: get the loop's post-exit
    BasicBlock *postExit = loopInformation->getPostExitBlock (cUnit);
    //Fourth: get the BWCC
    BasicBlock *bwcc = loopInformation->getBackwardBranchBlock (cUnit);

    //Huge paranoid
    if (bb == 0 || preheader == 0 || exit == 0 || postExit == 0 || bwcc == 0)
    {
        //We let it continue to the next loop
        return true;
    }

    //We want a copy of all of these
    BasicBlock *copyBasicBlock = dvmCompilerCopyBasicBlock (cUnit, bb);
    BasicBlock *copyPreHeader = dvmCompilerCopyBasicBlock (cUnit, preheader);
    BasicBlock *copyExit = dvmCompilerCopyBasicBlock (cUnit, exit);
    BasicBlock *copyBWCC = dvmCompilerCopyBasicBlock (cUnit, bwcc);

    //We want two test blocks: main block test and vectorized version
    BasicBlock *mainTest = createMainTest (cUnit, loopInformation, &info);

    //We want the vectorized test
    BasicBlock *vectorizedTest = createVectorizedTest (cUnit, loopInformation, &info);

    //Be sure the basic blocks have been created
    if (copyBasicBlock == 0 || copyPreHeader == 0 || copyExit == 0 || copyBWCC == 0 || vectorizedTest == 0 || mainTest == 0)
    {
        //We let it continue to the next loop
        return true;
    }

    //Now assign the vectorized registers
    assignVectorizedRegisters (&info);

    //Now log information about the vector registers
    VECTORIZATION_LOG (cUnit, &info, dumpVectorRegisterUsage);

    //Update all predecessors of the preheader to now go to the vectorized test
    updatePredecessors (cUnit, preheader, vectorizedTest);

    //First assemble all vectorized blocks together to form the loop
    formVectorizedLoop (vectorizedTest, copyPreHeader, copyBasicBlock, copyExit, copyBWCC);

    //Now the normal loop no longer needs the bwcc because it will finish automatically
    //Therefore, let's form the new one
    formNormalLoop (mainTest, preheader, bb, postExit);

    //We want to link things together
    linkBlocks (vectorizedTest, copyExit, mainTest, postExit);

    //At this point, recalculate SSA now: no filtering but do update the loop information
    dvmCompilerCalculateBasicBlockInformation (cUnit, false, true);

    //We have the CFG up now, what we want to do is now transform the vectorized loop into... a vectorized loop
    transformVectorized (cUnit, loopInformation, &info, copyPreHeader, copyBasicBlock, copyExit, copyBWCC);

    return true;
}

void dvmCompilerVectorize (CompilationUnit *cUnit, Pass *pass)
{
    //Now let's go through the loop information
    LoopInformation *info = cUnit->loopInformation;

    //Now try to sink accumulations
    if (info != 0)
    {
        info->iterate (cUnit, vectorizeHelper, pass);
    }
}
