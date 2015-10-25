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


/*! \file LowerAlu.cpp
    \brief This file lowers ALU bytecodes.
*/
#include "libdex/DexOpcodes.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "NcgAot.h"
#include "enc_wrapper.h"
#include "CompilationUnit.h"
#include "MethodContext.h"
#include "MethodContextHandler.h"

/////////////////////////////////////////////
#define P_GPR_1 PhysicalReg_EBX

/**
 * @brief Generate native code for bytecode neg-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_neg_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NEG_INT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_unary_reg(OpndSize_32, neg_opc, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode not-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_not_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NOT_INT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_unary_reg(OpndSize_32, not_opc, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}
#undef P_GPR_1

/**
 * @brief Generate native code for bytecode neg-long
 * @details Implementation uses XMM registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_neg_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NEG_LONG);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_64, 1, false);
    alu_binary_reg_reg(OpndSize_64, xor_opc, 2, false, 2, false);
    alu_binary_reg_reg(OpndSize_64, sub_opc, 1, false, 2, false);
    set_virtual_reg(vA, OpndSize_64, 2, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode not-long
 * @details Implementation uses XMM registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_not_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NOT_LONG);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_64, 1, false);
    load_global_data_API("64bits", OpndSize_64, 2, false);
    alu_binary_reg_reg(OpndSize_64, andn_opc, 2, false, 1, false);
    set_virtual_reg(vA, OpndSize_64, 1, false);
    return 0;
}

#define P_GPR_1 PhysicalReg_EBX
/**
 * @brief Generate native code for bytecode neg-float
 * @details Implementation uses general purpose registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_neg_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NEG_FLOAT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_binary_imm_reg(OpndSize_32, add_opc, 0x80000000, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}
#undef P_GPR_1

/**
 * @brief Generate native code for bytecode neg-double
 * @details Implementation uses XMM registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_neg_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NEG_DOUBLE);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_64, 1, false);
    load_global_data_API("doubNeg", OpndSize_64, 2, false);
    alu_binary_reg_reg(OpndSize_64, xor_opc, 2, false, 1, false);
    set_virtual_reg(vA, OpndSize_64, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode int-to-long
 * @details Implementation uses native instruction cdq
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_int_to_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_INT_TO_LONG);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, PhysicalReg_EAX, true);
    convert_integer(OpndSize_32, OpndSize_64);
    set_virtual_reg(vA, OpndSize_32, PhysicalReg_EAX, true);
    set_virtual_reg(vA+1, OpndSize_32, PhysicalReg_EDX, true);
    return 0;
}

/**
 * @brief Generate native code for bytecode int-to-float
 * @details Implementation uses FP stack
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_int_to_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_INT_TO_FLOAT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    load_int_fp_stack_VR(OpndSize_32, vB); //fildl
    store_fp_stack_VR(true, OpndSize_32, vA); //fstps
    return 0;
}

/**
 * @brief Generate native code for bytecode int-to-double
 * @details Implementation uses FP stack
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_int_to_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_INT_TO_DOUBLE);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    convert_int_to_fp(1, false, 2, false, true /* isDouble */);
    set_virtual_reg(vA, OpndSize_64, 2, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode long-to-float
 * @details Implementation uses FP stack
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_long_to_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_LONG_TO_FLOAT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    load_int_fp_stack_VR(OpndSize_64, vB); //fildll
    store_fp_stack_VR(true, OpndSize_32, vA); //fstps
    return 0;
}

/**
 * @brief Generate native code for bytecode long-to-double
 * @details Implementation uses FP stack
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_long_to_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_LONG_TO_DOUBLE);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    load_int_fp_stack_VR(OpndSize_64, vB); //fildll
    store_fp_stack_VR(true, OpndSize_64, vA); //fstpl
    return 0;
}

/**
 * @brief Generate native code for bytecode float-to-double
 * @details Implementation uses FP stack
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_float_to_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_FLOAT_TO_DOUBLE);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    load_fp_stack_VR(OpndSize_32, vB); //flds
    store_fp_stack_VR(true, OpndSize_64, vA); //fstpl
    return 0;
}

/**
 * @brief Generate native code for bytecode double-to-float
 * @details Implementation uses FP stack
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_double_to_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DOUBLE_TO_FLOAT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    load_fp_stack_VR(OpndSize_64, vB); //fldl
    store_fp_stack_VR(true, OpndSize_32, vA); //fstps
    return 0;
}

#define P_GPR_1 PhysicalReg_EBX
/**
 * @brief Generate native code for bytecode long-to-int
 * @details Implementation uses general purpose registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_long_to_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_LONG_TO_INT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}
#undef P_GPR_1

//! common code to convert a float or double to integer

//! It uses FP stack
int common_fp_to_int(bool isDouble, int vA, int vB) {
    if(isDouble) {
        load_fp_stack_VR(OpndSize_64, vB); //fldl
    }
    else {
        load_fp_stack_VR(OpndSize_32, vB); //flds
    }

    load_fp_stack_global_data_API("intMax", OpndSize_32);
    load_fp_stack_global_data_API("intMin", OpndSize_32);

    //ST(0) ST(1) ST(2) --> LintMin LintMax value
    compare_fp_stack(true, 2, false/*isDouble*/); //ST(2)
    //ST(0) ST(1) --> LintMax value
    conditional_jump(Condition_AE, ".float_to_int_negInf", true);
    rememberState(1);
    compare_fp_stack(true, 1, false/*isDouble*/); //ST(1)
    //ST(0) --> value
    rememberState(2);
    conditional_jump(Condition_C, ".float_to_int_nanInf", true);
    //fnstcw, orw, fldcw, xorw
    load_effective_addr(-2, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    store_fpu_cw(false/*checkException*/, 0, PhysicalReg_ESP, true);
    alu_binary_imm_mem(OpndSize_16, or_opc, 0xc00, 0, PhysicalReg_ESP, true);
    load_fpu_cw(0, PhysicalReg_ESP, true);
    alu_binary_imm_mem(OpndSize_16, xor_opc, 0xc00, 0, PhysicalReg_ESP, true);
    store_int_fp_stack_VR(true/*pop*/, OpndSize_32, vA); //fistpl
    //fldcw
    load_fpu_cw(0, PhysicalReg_ESP, true);
    load_effective_addr(2, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    rememberState(3);
    unconditional_jump(".float_to_int_okay", true);
    if (insertLabel(".float_to_int_nanInf", true) == -1)
        return -1;
    conditional_jump(Condition_NP, ".float_to_int_posInf", true);
    //fstps CHECK
    goToState(2);
    store_fp_stack_VR(true, OpndSize_32, vA);
    set_VR_to_imm(vA, OpndSize_32, 0);
    transferToState(3);
    unconditional_jump(".float_to_int_okay", true);
    if (insertLabel(".float_to_int_posInf", true) == -1)
        return -1;
    //fstps CHECK
    goToState(2);
    store_fp_stack_VR(true, OpndSize_32, vA);
    set_VR_to_imm(vA, OpndSize_32, 0x7fffffff);
    transferToState(3);
    unconditional_jump(".float_to_int_okay", true);
    if (insertLabel(".float_to_int_negInf", true) == -1)
        return -1;
    goToState(1);
    //fstps CHECK
    store_fp_stack_VR(true, OpndSize_32, vA);
    store_fp_stack_VR(true, OpndSize_32, vA);
    set_VR_to_imm(vA, OpndSize_32, 0x80000000);
    transferToState(3);
    if (insertLabel(".float_to_int_okay", true) == -1)
        return -1;
    return 0;
}

/**
 * @brief Generate native code for bytecode float-to-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_float_to_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_FLOAT_TO_INT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    int retval = common_fp_to_int(false, vA, vB);
    return retval;
}

/**
 * @brief Generate native code for bytecode double-to-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_double_to_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DOUBLE_TO_INT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    int retval = common_fp_to_int(true, vA, vB);
    return retval;
}

//! common code to convert float or double to long

//! It uses FP stack
int common_fp_to_long(bool isDouble, int vA, int vB) {
    if(isDouble) {
        load_fp_stack_VR(OpndSize_64, vB); //fldl
    }
    else {
        load_fp_stack_VR(OpndSize_32, vB); //flds
    }

    //Check if it is the special Negative Infinity value
    load_fp_stack_global_data_API("valueNegInfLong", OpndSize_64);
    //Stack status: ST(0) ST(1) --> LlongMin value
    compare_fp_stack(true, 1, false/*isDouble*/); // Pops ST(1)
    conditional_jump(Condition_AE, ".float_to_long_negInf", true);
    rememberState(1);

    //Check if it is the special Positive Infinity value
    load_fp_stack_global_data_API("valuePosInfLong", OpndSize_64);
    //Stack status: ST(0) ST(1) --> LlongMax value
    compare_fp_stack(true, 1, false/*isDouble*/); // Pops ST(1)
    rememberState(2);
    conditional_jump(Condition_C, ".float_to_long_nanInf", true);

    //Normal Case
    //We want to truncate to 0 for conversion. That will be rounding mode 0x11
    load_effective_addr(-2, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    store_fpu_cw(false/*checkException*/, 0, PhysicalReg_ESP, true);
    //Change control word to rounding mode 11:
    alu_binary_imm_mem(OpndSize_16, or_opc, 0xc00, 0, PhysicalReg_ESP, true);
    //Load the control word
    load_fpu_cw(0, PhysicalReg_ESP, true);
    //Reset the control word
    alu_binary_imm_mem(OpndSize_16, xor_opc, 0xc00, 0, PhysicalReg_ESP, true);
    //Perform the actual conversion
    store_int_fp_stack_VR(true/*pop*/, OpndSize_64, vA); //fistpll
    // Restore the original control word
    load_fpu_cw(0, PhysicalReg_ESP, true);
    load_effective_addr(2, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    rememberState(3);
    /* NOTE: We do not need to pop out the original value we pushed
     * since load_fpu_cw above already clears the stack for
     * normal values.
     */
    unconditional_jump(".float_to_long_okay", true);

    //We can be here for positive infinity or NaN. Check parity bit
    if (insertLabel(".float_to_long_nanInf", true) == -1)
        return -1;
    conditional_jump(Condition_NP, ".float_to_long_posInf", true);
    goToState(2);
    //Save corresponding Long NaN value
    load_global_data_API("valueNanLong", OpndSize_64, 1, false);
    set_virtual_reg(vA, OpndSize_64, 1, false);
    transferToState(3);
    //Pop out the original value we pushed
    compare_fp_stack(true, 0, false/*isDouble*/); //ST(0)
    unconditional_jump(".float_to_long_okay", true);

    if (insertLabel(".float_to_long_posInf", true) == -1)
        return -1;
    goToState(2);
    //Save corresponding Long Positive Infinity value
    load_global_data_API("valuePosInfLong", OpndSize_64, 2, false);
    set_virtual_reg(vA, OpndSize_64, 2, false);
    transferToState(3);
    //Pop out the original value we pushed
    compare_fp_stack(true, 0, false/*isDouble*/); //ST(0)
    unconditional_jump(".float_to_long_okay", true);

    if (insertLabel(".float_to_long_negInf", true) == -1)
        return -1;
    //fstpl
    goToState(1);
    //Load corresponding Long Negative Infinity value
    load_global_data_API("valueNegInfLong", OpndSize_64, 3, false);
    set_virtual_reg(vA, OpndSize_64, 3, false);
    transferToState(3);
    //Pop out the original value we pushed
    compare_fp_stack(true, 0, false/*isDouble*/); //ST(0)

    if (insertLabel(".float_to_long_okay", true) == -1)
        return -1;
    return 0;
}

/**
 * @brief Generate native code for bytecode float-to-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_float_to_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_FLOAT_TO_LONG);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    int retval = common_fp_to_long(false, vA, vB);
    return retval;
}

/**
 * @brief Generate native code for bytecode double-to-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_double_to_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DOUBLE_TO_LONG);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    int retval = common_fp_to_long(true, vA, vB);
    return retval;
}

#define P_GPR_1 PhysicalReg_EBX
/**
 * @brief Generate native code for bytecode int-to-byte
 * @details Implementation uses general purpose registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_int_to_byte(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_INT_TO_BYTE);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    moves_reg_to_reg(OpndSize_8, 1, false, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode int-to-char
 * @details Implementation uses general purpose registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_int_to_char(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_INT_TO_CHAR);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_binary_imm_reg(OpndSize_32, sal_opc, 16, 1, false);
    alu_binary_imm_reg(OpndSize_32, shr_opc, 16, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode int-to-short
 * @details Implementation uses general purpose registers
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_int_to_short(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_INT_TO_SHORT);
    int vA = mir->dalvikInsn.vA; //destination
    int vB = mir->dalvikInsn.vB;
    get_virtual_reg(vB, OpndSize_32, 1, false);
    moves_reg_to_reg(OpndSize_16, 1, false, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}
//! common code to handle integer ALU ops

//! It uses GPR
int common_alu_int(ALU_Opcode opc, int vA, int v1, int v2) { //except div and rem
    get_virtual_reg(v1, OpndSize_32, 1, false);
    //in encoder, reg is first operand, which is the destination
    //gpr_1 op v2(rFP) --> gpr_1
    //shift only works with reg cl, v2 should be in %ecx
    alu_binary_VR_reg(OpndSize_32, opc, v2, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}
#undef P_GPR_1
#define P_GPR_1 PhysicalReg_EBX
//! common code to handle integer shift ops

//! It uses GPR
int common_shift_int(ALU_Opcode opc, int vA, int v1, int v2) {
    get_virtual_reg(v2, OpndSize_32, PhysicalReg_ECX, true);
    get_virtual_reg(v1, OpndSize_32, 1, false);
    //in encoder, reg2 is first operand, which is the destination
    //gpr_1 op v2(rFP) --> gpr_1
    //shift only works with reg cl, v2 should be in %ecx
    alu_binary_reg_reg(OpndSize_32, opc, PhysicalReg_ECX, true, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}
#undef p_GPR_1

/**
 * @brief Generate native code for bytecode add-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_alu_int(add_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_alu_int(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_alu_int(imul_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode and-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_and_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_AND_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_alu_int(and_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode or-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_or_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_OR_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_alu_int(or_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode xor-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_xor_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_XOR_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_alu_int(xor_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shl-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shl_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHL_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_shift_int(shl_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shr-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shr_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHR_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_shift_int(sar_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode ushr-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_ushr_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_USHR_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_shift_int(shr_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode add-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_alu_int(add_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_alu_int(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_alu_int(imul_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode and-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_and_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_AND_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_alu_int(and_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode or-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_or_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_OR_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_alu_int(or_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode xor-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_xor_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_XOR_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_alu_int(xor_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shl-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shl_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHL_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_shift_int(shl_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shr-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shr_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHR_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_shift_int(sar_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode ushr-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_ushr_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_USHR_INT_2ADDR);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = vA;
    v2 = mir->dalvikInsn.vB;
    int retval = common_shift_int(shr_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief common code used by common_div_rem_int and common_div_rem_int_lit for replacing div with mul
 * @param divisor constant value of divisor
 * @param isRem true for REM, false for DIV
 * @return value >= 0 when handled
 */
static void common_div_to_mul(int divisor, bool isRem)
{
    int magic;
    int shift;

#ifdef DIVREM_BYTECODE_VERBOSE
    ALOGD("Using multiplication for integer division due to a constant divisor");
#endif

    // According to H.S.Warren's Hacker's Delight Chapter 10 and
    // T,Grablund, P.L.Montogomery's Division by invariant integers using multiplication
    // For a integer divided by a constant,
    // we can always find a magic number M and a shift S. Thus,
    // For d >= 2,
    //     int(n/d) = floor(n/d) = floor(M*n/2^S), while n > 0
    //     int(n/d) = ceil(n/d) = floor(M*n/2^S) +1, while n < 0.
    // For d <= -2,
    //     int(n/d) = ceil(n/d) = floor(M*n/2^S) +1 , while n > 0
    //     int(n/d) = floor(n/d) = floor(M*n/2^S), while n < 0.
    // We implement this algorithm in the following way:
    // 1. multiply magic number m and numerator n, get the higher 32bit result in EDX
    // 2. if divisor > 0 and magic < 0, add numerator to EDX
    //    if divisor < 0 and magic > 0, sub numerator to EDX
    // 3. if S !=0, SAR S bits for EDX
    // 4. add 1 to EDX if EDX < 0
    // 5. Thus, EDX is the quotient

    // mov %eax, %tmp1
    // mov magic, %tmp2
    // imul %tmp2
    calculateMagicAndShift(divisor, &magic, &shift);

    move_reg_to_reg(OpndSize_32, PhysicalReg_EAX, true, 1, false);
    move_imm_to_reg(OpndSize_32, magic, 2, false);
    alu_unary_reg(OpndSize_32, imul_opc, 2, false);

    // divisor > 0 && M < 0
    if (divisor > 0 && magic < 0)
    {
        alu_binary_reg_reg(OpndSize_32, add_opc, 1, false, PhysicalReg_EDX, true);
    }
    else
    {
        // divisor < 0 && M > 0
        if (divisor < 0 && magic > 0)
        {
            alu_binary_reg_reg(OpndSize_32, sub_opc, 1, false, PhysicalReg_EDX, true);
        }
    }

    // sarl shift, %edx
    if (shift != 0)
    {
        alu_binary_imm_reg(OpndSize_32, sar_opc, shift, PhysicalReg_EDX, true);
    }

    // mov %edx, %eax
    // shrl 31, %edx
    // add %edx, %eax
    move_reg_to_reg(OpndSize_32, PhysicalReg_EDX, true, PhysicalReg_EAX, true);
    alu_binary_imm_reg(OpndSize_32, shr_opc, 31, PhysicalReg_EDX, true);
    alu_binary_reg_reg(OpndSize_32, add_opc, PhysicalReg_EDX, true, PhysicalReg_EAX, true);

    // for REM operation
    if (isRem == true)
    {
        //mov v2, %tmp2
        //imul %tmp2
        //sub %eax, %tmp1
        //mov %tmp1, edx
        move_imm_to_reg(OpndSize_32, divisor, 2, false);
        alu_unary_reg(OpndSize_32, imul_opc, 2, false);
        alu_binary_reg_reg(OpndSize_32, sub_opc, PhysicalReg_EAX, true, 1, false);
        move_reg_to_reg(OpndSize_32, 1, false, PhysicalReg_EDX, true);
    }
}

#define P_GPR_1 PhysicalReg_EBX
/**
 * @brief common code to handle integer DIV & REM, it used GPR
 *  If the divisor is a constant at compiler time, use the algorithm from "Hacker's Delight", Henry S.
 *  Warren, Jr., chapter 10. to simplify the code.
 *  The special case: when op0 == minint && op1 == -1, return 0 for isRem, return 0x80000000 for isDiv
 *  There are four merge points in the control flow for this bytecode
 *  make sure the reg. alloc. state is the same at merge points by calling transferToState
 * @param mir bytecode representation
 * @param isRem true for REM, false for DIV
 * @param vA the destination VR
 * @param v1 the source VR for numerator
 * @param v2 the source VR for divisor
 * @return value >= 0 when handled
 */
static int common_div_rem_int(const MIR* mir, bool isRem, int vA, int v1, int v2) {

    // Handle the case where the divisor is a constant at compile time
    int divisor[2];
    int isConst = isVirtualRegConstant(v2, LowOpndRegType_gp, divisor, false);

    // check if vr is constant in method scope
    if (isConst != VR_IS_CONSTANT)
    {
        u8 value;

        // Get constant info with method context, value is passed as reference to return constant value of the v2
        ConstVRType isConstVRContext = dvmCompilerGetConstValueOfVR(mir, v2, value);

        // if VR is recognized as a non-wide constant with method context
        if (isConstVRContext == kVRNonWideConst)
        {
#ifdef DIVREM_BYTECODE_VERBOSE
            ALOGD("Method level constant recognized for virtual register v%d in DIV or REM bytecode with value %lu", v2, value);
#endif
            isConst = VR_IS_CONSTANT;
            divisor[0] = (int)value;
        }
    }

    // if divisor is constant and not a -1
    // For now we just use the generic code generation for division by -1
    if (isConst == VR_IS_CONSTANT && divisor[0] != -1)
    {
        // if divisor is 0
        if (divisor[0] == 0)
        {
            export_pc(); //use %edx
            beforeCall("exception"); //dump GG, GL VRs
            unconditional_jump("common_errDivideByZero", false);
            return 0;
        }

        // Get numerator to EAX
        get_virtual_reg(v1, OpndSize_32, PhysicalReg_EAX, true);
        move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EDX, true);

        //Check if numerator is 0
        compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
        rememberState(2);
        conditional_jump(Condition_Z, ".common_div_rem_int_divdone", true);

        // There is nothing to do with division by 1
        if (divisor[0] != 1)
        {
            common_div_to_mul(divisor[0], isRem);
        }
    }
    else
    {
        //It is a general case. Both divisor and numerator are variables.
        get_virtual_reg(v1, OpndSize_32, PhysicalReg_EAX, true);
        move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EDX, true);
        get_virtual_reg(v2, OpndSize_32, 2, false);

        // Handle the div 0 case
        compare_imm_reg(OpndSize_32, 0, 2, false);
        handlePotentialException(
                                       Condition_E, Condition_NE,
                                       1, "common_errDivideByZero");

        //Check if numerator is 0
        compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
        rememberState(2);
        conditional_jump(Condition_Z, ".common_div_rem_int_divdone", true);

        transferToState(1);

        //Find out Numerator | Denominator
        move_reg_to_reg(OpndSize_32, PhysicalReg_EAX, true, 3, false);
        alu_binary_reg_reg(OpndSize_32, or_opc, 2, false, 3, false);

        //If both arguments are less than 8-bits (and positive), do 8-bit divide
        test_imm_reg(OpndSize_32, 0xFFFFFF00, 3, false);

        rememberState(3);
        conditional_jump(Condition_Z, ".common_div_rem_int_8", true);

        //If both arguments are less than 16-bits (and positive), do 16-bit divide
        test_imm_reg(OpndSize_32, 0xFFFF0000, 3, false);
        conditional_jump(Condition_Z, ".common_div_rem_int_16", true);

        //Handle special cases:
        //0x80000000 / -1 should result in a quotient of 0x80000000
        //and a remainder of 0.
        //Check for -1:
        compare_imm_reg(OpndSize_32, -1, 2, false);
        rememberState(4);
        conditional_jump(Condition_NE, ".common_div_rem_int_32", true);
        //Check for 0x80000000 (MinInt)
        compare_imm_reg(OpndSize_32, 0x80000000, PhysicalReg_EAX, true);
        //Special case, no division is needed.
        //We set the quotient to 0x800000000 (EAX is already that),
        //and remainder to 0
        transferToState(2);
        conditional_jump(Condition_E, ".common_div_rem_int_divdone", true);


        goToState(4);
        if (insertLabel(".common_div_rem_int_32", true) == -1) //merge point
            return -1;
        convert_integer(OpndSize_32, OpndSize_64); //cdq
        //idiv: dividend in edx:eax; quotient in eax; remainder in edx
        alu_unary_reg(OpndSize_32, idiv_opc, 2, false);
        transferToState(2);
        unconditional_jump(".common_div_rem_int_divdone", true);

        //Do 8-bit unsigned divide:
        //div: dividend in ax; quotient in al; remainder in ah
        //We are forced to use a hard-coded register, since the register allocator
        //can allocate a register not capable of 8-bit operation, like ESI,
        //which will cause undefined behaviour.
        goToState(3);
        if (insertLabel(".common_div_rem_int_8", true) == -1)
            return -1;
        move_reg_to_reg(OpndSize_32, 2, false, 4, false);
        alu_unary_reg(OpndSize_8, div_opc, 4, false);
        if (isRem) {
            move_reg_to_reg(OpndSize_32, PhysicalReg_EAX, true, PhysicalReg_EDX, true);
            alu_binary_imm_reg(OpndSize_32, shr_opc, 8, PhysicalReg_EDX, true);
        } else
            alu_binary_imm_reg(OpndSize_32, and_opc, 0x000000FF, PhysicalReg_EAX, true);
        transferToState(2);
        unconditional_jump(".common_div_rem_int_divdone", true);

        //Do 16-bit divide:
        //div: dividend in dx:ax; quotient in ax; remainder in dx
        goToState(3);
        if (insertLabel(".common_div_rem_int_16", true) == -1)
            return -1;
        alu_unary_reg(OpndSize_16, div_opc, 2, false);
    }

    transferToState(2);
    if (insertLabel(".common_div_rem_int_divdone", true) == -1)
        return -1;
    if(isRem)
        set_virtual_reg(vA, OpndSize_32, PhysicalReg_EDX, true);
    else //divide: quotient in %eax
        set_virtual_reg(vA, OpndSize_32, PhysicalReg_EAX, true);

    return 0;
}
#undef P_GPR_1

/**
 * @brief Generate native code for bytecode div-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_div_rem_int(mir, false, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_INT);
    int vA, v1, v2;
    vA = mir->dalvikInsn.vA;
    v1 = mir->dalvikInsn.vB;
    v2 = mir->dalvikInsn.vC;
    int retval = common_div_rem_int(mir, true, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode div-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_INT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_div_rem_int(mir, false, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-int/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_int_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_INT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_div_rem_int(mir, true, vA, v1, v2);
    return retval;
}

#define P_GPR_1 PhysicalReg_EBX
/**
 * @brief Common function to handle alu operations involving literals
 * @param opc The Opcode to perform
 * @param vA The destination VR
 * @param vB The source VR
 * @param imm The literal value
 * @return value >= 0 when handled
 */
int common_alu_int_lit(ALU_Opcode opc, int vA, int vB, s2 imm) { //except div and rem
    // For add and sub, try if we can operate directly on VRs
    if ((opc == add_opc) || (opc == sub_opc)) {
        bool success = alu_imm_to_VR(OpndSize_32, opc, vB, vA, imm, 1, false, NULL);
        //If succeeded, we are done
        if (success == true) {
            return 0;
        }
        //Otherwise, go the normal path
    }

    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_binary_imm_reg(OpndSize_32, opc, imm, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}

//! calls common_alu_int_lit
int common_shift_int_lit(ALU_Opcode opc, int vA, int vB, s2 imm) {
    return common_alu_int_lit(opc, vA, vB, imm);
}
#undef p_GPR_1

/**
 * @brief Generate native code for bytecode add-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(add_opc, vA, vB, literal);
    return retval;
}

int alu_rsub_int(ALU_Opcode opc, int vA, s2 imm, int vB) {
    move_imm_to_reg(OpndSize_32, imm, 2, false);
    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_binary_reg_reg(OpndSize_32, opc, 1, false, 2, false);
    set_virtual_reg(vA, OpndSize_32, 2, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode rsub-int
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rsub_int(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_RSUB_INT);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = alu_rsub_int(sub_opc, vA, literal, vB);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(imul_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode and-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_and_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_AND_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(and_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode or-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_or_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_OR_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(or_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode xor-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_xor_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_XOR_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(xor_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode add-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;

    //try if we can operate directly on VRs
    bool success = alu_imm_to_VR(OpndSize_32, add_opc, vB, vA, literal, 1, false, mir);

    //If succeeded, we are done
    if (success == true) {
        return 0;
    }

    //Otherwise, go the normal path
    get_virtual_reg(vB, OpndSize_32, 1, false);
    alu_binary_imm_reg(OpndSize_32, add_opc, literal, 1, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode rsub-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rsub_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_RSUB_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = alu_rsub_int(sub_opc, vA, literal, vB);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(imul_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode and-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_and_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_AND_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(and_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode or-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_or_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_OR_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(or_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode xor-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_xor_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_XOR_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_alu_int_lit(xor_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode shl-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shl_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHL_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_shift_int_lit(shl_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode shr-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shr_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHR_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_shift_int_lit(sar_opc, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode ushr-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_ushr_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_USHR_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_shift_int_lit(shr_opc, vA, vB, literal);
    return retval;
}

int isPowerOfTwo(int imm) {
    int i;
    for(i = 1; i < 17; i++) {
        if(imm == (1 << i)) return i;
    }
    return -1;
}

#define P_GPR_1 PhysicalReg_EBX
int div_lit_strength_reduction(int vA, int vB, s2 imm) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        //strength reduction for div by 2,4,8,...
        int power = isPowerOfTwo(imm);
        if(power < 1) return 0;
        //tmp2 is not updated, so it can share with vB
        get_virtual_reg(vB, OpndSize_32, 2, false);
        //if imm is 2, power will be 1
        if(power == 1) {
            /* mov tmp1, tmp2
               shrl $31, tmp1
               addl tmp2, tmp1
               sarl $1, tmp1 */
            move_reg_to_reg(OpndSize_32, 2, false, 1, false);
            alu_binary_imm_reg(OpndSize_32, shr_opc, 31, 1, false);
            alu_binary_reg_reg(OpndSize_32, add_opc, 2, false, 1, false);
            alu_binary_imm_reg(OpndSize_32, sar_opc, 1, 1, false);
            set_virtual_reg(vA, OpndSize_32, 1, false);
            return 1;
        }
        //power > 1
        /* mov tmp1, tmp2
           sarl $power-1, tmp1
           shrl 32-$power, tmp1
           addl tmp2, tmp1
           sarl $power, tmp1 */
        move_reg_to_reg(OpndSize_32, 2, false, 1, false);
        alu_binary_imm_reg(OpndSize_32, sar_opc, power-1, 1, false);
        alu_binary_imm_reg(OpndSize_32, shr_opc, 32-power, 1, false);
        alu_binary_reg_reg(OpndSize_32, add_opc, 2, false, 1, false);
        alu_binary_imm_reg(OpndSize_32, sar_opc, power, 1, false);
        set_virtual_reg(vA, OpndSize_32, 1, false);
        return 1;
    }
    return 0;
}

/**
 * @brief common code to handle integer DIV & REM with literal, it uses GPR
 * @details If the constant divisor is a constant at compile time, use the
 *  same algorithm and the implementation as the one used in integer DIV & REM
 *  to use multiplication to replace division to save the cost.
 * @param isRem true for REM, false for DIV
 * @param vA the destination VR
 * @param vB the source VR for numerator
 * @param imm the constant divisor
 * @return value >= 0 when handled
 */
int common_div_rem_int_lit(bool isRem, int vA, int vB, s2 imm) {
    if(!isRem) {
        int retCode = div_lit_strength_reduction(vA, vB, imm);
        if(retCode > 0) return 0;
    }
    if(imm == 0) {
        export_pc(); //use %edx
#ifdef DEBUG_EXCEPTION
        ALOGI("EXTRA code to handle exception");
#endif
        beforeCall("exception"); //dump GG, GL VRs
        unconditional_jump ("common_errDivideByZero", false);

        return 0;
    }
    get_virtual_reg(vB, OpndSize_32, PhysicalReg_EAX, true);

    // zero EDX
    alu_binary_reg_reg(OpndSize_32, xor_opc, PhysicalReg_EDX, true, PhysicalReg_EDX, true);

    //check against -1 for DIV_INT??
    if(imm == -1) {
        compare_imm_reg(OpndSize_32, 0x80000000, PhysicalReg_EAX, true);
        conditional_jump(Condition_E, ".div_rem_int_lit_special", true);
        rememberState(1);
    }

    // if immediate is not -1
    if (imm != -1)
    {
        // if immediate is 1, no need to do the div/rem operation
        if (imm != 1)
        {
            common_div_to_mul(imm, isRem);
        }
    }

    // handling the -1 case.
    else
    {
        move_imm_to_reg(OpndSize_32, imm, 2, false);
        convert_integer(OpndSize_32, OpndSize_64); //cdq

        //idiv: dividend in edx:eax; quotient in eax; remainder in edx
        alu_unary_reg(OpndSize_32, idiv_opc, 2, false);
    }

    if(isRem)
        set_virtual_reg(vA, OpndSize_32, PhysicalReg_EDX, true);
    else
        set_virtual_reg(vA, OpndSize_32, PhysicalReg_EAX, true);

    if(imm == -1) {
        unconditional_jump(".div_rem_int_lit_okay", true);
        rememberState(2);

        if (insertLabel(".div_rem_int_lit_special", true) == -1)
            return -1;
        goToState(1);
        if(isRem)
            set_VR_to_imm(vA, OpndSize_32, 0);
        else
            set_VR_to_imm(vA, OpndSize_32, 0x80000000);
        transferToState(2);
    }

    if (insertLabel(".div_rem_int_lit_okay", true) == -1)
        return -1; //merge point 2
    return 0;
}
#undef P_GPR_1

/**
 * @brief Generate native code for bytecode div-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_div_rem_int_lit(false, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-int/lit16
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_int_lit16(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_INT_LIT16);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_div_rem_int_lit(true, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode div-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_div_rem_int_lit(false, vA, vB, literal);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-int/lit8
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_int_lit8(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_INT_LIT8);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;
    s2 literal = mir->dalvikInsn.vC;
    int retval = common_div_rem_int_lit(true, vA, vB, literal);
    return retval;
}
//! common code to hanle long ALU ops

//! It uses XMM
//!all logical operations and sub operation of long type (no add mul div or rem)
int common_alu_long(ALU_Opcode opc, int vA, int v1, int v2) {
    int value[2];
    int isConst = isVirtualRegConstant(v2, LowOpndRegType_xmm, value, false);

    get_virtual_reg(v1, OpndSize_64, 1, false);
    if (isConst == 3) {                                           //operate on constants stored in code stream
        alu_binary_VR_reg(OpndSize_64, opc, v2, 1, false);        //opc const, XMM
    } else {
        get_virtual_reg(v2, OpndSize_64, 2, false);               //operate on XMM registers
        alu_binary_reg_reg(OpndSize_64, opc, 2, false, 1, false); //opc XMM, XMM
    }
    set_virtual_reg(vA, OpndSize_64, 1, false);
    return 0;
}

//! Use general purpose registers during the lowering for add-long and add-long/2addr
int common_add_long(int vA, int v1, int v2) {
    get_virtual_reg(v1, OpndSize_32, 1, false);
    get_virtual_reg(v1+1, OpndSize_32, 2, false);
    alu_binary_VR_reg(OpndSize_32, add_opc, v2, 1, false);
    alu_binary_VR_reg(OpndSize_32, adc_opc, (v2+1), 2, false);
    set_virtual_reg(vA, OpndSize_32, 1, false);
    set_virtual_reg(vA+1, OpndSize_32, 2, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode add-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_add_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_long(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode and-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_and_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_AND_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_long(and_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode or-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_or_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_OR_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_long(or_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode xor-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_xor_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_XOR_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_long(xor_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode add-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_add_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_long(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode and-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_and_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_AND_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_long(and_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode or-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_or_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_OR_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_long(or_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode xor-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_xor_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_XOR_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_long(xor_opc, vA, v1, v2);
    return retval;
}

//signed vs unsigned imul and mul?
//! common code to handle multiplication of long

//! It uses GPR
int common_mul_long(int vA, int v1, int v2) {
    get_virtual_reg(v2, OpndSize_32, 1, false);
    move_reg_to_reg(OpndSize_32, 1, false, PhysicalReg_EAX, true);
    //imul: 2L * 1H update temporary 1
    alu_binary_VR_reg(OpndSize_32, imul_opc, (v1+1), 1, false);
    get_virtual_reg(v1, OpndSize_32, 3, false);
    move_reg_to_reg(OpndSize_32, 3, false, 2, false);
    //imul: 1L * 2H
    alu_binary_VR_reg(OpndSize_32, imul_opc, (v2+1), 2, false);
    alu_binary_reg_reg(OpndSize_32, add_opc, 2, false, 1, false);
    alu_unary_reg(OpndSize_32, mul_opc, 3, false);
    alu_binary_reg_reg(OpndSize_32, add_opc, PhysicalReg_EDX, true, 1, false);
    set_virtual_reg(vA+1, OpndSize_32, 1, false);
    set_virtual_reg(vA, OpndSize_32, PhysicalReg_EAX, true);
    return 0;
}

//! common code to handle multiplication when multiplicands of long type are the same

//! It uses GPR
int common_mul_long_square(int vA, int v1) {
    get_virtual_reg(v1, OpndSize_32, 1, false);
    move_reg_to_reg(OpndSize_32, 1, false, PhysicalReg_EAX, true);
    move_reg_to_reg(OpndSize_32,1, false, PhysicalReg_EDX, true);
    //imul: 1L * 1H update temporary 1.
    //same as 2L * 1H or 1L * 2H, thus eliminating need for second imul.
    alu_binary_VR_reg(OpndSize_32, imul_opc, (v1+1), 1, false);
    alu_binary_reg_reg(OpndSize_32, add_opc, 1, false, 1, false);
    alu_unary_reg(OpndSize_32, mul_opc, PhysicalReg_EDX, true);
    alu_binary_reg_reg(OpndSize_32, add_opc, PhysicalReg_EDX, true, 1, false);
    set_virtual_reg(vA+1, OpndSize_32, 1, false);
    set_virtual_reg(vA, OpndSize_32, PhysicalReg_EAX, true);
    return 0;
}

/**
 * @brief Generate native code for bytecode mul-long
 * @details when multiplicands are same, use special case for square
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval;
    if (v1 != v2){
      retval = common_mul_long(vA, v1, v2);
    }
    else{
      retval = common_mul_long_square(vA, v1);
    }
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-long/2addr
 * @details when multiplicands are same, use special case for square
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval;
    if (v1 != v2){
      retval = common_mul_long(vA, v1, v2);
    }
    else{
      retval = common_mul_long_square(vA, v1);
    }
    return retval;
}

//! common code to handle DIV & REM of long

//! It uses GPR & XMM; and calls call_moddi3 & call_divdi3
int common_div_rem_long(bool isRem, int vA, int v1, int v2) {
    get_virtual_reg(v2, OpndSize_32, 1, false);
    get_virtual_reg(v2+1, OpndSize_32, 2, false);
    //save to native stack before changing register 1, esp-8 is unused area
    move_reg_to_mem(OpndSize_32, 1, false, 8-16, PhysicalReg_ESP, true);
    alu_binary_reg_reg(OpndSize_32, or_opc, 2, false, 1, false);

    handlePotentialException(
                                       Condition_E, Condition_NE,
                                       1, "common_errDivideByZero");
    load_effective_addr(-16, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, 2, false, 12, PhysicalReg_ESP, true);
    get_virtual_reg(v1, OpndSize_64, 1, false);
    move_reg_to_mem(OpndSize_64, 1, false, 0, PhysicalReg_ESP, true);
    scratchRegs[0] = PhysicalReg_SCRATCH_1;
    nextVersionOfHardReg(PhysicalReg_EDX, 2); //next version has 2 refs
    if(isRem)
        call_moddi3();
    else
        call_divdi3();
    load_effective_addr(16, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    set_virtual_reg(vA+1, OpndSize_32,PhysicalReg_EDX, true);
    set_virtual_reg(vA, OpndSize_32, PhysicalReg_EAX, true);
    return 0;
}

/**
 * @brief Generate native code for bytecode div-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_div_rem_long(false, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_div_rem_long(true, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode div-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_div_rem_long(false, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_div_rem_long(true, vA, v1, v2);
    return retval;
}

//! common code to handle SHL long

//! It uses XMM
int common_shl_long(int vA, int v1, int v2) {
    get_VR_ss(v2, 2, false);
    get_virtual_reg(v1, OpndSize_64, 1, false);

    int value[2];
    int isConst = isVirtualRegConstant(v2, LowOpndRegType_gp, value, false); //do not update refCount
    if (isConst == 3) {                          // case where shift amount is available
        int shiftImm = (0x3f) & (value[0]); // compute masked shift amount statically
        alu_binary_imm_reg(OpndSize_64, sll_opc, shiftImm, 1, false);
    } else {                                // case where shift count to be read from VR
        load_global_data_API("shiftMask", OpndSize_64, 3, false);
        alu_binary_reg_reg(OpndSize_64, and_opc, 3, false, 2, false);
        alu_binary_reg_reg(OpndSize_64, sll_opc, 2, false, 1, false);
    }
    set_virtual_reg(vA, OpndSize_64, 1, false);
    return 0;
}

//! common code to handle SHR long

//! It uses XMM
int common_shr_long(int vA, int v1, int v2) {
    get_VR_ss(v2, 2, false);

    load_global_data_API("shiftMask", OpndSize_64, 3, false);

    get_virtual_reg(v1, OpndSize_64, 1, false);
    alu_binary_reg_reg(OpndSize_64, and_opc, 3, false, 2, false);
    alu_binary_reg_reg(OpndSize_64, srl_opc, 2, false, 1, false);
    compare_imm_VR(OpndSize_32, 0, (v1+1));
    conditional_jump(Condition_GE, ".common_shr_long_special", true);
    rememberState(1);

    load_global_data_API("value64", OpndSize_64, 4, false);

    alu_binary_reg_reg(OpndSize_64, sub_opc, 2, false, 4, false);

    load_global_data_API("64bits", OpndSize_64, 5, false);

    alu_binary_reg_reg(OpndSize_64, sll_opc, 4, false, 5, false);
    alu_binary_reg_reg(OpndSize_64, or_opc, 5, false, 1, false);
    rememberState(2);
    //check whether the target is next instruction TODO
    unconditional_jump(".common_shr_long_done", true);

    if (insertLabel(".common_shr_long_special", true) == -1)
        return -1;
    goToState(1);
    transferToState(2);
    if (insertLabel(".common_shr_long_done", true) == -1)
        return -1;
    set_virtual_reg(vA, OpndSize_64, 1, false);
    return 0;
}

//! common code to handle USHR long

//! It uses XMM
int common_ushr_long(int vA, int v1, int v2) {
    get_VR_sd(v1, 1, false);
    get_VR_ss(v2, 2, false);

    int value[2];
    int isConst = isVirtualRegConstant(v2, LowOpndRegType_gp, value, false); //do not update refCount
    if (isConst == 3) {                     // case where shift amount is available
        int shiftImm = (0x3f) & (value[0]); // compute masked shift amount statically
        alu_binary_imm_reg(OpndSize_64, srl_opc, shiftImm, 1, false);
    } else {                                // case where shift count to be read from VR
        load_sd_global_data_API("shiftMask", 3, false);
        alu_binary_reg_reg(OpndSize_64, and_opc, 3, false, 2, false);
        alu_binary_reg_reg(OpndSize_64, srl_opc, 2, false, 1, false);
    }
    set_VR_sd(vA, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode shl-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shl_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHL_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_shl_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shl-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shl_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHL_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_shl_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shr-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shr_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHR_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_shr_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode shr-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_shr_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SHR_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_shr_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode ushr-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_ushr_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_USHR_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_ushr_long(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode ushr-long/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_ushr_long_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_USHR_LONG_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_ushr_long(vA, v1, v2);
    return retval;
}
#define USE_MEM_OPERAND
///////////////////////////////////////////
//! common code to handle ALU of floats

//! It uses XMM
int common_alu_float(ALU_Opcode opc, int vA, int v1, int v2) {//add, sub, mul
    get_VR_ss(v1, 1, false);
#ifdef USE_MEM_OPERAND
    alu_sd_binary_VR_reg(opc, v2, 1, false, false/*isSD*/);
#else
    get_VR_ss(v2, 2, false);
    alu_ss_binary_reg_reg(opc, 2, false, 1, false);
#endif
    set_VR_ss(vA, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode add-float
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_float(add_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-float
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_float(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-float
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_float(mul_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode add-float/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_float_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_FLOAT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_float(add_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-float/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_float_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_FLOAT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_float(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-float/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_float_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_FLOAT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_float(mul_opc, vA, v1, v2);
    return retval;
}
//! common code to handle DIV of float

//! It uses FP stack
int common_div_float(int vA, int v1, int v2) {
    load_fp_stack_VR(OpndSize_32, v1); //flds
    fpu_VR(div_opc, OpndSize_32, v2);
    store_fp_stack_VR(true, OpndSize_32, vA); //fstps
    return 0;
}

/**
 * @brief Generate native code for bytecode div-float
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_float(div_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode div-float/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_float_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_FLOAT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_float(div_opc, vA, v1, v2);
    return retval;
}
//! common code to handle DIV of double

//! It uses XMM
int common_alu_double(ALU_Opcode opc, int vA, int v1, int v2) {//add, sub, mul
    get_VR_sd(v1, 1, false);
#ifdef USE_MEM_OPERAND
    alu_sd_binary_VR_reg(opc, v2, 1, false, true /*isSD*/);
#else
    get_VR_sd(v2, 2, false);
    alu_sd_binary_reg_reg(opc, 2, false, 1, false);
#endif
    set_VR_sd(vA, 1, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode add-double
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_double(add_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-double
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_double(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-double
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_double(mul_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode add-double/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_add_double_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_ADD_DOUBLE_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_double(add_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode sub-double/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_sub_double_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_SUB_DOUBLE_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_double(sub_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode mul-double/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_mul_double_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_MUL_DOUBLE_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_double(mul_opc, vA, v1, v2);
    return retval;
}
//! common code to handle DIV of double

//! It uses FP stack
int common_div_double(int vA, int v1, int v2) {
    load_fp_stack_VR(OpndSize_64, v1); //fldl
    fpu_VR(div_opc, OpndSize_64, v2); //fdivl
    store_fp_stack_VR(true, OpndSize_64, vA); //fstpl
    return 0;
}

/**
 * @brief Generate native code for bytecode div-double
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_alu_double(div_opc, vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode div-double/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_div_double_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_DIV_DOUBLE_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_alu_double(div_opc, vA, v1, v2);
    return retval;
}
#define P_GPR_1 PhysicalReg_EBX
#define P_GPR_2 PhysicalReg_ECX
//! common code to handle REM of float

//! It uses GPR & calls call_fmodf
int common_rem_float(int vA, int v1, int v2) {
    get_virtual_reg(v1, OpndSize_32, 1, false);
    get_virtual_reg(v2, OpndSize_32, 2, false);
    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, 1, false, 0, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, 2, false, 4, PhysicalReg_ESP, true);
    scratchRegs[0] = PhysicalReg_SCRATCH_1;
    call_fmodf(); //(float x, float y) return float
    load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    store_fp_stack_VR(true, OpndSize_32, vA); //fstps
    return 0;
}
#undef P_GPR_1
#undef P_GPR_2

/**
 * @brief Generate native code for bytecode rem-float
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_rem_float(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-float/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_float_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_FLOAT_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_rem_float(vA, v1, v2);
    return retval;
}
//! common code to handle REM of double

//! It uses XMM & calls call_fmod
int common_rem_double(int vA, int v1, int v2) {
    get_virtual_reg(v1, OpndSize_64, 1, false);
    get_virtual_reg(v2, OpndSize_64, 2, false);
    load_effective_addr(-16, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_64, 1, false, 0, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_64, 2, false, 8, PhysicalReg_ESP, true);
    scratchRegs[0] = PhysicalReg_SCRATCH_1;
    call_fmod(); //(long double x, long double y) return double
    load_effective_addr(16, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    store_fp_stack_VR(true, OpndSize_64, vA); //fstpl
    return 0;
}

/**
 * @brief Generate native code for bytecode rem-double
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    int retval = common_rem_double(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode rem-double/2addr
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_rem_double_2addr(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_REM_DOUBLE_2ADDR);
    int vA = mir->dalvikInsn.vA;
    int v1 = vA;
    int v2 = mir->dalvikInsn.vB;
    int retval = common_rem_double(vA, v1, v2);
    return retval;
}

/**
 * @brief Generate native code for bytecode cmpl-float
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_cmpl_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_CMPL_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    get_VR_ss(v1, 1, false); //xmm
    move_imm_to_reg(OpndSize_32, 0, 1, false);
    move_imm_to_reg(OpndSize_32, 1, 2, false);
    move_imm_to_reg(OpndSize_32, 0xffffffff, 3, false);
    compare_VR_ss_reg(v2, 1, false);
    //default: 0xffffffff??
    move_imm_to_reg(OpndSize_32,
                                 0xffffffff, 4, false);
    //ORDER of cmov matters !!! (Z,P,A)
    //finalNaN: unordered 0xffffffff
    conditional_move_reg_to_reg(OpndSize_32, Condition_Z,
                                             1, false, 4, false);
    conditional_move_reg_to_reg(OpndSize_32, Condition_P,
                                             3, false, 4, false);
    conditional_move_reg_to_reg(OpndSize_32, Condition_A,
                                             2, false, 4, false);
    set_virtual_reg(vA, OpndSize_32, 4, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode "cmpg-float vAA, vBB, vCC
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_cmpg_float(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_CMPG_FLOAT);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;

    //Operands are reversed here. Comparing vCC and vBB
    get_VR_ss(v2, 1, false);
    compare_VR_ss_reg(v1, 1, false);

    rememberState(1);

    //if vCC > vBB, jump to label ".cmp_float_less"
    conditional_jump(Condition_A, ".cmp_float_less", true);

    //if vCC < vBB, jump to label ".cmp_float_greater". Handles < and NaN
    conditional_jump(Condition_B, ".cmp_float_greater", true);

    //if vCC = vBB, move 0 to vAA
    set_VR_to_imm(vA, OpndSize_32, 0);

    rememberState(2);
    unconditional_jump(".cmp_float_done", true);

    // if vCC < vBB, i.e (if vBB > vCC) or if one of the operand is a NaN,  move +1 to vAA
    if (insertLabel(".cmp_float_greater", true) == -1)
       return -1;
    goToState(1);
    set_VR_to_imm(vA, OpndSize_32, 1);
    transferToState(2);
    unconditional_jump(".cmp_float_done", true);

    // if vCC > vBB, i.e (if vBB < vCC), move -1 to vAA
    if (insertLabel(".cmp_float_less", true) == -1)
       return -1;
    goToState(1);
    set_VR_to_imm(vA, OpndSize_32, 0xffffffff);
    transferToState(2);

    //cmpg_float handling over
    if (insertLabel(".cmp_float_done", true) == -1)
       return -1;
    return 0;
}

/**
 * @brief Generate native code for bytecode cmpl-double
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_cmpl_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_CMPL_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    get_VR_sd(v1, 1, false);
    compare_VR_sd_reg(v2, 1, false);
    move_imm_to_reg(OpndSize_32, 0, 1, false);
    move_imm_to_reg(OpndSize_32, 1, 2, false);
    move_imm_to_reg(OpndSize_32, 0xffffffff, 3, false);

    //default: 0xffffffff??
    move_imm_to_reg(OpndSize_32, 0xffffffff, 4, false);
    conditional_move_reg_to_reg(OpndSize_32, Condition_Z,
                                             1, false, 4, false);
    conditional_move_reg_to_reg(OpndSize_32, Condition_P,
                                             3, false, 4, false);
    conditional_move_reg_to_reg(OpndSize_32, Condition_A,
                                             2, false, 4, false);
    set_virtual_reg(vA, OpndSize_32, 4, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode cmpg-double vAA, vBB, vCC
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_cmpg_double(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_CMPG_DOUBLE);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;

    //Operands are reversed here. Comparing vCC and vBB
    get_VR_sd(v2, 1, false);
    compare_VR_sd_reg(v1, 1, false);

    rememberState(1);

    //if vCC > vBB, jump to label ".cmp_double_less"
    conditional_jump(Condition_A, ".cmp_double_less", true);

    //if vCC < vBB, jump to label ".cmp_double_greater". Handles < and NaN
    conditional_jump(Condition_B, ".cmp_double_greater", true);

    //if vCC = vBB, move 0 to vAA
    set_VR_to_imm(vA, OpndSize_32, 0);

    rememberState(2);
    unconditional_jump(".cmp_double_done", true);

    // if vCC < vBB, i.e (if vBB > vCC) or if one of the operand is a NaN, move +1 to vAA
    if (insertLabel(".cmp_double_greater", true) == -1)
       return -1;
    goToState(1);
    set_VR_to_imm(vA, OpndSize_32, 1);
    transferToState(2);
    unconditional_jump(".cmp_double_done", true);

    // if vCC > vBB, i.e (if vBB < vCC), move -1 to vAA
    if (insertLabel(".cmp_double_less", true) == -1)
       return -1;
    goToState(1);
    set_VR_to_imm(vA, OpndSize_32, 0xffffffff);
    transferToState(2);

    //cmpg_double handling over
    if (insertLabel(".cmp_double_done", true) == -1)
       return -1;
    return 0;
}

#define P_GPR_1 PhysicalReg_EBX
#define P_GPR_2 PhysicalReg_ECX
#define P_GPR_3 PhysicalReg_ESI
#define P_SCRATCH_1 PhysicalReg_EDX
#define P_SCRATCH_2 PhysicalReg_EAX

/**
 * @brief Generate native code for bytecode cmp-long
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_cmp_long(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_CMP_LONG);
    int vA = mir->dalvikInsn.vA;
    int v1 = mir->dalvikInsn.vB;
    int v2 = mir->dalvikInsn.vC;
    get_virtual_reg(v1+1, OpndSize_32, 2, false);

    //Compare higher 32 bits
    compare_VR_reg(OpndSize_32,v2+1, 2, false);
    rememberState(1);
    //If equal on higher 32 bits, goto compare of lower 32 bits
    conditional_jump(Condition_E, ".cmp_long_higher_32b_equal", true);
    //If less on higher 32 bits, it is less on 64 bits
    conditional_jump(Condition_L, ".cmp_long_higher_32b_less", true);
    //If greater on higher 32 bits, it is greater on 64 bits
    set_VR_to_imm(vA, OpndSize_32, 1);
    rememberState(2);
    unconditional_jump(".cmp_long_done", true);

    //If higher 32 bits are equal, compare lower 32 bits
    if (insertLabel(".cmp_long_higher_32b_equal",true) == -1)
       return -1;
    goToState(1);
    get_virtual_reg(v1, OpndSize_32, 1, false);

    //Compare lower 32 bits
    compare_VR_reg(OpndSize_32, v2, 1, false);
    rememberState(3);
    //Less on lower 32 bits
    conditional_jump(Condition_B, ".cmp_long_lower_32b_less", true);
    //Equal on lower 32 bits
    conditional_jump(Condition_E, ".cmp_long_lower_32b_equal", true);
    //Greater on lower 32 bits
    set_VR_to_imm(vA, OpndSize_32, 1);
    transferToState(2);
    unconditional_jump(".cmp_long_done", true);

    if (insertLabel(".cmp_long_higher_32b_less", true) == -1)
       return -1;
    goToState(1);
    set_VR_to_imm(vA, OpndSize_32, 0xffffffff);
    transferToState(2);
    unconditional_jump(".cmp_long_done", true);

    if (insertLabel(".cmp_long_lower_32b_less", true) == -1)
       return -1;
    goToState(3);
    set_VR_to_imm(vA, OpndSize_32, 0xffffffff);
    transferToState(2);
    unconditional_jump(".cmp_long_done", true);

    if (insertLabel(".cmp_long_lower_32b_equal", true) == -1)
       return -1;
    goToState(3);
    set_VR_to_imm(vA, OpndSize_32, 0);
    transferToState(2);

    if (insertLabel(".cmp_long_done", true) == -1)
       return -1;
    return 0;
}
#undef P_GPR_1
#undef P_GPR_2
#undef P_GPR_3
