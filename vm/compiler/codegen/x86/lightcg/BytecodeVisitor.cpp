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


/*! \file BytecodeVisitor.cpp
    \brief This file implements visitors of the bytecode
*/
#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "libdex/DexOpcodes.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "AnalysisO1.h"
#include "CompilationUnit.h"
#include "MethodContext.h"
#include "MethodContextHandler.h"

#if 0 /* This is dead code and has been disabled. If reenabling,
         the MIR or opcode must be passed in as a parameter */
//! Returns size of the current bytecode in u2 unit

//!
int getByteCodeSize() { //uses inst, unit in u2
    switch (INST_INST(inst)) {
    case OP_NOP:
        return 1;
    case OP_MOVE:
    case OP_MOVE_OBJECT:
        return 1;
    case OP_MOVE_FROM16:
    case OP_MOVE_OBJECT_FROM16:
        return 2;
    case OP_MOVE_16:
    case OP_MOVE_OBJECT_16:
        return 3;
    case OP_MOVE_WIDE:
        return 1;
    case OP_MOVE_WIDE_FROM16:
        return 2;
    case OP_MOVE_WIDE_16:
        return 3;
    case OP_MOVE_RESULT:
    case OP_MOVE_RESULT_OBJECT:
        return 1;
    case OP_MOVE_RESULT_WIDE:
        return 1;
    case OP_MOVE_EXCEPTION:
        return 1;
    case OP_RETURN_VOID:
    case OP_RETURN_VOID_BARRIER:
        return 1;
    case OP_RETURN:
    case OP_RETURN_OBJECT:
        return 1;
    case OP_RETURN_WIDE:
        return 1;
    case OP_CONST_4:
        return 1;
    case OP_CONST_16:
        return 2;
    case OP_CONST:
        return 3;
    case OP_CONST_HIGH16:
        return 2;
    case OP_CONST_WIDE_16:
        return 2;
    case OP_CONST_WIDE_32:
        return 3;
    case OP_CONST_WIDE:
        return 5;
    case OP_CONST_WIDE_HIGH16:
        return 2;
    case OP_CONST_STRING:
        return 2;
    case OP_CONST_STRING_JUMBO:
        return 3;
    case OP_CONST_CLASS:
        return 2;
    case OP_MONITOR_ENTER:
        return 1;
    case OP_MONITOR_EXIT:
        return 1;
    case OP_CHECK_CAST:
        return 2;
    case OP_INSTANCE_OF:
        return 2;
    case OP_ARRAY_LENGTH:
        return 1;
    case OP_NEW_INSTANCE:
        return 2;
    case OP_NEW_ARRAY:
        return 2;
    case OP_FILLED_NEW_ARRAY:
        return 3;
    case OP_FILLED_NEW_ARRAY_RANGE:
        return 3;
    case OP_FILL_ARRAY_DATA:
        return 3;
    case OP_THROW:
        return 1;
    case OP_THROW_VERIFICATION_ERROR:
        return 2;
    case OP_GOTO:
        return 1;
    case OP_GOTO_16:
        return 2;
    case OP_GOTO_32:
        return 3;
    case OP_PACKED_SWITCH:
        return 3;
    case OP_SPARSE_SWITCH:
        return 3;
    case OP_CMPL_FLOAT:
        return 2;
    case OP_CMPG_FLOAT:
        return 2;
    case OP_CMPL_DOUBLE:
        return 2;
    case OP_CMPG_DOUBLE:
        return 2;
    case OP_CMP_LONG:
        return 2;
    case OP_IF_EQ:
        return 2;
    case OP_IF_NE:
        return 2;
    case OP_IF_LT:
        return 2;
    case OP_IF_GE:
        return 2;
    case OP_IF_GT:
        return 2;
    case OP_IF_LE:
        return 2;
    case OP_IF_EQZ:
        return 2;
    case OP_IF_NEZ:
        return 2;
    case OP_IF_LTZ:
        return 2;
    case OP_IF_GEZ:
        return 2;
    case OP_IF_GTZ:
        return 2;
    case OP_IF_LEZ:
        return 2;
    case OP_AGET:
        return 2;
    case OP_AGET_WIDE:
        return 2;
    case OP_AGET_OBJECT:
        return 2;
    case OP_AGET_BOOLEAN:
        return 2;
    case OP_AGET_BYTE:
        return 2;
    case OP_AGET_CHAR:
        return 2;
    case OP_AGET_SHORT:
        return 2;
    case OP_APUT:
        return 2;
    case OP_APUT_WIDE:
        return 2;
    case OP_APUT_OBJECT:
        return 2;
    case OP_APUT_BOOLEAN:
        return 2;
    case OP_APUT_BYTE:
        return 2;
    case OP_APUT_CHAR:
        return 2;
    case OP_APUT_SHORT:
        return 2;
    case OP_IGET:
    case OP_IGET_WIDE:
    case OP_IGET_OBJECT:
    case OP_IGET_VOLATILE:
    case OP_IGET_WIDE_VOLATILE:
    case OP_IGET_OBJECT_VOLATILE:
    case OP_IGET_BOOLEAN:
    case OP_IGET_BYTE:
    case OP_IGET_CHAR:
    case OP_IGET_SHORT:
    case OP_IPUT:
    case OP_IPUT_WIDE:
    case OP_IPUT_OBJECT:
    case OP_IPUT_VOLATILE:
    case OP_IPUT_WIDE_VOLATILE:
    case OP_IPUT_OBJECT_VOLATILE:
    case OP_IPUT_BOOLEAN:
    case OP_IPUT_BYTE:
    case OP_IPUT_CHAR:
    case OP_IPUT_SHORT:
        return 2;
    case OP_SGET:
    case OP_SGET_WIDE:
    case OP_SGET_OBJECT:
    case OP_SGET_VOLATILE:
    case OP_SGET_WIDE_VOLATILE:
    case OP_SGET_OBJECT_VOLATILE:
    case OP_SGET_BOOLEAN:
    case OP_SGET_BYTE:
    case OP_SGET_CHAR:
    case OP_SGET_SHORT:
    case OP_SPUT:
    case OP_SPUT_WIDE:
    case OP_SPUT_OBJECT:
    case OP_SPUT_VOLATILE:
    case OP_SPUT_WIDE_VOLATILE:
    case OP_SPUT_OBJECT_VOLATILE:
    case OP_SPUT_BOOLEAN:
    case OP_SPUT_BYTE:
    case OP_SPUT_CHAR:
    case OP_SPUT_SHORT:
        return 2;
    case OP_INVOKE_VIRTUAL:
    case OP_INVOKE_SUPER:
    case OP_INVOKE_DIRECT:
    case OP_INVOKE_STATIC:
    case OP_INVOKE_INTERFACE:
    case OP_INVOKE_VIRTUAL_RANGE:
    case OP_INVOKE_SUPER_RANGE:
    case OP_INVOKE_DIRECT_RANGE:
    case OP_INVOKE_STATIC_RANGE:
    case OP_INVOKE_INTERFACE_RANGE:
        return 3;

    case OP_NEG_INT:
    case OP_NOT_INT:
    case OP_NEG_LONG:
    case OP_NOT_LONG:
    case OP_NEG_FLOAT:
    case OP_NEG_DOUBLE:
    case OP_INT_TO_LONG:
    case OP_INT_TO_FLOAT:
    case OP_INT_TO_DOUBLE:
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
        return 1;

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
        return 2;

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
        return 1;

    case OP_ADD_INT_LIT16:
    case OP_RSUB_INT:
    case OP_MUL_INT_LIT16:
    case OP_DIV_INT_LIT16:
    case OP_REM_INT_LIT16:
    case OP_AND_INT_LIT16:
    case OP_OR_INT_LIT16:
    case OP_XOR_INT_LIT16:
        return 2;

    case OP_ADD_INT_LIT8:
    case OP_RSUB_INT_LIT8:
    case OP_MUL_INT_LIT8:
    case OP_DIV_INT_LIT8:
    case OP_REM_INT_LIT8:
    case OP_AND_INT_LIT8:
    case OP_OR_INT_LIT8:
    case OP_XOR_INT_LIT8:
    case OP_SHL_INT_LIT8:
    case OP_SHR_INT_LIT8:
    case OP_USHR_INT_LIT8:
        return 2;

    case OP_EXECUTE_INLINE:
    case OP_EXECUTE_INLINE_RANGE:
        return 3;
#if FIXME
    case OP_INVOKE_OBJECT_INIT_RANGE:
        return 3;
#endif

    case OP_IGET_QUICK:
    case OP_IGET_WIDE_QUICK:
    case OP_IGET_OBJECT_QUICK:
    case OP_IPUT_QUICK:
    case OP_IPUT_WIDE_QUICK:
    case OP_IPUT_OBJECT_QUICK:
        return 2;

    case OP_INVOKE_VIRTUAL_QUICK:
    case OP_INVOKE_VIRTUAL_QUICK_RANGE:
    case OP_INVOKE_SUPER_QUICK:
    case OP_INVOKE_SUPER_QUICK_RANGE:
        return 3;
#ifdef SUPPORT_HLO
    case kExtInstruction:
        switch(inst) {
        case OP_X_AGET_QUICK:
        case OP_X_AGET_WIDE_QUICK:
        case OP_X_AGET_OBJECT_QUICK:
    case OP_X_AGET_BOOLEAN_QUICK:
    case OP_X_AGET_BYTE_QUICK:
    case OP_X_AGET_CHAR_QUICK:
    case OP_X_AGET_SHORT_QUICK:
    case OP_X_APUT_QUICK:
    case OP_X_APUT_WIDE_QUICK:
    case OP_X_APUT_OBJECT_QUICK:
    case OP_X_APUT_BOOLEAN_QUICK:
    case OP_X_APUT_BYTE_QUICK:
    case OP_X_APUT_CHAR_QUICK:
    case OP_X_APUT_SHORT_QUICK:
        return 3;
    case OP_X_DEREF_GET:
    case OP_X_DEREF_GET_OBJECT:
    case OP_X_DEREF_GET_WIDE:
    case OP_X_DEREF_GET_BOOLEAN:
    case OP_X_DEREF_GET_BYTE:
    case OP_X_DEREF_GET_CHAR:
    case OP_X_DEREF_GET_SHORT:
    case OP_X_DEREF_PUT:
    case OP_X_DEREF_PUT_WIDE:
    case OP_X_DEREF_PUT_OBJECT:
    case OP_X_DEREF_PUT_BOOLEAN:
    case OP_X_DEREF_PUT_BYTE:
    case OP_X_DEREF_PUT_CHAR:
    case OP_X_DEREF_PUT_SHORT:
        return 2;
    case OP_X_ARRAY_CHECKS:
    case OP_X_ARRAY_OBJECT_CHECKS:
        return 3;
    case OP_X_CHECK_BOUNDS:
    case OP_X_CHECK_NULL:
    case OP_X_CHECK_TYPE:
        return 2;
    }
#endif
    default:
        ALOGI("JIT_INFO: JIT does not support getting size of bytecode 0x%hx\n",
                currentMIR->dalvikInsn.opcode);
        SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
        assert(false && "All opcodes should be supported.");
        break;
    }
    return -1;
}
#endif

//! \brief reduces refCount of a virtual register
//!
//! \param vA
//! \param type
//!
//! \return -1 on error, 0 otherwise
static int touchOneVR(int vA, LowOpndRegType type) {
    int index = searchCompileTable(LowOpndRegType_virtual | type, vA);
    if(index < 0) {
        ALOGI("JIT_INFO: virtual reg %d type %d not found in touchOneVR\n", vA, type);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
    compileTable[index].refCount--;
    return 0;
}

/** @brief count of const in worklist  */
int num_const_worklist;

/** @brief  worklist to update constVRTable later */
int constWorklist[10];

/**
  * @brief Clears the list registers with killed constants
  */
static void clearConstKills(void) {
    num_const_worklist = 0;
}

/**
  * @brief  Adds a register for which any previous constant
  *  held is killed by an operation.
  * @param v a virtual register
  */
static void addConstKill(u2 v) {
    constWorklist[num_const_worklist++] = v;
}

int num_const_vr; //in a basic block
//! table to store the constant information for virtual registers
ConstVRInfo constVRTable[MAX_CONST_REG];
//! update constVRTable for a given virtual register

//! set "isConst" to false
void setVRToNonConst(int regNum, OpndSize size) {
    int k;
    int indexL = -1;
    int indexH = -1;
    for(k = 0; k < num_const_vr; k++) {
        if(constVRTable[k].regNum == regNum) {
            indexL = k;
            continue;
        }
        if(constVRTable[k].regNum == regNum + 1 && size == OpndSize_64) {
            indexH = k;
            continue;
        }
    }
    if(indexL >= 0) {
        //remove this entry??
        constVRTable[indexL].isConst = false;
    }
    if(size == OpndSize_64 && indexH >= 0) {
        constVRTable[indexH].isConst = false;
    }
}

/**
 * @brief Marks a virtual register as being constant.
 * @param regNum The virtual register number.
 * @param size The size of the virtual register.
 * @param tmpValue Array representing the constant values for this VR. It must be the correct size to match
 * the size argument.
 * @return Returns true if setting VR to constant succeeded. On failure it returns false.
 */
bool setVRToConst (int regNum, OpndSize size, int *tmpValue)
{
    assert (tmpValue != 0);

    int k;
    int indexL = -1;
    int indexH = -1;
    for(k = 0; k < num_const_vr; k++) {
        if(constVRTable[k].regNum == regNum) {
            indexL = k;
            continue;
        }
        if(constVRTable[k].regNum == regNum + 1 && size == OpndSize_64) {
            indexH = k;
            continue;
        }
    }

    //Add the entry for the VR to the table if we don't have it
    if(indexL < 0)
    {
        //Now check for possible table overflow. If we don't have an entry for this,
        //then we must add it. Check now for possible overflow.
        if (num_const_vr >= MAX_CONST_REG)
        {
            ALOGI("JIT_INFO: constVRTable overflows at setVRToConst.");
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return false;
        }

        indexL = num_const_vr;
        constVRTable[indexL].regNum = regNum;
        num_const_vr++;
    }

    //Now initialize the entry with the constant value
    constVRTable[indexL].isConst = true;
    constVRTable[indexL].value = tmpValue[0];

    //If we have a 64-bit VR, we must also initialize the high bits
    if (size == OpndSize_64)
    {
        //Add entry to the table if we don't have it
        if (indexH < 0)
        {
            //Now check for possible table overflow. If we don't have an entry for this,
            //then we must add it. Check now for possible overflow.
            if (num_const_vr >= MAX_CONST_REG)
            {
                ALOGI("JIT_INFO: constVRTable overflows at setVRToConst.");
                SET_JIT_ERROR(kJitErrorRegAllocFailed);
                return false;
            }

            indexH = num_const_vr;
            constVRTable[indexH].regNum = regNum + 1;
            num_const_vr++;
        }

        //Now initialize the entry with the constant value
        constVRTable[indexH].isConst = true;
        constVRTable[indexH].value = tmpValue[1];
    }

    //This VR just became a constant so invalidate other information we have about it
    invalidateVRDueToConst (regNum, size);

    //If we make it here we were successful
    return true;
}

//! perform work on constWorklist

//!
void updateConstInfo(BasicBlock_O1* bb) {
    if(bb == NULL) return;
    int k;
    for(k = 0; k < num_const_worklist; k++) {
        //int indexOrig = constWorklist[k];
        //compileTable[indexOrig].isConst = false;
        //int A = compileTable[indexOrig].regNum;
        //LowOpndRegType type = compileTable[indexOrig].physicalType & MASK_FOR_TYPE;
        setVRToNonConst(constWorklist[k], OpndSize_32);
    }
}

//! \brief check whether the current bytecode generates a const
//!
//! \details if yes, update constVRTable; otherwise, update constWorklist
//! if a bytecode uses vA (const), and updates vA to non const, getConstInfo
//! will return 0 and update constWorklist to make sure when lowering the
//! bytecode, vA is treated as constant
//!
//! \param bb the BasicBlock_O1 to analyze
//! \param currentMIR
//!
//! \return 1 if the bytecode generates a const, 0 otherwise, and -1 if an
//! error occured.
int getConstInfo(BasicBlock_O1* bb, const MIR * currentMIR) {
    //retCode and success are used to keep track of success of function calls from this function
    int retCode = 0;
    bool success = false;

    Opcode inst_op = currentMIR->dalvikInsn.opcode;
    int vA = 0, vB = 0, v1, v2;
    u2 BBBB;
    u2 tmp_u2;
    s4 tmp_s4;
    u4 tmp_u4;
    int entry, tmpValue[2], tmpValue2[2];

    clearConstKills ();

    /* A bytecode with the MIR_INLINED op will be treated as
     * no-op during codegen */
    if (currentMIR->OptimizationFlags & MIR_INLINED)
        return 0; // does NOT generate a constant

    // Check if we need to handle an extended MIR
    if (currentMIR->dalvikInsn.opcode >= static_cast<Opcode> (kMirOpFirst)) {
        //Currently no extended MIR generates constants
        switch (static_cast<ExtendedMIROpcode>(currentMIR->dalvikInsn.opcode)) {
            default:
                // No constant is generated
                return 0;
        }
    }

    switch(inst_op) {
        //for other opcode, if update the register, set isConst to false
    case OP_MOVE:
    case OP_MOVE_OBJECT:
    case OP_MOVE_FROM16:
    case OP_MOVE_OBJECT_FROM16:
    case OP_MOVE_16:
    case OP_MOVE_OBJECT_16:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        if(isVirtualRegConstant(vB, LowOpndRegType_gp, tmpValue, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
            if (entry < 0)
                return -1;

            success = setVRToConst(vA, OpndSize_32, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(vB, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
            return 1;
        } else {
            addConstKill(vA);
        }
        return 0;
    case OP_MOVE_WIDE:
    case OP_MOVE_WIDE_FROM16:
    case OP_MOVE_WIDE_16:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        if(isVirtualRegConstant(vB, LowOpndRegType_xmm, tmpValue, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_xmm);
            if (entry < 0)
                return -1;

            success = setVRToConst(vA, OpndSize_64, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(vB, LowOpndRegType_xmm);
            if (retCode < 0)
                return retCode;
            return 1;
        } else {
            addConstKill(vA);
            addConstKill(vA+1);
        }
        return 0;
    case OP_MOVE_RESULT:
    case OP_MOVE_RESULT_OBJECT:
    case OP_MOVE_EXCEPTION:
    case OP_CONST_STRING:
    case OP_CONST_STRING_JUMBO:
    case OP_CONST_CLASS:
    case OP_NEW_INSTANCE:
    case OP_CMPL_FLOAT:
    case OP_CMPG_FLOAT:
    case OP_CMPL_DOUBLE:
    case OP_CMPG_DOUBLE:
    case OP_AGET:
    case OP_AGET_OBJECT:
    case OP_AGET_BOOLEAN:
    case OP_AGET_BYTE:
    case OP_AGET_CHAR:
    case OP_AGET_SHORT:
    case OP_SGET:
    case OP_SGET_OBJECT:
    case OP_SGET_VOLATILE:
    case OP_SGET_OBJECT_VOLATILE:
    case OP_SGET_BOOLEAN:
    case OP_SGET_BYTE:
    case OP_SGET_CHAR:
    case OP_SGET_SHORT:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        return 0;
    case OP_MOVE_RESULT_WIDE:
    case OP_AGET_WIDE:
    case OP_SGET_WIDE:
    case OP_SGET_WIDE_VOLATILE:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_INSTANCE_OF:
    case OP_ARRAY_LENGTH:
    case OP_NEW_ARRAY:
    case OP_IGET:
    case OP_IGET_OBJECT:
    case OP_IGET_VOLATILE:
    case OP_IGET_OBJECT_VOLATILE:
    case OP_IGET_BOOLEAN:
    case OP_IGET_BYTE:
    case OP_IGET_CHAR:
    case OP_IGET_SHORT:
    case OP_IGET_QUICK:
    case OP_IGET_OBJECT_QUICK:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        return 0;
    case OP_IGET_WIDE:
    case OP_IGET_WIDE_VOLATILE:
    case OP_IGET_WIDE_QUICK:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
        //TODO: constant folding for float/double/long ALU
    case OP_ADD_FLOAT:
    case OP_SUB_FLOAT:
    case OP_MUL_FLOAT:
    case OP_DIV_FLOAT:
    case OP_REM_FLOAT:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        return 0;
    case OP_ADD_DOUBLE:
    case OP_SUB_DOUBLE:
    case OP_MUL_DOUBLE:
    case OP_DIV_DOUBLE:
    case OP_REM_DOUBLE:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_NEG_FLOAT:
    case OP_INT_TO_FLOAT:
    case OP_LONG_TO_FLOAT:
    case OP_FLOAT_TO_INT:
    case OP_DOUBLE_TO_INT:
    case OP_ADD_FLOAT_2ADDR:
    case OP_SUB_FLOAT_2ADDR:
    case OP_MUL_FLOAT_2ADDR:
    case OP_DIV_FLOAT_2ADDR:
    case OP_REM_FLOAT_2ADDR:
    case OP_DOUBLE_TO_FLOAT:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        return 0;
    case OP_FLOAT_TO_LONG:
    case OP_DOUBLE_TO_LONG:
    case OP_FLOAT_TO_DOUBLE:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_NEG_DOUBLE:
    case OP_INT_TO_DOUBLE: //fp stack
    case OP_LONG_TO_DOUBLE:
    case OP_ADD_DOUBLE_2ADDR:
    case OP_SUB_DOUBLE_2ADDR:
    case OP_MUL_DOUBLE_2ADDR:
    case OP_DIV_DOUBLE_2ADDR:
    case OP_REM_DOUBLE_2ADDR:
        //ops on float, double
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_NEG_INT:
    case OP_NOT_INT:
    case OP_LONG_TO_INT:
    case OP_INT_TO_BYTE:
    case OP_INT_TO_CHAR:
    case OP_INT_TO_SHORT:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        if(isVirtualRegConstant(vB, LowOpndRegType_gp, tmpValue, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
            if (entry < 0)
            {
                return -1;
            }

            switch (inst_op)
            {
                case OP_NEG_INT:
                    tmpValue[0] = -tmpValue[0];
                    break;
                case OP_NOT_INT:
                    tmpValue[0] = ~tmpValue[0]; //CHECK
                    break;
                case OP_LONG_TO_INT:
                    //Nothing to do for long to int to convert value
                    break;
                case OP_INT_TO_BYTE: // sar
                    tmpValue[0] = (tmpValue[0] << 24) >> 24;
                    break;
                case OP_INT_TO_CHAR: //shr
                    tmpValue[0] = ((unsigned int) (tmpValue[0] << 16)) >> 16;
                    break;
                case OP_INT_TO_SHORT: //sar
                    tmpValue[0] = (tmpValue[0] << 16) >> 16;
                    break;
                default:
                    ALOGI ("JIT_INFO: Unsupported constant folding for %d\n", inst_op);
                    SET_JIT_ERROR (kJitErrorConstantFolding);
                    return -1;
            }

            success = setVRToConst(vA, OpndSize_32, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(vB, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
#ifdef DEBUG_CONST
            ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
            return 1;
        }
        else {
            addConstKill(vA);
            return 0;
        }
    case OP_NEG_LONG:
    case OP_NOT_LONG:
    case OP_INT_TO_LONG:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_DIV_INT_2ADDR:
    case OP_REM_INT_2ADDR:
    case OP_REM_INT_LIT16:
    case OP_DIV_INT_LIT16:
    case OP_REM_INT_LIT8:
    case OP_DIV_INT_LIT8:
    case OP_DIV_INT:
    case OP_REM_INT:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        return 0;
    case OP_ADD_INT_2ADDR:
    case OP_SUB_INT_2ADDR:
    case OP_MUL_INT_2ADDR:
    case OP_AND_INT_2ADDR:
    case OP_OR_INT_2ADDR:
    case OP_XOR_INT_2ADDR:
    case OP_SHL_INT_2ADDR:
    case OP_SHR_INT_2ADDR:
    case OP_USHR_INT_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        if(isVirtualRegConstant(vA, LowOpndRegType_gp, tmpValue, false) == 3 &&
           isVirtualRegConstant(v2, LowOpndRegType_gp, tmpValue2, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
            if (entry < 0)
            {
                return -1;
            }

            switch (inst_op)
            {
                case OP_ADD_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] + tmpValue2[0];
                    break;
                case OP_SUB_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] - tmpValue2[0];
                    break;
                case OP_MUL_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] * tmpValue2[0];
                    break;
                case OP_DIV_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] / tmpValue2[0];
                    break;
                case OP_REM_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] % tmpValue2[0];
                    break;
                case OP_AND_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] & tmpValue2[0];
                    break;
                case OP_OR_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] | tmpValue2[0];
                    break;
                case OP_XOR_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] ^ tmpValue2[0];
                    break;
                case OP_SHL_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] << tmpValue2[0];
                    break;
                case OP_SHR_INT_2ADDR:
                    tmpValue[0] = tmpValue[0] >> tmpValue2[0];
                    break;
                case OP_USHR_INT_2ADDR:
                    tmpValue[0] = (unsigned int) tmpValue[0] >> tmpValue2[0];
                    break;
                default:
                    ALOGI ("JIT_INFO: Unsupported constant folding for %d\n", inst_op);
                    SET_JIT_ERROR (kJitErrorConstantFolding);
                    return -1;
            }

            success = setVRToConst(vA, OpndSize_32, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(v2, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
#ifdef DEBUG_CONST
            ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
            return 1;
        }
        else {
            addConstKill(vA);
            return 0;
        }
    case OP_ADD_INT_LIT16:
    case OP_RSUB_INT:
    case OP_MUL_INT_LIT16:
    case OP_AND_INT_LIT16:
    case OP_OR_INT_LIT16:
    case OP_XOR_INT_LIT16:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        tmp_s4 = currentMIR->dalvikInsn.vC;
        if(isVirtualRegConstant(vB, LowOpndRegType_gp, tmpValue, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
            if (entry < 0)
            {
                return -1;
            }

            switch (inst_op)
            {
                case OP_ADD_INT_LIT16:
                    tmpValue[0] = tmpValue[0] + tmp_s4;
                    break;
                case OP_RSUB_INT:
                    tmpValue[0] = tmp_s4 - tmpValue[0];
                    break;
                case OP_MUL_INT_LIT16:
                    tmpValue[0] = tmpValue[0] * tmp_s4;
                    break;
                case OP_DIV_INT_LIT16:
                    tmpValue[0] = tmpValue[0] / tmp_s4;
                    break;
                case OP_REM_INT_LIT16:
                    tmpValue[0] = tmpValue[0] % tmp_s4;
                    break;
                case OP_AND_INT_LIT16:
                    tmpValue[0] = tmpValue[0] & tmp_s4;
                    break;
                case OP_OR_INT_LIT16:
                    tmpValue[0] = tmpValue[0] | tmp_s4;
                    break;
                case OP_XOR_INT_LIT16:
                    tmpValue[0] = tmpValue[0] ^ tmp_s4;
                    break;
                default:
                    ALOGI ("JIT_INFO: Unsupported constant folding for %d\n", inst_op);
                    SET_JIT_ERROR (kJitErrorConstantFolding);
                    return -1;
            }

            success = setVRToConst(vA, OpndSize_32, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(vB, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
#ifdef DEBUG_CONST
            ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
            return 1;
        }
        else {
            addConstKill(vA);
            return 0;
        }
    case OP_ADD_INT:
    case OP_SUB_INT:
    case OP_MUL_INT:
    case OP_AND_INT:
    case OP_OR_INT:
    case OP_XOR_INT:
    case OP_SHL_INT:
    case OP_SHR_INT:
    case OP_USHR_INT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        if(isVirtualRegConstant(v1, LowOpndRegType_gp, tmpValue, false) == 3 &&
           isVirtualRegConstant(v2, LowOpndRegType_gp, tmpValue2, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
            if (entry < 0)
            {
                return -1;
            }

            switch (inst_op)
            {
                case OP_ADD_INT:
                    tmpValue[0] = tmpValue[0] + tmpValue2[0];
                    break;
                case OP_SUB_INT:
                    tmpValue[0] = tmpValue[0] - tmpValue2[0];
                    break;
                case OP_MUL_INT:
                    tmpValue[0] = tmpValue[0] * tmpValue2[0];
                    break;
                case OP_DIV_INT:
                    tmpValue[0] = tmpValue[0] / tmpValue2[0];
                    break;
                case OP_REM_INT:
                    tmpValue[0] = tmpValue[0] % tmpValue2[0];
                    break;
                case OP_AND_INT:
                    tmpValue[0] = tmpValue[0] & tmpValue2[0];
                    break;
                case OP_OR_INT:
                    tmpValue[0] = tmpValue[0] | tmpValue2[0];
                    break;
                case OP_XOR_INT:
                    tmpValue[0] = tmpValue[0] ^ tmpValue2[0];
                    break;
                case OP_SHL_INT:
                    tmpValue[0] = tmpValue[0] << tmpValue2[0];
                    break;
                case OP_SHR_INT:
                    tmpValue[0] = tmpValue[0] >> tmpValue2[0];
                    break;
                case OP_USHR_INT:
                    tmpValue[0] = (unsigned int) tmpValue[0] >> tmpValue2[0];
                    break;
                default:
                    ALOGI ("JIT_INFO: Unsupported constant folding for %d\n", inst_op);
                    SET_JIT_ERROR (kJitErrorConstantFolding);
                    return -1;
            }

            success = setVRToConst(vA, OpndSize_32, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(v1, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
            retCode = touchOneVR(v2, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
#ifdef DEBUG_CONST
            ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
            return 1;
        }
        else {
            addConstKill(vA);
            return 0;
        }
    case OP_ADD_INT_LIT8: //INST_AA
    case OP_RSUB_INT_LIT8:
    case OP_MUL_INT_LIT8:
    case OP_AND_INT_LIT8:
    case OP_OR_INT_LIT8:
    case OP_XOR_INT_LIT8:
    case OP_SHL_INT_LIT8:
    case OP_SHR_INT_LIT8:
    case OP_USHR_INT_LIT8:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        tmp_s4 = currentMIR->dalvikInsn.vC;
        if(isVirtualRegConstant(vB, LowOpndRegType_gp, tmpValue, false) == 3) {
            entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
            if (entry < 0)
            {
                return -1;
            }

            switch (inst_op)
            {
                case OP_ADD_INT_LIT8:
                    tmpValue[0] = tmpValue[0] + tmp_s4;
                    break;
                case OP_RSUB_INT_LIT8:
                    tmpValue[0] = tmp_s4 - tmpValue[0];
                    break;
                case OP_MUL_INT_LIT8:
                    tmpValue[0] = tmpValue[0] * tmp_s4;
                    break;
                case OP_DIV_INT_LIT8:
                    tmpValue[0] = tmpValue[0] / tmp_s4;
                    break;
                case OP_REM_INT_LIT8:
                    tmpValue[0] = tmpValue[0] % tmp_s4;
                    break;
                case OP_AND_INT_LIT8:
                    tmpValue[0] = tmpValue[0] & tmp_s4;
                    break;
                case OP_OR_INT_LIT8:
                    tmpValue[0] = tmpValue[0] | tmp_s4;
                    break;
                case OP_XOR_INT_LIT8:
                    tmpValue[0] = tmpValue[0] ^ tmp_s4;
                    break;
                case OP_SHL_INT_LIT8:
                    tmpValue[0] = tmpValue[0] << tmp_s4;
                    break;
                case OP_SHR_INT_LIT8:
                    tmpValue[0] = tmpValue[0] >> tmp_s4;
                    break;
                case OP_USHR_INT_LIT8:
                    tmpValue[0] = (unsigned int) tmpValue[0] >> tmp_s4;
                    break;
                default:
                    ALOGI ("JIT_INFO: Unsupported constant folding for %d\n", inst_op);
                    SET_JIT_ERROR (kJitErrorConstantFolding);
                    return -1;
            }

            success = setVRToConst(vA, OpndSize_32, tmpValue);
            if (success == false)
            {
                //setVRToConst set an error message when it failed so we just pass along the failure information
                return -1;
            }

            compileTable[entry].refCount--;
            retCode = touchOneVR(vB, LowOpndRegType_gp);
            if (retCode < 0)
                return retCode;
#ifdef DEBUG_CONST
            ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
            return 1;
        }
        else {
            addConstKill(vA);
            return 0;
        }
    case OP_ADD_LONG:
    case OP_SUB_LONG:
    case OP_AND_LONG:
    case OP_OR_LONG:
    case OP_XOR_LONG:
    case OP_MUL_LONG:
    case OP_DIV_LONG:
    case OP_REM_LONG:
    case OP_SHL_LONG:
    case OP_SHR_LONG:
    case OP_USHR_LONG:
        //TODO bytecode is not going to update state registers
        //constant folding
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_CMP_LONG:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        return 0;
    case OP_ADD_LONG_2ADDR:
    case OP_SUB_LONG_2ADDR:
    case OP_AND_LONG_2ADDR:
    case OP_OR_LONG_2ADDR:
    case OP_XOR_LONG_2ADDR:
    case OP_MUL_LONG_2ADDR:
    case OP_DIV_LONG_2ADDR:
    case OP_REM_LONG_2ADDR:
    case OP_SHL_LONG_2ADDR:
    case OP_SHR_LONG_2ADDR:
    case OP_USHR_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_CONST_4:
        vA = currentMIR->dalvikInsn.vA;
        tmp_s4 = currentMIR->dalvikInsn.vB;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = tmp_s4;

        success = setVRToConst(vA, OpndSize_32, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %d", vA, tmp_s4);
#endif
        return 1;
    case OP_CONST_16:
        BBBB = currentMIR->dalvikInsn.vB;
        vA = currentMIR->dalvikInsn.vA;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = (s2)BBBB;

        success = setVRToConst(vA, OpndSize_32, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
        return 1;
    case OP_CONST:
        vA = currentMIR->dalvikInsn.vA;
        tmp_u4 = currentMIR->dalvikInsn.vB;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = (s4)tmp_u4;

        success = setVRToConst(vA, OpndSize_32, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
        return 1;
    case OP_CONST_HIGH16:
        vA = currentMIR->dalvikInsn.vA;
        tmp_u2 = currentMIR->dalvikInsn.vB;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = ((s4)tmp_u2)<<16;

        success = setVRToConst(vA, OpndSize_32, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %d", vA, infoArray[entry].value[0]);
#endif
        return 1;
    case OP_CONST_WIDE_16:
        vA = currentMIR->dalvikInsn.vA;
        tmp_u2 = currentMIR->dalvikInsn.vB;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = (s2)tmp_u2;

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA, infoArray[entry].value[0]);
#endif

        entry = findVirtualRegInTable(vA+1, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[1] = ((s2)tmp_u2)>>31;

        success = setVRToConst(vA, OpndSize_64, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA+1, infoArray[entry].value[0]);
#endif
        return 1;
    case OP_CONST_WIDE_32:
        vA = currentMIR->dalvikInsn.vA;
        tmp_u4 = currentMIR->dalvikInsn.vB;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = (s4)tmp_u4;
        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA, infoArray[entry].value[0]);
#endif

        entry = findVirtualRegInTable(vA+1, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[1] = ((s4)tmp_u4)>>31;

        success = setVRToConst(vA, OpndSize_64, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA+1, infoArray[entry].value[0]);
#endif
        return 1;
    case OP_CONST_WIDE:
        vA = currentMIR->dalvikInsn.vA;
        tmp_u4 = (s4)currentMIR->dalvikInsn.vB_wide;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = (s4)tmp_u4;

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA, infoArray[entry].value[0]);
#endif

        tmp_u4 = (s4)(currentMIR->dalvikInsn.vB_wide >> 32);
        entry = findVirtualRegInTable(vA+1, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[1] = (s4)tmp_u4;

        success = setVRToConst(vA, OpndSize_64, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA+1, infoArray[entry].value[0]);
#endif
        return 1;
    case OP_CONST_WIDE_HIGH16:
        vA = currentMIR->dalvikInsn.vA;
        tmp_u2 = currentMIR->dalvikInsn.vB;
        entry = findVirtualRegInTable(vA, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[0] = 0;

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA, infoArray[entry].value[0]);
#endif

        entry = findVirtualRegInTable(vA+1, LowOpndRegType_gp);
        if (entry < 0)
            return -1;
        tmpValue[1] = ((s4)tmp_u2)<<16;

        success = setVRToConst(vA, OpndSize_64, tmpValue);
        if (success == false)
        {
            //setVRToConst set an error message when it failed so we just pass along the failure information
            return -1;
        }

        compileTable[entry].refCount--;
#ifdef DEBUG_CONST
        ALOGD("getConstInfo: set VR %d to %x", vA+1, infoArray[entry].value[0]);
#endif
        return 1;
#ifdef SUPPORT_HLO
    case OP_X_AGET_QUICK:
    case OP_X_AGET_OBJECT_QUICK:
    case OP_X_AGET_BOOLEAN_QUICK:
    case OP_X_AGET_BYTE_QUICK:
    case OP_X_AGET_CHAR_QUICK:
    case OP_X_AGET_SHORT_QUICK:
        vA = FETCH(1) & 0xff;
        addConstKill(vA);
        return 0;
    case OP_X_AGET_WIDE_QUICK:
        vA = FETCH(1) & 0xff;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
    case OP_X_DEREF_GET:
    case OP_X_DEREF_GET_OBJECT:
    case OP_X_DEREF_GET_BOOLEAN:
    case OP_X_DEREF_GET_BYTE:
    case OP_X_DEREF_GET_CHAR:
    case OP_X_DEREF_GET_SHORT:
        vA = FETCH(1) & 0xff;
        addConstKill(vA);
        return 0;
    case OP_X_DEREF_GET_WIDE:
        vA = FETCH(1) & 0xff;
        addConstKill(vA);
        addConstKill(vA+1);
        return 0;
#endif
    default:
        // Bytecode does not generate a const
        break;
    }
    return 0;
}

/**
 * @brief Updates infoArray with virtual registers accessed when lowering the bytecode.
 * @param infoArray Must be an array of size MAX_REG_PER_BYTECODE. This is updated by function.
 * @param currentMIR The MIR to examine.
 * @param updateBBConstraints should we update the BB constraints?
 * @return Returns the number of registers for the bytecode. Returns -1 in case of error.
 */
int getVirtualRegInfo (VirtualRegInfo* infoArray, const MIR * currentMIR, bool updateBBConstraints)
{
    u2 inst_op = currentMIR->dalvikInsn.opcode;
    int vA = 0, vB = 0, vref, vindex;
    int v1, v2, vD, vE, vF;
    u2 length, count;
    int kk, num, num_entry;
    s2 tmp_s2;
    int num_regs_per_bytecode = 0;
    //update infoArray[xx].allocConstraints
    for(num = 0; num < MAX_REG_PER_BYTECODE; num++) {
        for(kk = 0; kk < 8; kk++) {
            infoArray[num].allocConstraints[kk].physicalReg = (PhysicalReg)kk;
            infoArray[num].allocConstraints[kk].count = 0;
        }
    }

    //A bytecode with the inlined flag is treated as a nop so therefore simply return
    //that we have 0 regs for this bytecode
    if (currentMIR->OptimizationFlags & MIR_INLINED)
    {
        return 0;
    }

    // For bytecode optimized away, no need to update virtual register usage
    if (currentMIR->OptimizationFlags & MIR_OPTIMIZED_AWAY)
    {
        return 0;
    }

    bool isExtended = false;

    // Check if we need to handle an extended MIR
    if (currentMIR->dalvikInsn.opcode >= static_cast<Opcode> (kMirOpFirst)) {
        //We have an extended MIR
        isExtended = true;

        switch (static_cast<ExtendedMIROpcode>(currentMIR->dalvikInsn.opcode)) {
            case kMirOpPhi:
                num_regs_per_bytecode = 0;
                break;
            case kMirOpRegisterize:
                infoArray[0].regNum = currentMIR->dalvikInsn.vA;
                infoArray[0].refCount = 2;

                //The access type is use and then def because we use the VR when loading it into temporary
                //and then we alias virtual register to that temporary thus "defining" it.
                infoArray[0].accessType = REGACCESS_UD;

                //Decide the type depending on the register class
                switch (static_cast<RegisterClass> (currentMIR->dalvikInsn.vB))
                {
                    case kCoreReg:
                        infoArray[0].physicalType = LowOpndRegType_gp;
                        break;
                    case kSFPReg:
                        infoArray[0].physicalType = LowOpndRegType_ss;
                        break;
                    case kDFPReg:
                        infoArray[0].physicalType = LowOpndRegType_xmm;
                        break;
                    default:
                        ALOGI("JIT_INFO: kMirOpRegisterize does not support regClass %d", currentMIR->dalvikInsn.vB);
                        SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
                        break;
                }
                num_regs_per_bytecode = 1;
                break;
            case kMirOpCheckInlinePrediction:
                //vC holds the register which represents the "this" reference
                infoArray[0].regNum = currentMIR->dalvikInsn.vC;
                infoArray[0].refCount = 1;
                infoArray[0].accessType = REGACCESS_U;
                infoArray[0].physicalType = LowOpndRegType_gp;

                num_regs_per_bytecode = 1;
                break;
            case kMirOpConst128b:
            case kMirOpMove128b:
            case kMirOpPackedMultiply:
            case kMirOpPackedAddition:
            case kMirOpPackedSubtract:
            case kMirOpPackedXor:
            case kMirOpPackedOr:
            case kMirOpPackedAnd:
            case kMirOpPackedShiftLeft:
            case kMirOpPackedSignedShiftRight:
            case kMirOpPackedUnsignedShiftRight:
                //No virtual registers are being used
                num_regs_per_bytecode = 0;
                break;
            case kMirOpPackedAddReduce:
                //One virtual register defined to store final reduction
                infoArray[0].regNum = currentMIR->dalvikInsn.vA;
                infoArray[0].refCount = 2;
                infoArray[0].accessType = REGACCESS_UD;
                infoArray[0].physicalType = LowOpndRegType_gp;

                num_regs_per_bytecode = 1;
                break;
            case kMirOpPackedReduce:
                //One virtual register defined to store final reduction
                infoArray[0].regNum = currentMIR->dalvikInsn.vA;
                infoArray[0].refCount = 1;
                infoArray[0].accessType = REGACCESS_D;
                infoArray[0].physicalType = LowOpndRegType_gp;

                num_regs_per_bytecode = 1;
                break;
            case kMirOpPackedSet:
                //One virtual register used to load into 128-bit register
                infoArray[0].regNum = currentMIR->dalvikInsn.vB;
                infoArray[0].refCount = 1;
                infoArray[0].accessType = REGACCESS_U;
                infoArray[0].physicalType = LowOpndRegType_gp;

                num_regs_per_bytecode = 1;
                break;
            case kMirOpNullCheck:
                if ((currentMIR->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
                {
                    //We only reference register if we need to do a null check
                    infoArray[0].regNum = currentMIR->dalvikInsn.vA;
                    infoArray[0].refCount = 1;
                    infoArray[0].accessType = REGACCESS_U;
                    infoArray[0].physicalType = LowOpndRegType_gp;

                    num_regs_per_bytecode = 1;
                }
                else
                {
                    num_regs_per_bytecode = 0;
                }

                break;
            case kMirOpCheckStackOverflow:
                num_regs_per_bytecode = 0;
                break;
            default:
            {
                char *decoded = dvmCompilerGetDalvikDisassembly(&currentMIR->dalvikInsn, NULL);
                ALOGI("JIT_INFO: Extended MIR not supported in getVirtualRegInfo: %s", decoded);
                SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
                return -1;
            }
        }
    }

    //If we have an extended bytecode, we have nothing else to do
    if (isExtended == true)
    {
        return num_regs_per_bytecode;
    }

    switch (inst_op) {
    case OP_NOP:
        break;
    case OP_MOVE:
    case OP_MOVE_OBJECT:
    case OP_MOVE_FROM16:
    case OP_MOVE_OBJECT_FROM16:
    case OP_MOVE_16:
    case OP_MOVE_OBJECT_16:
        if(inst_op == OP_MOVE || inst_op == OP_MOVE_OBJECT) {
            vA = currentMIR->dalvikInsn.vA;
            vB = currentMIR->dalvikInsn.vB;
        }
        else if(inst_op == OP_MOVE_FROM16 || inst_op == OP_MOVE_OBJECT_FROM16) {
            vA = currentMIR->dalvikInsn.vA;
            vB = currentMIR->dalvikInsn.vB;
        }
        else if(inst_op == OP_MOVE_16 || inst_op == OP_MOVE_OBJECT_16) {
            vA = currentMIR->dalvikInsn.vA;
            vB = currentMIR->dalvikInsn.vB;
        }
        infoArray[1].regNum = vA; //dst
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_MOVE_WIDE:
    case OP_MOVE_WIDE_FROM16:
    case OP_MOVE_WIDE_16:
        if(inst_op == OP_MOVE_WIDE) {
            vA = currentMIR->dalvikInsn.vA;
            vB = currentMIR->dalvikInsn.vB;
        }
        else if(inst_op == OP_MOVE_WIDE_FROM16) {
            vA = currentMIR->dalvikInsn.vA;
            vB = currentMIR->dalvikInsn.vB;
        }
        else if(inst_op == OP_MOVE_WIDE_16) {
            vA = currentMIR->dalvikInsn.vA;
            vB = currentMIR->dalvikInsn.vB;
        }
        infoArray[1].regNum = vA; //dst
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[0].regNum = vB; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 2;
        break;
    case OP_MOVE_RESULT: //access memory
    case OP_MOVE_RESULT_OBJECT:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_MOVE_RESULT_WIDE: //note: 2 destinations
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 1;
        break;
    case OP_MOVE_EXCEPTION: //access memory
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_RETURN_VOID:
    case OP_RETURN_VOID_BARRIER:
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 0;
        break;
    case OP_RETURN:
    case OP_RETURN_OBJECT:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_RETURN_WIDE:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 1;
        break;
    case OP_CONST_4:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_CONST_16:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_CONST:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_CONST_HIGH16:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_CONST_WIDE_16:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_CONST_WIDE_32:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_CONST_WIDE:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_CONST_WIDE_HIGH16:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_CONST_STRING:
    case OP_CONST_STRING_JUMBO:
    case OP_CONST_CLASS:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[0].allocConstraints[PhysicalReg_EAX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_MONITOR_ENTER:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_MONITOR_EXIT:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX); //eax is used as return value from c function
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_CHECK_CAST:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_INSTANCE_OF:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = vB; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA; //dst
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 2;
        break;
    case OP_ARRAY_LENGTH:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = vB; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA; //dst
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        //%edx is used in this bytecode, update currentBB->allocConstraints
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 2;
        break;
    case OP_NEW_INSTANCE:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //dst
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_D;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[0].allocConstraints[PhysicalReg_EAX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_NEW_ARRAY:
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB; //length
        infoArray[0].regNum = vB; //src
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA; //dst
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[1].allocConstraints[PhysicalReg_EAX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 2;
        break;
    case OP_FILLED_NEW_ARRAY: {//update return value
        //can use up to 5 registers to fill the content of array
        length = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.arg[0];
        v2 = currentMIR->dalvikInsn.arg[1];
        int v3 = currentMIR->dalvikInsn.arg[2];
        int v4 = currentMIR->dalvikInsn.arg[3];
        int v5 = currentMIR->dalvikInsn.arg[4];
        if(length >= 1) {
            infoArray[0].regNum = v1; //src
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_gp;
        }
        if(length >= 2) {
            infoArray[1].regNum = v2; //src
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_U;
            infoArray[1].physicalType = LowOpndRegType_gp;
        }
        if(length >= 3) {
            infoArray[2].regNum = v3; //src
            infoArray[2].refCount = 1;
            infoArray[2].accessType = REGACCESS_U;
            infoArray[2].physicalType = LowOpndRegType_gp;
        }
        if(length >= 4) {
            infoArray[3].regNum = v4; //src
            infoArray[3].refCount = 1;
            infoArray[3].accessType = REGACCESS_U;
            infoArray[3].physicalType = LowOpndRegType_gp;
        }
        if(length >= 5) {
            infoArray[4].regNum = v5; //src
            infoArray[4].refCount = 1;
            infoArray[4].accessType = REGACCESS_U;
            infoArray[4].physicalType = LowOpndRegType_gp;
        }
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = length;
        break;
    }
    case OP_FILLED_NEW_ARRAY_RANGE: {//use "length" virtual registers
        length = currentMIR->dalvikInsn.vA;
        u4 vC = currentMIR->dalvikInsn.vC;
        for(kk = 0; kk < length; kk++) {
            infoArray[kk].regNum = vC+kk; //src
            infoArray[kk].refCount = 1;
            infoArray[kk].accessType = REGACCESS_U;
            infoArray[kk].physicalType = LowOpndRegType_gp;
        }
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = length;
        break;
    }
    case OP_FILL_ARRAY_DATA: //update content of array, read memory
        vA = currentMIR->dalvikInsn.vA; //use virtual register, but has side-effect, update memory
        infoArray[0].regNum = vA; //use
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_THROW: //update glue->exception
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //use
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_THROW_VERIFICATION_ERROR:
    case OP_GOTO:
    case OP_GOTO_16:
    case OP_GOTO_32:
        num_regs_per_bytecode = 0;
        break;
    case OP_PACKED_SWITCH:
    case OP_SPARSE_SWITCH:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //use
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 1;
        break;

    case OP_CMPL_FLOAT: //move 32 bits from memory to lower part of XMM register
    case OP_CMPG_FLOAT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        num_regs_per_bytecode = 1;
        infoArray[0].regNum = v1; //use ss or sd CHECK
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_ss;
        infoArray[1].regNum = v2; //use
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_ss;
        num_regs_per_bytecode = 3;
        num_entry = 2;
        infoArray[num_entry].regNum = vA; //define
        infoArray[num_entry].refCount = 3;
        infoArray[num_entry].accessType = REGACCESS_D;
        infoArray[num_entry].physicalType = LowOpndRegType_gp;
        break;
    case OP_CMPL_DOUBLE: //move 64 bits from memory to lower part of XMM register
    case OP_CMPG_DOUBLE:
    case OP_CMP_LONG: //load v1, v1+1, v2, v2+1 to gpr
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        num_regs_per_bytecode = 1;
        if(inst_op == OP_CMP_LONG) {
            infoArray[0].regNum = v1; //use
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_gp;
            infoArray[1].regNum = v1 + 1; //use
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_U;
            infoArray[1].physicalType = LowOpndRegType_gp;
            infoArray[2].regNum = v2; //use
            infoArray[2].refCount = 1;
            infoArray[2].accessType = REGACCESS_U;
            infoArray[2].physicalType = LowOpndRegType_gp;
            infoArray[3].regNum = v2 + 1; //use
            infoArray[3].refCount = 1;
            infoArray[3].accessType = REGACCESS_U;
            infoArray[3].physicalType = LowOpndRegType_gp;
            num_regs_per_bytecode = 5;
            num_entry = 4;
            infoArray[num_entry].regNum = vA; //define
            infoArray[num_entry].refCount = 5;
            infoArray[num_entry].accessType = REGACCESS_D;
            infoArray[num_entry].physicalType = LowOpndRegType_gp;
        }
        else {
            infoArray[0].regNum = v1; //use ss or sd CHECK
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_xmm;
            infoArray[1].regNum = v2; //use
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_U;
            infoArray[1].physicalType = LowOpndRegType_xmm;
            num_regs_per_bytecode = 3;
            num_entry = 2;
            infoArray[num_entry].regNum = vA; //define
            infoArray[num_entry].refCount = 3;
            infoArray[num_entry].accessType = REGACCESS_D;
            infoArray[num_entry].physicalType = LowOpndRegType_gp;
        }
        break;
    case OP_IF_EQ:
    case OP_IF_NE:
    case OP_IF_LT:
    case OP_IF_GE:
    case OP_IF_GT:
    case OP_IF_LE:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = vA; //use
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vB;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_IF_EQZ:
    case OP_IF_NEZ:
    case OP_IF_LTZ:
    case OP_IF_GEZ:
    case OP_IF_GTZ:
    case OP_IF_LEZ:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = vA; //use
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 1;
        break;
    case OP_AGET:
    case OP_AGET_WIDE:
    case OP_AGET_OBJECT:
    case OP_AGET_BOOLEAN: //movez 8
    case OP_AGET_BYTE: //moves 8
    case OP_AGET_CHAR: //movez 16
    case OP_AGET_SHORT: //moves 16
        vA = currentMIR->dalvikInsn.vA;
        vref = currentMIR->dalvikInsn.vB;
        vindex = currentMIR->dalvikInsn.vC;
        if(inst_op == OP_AGET_WIDE) {
            infoArray[2].regNum = vA;
            infoArray[2].refCount = 1;
            infoArray[2].accessType = REGACCESS_D;
            infoArray[2].physicalType = LowOpndRegType_xmm; //64, 128 not used in lowering
        } else {
            infoArray[2].regNum = vA;
            infoArray[2].refCount = 1;
            infoArray[2].accessType = REGACCESS_D;
            infoArray[2].physicalType = LowOpndRegType_gp;
        }
        infoArray[0].regNum = vref; //use
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vindex; //use
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 3;
        break;
    case OP_APUT:
    case OP_APUT_WIDE:
    case OP_APUT_OBJECT:
    case OP_APUT_BOOLEAN:
    case OP_APUT_BYTE:
    case OP_APUT_CHAR:
    case OP_APUT_SHORT:
        vA = currentMIR->dalvikInsn.vA;
        vref = currentMIR->dalvikInsn.vB;
        vindex = currentMIR->dalvikInsn.vC;
        if(inst_op == OP_APUT_WIDE) {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_xmm; //64, 128 not used in lowering
        } else {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_gp;
        }
        infoArray[1].regNum = vref; //use
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = vindex; //use
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        if (inst_op == OP_APUT_OBJECT && updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 3;
        break;

    case OP_IGET:
    case OP_IGET_WIDE:
    case OP_IGET_OBJECT:
    case OP_IGET_VOLATILE:
    case OP_IGET_WIDE_VOLATILE:
    case OP_IGET_OBJECT_VOLATILE:
    case OP_IGET_BOOLEAN:
    case OP_IGET_BYTE:
    case OP_IGET_CHAR:
    case OP_IGET_SHORT:
    case OP_IGET_QUICK:
    case OP_IGET_WIDE_QUICK:
    case OP_IGET_OBJECT_QUICK:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = vB; //object instance
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_IGET_WIDE || inst_op == OP_IGET_WIDE_QUICK) {
            infoArray[1].regNum = vA;
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_D;
            infoArray[1].physicalType = LowOpndRegType_xmm; //64
        } else if(inst_op == OP_IGET_WIDE_VOLATILE) {
            infoArray[1].regNum = vA;
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_D;
            infoArray[1].physicalType = LowOpndRegType_gp;
            infoArray[2].regNum = vA+1;
            infoArray[2].refCount = 1;
            infoArray[2].accessType = REGACCESS_D;
            infoArray[2].physicalType = LowOpndRegType_gp;
            ///Update num regs per bytecode in this case
            num_regs_per_bytecode = 3;
        } else {
            infoArray[1].regNum = vA;
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_D;
            infoArray[1].physicalType = LowOpndRegType_gp;
        }
#else
        if(inst_op == OP_IGET_WIDE || inst_op == OP_IGET_WIDE_QUICK ||
           inst_op == OP_IGET_WIDE_VOLATILE) {
            infoArray[1].regNum = vA;
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_D;
            infoArray[1].physicalType = LowOpndRegType_xmm; //64
        } else {
            infoArray[1].regNum = vA;
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_D;
            infoArray[1].physicalType = LowOpndRegType_gp;
        }
#endif
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        break;
    case OP_IPUT:
    case OP_IPUT_WIDE:
    case OP_IPUT_OBJECT:
    case OP_IPUT_VOLATILE:
    case OP_IPUT_WIDE_VOLATILE:
    case OP_IPUT_OBJECT_VOLATILE:
    case OP_IPUT_BOOLEAN:
    case OP_IPUT_BYTE:
    case OP_IPUT_CHAR:
    case OP_IPUT_SHORT:
    case OP_IPUT_QUICK:
    case OP_IPUT_WIDE_QUICK:
    case OP_IPUT_OBJECT_QUICK:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        if(inst_op == OP_IPUT_WIDE || inst_op == OP_IPUT_WIDE_QUICK || inst_op == OP_IPUT_WIDE_VOLATILE) {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_xmm; //64
        } else {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_gp;
            if(inst_op == OP_IPUT_OBJECT_VOLATILE) {
                infoArray[0].refCount++;
            }
        }
        infoArray[1].regNum = vB; //object instance
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 2;
        break;
    case OP_SGET:
    case OP_SGET_WIDE:
    case OP_SGET_OBJECT:
    case OP_SGET_VOLATILE:
    case OP_SGET_WIDE_VOLATILE:
    case OP_SGET_OBJECT_VOLATILE:
    case OP_SGET_BOOLEAN:
    case OP_SGET_BYTE:
    case OP_SGET_CHAR:
    case OP_SGET_SHORT:
        vA = currentMIR->dalvikInsn.vA;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_SGET_WIDE) {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_D;
            infoArray[0].physicalType = LowOpndRegType_xmm; //64
        } else if(inst_op == OP_SGET_WIDE_VOLATILE) {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_D;
            infoArray[0].physicalType = LowOpndRegType_gp;
            infoArray[1].regNum = vA+1;
            infoArray[1].refCount = 1;
            infoArray[1].accessType = REGACCESS_D;
            infoArray[1].physicalType = LowOpndRegType_gp;
        } else {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_D;
            infoArray[0].physicalType = LowOpndRegType_gp;
        }
        if(inst_op == OP_SGET_WIDE_VOLATILE)
            num_regs_per_bytecode = 2;
        else
            num_regs_per_bytecode = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        break;
#else
        if(inst_op == OP_SGET_WIDE || inst_op == OP_SGET_WIDE_VOLATILE) {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_D;
            infoArray[0].physicalType = LowOpndRegType_xmm; //64
        }  else {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_D;
            infoArray[0].physicalType = LowOpndRegType_gp;
        }
        num_regs_per_bytecode = 1;
        updateCurrentBBWithConstraints(PhysicalReg_EAX);
        break;
#endif
    case OP_SPUT:
    case OP_SPUT_WIDE:
    case OP_SPUT_OBJECT:
    case OP_SPUT_VOLATILE:
    case OP_SPUT_WIDE_VOLATILE:
    case OP_SPUT_OBJECT_VOLATILE:
    case OP_SPUT_BOOLEAN:
    case OP_SPUT_BYTE:
    case OP_SPUT_CHAR:
    case OP_SPUT_SHORT:
        vA = currentMIR->dalvikInsn.vA;
        if(inst_op == OP_SPUT_WIDE || inst_op == OP_SPUT_WIDE_VOLATILE) {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_xmm; //64
        } else {
            infoArray[0].regNum = vA;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            infoArray[0].physicalType = LowOpndRegType_gp;
            if(inst_op == OP_SPUT_OBJECT_VOLATILE) {
                infoArray[0].refCount++;
            }
        }
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 1;
        break;
    case OP_INVOKE_VIRTUAL:
    case OP_INVOKE_SUPER:
    case OP_INVOKE_DIRECT:
    case OP_INVOKE_STATIC:
    case OP_INVOKE_INTERFACE:
    case OP_INVOKE_VIRTUAL_QUICK:
    case OP_INVOKE_SUPER_QUICK:
        vD = currentMIR->dalvikInsn.arg[0]; //object for virtual,direct & interface
        count = currentMIR->dalvikInsn.vA;
        vE = currentMIR->dalvikInsn.arg[1];
        vF = currentMIR->dalvikInsn.arg[2];
        vA = currentMIR->dalvikInsn.arg[4]; //5th argument

        for (int vrNum = 0; vrNum < count; vrNum++) {
            if (vrNum == 0) {
                infoArray[num_regs_per_bytecode].regNum = currentMIR->dalvikInsn.arg[vrNum];
                if (inst_op == OP_INVOKE_VIRTUAL_QUICK || inst_op == OP_INVOKE_SUPER_QUICK
                    || inst_op == OP_INVOKE_VIRTUAL || inst_op == OP_INVOKE_DIRECT
                    || inst_op == OP_INVOKE_INTERFACE){
                        infoArray[num_regs_per_bytecode].refCount = 2;
                    } else {
                        infoArray[num_regs_per_bytecode].refCount = 1;
                    }
                    infoArray[num_regs_per_bytecode].accessType = REGACCESS_U;
                    infoArray[num_regs_per_bytecode].physicalType = LowOpndRegType_gp;
                    num_regs_per_bytecode++;
            } else if ((vrNum + 1 < count) && // Use XMM registers if adjacent VRs are accessed
                       (currentMIR->dalvikInsn.arg[vrNum] + 1 == currentMIR->dalvikInsn.arg[vrNum + 1])) {
                infoArray[num_regs_per_bytecode].regNum = currentMIR->dalvikInsn.arg[vrNum];
                infoArray[num_regs_per_bytecode].refCount = 1;
                infoArray[num_regs_per_bytecode].accessType = REGACCESS_U;
                infoArray[num_regs_per_bytecode].physicalType = LowOpndRegType_xmm;
                // We can now skip the vrNum+1 which represents the rest of the wide VR
                vrNum++;
                num_regs_per_bytecode++;
            } else { // Use gp registers
                infoArray[num_regs_per_bytecode].regNum = currentMIR->dalvikInsn.arg[vrNum];
                infoArray[num_regs_per_bytecode].refCount = 1;
                infoArray[num_regs_per_bytecode].accessType = REGACCESS_U;
                infoArray[num_regs_per_bytecode].physicalType = LowOpndRegType_gp;
                num_regs_per_bytecode++;
            }
        }

        if (updateBBConstraints == true)
        {
            if (inst_op != OP_INVOKE_VIRTUAL_QUICK && inst_op != OP_INVOKE_SUPER_QUICK)
            {
                updateCurrentBBWithConstraints (PhysicalReg_EAX);
            }
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        break;
    case OP_INVOKE_VIRTUAL_RANGE:
    case OP_INVOKE_SUPER_RANGE:
    case OP_INVOKE_DIRECT_RANGE:
    case OP_INVOKE_STATIC_RANGE:
    case OP_INVOKE_INTERFACE_RANGE:
    case OP_INVOKE_VIRTUAL_QUICK_RANGE:
    case OP_INVOKE_SUPER_QUICK_RANGE:
        vD = currentMIR->dalvikInsn.vC;
        count = currentMIR->dalvikInsn.vA;
        if(count == 0) {
            if(inst_op == OP_INVOKE_VIRTUAL_RANGE || inst_op == OP_INVOKE_DIRECT_RANGE ||
               inst_op == OP_INVOKE_INTERFACE_RANGE || inst_op == OP_INVOKE_VIRTUAL_QUICK_RANGE ||
               inst_op == OP_INVOKE_SUPER_QUICK_RANGE) {
                infoArray[0].regNum = vD;
                infoArray[0].refCount = 1;
                infoArray[0].accessType = REGACCESS_U;
                infoArray[0].physicalType = LowOpndRegType_gp;
            }
        }
        if(count > 0) { //same for count > 10
            for(kk = 0; kk < count; kk++) {
                infoArray[kk].regNum = vD+kk; //src
                if(kk == 0 && (inst_op == OP_INVOKE_VIRTUAL_QUICK_RANGE ||
                               inst_op == OP_INVOKE_SUPER_QUICK_RANGE))
                    infoArray[kk].refCount = 2;
                else if(kk == 0 && (inst_op == OP_INVOKE_VIRTUAL_RANGE ||
                                    inst_op == OP_INVOKE_DIRECT_RANGE ||
                                    inst_op == OP_INVOKE_INTERFACE_RANGE))
                    infoArray[kk].refCount = 2;
                else
                    infoArray[kk].refCount = 1;
                infoArray[kk].accessType = REGACCESS_U;
                infoArray[kk].physicalType = LowOpndRegType_gp;
            }
        }
        if (updateBBConstraints == true)
        {
            if (inst_op != OP_INVOKE_VIRTUAL_QUICK_RANGE && inst_op != OP_INVOKE_SUPER_QUICK_RANGE)
            {
                updateCurrentBBWithConstraints (PhysicalReg_EAX);
            }
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = count;
        break;
    case OP_NEG_INT:
    case OP_NOT_INT:
    case OP_NEG_FLOAT:
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_NEG_LONG:
    case OP_NOT_LONG:
    case OP_NEG_DOUBLE:
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 2;
        break;
    case OP_INT_TO_LONG: //hard-coded registers
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp; //save from %eax
        infoArray[1].allocConstraints[PhysicalReg_EAX].count = 1;
        infoArray[2].regNum = vA+1;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[2].allocConstraints[PhysicalReg_EDX].count = 1;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[0].allocConstraints[PhysicalReg_EAX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 3;
        break;
    case OP_INT_TO_FLOAT: //32 to 32
    case OP_INT_TO_DOUBLE: //32 to 64
    case OP_LONG_TO_FLOAT: //64 to 32
    case OP_LONG_TO_DOUBLE: //64 to 64
    case OP_FLOAT_TO_DOUBLE: //32 to 64
    case OP_DOUBLE_TO_FLOAT: //64 to 32
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        if(inst_op == OP_LONG_TO_DOUBLE || inst_op == OP_FLOAT_TO_DOUBLE)
            infoArray[1].physicalType = LowOpndRegType_fs;
        else if (inst_op == OP_INT_TO_DOUBLE)
            infoArray[1].physicalType = LowOpndRegType_xmm;
        else
            infoArray[1].physicalType = LowOpndRegType_fs_s;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        if(inst_op == OP_INT_TO_FLOAT || inst_op == OP_FLOAT_TO_DOUBLE)
            infoArray[0].physicalType = LowOpndRegType_fs_s; //float
        else if (inst_op == OP_INT_TO_DOUBLE)
            infoArray[0].physicalType = LowOpndRegType_gp;
        else
            infoArray[0].physicalType = LowOpndRegType_fs;
        num_regs_per_bytecode = 2;
        break;
    case OP_LONG_TO_INT:
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_FLOAT_TO_INT:
    case OP_DOUBLE_TO_INT: //for reaching-def analysis
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 3;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_fs_s; //store_int_fp_stack_VR
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        if(inst_op == OP_DOUBLE_TO_INT)
            infoArray[0].physicalType = LowOpndRegType_fs;
        else
            infoArray[0].physicalType = LowOpndRegType_fs_s;
        num_regs_per_bytecode = 3;
        break;
    case OP_FLOAT_TO_LONG:
    case OP_DOUBLE_TO_LONG:
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 3;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_fs;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        if(inst_op == OP_DOUBLE_TO_LONG)
            infoArray[0].physicalType = LowOpndRegType_fs;
        else
            infoArray[0].physicalType = LowOpndRegType_fs_s;
        num_regs_per_bytecode = 3;
        break;
    case OP_INT_TO_BYTE:
    case OP_INT_TO_CHAR:
    case OP_INT_TO_SHORT:
        vA = currentMIR->dalvikInsn.vA; //destination
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;

    case OP_ADD_INT:
    case OP_SUB_INT:
    case OP_MUL_INT:
    case OP_AND_INT:
    case OP_OR_INT:
    case OP_XOR_INT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 3;
        break;
    case OP_DIV_INT:
    case OP_REM_INT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].allocConstraints[PhysicalReg_EAX].count = 1; //for v1
        if(inst_op == OP_REM_INT)
            infoArray[2].allocConstraints[PhysicalReg_EDX].count = 1;//vA
        else
            infoArray[2].allocConstraints[PhysicalReg_EAX].count = 1;//vA
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 3;
        break;
    case OP_SHL_INT:
    case OP_SHR_INT:
    case OP_USHR_INT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v2; // in ecx
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[1].allocConstraints[PhysicalReg_ECX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
        }
        num_regs_per_bytecode = 3;
        break;
    case OP_ADD_LONG:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v1+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = v2;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = v2+1;
        infoArray[3].refCount = 1;
        infoArray[3].accessType = REGACCESS_U;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = vA;
        infoArray[4].refCount = 1;
        infoArray[4].accessType = REGACCESS_D;
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = vA+1;
        infoArray[5].refCount = 1;
        infoArray[5].accessType = REGACCESS_D;
        infoArray[5].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 6;
        break;
    case OP_SUB_LONG:
    case OP_AND_LONG:
    case OP_OR_LONG:
    case OP_XOR_LONG:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_xmm;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 3;
        break;
    case OP_MUL_LONG: //used int
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v1+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = v2;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = v2+1;
        infoArray[3].refCount = 1;
        infoArray[3].accessType = REGACCESS_U;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = vA;
        infoArray[4].refCount = 1;
        infoArray[4].accessType = REGACCESS_D;
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = vA+1;
        infoArray[5].refCount = 1;
        infoArray[5].accessType = REGACCESS_D;
        infoArray[5].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        num_regs_per_bytecode = 6;
        break;
    case OP_DIV_LONG: //v1: xmm v2,vA:
    case OP_REM_LONG:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = v2+1;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = vA;
        infoArray[3].refCount = 1;
        infoArray[3].accessType = REGACCESS_D;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = vA+1;
        infoArray[4].refCount = 1;
        infoArray[4].accessType = REGACCESS_D;
        infoArray[4].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 5;
        break;
    case OP_SHL_LONG: //v2: 32, move_ss; v1,vA: xmm CHECK
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_ss;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 3;
        break;
    case OP_SHR_LONG: //v2: 32, move_ss; v1,vA: xmm CHECK
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_ss;
        infoArray[2].regNum = v1+1;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = vA;
        infoArray[3].refCount = 1;
        infoArray[3].accessType = REGACCESS_D;
        infoArray[3].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 4;
        break;
    case OP_USHR_LONG: //v2: move_ss; v1,vA: move_sd
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm; //sd
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_ss; //ss
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_xmm; //sd
        num_regs_per_bytecode = 3;
        break;
    case OP_ADD_FLOAT: //move_ss
    case OP_SUB_FLOAT:
    case OP_MUL_FLOAT:
    case OP_DIV_FLOAT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_ss;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_ss;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_ss;
        num_regs_per_bytecode = 3;
        break;
    case OP_REM_FLOAT: //32 bit GPR, fp_stack for output
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_fs_s;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 3;
        break;
    case OP_ADD_DOUBLE: //move_sd
    case OP_SUB_DOUBLE:
    case OP_MUL_DOUBLE:
    case OP_DIV_DOUBLE:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_xmm;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 3;
        break;
    case OP_REM_DOUBLE: //64 bit XMM, fp_stack for output
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        v2 = currentMIR->dalvikInsn.vC;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_D;
        infoArray[2].physicalType = LowOpndRegType_fs;
        infoArray[0].regNum = v1;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 3;
        break;

    case OP_ADD_INT_2ADDR:
    case OP_SUB_INT_2ADDR:
    case OP_MUL_INT_2ADDR:
    case OP_AND_INT_2ADDR:
    case OP_OR_INT_2ADDR:
    case OP_XOR_INT_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD; //use then define
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_DIV_INT_2ADDR:
    case OP_REM_INT_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 3;
        infoArray[1].accessType = REGACCESS_UD; //use then define
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].allocConstraints[PhysicalReg_EAX].count = 1; //for v1 is vA
        if(inst_op == OP_REM_INT_2ADDR)
            infoArray[1].allocConstraints[PhysicalReg_EDX].count = 1;//vA
        else
            infoArray[1].allocConstraints[PhysicalReg_EAX].count = 1;//vA
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = 2;
        break;
    case OP_SHL_INT_2ADDR:
    case OP_SHR_INT_2ADDR:
    case OP_USHR_INT_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD; //use then define
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[0].allocConstraints[PhysicalReg_ECX].count = 1; //v2
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
        }
        num_regs_per_bytecode = 2;
        break;
    case OP_ADD_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = vA+1;
        infoArray[3].refCount = 2;
        infoArray[3].accessType = REGACCESS_UD;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = v2+1;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 4;
        break;
    case OP_SUB_LONG_2ADDR:
    case OP_AND_LONG_2ADDR:
    case OP_OR_LONG_2ADDR:
    case OP_XOR_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 2;
        break;
    case OP_MUL_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        num_regs_per_bytecode = 4;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = v2+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 2;
        infoArray[2].accessType = REGACCESS_UD;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = vA+1;
        infoArray[3].refCount = 2;
        infoArray[3].accessType = REGACCESS_UD;
        infoArray[3].physicalType = LowOpndRegType_gp;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_ECX);
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
        }
        break;
    case OP_DIV_LONG_2ADDR: //vA used as xmm, then updated as gps
    case OP_REM_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        num_regs_per_bytecode = 5;
        infoArray[0].regNum = vA;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = v2;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = v2+1;
        infoArray[2].refCount = 1;
        infoArray[2].accessType = REGACCESS_U;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = vA;
        infoArray[3].refCount = 1;
        infoArray[3].accessType = REGACCESS_D;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = vA+1;
        infoArray[4].refCount = 1;
        infoArray[4].accessType = REGACCESS_D;
        infoArray[4].physicalType = LowOpndRegType_gp;
        break;
    case OP_SHL_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        num_regs_per_bytecode = 2;
        infoArray[0].regNum = v2; //ss
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_ss;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        break;
    case OP_SHR_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        num_regs_per_bytecode = 3;
        infoArray[0].regNum = v2; //ss
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_ss;
        infoArray[1].regNum = vA+1;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_U;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = vA;
        infoArray[2].refCount = 2;
        infoArray[2].accessType = REGACCESS_UD;
        infoArray[2].physicalType = LowOpndRegType_xmm;
        break;
    case OP_USHR_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        num_regs_per_bytecode = 2;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_ss; //ss CHECK
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_xmm; //sd
        break;
    case OP_ADD_FLOAT_2ADDR:
    case OP_SUB_FLOAT_2ADDR:
    case OP_MUL_FLOAT_2ADDR:
    case OP_DIV_FLOAT_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_ss;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_ss;
        num_regs_per_bytecode = 2;
        break;
    case OP_REM_FLOAT_2ADDR: //load vA as GPR, store from fs
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_gp; //CHECK
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_ADD_DOUBLE_2ADDR:
    case OP_SUB_DOUBLE_2ADDR:
    case OP_MUL_DOUBLE_2ADDR:
    case OP_DIV_DOUBLE_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 2;
        break;
    case OP_REM_DOUBLE_2ADDR: //load to xmm, store from fs
        vA = currentMIR->dalvikInsn.vA;
        v2 = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 2;
        infoArray[1].accessType = REGACCESS_UD;
        infoArray[1].physicalType = LowOpndRegType_xmm; //CHECK
        infoArray[0].regNum = v2;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        num_regs_per_bytecode = 2;
        break;

    case OP_ADD_INT_LIT16:
    case OP_RSUB_INT:
    case OP_MUL_INT_LIT16:
    case OP_AND_INT_LIT16:
    case OP_OR_INT_LIT16:
    case OP_XOR_INT_LIT16:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_DIV_INT_LIT16:
    case OP_REM_INT_LIT16:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        tmp_s2 = currentMIR->dalvikInsn.vC;
        if(tmp_s2 == 0) {
            num_regs_per_bytecode = 0;
            break;
        }
        infoArray[1].regNum = vA; //in edx for rem, in eax
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB; //in eax
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        if(inst_op == OP_DIV_INT_LIT16) {
            int power = isPowerOfTwo(tmp_s2);
            if(power >= 1) { /* divide by a power of 2 constant */
                infoArray[1].refCount = 1;
                break;
            }
        }
        if(tmp_s2 == -1)
            infoArray[1].refCount = 2;
        else
            infoArray[1].refCount = 1;
        if(inst_op == OP_REM_INT_LIT16)
            infoArray[1].allocConstraints[PhysicalReg_EDX].count = 1;
        else
            infoArray[1].allocConstraints[PhysicalReg_EAX].count = 1;
        infoArray[0].allocConstraints[PhysicalReg_EAX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        break;
    case OP_ADD_INT_LIT8:
    case OP_RSUB_INT_LIT8:
    case OP_MUL_INT_LIT8:
    case OP_AND_INT_LIT8:
    case OP_OR_INT_LIT8:
    case OP_XOR_INT_LIT8:
    case OP_SHL_INT_LIT8:
    case OP_SHR_INT_LIT8:
    case OP_USHR_INT_LIT8:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[1].regNum = vA;
        infoArray[1].refCount = 1;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        break;
    case OP_DIV_INT_LIT8:
    case OP_REM_INT_LIT8:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        tmp_s2 = currentMIR->dalvikInsn.vC;
        if(tmp_s2 == 0) {
            num_regs_per_bytecode = 0;
            break;
        }

        infoArray[1].regNum = vA;
        infoArray[1].accessType = REGACCESS_D;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[0].regNum = vB;
        infoArray[0].refCount = 1;
        infoArray[0].accessType = REGACCESS_U;
        infoArray[0].physicalType = LowOpndRegType_gp;
        num_regs_per_bytecode = 2;
        if(inst_op == OP_DIV_INT_LIT8) {
            int power = isPowerOfTwo(tmp_s2);
            if(power >= 1) { /* divide by a power of 2 constant */
                infoArray[1].refCount = 1;
                break;
            }
        }

        if(tmp_s2 == -1)
            infoArray[1].refCount = 2;
        else
            infoArray[1].refCount = 1;
        if(inst_op == OP_REM_INT_LIT8)
            infoArray[1].allocConstraints[PhysicalReg_EDX].count = 1;
        else
            infoArray[1].allocConstraints[PhysicalReg_EAX].count = 1;
        infoArray[0].allocConstraints[PhysicalReg_EAX].count = 1;
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        break;
    case OP_EXECUTE_INLINE: //update glue->retval
    case OP_EXECUTE_INLINE_RANGE:
        u4 vC;
        num = currentMIR->dalvikInsn.vA;

        // get inline method id
        u2 inlineMethodId;
        inlineMethodId = currentMIR->dalvikInsn.vB;
        if(inst_op == OP_EXECUTE_INLINE) {
            // Note that vC, vD, vE, and vF might have bad values
            // depending on count. The variable "num" should be
            // checked before using any of these.
            vC = currentMIR->dalvikInsn.arg[0];
            vD = currentMIR->dalvikInsn.arg[1];
            vE = currentMIR->dalvikInsn.arg[2];
            vF = currentMIR->dalvikInsn.arg[3];
        } else {
            vC = currentMIR->dalvikInsn.vC;
            vD = vC + 1;
            vE = vC + 2;
            vF = vC + 3;
        }
        if(num >= 1) {
            infoArray[0].regNum = vC;
            infoArray[0].refCount = 1;
            infoArray[0].accessType = REGACCESS_U;
            if (inlineMethodId == INLINE_MATH_ABS_DOUBLE) {
                infoArray[0].physicalType = LowOpndRegType_xmm;
                MIR* mirNext = currentMIR->next;
                if (mirNext != 0 && mirNext->dalvikInsn.opcode == OP_MOVE_RESULT_WIDE)
                {
                    infoArray[1].regNum = mirNext->dalvikInsn.vA;
                    infoArray[1].refCount = 1;
                    infoArray[1].accessType = REGACCESS_D;
                    infoArray[1].physicalType = LowOpndRegType_xmm;
                    num_regs_per_bytecode = 2;
                    break;
                }
            }
            else {
                infoArray[0].physicalType = LowOpndRegType_gp;
            }
        }
        if(num >= 2) {
            if (inlineMethodId != INLINE_MATH_ABS_DOUBLE) {
                infoArray[1].regNum = vD;
                infoArray[1].refCount = 1;
                infoArray[1].accessType = REGACCESS_U;
                infoArray[1].physicalType = LowOpndRegType_gp;
            }
            else {
                num_regs_per_bytecode = 1;
                break;
            }
        }
        if(num >= 3) {
            infoArray[2].regNum = vE;
            infoArray[2].refCount = 1;
            infoArray[2].accessType = REGACCESS_U;
            infoArray[2].physicalType = LowOpndRegType_gp;
        }
        if(num >= 4) {
            infoArray[3].regNum = vF;
            infoArray[3].refCount = 1;
            infoArray[3].accessType = REGACCESS_U;
            infoArray[3].physicalType = LowOpndRegType_gp;
        }
        if (updateBBConstraints == true)
        {
            updateCurrentBBWithConstraints (PhysicalReg_EAX);
            updateCurrentBBWithConstraints (PhysicalReg_EDX);
        }
        num_regs_per_bytecode = num;
        break;
#if FIXME
    case OP_INVOKE_OBJECT_INIT_RANGE:
        codeSize = 3;
        num_regs_per_bytecode = 0;
        break;
#endif
    default:
        ALOGI("JIT_INFO: JIT does not support bytecode 0x%hx when updating VR accesses", currentMIR->dalvikInsn.opcode);
        SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
        assert(false && "All opcodes should be supported.");
        break;
    }
    return num_regs_per_bytecode;
}

/**
 * @brief Updates infoArray(TempRegInfo) with temporaries accessed by INVOKE_NO_RANGE type
 * @details Allocates both XMM and gp registers for INVOKE_(VIRTUAL,DIRECT,STATIC,INTERFACE,SUPER)
 * @param infoArray the array to be filled
 * @param startIndex the start index of infoArray
 * @param currentMIR the instruction we are looking at
 * @return j the new index of infoArray
 */
int updateInvokeNoRange(TempRegInfo* infoArray, int startIndex, const MIR * currentMIR) {
    int j = startIndex;
    int count = currentMIR->dalvikInsn.vA;// Max value is 5 (#ofArguments)

    // Use XMM registers to read and store max of 5 arguments
    infoArray[j].regNum = 22;
    infoArray[j].refCount = 4; //DUDU Read & Store a pair of VRs. Max refCount is 4, since max #of VR pairs is 2
    infoArray[j].physicalType = LowOpndRegType_xmm;
    j++;

    // Use gp registers when 64 bit move is not possible. Upto 5 gp reg may be needed.
    if(count == 5) {
        infoArray[j].regNum = 27;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 4) {
        infoArray[j].regNum = 26;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 3) {
        infoArray[j].regNum = 25;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 2) {
        infoArray[j].regNum = 24;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 1) {
        infoArray[j].regNum = 23;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    return j;
}
//! Updates infoArray(TempRegInfo) with temporaries accessed by INVOKE_RANGE

//! LOOP_COUNT is used to indicate a variable is live through a loop
int updateInvokeRange(TempRegInfo* infoArray, int startIndex, const MIR * currentMIR) {
    int j = startIndex;
    int count = currentMIR->dalvikInsn.vA;
    infoArray[j].regNum = 21;
    if(count <= 10) {
        infoArray[j].refCount = 1+count; //DU
    } else {
        infoArray[j].refCount = 2+3*LOOP_COUNT;
    }
    infoArray[j].physicalType = LowOpndRegType_gp;
    j++;
    if(count >= 1 && count <= 10) {
        infoArray[j].regNum = 22;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 2 && count <= 10) {
        infoArray[j].regNum = 23;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 3 && count <= 10) {
        infoArray[j].regNum = 24;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 4 && count <= 10) {
        infoArray[j].regNum = 25;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 5 && count <= 10) {
        infoArray[j].regNum = 26;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 6 && count <= 10) {
        infoArray[j].regNum = 27;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 7 && count <= 10) {
        infoArray[j].regNum = 28;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 8 && count <= 10) {
        infoArray[j].regNum = 29;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count >= 9 && count <= 10) {
        infoArray[j].regNum = 30;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count == 10) {
        infoArray[j].regNum = 31;
        infoArray[j].refCount = 2; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    if(count > 10) {
        //NOTE: inside a loop, LOOP_COUNT can't be 1
        //      if LOOP_COUNT is 1, it is likely that a logical register is freed inside the loop
        //         and the next iteration will have incorrect result
        infoArray[j].regNum = 12;
        infoArray[j].refCount = 1+3*LOOP_COUNT; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
        infoArray[j].regNum = 13;
        infoArray[j].refCount = 1+LOOP_COUNT; //DU
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
        infoArray[j].regNum = 14;
        //MUST be 2, otherwise, transferToState will think its state was in memory
        infoArray[j].refCount = 2; //DU local
        infoArray[j].physicalType = LowOpndRegType_gp;
        j++;
    }
    return j;
}

/* update temporaries used by predicted INVOKE_VIRTUAL & INVOKE_INTERFACE */
int updateGenPrediction(TempRegInfo* infoArray, bool isInterface) {
    infoArray[0].regNum = 40;
    infoArray[0].physicalType = LowOpndRegType_gp;
    infoArray[1].regNum = 41;
    infoArray[1].physicalType = LowOpndRegType_gp;
    infoArray[2].regNum = 32;
    infoArray[2].refCount = 2;
    infoArray[2].physicalType = LowOpndRegType_gp;

    if(isInterface) {
        infoArray[0].refCount = 2+2;
        infoArray[1].refCount = 3+2-1; //for temp41, -1 for gingerbread
        infoArray[3].regNum = 33;
        infoArray[3].refCount = 4+1;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = PhysicalReg_EAX;
        infoArray[4].refCount = 5;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[5].regNum = PhysicalReg_ECX;
        infoArray[5].refCount = 1+1+2; //used in ArgsDone (twice)
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = 10;
        infoArray[6].refCount = 2;
        infoArray[6].physicalType = LowOpndRegType_scratch;
        infoArray[7].regNum = 9;
        infoArray[7].refCount = 2;
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = 8;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_scratch;
        infoArray[9].regNum = PhysicalReg_EDX; //space holder
        infoArray[9].refCount = 1;
        infoArray[9].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[10].regNum = 43;
        infoArray[10].refCount = 3;
        infoArray[10].physicalType = LowOpndRegType_gp;
        infoArray[11].regNum = 44;
        infoArray[11].refCount = 3;
        infoArray[11].physicalType = LowOpndRegType_gp;
        infoArray[12].regNum = 45;
        infoArray[12].refCount = 2;
        infoArray[12].physicalType = LowOpndRegType_gp;
        infoArray[13].regNum = 7;
        infoArray[13].refCount = 4;
        infoArray[13].physicalType = LowOpndRegType_scratch;
        return 14;
    } else { //virtual or virtual_quick
        infoArray[0].refCount = 2+2;
        infoArray[1].refCount = 3+2-2; //for temp41, -2 for gingerbread
        infoArray[2].refCount++; //for temp32 gingerbread
        infoArray[3].regNum = 33;
        infoArray[3].refCount = 4+1;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 34;
        infoArray[4].refCount = 2;
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = PhysicalReg_EAX;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_ECX;
        infoArray[6].refCount = 1+3+2;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[7].regNum = 10;
        infoArray[7].refCount = 2;
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_EDX; //space holder
        infoArray[8].refCount = 1;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[9].regNum = 43;
        infoArray[9].refCount = 3;
        infoArray[9].physicalType = LowOpndRegType_gp;
        infoArray[10].regNum = 44;
        infoArray[10].refCount = 3;
        infoArray[10].physicalType = LowOpndRegType_gp;
        infoArray[11].regNum = 7;
        infoArray[11].refCount = 4;
        infoArray[11].physicalType = LowOpndRegType_scratch;
        return 12;
    }
}

int updateMarkCard(TempRegInfo* infoArray, int j1/*valReg*/,
                    int j2/*tgtAddrReg*/, int j3/*scratchReg*/) {
    infoArray[j3].regNum = 11;
    infoArray[j3].physicalType = LowOpndRegType_gp;
    infoArray[j3].refCount = 3;
    infoArray[j3].is8Bit = true;
    infoArray[j1].refCount++;
#ifdef WITH_CONDMARK
    infoArray[j2].refCount += 3;
#else
    infoArray[j2].refCount += 2;
#endif
    infoArray[j3+1].regNum = 6;
    infoArray[j3+1].physicalType = LowOpndRegType_scratch;
#ifdef WITH_CONDMARK
    infoArray[j3+1].refCount = 3;
#else
    infoArray[j3+1].refCount = 2;
#endif
    return j3+2;
}

int updateMarkCard_notNull(TempRegInfo* infoArray,
                           int j2/*tgtAddrReg*/, int j3/*scratchReg*/) {
    infoArray[j3].regNum = 11;
    infoArray[j3].physicalType = LowOpndRegType_gp;
    infoArray[j3].refCount = 3;
    infoArray[j3].is8Bit = true;
#ifdef WITH_CONDMARK
    infoArray[j2].refCount += 3;
#else
    infoArray[j2].refCount += 2;
#endif
    infoArray[j3+1].regNum = 2;
#ifdef WITH_CONDMARK
    infoArray[j3+1].refCount = 3; //DU
#else
    infoArray[j3+1].refCount = 2; //DU
#endif
    infoArray[j3+1].physicalType = LowOpndRegType_scratch;
    return j3+2;
}

int iget_obj_inst = -1;
//! This function updates infoArray with temporaries accessed when lowering the bytecode

//! returns the number of temporaries
int getTempRegInfo(TempRegInfo* infoArray, const MIR * currentMIR, const u2* dalvikPC) { //returns an array of TempRegInfo
    int k;
    int numTmps;
    for(k = 0; k < MAX_TEMP_REG_PER_BYTECODE; k++) {
        infoArray[k].linkageToVR = -1;
        infoArray[k].versionNum = 0;
        infoArray[k].shareWithVR = true;
        infoArray[k].is8Bit = false;
    }
    u2 length, num, tmp;
    int vA, vB, v1, v2;
    Opcode inst_op = currentMIR->dalvikInsn.opcode;
    s2 tmp_s2;
    int tmpvalue, isConst;

    /* A bytecode with the MIR_INLINED op will be treated as
     * no-op during codegen */
    if (currentMIR->OptimizationFlags & MIR_INLINED)
        return 0; // No temporaries accessed

    // For bytecode which is optimized away, no need to update temporaries infoArray
    if (currentMIR->OptimizationFlags & MIR_OPTIMIZED_AWAY)
    {
        return 0;
    }

    // Check if we need to handle an extended MIR
    if (currentMIR->dalvikInsn.opcode >= static_cast<Opcode> (kMirOpFirst)) {
        switch (static_cast<ExtendedMIROpcode>(currentMIR->dalvikInsn.opcode)) {
            case kMirOpPhi:
                return 0;
            case kMirOpRegisterize:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2; //UD

                //Decide the type depending on the register class
                switch (static_cast<RegisterClass> (currentMIR->dalvikInsn.vB))
                {
                    case kCoreReg:
                        infoArray[0].physicalType = LowOpndRegType_gp;
                        break;
                    case kSFPReg:
                    case kDFPReg:
                        //Temps don't have concept of SS type so the physical type of both
                        //Single FP and Double FP must be xmm.
                        infoArray[0].physicalType = LowOpndRegType_xmm;
                        break;
                    default:
                        ALOGI("JIT_INFO: kMirOpRegisterize does not support regClass %d", currentMIR->dalvikInsn.vB);
                        SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
                        break;
                }
                return 1;
            case kMirOpCheckInlinePrediction:
            {
                unsigned int tempRegCount = 0;

                //Use temp1 to hold the "this" object reference
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 3; //DUU
                infoArray[0].physicalType = LowOpndRegType_gp;
                tempRegCount++;

                //Use temp2 to hold the object's actual class
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2; //DU
                infoArray[1].physicalType = LowOpndRegType_gp;
                tempRegCount++;

                //If we won't be generating a null check on object, then we have fewer references
                if ((currentMIR->OptimizationFlags & MIR_IGNORE_NULL_CHECK) != 0)
                {
                    //Null check requires one reference so we subtract that right now
                    infoArray[0].refCount -= 1;
                }
                else
                {
                    //When nullCheck is called it expects that it can update references to EDX.
                    //Although EDX is not explicitly used, we must add two references now in order to satisfy
                    //this dependency. Eventually when nullCheck is fixed this can be removed.
                    infoArray[2].regNum = PhysicalReg_EDX;
                    infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                    infoArray[2].refCount = 2;
                    tempRegCount++;
                }

                //Return the number of temps being used
                return tempRegCount;
            }
            case kMirOpConst128b:
            {
                const int destXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vA;

                infoArray[0].regNum = destXmm;
                infoArray[0].refCount = 1;
                infoArray[0].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                //If we are just loading zero, then we can just zero out the destination register
                if (currentMIR->dalvikInsn.arg[0] == 0 && currentMIR->dalvikInsn.arg[1] == 0
                        && currentMIR->dalvikInsn.arg[2] == 0 && currentMIR->dalvikInsn.arg[3] == 0)
                {
                    infoArray[0].refCount = 2;
                }
                else
                {
                    infoArray[0].refCount = 1;
                }

                return 1;
            }
            case kMirOpMove128b:
            case kMirOpPackedMultiply:
            case kMirOpPackedAddition:
            case kMirOpPackedSubtract:
            case kMirOpPackedXor:
            case kMirOpPackedOr:
            case kMirOpPackedAnd:
            {
                const int sourceXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vB;
                const int destXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vA;
                int numTemps = 0;

                if (sourceXmm == destXmm)
                {
                    infoArray[0].regNum = destXmm;
                    infoArray[0].refCount = 2;
                    infoArray[0].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                    numTemps = 1;
                }
                else
                {
                    infoArray[0].regNum = sourceXmm;
                    infoArray[0].refCount = 1;
                    infoArray[0].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                    //The destination is used and then defined
                    infoArray[1].regNum = destXmm;
                    infoArray[1].refCount = 1;
                    infoArray[1].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                    numTemps = 2;
                }

                return numTemps;
            }
            case kMirOpPackedShiftLeft:
            case kMirOpPackedSignedShiftRight:
            case kMirOpPackedUnsignedShiftRight:
            {
                const int destXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vA;

                infoArray[0].regNum = destXmm;
                infoArray[0].refCount = 1;
                infoArray[0].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                return 2;
            }
            case kMirOpPackedAddReduce:
            {
                OpndSize vecUnitSize = static_cast<OpndSize> (currentMIR->dalvikInsn.vC);

                //Determine number of times we need to do horizontal operation
                int times = 0;
                int width = 16 / vecUnitSize;
                while (width > 1)
                {
                    times++;
                    width >>= 1;
                }

                //We will use one xmm for doing the reduction
                const int reductionXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vB;
                infoArray[0].regNum = reductionXmm;
                infoArray[0].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                //Basically we will use it and define it for each iteration.
                //Then we need one reference from transfer from xmm into general purpose register.
                infoArray[0].refCount = 2 * times + 1;

                //We need a temporary to use for virtual register
                const int temp1 = 1;
                infoArray[1].regNum = temp1;
                infoArray[1].physicalType = LowOpndRegType_gp;
                //We need one reference for reduction and another for transfer into VR
                infoArray[1].refCount = 3;
                infoArray[1].shareWithVR = true;

                //We need a temporary to reduce to
                const int temp2 = 2;
                infoArray[2].regNum = temp2;
                infoArray[2].physicalType = LowOpndRegType_gp;
                //We need one reference for reduction and another for transfer into VR
                infoArray[2].refCount = 2;

                return 3;
            }
            case kMirOpPackedReduce:
            {
                //We will use one xmm for doing the reduction
                const int reductionXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vB;
                infoArray[0].regNum = reductionXmm;
                infoArray[0].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;
                infoArray[0].refCount = 1;

                //We need a temporary to use for virtual register
                const int temp1 = 1;
                infoArray[1].regNum = temp1;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[1].refCount = 1;
                infoArray[1].shareWithVR = true;

                return 2;
            }
            case kMirOpPackedSet:
            {
                const unsigned int operandSize = currentMIR->dalvikInsn.vC;

                //We need a temporary to use for virtual register
                const int temp1 = 1;
                infoArray[0].regNum = temp1;
                infoArray[0].physicalType = LowOpndRegType_gp;
                //We need one reference for transfer from VR and one for reading when using it
                infoArray[0].refCount = 2;

                //Check if we need an 8-bit addressable register
                if (operandSize == sizeof (OpndSize_8))
                {
                    infoArray[0].is8Bit = true;
                }

                const int destXmm = PhysicalReg_StartOfXmmMarker + currentMIR->dalvikInsn.vA;

                //Now set up the destination xmm
                infoArray[1].regNum = destXmm;
                infoArray[1].refCount = 3;
                infoArray[1].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;

                return 2;
            }
            case kMirOpNullCheck:
            {
                unsigned int tempRegCount = 0;

                //We only use temps if we need to do a null check
                if ((currentMIR->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
                {
                    //Use temp1 to hold the "this" object reference
                    infoArray[0].regNum = 1;
                    infoArray[0].refCount = 2;
                    infoArray[0].physicalType = LowOpndRegType_gp;
                    tempRegCount++;

                    //When nullCheck is called it expects that it can update references to EDX.
                    //Although EDX is not explicitly used, we must add two references now in order to satisfy
                    //this dependency. Eventually when nullCheck is fixed this can be removed.
                    infoArray[1].regNum = PhysicalReg_EDX;
                    infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                    infoArray[1].refCount = 2;
                    tempRegCount++;
                }

                //Return the number of temps being used
                return tempRegCount;
            }
            case kMirOpCheckStackOverflow:
                //Temp1 is used for loading self pointer
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2;
                infoArray[0].physicalType = LowOpndRegType_gp;

                //Temp2 is used for storing calculations on FP for overflow
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 3;
                infoArray[1].physicalType = LowOpndRegType_gp;

                return 2;
            default:
                ALOGI("JIT_INFO: Extended MIR not supported in getTempRegInfo");
                SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
                return -1;
        }
    }

    switch (inst_op) {
    case OP_NOP:
        return 0;
    case OP_MOVE:
    case OP_MOVE_OBJECT:
    case OP_MOVE_FROM16:
    case OP_MOVE_OBJECT_FROM16:
    case OP_MOVE_16:
    case OP_MOVE_OBJECT_16:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        return 1;
    case OP_MOVE_WIDE:
    case OP_MOVE_WIDE_FROM16:
    case OP_MOVE_WIDE_16:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_xmm;
        return 1;
    case OP_MOVE_RESULT:
    case OP_MOVE_RESULT_OBJECT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;
        return 2;
    case OP_MOVE_RESULT_WIDE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;
        return 2;
    case OP_MOVE_EXCEPTION:
        infoArray[0].regNum = 2;
        infoArray[0].refCount = 3; //DUU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 3;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        return 3;

    case OP_CONST_4:
    case OP_CONST_16:
    case OP_CONST:
    case OP_CONST_HIGH16:
    case OP_CONST_WIDE_16:
    case OP_CONST_WIDE_32:
    case OP_CONST_WIDE:
    case OP_CONST_WIDE_HIGH16:
        return 0;
    case OP_CONST_STRING: //hardcode %eax
    case OP_CONST_STRING_JUMBO:
        infoArray[0].regNum = 3;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;
        infoArray[2].regNum = 2;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].refCount = 4;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 4;
    case OP_CONST_CLASS:
        infoArray[0].regNum = 3;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;
        infoArray[2].regNum = 2;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].refCount = 4;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 4;

    case OP_MONITOR_ENTER:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 5; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 3;
        infoArray[1].refCount = 7; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        infoArray[3].regNum = 2;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_scratch;
        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].refCount = 2;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[5].regNum = 2;
        infoArray[5].refCount = 4; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;
        infoArray[6].regNum = 4;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        infoArray[7].regNum = 5;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_gp;
        infoArray[8].regNum = PhysicalReg_EAX;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
       return 9;

    case OP_MONITOR_EXIT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = PhysicalReg_EAX;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        infoArray[3].regNum = PhysicalReg_EDX;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[4].regNum = 2;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = 3;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = 3;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        infoArray[7].regNum = 4;
        infoArray[7].refCount = 3; //DU
        infoArray[7].physicalType = LowOpndRegType_gp;
        infoArray[8].regNum = 5;
        infoArray[8].refCount = 4; //DU
        infoArray[8].physicalType = LowOpndRegType_gp;
        infoArray[9].regNum = 6;
        infoArray[9].refCount = 4; //DU
        infoArray[9].physicalType = LowOpndRegType_gp;
        infoArray[10].regNum = 7;
        infoArray[10].refCount = 3; //DU
        infoArray[10].physicalType = LowOpndRegType_gp;
        return 11;

    case OP_CHECK_CAST:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 4;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 6;
        infoArray[2].refCount = 3; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;

        infoArray[3].regNum = 1;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_scratch;
        infoArray[4].regNum = 2;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;

        infoArray[5].regNum = PhysicalReg_EAX;
        /* %eax has 3 live ranges
           1> 5 accesses: to resolve the class object
           2> call dvmInstanceofNonTrivial to define %eax, then use it once
           3> move exception object to %eax, then jump to throw_exception
           if WITH_JIT is true, the first live range has 6 accesses
        */
        infoArray[5].refCount = 6;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_EDX;
        infoArray[6].refCount = 2; //export_pc
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[7].regNum = PhysicalReg_ECX;
        infoArray[7].refCount = 1;
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[8].regNum = 3;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_scratch;
        return 9;
    case OP_INSTANCE_OF:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 3;
        infoArray[1].refCount = 4; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 4;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 6;
        infoArray[3].refCount = 3; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;

        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = 2;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;

        infoArray[6].regNum = PhysicalReg_EAX;
        infoArray[6].refCount = 6;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[7].regNum = 3;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_EDX;
        infoArray[8].refCount = 2; //export_pc for class_resolve
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 9;

    case OP_ARRAY_LENGTH:
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[1].linkageToVR = vA;
        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 3;
    case OP_NEW_INSTANCE:
        infoArray[0].regNum = PhysicalReg_EAX;
        //6: class object
        //3: defined by C function, used twice
        infoArray[0].refCount = 6; //next version has 3 references
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].regNum = PhysicalReg_ECX; //before common_throw_message
        infoArray[1].refCount = 1;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 5;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[3].is8Bit = true;
        infoArray[4].regNum = 6;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;

        infoArray[5].regNum = 1;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = 2;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        infoArray[7].regNum = 3;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;

        infoArray[8].regNum = PhysicalReg_EDX; //before common_throw_message
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[9].regNum = 4;
        infoArray[9].refCount = 2; //DU
        infoArray[9].physicalType = LowOpndRegType_scratch;
        return 10;

    case OP_NEW_ARRAY:
        infoArray[0].regNum = PhysicalReg_EAX;
        //4: class object
        //3: defined by C function, used twice
        infoArray[0].refCount = 4; //next version has 3 references
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].regNum = PhysicalReg_EDX; //before common_throw_message
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 5;
        infoArray[3].refCount = 3; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;

        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = 2;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = 3;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        infoArray[7].regNum = 4;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        return 8;

    case OP_FILLED_NEW_ARRAY:
        length = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = PhysicalReg_EAX;
        //4: class object
        //3: defined by C function, used twice (array object)
        //length: access array object to update the content
        infoArray[0].refCount = 4; //next version has 5+length references
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].regNum = PhysicalReg_EDX; //before common_throw_message
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 5;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 6;
        infoArray[4].refCount = 8; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[4].is8Bit = true;

        if(length >= 1) {
            infoArray[5].regNum = 7;
            infoArray[5].refCount = 2; //DU
            infoArray[5].physicalType = LowOpndRegType_gp;
        }
        if(length >= 2) {
            infoArray[6].regNum = 8;
            infoArray[6].refCount = 2; //DU
            infoArray[6].physicalType = LowOpndRegType_gp;
        }
        if(length >= 3) {
            infoArray[7].regNum = 9;
            infoArray[7].refCount = 2; //DU
            infoArray[7].physicalType = LowOpndRegType_gp;
        }
        if(length >= 4) {
            infoArray[8].regNum = 10;
            infoArray[8].refCount = 2; //DU
            infoArray[8].physicalType = LowOpndRegType_gp;
        }
        if(length >= 5) {
            infoArray[9].regNum = 11;
            infoArray[9].refCount = 2; //DU
            infoArray[9].physicalType = LowOpndRegType_gp;
        }
        infoArray[5+length].regNum = 1;
        infoArray[5+length].refCount = 2; //DU
        infoArray[5+length].physicalType = LowOpndRegType_scratch;
        infoArray[6+length].regNum = 2;
        infoArray[6+length].refCount = 4; //DU
        infoArray[6+length].physicalType = LowOpndRegType_scratch;
        infoArray[7+length].regNum = 3;
        infoArray[7+length].refCount = 2; //DU
        infoArray[7+length].physicalType = LowOpndRegType_scratch;
        infoArray[8+length].regNum = 4;
        infoArray[8+length].refCount = 5; //DU
        infoArray[8+length].physicalType = LowOpndRegType_scratch;
        return 9+length;

    case OP_FILLED_NEW_ARRAY_RANGE:
        length = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = PhysicalReg_EAX;
        //4: class object
        //3: defined by C function, used twice (array object)
        //if length is 0, no access to array object
        //else, used inside a loop
        infoArray[0].refCount = 4; //next version: 5+(length >= 1 ? LOOP_COUNT : 0)
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].regNum = PhysicalReg_EDX; //before common_throw_message
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 5;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 6;
        infoArray[4].refCount = 8; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[4].is8Bit = true;

        infoArray[5].regNum = 1;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = 2;
        infoArray[6].refCount = 4; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        infoArray[7].regNum = 3;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;

        infoArray[8].regNum = 7;
        infoArray[8].refCount = 3*(length >= 1 ? LOOP_COUNT : 0);
        infoArray[8].physicalType = LowOpndRegType_gp;
        infoArray[9].regNum = 8;
        infoArray[9].refCount = 3*(length >= 1 ? LOOP_COUNT : 0);
        infoArray[9].physicalType = LowOpndRegType_gp;
        infoArray[10].regNum = 9;
        infoArray[10].refCount = 2*(length >= 1 ? LOOP_COUNT : 0);
        infoArray[10].physicalType = LowOpndRegType_gp;
        infoArray[11].regNum = 10;
        infoArray[11].refCount = 2*(length >= 1 ? LOOP_COUNT : 0);
        infoArray[11].physicalType = LowOpndRegType_gp;
        infoArray[12].regNum = 4;
        infoArray[12].refCount = 5; //DU
        infoArray[12].physicalType = LowOpndRegType_scratch;
        return 13;

    case OP_FILL_ARRAY_DATA:
        infoArray[0].regNum = PhysicalReg_EAX;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].regNum = PhysicalReg_EDX; //before common_throw_message
#if 0//def HARDREG_OPT
        infoArray[1].refCount = 3; //next version has refCount of 2
#else
        infoArray[1].refCount = 5;
#endif
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum =1;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;

        infoArray[3].regNum = 1;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_scratch;
        infoArray[4].regNum = 2;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        return 5;

    case OP_THROW:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = PhysicalReg_EDX; //before common_throw_message
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        infoArray[3].regNum = 2;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_scratch;
        return 4;
    case OP_THROW_VERIFICATION_ERROR:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = PhysicalReg_EDX; //export_pc
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_scratch;
        infoArray[3].regNum = 2;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_scratch;
        return 4;

    case OP_GOTO: //called function common_periodicChecks4
#if defined(ENABLE_TRACING)
        tt = INST_AA(inst);
        tmp_s2 = (s2)((s2)tt << 8) >> 8;
        if(tmp_s2 < 0) {
            infoArray[0].regNum = PhysicalReg_EDX;
            infoArray[0].refCount = 2;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 1;
        }
#endif
        return 0;
    case OP_GOTO_16:
#if defined(ENABLE_TRACING)
        tmp_s2 = (s2)FETCH(1);
        if(tmp_s2 < 0) {
            infoArray[0].regNum = PhysicalReg_EDX;
            infoArray[0].refCount = 2;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 1;
        }
#endif
        return 0;
    case OP_GOTO_32:
#if defined(ENABLE_TRACING)
        tmp_u4 = (u4)FETCH(1);
        tmp_u4 |= (u4)FETCH(2) << 16;
        if(((s4)tmp_u4) < 0) {
            infoArray[0].regNum = PhysicalReg_EDX;
            infoArray[0].refCount = 2;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 1;
        }
#endif
        return 0;
    case OP_IF_EQ:
    case OP_IF_NE:
    case OP_IF_LT:
    case OP_IF_GE:
    case OP_IF_GT:
    case OP_IF_LE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
#if defined(ENABLE_TRACING)
        tmp_s2 = (s2)FETCH(1);
        if(tmp_s2 < 0) {
            infoArray[1].regNum = PhysicalReg_EDX;
            infoArray[1].refCount = 2;
            infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 2;
        }
#endif
        return 1;
    case OP_IF_EQZ: //called function common_periodicChecks4
    case OP_IF_NEZ:
    case OP_IF_LTZ:
    case OP_IF_GEZ:
    case OP_IF_GTZ:
    case OP_IF_LEZ:
#if defined(ENABLE_TRACING)
        tmp_s2 = (s2)FETCH(1);
        if(tmp_s2 < 0) {
            infoArray[0].regNum = PhysicalReg_EDX;
            infoArray[0].refCount = 2;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 1;
        }
#endif
        return 0;
    case OP_PACKED_SWITCH: //jump common_backwardBranch, which calls common_periodicChecks_entry, then jump_reg %eax
    case OP_SPARSE_SWITCH: //%edx, %eax
        u2 tSize;
        u2* switchData;

        //get the inlined switch data offset in dex file
        vB = currentMIR->dalvikInsn.vB;
        switchData = const_cast<u2*>(dalvikPC) + (s4)vB;
        switchData++;

        // get the size of switch bytecode cases
        tSize = *switchData;

        // if number of switch bytecode cases is within the range of MAX_CHAINED_SWITCH_CASES(64)
        if (tSize <= MAX_CHAINED_SWITCH_CASES) {

            // for packed-switch lowering implementation
            if (inst_op == OP_PACKED_SWITCH) {

                infoArray[0].regNum = 1;
                infoArray[0].refCount = 5; //DU
                infoArray[0].shareWithVR = false;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 5; //DU
                infoArray[1].physicalType = LowOpndRegType_gp;
                return 2;
            }

            // for sparse-switch lowering implementation
            else {
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2; //DU
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = PhysicalReg_EAX; //return by dvm helper
                infoArray[1].refCount = 2; //2 uses
                infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                infoArray[2].regNum = 1;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_scratch;
                return 3;
            }
        }

        // if number of switch bytecode cases is bigger than MAX_CHAINED_SWITCH_CASES(64)
        // set temporary register info separately for sparse and packed switch bytecode lowering implementation
        else {
            infoArray[0].regNum = 1;
            if (inst_op == OP_PACKED_SWITCH) {
                infoArray[0].refCount = 9;
                infoArray[0].shareWithVR = false;
            }
            else {
                infoArray[0].refCount = 2;
            }
            infoArray[0].physicalType = LowOpndRegType_gp;
            infoArray[1].regNum = 2;
            infoArray[1].refCount = 6;
            infoArray[1].physicalType = LowOpndRegType_gp;
            infoArray[2].regNum = PhysicalReg_EAX;
            if (inst_op == OP_PACKED_SWITCH) {
                infoArray[2].refCount = 4;
            }
            else {
                infoArray[2].refCount = 10;
            }
            infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[3].regNum = 1;
            infoArray[3].refCount = 2;
            infoArray[3].physicalType = LowOpndRegType_scratch;
            infoArray[4].regNum = 2;
            infoArray[4].refCount = 2;
            infoArray[4].physicalType = LowOpndRegType_scratch;
            return 5;
        }

    case OP_AGET:
    case OP_AGET_OBJECT:
    case OP_AGET_BOOLEAN:
    case OP_AGET_BYTE:
    case OP_AGET_CHAR:
    case OP_AGET_SHORT:
#ifdef INC_NCG_O0
        if(gDvm.helper_switch[7]) {
            infoArray[0].regNum = PhysicalReg_EBX;
            infoArray[0].refCount = 2;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[1].regNum = PhysicalReg_ECX;
            infoArray[1].refCount = 2;
            infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[2].regNum = PhysicalReg_EDX;
            infoArray[2].refCount = 2;
            infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 3;
        }
#endif
        vA = currentMIR->dalvikInsn.vA;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 4;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[3].linkageToVR = vA;

        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_AGET_BYTE || inst_op == OP_AGET_BOOLEAN)
            infoArray[3].is8Bit = true;
        infoArray[4].refCount = 2;
        return 5;
#else
        infoArray[4].refCount = 4;
        // Use temp 5 to store address of heap access
        infoArray[5].regNum = 5;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;
        // Return value from calling loadFromShadowHeap will be in EAX
        infoArray[6].regNum = PhysicalReg_EAX;
        infoArray[6].refCount = 4;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        // Scratch for calling loadFromShadowHeap
        infoArray[7].regNum = 1;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_ECX;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 9;
#endif
    case OP_AGET_WIDE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 1;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_xmm;

        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[4].refCount = 2;
        return 5;
#else
        infoArray[4].refCount = 4;
        infoArray[5].regNum = PhysicalReg_XMM7;
        infoArray[5].refCount = 1; //U
        infoArray[5].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;
        // Use temp 5 to store address of heap access
        infoArray[6].regNum = 5;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        // Scratch for calling loadFromShadowHeap
        infoArray[7].regNum = 1;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_EAX;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[9].regNum = PhysicalReg_ECX;
        infoArray[9].refCount = 2;
        infoArray[9].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 10;
#endif
    case OP_APUT_BYTE:
        for(k = 0; k < MAX_TEMP_REG_PER_BYTECODE; k++)
            infoArray[k].shareWithVR = true; //false;
        // Intentional fall through
    case OP_APUT:
    case OP_APUT_BOOLEAN:
    case OP_APUT_CHAR:
    case OP_APUT_SHORT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 4;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;

        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_APUT_BYTE || inst_op == OP_APUT_BOOLEAN)
            infoArray[3].is8Bit = true;
        infoArray[4].refCount = 2;
        return 5;
#else
        infoArray[4].refCount = 4;
        // Use temp 5 to store address of heap access
        infoArray[5].regNum = 5;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[6].regNum = 1;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        infoArray[7].regNum = PhysicalReg_ECX;
        infoArray[7].refCount = 2;
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[8].regNum = PhysicalReg_EAX;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 9;
#endif
    case OP_APUT_WIDE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 1;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_xmm;

        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[4].refCount = 2;
        return 5;
#else
        infoArray[4].refCount = 4;
        // Use temp 4 to store address of heap access
        infoArray[5].regNum = 4;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[6].regNum = 1;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        infoArray[7].regNum = PhysicalReg_EAX;
        infoArray[7].refCount = 2;
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[8].regNum = PhysicalReg_ECX;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 9;
#endif
    case OP_APUT_OBJECT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 5+1; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2; //live through function call dvmCanPut
        infoArray[1].refCount = 3+1; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 4;
        infoArray[3].refCount = 4+1; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 5;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = 6;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;

        infoArray[6].regNum = PhysicalReg_EDX;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[7].regNum = PhysicalReg_EAX;
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[6].refCount = 2; //DU
        infoArray[7].refCount = 2; //DU
#else
        infoArray[6].refCount = 4+2; //DU
        infoArray[7].refCount = 4+2;
#endif
        infoArray[8].regNum = 1;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_scratch;
        infoArray[0].shareWithVR = false;

#ifndef WITH_SELF_VERIFICATION
        return updateMarkCard_notNull(infoArray,
                                      0/*index for tgtAddrReg*/, 9);
#else
        // Use temp 7 to store address of heap access
        infoArray[9].regNum = 7;
        infoArray[9].refCount = 4; //DU
        infoArray[9].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[10].regNum = 1;
        infoArray[10].refCount = 6; //DU
        infoArray[10].physicalType = LowOpndRegType_scratch;
        infoArray[11].regNum = PhysicalReg_ECX;
        infoArray[11].refCount = 2+2;
        infoArray[11].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return updateMarkCard_notNull(infoArray,
                                      0/*index for tgtAddrReg*/, 12);
#endif

    case OP_IGET:
    case OP_IGET_OBJECT:
    case OP_IGET_VOLATILE:
    case OP_IGET_OBJECT_VOLATILE:
    case OP_IGET_BOOLEAN:
    case OP_IGET_BYTE:
    case OP_IGET_CHAR:
    case OP_IGET_SHORT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_scratch;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        infoArray[3].refCount = 3; //DU
#else
        infoArray[2].refCount = 4; //DU
        // Return value from calling loadFromShadowHeap will be in EAX
        infoArray[3].refCount = 6; //DU
#endif
        infoArray[4].regNum = 3;
        infoArray[4].refCount = 3; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = 7;
#ifdef DEBUG_IGET_OBJ
        //add hack for a specific instance (iget_obj_inst) of IGET_OBJECT within a method
        if(inst_op == OP_IGET_OBJECT && !strncmp(currentMethod->clazz->descriptor, "Lspec/benchmarks/_228_jack/Parse", 32) &&
           !strncmp(currentMethod->name, "buildPhase3", 11))
        {
#if 0
          if(iget_obj_inst == 12) {
            ALOGD("increase count for instance %d of %s %s", iget_obj_inst, currentMethod->clazz->descriptor, currentMethod->name);
            infoArray[5].refCount = 4; //DU
          }
          else
#endif
            infoArray[5].refCount = 3;
          iget_obj_inst++;
        }
        else
          infoArray[5].refCount = 3;
#else
        infoArray[5].refCount = 3; //DU
#endif
        infoArray[5].physicalType = LowOpndRegType_gp;
        infoArray[6].regNum = 8;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        infoArray[7].regNum = 9;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_gp;
#ifndef WITH_SELF_VERIFICATION
        return 8;
#else
        // Use temp 10 to store address of heap access
        infoArray[8].regNum = 10;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_gp;
        infoArray[9].regNum = 5;
        infoArray[9].refCount = 2; //DU
        infoArray[9].physicalType = LowOpndRegType_scratch;
        infoArray[10].regNum = PhysicalReg_ECX;
        infoArray[10].refCount = 2;
        infoArray[10].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 11;
#endif
    case OP_IPUT:
    case OP_IPUT_OBJECT:
    case OP_IPUT_VOLATILE:
    case OP_IPUT_OBJECT_VOLATILE:
    case OP_IPUT_BOOLEAN:
    case OP_IPUT_BYTE:
    case OP_IPUT_CHAR:
    case OP_IPUT_SHORT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_scratch;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        infoArray[3].refCount = 3; //DU
#else
        infoArray[2].refCount = 4; //DU
        infoArray[3].refCount = 5; //DU
#endif
        infoArray[4].regNum = 3;
        infoArray[4].refCount = 3; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = 7;
        infoArray[5].refCount = 3; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;
        infoArray[6].regNum = 8;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        infoArray[7].regNum = 9;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_gp;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_IPUT_VOLATILE || inst_op == OP_IPUT_OBJECT_VOLATILE) {
            infoArray[7].shareWithVR = false; //To avoid redundant read from memory
            infoArray[7].refCount ++; //for xchg
            if(inst_op == OP_IPUT_OBJECT_VOLATILE) {
                infoArray[7].refCount ++; //to restore after xchg
            }
        }
        if(inst_op == OP_IPUT_OBJECT || inst_op == OP_IPUT_OBJECT_VOLATILE) {
            infoArray[5].shareWithVR = false;
            return updateMarkCard(infoArray, 7/*index for valReg*/,
                                  5/*index for tgtAddrReg*/, 8);
        }
        return 8;
#else
        // Use temp 10 to store address of heap access
        infoArray[8].regNum = 10;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_gp;
        infoArray[9].regNum = 5;
        infoArray[9].refCount = 2; //DU
        infoArray[9].physicalType = LowOpndRegType_scratch;
        infoArray[10].regNum = PhysicalReg_ECX;
        infoArray[10].refCount = 2;
        infoArray[10].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        if(inst_op == OP_IPUT_OBJECT || inst_op == OP_IPUT_OBJECT_VOLATILE) {
            infoArray[5].shareWithVR = false;
            return updateMarkCard(infoArray, 7/*index for valReg*/,
                                  5/*index for tgtAddrReg*/, 11);
        }
        return 11;
#endif

    case OP_IGET_WIDE:
    case OP_IGET_WIDE_VOLATILE:
    case OP_IPUT_WIDE:
    case OP_IPUT_WIDE_VOLATILE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_scratch;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        infoArray[3].refCount = 3; //DU
#else
        infoArray[2].refCount = 4;
        infoArray[3].refCount = 5;
#endif
        infoArray[4].regNum = 3;
        infoArray[4].refCount = 3; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = 7;
        infoArray[5].refCount = 3; //DU
        infoArray[5].physicalType = LowOpndRegType_gp;
        infoArray[6].regNum = 8;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        infoArray[7].regNum = 1;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_xmm;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_IPUT_WIDE_VOLATILE || inst_op == OP_IGET_WIDE_VOLATILE) {
            infoArray[8].regNum = 3;
            infoArray[8].refCount = 2; //DU
            infoArray[8].physicalType = LowOpndRegType_scratch;
            infoArray[9].regNum = 9;
            infoArray[9].refCount = 2; //DU
            infoArray[9].physicalType = LowOpndRegType_gp;
            return 10;
        }
        return 8;

#else
        infoArray[8].regNum = PhysicalReg_XMM7;
        infoArray[8].refCount = 1; //U
        infoArray[8].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;
        // Use temp 10 to store address of heap access
        infoArray[9].regNum = 10;
        infoArray[9].refCount = 4; //DU
        infoArray[9].physicalType = LowOpndRegType_gp;
        infoArray[10].regNum = 5;
        infoArray[10].refCount = 4; //DU
        infoArray[10].physicalType = LowOpndRegType_scratch;
        infoArray[11].regNum = PhysicalReg_ECX;
        infoArray[11].refCount = 2;
        infoArray[11].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        if(inst_op == OP_IPUT_WIDE_VOLATILE || inst_op == OP_IGET_WIDE_VOLATILE) {
            infoArray[12].regNum = 3;
            infoArray[12].refCount = 4; //DU
            infoArray[12].physicalType = LowOpndRegType_scratch;
            infoArray[13].regNum = 9;
            infoArray[13].refCount = 2; //DU
            infoArray[13].physicalType = LowOpndRegType_gp;
            return 14;
        }
        return 12;
#endif
    case OP_SGET:
    case OP_SGET_OBJECT:
    case OP_SGET_VOLATILE:
    case OP_SGET_OBJECT_VOLATILE:
    case OP_SGET_BOOLEAN:
    case OP_SGET_BYTE:
    case OP_SGET_CHAR:
    case OP_SGET_SHORT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_scratch;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;

        infoArray[2].regNum = PhysicalReg_EAX;
#if defined(WITH_SELF_VERIFICATION)
        // Return value from calling loadFromShadowHeap will be in EAX
        infoArray[2].refCount = 6;
#else
        infoArray[2].refCount = 2;
#endif
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 7;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;

        infoArray[5].regNum = PhysicalReg_EDX;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[5].refCount = 2; //DU
        return 6;
#else
        infoArray[5].refCount = 4; //DU
        // Use temp 8 to store address of heap access
        infoArray[6].regNum = 8;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        // Scratch for calling loadFromShadowHeap
        infoArray[7].regNum = 5;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_ECX;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 9;
#endif

    case OP_SPUT:
    case OP_SPUT_OBJECT:
    case OP_SPUT_VOLATILE:
    case OP_SPUT_OBJECT_VOLATILE:
    case OP_SPUT_BOOLEAN:
    case OP_SPUT_BYTE:
    case OP_SPUT_CHAR:
    case OP_SPUT_SHORT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_scratch;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;

        infoArray[2].regNum = PhysicalReg_EAX;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2+1; //access clazz of the field
#else
        infoArray[2].refCount = 4+2;
#endif
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 7;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;

        infoArray[5].regNum = PhysicalReg_EDX;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        if(inst_op == OP_SPUT_VOLATILE || inst_op == OP_SPUT_OBJECT_VOLATILE) {
            infoArray[4].shareWithVR = false; //To avoid redundant read from memory
            infoArray[4].refCount ++; //for xchg
            if(inst_op == OP_SPUT_OBJECT_VOLATILE) {
                infoArray[4].refCount ++; //to restore after xchg
            }
        }
        infoArray[5].refCount = 2; //DU
        if(inst_op == OP_SPUT_OBJECT || inst_op == OP_SPUT_OBJECT_VOLATILE) {
            infoArray[2].shareWithVR = false;
            infoArray[6].regNum = 12;
            infoArray[6].refCount = 1; //1 def, 2 uses in updateMarkCard
            infoArray[6].physicalType = LowOpndRegType_gp;
            return updateMarkCard(infoArray, 4/*index for valReg*/,
                                  6/*index for tgtAddrReg */, 7);
        }
        return 6;
#else
        infoArray[5].refCount = 4; //DU
        // Use temp 8 to store address of heap access
        infoArray[6].regNum = 8;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[7].regNum = 5;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_ECX;
        infoArray[8].refCount = 2;
        infoArray[8].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        if(inst_op == OP_SPUT_OBJECT || inst_op == OP_SPUT_OBJECT_VOLATILE) {
            infoArray[2].shareWithVR = false;
            infoArray[9].regNum = 12;
            infoArray[9].refCount = 3; //1 def, 2 uses in updateMarkCard
            infoArray[9].physicalType = LowOpndRegType_gp;
            return updateMarkCard(infoArray, 4/*index for valReg*/,
                                  6/*index for tgtAddrReg */, 10);
        }
        return 9;
#endif
    case OP_SGET_WIDE:
    case OP_SGET_WIDE_VOLATILE:
    case OP_SPUT_WIDE:
    case OP_SPUT_WIDE_VOLATILE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_scratch;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_scratch;

        infoArray[2].regNum = PhysicalReg_EAX;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2;
#else
        infoArray[2].refCount = 4;
#endif
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_xmm;

        infoArray[5].regNum = PhysicalReg_EDX;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[5].refCount = 2; //DU
        if(inst_op == OP_SPUT_WIDE_VOLATILE || inst_op == OP_SGET_WIDE_VOLATILE) {
            infoArray[6].regNum = 3;
            infoArray[6].refCount = 2; //DU
            infoArray[6].physicalType = LowOpndRegType_scratch;
            infoArray[7].regNum = 9;
            infoArray[7].refCount = 2; //DU
            infoArray[7].physicalType = LowOpndRegType_gp;
            return 8;
        }
        return 6;
#else
        infoArray[5].refCount = 4; //DU
        // use temp 4 to store address of shadow heap access
        infoArray[6].regNum = 4;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[7].regNum = 5;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = PhysicalReg_XMM7;
        infoArray[8].refCount = 1; //U
        infoArray[8].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;
        infoArray[9].regNum = PhysicalReg_ECX;
        infoArray[9].refCount = 2;
        infoArray[9].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        if(inst_op == OP_SPUT_WIDE_VOLATILE || inst_op == OP_SGET_WIDE_VOLATILE) {
            infoArray[10].regNum = 3;
            infoArray[10].refCount = 2; //DU
            infoArray[10].physicalType = LowOpndRegType_scratch;
            infoArray[11].regNum = 9;
            infoArray[11].refCount = 2; //DU
            infoArray[11].physicalType = LowOpndRegType_gp;
            return 12;
        }
        return 10;
#endif


    case OP_IGET_QUICK:
    case OP_IGET_OBJECT_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        return 3;
#else
        infoArray[2].refCount = 4; //DU
        // Use temp 3 to store address of heap access
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        // Return value from calling loadFromShadowHeap will be in EAX
        infoArray[4].regNum = PhysicalReg_EAX;
        infoArray[4].refCount = 4;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[5].regNum = PhysicalReg_ECX;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        // Scratch for calling loadFromShadowHeap
        infoArray[6].regNum = 1;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        return 7;
#endif
    case OP_IPUT_QUICK:
    case OP_IPUT_OBJECT_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        if(inst_op == OP_IPUT_OBJECT_QUICK) {
            infoArray[0].shareWithVR = false;
            return updateMarkCard(infoArray, 1/*index for valReg*/,
                                  0/*index for tgtAddrReg*/, 3);
        }
        return 3;
#else
        infoArray[2].refCount = 4; //DU
        // Use temp 3 to store address of heap access
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = PhysicalReg_EAX;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_ECX;
        infoArray[6].refCount = 2;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        if(inst_op == OP_IPUT_OBJECT_QUICK) {
            infoArray[0].shareWithVR = false;
            return updateMarkCard(infoArray, 1/*index for valReg*/,
                                  0/*index for tgtAddrReg*/, 7/*ScratchReg*/);
        }
        return 7;
#endif
    case OP_IGET_WIDE_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_xmm;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        return 3;
#else
        infoArray[2].refCount = 4; //DU
        // use temp 3 to store address of heap access
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = PhysicalReg_XMM7;
        infoArray[5].refCount = 1; //U
        infoArray[5].physicalType = LowOpndRegType_xmm | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_EAX;
        infoArray[6].refCount = 2;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[7].regNum = PhysicalReg_ECX;
        infoArray[7].refCount = 2;
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 8;
#endif
    case OP_IPUT_WIDE_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_xmm;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
#ifndef WITH_SELF_VERIFICATION
        infoArray[2].refCount = 2; //DU
        return 3;
#else
        infoArray[2].refCount = 4; //DU
        // use temp 3 to store address of heap access
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        // Scratch for calling storeToShadowHeap
        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = PhysicalReg_EAX;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_ECX;
        infoArray[6].refCount = 2;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 7;
#endif

    case OP_RETURN_VOID:
    case OP_RETURN_VOID_BARRIER:
        infoArray[0].regNum = PhysicalReg_ECX;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].regNum = PhysicalReg_EDX;
        infoArray[1].refCount = 1; //D
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 2;
    case OP_RETURN:
    case OP_RETURN_OBJECT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = PhysicalReg_ECX;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].refCount = 1; //D
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 3;
    case OP_RETURN_WIDE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = PhysicalReg_ECX;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].refCount = 1; //D
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 3;
    case OP_INVOKE_VIRTUAL:
    case OP_INVOKE_VIRTUAL_RANGE:
#ifdef PREDICTED_CHAINING
        numTmps = updateGenPrediction(infoArray, false /*not interface*/);
        infoArray[numTmps].regNum = 5;
        infoArray[numTmps].refCount = 3; //DU
        infoArray[numTmps].physicalType = LowOpndRegType_gp;
        numTmps++;
        if(inst_op == OP_INVOKE_VIRTUAL)
            k = updateInvokeNoRange(infoArray, numTmps, currentMIR);
        else
            k = updateInvokeRange(infoArray, numTmps, currentMIR);
        return k;
#else
        infoArray[0].regNum = 3;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 7;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 8;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 6;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 5;
        infoArray[4].refCount = 3; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = PhysicalReg_EDX;
        infoArray[5].refCount = 2; //2 versions, first version DU is for exception, 2nd version: eip right before jumping to invokeArgsDone
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_ECX; //ecx is ued in invokeArgsDone
        infoArray[6].refCount = 1+1; //used in .invokeArgsDone
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        //when WITH_JIT is true and PREDICTED_CHAINING is false
        //  temp 8 and EAX are not used; but it is okay to keep it here
        infoArray[7].regNum = PhysicalReg_EAX;
        infoArray[7].refCount = 4; //DU
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[8].regNum = 1;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_scratch;
        infoArray[9].regNum = 2;
        infoArray[9].refCount = 2; //DU
        infoArray[9].physicalType = LowOpndRegType_scratch;
        if(inst_op == OP_INVOKE_VIRTUAL)
            k = updateInvokeNoRange(infoArray, 10);
        else
            k = updateInvokeRange(infoArray, 10);
        return k;
#endif
    case OP_INVOKE_SUPER:
    case OP_INVOKE_SUPER_RANGE:
        infoArray[0].regNum = 3;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 7;
        infoArray[1].refCount = 3; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 8;
        infoArray[2].refCount = 3; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 6;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 9;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;

        infoArray[5].regNum = PhysicalReg_EDX;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_ECX;
        infoArray[6].refCount = 1+1; //DU
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[7].regNum = PhysicalReg_EAX;
        infoArray[7].refCount = 4; //DU
        infoArray[7].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[8].regNum = 1;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_scratch;
        infoArray[9].regNum = 2;
        infoArray[9].refCount = 2; //DU
        infoArray[9].physicalType = LowOpndRegType_scratch;
        infoArray[10].regNum = 3;
        infoArray[10].refCount = 2; //DU
        infoArray[10].physicalType = LowOpndRegType_scratch;
        infoArray[11].regNum = 4;
        infoArray[11].refCount = 2; //DU
        infoArray[11].physicalType = LowOpndRegType_scratch;
        if(inst_op == OP_INVOKE_SUPER)
            k = updateInvokeNoRange(infoArray, 12, currentMIR);
        else
            k = updateInvokeRange(infoArray, 12, currentMIR);
        return k;
    case OP_INVOKE_DIRECT:
    case OP_INVOKE_DIRECT_RANGE:
        infoArray[0].regNum = 3;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 5;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;

        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = PhysicalReg_ECX;
        infoArray[3].refCount = 2;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[4].regNum = PhysicalReg_EAX;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[5].regNum = 1;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = 2;
        infoArray[6].refCount = 2; //DU
        infoArray[6].physicalType = LowOpndRegType_scratch;
        if(inst_op == OP_INVOKE_DIRECT)
            k = updateInvokeNoRange(infoArray, 7, currentMIR);
        else
            k = updateInvokeRange(infoArray, 7, currentMIR);
        return k;
    case OP_INVOKE_STATIC:
    case OP_INVOKE_STATIC_RANGE:
        infoArray[0].regNum = 3;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;

        infoArray[1].regNum = PhysicalReg_EDX;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[2].regNum = PhysicalReg_ECX;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = 2;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        if(inst_op == OP_INVOKE_STATIC)
            k = updateInvokeNoRange(infoArray, 6, currentMIR);
        else
            k = updateInvokeRange(infoArray, 6, currentMIR);
        return k;
    case OP_INVOKE_INTERFACE:
    case OP_INVOKE_INTERFACE_RANGE:
#ifdef PREDICTED_CHAINING
        numTmps = updateGenPrediction(infoArray, true /*interface*/);
        infoArray[numTmps].regNum = 1;
        infoArray[numTmps].refCount = 3; //DU
        infoArray[numTmps].physicalType = LowOpndRegType_gp;
        numTmps++;
        if(inst_op == OP_INVOKE_INTERFACE)
            k = updateInvokeNoRange(infoArray, numTmps, currentMIR);
        else
            k = updateInvokeRange(infoArray, numTmps, currentMIR);
        return k;
#else
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 3;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 4;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 5;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;

        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[5].regNum = PhysicalReg_ECX;
        infoArray[5].refCount = 1+1; //DU
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[6].regNum = PhysicalReg_EAX;
        infoArray[6].refCount = 2+1; //2 uses
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[7].regNum = 1;
        infoArray[7].refCount = 2; //DU
        infoArray[7].physicalType = LowOpndRegType_scratch;
        infoArray[8].regNum = 2;
        infoArray[8].refCount = 2; //DU
        infoArray[8].physicalType = LowOpndRegType_scratch;
        infoArray[9].regNum = 3;
        infoArray[9].refCount = 2; //DU
        infoArray[9].physicalType = LowOpndRegType_scratch;
        if(inst_op == OP_INVOKE_INTERFACE)
            k = updateInvokeNoRange(infoArray, 10);
        else
            k = updateInvokeRange(infoArray, 10);
        return k;
#endif
        ////////////////////////////////////////////// ALU
    case OP_NEG_INT:
    case OP_NOT_INT:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (vA != vB)
            infoArray[0].shareWithVR = false;
        return 1;
    case OP_NEG_LONG:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //define, use
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 4; //define, update, use
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;
    case OP_NOT_LONG:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_xmm;
        if (vA != vB)
            infoArray[0].shareWithVR = false;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;
    case OP_NEG_FLOAT:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (vA != vB)
            infoArray[0].shareWithVR = false;
        return 1;
    case OP_NEG_DOUBLE:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, use
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //define, update, use
        infoArray[1].physicalType = LowOpndRegType_xmm;
        if (vA != vB)
            infoArray[0].shareWithVR = false;
        return 2;

    case OP_INT_TO_LONG: //hard-code eax & edx
        infoArray[0].regNum = PhysicalReg_EAX;
        infoArray[0].refCount = 2+1;
        infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[0].shareWithVR = false;
        infoArray[1].regNum = PhysicalReg_EDX;
        infoArray[1].refCount = 1+1; //cdq accesses edx & eax
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 2;
    case OP_INT_TO_DOUBLE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;
    case OP_INT_TO_FLOAT:
    case OP_LONG_TO_FLOAT:
    case OP_LONG_TO_DOUBLE:
    case OP_FLOAT_TO_DOUBLE:
    case OP_DOUBLE_TO_FLOAT:
        return 0; //fp stack
    case OP_LONG_TO_INT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        return 1;
    case OP_FLOAT_TO_INT:
    case OP_DOUBLE_TO_INT: //fp stack
        return 0;
    case OP_FLOAT_TO_LONG:
    case OP_DOUBLE_TO_LONG:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //define, use
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //define, use
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //define, use
        infoArray[2].physicalType = LowOpndRegType_xmm;
        return 3;
    case OP_INT_TO_BYTE:
    case OP_INT_TO_CHAR:
    case OP_INT_TO_SHORT:
        vA = currentMIR->dalvikInsn.vA;
        vB = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //define, update, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (vA != vB)
            infoArray[0].shareWithVR = false;
        if (inst_op == OP_INT_TO_BYTE)
            infoArray[0].is8Bit = true;
        return 1;

    case OP_ADD_INT:
    case OP_SUB_INT:
    case OP_MUL_INT:
    case OP_AND_INT:
    case OP_OR_INT:
    case OP_XOR_INT:
    case OP_ADD_INT_2ADDR:
    case OP_SUB_INT_2ADDR:
    case OP_MUL_INT_2ADDR:
    case OP_AND_INT_2ADDR:
    case OP_OR_INT_2ADDR:
    case OP_XOR_INT_2ADDR:
        if(inst_op == OP_ADD_INT || inst_op == OP_SUB_INT || inst_op == OP_MUL_INT ||
           inst_op == OP_AND_INT || inst_op == OP_OR_INT || inst_op == OP_XOR_INT) {
            vA = currentMIR->dalvikInsn.vA;
            v1 = currentMIR->dalvikInsn.vB;
        } else {
            vA = currentMIR->dalvikInsn.vA;
            v1 = vA;
        }
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if(vA != v1)
            infoArray[0].shareWithVR = false;
        return 1; //common_alu_int

    case OP_SHL_INT:
    case OP_SHR_INT:
    case OP_USHR_INT:
    case OP_SHL_INT_2ADDR:
    case OP_SHR_INT_2ADDR:
    case OP_USHR_INT_2ADDR: //use %cl or %ecx?
        if(inst_op == OP_SHL_INT || inst_op == OP_SHR_INT || inst_op == OP_USHR_INT) {
            vA = currentMIR->dalvikInsn.vA;
            v1 = currentMIR->dalvikInsn.vB;
        } else {
            vA = currentMIR->dalvikInsn.vA;
            v1 = vA;
        }
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if(vA != v1)
            infoArray[0].shareWithVR = false;
        infoArray[1].regNum = PhysicalReg_ECX;
        infoArray[1].refCount = 2; //define, use
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 2;//common_shift_int

    case OP_DIV_INT:
    case OP_REM_INT:
    case OP_DIV_INT_2ADDR:
    case OP_REM_INT_2ADDR: //hard-code %eax, %edx (dividend in edx:eax; quotient in eax; remainder in edx)
        if (inst_op == OP_DIV_INT || inst_op == OP_REM_INT) {
            v2 = currentMIR->dalvikInsn.vC;
        }else {
            v2 = currentMIR->dalvikInsn.vB;
        }

        //Check if the virtual register is a constant because if it is we can figure out result without division
        isConst = isVirtualRegConstant(v2, LowOpndRegType_gp, &tmpvalue, false);

        // Check if vr is constant in method scope
        if (isConst != VR_IS_CONSTANT)
        {
            u8 value;

            // value is passed as a reference to return the constant value if v2 is a constant
            ConstVRType isConstVRContext = dvmCompilerGetConstValueOfVR(const_cast<MIR*>(currentMIR), v2, value);
            if (isConstVRContext == kVRNonWideConst)
            {
                isConst = VR_IS_CONSTANT;
                tmpvalue = (int)value;
            }
        }

        //If we have a constant, we can use a multiplication approach instead.
        //However, we currently do not handle case of -1 constant so we take the divide path.
        //It also does not make sense to optimize division by zero.
        if (isConst == VR_IS_CONSTANT && tmpvalue != -1)
        {
            if (tmpvalue == 0)
            {
                infoArray[0].regNum = PhysicalReg_EDX;
                infoArray[0].refCount = 1;
                infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                return 1;
            }
            if (tmpvalue == 1)
            {
                infoArray[0].regNum = PhysicalReg_EAX;
                infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                infoArray[0].shareWithVR = false;
                infoArray[0].refCount = 2;
                infoArray[1].regNum = PhysicalReg_EDX;
                infoArray[1].refCount = 1;
                infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                if (inst_op == OP_REM_INT || inst_op == OP_REM_INT_2ADDR) {
                    infoArray[1].refCount++;
                }
                else
                {
                    infoArray[0].refCount++;
                }
                return 2;
            }
            else
            {
                int magic, shift;
                calculateMagicAndShift(tmpvalue, &magic, &shift);

                infoArray[0].regNum = 2;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = PhysicalReg_EAX;
                infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                infoArray[1].shareWithVR = false;
                infoArray[2].regNum = PhysicalReg_EDX;
                infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
                infoArray[3].regNum = 1;
                infoArray[3].physicalType = LowOpndRegType_gp;
                if (inst_op == OP_REM_INT || inst_op == OP_REM_INT_2ADDR) {
                    infoArray[0].refCount = 4;
                    infoArray[1].refCount = 8;
                    infoArray[2].refCount = 8;
                    infoArray[3].refCount = 3;
                    if ((tmpvalue > 0 && magic < 0) || (tmpvalue < 0 && magic > 0)) {
                        infoArray[3].refCount++;
                        infoArray[2].refCount++;
                    }
                    if (shift != 0) {
                        infoArray[2].refCount++;
                    }
                }else {
                    infoArray[0].refCount = 2;
                    infoArray[1].refCount = 7;
                    infoArray[2].refCount = 5;
                    infoArray[3].refCount = 1;
                    if ((tmpvalue > 0 && magic < 0) || (tmpvalue < 0 && magic > 0)) {
                        infoArray[3].refCount++;
                        infoArray[2].refCount++;
                    }
                    if (shift != 0) {
                        infoArray[2].refCount++;
                    }
                }
               return 4;
            }
        }else {
            infoArray[0].regNum = 2;
            infoArray[0].refCount = 7; //define, update, use
            infoArray[0].physicalType = LowOpndRegType_gp;
            infoArray[1].regNum = PhysicalReg_EAX; //dividend, quotient
            infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[1].shareWithVR = false;
            infoArray[2].regNum = PhysicalReg_EDX; //export_pc, output for REM
            infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[3].regNum = 1;
            infoArray[3].refCount = 2; //define, use
            infoArray[3].physicalType = LowOpndRegType_scratch;
            infoArray[4].regNum = 3;
            infoArray[4].refCount = 4; //define, use
            infoArray[4].physicalType = LowOpndRegType_gp;
            infoArray[5].regNum = 4;
            infoArray[5].refCount = 2; //define, use
            infoArray[5].physicalType = LowOpndRegType_gp;
            infoArray[5].is8Bit = true;
            if(inst_op == OP_DIV_INT || inst_op == OP_DIV_INT_2ADDR) {
                infoArray[1].refCount = 11;
                infoArray[2].refCount = 9;
            } else {
                infoArray[1].refCount = 10;
                infoArray[2].refCount = 12;
            }
            return 6;
         }
    case OP_ADD_INT_LIT16:
    case OP_MUL_INT_LIT16:
    case OP_AND_INT_LIT16:
    case OP_OR_INT_LIT16:
    case OP_XOR_INT_LIT16:
    case OP_ADD_INT_LIT8:
    case OP_MUL_INT_LIT8:
    case OP_AND_INT_LIT8:
    case OP_OR_INT_LIT8:
    case OP_XOR_INT_LIT8:
    case OP_SHL_INT_LIT8:
    case OP_SHR_INT_LIT8:
    case OP_USHR_INT_LIT8:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if(vA != v1)
            infoArray[0].shareWithVR = false;
        return 1;

    case OP_RSUB_INT_LIT8:
    case OP_RSUB_INT:
        vA = currentMIR->dalvikInsn.vA;
        v1 = currentMIR->dalvikInsn.vB;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if(vA != v1)
            infoArray[0].shareWithVR = false;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3;
        infoArray[1].physicalType = LowOpndRegType_gp;
        if(vA != v1)
            infoArray[1].shareWithVR = false;
        return 2;

    case OP_DIV_INT_LIT16:
    case OP_REM_INT_LIT16:
    case OP_DIV_INT_LIT8:
    case OP_REM_INT_LIT8:
        tmp_s2 = currentMIR->dalvikInsn.vC;
        if((inst_op == OP_DIV_INT_LIT8 || inst_op == OP_DIV_INT_LIT16)) {
            int power = isPowerOfTwo(tmp_s2);
            if(power >= 1) { /* divide by a power of 2 constant */
                infoArray[0].regNum = 2;
                infoArray[0].refCount = 3; //define, use, use
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 1;
                infoArray[1].physicalType = LowOpndRegType_gp;
                if(power == 1) infoArray[1].refCount = 5;
                else infoArray[1].refCount = 6;
                return 2;
            }
        }
        if(tmp_s2 == 0) {
            //export_pc
            infoArray[0].regNum = PhysicalReg_EDX; //export_pc, output for REM
            infoArray[0].refCount = 2;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            return 1;
        }
        if (tmp_s2 == 1)
        {
            infoArray[0].regNum = PhysicalReg_EAX;
            infoArray[0].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[0].shareWithVR = false;
            infoArray[0].refCount = 1;
            infoArray[1].regNum = PhysicalReg_EDX;
            infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
            infoArray[1].refCount = 2;
            if (inst_op == OP_REM_INT_LIT16 || inst_op == OP_REM_INT_LIT8)
            {
                infoArray[1].refCount++;
            }
            else
            {
                infoArray[0].refCount++;
            }
            return 2;
        }
        infoArray[0].regNum = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = PhysicalReg_EAX;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[1].shareWithVR = false;
        infoArray[2].regNum = PhysicalReg_EDX;
        infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        if (tmp_s2 != -1)
        {
            int magic, shift;

            // Use algorithm in H.S.Warren's Hacker's Delight Chapter 10 to replace div with mul
            // Calculate magic number and shift for a given constant divisor based on the algorithm
            calculateMagicAndShift(tmp_s2, &magic, &shift);
            infoArray[3].regNum = 1;
            infoArray[3].physicalType = LowOpndRegType_gp;
            infoArray[0].refCount = 2;
            infoArray[1].refCount = 6; // EAX
            infoArray[2].refCount = 6; // EDX
            infoArray[3].refCount = 3;

            // if divisor > 0 and magic number < 0 or divisor < 0 and magic number > 0
            if ((tmp_s2 > 0 && magic < 0) || (tmp_s2 < 0 && magic > 0))
            {
                infoArray[3].refCount++;
                infoArray[2].refCount++;
            }

            // if shift !=0, update refcount of EDX
            if (shift != 0) {
                infoArray[2].refCount++;
            }
            if (inst_op == OP_REM_INT_LIT16 || inst_op == OP_REM_INT_LIT8)
            {
                infoArray[0].refCount += 2;
                infoArray[1].refCount += 1;
                infoArray[2].refCount += 3;
                infoArray[3].refCount += 2;
            }
            return 4;
        }
        else
        {
            infoArray[0].refCount = 2;
            infoArray[1].refCount = 3;
            infoArray[2].refCount = 4;
            if(inst_op == OP_DIV_INT_LIT16 || inst_op == OP_DIV_INT_LIT8)
            {
                infoArray[1].refCount ++;
            }
            else
            {
                infoArray[2].refCount ++;
            }

            if (tmp_s2 == -1)
            {
                infoArray[1].refCount++;
            }
            return 3;
        }

    case OP_ADD_LONG:
    case OP_ADD_LONG_2ADDR:
        vA = currentMIR->dalvikInsn.vA;

        //In the case of OP_ADD_LONG, we use vB otherwise we use vA
        if (inst_op == OP_ADD_LONG)
        {
            v1 = currentMIR->dalvikInsn.vB;
        }
        else
        {
            v1 = vA;
        }

        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_gp;
        if (vA != v1)
        {
            infoArray[0].shareWithVR = false;
        }
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3; //define, use
        infoArray[1].physicalType = LowOpndRegType_gp;
        if (vA != v1)
        {
            infoArray[1].shareWithVR = false;
        }
        return 2;
    case OP_SUB_LONG:
    case OP_AND_LONG:
    case OP_OR_LONG:
    case OP_XOR_LONG:
    case OP_SUB_LONG_2ADDR:
    case OP_AND_LONG_2ADDR:
    case OP_OR_LONG_2ADDR:
    case OP_XOR_LONG_2ADDR:
        //In the case of non 2ADDR, we use vB otherwise we use vA
        if(inst_op == OP_ADD_LONG || inst_op == OP_SUB_LONG || inst_op == OP_AND_LONG ||
           inst_op == OP_OR_LONG || inst_op == OP_XOR_LONG) {
            vA = currentMIR->dalvikInsn.vA;
            v1 = currentMIR->dalvikInsn.vB;
        } else {
            vA = currentMIR->dalvikInsn.vA;
            v1 = vA;
        }
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_xmm;
        if(vA != v1)
            infoArray[0].shareWithVR = false;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //define, use
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;

    case OP_SHL_LONG:
    case OP_SHL_LONG_2ADDR:
        //In the case of non 2ADDR, we use vB and vC otherwise we use vA
        if(inst_op == OP_SHL_LONG) {
            vA = currentMIR->dalvikInsn.vA;
            v1 = currentMIR->dalvikInsn.vB;
            v2 = currentMIR->dalvikInsn.vC;
            isConst = isVirtualRegConstant(v2, LowOpndRegType_gp, &tmpvalue, false); //do not update refCount
        } else {
            vA = currentMIR->dalvikInsn.vA;
            v1 = vA;
            isConst = isVirtualRegConstant(v1, LowOpndRegType_gp, &tmpvalue, false); //do not update refCount
        }
        if(isConst == 3) {  // case where VR contents is a constant, shift amount is available
           infoArray[0].regNum = 1;
           infoArray[0].refCount = 3; //define, update, use
           infoArray[0].physicalType = LowOpndRegType_xmm;
           if(vA != v1)
               infoArray[0].shareWithVR = false;
           infoArray[1].regNum = 2;
           infoArray[1].refCount = 1; //define, update, use
           infoArray[1].physicalType = LowOpndRegType_xmm;
           infoArray[1].shareWithVR = false;
           return 2;
        } else {      // case where VR content is not a constant, shift amount has to be read from VR.
           infoArray[0].regNum = 1;
           infoArray[0].refCount = 3; //define, update, use
           infoArray[0].physicalType = LowOpndRegType_xmm;
           if(vA != v1)
               infoArray[0].shareWithVR = false;
           infoArray[1].regNum = 2;
           infoArray[1].refCount = 3; //define, update, use
           infoArray[1].physicalType = LowOpndRegType_xmm;
           infoArray[1].shareWithVR = false;
           infoArray[2].regNum = 3;
           infoArray[2].refCount = 2; //define, use
           infoArray[2].physicalType = LowOpndRegType_xmm;
           return 3;
        }
    case OP_SHR_LONG:
    case OP_SHR_LONG_2ADDR:
        if(inst_op == OP_SHR_LONG) {
            vA = currentMIR->dalvikInsn.vA;
            v1 = currentMIR->dalvikInsn.vB;
        } else {
            vA = currentMIR->dalvikInsn.vA;
            v1 = vA;
        }
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4; //define, update, use
        infoArray[0].physicalType = LowOpndRegType_xmm;
        if(vA != v1)
            infoArray[0].shareWithVR = false;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 4; //define, update, use
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[1].shareWithVR = false;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //define, use
        infoArray[2].physicalType = LowOpndRegType_xmm;
        infoArray[3].regNum = 4;
        infoArray[3].refCount = 3;
        infoArray[3].physicalType = LowOpndRegType_xmm;
        infoArray[4].regNum = 5;
        infoArray[4].refCount = 3;
        infoArray[4].physicalType = LowOpndRegType_xmm;
        return 5;

    case OP_USHR_LONG:
    case OP_USHR_LONG_2ADDR:
        if(inst_op == OP_USHR_LONG) {
            vA = currentMIR->dalvikInsn.vA;
            v1 = currentMIR->dalvikInsn.vB;
            v2 = currentMIR->dalvikInsn.vC;
            isConst = isVirtualRegConstant(v2, LowOpndRegType_gp, &tmpvalue, false);
        } else {
            vA = currentMIR->dalvikInsn.vA;
            v1 = vA;
            isConst = isVirtualRegConstant(v1, LowOpndRegType_gp, &tmpvalue, false);
        }
        if (isConst == 3) { // case where VR contents is a constant, shift amount is available
            infoArray[0].regNum = 1;
            infoArray[0].refCount = 3; //define, update, use
            infoArray[0].physicalType = LowOpndRegType_xmm;
            if(vA != v1)
                infoArray[0].shareWithVR = false;
            infoArray[1].regNum = 2;
            infoArray[1].refCount = 1; //define, update, use
            infoArray[1].physicalType = LowOpndRegType_xmm;
            infoArray[1].shareWithVR = false;
            return 2;
         } else { // case where VR content is not a constant, shift amount has to be read from VR.
            infoArray[0].regNum = 1;
            infoArray[0].refCount = 3; //define, update, use
            infoArray[0].physicalType = LowOpndRegType_xmm;
            if(vA != v1)
                infoArray[0].shareWithVR = false;
            infoArray[1].regNum = 2;
            infoArray[1].refCount = 3; //define, update, use
            infoArray[1].physicalType = LowOpndRegType_xmm;
            infoArray[1].shareWithVR = false;
            infoArray[2].regNum = 3;
            infoArray[2].refCount = 2; //define, use
            infoArray[2].physicalType = LowOpndRegType_xmm;
            return 3;
         }

    case OP_MUL_LONG:
    case OP_MUL_LONG_2ADDR:
        if(inst_op == OP_MUL_LONG)
        {
            v1 = currentMIR->dalvikInsn.vB;
        }
        else
        {
            //For 2addr form, the destination is also first operand
            v1 = currentMIR->dalvikInsn.vA;
        }
        v2 = currentMIR->dalvikInsn.vC;

        if (v1 != v2) // when the multiplicands are not the same
        {
           infoArray[0].regNum = 1;
           infoArray[0].refCount = 6;
           infoArray[0].physicalType = LowOpndRegType_gp;
           infoArray[0].shareWithVR = false;
           infoArray[1].regNum = 2;
           infoArray[1].refCount = 3;
           infoArray[1].physicalType = LowOpndRegType_gp;
           infoArray[2].regNum = 3;
           infoArray[2].refCount = 3;
           infoArray[2].physicalType = LowOpndRegType_gp;
           infoArray[3].regNum = PhysicalReg_EAX;
           infoArray[3].refCount = 2+1; //for mul_opc
           infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
           infoArray[4].regNum = PhysicalReg_EDX;
           infoArray[4].refCount = 2; //for mul_opc
           infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
           return 5;
        }
        else // when square of a number is to be computed
        {
           infoArray[0].regNum = 1;
           infoArray[0].refCount = 8;
           infoArray[0].physicalType = LowOpndRegType_gp;
           infoArray[0].shareWithVR = false;
           infoArray[1].regNum = PhysicalReg_EAX;
           infoArray[1].refCount = 2+1; //for mul_opc
           infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
           infoArray[2].regNum = PhysicalReg_EDX;
           infoArray[2].refCount = 3+1; //for mul_opc
           infoArray[2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
           return 3;
         }
    case OP_DIV_LONG:
    case OP_REM_LONG:
    case OP_DIV_LONG_2ADDR:
    case OP_REM_LONG_2ADDR:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[0].shareWithVR = false;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_xmm;
        infoArray[3].regNum = PhysicalReg_EAX;
        infoArray[3].refCount = 2; //defined by function call
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].refCount = 2; //next version has 2 references
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[5].regNum = 1;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_scratch;
        return 6;

    case OP_ADD_FLOAT:
    case OP_SUB_FLOAT:
    case OP_MUL_FLOAT:
    case OP_ADD_FLOAT_2ADDR:
    case OP_SUB_FLOAT_2ADDR:
    case OP_MUL_FLOAT_2ADDR:
    case OP_ADD_DOUBLE: //PhysicalReg_FP TODO
    case OP_SUB_DOUBLE:
    case OP_MUL_DOUBLE:
    case OP_ADD_DOUBLE_2ADDR:
    case OP_SUB_DOUBLE_2ADDR:
    case OP_MUL_DOUBLE_2ADDR:
    case OP_DIV_FLOAT:
    case OP_DIV_FLOAT_2ADDR:
    case OP_DIV_DOUBLE:
    case OP_DIV_DOUBLE_2ADDR:
        vA = currentMIR->dalvikInsn.vA;
        //In the case of non 2ADDR, we use vB and vC otherwise we use vA
        if (inst_op == OP_ADD_FLOAT || inst_op == OP_SUB_FLOAT
                || inst_op == OP_MUL_FLOAT || inst_op == OP_ADD_DOUBLE
                || inst_op == OP_SUB_DOUBLE || inst_op == OP_MUL_DOUBLE
                || inst_op == OP_DIV_FLOAT || inst_op == OP_DIV_DOUBLE)
        {
            v1 = currentMIR->dalvikInsn.vB;
        }
        else
        {
            v1 = vA;
        }
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        if(vA != v1)
        {
            infoArray[0].shareWithVR = false;
        }
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;

    case OP_REM_FLOAT:
    case OP_REM_FLOAT_2ADDR:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_scratch;
        return 3;

    case OP_REM_DOUBLE:
    case OP_REM_DOUBLE_2ADDR:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_xmm;
        infoArray[2].regNum = 1;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_scratch;
        return 3;

    case OP_CMPL_FLOAT:
    case OP_CMPL_DOUBLE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 2;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 3;
        infoArray[3].refCount = 2;
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 4; //return
        infoArray[4].refCount = 5;
        infoArray[4].physicalType = LowOpndRegType_gp;
        return 5;

    case OP_CMPG_FLOAT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        return 1;

    case OP_CMPG_DOUBLE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_xmm;
        return 1;

    case OP_CMP_LONG:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp;
        return 2;

    case OP_EXECUTE_INLINE:
    case OP_EXECUTE_INLINE_RANGE:
        num = currentMIR->dalvikInsn.vA;
#if defined(WITH_JIT)
        tmp = currentMIR->dalvikInsn.vB;
        switch (tmp) {
            case INLINE_STRING_LENGTH:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 3;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_gp;
                infoArray[3].regNum = 1;
                infoArray[3].refCount = 2;
                infoArray[3].physicalType = LowOpndRegType_scratch;
                return 4;
            case INLINE_STRING_IS_EMPTY:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 3;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 4;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 1;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_scratch;
                return 3;
            case INLINE_STRING_CHARAT:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 7;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 7;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[1].shareWithVR = false;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_gp;
                return 3;
            case INLINE_STRING_FASTINDEXOF_II:
#if defined(USE_GLOBAL_STRING_DEFS)
                break;
#else
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 14 * LOOP_COUNT;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 3 * LOOP_COUNT;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 11 * LOOP_COUNT;
                infoArray[2].physicalType = LowOpndRegType_gp;
                infoArray[2].shareWithVR = false;
                infoArray[3].regNum = 4;
                infoArray[3].refCount = 3 * LOOP_COUNT;
                infoArray[3].physicalType = LowOpndRegType_gp;
                infoArray[4].regNum = 5;
                infoArray[4].refCount = 9 * LOOP_COUNT;
                infoArray[4].physicalType = LowOpndRegType_gp;
                infoArray[5].regNum = 6;
                infoArray[5].refCount = 4 * LOOP_COUNT;
                infoArray[5].physicalType = LowOpndRegType_gp;
                infoArray[6].regNum = 7;
                infoArray[6].refCount = 2;
                infoArray[6].physicalType = LowOpndRegType_gp;
                infoArray[7].regNum = 1;
                infoArray[7].refCount = 2;
                infoArray[7].physicalType = LowOpndRegType_scratch;
                return 8;
#endif
            case INLINE_MATH_ABS_LONG:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 7;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 3;
                infoArray[2].physicalType = LowOpndRegType_gp;
                infoArray[3].regNum = 4;
                infoArray[3].refCount = 3;
                infoArray[3].physicalType = LowOpndRegType_gp;
                infoArray[4].regNum = 5;
                infoArray[4].refCount = 2;
                infoArray[4].physicalType = LowOpndRegType_gp;
                infoArray[5].regNum = 6;
                infoArray[5].refCount = 5;
                infoArray[5].physicalType = LowOpndRegType_gp;
                return 6;
            case INLINE_MATH_ABS_INT:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 5;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 4;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_gp;
                return 3;
            case INLINE_MATH_MAX_INT:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 4;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 3;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_gp;
                return 3;
            case INLINE_MATH_MIN_INT:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 4;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 3;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 2;
                infoArray[2].physicalType = LowOpndRegType_gp;
                return 3;
            case INLINE_MATH_ABS_FLOAT:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 3;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[0].shareWithVR = false;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                return 2;
            case INLINE_MATH_ABS_DOUBLE:
                {
                    infoArray[0].regNum = 1;
                    infoArray[0].refCount = 3;
                    infoArray[0].physicalType = LowOpndRegType_xmm;
                    infoArray[0].shareWithVR = false;
                    MIR* mirNext = currentMIR->next;
                    if (mirNext != 0 && mirNext->dalvikInsn.opcode == OP_MOVE_RESULT_WIDE)
                    {
                        return 1;
                    }
                    infoArray[1].regNum = 2;
                    infoArray[1].refCount = 2;
                    infoArray[1].physicalType = LowOpndRegType_gp;
                    return 2;
                }
            case INLINE_FLOAT_TO_RAW_INT_BITS:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                return 2;
            case INLINE_INT_BITS_TO_FLOAT:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                return 2;
            case INLINE_DOUBLE_TO_RAW_LONG_BITS:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 3;
                infoArray[2].physicalType = LowOpndRegType_gp;
                return 3;
            case INLINE_LONG_BITS_TO_DOUBLE:
                infoArray[0].regNum = 1;
                infoArray[0].refCount = 2;
                infoArray[0].physicalType = LowOpndRegType_gp;
                infoArray[1].regNum = 2;
                infoArray[1].refCount = 2;
                infoArray[1].physicalType = LowOpndRegType_gp;
                infoArray[2].regNum = 3;
                infoArray[2].refCount = 3;
                infoArray[2].physicalType = LowOpndRegType_gp;
                return 3;
            default:
                break;
        }
#endif
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 4;
        infoArray[0].physicalType = LowOpndRegType_gp;
        if(num >= 1) {
            infoArray[1].regNum = 2;
            infoArray[1].refCount = 2;
            infoArray[1].physicalType = LowOpndRegType_gp;
        }
        if(num >= 2) {
            infoArray[2].regNum = 3;
            infoArray[2].refCount = 2;
            infoArray[2].physicalType = LowOpndRegType_gp;
        }
        if(num >= 3) {
            infoArray[3].regNum = 4;
            infoArray[3].refCount = 2;
            infoArray[3].physicalType = LowOpndRegType_gp;
        }
        if(num >= 4) {
            infoArray[4].regNum = 5;
            infoArray[4].refCount = 2;
            infoArray[4].physicalType = LowOpndRegType_gp;
        }
        infoArray[num+1].regNum = 6;
        infoArray[num+1].refCount = 2;
        infoArray[num+1].physicalType = LowOpndRegType_gp;
        infoArray[num+2].regNum = PhysicalReg_EAX;
        infoArray[num+2].refCount = 2;
        infoArray[num+2].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[num+3].regNum = PhysicalReg_EDX;
        infoArray[num+3].refCount = 2;
        infoArray[num+3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[num+4].regNum = 1;
        infoArray[num+4].refCount = 4;
        infoArray[num+4].physicalType = LowOpndRegType_scratch;
        return num+5;
#if FIXME
    case OP_INVOKE_OBJECT_INIT_RANGE:
        return 0;
#endif
    case OP_INVOKE_VIRTUAL_QUICK:
    case OP_INVOKE_VIRTUAL_QUICK_RANGE:
#ifdef PREDICTED_CHAINING
        numTmps = updateGenPrediction(infoArray, false /*not interface*/);
        infoArray[numTmps].regNum = 1;
        infoArray[numTmps].refCount = 3; //DU
        infoArray[numTmps].physicalType = LowOpndRegType_gp;
        numTmps++;
        if(inst_op == OP_INVOKE_VIRTUAL_QUICK)
            k = updateInvokeNoRange(infoArray, numTmps, currentMIR);
        else
            k = updateInvokeRange(infoArray, numTmps, currentMIR);
        return k;
#else
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_gp;

        infoArray[3].regNum = PhysicalReg_ECX;
        infoArray[3].refCount = 1+1;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].refCount = 2;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        if(inst_op == OP_INVOKE_VIRTUAL_QUICK_RANGE)
            k = updateInvokeRange(infoArray, 5);
        else
            k = updateInvokeNoRange(infoArray, 5);
        return k;
#endif
    case OP_INVOKE_SUPER_QUICK:
    case OP_INVOKE_SUPER_QUICK_RANGE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2;
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 4;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 5;
        infoArray[2].refCount = 2;
        infoArray[2].physicalType = LowOpndRegType_gp;

        infoArray[3].regNum = PhysicalReg_ECX;
        infoArray[3].refCount = 1+1;
        infoArray[3].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        infoArray[4].regNum = PhysicalReg_EDX;
        infoArray[4].refCount = 2;
        infoArray[4].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;

        infoArray[5].regNum = 1;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = 2;
        infoArray[6].refCount = 2;
        infoArray[6].physicalType = LowOpndRegType_scratch;
        if(inst_op == OP_INVOKE_SUPER_QUICK_RANGE)
            k = updateInvokeRange(infoArray, 7, currentMIR);
        else
            k = updateInvokeNoRange(infoArray, 7, currentMIR);
        return k;
#ifdef SUPPORT_HLO
    case kExtInstruction:
        switch(inst) {
    case OP_X_AGET_QUICK:
    case OP_X_AGET_OBJECT_QUICK:
    case OP_X_AGET_BOOLEAN_QUICK:
    case OP_X_AGET_BYTE_QUICK:
    case OP_X_AGET_CHAR_QUICK:
    case OP_X_AGET_SHORT_QUICK:
        vA = FETCH(1) & 0xff;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 4;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[3].linkageToVR = vA;
        if(inst == OP_X_AGET_BYTE_QUICK || inst == OP_X_AGET_BOOLEAN_QUICK)
            infoArray[3].is8Bit = true;
        return 4;
    case OP_X_AGET_WIDE_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 1;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_xmm;
        return 4;
    case OP_X_APUT_QUICK:
    case OP_X_APUT_OBJECT_QUICK:
    case OP_X_APUT_BOOLEAN_QUICK:
    case OP_X_APUT_BYTE_QUICK:
    case OP_X_APUT_CHAR_QUICK:
    case OP_X_APUT_SHORT_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 4;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        if(inst == OP_X_APUT_BYTE_QUICK || inst == OP_X_APUT_BOOLEAN_QUICK)
            infoArray[3].is8Bit = true;
        return 4;
    case OP_X_APUT_WIDE_QUICK:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 1;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_xmm;
        return 4;
    case OP_X_DEREF_GET:
    case OP_X_DEREF_GET_OBJECT:
    case OP_X_DEREF_GET_BOOLEAN:
    case OP_X_DEREF_GET_BYTE:
    case OP_X_DEREF_GET_CHAR:
    case OP_X_DEREF_GET_SHORT:
        vA = FETCH(1) & 0xff;
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[1].linkageToVR = vA;
        if(inst == OP_X_DEREF_GET_BYTE || inst == OP_X_DEREF_GET_BOOLEAN)
            infoArray[1].is8Bit = true;
        return 2;
    case OP_X_DEREF_GET_WIDE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;
    case OP_X_DEREF_PUT:
    case OP_X_DEREF_PUT_OBJECT:
    case OP_X_DEREF_PUT_BOOLEAN:
    case OP_X_DEREF_PUT_BYTE:
    case OP_X_DEREF_PUT_CHAR:
    case OP_X_DEREF_PUT_SHORT:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        if(inst == OP_X_DEREF_PUT_BYTE || inst == OP_X_DEREF_PUT_BOOLEAN)
            infoArray[1].is8Bit = true;
        return 2;
    case OP_X_DEREF_PUT_WIDE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 1;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_xmm;
        return 2;
    case OP_X_ARRAY_CHECKS:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        return 2;
    case OP_X_CHECK_BOUNDS:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 2; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        return 2;
    case OP_X_CHECK_NULL:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 2; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = PhysicalReg_EDX;
        infoArray[1].refCount = 2;
        infoArray[1].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 2;
    case OP_X_CHECK_TYPE:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 3; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 5;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 6;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 1;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_scratch;
        infoArray[5].regNum = PhysicalReg_EAX;
        infoArray[5].refCount = 2;
        infoArray[5].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 6;
    case OP_X_ARRAY_OBJECT_CHECKS:
        infoArray[0].regNum = 1;
        infoArray[0].refCount = 3; //DU
        infoArray[0].physicalType = LowOpndRegType_gp;
        infoArray[1].regNum = 2;
        infoArray[1].refCount = 4; //DU
        infoArray[1].physicalType = LowOpndRegType_gp;
        infoArray[2].regNum = 3;
        infoArray[2].refCount = 2; //DU
        infoArray[2].physicalType = LowOpndRegType_gp;
        infoArray[3].regNum = 5;
        infoArray[3].refCount = 2; //DU
        infoArray[3].physicalType = LowOpndRegType_gp;
        infoArray[4].regNum = 6;
        infoArray[4].refCount = 2; //DU
        infoArray[4].physicalType = LowOpndRegType_gp;
        infoArray[5].regNum = 1;
        infoArray[5].refCount = 2; //DU
        infoArray[5].physicalType = LowOpndRegType_scratch;
        infoArray[6].regNum = PhysicalReg_EAX;
        infoArray[6].refCount = 2;
        infoArray[6].physicalType = LowOpndRegType_gp | LowOpndRegType_hard;
        return 7;
    }
#endif
    default:
        ALOGI("JIT_INFO: JIT does not support bytecode 0x%hx when updating temp accesses",
                currentMIR->dalvikInsn.opcode);
        SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
        assert(false && "All opcodes should be supported.");
        break;
    }
    return -1;
}
