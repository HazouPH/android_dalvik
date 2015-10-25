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


/*! \file LowerReturn.cpp
    \brief This file lowers the following bytecodes: RETURN

*/

#include "Lower.h"
#include "NcgHelper.h"

/**
 * @brief Generates jump to dvmJitHelper_returnFromMethod.
 * @details Uses one scratch register to make the jump
 * @return value 0 when successful
 */
static int jumpTocommon_returnFromMethod (void)
{
    //The stack save area is in negative direction relative to frame pointer so we calculate displacement now
    const int saveAreaDisp = -(sizeof(StackSaveArea));

    // Load save area into EDX
    load_effective_addr (saveAreaDisp, PhysicalReg_FP, true, PhysicalReg_EDX, true);

    // We may suffer from agen stall here due if edx is not ready
    // So instead of doing:
    //   movl offStackSaveArea_prevFrame(%edx), rFP
    // We can just compute directly
    //   movl (offStackSaveArea_prevFrame - sizeofStackSaveArea)(rFP), rFP
    move_mem_to_reg(OpndSize_32, OFFSETOF_MEMBER(StackSaveArea, prevFrame) + saveAreaDisp,
            PhysicalReg_FP, true, PhysicalReg_FP, true);

    //Jump to dvmJitHelper_returnFromMethod
    unconditional_jump_rel32 (reinterpret_cast<void *> (dvmJitHelper_returnFromMethod));

    //We return 0 as per our function definition when successful
    return 0;
}

/**
 * @brief Generate native code for bytecodes return-void
 * and return-void-barrier
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_return_void(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_RETURN_VOID
            || mir->dalvikInsn.opcode == OP_RETURN_VOID_BARRIER);

    // Put self pointer in ecx
    get_self_pointer(PhysicalReg_ECX, true);

    return jumpTocommon_returnFromMethod();
}

/**
 * @brief Generate native code for bytecodes return
 * and return-object
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_return(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_RETURN
            || mir->dalvikInsn.opcode == OP_RETURN_OBJECT);
    int vA = mir->dalvikInsn.vA;
    get_virtual_reg(vA, OpndSize_32, 1, false);

    set_return_value(OpndSize_32, 1, false, PhysicalReg_ECX, true);

    return jumpTocommon_returnFromMethod();
}

/**
 * @brief Generate native code for bytecode return-wide
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_return_wide(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_RETURN_WIDE);
    int vA = mir->dalvikInsn.vA;
    get_virtual_reg(vA, OpndSize_64, 1, false);

    set_return_value(OpndSize_64, 1, false, PhysicalReg_ECX, true);

    return jumpTocommon_returnFromMethod();
}
