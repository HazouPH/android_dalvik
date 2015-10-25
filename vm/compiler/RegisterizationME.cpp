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

#include "Dalvik.h"
#include "Dataflow.h"
#include "CompilerIR.h"
#include "LoopInformation.h"
#include "PassDriver.h"
#include "RegisterizationME.h"

#include <algorithm>

/**
 * @brief Get the register class for a given define from a PHI node
 * @param mir the PHI node MIR
 * @param vR the virtual register number
 * @param regClass the RegisterClass (updated by the function)
 * @return whether the function found the register class
 */
static bool getType (const MIR *mir, int vR, RegisterClass &regClass)
{
    //Paranoid
    if (mir == 0)
    {
        return false;
    }

    //Get SSA representation
    SSARepresentation *ssaRep = mir->ssaRep;

    //Paranoid
    if (ssaRep == 0 || ssaRep->defs == 0)
    {
        return false;
    }

    SUsedChain **chains = ssaRep->usedNext;

    //If chain is emtpy, we cannot do anything
    if (chains == 0)
    {
        return false;
    }

    //For a PHI node, it will be in chains[0]
    SUsedChain *chain = chains[0];

    //Paranoid
    if (chain == 0)
    {
        return false;
    }

    MIR *firstUse = chain->mir;

    //Paranoid
    if (firstUse == 0)
    {
        return false;
    }

    //If the first use is extended, reject it
    if (firstUse->dalvikInsn.opcode >= static_cast<Opcode> (kMirOpFirst))
    {
        return false;
    }

    //Ok now find out about this one
    bool res = dvmCompilerFindRegClass (firstUse, vR, regClass, true);

    if (res == false)
    {
        return false;
    }

    //Currently we ignore kX87Reg registers
    if (regClass == kX87Reg)
    {
        return false;
    }

    //Success
    return true;
}

/**
 * @brief Select the registers we want to registerize: currently only the entry PHI nodes
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param registers the final vector of registers
 */
static void selectRegisters (CompilationUnit *cUnit, const LoopInformation *info, std::vector<std::pair<int, RegisterClass> > &registers)
{
    //Clear the vector just in case
    registers.clear ();

    //As a first iteration of the algorithm, we are only going to registerize interloop dependent variables
    //These variables are automatically PHI nodes in the entry block
    BasicBlock *entry = info->getEntryBlock ();

    for (MIR *mir = entry->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get dalvik instruction
        DecodedInstruction &dalvikInsn = mir->dalvikInsn;

        //Check opcode: is it a Phi node?
        if (dalvikInsn.opcode == static_cast<Opcode> (kMirOpPhi))
        {
            //Paranoid
            assert (mir->ssaRep != 0);
            assert (mir->ssaRep->numDefs > 0);

            //It is, ok let's get the left side for it: it is in defs[0]
            int ssaName = mir->ssaRep->defs[0];

            //Get the first type used for this register
            RegisterClass type = kAnyReg;

            int reg = dvmConvertSSARegToDalvik (cUnit, ssaName);
            reg = DECODE_REG (reg);
            bool res = getType (mir, reg, type);

            //If success, we can add it
            if (res == true)
            {
                registers.push_back (std::make_pair<int, RegisterClass> (ssaName, type));
            }
        }
    }
}

/**
 * @brief Fill the write back requests using the destination's phi nodes
 * @param bb the BasicBlock that will be walked to get the PHI nodes
 * @param bv the BitVector to fill
 */
static void fillWriteBackRequests (BasicBlock *bb, BitVector *bv)
{
    //If null, we have nothing to do
    if (bb == 0)
    {
        return;
    }

    //Go through each instruction
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get the instruction
        DecodedInstruction &insn = mir->dalvikInsn;

        //Get the opcode
        Opcode opcode = insn.opcode;

        //Check for a PHI node
        if (opcode == static_cast<Opcode> (kMirOpPhi))
        {
            //Get the va, it contains the register in question
            int dalvikReg = insn.vA;

            //Now add it
            dvmSetBit (bv, dalvikReg);
        }
    }
}

/**
 * @brief Fill the write back requests of the post loop basic blocks using their live outs
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 */
static void handleWriteBackRequestsPostLoop (const CompilationUnit *cUnit, LoopInformation *info)
{
    const BitVector *postBasicBlocks = info->getExitLoops ();

    //Now take care of write back requests
    BitVectorIterator bvIterator;

    //Const cast because of incompatibility here
    BitVector *tmp = const_cast<BitVector *> (postBasicBlocks);
    dvmBitVectorIteratorInit (tmp, &bvIterator);

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

        //Paranoid
        if (bb == 0)
        {
            continue;
        }

        //For the moment, we are being simple, exiting the loop, we request write backs of
        //every register in the method. Note that we do not request writebacks of
        //temporaries and thus we do not want to use cUnit->numDalvikRegisters
        unsigned int size = cUnit->numDalvikRegisters;
        dvmSetInitialBits (bb->requestWriteBack, size);
    }
}

/**
 * @brief Handle write backs requests for the PreHeader, we want to not write back the registerize requests
 * @param preHeader the preHeader block
 */
static void handlePreHeaderWriteBackRequests (BasicBlock *preHeader)
{
    //Get a local version of the requests
    BitVector *requests = preHeader->requestWriteBack;

    //Go through the instructions
    for (MIR *mir = preHeader->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get local version of the instruction
        const DecodedInstruction &insn = mir->dalvikInsn;

        //Get the opcode
        int opcode = insn.opcode;

        //If it's a registerize
        if (opcode == kMirOpRegisterize)
        {
            //Get the register
            int reg = insn.vA;

            //Clear it's bit from the write back requests
            dvmClearBit (requests, reg);
        }
    }
}

/**
 * @brief Handle write backs requests for a given BitVector representing blocks
 * @param cUnit the CompilationUnit
 * @param blocks a BitVector representing which BasicBlocks to handle
 */
static void handleWriteBackRequests (const CompilationUnit *cUnit, const BitVector *blocks)
{
    //Paranoid
    if (blocks == 0)
    {
        return;
    }

    //Now take care of write backs requests
    BitVectorIterator bvIterator;

    //Const cast due to incompatibility here
    BitVector *tmp = const_cast<BitVector *> (blocks);
    dvmBitVectorIteratorInit (tmp, &bvIterator);
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

        //Paranoid
        if (bb == 0)
        {
            continue;
        }

        //Get any merge issues until the BE can handle them by itself
        //Get the write backs BitVector
        BitVector *writeBack = bb->requestWriteBack;

        //First clear the vector
        dvmClearAllBits (writeBack);

        //Now get a union of any Phi nodes we need to handle here
        fillWriteBackRequests (bb->taken, writeBack);
        fillWriteBackRequests (bb->fallThrough, writeBack);
    }
}

/**
 * @brief Count the register usage for each SSA register in the BasicBlock
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return false, the function does not change the BasicBlock
 */
static bool countRegistersHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Get the register count data
    std::map<int, int> *registerCounts = static_cast<std::map<int, int> *> (cUnit->walkData);

    //If there is no data, bail
    if (registerCounts == 0)
    {
        return false;
    }

    //Now go through the BasicBlock
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //If there is the SSA information
        SSARepresentation *ssa = mir->ssaRep;

        if (ssa != 0)
        {
            //Go through the uses and count
            for (int i = 0; i < ssa->numUses; i++)
            {
                int use = ssa->uses[i];

                (*registerCounts)[use]++;
            }
        }
    }

    return false;
}

/**
 * @brief Count the register usage
 * @param cUnit the CompilationUnit
 * @param registerCounts the map to count register usage
 */
static void countRegisters (CompilationUnit *cUnit, std::map<int, int> &registerCounts)
{
    //Set walking data
    void *walkData = static_cast<void *> (&registerCounts);

    //Dispatch the counting
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, countRegistersHelper, kAllNodes, false, walkData);
}

/**
 * @class RegisterSorter
 * @brief Used to sort the registerize order
 */
class RegisterSorter
{
    public:
        /** @brief Register counts to determine the order to registerize */
        std::map<int, int> registerCounts;

        /**
         * @brief Operator used for the sorting of first < second, it uses the information in registerCount to sort in reverse order
         * @param first the first register to compare
         * @param second the second register to compare
         * @return whether first has a higher register count than second
         */
        bool operator() (const std::pair<int, RegisterClass> &first, const std::pair<int, RegisterClass> &second)
        {
            int firstCount = registerCounts[first.second];
            int secondCount = registerCounts[second.second];

            //We only care about the first
            return (firstCount > secondCount);
        }

        /**
         * @brief Get the register count map
         * @return the registerCount map
         */
        std::map<int, int> &getRegisterCounts (void)
        {
            return registerCounts;
        }

        /**
         * @brief Destructor to clear the register count map
         */
        ~RegisterSorter (void)
        {
            registerCounts.clear ();
        }
};

/**
 * @brief Parse a BasicBlock of the loop
 * @param bb the BasicBlock of the loop
 * @param verbose are we in verbose mode or not
 * @return whether the BasicBlock contains any opcodes we don't want to support for registerization
 */
static bool parseBlock (BasicBlock *bb, bool verbose)
{
    //We want to disable registerization when we have control flow in inner loop
    if (bb->taken != 0 && bb->fallThrough != 0)
    {
        //We have two branches so check if we loop back
        if (bb->taken->blockType != kPreBackwardBlock && bb->taken->blockType != kChainingCellBackwardBranch
                && bb->fallThrough->blockType != kPreBackwardBlock
                && bb->fallThrough->blockType != kChainingCellBackwardBranch)
        {
            //We are not looping back so disable registerization
            return false;
        }
    }

    //Go through the instructions
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        int opcode = mir->dalvikInsn.opcode;

        switch (opcode)
        {
            case OP_NOP:
            case OP_MOVE_FROM16:
            case OP_MOVE_16:
            case OP_MOVE_WIDE:
            case OP_MOVE_WIDE_FROM16:
            case OP_MOVE_WIDE_16:
            case OP_MOVE_OBJECT:
            case OP_MOVE_OBJECT_16:

            //Not OP_MOVE_RESULT to OP_RETURN_OBJECT
            case OP_CONST_4:
            case OP_CONST_16:
            case OP_CONST:
            case OP_CONST_HIGH16:
            case OP_CONST_WIDE_16:
            case OP_CONST_WIDE_32:
            case OP_CONST_WIDE:
            case OP_CONST_WIDE_HIGH16:
            case OP_CONST_STRING:
            case OP_CONST_STRING_JUMBO:
            case OP_CONST_CLASS:

            //Not monitor/check/instance of, array or instance/trow

            case OP_GOTO:
            case OP_GOTO_16:
            case OP_GOTO_32:

            //Not switch

            case OP_CMPL_FLOAT:
            case OP_CMPG_FLOAT:
            case OP_CMPL_DOUBLE:
            case OP_CMPG_DOUBLE:
            case OP_CMP_LONG:

            case OP_IF_EQ:
            case OP_IF_NE:
            case OP_IF_LT:
            case OP_IF_GE:
            case OP_IF_GT:
            case OP_IF_LE:
            case OP_IF_EQZ:
            case OP_IF_NEZ:
            case OP_IF_LTZ:
            case OP_IF_GEZ:
            case OP_IF_GTZ:
            case OP_IF_LEZ:

            //Not the unused

            //Not agets/aputs/iget/iputs/sgets/sputs
            case OP_AGET:
            case OP_AGET_WIDE:
            case OP_AGET_OBJECT:
            case OP_AGET_BYTE:
            case OP_AGET_CHAR:
            case OP_AGET_SHORT:
            case OP_APUT:
            case OP_APUT_WIDE:
            case OP_APUT_OBJECT:
            case OP_APUT_BYTE:
            case OP_APUT_CHAR:
            case OP_APUT_SHORT:

            //Not the invokes

            //Not the unused

            //Not the invokes

            //Not the unused

            case OP_NEG_INT:
            case OP_NOT_INT:
            case OP_NEG_LONG:
            case OP_NOT_LONG:
            case OP_NEG_FLOAT:
            case OP_NEG_DOUBLE:

            case OP_INT_TO_DOUBLE:
            case OP_INT_TO_LONG:
            case OP_INT_TO_FLOAT:
            case OP_LONG_TO_INT:
            case OP_LONG_TO_FLOAT:
            case OP_LONG_TO_DOUBLE:
            case OP_FLOAT_TO_INT:
            case OP_FLOAT_TO_LONG:
            case OP_FLOAT_TO_DOUBLE:
            case OP_DOUBLE_TO_INT:
            case OP_DOUBLE_TO_LONG:
            case OP_DOUBLE_TO_FLOAT:
            case OP_INT_TO_BYTE:
            case OP_INT_TO_CHAR:
            case OP_INT_TO_SHORT:
            //Only a subset of alu
            case OP_ADD_INT:
            case OP_SUB_INT:
            case OP_MUL_INT:
            case OP_DIV_INT:
            case OP_REM_INT:
            case OP_AND_INT:
            case OP_OR_INT:
            case OP_XOR_INT:
            case OP_SHL_INT:
            case OP_SHR_INT:
            case OP_USHR_INT:
            case OP_ADD_LONG:
            case OP_SUB_LONG:
            case OP_MUL_LONG:
            case OP_DIV_LONG:
            case OP_REM_LONG:
            case OP_AND_LONG:
            case OP_OR_LONG:
            case OP_XOR_LONG:
            case OP_SHL_LONG:
            case OP_SHR_LONG:
            case OP_USHR_LONG:

            case OP_ADD_FLOAT:
            case OP_SUB_FLOAT:
            case OP_MUL_FLOAT:
            case OP_DIV_FLOAT:
            case OP_REM_FLOAT:

            case OP_ADD_DOUBLE:
            case OP_SUB_DOUBLE:
            case OP_MUL_DOUBLE:
            case OP_DIV_DOUBLE:
            case OP_REM_DOUBLE:

            case OP_ADD_INT_2ADDR:
            case OP_SUB_INT_2ADDR:
            case OP_MUL_INT_2ADDR:
            case OP_DIV_INT_2ADDR:
            case OP_REM_INT_2ADDR:
            case OP_AND_INT_2ADDR:
            case OP_OR_INT_2ADDR:
            case OP_XOR_INT_2ADDR:
            case OP_SHL_INT_2ADDR:
            case OP_SHR_INT_2ADDR:
            case OP_USHR_INT_2ADDR:
            case OP_ADD_LONG_2ADDR:
            case OP_SUB_LONG_2ADDR:
            case OP_MUL_LONG_2ADDR:
            case OP_DIV_LONG_2ADDR:
            case OP_REM_LONG_2ADDR:
            case OP_AND_LONG_2ADDR:
            case OP_OR_LONG_2ADDR:
            case OP_XOR_LONG_2ADDR:
            case OP_SHL_LONG_2ADDR:
            case OP_SHR_LONG_2ADDR:
            case OP_USHR_LONG_2ADDR:

            case OP_ADD_FLOAT_2ADDR:
            case OP_SUB_FLOAT_2ADDR:
            case OP_MUL_FLOAT_2ADDR:
            case OP_DIV_FLOAT_2ADDR:
            case OP_REM_FLOAT_2ADDR:

            case OP_ADD_DOUBLE_2ADDR:
            case OP_SUB_DOUBLE_2ADDR:
            case OP_MUL_DOUBLE_2ADDR:
            case OP_DIV_DOUBLE_2ADDR:
            case OP_REM_DOUBLE_2ADDR:

            //Not the lit 16 lit 8
            case OP_ADD_INT_LIT16:
            case OP_ADD_INT_LIT8:
            //Not the volatile

            //Not the breakpoint/throw/execute inline

            //Not the invokes

            //Not the return barrier

            //Not the quick

            //Only a few of the extended
            case kMirOpPhi:
            case kMirOpConst128b:
            case kMirOpMove128b:
            case kMirOpPackedMultiply:
            case kMirOpPackedAddition:
            case kMirOpPackedAddReduce:
            case kMirOpPackedReduce:
            case kMirOpPackedSet:
            case kMirOpPackedSubtract:
            case kMirOpPackedXor:
            case kMirOpPackedOr:
            case kMirOpPackedAnd:
            case kMirOpPackedShiftLeft:
            case kMirOpPackedSignedShiftRight:
            case kMirOpPackedUnsignedShiftRight:

                break;

            default:
                //Not accepted, refuse the block
                if (verbose == true)
                {
                    ALOGD ("Rejecting registerization due to %s", dvmCompilerGetDalvikDisassembly (& (mir->dalvikInsn), NULL));
                }
                return false;
        }
    }

    //Got to here, we can accept
    return true;
}

/**
 * @brief Check a loop: is it ok to registerize
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @return whether it is acceptable for registerization
 */
static bool checkLoop (CompilationUnit *cUnit, LoopInformation *info)
{
    // Consider only innermost loops
    if (info->getNested () != 0)
    {
        return false;
    }

    const BitVector *blocks = info->getBasicBlocks ();

    //Go through each block
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit( (BitVector *) blocks, &bvIterator);
    while (true) {
        int blockIdx = dvmBitVectorIteratorNext(&bvIterator);

        //If done, bail
        if (blockIdx == -1)
        {
            break;
        }

        //Get BasicBlock
        BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, blockIdx);

        //Paranoid
        if (bb == 0)
        {
            break;
        }

        //If we don't accept this one, refuse everything
        if (parseBlock (bb, cUnit->printMe) == false)
        {
            return false;
        }
    }

    //Got here so we are good
    return true;
}

/**
 * @brief Registerize a given loop
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param data required by interface (not used)
 * @return true to continue iteration over loops
 */
static bool registerizeLoop (CompilationUnit *cUnit, LoopInformation *info, void *data = 0)
{
    // Now we check the loop. If it returns false then we don't continue with trying to registerize
    if (checkLoop (cUnit, info) == false)
    {
        return true;
    }

    //For now refuse to registerize inner loops that have branches
    if (dvmCountSetBits (info->getExitLoops ()) > 1 || dvmCountSetBits (info->getBackwardBranches ()) > 1)
    {
        return true;
    }

    RegisterSorter sorter;
    std::vector<std::pair<int, RegisterClass> > registers;
    std::map<int, int> &registerCounts = sorter.getRegisterCounts ();

    BasicBlock *preHeader = info->getPreHeader ();

    //Paranoid
    assert (preHeader != 0);

    //Select which registers should get registerized
    selectRegisters (cUnit, info, registers);

    //Set maximum registerization
    cUnit->maximumRegisterization = registers.size ();

    //Now count the uses of each register, do it for all, it's simpler than trying to do it only for the ones we care
    countRegisters (cUnit, registerCounts);

    //Finally, filter out and sort the registers in priority order
    std::sort (registers.begin (), registers.end (), sorter);

    //Now go through these registers and add the instructions
    for (std::vector<std::pair<int, RegisterClass> >::const_iterator it = registers.begin (); it != registers.end (); it++)
    {
        //Get the values
        const std::pair<int, RegisterClass> &p = *it;

        int reg = p.first;
        // Get the Dalvik number
        reg = dvmConvertSSARegToDalvik(cUnit, reg);

        RegisterClass regClass = p.second;

        //Create a registerize request in the preheader
        //Actually generate the hoisting code
        MIR *registerizeInsn = static_cast<MIR *> (dvmCompilerNew (sizeof (*registerizeInsn), true));
        registerizeInsn->dalvikInsn.opcode = static_cast<Opcode> (kMirOpRegisterize);
        //We only care about the register number
        registerizeInsn->dalvikInsn.vA = DECODE_REG (reg);
        registerizeInsn->dalvikInsn.vB = regClass;
        registerizeInsn->dalvikInsn.vC = 0;

        dvmCompilerPrependMIR (preHeader, registerizeInsn);
    }

    //Handle the BasicBlocks of the loop
    const BitVector *basicBlocks = info->getBasicBlocks ();

    //Paranoid
    assert (basicBlocks != 0);

    //Call the helper function to set the writebacks for each BasicBlock
    handleWriteBackRequests (cUnit, basicBlocks);

    //Paranoid
    assert (preHeader->requestWriteBack != 0);

    //Clear the writebacks for the loop preheader
    handlePreHeaderWriteBackRequests (preHeader);

    //Handle the backward chaining cells of the loop
    const BitVector *backwards = info->getBackwardBranches ();

    //Paranoid
    assert (backwards != 0);

    //Call the helper function to set the writebacks for the backward chaining cells
    handleWriteBackRequests (cUnit, backwards);

    //Last handle the write backs of all live outs for the post loops
    handleWriteBackRequestsPostLoop (cUnit, info);

    return true;
}

bool dvmCompilerWriteBackAll (CompilationUnit *cUnit, BasicBlock *bb)
{
    //First job is going through the BasicBlocks and requesting to write back any defs
    dvmClearAllBits (bb->requestWriteBack);

    if (bb->dataFlowInfo != 0 && bb->dataFlowInfo->defV != 0 && bb->dataFlowInfo->useV != 0)
    {
        dvmUnifyBitVectors (bb->requestWriteBack, bb->requestWriteBack, bb->dataFlowInfo->defV);
        // We also add the uses because it is possible to enter loop preheader with
        // physical register association but when going through interpreter, we may
        // clobber those register associations.
        dvmUnifyBitVectors (bb->requestWriteBack, bb->requestWriteBack, bb->dataFlowInfo->useV);
    }

    //We don't want to iterate, do this once
    return false;
}

void dvmCompilerRegisterize (CompilationUnit *cUnit, Pass *currentPass)
{
    //Now let's go through the loop information
    LoopInformation *info = cUnit->loopInformation;

    //Now registerize it
    if (info != 0)
    {
        info->iterate (cUnit, registerizeLoop);
    }

    //Unused argument
    (void) currentPass;
}
