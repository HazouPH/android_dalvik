/*
 * Copyright (C) 2010-2013 Intel Corporation
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

#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "Dalvik.h"
#include "InstructionGeneration.h"
#include "libdex/DexOpcodes.h"
#include "compiler/Compiler.h"
#include "compiler/CompilerIR.h"
#include "interp/Jit.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "NcgAot.h"
#include "compiler/codegen/CompilerCodegen.h"

#define P_GPR_1 PhysicalReg_EBX
#define P_GPR_2 PhysicalReg_ECX

/**
 * @brief Generate a Null check
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedNullCheck (CompilationUnit *cUnit, MIR *mir)
{
    //Check if register allocator is turned on
    if (gDvm.executionMode == kExecutionModeNcgO1)
    {
        //Now we do a null check if it is needed
        if ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
        {
            //Now create some locals to make it easier to read
            const int temp1 = 1;
            const int vrThisPtr = mir->dalvikInsn.vA;

            //Put the object register in temp1
            get_virtual_reg (vrThisPtr, OpndSize_32, temp1, false);

            //Do a null check on temp1
            nullCheck (temp1, false, 1, vrThisPtr);
        }
    }
    else
    {
        get_virtual_reg(mir->dalvikInsn.vA, OpndSize_32, P_GPR_1, true);
        export_pc();
        compare_imm_reg(OpndSize_32, 0, P_GPR_1, true);
        condJumpToBasicBlock (Condition_E, cUnit->exceptionBlockId);
    }
}

/**
 * @brief Generate a Bound check:
      vA arrayReg
      arg[0] -> determines whether it is a constant or a register
      arg[1] -> register or constant

      is idx < 0 || idx >= array.length ?
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedBoundCheck (CompilationUnit *cUnit, MIR *mir)
{
    /* Assign array in virtual register to P_GPR_1 */
    get_virtual_reg(mir->dalvikInsn.vA, OpndSize_32, P_GPR_1, true);

    if (mir->dalvikInsn.arg[0] == MIR_BOUND_CHECK_REG)
    {
        //Ok let us get the index in P_GPR_2
        get_virtual_reg(mir->dalvikInsn.arg[1], OpndSize_32, P_GPR_2, true);
    }
    else
    {
        //Move the constant to P_GPR_2
        move_imm_to_reg(OpndSize_32, mir->dalvikInsn.arg[1], P_GPR_2, true);
    }
    export_pc();

    //Compare array length with index value
    compare_mem_reg(OpndSize_32, OFFSETOF_MEMBER(ArrayObject, length), P_GPR_1, true, P_GPR_2, true);
    //Jump to exception block if array.length <= index
    condJumpToBasicBlock (Condition_LE, cUnit->exceptionBlockId);

    //Now, compare to 0
    compare_imm_reg(OpndSize_32, 0, P_GPR_2, true);
    //Jump to exception if index < 0
    condJumpToBasicBlock (Condition_L, cUnit->exceptionBlockId);
}

//use O0 code generator for hoisted checks outside of the loop
/*
 * vA = arrayReg;
 * vB = idxReg;
 * vC = endConditionReg;
 * arg[0] = maxC
 * arg[1] = minC
 * arg[2] = loopBranchConditionCode
 */
void genHoistedChecksForCountUpLoop(CompilationUnit *cUnit, MIR *mir)
{
    /*
     * NOTE: these synthesized blocks don't have ssa names assigned
     * for Dalvik registers.  However, because they dominate the following
     * blocks we can simply use the Dalvik name w/ subscript 0 as the
     * ssa name.
     */
    DecodedInstruction *dInsn = &mir->dalvikInsn;
    const int maxC = dInsn->arg[0];

    //First do the null check
    genHoistedNullCheck (cUnit, mir);

    /* assign index in virtual register to P_GPR_2 */
    get_virtual_reg(mir->dalvikInsn.vC, OpndSize_32, P_GPR_2, true);

    int delta = maxC;
    /*
     * If the loop end condition is ">=" instead of ">", then the largest value
     * of the index is "endCondition - 1".
     */
    if (dInsn->arg[2] == OP_IF_GE) {
        delta--;
    }

    if (delta < 0) { //+delta
        //if P_GPR_2 is mapped to a VR, we can't do this
        alu_binary_imm_reg(OpndSize_32, sub_opc, -delta, P_GPR_2, true);
    } else if(delta > 0) {
        alu_binary_imm_reg(OpndSize_32, add_opc, delta, P_GPR_2, true);
    }
    compare_mem_reg(OpndSize_32, OFFSETOF_MEMBER(ArrayObject, length), P_GPR_1, true, P_GPR_2, true);
    condJumpToBasicBlock (Condition_NC, cUnit->exceptionBlockId);
}

/*
 * vA = arrayReg;
 * vB = idxReg;
 * vC = endConditionReg;
 * arg[0] = maxC
 * arg[1] = minC
 * arg[2] = loopBranchConditionCode
 */
void genHoistedChecksForCountDownLoop(CompilationUnit *cUnit, MIR *mir)
{
    DecodedInstruction *dInsn = &mir->dalvikInsn;
    const int maxC = dInsn->arg[0];

    //First do the null check
    genHoistedNullCheck (cUnit, mir);

    /* assign index in virtual register to P_GPR_2 */
    get_virtual_reg(mir->dalvikInsn.vB, OpndSize_32, P_GPR_2, true);

    if (maxC < 0) {
        //if P_GPR_2 is mapped to a VR, we can't do this
        alu_binary_imm_reg(OpndSize_32, sub_opc, -maxC, P_GPR_2, true);
    } else if(maxC > 0) {
        alu_binary_imm_reg(OpndSize_32, add_opc, maxC, P_GPR_2, true);
    }
    compare_mem_reg(OpndSize_32, OFFSETOF_MEMBER(ArrayObject, length), P_GPR_1, true, P_GPR_2, true);
    condJumpToBasicBlock (Condition_NC, cUnit->exceptionBlockId);

}

#undef P_GPR_1
#undef P_GPR_2

/*
 * vA = idxReg;
 * vB = minC;
 */
#define P_GPR_1 PhysicalReg_ECX
void genHoistedLowerBoundCheck(CompilationUnit *cUnit, MIR *mir)
{
    DecodedInstruction *dInsn = &mir->dalvikInsn;
    const int minC = dInsn->vB;
    get_virtual_reg(mir->dalvikInsn.vA, OpndSize_32, P_GPR_1, true); //array
    export_pc();
    compare_imm_reg(OpndSize_32, -minC, P_GPR_1, true);
    condJumpToBasicBlock (Condition_L, cUnit->exceptionBlockId);
}
#undef P_GPR_1

bool genValidationForPredictedInline (CompilationUnit *cUnit, MIR *mir)
{
    //This function should only be called when generating inline prediction
    assert (static_cast<ExtendedMIROpcode> (mir->dalvikInsn.opcode) == kMirOpCheckInlinePrediction);

    //Now create some locals to make it easier to read
    const int temp1 = 1;
    const int temp2 = 2;
    const int vrThisPtr = mir->dalvikInsn.vC;
    const int clazzLiteral = mir->dalvikInsn.vB;

    //Now that we got the desired information we start generating some code.
    //First we get the "this" pointer and put it in temp1
    get_virtual_reg (vrThisPtr, OpndSize_32, temp1, false);

    //Now we do a null check unless it is not needed
    if ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
    {
        nullCheck (temp1, false, 1, vrThisPtr);
    }

    //Load the class of "this" into temp2
    move_mem_to_reg (OpndSize_32, OFFSETOF_MEMBER (Object, clazz), temp1, false, temp2, false);

    //Compare the predicted class with the actual class
    compare_imm_reg (OpndSize_32, clazzLiteral, temp2, false);

    //If the classes are not equal, then conditionally jump to the taken branch which is the invoke.
    //Otherwise, fall through to the inlined method. Since this has same semantics as the if bytecode,
    //we can use the common_if implementation
    generateConditionalJumpToTakenBlock (Condition_NE);

    //We successfully generated the prediction validation if we get here
    return true;
}

/**
 * @brief Uses heuristics to determine whether a registerize request should be satisfied.
 * @param physicalType The backend physical type for the registerize request
 * @return Returns true if the registerize request should be satisfied.
 */
static bool shouldGenerateRegisterize (LowOpndRegType physicalType)
{
    std::set<PhysicalReg> freeGPs, freeXMMs;

    //Get the free registers available
    findFreeRegisters (freeGPs, true, false);
    findFreeRegisters (freeXMMs, false, true);

    //If we want to registerize into a GP and we have no more, then reject this request
    if (freeGPs.size () == 0 && physicalType == LowOpndRegType_gp)
    {
        return false;
    }

    //If we want to registerize into an XMM and we have no more, then reject this request
    if (freeXMMs.size () == 0 && (physicalType == LowOpndRegType_ss || physicalType == LowOpndRegType_xmm))
    {
        return false;
    }

    //We accept the registerize request if we get here
    return true;
}

bool genRegisterize (CompilationUnit *cUnit, BasicBlock_O1 *bb, MIR *mir)
{
    //Get the virtual register which is vA
    int vR = mir->dalvikInsn.vA;

    //Get the class from vB, it determines which instruction to use for the move
    RegisterClass regClass = static_cast<RegisterClass> (mir->dalvikInsn.vB);

    LowOpndRegType physicalType = LowOpndRegType_invalid;

    // We want to figure out the mapping between the register class and the backend physical type
    if (regClass == kCoreReg)
    {
        physicalType = LowOpndRegType_gp;
    }
    else if (regClass == kSFPReg)
    {
        physicalType = LowOpndRegType_ss;
    }
    else if (regClass == kDFPReg)
    {
        physicalType = LowOpndRegType_xmm;
    }

    //If we haven't determined a proper backend type, we reject this case
    if (physicalType == LowOpndRegType_invalid)
    {
        ALOGI ("JIT_INFO: genRegisterize is requesting an unsupported regClass %d", regClass);
        SET_JIT_ERROR (kJitErrorUnsupportedBytecode);
        return false;
    }

    //We haven't registerized yet so we mark it as false for now until we actually do it
    bool registerized = false;

    //Look for this virtual register in the compile table
    CompileTable::const_iterator vrPtr = compileTable.findVirtualRegister (vR, physicalType);

    //We should already have this virtual register in the compile table because it is part of
    //uses of this extended MIR. However, if we don't, then simply ignore the registerize request.
    if (vrPtr != compileTable.end())
    {
        //Get the compile entry reference
        const CompileTableEntry &compileEntry = *vrPtr;

        //We check if it is already in physical register so we don't reload if not needed.
        if (compileEntry.inPhysicalRegister () == false)
        {
            //We might want to load it in physical register so check the heuristics
            if (shouldGenerateRegisterize (physicalType) == true)
            {
                //What is the size of this virtual register?
                OpndSize size = compileEntry.getSize ();

                //Define the temporary we will load into
                const int temp = 1;

                //Now we want to do the actual loading of this virtual register. We do this by using a trick
                //to load the virtual register into a temp. And then to make sure the load happens we alias
                //the virtual register to that temp
                if (physicalType == LowOpndRegType_gp)
                {
                    get_virtual_reg (vR, size, temp, false);
                    set_virtual_reg (vR, size, temp, false);
                    registerized = true;
                }
                else if (physicalType == LowOpndRegType_ss)
                {
                    get_VR_ss (vR, temp, false);
                    set_VR_ss (vR, temp, false);
                    registerized = true;
                }
                else if (physicalType == LowOpndRegType_xmm)
                {
                    get_VR_sd (vR, temp, false);
                    set_VR_sd (vR, temp, false);
                    registerized = true;
                }
            }
        }
        else
        {
            //This is already in physical register so we mark it as having been registerized
            registerized = true;
        }
    }

    //If we don't satisfy this registerize request then we should make this part of the writeback requests
    if (registerized == false)
    {
        //We have a wide virtual register if its backend type is xmm
        bool isWideVr = (physicalType == LowOpndRegType_xmm);

        BitVector *writebacks = bb->requestWriteBack;

        //Put this VR in this block's writeback requests
        dvmSetBit (writebacks, vR);

        //If it is wide, we make sure the high VR also makes it in the writeback requests
        if (isWideVr == true)
        {
            dvmSetBit (writebacks, vR + 1);
        }
    }

    //If we get here, everything was handled
    return true;
}

bool genMove128b (CompilationUnit *cUnit, MIR *mir)
{
    const int sourceXmm = PhysicalReg_StartOfXmmMarker + mir->dalvikInsn.vB;
    const int destXmm = PhysicalReg_StartOfXmmMarker + mir->dalvikInsn.vA;

    //Move from one xmm to the other
    move_reg_to_reg (OpndSize_128, sourceXmm, true, destXmm, true);

    //No error
    return true;
}

bool genPackedSet (CompilationUnit *cUnit, MIR *mir)
{

    int destXMM = PhysicalReg_StartOfXmmMarker + mir->dalvikInsn.vA;

    int vrNum = mir->dalvikInsn.vB;
    OpndSize vecUnitSize = static_cast<OpndSize> (mir->dalvikInsn.vC);

    //We use temp1 to keep the virtual register
    const int temp1 = 1;

    //Get the virtual register which is 32-bit
    get_virtual_reg (vrNum, OpndSize_32, temp1, false);

    //Move it to the src VR
    move_gp_to_xmm (temp1, false, destXMM, true);

    //Zero out the shuffle mask
    unsigned short mask = 0;

    //Do the shuffle
    bool success = vec_shuffle_reg_reg (destXMM, true, destXMM, true, vecUnitSize, mask);

    return success;
}

bool genMoveData128b (CompilationUnit *cUnit, MIR *mir)
{

    int destXMM = PhysicalReg_StartOfXmmMarker + mir->dalvikInsn.vA;
    int val128 = mir->dalvikInsn.arg[0];
    int val96 = mir->dalvikInsn.arg[1];
    int val64 = mir->dalvikInsn.arg[2];
    int val32 = mir->dalvikInsn.arg[3];

    //If we are just loading zero, then we can just zero out the destination register
    if (val32 == 0 && val64 == 0 && val96 == 0 && val128 == 0)
    {
        //We just do a PXOR on the destination register.
        dump_reg_reg (Mnemonic_PXOR, ATOM_NORMAL_ALU, OpndSize_64, destXMM, true, destXMM, true, LowOpndRegType_xmm);
    }
    else
    {
        //The width of instruction for MOVDQA (66 0F 6F) plus one modRM byte
        const int insWidth = 4;

        //Since const list is always appends to head, we add the second of constant first
        addNewToConstList (&(cUnit->constListHead), val96, val128, destXMM, false);

        // We want this const value to be ignored. The system should not look for an instruction to patch
        // So, we put the steam address and ins offset to 0
        bool stored = saveAddrToConstList (&(cUnit->constListHead), val96, val128, destXMM, 0, 0);

        if (stored == false)
        {
            return false;
        }

        //Now add the first part of constant and ensure to ask for 16-byte alignment
        addNewToConstList (&(cUnit->constListHead), val32, val64, destXMM, true);

        //This is the beginning 64-bits of the const value. The next call to this function puts the other half. We use
        //the address of this const in our movdqa.
        stored = saveAddrToConstList (&(cUnit->constListHead), val32, val64, destXMM, stream, insWidth);

        if (stored == false)
        {
            return false;
        }

        //Dummy address so that the constant patching is done on this address
        int dispAddr = getGlobalDataAddr ("64bits");

        //Now generate the MOVDQA
        dump_mem_reg(Mnemonic_MOVDQA, ATOM_NORMAL, OpndSize_128, dispAddr, PhysicalReg_Null, true,
               MemoryAccess_Constants, 0, destXMM, true, LowOpndRegType_xmm, &(cUnit->constListHead));
    }

    return true;
}

bool genPackedAlu (CompilationUnit *cUnit, MIR *mir, ALU_Opcode aluOperation)
{
    int dstXmm = mir->dalvikInsn.vA + PhysicalReg_StartOfXmmMarker;
    OpndSize vecUnitSize = static_cast<OpndSize> (mir->dalvikInsn.vC);

    //For some of the packed extended MIRs, the field vB can mean different things.
    //For shifts, the vB holds immediate. For others it holds the vector register.
    //So right now we set both up and each invidual implementation picks one of these.
    int srcXmm = mir->dalvikInsn.vB + PhysicalReg_StartOfXmmMarker;
    int immediate = mir->dalvikInsn.vB;

    bool success = false;

    switch (aluOperation)
    {
        case add_opc:
            success = vec_add_reg_reg (srcXmm, true, dstXmm, true, vecUnitSize);
            break;
        case mul_opc:
            success = vec_mul_reg_reg (srcXmm, true, dstXmm, true, vecUnitSize);
            break;
        case sub_opc:
            success = vec_sub_reg_reg (srcXmm, true, dstXmm, true, vecUnitSize);
            break;
        case and_opc:
            success = vec_and_reg_reg (srcXmm, true, dstXmm, true);
            break;
        case or_opc:
            success = vec_or_reg_reg (srcXmm, true, dstXmm, true);
            break;
        case xor_opc:
            success = vec_xor_reg_reg (srcXmm, true, dstXmm, true);
            break;
        case shl_opc:
            success = vec_shift_left_imm_reg (immediate, dstXmm, true, vecUnitSize);
            break;
        case shr_opc:
            success = vec_unsigned_shift_right_imm_reg (immediate, dstXmm, true, vecUnitSize);
            break;
        case sar_opc:
            success = vec_signed_shift_right_imm_reg (immediate, dstXmm, true, vecUnitSize);
            break;
        default:
            ALOGD ("JIT_INFO: Unsupported operation type for packed alu generation.");
            break;
    }

    return success;
}

bool genPackedHorizontalOperationWithReduce (CompilationUnit *cUnit, MIR *mir, ALU_Opcode horizontalOperation)
{
    int dstVr = mir->dalvikInsn.vA;
    int srcXmm = mir->dalvikInsn.vB + PhysicalReg_StartOfXmmMarker;
    OpndSize vecUnitSize = static_cast<OpndSize> (mir->dalvikInsn.vC);
    int extractIndex = mir->dalvikInsn.arg[0];

    if (vecUnitSize > OpndSize_32)
    {
        //We are extracting to a GP and thus cannot hold more than 4 bytes
        return false;
    }

    //Now determine number of times we need to do the horizontal operation
    const unsigned int vectorBytes = 16;
    int width = vectorBytes / vecUnitSize;

    //Create the right number of horizontal adds
    while (width > 1)
    {
        bool success = false;

        if (horizontalOperation == add_opc)
        {
            success = vec_horizontal_add_reg_reg (srcXmm, true, srcXmm, true, vecUnitSize);
        }
        else if (horizontalOperation == sub_opc)
        {
            success = vec_horizontal_sub_reg_reg (srcXmm, true, srcXmm, true, vecUnitSize);
        }
        else
        {
            ALOGD ("JIT_INFO: Unsupported horizontal operation for packed reduce");
            return false;
        }

        if (success == false)
        {
            //Just pass the error message
            return false;
        }

        width >>= 1;
    }

    //We will use temp2 to extract to. And temp 1 for VR
    const int temp1 = 1;
    const int temp2 = 2;

    //Now do the actual extraction
    bool extracted = vec_extract_imm_reg_reg (extractIndex, srcXmm, true, temp2, false, vecUnitSize);

    if (extracted == false)
    {
        //Just pass the error message
        return false;
    }

    //Get virtual register
    get_virtual_reg (dstVr, OpndSize_32, temp1, false);

    //Now add the reduction result to VR
    alu_binary_reg_reg (OpndSize_32, add_opc, temp2, false, temp1, false);

    //Now alias the destination VR to the temp where we figured out result
    set_virtual_reg (dstVr, OpndSize_32, temp1, false);
    return true;
}

bool genCheckStackOverflow (CompilationUnit *cUnit, MIR *mir)
{
    assert (static_cast<ExtendedMIROpcode> (mir->dalvikInsn.opcode) == kMirOpCheckStackOverflow);

    //Set up some variables to improve readability
    const int temp1 = 1;
    const int temp2 = 2;
    const int exceptionState = 1;

    //Get self pointer and put it in temp1
    get_self_pointer (temp1, false);

    //Move the frame pointer into temp2
    move_reg_to_reg (OpndSize_32, PhysicalReg_FP, true, temp2, false);

    //vB holds the size of space of frame needed relative to frame pointer
    int spaceNeeded = mir->dalvikInsn.vB;

    //Stack grows in negative direction so subtract the size from the frame pointer
    alu_binary_imm_reg (OpndSize_32, sub_opc, spaceNeeded, temp2, false);

    //Now compare the stack bottom with our expected stack bottom
    compare_mem_reg (OpndSize_32, OFFSETOF_MEMBER (Thread, interpStackEnd), temp1, false, temp2, false);

    //We want to throw a StackOverflow exception but we don't have the right logic here to do that.
    //Therefore we simply jump to "common_exception" which in turn generates a jump to exception block.
    handlePotentialException (Condition_BE, Condition_NBE, exceptionState, "common_exception");

    //If we get here everything went well
    return true;
}

bool genPackedReduce (CompilationUnit *cUnit, MIR *mir)
{
    int dstVr = mir->dalvikInsn.vA;
    int srcXmm = mir->dalvikInsn.vB + PhysicalReg_StartOfXmmMarker;
    int extractIndex = mir->dalvikInsn.arg[0];
    OpndSize vecUnitSize = static_cast<OpndSize> (mir->dalvikInsn.vC);

    //Use temp 1 for VR. We extract directly there
    const int temp1 = 1;

    //Now do the actual extraction
    bool extracted = vec_extract_imm_reg_reg (extractIndex, srcXmm, true, temp1, false, vecUnitSize);

    if (extracted == false)
    {
        //Just pass the error message
        return false;
    }

    //Now alias the destination VR to the temp where we figured out result
    set_virtual_reg (dstVr, OpndSize_32, temp1, false);

    return true;
}
