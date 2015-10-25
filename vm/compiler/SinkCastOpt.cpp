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

#include <set>
#include <map>

#include "BBOptimization.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "Loop.h"
#include "LoopInformation.h"
#include "Pass.h"
#include "PassDriver.h"
#include "Utility.h"
#include <set>

#define SINK_CAST_LOG(cUnit,data,function) \
    do { \
        if (cUnit->printPass == true) { \
            function (cUnit, data); \
        } \
    } while (false)

/**
 * @brief Used to report failure of applying cast sinking optimization pass
 * @param cUnit The compilation unit
 * @param message The message to display with failure
 */
static void reportSinkCastFailure (const CompilationUnit *cUnit, const char *message)
{
    ALOGI("JIT_INFO: Sink cast failure for %s%s@0x%02x: %s", cUnit->method->clazz->descriptor,
            cUnit->method->name, cUnit->entryBlock->startOffset, message);
}

/**
 * @brief Used to report success of sunk cast
 * @param cUnit The compilation unit
 * @param opcode The opcode of sunk cast
 */
static void reportSunkCast (const CompilationUnit *cUnit, Opcode opcode)
{
    ALOGI ("JIT_INFO: Sinking %s for %s%s@0x%02x", dexGetOpcodeName(opcode), cUnit->method->clazz->descriptor,
            cUnit->method->name, cUnit->entryBlock->startOffset);
}

/**
 * @brief Check if the cast operation on an IV is valid
 * @details Verify that the loop bounds being compared to the
 * IV are consistent with the cast operation applied to it.
 * For example an IV casted to byte should be compared to values < 127
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param ifMir the MIR instruction which contains one use need to be checked
 * @param castMir the MIR instruction which contains cast bytecode
 * @return whether mir passed the check for the cast sinking
 */
static bool checkValidCastingForIV(CompilationUnit *cUnit, LoopInformation *info,
        MIR *ifMir, const MIR *const castMir)
{
    // check if induction variable is a count-up
    if (info->getCountUpLoop () == false)
    {
        SINK_CAST_LOG (cUnit, "Loop is not count up", reportSinkCastFailure);
        return false;
    }

    DecodedInstruction &ifInsn = ifMir->dalvikInsn;

    //Get the SSA
    SSARepresentation *ssaRep = ifMir->ssaRep;

    // null check
    if (ssaRep == 0)
    {
        SINK_CAST_LOG (cUnit, "Missing ssa representation for if mir", reportSinkCastFailure);
        return false;
    }

    // vA tracks the virtual register defined in castMir and used in ifMir
    int vA = 0;

    // constValue record the constant for one of ifMir's operand.
    int constValue = 0;

    // Check if "if" bytecode has one operand
    if (ifMir->dalvikInsn.opcode >= OP_IF_EQZ && ifMir->dalvikInsn.opcode <= OP_IF_LEZ)
    {
        if (ifInsn.vA == castMir->dalvikInsn.vA)
        {
            vA = ifInsn.vA;
        }

        // did not found the corresponding use in ifMir
        else
        {
            SINK_CAST_LOG (cUnit, "Did not find corresponding use in if mir", reportSinkCastFailure);
            return false;
        }
        constValue = 0;
    }
    else
    {
        // Check if "if" bytecode has two operands
        if (ifMir->dalvikInsn.opcode >= OP_IF_EQ && ifMir->dalvikInsn.opcode <= OP_IF_LE)
        {
            //This should hold the define for the non IV VR of the IF, which should be a const
            MIR * mirConst = 0;

            // vA tracks the virtual register defined in castMir and used in ifMir
            if (ifInsn.vA == castMir->dalvikInsn.vA)
            {
                vA = ifInsn.vA;
                assert(ssaRep->numUses > 1);

                //Check the MIR defining the vB use
                mirConst = ssaRep->defWhere[1];
            }
            else
            {
                if (ifInsn.vB == castMir->dalvikInsn.vA)
                {
                    vA = ifInsn.vB;
                    assert(ssaRep->numUses > 0);

                    //Check the MIR defining the vA use
                    mirConst = ssaRep->defWhere[0];
                }

                // did not found the corresponding use in ifMir
                else
                {
                    SINK_CAST_LOG (cUnit, "Did not find corresponding use in if mir", reportSinkCastFailure);
                    return false;
                }
            }

            //Paranoid
            if (mirConst == 0)
            {
                SINK_CAST_LOG (cUnit, "Could not find const bytecode for loop bound", reportSinkCastFailure);
                return false;
            }

            int highConst = 0;
            bool isWide = false;

            // start to find if the other operand is coming from a const bytecode
            bool isConst = dexGetConstant (mirConst->dalvikInsn, constValue, highConst, isWide);

            // if the mirConst is not a const bytecode or it set a wide constant, return false
            if (isConst == false || isWide == true)
            {
                SINK_CAST_LOG (cUnit, "The loop bound is not constant or is wide", reportSinkCastFailure);
                return false;
            }
        }
    }

    // return false if vA is not an induction variable
    if (info->isBasicInductionVariable (cUnit, vA) == false)
    {
        SINK_CAST_LOG (cUnit, "The VR we are considering must be an induction variable", reportSinkCastFailure);
        return false;
    }

    // Check the constant to see if it's within the range of the cast type
    switch (castMir->dalvikInsn.opcode)
    {
        case OP_INT_TO_BYTE:
            if (constValue > 127 || constValue < -128)
            {
                SINK_CAST_LOG (cUnit, "Loop bound incompatible with the cast to byte", reportSinkCastFailure);
                return false;
            }
            break;
        case OP_INT_TO_SHORT:
            if (constValue > 32767 || constValue < -32768)
            {
                SINK_CAST_LOG (cUnit, "Loop bound incompatible with the cast to short", reportSinkCastFailure);
                return false;
            }
            break;
        default:
            SINK_CAST_LOG (cUnit, "No logic to handle unexpected cast type", reportSinkCastFailure);
            return false;
    }

    //We got here so we are happy
    return true;
}

/**
 * @brief check if the cast byte is supported for sinking
 * @param mir current mir instruction
 * @return whether bytecode type of mir is support for cast sinking
 */
static bool isCastSinkable(MIR *mir)
{
    switch (mir->dalvikInsn.opcode)
    {
        case OP_INT_TO_BYTE:
        case OP_INT_TO_SHORT:
            break;
        default:
            return false;
    }

    //Get instruction
    DecodedInstruction &insn = mir->dalvikInsn;

    //Get dataflow flags
    long long flags = dvmCompilerDataFlowAttributes[insn.opcode];

    //First off, we only sink casts that aren't wide associated
    if ( (flags & DF_DA_WIDE) != 0 || ( (flags & DF_UB_WIDE) != 0))
    {
        return false;
    }

    //Second, we only sink same vr to same vr casts
    if (insn.vA != insn.vB)
    {
        return false;
    }

    return true;
}

/**
 * @brief Serves as a quick check on whether cast sinking can be further evaluated
 * @param opcode The opcode to check which may be an extended one
 * @return Returns true if opcode is an alu operation that will give same result even if intermediate casts are sunk.
 */
static bool isSafeInPresenceOfCasts (int opcode)
{
    bool safe = false;

    switch (opcode)
    {
        //Operations add, mul, sub, rsub, and, or, xor, and shl are safe
        case OP_ADD_INT:
        case OP_ADD_INT_2ADDR:
        case OP_ADD_INT_LIT8:
        case OP_ADD_INT_LIT16:
        case OP_MUL_INT:
        case OP_MUL_INT_2ADDR:
        case OP_MUL_INT_LIT8:
        case OP_MUL_INT_LIT16:
        case OP_SUB_INT:
        case OP_SUB_INT_2ADDR:
        case OP_RSUB_INT_LIT8:
        case OP_RSUB_INT:
        case OP_AND_INT:
        case OP_AND_INT_2ADDR:
        case OP_AND_INT_LIT8:
        case OP_AND_INT_LIT16:
        case OP_OR_INT:
        case OP_OR_INT_2ADDR:
        case OP_OR_INT_LIT8:
        case OP_OR_INT_LIT16:
        case OP_XOR_INT:
        case OP_XOR_INT_2ADDR:
        case OP_XOR_INT_LIT8:
        case OP_XOR_INT_LIT16:
        case OP_SHL_INT:
        case OP_SHL_INT_2ADDR:
        case OP_SHL_INT_LIT8:
        case OP_INT_TO_BYTE:
        case OP_INT_TO_SHORT:
        case OP_INT_TO_CHAR:
        case kMirOpPhi:
            safe = true;
            break;
        default:
            if (dvmCompilerIsOpcodeConditionalBranch (opcode) == true)
            {
                //Technically for the if bytecodes we don't know if they are safe.
                //But when we do our analysis we will check them explicitly.
                safe = true;
            }

            if ((dvmCompilerDataFlowAttributes[opcode] & DF_SETS_CONST) != 0)
            {
                //Constants only define so they don't use the result of our cast
                safe = true;
            }

            //Others most likely aren't safe. This includes integer operations: div, rem, shr, and ushr.
            break;
    }

    return safe;
}

/**
 * @brief Helper to detect whether the MIR is ok to use sink cast optimization.
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param mir the MIR instruction to check
 * @return true if it is safe to use the MIR in sink cast optimization.
 */
static bool isInstructionSafeForCastSinking (const CompilationUnit *cUnit, const LoopInformation *info, const MIR *mir)
{
    //Get opcode
    Opcode opcode = mir->dalvikInsn.opcode;

    //Get dataflow flags
    long long flags = dvmCompilerDataFlowAttributes[opcode];

    //First off, we only sink casts that aren't wide associated
    if ( (flags & DF_DA_WIDE) != 0 || ( (flags & DF_UB_WIDE) != 0))
    {
        return false;
    }

    return isSafeInPresenceOfCasts (opcode);
}

/**
 * @brief Analyze whether our VR candidate is used. Fill vrOkToSinkDependsOn map if we can be sunk only
 * if other VR is sunk, return true if we are not sunk in any conditions.
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param phi the root Phi node in a loop corresponding to our VR
 * @param vr the candidate VR to sink
 * @param vrOkToSinkDependsOn the map is filled by this function and means that our VR can be sunk
 * only if other VRs in this map are sunk
 * @return true ifour candidate will not be sunk in any conditions
 */
static bool fillDependencyFromOthers (CompilationUnit *cUnit, LoopInformation *info, MIR *phi, int vr, std::multimap<int, int> &vrOkToSinkDependsOn)
{
    //what should be considered on this iteration
    std::set<SSARepresentation *> defToCheck;

    //what should be considered on next iteration
    std::set<SSARepresentation *> defToCheckNextIter;

    //what is added already for consideration to avoid redundant checks
    std::set<SSARepresentation *> defAddedForCheck;

    //Start from Phi node
    {
        SSARepresentation *ssaRep = phi->ssaRep;
        if (ssaRep == 0)
        {
            //Something went wrong
            SINK_CAST_LOG (cUnit, "Found bad ssa for phi while filling dependencies", reportSinkCastFailure);
            return true;
        }

        defToCheck.insert (ssaRep);
        defAddedForCheck.insert (ssaRep);
    }

    while (true)
    {
        for (std::set<SSARepresentation *>::iterator it = defToCheck.begin (); it != defToCheck.end (); ++it)
        {
            SSARepresentation *ssaRep = *it;

            if (ssaRep->numDefs != 1 || ssaRep->defs == 0)
            {
                //Something went wrong
                SINK_CAST_LOG (cUnit, "Found bad ssa while filling dependencies", reportSinkCastFailure);
                return true;
            }

            int def = ssaRep->defs[0];

            //If def leaves a loop, we can sink only if vr corresponding to this def is sunk
            //Still need to check other uses because it can be used in some operation wich is not allowed
            if (info->isSSARegLeavesLoop (cUnit, def) == true)
            {
                int otherVR = dvmExtractSSARegister (cUnit, def);

                //If it is we are, so ignore this dependancy
                if (vr != otherVR)
                {
                    vrOkToSinkDependsOn.insert (std::pair<int, int> (vr, otherVR));
                }
            }

            //Traverse all its uses
            for (SUsedChain *usedChain = ssaRep->usedNext[0]; usedChain != 0; usedChain = usedChain->nextUse)
            {
                //Get the MIR
                MIR *next = usedChain->mir;

                //No need to look outside a loop
                if (info->contains (next->bb) == false)
                {
                    continue;
                }

                //If there is a usage in instruction we do not support => no chances to sink
                if (isInstructionSafeForCastSinking (cUnit, info, next) == false)
                {
                    SINK_CAST_LOG (cUnit, "While filling dependencies found instruction affected by sinking",
                            reportSinkCastFailure);
                    return true;
                }

                //If it is a comparison so we can sink only if that VR is sunk also
                bool isOpcodeConditionalBranch = dvmCompilerIsOpcodeConditionalBranch (next->dalvikInsn.opcode);
                if (isOpcodeConditionalBranch == true)
                {
                    int otherVR = dvmExtractSSARegister (cUnit, def);

                    if (vr != otherVR)
                    {
                        vrOkToSinkDependsOn.insert (std::pair<int, int> (vr, otherVR));
                    }
                }
                else
                {
                    //We are defining some VR inside a loop, so we should check it then
                    if (next->ssaRep == 0)
                    {
                        //Something went wrong
                        SINK_CAST_LOG (cUnit, "Found missing ssa while filling dependencies", reportSinkCastFailure);
                        return true;
                    }
                    if (defAddedForCheck.find (next->ssaRep) == defAddedForCheck.end ())
                    {
                        defToCheckNextIter.insert (next->ssaRep);
                        defAddedForCheck.insert (next->ssaRep);
                    }
                }
            }
        }

        //Wether new work came
        if (defToCheckNextIter.empty () == true)
        {
            break;
        }

        defToCheck = defToCheckNextIter;
        defToCheckNextIter.clear ();
    }

    //Dependancy is filled
    return false;
}

/**
 * @brief Check whether candidate has chances to be sunk
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param mir the MIR instruction corresponding to cast candidate
 * @param isPeelingRequired the variable is set to true by this function in case peeling will be required.
 * @param okToSink cast MIR will be added to this map if it is ok to sink this cast
 * @param potentialOkToSink cast MIR will be added to this map if it is potentially can be sunk
 * @param vrOkToSinkDependsOn the map represents dependancy - VR can be sunk only if corresponding VR are sunk
 */
static void considerCastsForSinking (CompilationUnit *cUnit, LoopInformation *info, MIR *mir, bool &isPeelingRequired,
        std::set<MIR*> &okToSink, std::set<MIR*> &potentialOkToSink, std::multimap<int, int> &vrOkToSinkDependsOn)
{
    // if current cast mir is not supported for cast sinking optimization
    if (isCastSinkable(mir) == false)
    {
        SINK_CAST_LOG (cUnit, "Unsupported cast for sinking", reportSinkCastFailure);
        return;
    }

    //Ok, now is it the last use of this VR in the loop?
    SSARepresentation *ssaRep = mir->ssaRep;

    if (ssaRep == 0)
    {
        SINK_CAST_LOG (cUnit, "Missing ssa representation for considered cast", reportSinkCastFailure);
        return;
    }

    //Klocwork checks
    if (ssaRep->numDefs != 1 || ssaRep->defs == 0 || ssaRep->usedNext == 0)
    {
        SINK_CAST_LOG (cUnit, "Bad ssa representation for considered cast", reportSinkCastFailure);
        return;
    }

    //Get the VR we are working on
    int vr = dvmExtractSSARegister (cUnit, ssaRep->defs[0]);

    //Get def-use chain for the single def, wide-defs are excluded in isCastSinkable
    const SUsedChain *usedChain = ssaRep->usedNext[0];

    //If the casted VR is used, we have work to do
    if (usedChain != 0)
    {
        //Walk the def-use chain
        for (; usedChain != 0; usedChain = usedChain->nextUse)
        {
            //Get the MIR
            MIR *next = usedChain->mir;

            //if the use inside our loop we need more checks
            if (info->contains (next->bb) == true)
            {
                bool isOpcodeConditionalBranch = dvmCompilerIsOpcodeConditionalBranch (next->dalvikInsn.opcode);
                //It must the PHI node or an IF
                if (next->dalvikInsn.opcode != static_cast<Opcode> (kMirOpPhi) && (isOpcodeConditionalBranch == false))
                {
                    vrOkToSinkDependsOn.erase (vr);

                    SINK_CAST_LOG (cUnit, "The cast is skipped because use is not if or phi", reportSinkCastFailure);
                    return;
                }

                //If next use mir within current loop is a if
                if (isOpcodeConditionalBranch == true)
                {
                   //Check if the nextUse allows a valid cast sinking
                   if (checkValidCastingForIV(cUnit, info, next, mir) == false)
                   {
                       SINK_CAST_LOG (cUnit, "The cast is skipped because it was determined it was not valid IV cast",
                               reportSinkCastFailure);
                       vrOkToSinkDependsOn.erase (vr);
                       return;
                   }

                   //It is safe but we need a peeling
                   isPeelingRequired = true;
                }

                //If it is a Phi node we should ensure that it is a main Phi node of the loop
                if (next->dalvikInsn.opcode == static_cast<Opcode> (kMirOpPhi))
                {
                    if (next != info->getPhiInstruction (cUnit, vr))
                    {
                        SINK_CAST_LOG (cUnit, "The cast is skipped because its phi node use is not the main one for loop",
                                reportSinkCastFailure);
                        vrOkToSinkDependsOn.erase (vr);
                        return;
                    }

                    //We're returning to entry of the loop, check impact on other VRs
                    //fillDependencyFromOthers will add elements in vrOkToSinkDependsOn
                    //if there are any dependancy and return false if no chances to sink
                    if (fillDependencyFromOthers (cUnit, info, next, vr, vrOkToSinkDependsOn) == true)
                    {
                        SINK_CAST_LOG (cUnit, "Dependency analysis deemed cast not safe for sinking",
                                reportSinkCastFailure);
                        vrOkToSinkDependsOn.erase (vr);
                        return;
                    }
                }
            }
        }
    }
    else
    {
        //Something went wrong: we have a cast in a loop, so at least we should have a Phi node
        //But we do not! It means that there is some bug in a Compiler
        //Report it and skip this cast for safety
        char buffer[256];
        dvmCompilerExtendedDisassembler (cUnit, mir, & (mir->dalvikInsn), buffer, sizeof (buffer));
        ALOGE ("JIT ERROR: no expected Phi node for cast: %04x %s", mir->offset, buffer);
        return;
    }

    //ok to sink only if there are no dependancy
    if (vrOkToSinkDependsOn.count (vr) == 0)
    {
        okToSink.insert (mir);
    }
    else
    {
        potentialOkToSink.insert (mir);
    }
}

/**
 * @brief Tries to sink casts to the loop exit
 * @param cUnit The compilation unit
 * @param info The loop to try for cast sinking
 * @param data pass data
 * @return Always returns true to signify that cast sinking was tried for all loops (even if it doesn't actually do it)
 */
static bool tryCastSinking (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    if (dvmCompilerVerySimpleLoopGateWithLoopInfo (cUnit, info) == false)
    {
        SINK_CAST_LOG (cUnit, "The loop we have analyzed is not very simple", reportSinkCastFailure);

        //We only return so we continue looking through the rest of the loops
        return true;
    }

    bool isPeelingRequired = false;

    //Check whether loop cannot throw any exception or peeling will help us
    if (info->canThrow (cUnit))
    {
        isPeelingRequired  = info->guaranteedToThrowFirstIteration (cUnit);
        if (isPeelingRequired == false)
        {
            SINK_CAST_LOG (cUnit, "Loop may throw", reportSinkCastFailure);

            //We can throw, so reject optimization for this loop but consider others
            return true;
        }
    }

    //Casts which are ok to sink
    std::set<MIR*> okToSink;
    std::set<MIR*> potentialOkToSink;
    //vr is ok to sink if all dependant VRs are sunk
    std::multimap<int, int> vrOkToSinkDependsOn;

    //Get the BasicBlock vector for this loop
    BitVector *blocks = info->getBasicBlocks ();

    //Iterate through them
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (blocks, &bvIterator);
    for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList); bb != 0;
                     bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList))
    {
        //Iterate over instructions to find candidates
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Get the opcode
            Opcode opcode = mir->dalvikInsn.opcode;

            //Check if it is a cast
            if (opcode >= OP_INT_TO_LONG && opcode <= OP_INT_TO_SHORT)
            {
                considerCastsForSinking (cUnit, info, mir, isPeelingRequired, okToSink, potentialOkToSink, vrOkToSinkDependsOn);
            }
        }
    }

    //Peel Loop if needed
    if (okToSink.empty () == false && isPeelingRequired == true)
    {
        bool result = dvmCompilerPeel (cUnit, info);
        if (result == false)
        {
            SINK_CAST_LOG (cUnit, "We needed to peel but failed to do so", reportSinkCastFailure);
            return true;
        }
    }

    //ok we are ready to sink
    while (okToSink.empty () == false)
    {
        for (std::set<MIR*>::iterator it = okToSink.begin (); it != okToSink.end (); ++it)
        {
            MIR *mir = *it;

            //Ok we definitely can sink this MIR
            //First step, remove it
            bool removed = dvmCompilerRemoveMIR (mir);

            if (removed == true)
            {
                //Now add it to exits
                SINK_CAST_LOG (cUnit, mir->dalvikInsn.opcode, reportSunkCast);
                info->addInstructionToExits (cUnit, mir);

                //Now we need to clean dependancy data
                if (mir->ssaRep != 0 && mir->ssaRep->numDefs == 1 && mir->ssaRep->defs != 0)
                {
                    int vr = dvmExtractSSARegister (cUnit, mir->ssaRep->defs[0]);

                    for (std::multimap<int,int>::iterator it = vrOkToSinkDependsOn.begin (); it != vrOkToSinkDependsOn.end ();)
                    {
                        if ((*it).second == vr)
                        {
                            vrOkToSinkDependsOn.erase (it++);
                        }
                        else
                        {
                            it++;
                        }
                    }
                }
            }
            else
            {
                SINK_CAST_LOG (cUnit, "Failed to remove cast from its block", reportSinkCastFailure);
            }
        }

        okToSink.clear ();
        //Try to find whether we can sink more
        for (std::set<MIR*>::iterator it = potentialOkToSink.begin (); it != potentialOkToSink.end (); )
        {
            bool okNow = false;
            MIR *mir = *it;

            if (mir->ssaRep != 0 && mir->ssaRep->numDefs == 1 && mir->ssaRep->defs != 0)
            {
                int vr = dvmExtractSSARegister (cUnit, mir->ssaRep->defs[0]);
                if (vrOkToSinkDependsOn.count (vr) == 0)
                {
                    okToSink.insert (mir);
                    okNow = true;
                }
            }

            if (okNow == true)
            {
                potentialOkToSink.erase (it++);
            }
            else
            {
                it++;
            }
        }
    }

    if (potentialOkToSink.empty () == false)
    {
        SINK_CAST_LOG (cUnit, "Casts potentially considered for sinking were not all sunk", reportSinkCastFailure);
    }

    //We have successfully attempted to sink casts
    return true;
}

/**
 * Sink Loop Casts
 */
void dvmCompilerSinkCasts (CompilationUnit *cUnit, Pass *pass)
{
    //Find the loop information
    LoopInformation *info = cUnit->loopInformation;

    //The gate should have checked this, let's be paranoid
    assert (info != 0);

    //Now go through the loops and try sinking the casts
    info->iterate (cUnit, tryCastSinking, pass->getData ());
}
