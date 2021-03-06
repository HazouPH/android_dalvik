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


/*! \file LowerHelper.cpp
    \brief This file implements helper functions for lowering

With NCG O0: all registers are hard-coded ;
With NCG O1: the lowering module will use variables that will be allocated to a physical register by the register allocator.

register types: FS 32-bit or 64-bit;
                XMM: SS(32-bit) SD (64-bit);
                GPR: 8-bit, 16-bit, 32-bit;
LowOpndRegType tells whether it is gpr, xmm or fs;
OpndSize can be OpndSize_8, OpndSize_16, OpndSize_32, OpndSize_64

A single native instruction can use multiple physical registers.
  we can't call freeReg in the middle of emitting a native instruction,
  since it may free the physical register used by an operand and cause two operands being allocated to the same physical register.

When allocating a physical register for an operand, we can't spill the operands that are already allocated. To avoid that, we call startNativeCode before each native instruction, it resets the spill information to true for each physical register;
  when a physical register is allocated, we set its corresponding flag to false;
  at end of each native instruction, call endNativeCode to also reset the flags to true.
*/

#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "libdex/DexOpcodes.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "NcgAot.h"
#include "enc_wrapper.h"
#include "vm/mterp/Mterp.h"
#include "vm/mterp/common/FindInterface.h"
#include "NcgHelper.h"
#include <math.h>
#include "interp/InterpState.h"
#include "Scheduler.h"
#include "Singleton.h"
#include "ExceptionHandling.h"
#include "compiler/Dataflow.h"
#include "X86Common.h"

extern "C" int64_t __divdi3(int64_t, int64_t);
extern "C" int64_t __moddi3(int64_t, int64_t);
bool isScratchPhysical;

//4 tables are defined: GPR integer ALU ops, ALU ops in FPU, SSE 32-bit, SSE 64-bit
//the index to the table is the opcode
//add_opc,    or_opc,     adc_opc,    sbb_opc,
//and_opc,    sub_opc,    xor_opc,    cmp_opc,
//mul_opc,    imul_opc,   div_opc,    idiv_opc,
//sll_opc,    srl_opc,    sra, (SSE)
//shl_opc,    shr_opc,    sal_opc,    sar_opc, //integer shift
//neg_opc,    not_opc,    andn_opc, (SSE)
//n_alu
//!mnemonic for integer ALU operations
const  Mnemonic map_of_alu_opcode_2_mnemonic[] = {
    Mnemonic_ADD,  Mnemonic_OR,   Mnemonic_ADC,  Mnemonic_SBB,
    Mnemonic_AND,  Mnemonic_SUB,  Mnemonic_XOR,  Mnemonic_CMP,
    Mnemonic_MUL,  Mnemonic_IMUL, Mnemonic_DIV,  Mnemonic_IDIV,
    Mnemonic_Null, Mnemonic_Null, Mnemonic_Null,
    Mnemonic_SHL,  Mnemonic_SHR,  Mnemonic_SAL,  Mnemonic_SAR,
    Mnemonic_NEG,  Mnemonic_NOT,  Mnemonic_Null,
    Mnemonic_Null
};
//!mnemonic for ALU operations in FPU
const  Mnemonic map_of_fpu_opcode_2_mnemonic[] = {
    Mnemonic_FADD,  Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_FSUB,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_FMUL,  Mnemonic_Null,  Mnemonic_FDIV,  Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null
};
//!mnemonic for SSE 32-bit
const  Mnemonic map_of_sse_opcode_2_mnemonic[] = {
    Mnemonic_ADDSD,  Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null,   Mnemonic_SUBSD, Mnemonic_XORPD, Mnemonic_Null,
    Mnemonic_MULSD,  Mnemonic_Null,  Mnemonic_DIVSD,  Mnemonic_Null,
    Mnemonic_Null,   Mnemonic_Null,
    Mnemonic_Null,   Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null,   Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null
};
//!mnemonic for SSE 64-bit integer
const  Mnemonic map_of_64_opcode_2_mnemonic[] = {
    Mnemonic_PADDQ, Mnemonic_POR,   Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_PAND,  Mnemonic_PSUBQ, Mnemonic_PXOR,  Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_PSLLQ, Mnemonic_PSRLQ, Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,  Mnemonic_Null,
    Mnemonic_Null,  Mnemonic_Null,  Mnemonic_PANDN,
    Mnemonic_Null
};

//! \brief Simplifies update of LowOpndReg fields.
void set_reg_opnd(LowOpndReg* op_reg, int reg, bool isPhysical,
        LowOpndRegType type) {
    op_reg->regType = type;
    op_reg->regNum = reg;
    op_reg->isPhysical = isPhysical;
}

//! \brief Simplifies update of LowOpndMem fields when only base and
//! displacement is used.
void set_mem_opnd(LowOpndMem* mem, int disp, int base, bool isPhysical) {
    mem->m_disp.value = disp;
    mem->hasScale = false;
    mem->m_base.regType = LowOpndRegType_gp;
    mem->m_base.regNum = base;
    mem->m_base.isPhysical = isPhysical;
}

//! \brief Simplifies update of LowOpndMem fields when base, displacement, index,
//! and scaling is used.
void set_mem_opnd_scale(LowOpndMem* mem, int base, bool isPhysical, int disp,
        int index, bool indexPhysical, int scale) {
    mem->hasScale = true;
    mem->m_base.regType = LowOpndRegType_gp;
    mem->m_base.regNum = base;
    mem->m_base.isPhysical = isPhysical;
    mem->m_index.regNum = index;
    mem->m_index.isPhysical = indexPhysical;
    mem->m_disp.value = disp;
    mem->m_scale.value = scale;
}

//! \brief Return either LowOpndRegType_xmm or LowOpndRegType_gp
//! depending on operand size.
//! \param size
inline LowOpndRegType getTypeFromIntSize(OpndSize size) {
    //If we can fit in 32-bit, then assume we will use a GP register
    if (size <= OpndSize_32)
    {
        return LowOpndRegType_gp;
    }
    //Otherwise we must use an xmm register
    else
    {
        return LowOpndRegType_xmm;
    }
}

//! \brief Thin layer over encoder that makes scheduling decision and
//! is used for dumping instruction whose immediate is a target label.
//! \param m x86 mnemonic
//! \param size operand size
//! \param imm When scheduling is disabled, this is the actual immediate.
//! When scheduling is enabled, this is 0 because immediate has not been
//! generated yet.
//! \param label name of label for which we need to generate immediate for
//! using the label address.
//! \param isLocal Used to hint the distance from this instruction to label.
//! When this is true, it means that 8 bits should be enough.
inline LowOpLabel* lower_label(Mnemonic m, OpndSize size, int imm,
        const char* label, bool isLocal) {
    if (!gDvmJit.scheduling) {
        stream = encoder_imm(m, size, imm, stream);
        return NULL;
    }
    LowOpLabel * op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpLabel>();
    op->opCode = m;
    op->opCode2 = ATOM_NORMAL;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Label;
    op->numOperands = 1;
    snprintf(op->labelOpnd.label, LABEL_SIZE, "%s", label);
    op->labelOpnd.isLocal = isLocal;
    singletonPtr<Scheduler>()->updateUseDefInformation_imm(op);
    return op;
}

//! \brief Interface to encoder.
LowOpLabel* dump_label(Mnemonic m, OpndSize size, int imm, const char* label,
        bool isLocal) {
    return lower_label(m, size, imm, label, isLocal);
}

//! Used for dumping an instruction with a single immediate to the code stream
//! but the immediate is not yet known because the target MIR block still needs
//! code generated for it. This is only valid when scheduling is on.
//! \pre Instruction scheduling must be enabled
//! \param m x86 mnemonic
//! \param targetBlockId id of the MIR block
//! \param immediateNeedsAligned if the immediate in the instruction need to be aligned within 16B
LowOpBlock* dump_blockid_imm(Mnemonic m, int targetBlockId,
        bool immediateNeedsAligned) {
    assert(gDvmJit.scheduling && "Scheduling must be turned on before "
                "calling dump_blockid_imm");
    LowOpBlock* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpBlock>();
    op->opCode = m;
    op->opCode2 = ATOM_NORMAL;
    op->opndSrc.type = LowOpndType_BlockId;
    op->numOperands = 1;
    op->blockIdOpnd.value = targetBlockId;
    op->blockIdOpnd.immediateNeedsAligned = immediateNeedsAligned;
    singletonPtr<Scheduler>()->updateUseDefInformation_imm(op);
    return op;
}

//! \brief Thin layer over encoder that makes scheduling decision and
//! is used for dumping instruction with a known immediate.
//! \param m x86 mnemonic
//! \param size operand size
//! \param imm immediate
LowOpImm* lower_imm(Mnemonic m, OpndSize size, int imm) {
    if (!gDvmJit.scheduling) {
        stream = encoder_imm(m, size, imm, stream);
        return NULL;
    }
    LowOpImm* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpImm>();
    op->opCode = m;
    op->opCode2 = ATOM_NORMAL;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Imm;
    op->numOperands = 1;
    op->immOpnd.value = imm;
    singletonPtr<Scheduler>()->updateUseDefInformation_imm(op);
    return op;
}

//! \brief Interface to encoder.
LowOpImm* dump_imm(Mnemonic m, OpndSize size, int imm) {
    return lower_imm(m, size, imm);
}

//! \brief Used to update the immediate of an instruction already in the
//! code stream.
//! \warning This assumes that the instruction to update is already in the
//! code stream. If it is not, the VM will abort.
//! \param imm new immediate to use
//! \param codePtr pointer to location in code stream where the instruction
//! whose immediate needs updated
//! \param updateSecondOperand This is true when second operand needs updated
void dump_imm_update(int imm, char* codePtr, bool updateSecondOperand) {
    // These encoder call do not need to go through scheduler since they need
    // to be dumped at a specific location in code stream.
    if(updateSecondOperand)
        encoder_update_imm_rm(imm, codePtr);
    else // update first operand
        encoder_update_imm(imm, codePtr);
}

//! \brief Thin layer over encoder that makes scheduling decision and
//! is used for dumping instruction with a single memory operand.
//! \param m x86 mnemonic
//! \param m2 Atom pseudo-mnemonic
//! \param size operand size
//! \param disp displacement offset
//! \param base_reg physical register (PhysicalReg type) or a logical register
//! \param isBasePhysical notes if base_reg is a physical register. It must
//! be true when scheduling is enabled or else VM will abort.
LowOpMem* lower_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int disp,
        int base_reg, bool isBasePhysical) {
    if (!gDvmJit.scheduling) {
        stream = encoder_mem(m, size, disp, base_reg, isBasePhysical, stream);
        return NULL;
    }

    if (!isBasePhysical) {
        ALOGI("JIT_INFO: Base register not physical in lower_mem");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }
    LowOpMem* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpMem>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Mem;
    op->numOperands = 1;
    op->memOpnd.mType = MemoryAccess_Unknown;
    op->memOpnd.index = -1;
    set_mem_opnd(&(op->memOpnd), disp, base_reg, isBasePhysical);
    singletonPtr<Scheduler>()->updateUseDefInformation_mem(op);
    return op;
}

//! \brief Interface to encoder which includes register allocation
//! decision.
//! \details With NCG O1, call freeReg to free up physical registers,
//! then call registerAlloc to allocate a physical register for memory base
LowOpMem* dump_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int disp,
        int base_reg, bool isBasePhysical) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        freeReg(false);
        //type of the base is gpr
        int regAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true, true);
        return lower_mem(m, m2, size, disp, regAll, true /*isBasePhysical*/);
    } else {
        return lower_mem(m, m2, size, disp, base_reg, isBasePhysical);
    }
}

//!update fields of LowOp and generate a x86 instruction that takes a single reg operand
LowOpReg* lower_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        LowOpndRegType type, bool isPhysical) {
    if (!gDvmJit.scheduling) {
        stream = encoder_reg(m, size, reg, isPhysical, type, stream);
        return NULL;
    }

    if (!isPhysical) {
        ALOGI("JIT_INFO: Register not physical at lower_reg");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }
    LowOpReg* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpReg>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Reg;
    op->numOperands = 1;
    set_reg_opnd(&(op->regOpnd), reg, isPhysical, type);
    singletonPtr<Scheduler>()->updateUseDefInformation_reg(op);
    return op;
}

//!With NCG O1, wecall freeReg to free up physical registers, then call registerAlloc to allocate a physical register for the single operand
LowOpReg* dump_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        bool isPhysical, LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        freeReg(false);
        if (m == Mnemonic_MUL || m == Mnemonic_IMUL || m == Mnemonic_DIV
                || m == Mnemonic_IDIV) {
            //these four instructions use eax & edx implicitly
            touchEax();
            touchEdx();
        }
        int regAll = registerAlloc(type, reg, isPhysical, true);
        return lower_reg(m, m2, size, regAll, type, true /*isPhysical*/);
    } else {
        return lower_reg(m, m2, size, reg, type, isPhysical);
    }
}

LowOpReg* dump_reg_noalloc(Mnemonic m, OpndSize size, int reg, bool isPhysical,
        LowOpndRegType type) {
    return lower_reg(m, ATOM_NORMAL, size, reg, type, true /*isPhysical*/);
}

//! \brief Update fields of LowOp to generate an instruction with
//! two register operands
//!
//! \details For MOVZX and MOVSX, allows source and destination
//! operand sizes to be different, and fixes type to general purpose.
//! \param m x86 mnemonic
//! \param m2 Atom pseudo-mnemonic
//! \param size operand size
//! \param regSrc source register
//! \param isPhysical if regSrc is a physical register
//! \param regDest destination register
//! \param isPhysical2 if regDest is a physical register
//! \param type the register type. For MOVSX and MOVZX, type is fixed
//! as general purpose
//! \return a LowOp corresponding to the reg-reg operation
LowOpRegReg* lower_reg_to_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int regSrc,
        bool isPhysical, int regDest, bool isPhysical2, LowOpndRegType type) {

    OpndSize srcSize = size;
    OpndSize destSize = size;
    LowOpndRegType srcType = type;
    LowOpndRegType destType = type;

    //We may need to override the default size and type if src and dest can be
    //of different size / type, as follows:

    //For MOVSX and MOVZX, fix the destination size and type to 32-bit and GP
    //respectively. Note that this is a rigid requirement, and for now won't
    //allow, for example, MOVSX Sz8, Sz16
    if (m == Mnemonic_MOVZX || m == Mnemonic_MOVSX) {
        destSize = OpndSize_32;
    }
    //For CVTSI2SD or CVTSI2SS, the source needs to be fixed at 32-bit GP
    else if (m == Mnemonic_CVTSI2SD || m == Mnemonic_CVTSI2SS) {
        srcSize = OpndSize_32;
        srcType = LowOpndRegType_gp;
    }

    if (!gDvmJit.scheduling) {
        if (m == Mnemonic_FUCOMIP || m == Mnemonic_FUCOMI) {
            stream = encoder_compare_fp_stack(m == Mnemonic_FUCOMIP, regSrc - regDest,
                    size == OpndSize_64, stream);
        } else {
            stream = encoder_reg_reg_diff_sizes(m, srcSize, regSrc, isPhysical, destSize,
                    regDest, isPhysical2, destType, stream);
        }
        return NULL;
    }

    if (!isPhysical && !isPhysical2) {
        ALOGI("JIT_INFO: Registers not physical at lower_reg_to_reg");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }

    LowOpRegReg* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpRegReg>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = destSize;
    op->opndDest.type = LowOpndType_Reg;
    op->opndSrc.size = srcSize;
    op->opndSrc.type = LowOpndType_Reg;
    op->numOperands = 2;
    set_reg_opnd(&(op->regDest), regDest, isPhysical2, destType);
    set_reg_opnd(&(op->regSrc), regSrc, isPhysical, srcType);
    singletonPtr<Scheduler>()->updateUseDefInformation_reg_to_reg(op);

    return op;
}

//!update fields of LowOp and generate a x86 instruction that takes two reg operands

//!Here, both registers are physical
LowOpRegReg* dump_reg_reg_noalloc(Mnemonic m, OpndSize size, int reg,
        bool isPhysical, int reg2, bool isPhysical2, LowOpndRegType type) {
    return lower_reg_to_reg(m, ATOM_NORMAL, size, reg, true /*isPhysical*/, reg2,
            true /*isPhysical2*/, type);
}

//! \brief Check if we have a MOV instruction which can be redundant
//!
//! \details Checks if the Mnemonic is a MOV which can possibly be
//! optimized. For example, MOVSX %ax, %eax cannot be optimized, while
//! MOV %eax, %eax is a NOP, and can be treated as such.
//! \param m Mnemonic to check for
//! \return whether the move can possibly be optimized away
inline bool isMoveOptimizable(Mnemonic m) {
    return (m == Mnemonic_MOV || m == Mnemonic_MOVQ || m == Mnemonic_MOVSS
            || m == Mnemonic_MOVSD);
}

//!update fields of LowOp and generate a x86 instruction that takes two reg operands

//!here dst reg is already allocated to a physical reg
//! we should not spill the physical register for dst when allocating for src
LowOpRegReg* dump_reg_reg_noalloc_dst(Mnemonic m, OpndSize size, int reg,
        bool isPhysical, int reg2, bool isPhysical2, LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        //TODO should mark reg2 as written
        int regAll = registerAlloc(type, reg, isPhysical, true);
        /* remove move from one register to the same register */
        if (isMoveOptimizable(m) && regAll == reg2)
            return NULL;
        return lower_reg_to_reg(m, ATOM_NORMAL, size, regAll, true /*isPhysical*/,
                reg2, true /*isPhysical2*/, type);
    } else {
        return lower_reg_to_reg(m, ATOM_NORMAL, size, reg, isPhysical, reg2,
                isPhysical2, type);
    }
}

//!update fields of LowOp and generate a x86 instruction that takes two reg operands

//!here src reg is already allocated to a physical reg
LowOpRegReg* dump_reg_reg_noalloc_src(Mnemonic m, AtomOpCode m2, OpndSize size,
        int reg, bool isPhysical, int reg2, bool isPhysical2,
        LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        int regAll2;
        if(isMoveOptimizable(m) && checkTempReg2(reg2, type, isPhysical2, reg, -1)) { //dst reg is logical
            //only from get_virtual_reg_all
            regAll2 = registerAllocMove(reg2, type, isPhysical2, reg, true);
        } else {
            regAll2 = registerAlloc(type, reg2, isPhysical2, true, true);
            return lower_reg_to_reg(m, m2, size, reg, true /*isPhysical*/, regAll2,
                    true /*isPhysical2*/, type);
        }
    } else {
        return lower_reg_to_reg(m, m2, size, reg, isPhysical, reg2, isPhysical2,
                type);
    }
    return NULL;
}

//! \brief Wrapper around lower_reg_to_reg with reg allocation
//! \details Allocates both registers, checks for optimizations etc,
//! and calls lower_reg_to_reg
//! \param m The mnemonic
//! \param m2 The ATOM mnemonic type
//! \param srcSize Size of the source operand
//! \param srcReg The source register itself
//! \param isSrcPhysical Whether source is physical
//! \param srcType The type of source register
//! \param destSize Size of the destination operand
//! \param destReg The destination register itself
//! \param isDestPhysical Whether destination is physical
//! \param destType The type of destination register
//! \return The generated LowOp
LowOpRegReg* dump_reg_reg_diff_types(Mnemonic m, AtomOpCode m2, OpndSize srcSize,
        int srcReg, int isSrcPhysical, LowOpndRegType srcType, OpndSize destSize,
        int destReg, int isDestPhysical, LowOpndRegType destType) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        //reg is source if m is MOV
        freeReg(false);
        int regAll = registerAlloc(srcType, srcReg, isSrcPhysical, true);
        int regAll2;
        LowOpRegReg* op = NULL;
#ifdef MOVE_OPT2
        if(isMoveOptimizable(m) &&
                ((reg != PhysicalReg_EDI && srcReg != PhysicalReg_ESP && srcReg != PhysicalReg_EBP) || (!isSrcPhysical)) &&
                isDestPhysical == false) { //dst reg is logical
            //called from move_reg_to_reg
            regAll2 = registerAllocMove(regDest, destType, isDestPhysical, regAll, true);
        } else {
#endif
        //Do not spill regAll
        gCompilationUnit->setCanSpillRegister (regAll, false);

        regAll2 = registerAlloc(destType, destReg, isDestPhysical, true, true);

        // NOTE: The use of (destSize, destType) as THE (size, type) can be confusing. In most
        // cases, we are using this function through dump_reg_reg, so the (size, type) doesn't
        // matter. For MOVSX and MOVZX, the size passed to dump_reg_reg is the srcSize (8 or 16),
        // so destSize is technically the srcSize, (type is gpr) and we override destSize inside
        // lower_reg_to_reg to 32. For CVTSI2SS and CVTSI2SD, the destSize is 64-bit, and we
        // override the srcSize inside lower_reg_to_reg.
        op = lower_reg_to_reg(m, m2, destSize, regAll, true /*isPhysical*/, regAll2,
                true /*isPhysical2*/, destType);
#ifdef MOVE_OPT2
    }
#endif
        endNativeCode();
        return op;
    } else {
        return lower_reg_to_reg(m, m2, destSize, srcReg, isSrcPhysical, destReg, isDestPhysical,
                destType);
    }
    return NULL;
}

//! \brief Wrapper around dump_reg_reg_diff_types assuming sizes and types are same
//! \param m The mnemonic
//! \param m2 The ATOM mnemonic type
//! \param size Size of the source and destination operands
//! \param reg The source register
//! \param isPhysical Whether source is physical
//! \param reg2 The destination register
//! \param isPhysical2 Whether destination is physical
//! \param type The type of operation
//! \return The generated LowOp
LowOpRegReg* dump_reg_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        bool isPhysical, int reg2, bool isPhysical2, LowOpndRegType type) {
    return dump_reg_reg_diff_types(m, m2, size, reg, isPhysical, type, size,
            reg2, isPhysical2, type);
}

LowOpMemReg* lower_mem_to_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex,
        int reg, bool isPhysical, LowOpndRegType type, struct ConstInfo** listPtr) {
    bool isMovzs = (m == Mnemonic_MOVZX || m == Mnemonic_MOVSX);
    OpndSize overridden_size = isMovzs ? OpndSize_32 : size;
    LowOpndRegType overridden_type = isMovzs ? LowOpndRegType_gp : type;
    if (!gDvmJit.scheduling) {
        stream = encoder_mem_to_reg_diff_sizes(m, size, disp, base_reg, isBasePhysical,
                overridden_size, reg, isPhysical, overridden_type, stream);
        return NULL;
    }

    if (!isBasePhysical && !isPhysical) {
        ALOGI("JIT_INFO: Base register or operand register not physical in lower_mem_to_reg");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }

    LowOpMemReg* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpMemReg>();
    if (listPtr != NULL) {
        op->constLink = *listPtr;
    } else {
        op->constLink = NULL;
    }

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = overridden_size;
    op->opndDest.type = LowOpndType_Reg;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Mem;
    op->numOperands = 2;
    set_reg_opnd(&(op->regDest), reg, isPhysical, overridden_type);
    set_mem_opnd(&(op->memSrc), disp, base_reg, isBasePhysical);
    op->memSrc.mType = mType;
    op->memSrc.index = mIndex;
    singletonPtr<Scheduler>()->updateUseDefInformation_mem_to_reg(op);
    return op;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!Here, operands are already allocated to physical registers
LowOpMemReg* dump_mem_reg_noalloc(Mnemonic m, OpndSize size, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex,
        int reg, bool isPhysical, LowOpndRegType type) {
    return lower_mem_to_reg(m, ATOM_NORMAL, size, disp, base_reg,
            true /*isBasePhysical*/, mType, mIndex, reg, true /*isPhysical*/,
            type, NULL);
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!Here, memory operand is already allocated to physical register
LowOpMemReg* dump_mem_reg_noalloc_mem(Mnemonic m, AtomOpCode m2, OpndSize size,
        int disp, int base_reg, bool isBasePhysical, MemoryAccessType mType,
        int mIndex, int reg, bool isPhysical, LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        int regAll = registerAlloc(type, reg, isPhysical, true, true);
        return lower_mem_to_reg(m, m2, size, disp, base_reg,
                true /*isBasePhysical*/, mType, mIndex, regAll,
                true /*isPhysical*/, type, NULL);
    } else {
        return lower_mem_to_reg(m, m2, size, disp, base_reg, isBasePhysical, mType,
                mIndex, reg, isPhysical, type, NULL);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!
LowOpMemReg* dump_mem_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex,
        int reg, bool isPhysical, LowOpndRegType type, struct ConstInfo** listPtr) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);
        //it is okay to use the same physical register
        if (isMoveOptimizable(m)) {
            freeReg(false);
        } else {
            //Do not spill baseAll
            gCompilationUnit->setCanSpillRegister (baseAll, false);
        }
        int regAll = registerAlloc(type, reg, isPhysical, true, true);
        endNativeCode();
        return lower_mem_to_reg(m, m2, size, disp, baseAll,
                true /*isBasePhysical*/, mType, mIndex, regAll,
                true /*isPhysical*/, type, listPtr);
    } else {
        return lower_mem_to_reg(m, m2, size, disp, base_reg, isBasePhysical, mType,
                mIndex, reg, isPhysical, type, NULL);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!
LowOpMemReg* dump_moves_mem_reg(Mnemonic m, OpndSize size,
                         int disp, int base_reg, bool isBasePhysical,
             int reg, bool isPhysical) {
#if 0 /* Commented out because it is dead code. If re-enabling, this needs to be updated
         to work with instruction scheduling and cannot call encoder directly. Please see
         dump_movez_mem_reg for an example */
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        freeReg(true);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical, true);

        //Do not spill baseAll
        gCompilationUnit->setCanSpillRegister (baseAll, false);

        int regAll = registerAlloc(LowOpndRegType_gp, reg, isPhysical, true);
        endNativeCode();
        return lower_mem_reg(m, ATOM_NORMAL, size, disp, baseAll, MemoryAccess_Unknown, -1,
            regAll, LowOpndRegType_gp, true/*moves*/);
    } else {
        stream = encoder_moves_mem_to_reg(size, disp, base_reg, isBasePhysical, reg, isPhysical, stream);
    }
#endif
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!
LowOpMemReg* dump_movez_mem_reg(Mnemonic m, OpndSize size, int disp,
        int base_reg, bool isBasePhysical, int reg, bool isPhysical) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);

        //Do not spill baseAll
        gCompilationUnit->setCanSpillRegister (baseAll, false);

        int regAll = registerAlloc(LowOpndRegType_gp, reg, isPhysical, true, true);
        endNativeCode();
        return lower_mem_to_reg(m, ATOM_NORMAL, size, disp, baseAll,
                true /*isBasePhysical*/, MemoryAccess_Unknown, -1, regAll,
                true /*isPhysical*/, LowOpndRegType_gp, NULL);
    } else {
        return lower_mem_to_reg(m, ATOM_NORMAL, size, disp, base_reg,
                isBasePhysical, MemoryAccess_Unknown, -1, reg, isPhysical,
                LowOpndRegType_gp, NULL);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one reg operand

//!
LowOpRegReg* dump_movez_reg_reg(Mnemonic m, OpndSize size,
             int reg, bool isPhysical,
             int reg2, bool isPhysical2) {
#if 0 /* Commented out because it is dead code. If re-enabling, this needs to be updated
         to work with instruction scheduling and cannot call encoder directly. Please see
         dump_movez_mem_reg for an example */
    LowOpRegReg* op = (LowOpRegReg*)atomNew(sizeof(LowOpRegReg));
    op->lop.opCode = m;
    op->lop.opnd1.size = OpndSize_32;
    op->lop.opnd1.type = LowOpndType_Reg;
    op->lop.opnd2.size = size;
    op->lop.opnd2.type = LowOpndType_Reg;
    set_reg_opnd(&(op->regOpnd1), reg2, isPhysical2, LowOpndRegType_gp);
    set_reg_opnd(&(op->regOpnd2), reg, isPhysical, LowOpndRegType_gp);
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        //reg is source if m is MOV
        freeReg(true);
        int regAll = registerAlloc(LowOpndRegType_gp, reg, isPhysical, true);

        //Do not spill regAll
        gCompilationUnit->setCanSpillRegister (regAll, false);

        int regAll2 = registerAlloc(LowOpndRegType_gp, reg2, isPhysical2, true);
        stream = encoder_movez_reg_to_reg(size, regAll, true, regAll2, true,
                                          LowOpndRegType_gp, stream);
        endNativeCode();
    }
    else {
        stream = encoder_movez_reg_to_reg(size, reg, isPhysical, reg2,
                                        isPhysical2, LowOpndRegType_gp, stream);
    }
#endif
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!
LowOpMemReg* lower_mem_scale_to_reg(Mnemonic m, OpndSize size, int base_reg,
        bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical,
        int scale, int reg, bool isPhysical, LowOpndRegType type) {
    bool isMovzs = (m == Mnemonic_MOVZX || m == Mnemonic_MOVSX);
    OpndSize overridden_size = isMovzs ? OpndSize_32 : size;
    LowOpndRegType overridden_type = isMovzs ? LowOpndRegType_gp : type;
    if (!gDvmJit.scheduling) {
        stream = encoder_mem_disp_scale_to_reg_diff_sizes(m, size, base_reg, isBasePhysical,
                disp, index_reg, isIndexPhysical, scale, overridden_size, reg,
                isPhysical, overridden_type, stream);
        return NULL;
    }

    if (!isBasePhysical && !isIndexPhysical && !isPhysical) {
        ALOGI("JIT_INFO: Base, index or operand register not physical at lower_mem_scale_to_reg");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }
    LowOpMemReg* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpMemReg>();

    op->opCode = m;
    op->opCode2 = ATOM_NORMAL;
    op->opndDest.size = overridden_size;
    op->opndDest.type = LowOpndType_Reg;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Mem;
    op->numOperands = 2;
    op->memSrc.mType = MemoryAccess_Unknown;
    op->memSrc.index = -1;
    set_reg_opnd(&(op->regDest), reg, isPhysical, overridden_type);
    set_mem_opnd_scale(&(op->memSrc), base_reg, isBasePhysical, disp,
            index_reg, isIndexPhysical, scale);
    singletonPtr<Scheduler>()->updateUseDefInformation_mem_to_reg(op);
    return op;
}

LowOpMemReg* dump_mem_scale_reg(Mnemonic m, OpndSize size, int base_reg,
        bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical,
        int scale, int reg, bool isPhysical, LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);

        //Do not spill baseAll
        gCompilationUnit->setCanSpillRegister (baseAll, false);

        int indexAll = registerAlloc(LowOpndRegType_gp, index_reg,
                isIndexPhysical, true);
        if (isMoveOptimizable(m)) {
            freeReg(false);

            //We can now spill base
            gCompilationUnit->setCanSpillRegister (baseAll, true);
        } else {
            //Do not spill indexAll
            gCompilationUnit->setCanSpillRegister (indexAll, false);
        }
        bool isMovzs = (m == Mnemonic_MOVZX || m == Mnemonic_MOVSX);
        int regAll = registerAlloc(isMovzs ? LowOpndRegType_gp : type, reg,
                isPhysical, true, true);
        endNativeCode();
        return lower_mem_scale_to_reg(m, size, baseAll, true /*isBasePhysical*/,
                disp, indexAll, true /*isIndexPhysical*/, scale, regAll,
                true /*isPhysical*/, type);
    } else {
        return lower_mem_scale_to_reg(m, size, base_reg, isBasePhysical, disp,
                index_reg, isIndexPhysical, scale, reg, isPhysical, type);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!
LowOpRegMem* lower_reg_to_mem_scale(Mnemonic m, OpndSize size, int reg,
        bool isPhysical, int base_reg, bool isBasePhysical, int disp,
        int index_reg, bool isIndexPhysical, int scale, LowOpndRegType type) {
    if (!gDvmJit.scheduling) {
        stream = encoder_reg_mem_disp_scale(m, size, reg, isPhysical, base_reg,
                isBasePhysical, disp, index_reg, isIndexPhysical, scale, type,
                stream);
        return NULL;
    }

    if (!isBasePhysical && !isIndexPhysical && !isPhysical) {
        ALOGI("JIT_INFO: Base, index or operand register not physical in lower_reg_to_mem_scale");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }
    LowOpRegMem* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpRegMem>();

    op->opCode = m;
    op->opCode2 = ATOM_NORMAL;
    op->opndDest.size = size;
    op->opndDest.type = LowOpndType_Mem;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Reg;
    op->numOperands = 2;
    op->memDest.mType = MemoryAccess_Unknown;
    op->memDest.index = -1;
    set_reg_opnd(&(op->regSrc), reg, isPhysical, type);
    set_mem_opnd_scale(&(op->memDest), base_reg, isBasePhysical, disp,
            index_reg, isIndexPhysical, scale);
    singletonPtr<Scheduler>()->updateUseDefInformation_reg_to_mem(op);
    return op;
}

LowOpRegMem* dump_reg_mem_scale(Mnemonic m, OpndSize size, int reg,
        bool isPhysical, int base_reg, bool isBasePhysical, int disp,
        int index_reg, bool isIndexPhysical, int scale, LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);

        //Do not spill baseAll
        gCompilationUnit->setCanSpillRegister (baseAll, false);

        int indexAll = registerAlloc(LowOpndRegType_gp, index_reg,
                isIndexPhysical, true);

        //Do not spill indexAll
        gCompilationUnit->setCanSpillRegister (indexAll, false);

        int regAll = registerAlloc(type, reg, isPhysical, true, true);
        endNativeCode();
        return lower_reg_to_mem_scale(m, size, regAll, true /*isPhysical*/,
                baseAll, true /*isBasePhysical*/, disp, indexAll,
                true /*isIndexPhysical*/, scale, type);
    } else {
        return lower_reg_to_mem_scale(m, size, reg, isPhysical, base_reg,
                isBasePhysical, disp, index_reg, isIndexPhysical, scale, type);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!Here operands are already allocated
LowOpRegMem* lower_reg_to_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        bool isPhysical, int disp, int base_reg, bool isBasePhysical,
        MemoryAccessType mType, int mIndex, LowOpndRegType type) {
    if (!gDvmJit.scheduling) {
        stream = encoder_reg_mem(m, size, reg, isPhysical, disp, base_reg,
                isBasePhysical, type, stream);
        return NULL;
    }

    if (!isBasePhysical && !isPhysical) {
        ALOGI("JIT_INFO: Base or operand register not physical in lower_reg_to_mem");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }
    LowOpRegMem* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpRegMem>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = size;
    op->opndDest.type = LowOpndType_Mem;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Reg;
    op->numOperands = 2;
    set_reg_opnd(&(op->regSrc), reg, isPhysical, type);
    set_mem_opnd(&(op->memDest), disp, base_reg, isBasePhysical);
    op->memDest.mType = mType;
    op->memDest.index = mIndex;
    singletonPtr<Scheduler>()->updateUseDefInformation_reg_to_mem(op);
    return op;
}

LowOpRegMem* dump_reg_mem_noalloc(Mnemonic m, OpndSize size, int reg,
        bool isPhysical, int disp, int base_reg, bool isBasePhysical,
        MemoryAccessType mType, int mIndex, LowOpndRegType type) {
    return lower_reg_to_mem(m, ATOM_NORMAL, size, reg, true /*isPhysical*/, disp,
            base_reg, true /*isBasePhysical*/, mType, mIndex, type);
}

//!update fields of LowOp and generate a x86 instruction that takes one reg operand and one mem operand

//!
LowOpRegMem* dump_reg_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        bool isPhysical, int disp, int base_reg, bool isBasePhysical,
        MemoryAccessType mType, int mIndex, LowOpndRegType type) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        startNativeCode(-1, -1);
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);

        //Do not spill baseAll
        gCompilationUnit->setCanSpillRegister (baseAll, false);

        int regAll = registerAlloc(type, reg, isPhysical, true);
        endNativeCode();
        return lower_reg_to_mem(m, m2, size, regAll, true /*isPhysical*/, disp,
                baseAll, true /*isBasePhysical*/, mType, mIndex, type);
    } else {
        return lower_reg_to_mem(m, m2, size, reg, isPhysical, disp, base_reg,
                isBasePhysical, mType, mIndex, type);
    }
    return NULL;
}

//! \brief Checks if Mnemonic sign extends imm operand
//! \details Information taken from Atom instruction manual
//! \param mn Mnemonic to check for
//! \return whether mn sign extends its imm operand
bool mnemonicSignExtendsImm(Mnemonic mn) {
    if ((mn == Mnemonic_ADD) || (mn == Mnemonic_ADC)
            || (mn == Mnemonic_SUB) || (mn == Mnemonic_SBB)) {
        return true;
    }
    return false;
}

//! \brief Returns minimum size to fit an imm
//! \param imm The immediate value to check for
//! \return the OpndSize befitting the imm
OpndSize minSizeForImm(int imm) {
    //Don't care about signed values
    if (imm < 0)
        return OpndSize_32;
    if (imm < 128)
        return OpndSize_8;
    if (imm < 32768)
        return OpndSize_16;

    return OpndSize_32;
}

/**
 * @brief Determines if x86 mnemonic is shift or rotate
 * @param m The x86 mnemonic
 * @return Returns whether we have a shift/rotate instruction
 */
static bool isShiftMnemonic (Mnemonic m)
{
    return (m == Mnemonic_SAL || m == Mnemonic_SHR || m == Mnemonic_SHL || m == Mnemonic_SAR || m == Mnemonic_ROR
            || m == Mnemonic_PSLLD || m == Mnemonic_PSLLQ || m == Mnemonic_PSLLW || m == Mnemonic_PSRAD
            || m == Mnemonic_PSRAW || m == Mnemonic_PSRLQ || m == Mnemonic_PSRLD || m == Mnemonic_PSRLW);
}

//!update fields of LowOp and generate a x86 instruction that takes one immediate and one reg operand

//!The reg operand is allocated already
LowOpImmReg* lower_imm_to_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int imm,
        int reg, bool isPhysical, LowOpndRegType type, bool chaining, SwitchInfoScheduler *switchInfoScheduler)
{
    //Start off with assumption that the immediate and register sizes match
    OpndSize immediateSize = size;

    //Now check if the immediate actually should be a different size
    if (isShiftMnemonic (m) == true)
    {
        immediateSize = OpndSize_8;
    }
    else if (mnemonicSignExtendsImm (m))
    {
        immediateSize = minSizeForImm (imm);
    }

    //If scheduling is disabled, call encoder directly
    if (gDvmJit.scheduling == false)
    {
        stream = encoder_imm_reg_diff_sizes (m, immediateSize, imm, size, reg, isPhysical, type, stream);
        return NULL;
    }

    //We must have already done register allocation by this point
    if (isPhysical == false)
    {
        ALOGI ("JIT_INFO: Operand register not physical in lower_imm_to_reg");
        SET_JIT_ERROR (kJitErrorInsScheduling);
        return NULL;
    }

    //Create the LIR representation
    LowOpImmReg* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpImmReg>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = size;
    op->opndDest.type = LowOpndType_Reg;
    op->numOperands = 2;
    op->opndSrc.size = immediateSize;
    op->opndSrc.type = chaining ? LowOpndType_Chain : LowOpndType_Imm;
    set_reg_opnd(&(op->regDest), reg, isPhysical, type);
    op->immSrc.value = imm;
    op->switchInfoScheduler = switchInfoScheduler;
    singletonPtr<Scheduler>()->updateUseDefInformation_imm_to_reg(op);
    return op;
}

LowOpImmReg* dump_imm_reg_noalloc(Mnemonic m, OpndSize size, int imm, int reg,
        bool isPhysical, LowOpndRegType type) {
    return lower_imm_to_reg(m, ATOM_NORMAL, size, imm, reg, true /*isPhysical*/,
            type, false, NULL);
}

LowOpImmReg* dump_imm_reg_noalloc_alu(Mnemonic m, OpndSize size, int imm, int reg,
        bool isPhysical, LowOpndRegType type) {
    return lower_imm_to_reg(m, ATOM_NORMAL_ALU, size, imm, reg, true /*isPhysical*/,
            type, false, NULL);
}

//!update fields of LowOp and generate a x86 instruction that takes one immediate and one reg operand

//!
LowOpImmReg* dump_imm_reg(Mnemonic m, AtomOpCode m2, OpndSize size, int imm,
        int reg, bool isPhysical, LowOpndRegType type, bool chaining, SwitchInfoScheduler *switchInfoScheduler) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        freeReg(false);
        int regAll = registerAlloc(type, reg, isPhysical, true, true);
        return lower_imm_to_reg(m, m2, size, imm, regAll, true /*isPhysical*/,
                type, chaining, switchInfoScheduler);
    } else {
        return lower_imm_to_reg(m, m2, size, imm, reg, isPhysical, type, chaining, NULL);
    }
    return NULL;
}

//! \brief Three operand instruction with an imm and two regs
//! \param m The mnemonic
//! \param m2 The ATOM mnemonic type
//! \param imm The immediate operand
//! \param immediateSize the size of the imm operand
//! \param sourceReg The source register
//! \param sourceRegSize Size of the sourceReg operand
//! \param sourcePhysicalType The physical type of the sourceReg
//! \param destReg The destination register
//! \param destRegSize Size of the destReg operand
//! \param destPhysicalType The physical type of the destReg
//! \return The generated LowOp
static LowOpImmRegReg *lower_imm_reg_reg (Mnemonic m, AtomOpCode m2, int imm, OpndSize immediateSize, int sourceReg,
        OpndSize sourceRegSize, LowOpndRegType sourcePhysicalType, int destReg, OpndSize destRegSize,
        LowOpndRegType destPhysicalType)
{
    if (gDvmJit.scheduling == false)
    {
        stream = encoder_imm_reg_reg (m, imm, immediateSize, sourceReg, sourceRegSize, destReg, destRegSize, stream);
        return 0;
    }

    LowOpImmRegReg* op = singletonPtr<Scheduler> ()->allocateNewEmptyLIR<LowOpImmRegReg> ();

    //Set up opcode
    op->opCode = m;
    op->opCode2 = m2;

    //Set up destination register
    op->opndDest.size = destRegSize;
    op->opndDest.type = LowOpndType_Reg;

    //Set up source register
    op->opndSrc.size = sourceRegSize;
    op->opndSrc.type = LowOpndType_Reg;

    //Finally set up the immediate value
    op->imm.value = imm;
    op->imm.immediateSize = immediateSize;

    //We have 3 operands
    op->numOperands = 3;

    //Now set up information about register operands
    set_reg_opnd(&(op->regDest), destReg, true, destPhysicalType);
    set_reg_opnd(&(op->regSrc), sourceReg, true, sourcePhysicalType);

    //Send it off to scheduler to creade dependency graph
    singletonPtr<Scheduler> ()->updateUseDefInformation_reg_to_reg (op);

    return op;
}

void dump_imm_reg_reg (Mnemonic op, AtomOpCode m2, int imm, OpndSize immediateSize, int sourceReg,
        bool isSourcePhysical, LowOpndRegType sourcePhysicalType, OpndSize sourceRegSize, int destReg,
        bool isDestPhysical, LowOpndRegType destPhysicalType, OpndSize destRegSize)
{
    //Check for NCGO1 mode in case we are supposed to use the register allocator
    if (gDvm.executionMode == kExecutionModeNcgO1)
    {
        //We start generating the actual code at this point so we keep track of it
        startNativeCode (-1, -1);

        //We are doing register allocation so we need to free anything with no remaining references
        freeReg (false);

        //Allocate a physical register for the source
        const int physicalSourceReg = registerAlloc (sourcePhysicalType, sourceReg, isSourcePhysical, false);

        //We cannot spill physical register for source
        gCompilationUnit->setCanSpillRegister (physicalSourceReg, false);

        //Allocate a physical register for the destination
        const int physicaldestReg = registerAlloc (destPhysicalType, destReg, isDestPhysical, true);

        //We cannot spill physical register for destination
        gCompilationUnit->setCanSpillRegister (physicaldestReg, false);

        //Now actually call encoder to do the generation
        lower_imm_reg_reg (op, m2, imm, immediateSize, physicalSourceReg, sourceRegSize, sourcePhysicalType,
                physicaldestReg, destRegSize, destPhysicalType);

        //We finished generating native code
        endNativeCode ();
    }
    else
    {
        //The registers must be physical
        assert (isSourcePhysical == true && isDestPhysical == true);

        //Call the encoder
        lower_imm_reg_reg (op, m2, imm, immediateSize, sourceReg, sourceRegSize, sourcePhysicalType, destReg,
                destRegSize, destPhysicalType);
    }
}

//!update fields of LowOp and generate a x86 instruction that takes one immediate and one mem operand

//!The mem operand is already allocated
LowOpImmMem* lower_imm_to_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int imm,
        int disp, int base_reg, bool isBasePhysical, MemoryAccessType mType,
        int mIndex, bool chaining, SwitchInfoScheduler * switchInfoScheduler)
{
    //Start off with assumption that the immediate and register sizes match
    OpndSize immediateSize = size;

    //Now check if the immediate actually should be a different size
    if (isShiftMnemonic (m) == true)
    {
        immediateSize = OpndSize_8;
    }
    else if (mnemonicSignExtendsImm (m))
    {
        immediateSize = minSizeForImm (imm);
    }

    //If scheduling is disabled, call encoder directly
    if (gDvmJit.scheduling == false)
    {
        stream = encoder_imm_mem_diff_sizes (m, immediateSize, imm, size, disp, base_reg, isBasePhysical, stream);
        return NULL;
    }

    //We must have already done register allocation by this point
    if (isBasePhysical == false)
    {
        ALOGI("JIT_INFO: Base register not physical in lower_imm_to_mem");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }

    //Now create the LIR representation
    LowOpImmMem* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpImmMem>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = size;
    op->opndDest.type = LowOpndType_Mem;
    op->opndSrc.size = immediateSize;
    op->opndSrc.type = chaining ? LowOpndType_Chain : LowOpndType_Imm;
    op->numOperands = 2;
    set_mem_opnd(&(op->memDest), disp, base_reg, isBasePhysical);
    op->immSrc.value = imm;
    op->memDest.mType = mType;
    op->memDest.index = mIndex;
    op->switchInfoScheduler = switchInfoScheduler;
    singletonPtr<Scheduler>()->updateUseDefInformation_imm_to_mem(op);
    return op;
}

LowOpImmMem* dump_imm_mem_noalloc(Mnemonic m, OpndSize size, int imm, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex, SwitchInfoScheduler *switchInfoScheduler) {
    return lower_imm_to_mem(m, ATOM_NORMAL, size, imm, disp, base_reg,
            true /*isBasePhysical*/, mType, mIndex, false, switchInfoScheduler);
}

LowOpImmMem* dump_imm_mem_noalloc_alu(Mnemonic m, OpndSize size, int imm, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex) {
    return lower_imm_to_mem(m, ATOM_NORMAL_ALU, size, imm, disp, base_reg,
            true /*isBasePhysical*/, mType, mIndex, false, NULL);
}

//!update fields of LowOp and generate a x86 instruction that takes one immediate and one mem operand

//!
LowOpImmMem* dump_imm_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int imm,
        int disp, int base_reg, bool isBasePhysical, MemoryAccessType mType,
        int mIndex, bool chaining) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        /* do not free register if the base is %edi, %esp, or %ebp
         make sure dump_imm_mem will only generate a single instruction */
        if (!isBasePhysical
                || (base_reg != PhysicalReg_EDI && base_reg != PhysicalReg_ESP
                        && base_reg != PhysicalReg_EBP)) {
            freeReg(false);
        }
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);
        return lower_imm_to_mem(m, m2, size, imm, disp, baseAll,
                true /*isBasePhysical*/, mType, mIndex, chaining, NULL);
    } else {
        return lower_imm_to_mem(m, m2, size, imm, disp, base_reg, isBasePhysical,
                mType, mIndex, chaining, NULL);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that uses the FP stack and takes one mem operand

//!
LowOpRegMem* lower_fp_to_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        int disp, int base_reg, bool isBasePhysical, MemoryAccessType mType,
        int mIndex) {
    if (!gDvmJit.scheduling) {
        stream = encoder_fp_mem(m, size, reg, disp, base_reg, isBasePhysical,
                stream);
        return NULL;
    }

    if (!isBasePhysical) {
        ALOGI("JIT_INFO: Base register not physical in lower_fp_to_mem");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }
    LowOpRegMem* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpRegMem>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = size;
    op->opndDest.type = LowOpndType_Mem;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Reg;
    op->numOperands = 2;
    set_reg_opnd(&(op->regSrc), PhysicalReg_ST0 + reg, true,
            LowOpndRegType_fs);
    set_mem_opnd(&(op->memDest), disp, base_reg, isBasePhysical);
    op->memDest.mType = mType;
    op->memDest.index = mIndex;
    singletonPtr<Scheduler>()->updateUseDefInformation_fp_to_mem(op);
    return op;
}

LowOpRegMem* dump_fp_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
        int disp, int base_reg, bool isBasePhysical, MemoryAccessType mType,
        int mIndex) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);
        return lower_fp_to_mem(m, m2, size, reg, disp, baseAll,
                true /*isBasePhysical*/, mType, mIndex);
    } else {
        return lower_fp_to_mem(m, m2, size, reg, disp, base_reg, isBasePhysical,
                mType, mIndex);
    }
    return NULL;
}

//!update fields of LowOp and generate a x86 instruction that uses the FP stack and takes one mem operand

//!
LowOpMemReg* lower_mem_to_fp(Mnemonic m, AtomOpCode m2, OpndSize size, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex,
        int reg) {
    if (!gDvmJit.scheduling) {
        stream = encoder_mem_fp(m, size, disp, base_reg, isBasePhysical, reg,
                stream);
        return NULL;
    }

    if (!isBasePhysical) {
        ALOGI("JIT_INFO: Base register not physical in lower_mem_to_fp");
        SET_JIT_ERROR(kJitErrorInsScheduling);
        return NULL;
    }

    LowOpMemReg* op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOpMemReg>();

    op->opCode = m;
    op->opCode2 = m2;
    op->opndDest.size = size;
    op->opndDest.type = LowOpndType_Reg;
    op->opndSrc.size = size;
    op->opndSrc.type = LowOpndType_Mem;
    op->numOperands = 2;
    set_reg_opnd(&(op->regDest), PhysicalReg_ST0 + reg, true,
            LowOpndRegType_fs);
    set_mem_opnd(&(op->memSrc), disp, base_reg, isBasePhysical);
    op->memSrc.mType = mType;
    op->memSrc.index = mIndex;
    singletonPtr<Scheduler>()->updateUseDefInformation_mem_to_fp(op);
    return op;
}

LowOpMemReg* dump_mem_fp(Mnemonic m, AtomOpCode m2, OpndSize size, int disp,
        int base_reg, bool isBasePhysical, MemoryAccessType mType, int mIndex,
        int reg) {
    if (gDvm.executionMode == kExecutionModeNcgO1) {
        freeReg(false);
        int baseAll = registerAlloc(LowOpndRegType_gp, base_reg, isBasePhysical,
                true);
        return lower_mem_to_fp(m, m2, size, disp, baseAll,
                true /*isBasePhysical*/, mType, mIndex, reg);
    } else {
        return lower_mem_to_fp(m, m2, size, disp, base_reg, isBasePhysical,
                mType, mIndex, reg);
    }
    return NULL;
}
///////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////
//OPERAND ORDER:
//LowOp same as EncoderBase destination first
//parameter order of function: src first

////////////////////////////////// IA32 native instructions //////////////
//! generate a native instruction lea

//!
void load_effective_addr(int disp, int base_reg, bool isBasePhysical,
                          int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_LEA;
    dump_mem_reg(m, ATOM_NORMAL, OpndSize_32, disp, base_reg, isBasePhysical,
        MemoryAccess_Unknown, -1, reg, isPhysical, LowOpndRegType_gp, NULL);
}
//! generate a native instruction lea

//! Computes the effective address of the source operand and stores it in the
//! first operand. (lea reg, [base_reg + index_reg*scale])
void load_effective_addr_scale(int base_reg, bool isBasePhysical,
                int index_reg, bool isIndexPhysical, int scale,
                int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_LEA;
    dump_mem_scale_reg(m, OpndSize_32,
                              base_reg, isBasePhysical, 0/*disp*/, index_reg, isIndexPhysical, scale,
                              reg, isPhysical, LowOpndRegType_gp);
}

//! lea reg, [base_reg + index_reg*scale + disp]
void load_effective_addr_scale_disp(int base_reg, bool isBasePhysical, int disp,
                int index_reg, bool isIndexPhysical, int scale,
                int reg, bool isPhysical) {
    dump_mem_scale_reg(Mnemonic_LEA, OpndSize_32, base_reg, isBasePhysical, disp,
            index_reg, isIndexPhysical, scale, reg, isPhysical,
            LowOpndRegType_gp);
}
//!fldcw

//!
void load_fpu_cw(int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m = Mnemonic_FLDCW;
    dump_mem(m, ATOM_NORMAL, OpndSize_16, disp, base_reg, isBasePhysical);
}
//!fnstcw

//!
void store_fpu_cw(bool checkException, int disp, int base_reg, bool isBasePhysical) {
    assert(!checkException);
    Mnemonic m = Mnemonic_FNSTCW;
    dump_mem(m, ATOM_NORMAL, OpndSize_16, disp, base_reg, isBasePhysical);
}
//!cdq

//!
void convert_integer(OpndSize srcSize, OpndSize dstSize) { //cbw, cwd, cdq
    assert(srcSize == OpndSize_32 && dstSize == OpndSize_64);
    Mnemonic m = Mnemonic_CDQ;
    dump_reg_reg(m, ATOM_NORMAL, OpndSize_32, PhysicalReg_EAX, true, PhysicalReg_EDX, true, LowOpndRegType_gp);
}

//! \brief Generates the CVTSI2SD and CVTSI2SS opcodes
//! \details performs cvtsi2** destReg, srcReg
//! NOTE: Even for cvtsi2ss, the destination is still XMM
//! and needs to be moved to a GPR.
//! \param srcReg the src register
//! \param isSrcPhysical if the srcReg is a physical register
//! \param destReg the destination register
//! \param isDestPhysical if destReg is a physical register
//! \param isDouble if the destination needs to be a double value (float otherwise)
void convert_int_to_fp(int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, bool isDouble) {
    Mnemonic m = isDouble ? Mnemonic_CVTSI2SD : Mnemonic_CVTSI2SS;
    dump_reg_reg_diff_types(m, ATOM_NORMAL, OpndSize_32, srcReg, isSrcPhysical, LowOpndRegType_gp,
            OpndSize_64, destReg, isDestPhysical, LowOpndRegType_xmm);
}

//!fld: load from memory (float or double) to stack

//!
void load_fp_stack(LowOp* op, OpndSize size, int disp, int base_reg, bool isBasePhysical) {//fld(s|l)
    Mnemonic m = Mnemonic_FLD;
    dump_mem_fp(m, ATOM_NORMAL, size, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, 0); //ST0
}
//! fild: load from memory (int or long) to stack

//!
void load_int_fp_stack(OpndSize size, int disp, int base_reg, bool isBasePhysical) {//fild(ll|l)
    Mnemonic m = Mnemonic_FILD;
    dump_mem_fp(m, ATOM_NORMAL, size, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, 0); //ST0
}
//!fild: load from memory (absolute addr)

//!
void load_int_fp_stack_imm(OpndSize size, int imm) {//fild(ll|l)
    return load_int_fp_stack(size, imm, PhysicalReg_Null, true);
}
//!fst: store from stack to memory (float or double)

//!
void store_fp_stack(LowOp* op, bool pop, OpndSize size, int disp, int base_reg, bool isBasePhysical) {//fst(p)(s|l)
    Mnemonic m = pop ? Mnemonic_FSTP : Mnemonic_FST;
    dump_fp_mem(m, ATOM_NORMAL, size, 0, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1);
}
//!fist: store from stack to memory (int or long)

//!
void store_int_fp_stack(LowOp* op, bool pop, OpndSize size, int disp, int base_reg, bool isBasePhysical) {//fist(p)(l)
    Mnemonic m = pop ? Mnemonic_FISTP : Mnemonic_FIST;
    dump_fp_mem(m, ATOM_NORMAL, size, 0, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1);
}
//!cmp reg, mem

//!
void compare_reg_mem(LowOp* op, OpndSize size, int reg, bool isPhysical,
              int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m = Mnemonic_CMP;
    dump_reg_mem(m, ATOM_NORMAL, size, reg, isPhysical, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, getTypeFromIntSize(size));
}
//!cmp mem, reg

//!
void compare_mem_reg(OpndSize size,
              int disp, int base_reg, bool isBasePhysical,
              int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_CMP;
    dump_mem_reg(m, ATOM_NORMAL, size, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, reg, isPhysical, getTypeFromIntSize(size), NULL);
}
//! compare a VR with a temporary variable

//!
void compare_VR_reg_all(OpndSize size,
             int vA,
             int reg, bool isPhysical, Mnemonic m) {
    LowOpndRegType type = getTypeFromIntSize(size);
    LowOpndRegType pType = type;
    if(m == Mnemonic_COMISS) {
        size = OpndSize_32;
        type = LowOpndRegType_ss;
        pType = LowOpndRegType_xmm;
    }
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        int tmpValue[2];
        int isConst = isVirtualRegConstant(vA, type, tmpValue, true/*updateRefCount*/);
        if(isConst == 3) {
            if(m == Mnemonic_COMISS) {
#ifdef DEBUG_NCG_O1
                ALOGI("VR is const and SS in compare_VR_reg");
#endif
                bool storedAddr = false;

                if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){
                    tmpValue[1] = 0;// set higher 32 bits to zero
                    // create a new record of a constant
                    addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, false);

                    // save mem access location in constList
                    const int offset = 3; // offset is 3 for COMISS
                    storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, stream, offset);

                    ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                    if (storedAddr == true){
#ifdef DEBUG_CONST
                        ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL,
                                tmpPtr->valueH, tmpPtr->valueH);
#endif
                    } else {
                        ALOGI("JIT_INFO: Error creating constant failed for VR %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL,
                                tmpPtr->valueH, tmpPtr->valueH);
                    }
                }
                // Lower mem_reg instruction with constant to be accessed from constant data section
                if (storedAddr == true) {
                    int dispAddr =  getGlobalDataAddr("64bits");
                    dump_mem_reg(m, ATOM_NORMAL, OpndSize_32, dispAddr, PhysicalReg_Null, true,
                                     MemoryAccess_Constants, vA, reg, isPhysical, pType,
                                     &(gCompilationUnit->constListHead));
                } else {
                    writeBackConstVR(vA, tmpValue[0]);
                    const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
                    dump_mem_reg(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, reg, isPhysical, pType, NULL);
                }
                return;
            }
            else if(size != OpndSize_64) {
#ifdef DEBUG_NCG_O1
                ALOGI("VR is const and 32 bits in compare_VR_reg");
#endif
                dump_imm_reg(m, ATOM_NORMAL, size, tmpValue[0], reg, isPhysical, pType, false, NULL);
                return;
            }
            else if(size == OpndSize_64) {
#ifdef DEBUG_NCG_O1
                ALOGI("VR is const and 64 bits in compare_VR_reg");
#endif
                bool storedAddr = false;

                if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){
                    // create a new record of a constant
                    addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, false);
                    const int offset = 4; // offset is 4 for COMISD

                    // save mem access location in constList
                    storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, stream, offset);

                    ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                    if (storedAddr == true){
#ifdef DEBUG_CONST
                        ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL,
                                tmpPtr->valueH, tmpPtr->valueH);
#endif
                    } else {
                        ALOGI("JIT_INFO: Error creating constant failed for VR %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL,
                                tmpPtr->valueH, tmpPtr->valueH);
                    }
                }
                // Lower mem_reg instruction with constant to be accessed from constant data section
                if (storedAddr == true) {
                    int dispAddr =  getGlobalDataAddr("64bits");
                    dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_64, dispAddr, PhysicalReg_Null, true,
                                     MemoryAccess_Constants, vA, reg, isPhysical, LowOpndRegType_xmm,
                                     &(gCompilationUnit->constListHead));
                } else {
                    writeBackConstVR(vA, tmpValue[0]);
                    writeBackConstVR(vA+1, tmpValue[1]);
                    const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
                    dump_mem_reg(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true,
                                    MemoryAccess_VR, vA, reg, isPhysical, pType, NULL);
                }
                return;
            }
        }
        if(isConst == 1) writeBackConstVR(vA, tmpValue[0]);
        if(isConst == 2) writeBackConstVR(vA+1, tmpValue[1]);
        freeReg(false);
        int regAll = checkVirtualReg(vA, type, 0/*do not update*/);
        if(regAll != PhysicalReg_Null) { //do not spill regAll when allocating register for dst
            startNativeCode(-1, -1);

            //Do not spill regAll
            gCompilationUnit->setCanSpillRegister (regAll, false);

            dump_reg_reg_noalloc_src(m, ATOM_NORMAL, size, regAll, true, reg, isPhysical, pType);
            endNativeCode();
        }
        else {
            //virtual register is not allocated to a physical register
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
            dump_mem_reg_noalloc_mem(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vA, reg, isPhysical, pType);
        }
        updateRefCount(vA, type);
        return;
    } else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_mem_reg(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true,
            MemoryAccess_VR, vA, reg, isPhysical, pType, NULL);
        return;
    }
}
void compare_VR_reg(OpndSize size,
             int vA,
             int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_CMP;
    return compare_VR_reg_all(size, vA, reg, isPhysical, m);
}
void compare_VR_ss_reg(int vA, int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_COMISS;
    return compare_VR_reg_all(OpndSize_32, vA, reg, isPhysical, m);
}
void compare_VR_sd_reg(int vA, int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_COMISD;
    return compare_VR_reg_all(OpndSize_64, vA, reg, isPhysical, m);
}
//!load VR to stack

//!
void load_fp_stack_VR_all(OpndSize size, int vB, Mnemonic m) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        //can't load from immediate to fp stack
        int tmpValue[2];
        int isConst = isVirtualRegConstant(vB, getTypeFromIntSize(size), tmpValue, false/*updateRefCount*/);
        if(isConst > 0) {
            if(size != OpndSize_64) {
#ifdef DEBUG_NCG_O1
                ALOGI("VR is const and 32 bits in load_fp_stack");
#endif
                writeBackConstVR(vB, tmpValue[0]);
            }
            else {
#ifdef DEBUG_NCG_O1
                ALOGI("VR is const and 64 bits in load_fp_stack_VR");
#endif
                if(isConst == 1 || isConst == 3) writeBackConstVR(vB, tmpValue[0]);
                if(isConst == 2 || isConst == 3) writeBackConstVR(vB+1, tmpValue[1]);
            }
        }
        else { //if VR was updated by a def of gp, a xfer point was inserted
            //if VR was updated by a def of xmm, a xfer point was inserted
#if 0
            int regAll = checkVirtualReg(vB, size, 1);
            if(regAll != PhysicalReg_Null) //dump from register to memory
                dump_reg_mem_noalloc(m, size, regAll, true, 4*vB, PhysicalReg_FP, true,
                    MemoryAccess_VR, vB, getTypeFromIntSize(size));
#endif
        }
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vB);
        dump_mem_fp(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vB, 0);
    } else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vB);
        dump_mem_fp(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vB, 0);
    }
}
//!load VR(float or double) to stack

//!
void load_fp_stack_VR(OpndSize size, int vA) {//fld(s|l)
    Mnemonic m = Mnemonic_FLD;
    return load_fp_stack_VR_all(size, vA, m);
}
//!load VR(int or long) to stack

//!
void load_int_fp_stack_VR(OpndSize size, int vA) {//fild(ll|l)
    Mnemonic m = Mnemonic_FILD;
    return load_fp_stack_VR_all(size, vA, m);
}
//!store from stack to VR (float or double)

//!
void store_fp_stack_VR(bool pop, OpndSize size, int vA) {//fst(p)(s|l)
    Mnemonic m = pop ? Mnemonic_FSTP : Mnemonic_FST;
    const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
    dump_fp_mem(m, ATOM_NORMAL, size, 0, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA);
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        if(size == OpndSize_32)
            updateVirtualReg(vA, LowOpndRegType_fs_s);
        else
            updateVirtualReg(vA, LowOpndRegType_fs);
    }
}
//!store from stack to VR (int or long)

//!
void store_int_fp_stack_VR(bool pop, OpndSize size, int vA) {//fist(p)(l)
    Mnemonic m = pop ? Mnemonic_FISTP : Mnemonic_FIST;
    const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
    dump_fp_mem(m, ATOM_NORMAL, size, 0, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA);
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        if(size == OpndSize_32)
            updateVirtualReg(vA, LowOpndRegType_fs_s);
        else
            updateVirtualReg(vA, LowOpndRegType_fs);
    }
}
//! ALU ops in FPU, one operand is a VR

//!
void fpu_VR(ALU_Opcode opc, OpndSize size, int vA) {
    Mnemonic m = map_of_fpu_opcode_2_mnemonic[opc];
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        int tmpValue[2];
        int isConst = isVirtualRegConstant(vA, getTypeFromIntSize(size), tmpValue, false/*updateRefCount*/);
        if(isConst > 0) {
            if(size != OpndSize_64) {
                //allocate a register for dst
                writeBackConstVR(vA, tmpValue[0]);
            }
            else {
                if((isConst == 1 || isConst == 3) && size == OpndSize_64) {
                    writeBackConstVR(vA, tmpValue[0]);
                }
                if((isConst == 2 || isConst == 3) && size == OpndSize_64) {
                    writeBackConstVR(vA+1, tmpValue[1]);
                }
            }
        }
        if(!isInMemory(vA, size)) {
            ALOGI("JIT_INFO: VR not in memory for FPU operation");
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return;
        }
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_mem_fp(m, ATOM_NORMAL_ALU, size, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, 0);
    } else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_mem_fp(m, ATOM_NORMAL_ALU, size, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, 0);
    }
}
//! cmp imm reg

//!
void compare_imm_reg(OpndSize size, int imm,
              int reg, bool isPhysical) {
    if(imm == 0) {
        LowOpndRegType type = getTypeFromIntSize(size);
        Mnemonic m = Mnemonic_TEST;
        if(gDvm.executionMode == kExecutionModeNcgO1) {
            freeReg(false);
            int regAll = registerAlloc(type, reg, isPhysical, true);
            lower_reg_to_reg(m, ATOM_NORMAL, size, regAll, true /*isPhysical*/, regAll, true /*isPhysical2*/, type);
        } else {
            lower_reg_to_reg(m, ATOM_NORMAL, size, reg, isPhysical, reg, isPhysical, type);
        }
        return;
    }
    Mnemonic m = Mnemonic_CMP;
    dump_imm_reg(m, ATOM_NORMAL, size, imm, reg, isPhysical, getTypeFromIntSize(size), false, NULL);
}
//! cmp imm mem

//!
void compare_imm_mem(OpndSize size, int imm,
              int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m = Mnemonic_CMP;
    dump_imm_mem(m, ATOM_NORMAL, size, imm, disp,
                        base_reg, isBasePhysical, MemoryAccess_Unknown, -1, false);
}
//! cmp imm VR

//!
void compare_imm_VR(OpndSize size, int imm,
             int vA) {
    Mnemonic m = Mnemonic_CMP;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        if(size != OpndSize_32) {
            ALOGI("JIT_INFO: Only 32 bits supported in compare_imm_VR");
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return;
        }
        int tmpValue[2];
        int isConst = isVirtualRegConstant(vA, getTypeFromIntSize(size), tmpValue, false/*updateRefCount*/);
        if(isConst > 0) {
            writeBackConstVR(vA, tmpValue[0]);
        }
        int regAll = checkVirtualReg(vA, getTypeFromIntSize(size), 0);
        if(regAll != PhysicalReg_Null)
        {
            dump_imm_reg_noalloc(m, size, imm, regAll, true, LowOpndRegType_gp);
        }
        else
        {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
            dump_imm_mem_noalloc(m, size, imm, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vA, NULL);
        }
        updateRefCount(vA, getTypeFromIntSize(size));
    } else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_imm_mem(m, ATOM_NORMAL, size, imm, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, false);
    }
}
//! cmp reg reg

//!
void compare_reg_reg(int reg1, bool isPhysical1,
              int reg2, bool isPhysical2) {
    Mnemonic m = Mnemonic_CMP;
    dump_reg_reg(m, ATOM_NORMAL, OpndSize_32, reg1, isPhysical1, reg2, isPhysical2, LowOpndRegType_gp);
}
void compare_reg_reg_16(int reg1, bool isPhysical1,
              int reg2, bool isPhysical2) {
    Mnemonic m = Mnemonic_CMP;
    dump_reg_reg(m, ATOM_NORMAL, OpndSize_16, reg1, isPhysical1, reg2, isPhysical2, LowOpndRegType_gp);
}

//! comiss mem reg

//!SSE, XMM: comparison of floating point numbers
void compare_ss_mem_reg(LowOp* op, int disp, int base_reg, bool isBasePhysical,
             int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_COMISS;
    dump_mem_reg(m, ATOM_NORMAL, OpndSize_32, disp, base_reg, isBasePhysical,
        MemoryAccess_Unknown, -1, reg, isPhysical, LowOpndRegType_xmm, NULL);
}
//! comiss reg reg

//!
void compare_ss_reg_with_reg(LowOp* op, int reg1, bool isPhysical1,
                  int reg2, bool isPhysical2) {
    Mnemonic m = Mnemonic_COMISS;
    dump_reg_reg(m,  ATOM_NORMAL, OpndSize_32, reg1, isPhysical1, reg2, isPhysical2, LowOpndRegType_xmm);
}
//! comisd mem reg

//!
void compare_sd_mem_with_reg(LowOp* op, int disp, int base_reg, bool isBasePhysical,
                  int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_COMISD;
    dump_mem_reg(m, ATOM_NORMAL, OpndSize_64, disp, base_reg, isBasePhysical,
        MemoryAccess_Unknown, -1, reg, isPhysical, LowOpndRegType_xmm, NULL);
}
//! comisd reg reg

//!
void compare_sd_reg_with_reg(LowOp* op, int reg1, bool isPhysical1,
                  int reg2, bool isPhysical2) {
    Mnemonic m = Mnemonic_COMISD;
    dump_reg_reg(m, ATOM_NORMAL, OpndSize_64, reg1, isPhysical1, reg2, isPhysical2, LowOpndRegType_xmm);
}
//! fucom[p]

//!
void compare_fp_stack(bool pop, int reg, bool isDouble) { //compare ST(0) with ST(reg)
    Mnemonic m = pop ? Mnemonic_FUCOMIP : Mnemonic_FUCOMI;
    lower_reg_to_reg(m, ATOM_NORMAL, isDouble ? OpndSize_64 : OpndSize_32,
                  PhysicalReg_ST0+reg, true /*isPhysical*/, PhysicalReg_ST0, true /*isPhysical2*/, LowOpndRegType_fs);
}

/*!
\brief generate a single return instruction

*/
inline LowOp* lower_return() {
    if (gDvm.executionMode == kExecutionModeNcgO0 || !gDvmJit.scheduling) {
        stream = encoder_return(stream);
        return NULL;
    }
    LowOp * op = singletonPtr<Scheduler>()->allocateNewEmptyLIR<LowOp>();
    op->numOperands = 0;
    op->opCode = Mnemonic_RET;
    op->opCode2 = ATOM_NORMAL;
    singletonPtr<Scheduler>()->updateUseDefInformation(op);
    return op;
}

void x86_return() {
    lower_return();
}

//!test imm reg

//!
void test_imm_reg(OpndSize size, int imm, int reg, bool isPhysical) {
    dump_imm_reg(Mnemonic_TEST, ATOM_NORMAL, size, imm, reg, isPhysical, getTypeFromIntSize(size), false, NULL);
}
//!test imm mem

//!
void test_imm_mem(OpndSize size, int imm, int disp, int reg, bool isPhysical) {
    dump_imm_mem(Mnemonic_TEST, ATOM_NORMAL, size, imm, disp, reg, isPhysical, MemoryAccess_Unknown, -1, false);
}
//!alu unary op with one reg operand

//!
void alu_unary_reg(OpndSize size, ALU_Opcode opc, int reg, bool isPhysical) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_reg(m, ATOM_NORMAL_ALU, size, reg, isPhysical, getTypeFromIntSize(size));
}
//!alu unary op with one mem operand

//!
void alu_unary_mem(LowOp* op, OpndSize size, ALU_Opcode opc, int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_mem(m, ATOM_NORMAL_ALU, size, disp, base_reg, isBasePhysical);
}
//!alu binary op with immediate and one mem operand

//!
void alu_binary_imm_mem(OpndSize size, ALU_Opcode opc, int imm, int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_imm_mem(m, ATOM_NORMAL_ALU, size, imm, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, false);
}
//!alu binary op with immediate and one reg operand

//!
void alu_binary_imm_reg(OpndSize size, ALU_Opcode opc, int imm, int reg, bool isPhysical) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_imm_reg(m, ATOM_NORMAL_ALU, size, imm, reg, isPhysical, getTypeFromIntSize(size), false, NULL);
}

/**
 * @brief Performs get_VR, alu_op and set_VR but with lesser instructions
 * @details Only for 32-bit integers for now
 * @param size The Operand size (Only 32-bit currently)
 * @param opc The alu operation to perform (add or subtract)
 * @param srcVR The source VR to fetch
 * @param destVR The destination VR to set
 * @param imm The literal value to be added to VR value
 * @param tempReg A temporary register
 * @param isTempPhysical Whether the tempReg is physical
 * @param mir current lowered MIR
 * @return whether we were successful. If false, caller needs to perform get_VR, alu_op, set_VR separately
 */
bool alu_imm_to_VR(OpndSize size, ALU_Opcode opc, int srcVR, int destVR, int imm, int tempReg, bool isTempPhysical, const MIR * mir) {
    const LowOpndRegType pType = getTypeFromIntSize(size); //gp or xmm

    //We accept only add_opc and sub_opc for now
    if (opc != add_opc && opc != sub_opc) {
        return false;
    }

    //We accept only 32-bit values for now
    if (size != OpndSize_32) {
        return false;
    }

    Mnemonic alu_mn = map_of_alu_opcode_2_mnemonic[opc];

    enum CaseSrc {
        SRC_IS_CONSTANT,
        SRC_IN_MEMORY,
        SRC_IS_ALLOCATED
    };

    enum CaseDest {
        DEST_SAME_AS_SRC,
        DEST_IN_MEMORY,
        DEST_IS_ALLOCATED
    };

    if(gDvm.executionMode == kExecutionModeNcgO1) {

        /*
         * We have the following possibilities with the VRs
         *
         *  CaseSrc == 1: srcVR is constant
         *          CaseDest == 1: destVR == srcVR (We do constant += IMM)
         *          CaseDest == 2: destVR is in Memory (We do MOV IMM + const, MEM)
         *          CaseDest == 3: destVR is allocated (We do MOV IMM + const, REG)
         *
         * CaseSrc == 2: srcVR is in memory
         *          CaseDest == 1: destVR == srcVR (We do <op> IMM, MEM)
         *          CaseDest == 2: destVR is in memory (worst case. We return from here and do normal op)
         *          CaseDest == 3: destVR is allocated (We spill srcVR to same reg, then <op> imm, reg)
         *
         * CaseSrc == 3: srcVR is allocated
         *          CaseDest == 1: destVR == srcVR (We do <op> IMM, REG)
         *          CaseDest == 2: destVR is in memory (We LEA srcVR plus imm to a temp, and then set destVR to temp)
         *          CaseDest == 3: destVR is allocated (LEA IMM(srcVR), destVR)
         *
         * Now depending on above, we find out the cases, and if needed, find out the const value of src,
         * and reg allocated to dest and/or src. Memory locations, if needed, are (4*destVRNum/srcVRNum + PhysicalReg_FP)
         */

        //Initializing
        CaseSrc caseSrc = SRC_IS_CONSTANT;
        CaseDest caseDest = DEST_SAME_AS_SRC;
        int constValSrc = 0;
        int regDest = -1;
        int regSrc = -1;

        //Check the case for srcVR
        int constValue[2];
        int isConst = isVirtualRegConstant(srcVR, pType, constValue, true/*updateRefCount*/);
        int tempPhysicalReg = checkVirtualReg(srcVR, pType, 0);
        if (isConst == 3) {
            caseSrc = SRC_IS_CONSTANT;
            constValSrc = constValue[0];
        }
        else if (tempPhysicalReg != PhysicalReg_Null) {
            caseSrc = SRC_IS_ALLOCATED;
            regSrc = tempPhysicalReg;
        }
        else {
            caseSrc = SRC_IN_MEMORY;
        }

        //Check the case for destVR
        if (destVR != srcVR) {
            tempPhysicalReg = checkVirtualReg(destVR, pType, 0);
            if (tempPhysicalReg != PhysicalReg_Null) {
                caseDest = DEST_IS_ALLOCATED;
                regDest = tempPhysicalReg;
            }
            else {
                caseDest = DEST_IN_MEMORY;
            }
        }
        else {
            caseDest = DEST_SAME_AS_SRC;
        }

        int signedImm = (opc == add_opc ? imm : -imm);
        int finalSum = constValSrc + signedImm;

        //Now handle the cases
        switch (caseSrc) {
            case SRC_IS_CONSTANT:
                if (caseDest == DEST_SAME_AS_SRC) {
                    //Add or subtract
                    constValue[0] = finalSum;
                    constValue[1] = 0; //To be safe
                    return setVRToConst(destVR, size, constValue);
                }
                else if (caseDest == DEST_IN_MEMORY) {
                    // reset any physical regs for vR because
                    // we operate in memory directly
                    resetVRInCompileTable(destVR);
                    const int vrOffset = getVirtualRegOffsetRelativeToFP (destVR);
                    dump_imm_mem_noalloc(Mnemonic_MOV, size, finalSum, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, destVR, NULL);
                    return true; //Successfully updated
                }
                else if (caseDest == DEST_IS_ALLOCATED) {
                    dump_imm_reg_noalloc(Mnemonic_MOV, size, finalSum, regDest, true, pType);
                    updateRefCount(destVR, pType);
                    updateVirtualReg(destVR, pType);
                    return true; //Successfully updated
                }
                break;

            case SRC_IN_MEMORY:
                if (caseDest == DEST_SAME_AS_SRC) {

                    //For Silvermont we use a heuristic to avoid REHABQ hazard.
                    if (strcmp(ARCH_VARIANT, "x86-slm") == 0) {

                        /* Heuristic for inc optimization to avoid store/load REHABQ hazard.
                           number of adjacent bytecodes which need to be checked for avoiding
                           store/load REHABQ hazard for increment in memory */
                        const int incOptMirWindow = 2;

                        // Initialize ssa info pointer to 0
                        SSARepresentation *ssa = 0;

                        // Get SSA representation
                        if (mir != 0) {
                            ssa = mir->ssaRep;
                        }

                        // current add/sub mir should only have one def and we
                        // only care if this def is used
                        if(ssa != 0 && ssa->numDefs == 1 && ssa->usedNext != 0 &&
                           ssa->usedNext[0] != 0 && ssa->usedNext[0]->mir != 0) {

                            MIR * mirUse = ssa->usedNext[0]->mir;
                            MIR * nextMIR = mir->next;

                            // check adjacent mirs window
                            for (int i = 0; i < incOptMirWindow; i ++) {
                                if (nextMIR != 0) {

                                    // if the define variable of mir is used in adjacent mir, return
                                    // false to avoid add/sub in memory
                                    if (mirUse == nextMIR) {
                                        return false;
                                    }

                                    nextMIR = nextMIR->next;
                                }
                            }
                        }

                        // when we reach here, we can use add/sub on memory directly based
                        // on the fact that no uses of the mir's def in adjacent mirs window
                        // reset any physical regs for vR because
                        // we operate in memory directly
                        resetVRInCompileTable(destVR);
                        const int vrOffset = getVirtualRegOffsetRelativeToFP (destVR);
                        dump_imm_mem_noalloc_alu(alu_mn, size, imm, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, destVR);

                        // Successfully updated
                        return true;
                    }

                    // for other platforms
                    else {
                        // reset any physical regs for vR because
                        // we operate in memory directly
                        resetVRInCompileTable(destVR);
                        const int vrOffset = getVirtualRegOffsetRelativeToFP (destVR);
                        dump_imm_mem_noalloc_alu(alu_mn, size, imm, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, destVR);

                        // Successfully updated
                        return true;
                    }
                }
                else if (caseDest == DEST_IN_MEMORY) {
                    //We can in no way do better than get_VR, add / sub, set_VR
                    return false;
                }
                else if (caseDest == DEST_IS_ALLOCATED) {
                    //Load srcVR to regDest, and then add the constant
                    //Note that with MOVE_OPT on, this is as good as get_VR, add / sub , set_VR
                    const int vrOffset = getVirtualRegOffsetRelativeToFP (srcVR);
                    dump_mem_reg_noalloc(Mnemonic_MOV, size, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, srcVR, regDest, true, pType);
                    dump_imm_reg_noalloc_alu(alu_mn, size, imm, regDest, true, pType);
                    updateRefCount(destVR, pType);
                    updateVirtualReg(destVR, pType);
                    return true; //Successfully updated
                }
                break;

            case SRC_IS_ALLOCATED:
                if (caseDest == DEST_SAME_AS_SRC) {
                    dump_imm_reg_noalloc_alu(alu_mn, size, imm, regSrc, true, pType);
                    //We have to reduce refCounts twice. Let's call the VR with
                    //different names, even though srcVR == destVR
                    updateRefCount(srcVR, pType);
                    updateRefCount(destVR, pType);
                    updateVirtualReg(destVR, pType);
                    return true; //Successfully updated
                }
                else if (caseDest == DEST_IN_MEMORY) {
                    //We can write regSrc directly to destVR, and then ADD imm, destVR (which is 2 inst). But
                    //if destVR gets used later, we will load it to a reg anyways. That makes it 3 instructions.
                    //Instead, let's do LEA imm(regSrc), temp. And assign destVR to temp. Worst case we write
                    // back destVR soon after, which is still 2 instructions. Best case we get away with just 1.
                    dump_mem_reg_noalloc_mem(Mnemonic_LEA, ATOM_NORMAL, size, signedImm, regSrc, true, MemoryAccess_Unknown, -1, tempReg, isTempPhysical, pType);
                    set_virtual_reg(destVR, size, tempReg, isTempPhysical);
                    updateRefCount(srcVR, pType);
                    return true; //Successfully updated
                }
                else if (caseDest == DEST_IS_ALLOCATED) {
                    dump_mem_reg_noalloc(Mnemonic_LEA, size, signedImm, regSrc, true, MemoryAccess_Unknown, -1, regDest, true, pType);
                    //Done with srcVR and destVR
                    updateRefCount(srcVR, pType);
                    updateRefCount(destVR, pType);
                    updateVirtualReg(destVR, pType);
                    return true; //Successfully updated
                }
                break;

            default:
                return false;
        }
    }

    //No optimization for O0
    return false;
}

//!alu binary op with one mem operand and one reg operand

//!
void alu_binary_mem_reg(OpndSize size, ALU_Opcode opc,
             int disp, int base_reg, bool isBasePhysical,
             int reg, bool isPhysical) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_mem_reg(m, ATOM_NORMAL_ALU, size, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, reg, isPhysical, getTypeFromIntSize(size), NULL);
}

void alu_sd_binary_VR_reg(ALU_Opcode opc, int vA, int reg, bool isPhysical, bool isSD) {
    Mnemonic m;
    if(isSD) m = map_of_sse_opcode_2_mnemonic[opc];
    else m = (Mnemonic)(map_of_sse_opcode_2_mnemonic[opc]+1); //from SD to SS
    OpndSize size = isSD ? OpndSize_64 : OpndSize_32;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        LowOpndRegType type = isSD ? LowOpndRegType_xmm : LowOpndRegType_ss; //type of the mem operand
        int tmpValue[2];
        int isConst = isVirtualRegConstant(vA, type, tmpValue,
                          true/*updateRefCount*/);
        if(isConst == 3 && !isSD) {            //isConst can be 0 or 3, mem32, use xmm
            bool storedAddr = false;

            if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){
                tmpValue[1] = 0;// set higher 32 bits to zero
                // create a new record of a constant
                addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, false);

                // save mem access location in constList
                const int offset = 4; // offset is 4 for OPC_(ADD,SUB,MUL,DIV) float operations
                storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, stream, offset);

                ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                if (storedAddr == true){ // creating constant record and saving address to constant list was successful
#ifdef DEBUG_CONST
                    ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                            tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
#endif
                } else {
                    ALOGI("JIT_INFO: Error creating constant failed for VR %d, valueL %d(%x) valueH %d(%x)",
                            tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
                }
            }
            // Lower mem_reg instruction with constant to be accessed from constant data section
            if (storedAddr == true){
                int dispAddr =  getGlobalDataAddr("64bits");
                dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_32, dispAddr, PhysicalReg_Null, true,
                                   MemoryAccess_Constants, vA, reg, isPhysical, LowOpndRegType_xmm,
                                   &(gCompilationUnit->constListHead));
            } else {
                 writeBackConstVR(vA, tmpValue[0]);
                 const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
                 dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_32, vrOffset, PhysicalReg_FP, true,
                                MemoryAccess_VR, vA, reg, isPhysical, LowOpndRegType_xmm, NULL);
            }
            return;
        }
        if(isConst == 3 && isSD) {
            bool storedAddr = false;

            if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){
                // create a new record of a constant
                addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, false);

                const int offset = 4; // offset is 4 for OPC_(ADD,SUB,MUL,DIV) double operations
                // save mem access location in constList
                storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, stream, offset);

                ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                if (storedAddr == true){ // creating constant record and saving address to constant list was successful
#ifdef DEBUG_CONST
                    ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                            tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
#endif
                } else {
                    ALOGI("JIT_INFO: Error creating constant failed for VR %d, valueL %d(%x) valueH %d(%x)",
                            tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
                }
            }
            // Lower mem_reg instruction with constant to be accessed from constant data section
            if (storedAddr == true){
                int dispAddr =  getGlobalDataAddr("64bits");
                dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_64, dispAddr, PhysicalReg_Null, true,
                       MemoryAccess_Constants, vA, reg, isPhysical, LowOpndRegType_xmm, &(gCompilationUnit->constListHead));
            } else {
                writeBackConstVR(vA, tmpValue[0]);
                writeBackConstVR(vA+1, tmpValue[1]);
                const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
                dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_64, vrOffset, PhysicalReg_FP, true,
                       MemoryAccess_VR, vA, reg, isPhysical, LowOpndRegType_xmm, NULL);
            }
            return;
        }
        if(isConst == 1) writeBackConstVR(vA, tmpValue[0]);
        if(isConst == 2) writeBackConstVR(vA+1, tmpValue[1]);
        freeReg(false);

        int regAll = checkVirtualReg(vA, type, 0/*do not update refCount*/);
        if(regAll != PhysicalReg_Null) {
            startNativeCode(-1, -1); //should we use vA, type
            //CHECK: callupdateVRAtUse

            //Do not spill regAll
            gCompilationUnit->setCanSpillRegister (regAll, false);

            dump_reg_reg_noalloc_src(m, ATOM_NORMAL_ALU, size, regAll, true, reg,
                         isPhysical, LowOpndRegType_xmm);
            endNativeCode();
        }
        else {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
            dump_mem_reg_noalloc_mem(m, ATOM_NORMAL_ALU, size, vrOffset, PhysicalReg_FP, true,
                         MemoryAccess_VR, vA, reg, isPhysical, LowOpndRegType_xmm);
        }
        updateRefCount(vA, type);
    }
    else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_mem_reg(m, ATOM_NORMAL_ALU, size, vrOffset, PhysicalReg_FP, true,
                    MemoryAccess_VR, vA, reg, isPhysical, LowOpndRegType_xmm, NULL);
    }
}

//!alu binary op with a VR and one reg operand

//!
void alu_binary_VR_reg(OpndSize size, ALU_Opcode opc, int vA, int reg, bool isPhysical) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        int tmpValue[2];
        int isConst = isVirtualRegConstant(vA, getTypeFromIntSize(size), tmpValue,
                          true/*updateRefCount*/);
        if(isConst == 3 && size != OpndSize_64) {
            //allocate a register for dst
            dump_imm_reg(m, ATOM_NORMAL_ALU, size, tmpValue[0], reg, isPhysical,
                       getTypeFromIntSize(size), false, NULL);
            return;
        }
        if(isConst == 3 && size == OpndSize_64) {
            bool storedAddr = false;
            bool align = false;
            if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){

                // create a new record of a constant
                if (m == Mnemonic_PADDQ || Mnemonic_PSUBQ || Mnemonic_PAND || Mnemonic_POR || Mnemonic_PXOR) {
                    align = true;
                }
                addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, align);
                const int offset = 4; // offset is 4 for OPC_(ADD,SUB and logical) long operations
                // save mem access location in constList
                storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vA, stream, offset);

                ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                if (storedAddr == true){ // creating constant record and saving address to constant list was successful
#ifdef DEBUG_CONST
                    ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                            tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
#endif
                } else {
                    ALOGI("JIT_INFO: Error creating constant failed for regnum %d, valueL %d(%x) valueH %d(%x)",
                            tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
                }
            }
            // Lower mem_reg instruction with constant to be accessed from constant data section
            if (storedAddr == true){
                int dispAddr =  getGlobalDataAddr("64bits");
                dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_64, dispAddr, PhysicalReg_Null, true,
                       MemoryAccess_Constants, vA, reg, isPhysical, LowOpndRegType_xmm, &(gCompilationUnit->constListHead));
            } else {
                writeBackConstVR(vA, tmpValue[0]);
                writeBackConstVR(vA+1, tmpValue[1]);

                const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
                dump_mem_reg(m, ATOM_NORMAL_ALU, OpndSize_64, vrOffset, PhysicalReg_FP, true,
                       MemoryAccess_VR, vA, reg, isPhysical, LowOpndRegType_xmm, NULL);
            }
            return;
        }
        if(isConst == 1) writeBackConstVR(vA, tmpValue[0]);
        if(isConst == 2) writeBackConstVR(vA+1, tmpValue[1]);

        freeReg(false);
        int regAll = checkVirtualReg(vA, getTypeFromIntSize(size), 0);
        if(regAll != PhysicalReg_Null) {
            startNativeCode(-1, -1);

            //Do not spill regAll
            gCompilationUnit->setCanSpillRegister (regAll, false);

            dump_reg_reg_noalloc_src(m, ATOM_NORMAL_ALU, size, regAll, true, reg,
                         isPhysical, getTypeFromIntSize(size));
            endNativeCode();
        }
        else {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
            dump_mem_reg_noalloc_mem(m, ATOM_NORMAL_ALU, size, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vA, reg, isPhysical, getTypeFromIntSize(size));
        }
        updateRefCount(vA, getTypeFromIntSize(size));
    }
    else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_mem_reg(m, ATOM_NORMAL_ALU, size, vrOffset, PhysicalReg_FP, true,
            MemoryAccess_VR, vA, reg, isPhysical, getTypeFromIntSize(size), NULL);
    }
}
//!alu binary op with two reg operands

//!
void alu_binary_reg_reg(OpndSize size, ALU_Opcode opc,
                         int reg1, bool isPhysical1,
                         int reg2, bool isPhysical2) {
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_reg_reg(m, ATOM_NORMAL_ALU, size, reg1, isPhysical1, reg2, isPhysical2, getTypeFromIntSize(size));
}
//!alu binary op with one reg operand and one mem operand

//!
void alu_binary_reg_mem(OpndSize size, ALU_Opcode opc,
             int reg, bool isPhysical,
             int disp, int base_reg, bool isBasePhysical) { //destination is mem!!
    Mnemonic m;
    if(size == OpndSize_64)
        m = map_of_64_opcode_2_mnemonic[opc];
    else
        m = map_of_alu_opcode_2_mnemonic[opc];
    dump_reg_mem(m, ATOM_NORMAL_ALU, size, reg, isPhysical, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, getTypeFromIntSize(size));
}
//!FPU ops with one mem operand

//!
void fpu_mem(LowOp* op, ALU_Opcode opc, OpndSize size, int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m = map_of_fpu_opcode_2_mnemonic[opc];
    dump_mem_fp(m, ATOM_NORMAL_ALU, size, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, 0);
}
//!SSE 32-bit ALU

//!
void alu_ss_binary_reg_reg(ALU_Opcode opc, int reg, bool isPhysical,
                int reg2, bool isPhysical2) {
    Mnemonic m = (Mnemonic)(map_of_sse_opcode_2_mnemonic[opc]+1); //from SD to SS
    dump_reg_reg(m, ATOM_NORMAL_ALU, OpndSize_32, reg, isPhysical, reg2, isPhysical2, LowOpndRegType_xmm);
}
//!SSE 64-bit ALU

//!
void alu_sd_binary_reg_reg(ALU_Opcode opc, int reg, bool isPhysical,
                int reg2, bool isPhysical2) {
    Mnemonic m = map_of_sse_opcode_2_mnemonic[opc];
    dump_reg_reg(m, ATOM_NORMAL_ALU, OpndSize_64, reg, isPhysical, reg2, isPhysical2, LowOpndRegType_xmm);
}
//!push reg to native stack

//!
void push_reg_to_stack(OpndSize size, int reg, bool isPhysical) {
    dump_reg(Mnemonic_PUSH, ATOM_NORMAL, size, reg, isPhysical, getTypeFromIntSize(size));
}
//!push mem to native stack

//!
void push_mem_to_stack(OpndSize size, int disp, int base_reg, bool isBasePhysical) {
    dump_mem(Mnemonic_PUSH, ATOM_NORMAL, size, disp, base_reg, isBasePhysical);
}
//!move from reg to memory

//!
void move_reg_to_mem(OpndSize size,
                      int reg, bool isPhysical,
                      int disp, int base_reg, bool isBasePhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    dump_reg_mem(m, ATOM_NORMAL, size, reg, isPhysical, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, getTypeFromIntSize(size));
}

void xchg_reg_to_mem(OpndSize size,
                      int reg, bool isPhysical,
                      int disp, int base_reg, bool isBasePhysical) {
    dump_reg_mem(Mnemonic_XCHG, ATOM_NORMAL, size, reg, isPhysical, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, getTypeFromIntSize(size));
}
//!move from reg to memory

//!Operands are already allocated
void move_reg_to_mem_noalloc(OpndSize size,
                  int reg, bool isPhysical,
                  int disp, int base_reg, bool isBasePhysical,
                  MemoryAccessType mType, int mIndex) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    dump_reg_mem_noalloc(m, size, reg, isPhysical, disp, base_reg, isBasePhysical, mType, mIndex, getTypeFromIntSize(size));
}
//!move from memory to reg

//!
LowOpMemReg* move_mem_to_reg(OpndSize size,
                      int disp, int base_reg, bool isBasePhysical,
                      int reg, bool isPhysical) {
    //Start off with assuming we will doing an int move
    Mnemonic m = Mnemonic_MOV;

    //Now really select another mnemonic if size is different
    if (size == OpndSize_64)
    {
        m = Mnemonic_MOVQ;
    }
    else if (size == OpndSize_128)
    {
        m = Mnemonic_MOVDQA;
    }

    return dump_mem_reg(m, ATOM_NORMAL, size, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, reg, isPhysical, getTypeFromIntSize(size), NULL);
}
//!move from memory to reg

//!Operands are already allocated
LowOpMemReg* move_mem_to_reg_noalloc(OpndSize size,
                  int disp, int base_reg, bool isBasePhysical,
                  MemoryAccessType mType, int mIndex,
                  int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    return dump_mem_reg_noalloc(m, size, disp, base_reg, isBasePhysical, mType, mIndex, reg, isPhysical, getTypeFromIntSize(size));
}
//!movss from memory to reg

//!Operands are already allocated
LowOpMemReg* move_ss_mem_to_reg_noalloc(int disp, int base_reg, bool isBasePhysical,
                 MemoryAccessType mType, int mIndex,
                 int reg, bool isPhysical) {
    return dump_mem_reg_noalloc(Mnemonic_MOVSS, OpndSize_32, disp, base_reg, isBasePhysical, mType, mIndex, reg, isPhysical, LowOpndRegType_xmm);
}
//!movss from reg to memory

//!Operands are already allocated
LowOpRegMem* move_ss_reg_to_mem_noalloc(int reg, bool isPhysical,
                 int disp, int base_reg, bool isBasePhysical,
                 MemoryAccessType mType, int mIndex) {
    return dump_reg_mem_noalloc(Mnemonic_MOVSS, OpndSize_32, reg, isPhysical, disp, base_reg, isBasePhysical, mType, mIndex, LowOpndRegType_xmm);
}
//!movzx from memory to reg

//!
void movez_mem_to_reg(OpndSize size,
               int disp, int base_reg, bool isBasePhysical,
               int reg, bool isPhysical) {
    dump_movez_mem_reg(Mnemonic_MOVZX, size, disp, base_reg, isBasePhysical, reg, isPhysical);
}

//!movzx from one reg to another reg

//!
void movez_reg_to_reg(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2) {
    Mnemonic m = Mnemonic_MOVZX;
    dump_movez_reg_reg(m, size, reg, isPhysical, reg2, isPhysical2);
}

void movez_mem_disp_scale_to_reg(OpndSize size,
                 int base_reg, bool isBasePhysical,
                 int disp, int index_reg, bool isIndexPhysical, int scale,
                 int reg, bool isPhysical) {
    dump_mem_scale_reg(Mnemonic_MOVZX, size, base_reg, isBasePhysical,
                 disp, index_reg, isIndexPhysical, scale,
                 reg, isPhysical, LowOpndRegType_gp);
}
void moves_mem_disp_scale_to_reg(OpndSize size,
                  int base_reg, bool isBasePhysical,
                  int disp, int index_reg, bool isIndexPhysical, int scale,
                  int reg, bool isPhysical) {
    dump_mem_scale_reg(Mnemonic_MOVSX, size, base_reg, isBasePhysical,
                  disp, index_reg, isIndexPhysical, scale,
                  reg, isPhysical, LowOpndRegType_gp);
}
//!movsx from memory to reg

//!
void moves_mem_to_reg(LowOp* op, OpndSize size,
               int disp, int base_reg, bool isBasePhysical,
               int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_MOVSX;
    dump_moves_mem_reg(m, size, disp, base_reg, isBasePhysical, reg, isPhysical);
}
//!mov from one reg to another reg

//!
void move_reg_to_reg(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2) {
    //Start off with assuming we will doing an int move
    Mnemonic m = Mnemonic_MOV;

    //Now really select another mnemonic if size is different
    if (size == OpndSize_64)
    {
        m = Mnemonic_MOVQ;
    }
    else if (size == OpndSize_128)
    {
        m = Mnemonic_MOVDQA;
    }

    dump_reg_reg(m, ATOM_NORMAL, size, reg, isPhysical, reg2, isPhysical2, getTypeFromIntSize(size));
}

void move_gp_to_xmm (int sourceReg, bool isSourcePhysical, int destReg, bool isDestPhysical)
{
    //We are moving a double word from GP to XMM
    Mnemonic op = Mnemonic_MOVD;
    OpndSize size = OpndSize_32;
    LowOpndRegType sourceType = LowOpndRegType_gp;
    LowOpndRegType destType = LowOpndRegType_xmm;

    //Now generate the move
    dump_reg_reg_diff_types (op, ATOM_NORMAL, size, sourceReg, isSourcePhysical, sourceType, size, destReg, isDestPhysical, destType);
}

//!mov from one reg to another reg

//!Sign extends the value. Only 32-bit support.
void moves_reg_to_reg(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2) {
    Mnemonic m = Mnemonic_MOVSX;
    dump_reg_reg(m, ATOM_NORMAL, size, reg, isPhysical, reg2, isPhysical2, getTypeFromIntSize(size));
}

//!mov from one reg to another reg

//!Operands are already allocated
void move_reg_to_reg_noalloc(OpndSize size,
                  int reg, bool isPhysical,
                  int reg2, bool isPhysical2) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    dump_reg_reg_noalloc(m, size, reg, isPhysical, reg2, isPhysical2, getTypeFromIntSize(size));
}
//!move from memory to reg

//!
LowOpMemReg* move_mem_scale_to_reg(OpndSize size,
                int base_reg, bool isBasePhysical, int index_reg, bool isIndexPhysical, int scale,
                int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    return dump_mem_scale_reg(m, size, base_reg, isBasePhysical, 0/*disp*/, index_reg, isIndexPhysical, scale,
                              reg, isPhysical, getTypeFromIntSize(size));
}
void move_mem_disp_scale_to_reg(OpndSize size,
                int base_reg, bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical, int scale,
                int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    dump_mem_scale_reg(m, size, base_reg, isBasePhysical, disp, index_reg, isIndexPhysical, scale,
                              reg, isPhysical, getTypeFromIntSize(size));
}
//!move from reg to memory

//!
void move_reg_to_mem_scale(OpndSize size,
                int reg, bool isPhysical,
                int base_reg, bool isBasePhysical, int index_reg, bool isIndexPhysical, int scale) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    dump_reg_mem_scale(m, size, reg, isPhysical,
                              base_reg, isBasePhysical, 0/*disp*/, index_reg, isIndexPhysical, scale,
                              getTypeFromIntSize(size));
}
void xchg_reg_to_mem_scale(OpndSize size,
                int reg, bool isPhysical,
                int base_reg, bool isBasePhysical, int index_reg, bool isIndexPhysical, int scale) {
    dump_reg_mem_scale(Mnemonic_XCHG, size, reg, isPhysical,
                              base_reg, isBasePhysical, 0/*disp*/, index_reg, isIndexPhysical, scale,
                              getTypeFromIntSize(size));
}
void move_reg_to_mem_disp_scale(OpndSize size,
                int reg, bool isPhysical,
                int base_reg, bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical, int scale) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    dump_reg_mem_scale(m, size, reg, isPhysical,
                              base_reg, isBasePhysical, disp, index_reg, isIndexPhysical, scale,
                              getTypeFromIntSize(size));
}

void move_chain_to_mem(OpndSize size, int imm,
                        int disp, int base_reg, bool isBasePhysical) {
    dump_imm_mem(Mnemonic_MOV, ATOM_NORMAL, size, imm, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, true);
}

//!move an immediate to memory

//!
void move_imm_to_mem(OpndSize size, int imm,
                      int disp, int base_reg, bool isBasePhysical) {
    assert(size != OpndSize_64);
    if(size == OpndSize_64) {
        ALOGI("JIT_INFO: Trying to move 64-bit imm to memory");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
    dump_imm_mem(Mnemonic_MOV, ATOM_NORMAL, size, imm, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, false);
}
//! set a VR to an immediate

//!
void set_VR_to_imm(int vA, OpndSize size, int imm) {
    assert(size != OpndSize_64);
    if(size == OpndSize_64) {
        ALOGI("JIT_INFO: Trying to set VR with 64-bit imm");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        int regAll = checkVirtualReg(vA, getTypeFromIntSize(size), 0);
        if(regAll != PhysicalReg_Null) {
            dump_imm_reg_noalloc(m, size, imm, regAll, true, LowOpndRegType_gp);
            updateRefCount(vA, getTypeFromIntSize(size));
            updateVirtualReg(vA, getTypeFromIntSize(size));
            return;
        }
        //will call freeReg
        freeReg(false);
        regAll = registerAlloc(LowOpndRegType_virtual | getTypeFromIntSize(size), vA, false/*dummy*/, true, true);
        if(regAll == PhysicalReg_Null) {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
            dump_imm_mem_noalloc(m, size, imm, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, NULL);
            return;
        }

        dump_imm_reg_noalloc(m, size, imm, regAll, true, LowOpndRegType_gp);
        updateVirtualReg(vA, getTypeFromIntSize(size));
    }
    else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_imm_mem(m, ATOM_NORMAL, size, imm, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, false);
    }
}
void set_VR_to_imm_noupdateref(LowOp* op, int vA, OpndSize size, int imm) {
    return;
}
//! set a VR to an immediate

//! Do not allocate a physical register for the VR
void set_VR_to_imm_noalloc(int vA, OpndSize size, int imm) {
    assert(size != OpndSize_64);
    if(size == OpndSize_64) {
        ALOGI("JIT_INFO: Trying to move 64-bit imm to memory (noalloc)");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;

    const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
    dump_imm_mem_noalloc(m, size, imm, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA, NULL);
}

void move_chain_to_reg(OpndSize size, int imm, int reg, bool isPhysical) {
    dump_imm_reg(Mnemonic_MOV, ATOM_NORMAL, size, imm, reg, isPhysical, LowOpndRegType_gp, true, NULL);
}

//! move an immediate to reg

//!
void move_imm_to_reg(OpndSize size, int imm, int reg, bool isPhysical) {
    assert(size != OpndSize_64);
    if(size == OpndSize_64) {
        ALOGI("JIT_INFO: Trying to move 64-bit imm to register");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
    Mnemonic m = Mnemonic_MOV;
    dump_imm_reg(m, ATOM_NORMAL, size, imm, reg, isPhysical, LowOpndRegType_gp, false, NULL);
}
//! move an immediate to reg

//! The operand is already allocated
void move_imm_to_reg_noalloc(OpndSize size, int imm, int reg, bool isPhysical) {
    assert(size != OpndSize_64);
    if(size == OpndSize_64) {
        ALOGI("JIT_INFO: Trying to move 64-bit imm to register (noalloc)");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
    Mnemonic m = Mnemonic_MOV;
    dump_imm_reg_noalloc(m, size, imm, reg, isPhysical, LowOpndRegType_gp);
}
//!cmov from reg to reg

//!
void conditional_move_reg_to_reg(OpndSize size, ConditionCode cc, int reg1, bool isPhysical1, int reg, bool isPhysical) {
    Mnemonic m = (Mnemonic)(Mnemonic_CMOVcc+cc);
    dump_reg_reg(m, ATOM_NORMAL, size, reg1, isPhysical1, reg, isPhysical, LowOpndRegType_gp);
}
//!movss from memory to reg

//!
void move_ss_mem_to_reg(LowOp* op, int disp, int base_reg, bool isBasePhysical,
                         int reg, bool isPhysical) {
    dump_mem_reg(Mnemonic_MOVSS, ATOM_NORMAL, OpndSize_32, disp, base_reg, isBasePhysical,
        MemoryAccess_Unknown, -1, reg, isPhysical, LowOpndRegType_xmm, NULL);
}
//!movss from reg to memory

//!
void move_ss_reg_to_mem(LowOp* op, int reg, bool isPhysical,
                         int disp, int base_reg, bool isBasePhysical) {
    dump_reg_mem(Mnemonic_MOVSS, ATOM_NORMAL, OpndSize_32, reg, isPhysical, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, LowOpndRegType_xmm);
}
//!movsd from memory to reg

//!
void move_sd_mem_to_reg(int disp, int base_reg, bool isBasePhysical,
                         int reg, bool isPhysical) {
    dump_mem_reg(Mnemonic_MOVSD, ATOM_NORMAL, OpndSize_64, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, reg, isPhysical, LowOpndRegType_xmm, NULL);
}
//!movsd from reg to memory

//!
void move_sd_reg_to_mem(LowOp* op, int reg, bool isPhysical,
                         int disp, int base_reg, bool isBasePhysical) {
    dump_reg_mem(Mnemonic_MOVSD, ATOM_NORMAL, OpndSize_64, reg, isPhysical,
                        disp, base_reg, isBasePhysical,
                        MemoryAccess_Unknown, -1, LowOpndRegType_xmm);
}
//!load from VR to a temporary

//!
void get_virtual_reg_all(int vR, OpndSize size, int reg, bool isPhysical, Mnemonic m) {
    LowOpndRegType type = getTypeFromIntSize(size);
    LowOpndRegType pType = type;//gp or xmm
    OpndSize size2 = size;
    Mnemonic m2 = m;
    if(m == Mnemonic_MOVSS) {
        size = OpndSize_32;
        size2 = OpndSize_64;
        type = LowOpndRegType_ss;
        pType = LowOpndRegType_xmm;
        m2 = Mnemonic_MOVQ; //to move from one xmm register to another
    }
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        int tmpValue[2];
        int isConst;
        isConst = isVirtualRegConstant(vR, type, tmpValue, true/*updateRefCount*/);
        if(isConst == 3) {
            if(m == Mnemonic_MOVSS) { //load 32 bits from VR
                bool storedAddr = false;

                if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){
                    tmpValue[1] = 0;// set higher 32 bits to zero
                    // create a new record of a constant
                    addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vR, false);

                    // save mem access location in constList
                    const int offset = 4; // offset is 4 for MOVSS operations
                    storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vR, stream, offset);

                    ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                    if (storedAddr == true){ // creating constant record and saving address to constant list was successful
#ifdef DEBUG_CONST
                        ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
#endif
                    } else {
                        ALOGI("JIT_INFO: Error creating constant failed for VR %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
                    }
                }
                // Lower mem_reg instruction with constant to be accessed from constant data section
                if (storedAddr == true){
                    int dispAddr =  getGlobalDataAddr("64bits");
                    dump_mem_reg(m, ATOM_NORMAL, size, dispAddr, PhysicalReg_Null, true,
                                       MemoryAccess_Constants, vR, reg, isPhysical, pType,
                                       &(gCompilationUnit->constListHead));
                } else {
                    //VR is not mapped to a register but in memory
                    writeBackConstVR(vR, tmpValue[0]);
                    //temporary reg has "pType" (which is xmm)
                    const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
                    dump_mem_reg(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true,
                                       MemoryAccess_VR, vR, reg, isPhysical, pType, NULL);
                }
                return;
            }
            else if(m == Mnemonic_MOVSD || size == OpndSize_64) {
                bool storedAddr = false;

                if((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false){
                    // create a new record of a constant
                    addNewToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vR, false);

                    // save mem access location in constList
                    const int offset = 4; // offset is 4 for MOVSD operations
                    storedAddr = saveAddrToConstList(&(gCompilationUnit->constListHead), tmpValue[0], tmpValue[1], vR, stream, offset);

                    ConstInfo* tmpPtr = gCompilationUnit->constListHead;
                    if (storedAddr == true){ // creating constant record and saving address to constant list was successful
#ifdef DEBUG_CONST
                        ALOGD("constVRList regnum %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
#endif
                    } else {
                        ALOGI("JIT_INFO: Error creating constant failed for VR %d, valueL %d(%x) valueH %d(%x)",
                                tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueL, tmpPtr->valueH, tmpPtr->valueH);
                    }
                }
                // Lower mem_reg instruction with constant to be accessed from constant data section
                if (storedAddr == true){
                    int dispAddr =  getGlobalDataAddr("64bits");
                    dump_mem_reg(m, ATOM_NORMAL, size, dispAddr, PhysicalReg_Null, true,
                                       MemoryAccess_Constants, vR, reg, isPhysical, pType,
                                       &(gCompilationUnit->constListHead));
                } else {
                    //VR is not mapped to a register but in memory
                    writeBackConstVR(vR, tmpValue[0]);
                    writeBackConstVR(vR+1, tmpValue[1]);
                    const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
                    dump_mem_reg(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true,
                                       MemoryAccess_VR, vR, reg, isPhysical, pType, NULL);
                }
                return;
            }
            else if(size != OpndSize_64) {
                //VR is not mapped to a register
                dump_imm_reg(m, ATOM_NORMAL, size, tmpValue[0], reg, isPhysical, pType, false, NULL);
                return;
            }
        }
        if(isConst == 1) writeBackConstVR(vR, tmpValue[0]);
        if(isConst == 2) writeBackConstVR(vR+1, tmpValue[1]);

        // We want to free any variables no longer in use
        freeReg(false);

        // Do we have a physical register associated for this VR?
        int physRegForVR = checkVirtualReg(vR, type, 0);

        // If we do, then let register allocator decide if a new physical
        // register needs allocated for the temp
        if(physRegForVR != PhysicalReg_Null) {
            startNativeCode(vR, type);

            //Do not spill physRegForVR
            gCompilationUnit->setCanSpillRegister (physRegForVR, false);

            //check XFER_MEM_TO_XMM
            updateVRAtUse(vR, type, physRegForVR);
            //temporary reg has "pType"
            dump_reg_reg_noalloc_src(m2, ATOM_NORMAL, size2, physRegForVR, true, reg, isPhysical, pType);
            endNativeCode();
            updateRefCount(vR, type);
            return;
        }

        // When we get to this point, we know that we have no physical register
        // associated with the VR
        physRegForVR = registerAlloc(LowOpndRegType_virtual | type, vR, false/*dummy*/, false);

        // If we still have no physical register for the VR, then use it as
        // a memory operand
        if(physRegForVR == PhysicalReg_Null) {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
            dump_mem_reg_noalloc(m, size, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vR, reg, isPhysical, pType);
            return;
        }

        // At this point we definitely have a physical register for the VR.
        // Check to see if the temp can share same physical register.
        if(checkTempReg2(reg, pType, isPhysical, physRegForVR, vR)) {
            registerAllocMove(reg, pType, isPhysical, physRegForVR);

            const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
            dump_mem_reg_noalloc(m, size, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vR, physRegForVR, true, pType);
            updateRefCount(vR, type);
            return;
        }
        else {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
            dump_mem_reg_noalloc(m, size, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vR, physRegForVR, true, pType);
            //xmm with 32 bits
            startNativeCode(vR, type);

            //Do not spill physRegForVR
            gCompilationUnit->setCanSpillRegister (physRegForVR, false);

            dump_reg_reg_noalloc_src(m2, ATOM_NORMAL, size2, physRegForVR, true, reg, isPhysical, pType);
            endNativeCode();
            updateRefCount(vR, type);
            return;
        }
    }
    else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
        dump_mem_reg(m, ATOM_NORMAL, size, vrOffset, PhysicalReg_FP, true,
            MemoryAccess_VR, vR, reg, isPhysical, pType, NULL);
    }
}
void get_virtual_reg(int vB, OpndSize size, int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    return get_virtual_reg_all(vB, size, reg, isPhysical, m);
}
void get_virtual_reg_noalloc(int vB, OpndSize size, int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    const int vrOffset = getVirtualRegOffsetRelativeToFP (vB);
    dump_mem_reg_noalloc(m, size, vrOffset, PhysicalReg_FP, true,
        MemoryAccess_VR, vB, reg, isPhysical, getTypeFromIntSize(size));
}
//3 cases: gp, xmm, ss
//ss: the temporary register is xmm
//!load from a temporary to a VR

//!
void set_virtual_reg_all(int vA, OpndSize size, int reg, bool isPhysical, Mnemonic m) {
    LowOpndRegType type = getTypeFromIntSize(size);
    LowOpndRegType pType = type;//gp or xmm
    OpndSize size2 = size;
    Mnemonic m2 = m;
    if(m == Mnemonic_MOVSS) {
        size = OpndSize_32;
        size2 = OpndSize_64;
        type = LowOpndRegType_ss;
        pType = LowOpndRegType_xmm;
        m2 = Mnemonic_MOVQ;
    }
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        //3 cases
        //1: virtual register is already allocated to a physical register
        //   call dump_reg_reg_noalloc_dst
        //2: src reg is already allocated, VR is not yet allocated
        //   allocate VR to the same physical register used by src reg
        //   [call registerAllocMove]
        //3: both not yet allocated
        //   allocate a physical register for the VR
        //   then call dump_reg_reg_noalloc_dst
        //may need to convert from gp to xmm or the other way
        freeReg(false);
        int regAll = checkVirtualReg(vA, type, 0);
        if(regAll != PhysicalReg_Null)  { //case 1
            startNativeCode(-1, -1);

            //Do not spill regAll
            gCompilationUnit->setCanSpillRegister (regAll, false);

            dump_reg_reg_noalloc_dst(m2, size2, reg, isPhysical, regAll, true, pType); //temporary reg is "pType"
            endNativeCode();
            updateRefCount(vA, type);
            updateVirtualReg(vA, type); //will dump VR to memory, should happen afterwards
            return;
        }
        regAll = checkTempReg(reg, pType, isPhysical, vA); //vA is not used inside
        if(regAll != PhysicalReg_Null) { //case 2
            registerAllocMove(vA, LowOpndRegType_virtual | type, false, regAll, true);
            updateVirtualReg(vA, type); //will dump VR to memory, should happen afterwards
            return; //next native instruction starts at op
        }
        //case 3
        regAll = registerAlloc(LowOpndRegType_virtual | type, vA, false/*dummy*/, false, true);
        if(regAll == PhysicalReg_Null) {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
            dump_reg_mem_noalloc(m, size, reg, isPhysical, vrOffset, PhysicalReg_FP, true,
                MemoryAccess_VR, vA, pType);
            return;
        }

        startNativeCode(-1, -1);

        //Do not spill regAll
        gCompilationUnit->setCanSpillRegister (regAll, false);

        dump_reg_reg_noalloc_dst(m2, size2, reg, isPhysical, regAll, true, pType);
        endNativeCode();
        updateRefCount(vA, type);
        updateVirtualReg(vA, type);
    }
    else {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        dump_reg_mem(m, ATOM_NORMAL, size, reg, isPhysical, vrOffset, PhysicalReg_FP, true,
            MemoryAccess_VR, vA, pType);
    }
}
void set_virtual_reg(int vA, OpndSize size, int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    return set_virtual_reg_all(vA, size, reg, isPhysical, m);
}
void set_virtual_reg_noalloc(int vA, OpndSize size, int reg, bool isPhysical) {
    Mnemonic m = (size == OpndSize_64) ? Mnemonic_MOVQ : Mnemonic_MOV;
    const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
    dump_reg_mem_noalloc(m, size, reg, isPhysical, vrOffset, PhysicalReg_FP, true,
        MemoryAccess_VR, vA, getTypeFromIntSize(size));
}
void get_VR_ss(int vB, int reg, bool isPhysical) {
    return get_virtual_reg_all(vB, OpndSize_64, reg, isPhysical, Mnemonic_MOVSS);
}
void set_VR_ss(int vA, int reg, bool isPhysical) {
    return set_virtual_reg_all(vA, OpndSize_64, reg, isPhysical, Mnemonic_MOVSS);
}

/**
 * @brief load 64bits wide VR to temporary or physical register reg
 * @param vB virtual register number
 * @param reg tempory register number or physical register number
 * @param isPhysical false if reg is temporary register and true if reg is physical register
 */
void get_VR_sd(int vB, int reg, bool isPhysical) {
    return get_virtual_reg_all(vB, OpndSize_64, reg, isPhysical, Mnemonic_MOVQ);
}

/**
 * @brief store 64bits wide virtual register from register reg
 * @param vA virtual register number
 * @param reg tempory register number or physical register number
 * @param isPhysical false if reg is temporary register and true if reg is physical register
 */
void set_VR_sd(int vA, int reg, bool isPhysical) {
    return set_virtual_reg_all(vA, OpndSize_64, reg, isPhysical, Mnemonic_MOVQ);
}
////////////////////////////////// END: IA32 native instructions //////////////

//! \brief generate native code to perform null check
//!
//! \details This function does not export PC
//! \param reg
//! \param isPhysical is the reg is physical
//! \param vr the vr corresponding to reg
//!
//! \return -1 if error happened, 0 otherwise
int simpleNullCheck(int reg, bool isPhysical, int vr) {
    if(isVRNullCheck(vr, OpndSize_32)) {
        updateRefCount2(reg, LowOpndRegType_gp, isPhysical);
        num_removed_nullCheck++;
        return 0;
    }
    compare_imm_reg(OpndSize_32, 0, reg, isPhysical);
    conditional_jump (Condition_E, "common_errNullObject", false);
    int retCode = setVRNullCheck(vr, OpndSize_32);
    if (retCode < 0)
        return retCode;
    return 0;
}

/* only for O1 code generator */
int boundCheck(int vr_array, int reg_array, bool isPhysical_array,
               int vr_index, int reg_index, bool isPhysical_index,
               int exceptionNum) {
#ifdef BOUNDCHECK_OPT
    if(isVRBoundCheck(vr_array, vr_index)) {
        updateRefCount2(reg_array, LowOpndRegType_gp, isPhysical_array);
        updateRefCount2(reg_index, LowOpndRegType_gp, isPhysical_index);
        return 0;
    }
#endif
    compare_mem_reg(OpndSize_32, OFFSETOF_MEMBER(ArrayObject, length),
                    reg_array, isPhysical_array,
                    reg_index, isPhysical_index);

    char errName[256];
    sprintf(errName, "common_errArrayIndex");
    handlePotentialException(
                                       Condition_NC, Condition_C,
                                       exceptionNum, errName);
#ifdef BOUNDCHECK_OPT
    setVRBoundCheck(vr_array, vr_index);
#endif
    return 0;
}

/**
 * @brief Generates native code to perform null check
 * @param reg temporary or physical register to test
 * @param isPhysical flag to indicate whether parameter reg is physical
 * register
 * @param exceptionNum
 * @param vr virtual register for which the null check is being done
 * @return >= 0 on success
 */
int nullCheck(int reg, bool isPhysical, int exceptionNum, int vr) {
    const char * errorName = "common_errNullObject";
    int retCode = 0;

    //nullCheck optimization is available in O1 mode only
    if(gDvm.executionMode == kExecutionModeNcgO1 && isVRNullCheck(vr, OpndSize_32)) {
        updateRefCount2(reg, LowOpndRegType_gp, isPhysical);
        if(exceptionNum <= 1) {
            //TODO Updating edx references is an artifact of older codebase where null checking didn't
            //punt to the exception handling cell. These manual reference count updates should be removed
            //along with BytecodeVisitor.cpp updated to not refer to these.
            updateRefCount2(PhysicalReg_EDX, LowOpndRegType_gp, true);
            updateRefCount2(PhysicalReg_EDX, LowOpndRegType_gp, true);
        }
        num_removed_nullCheck++;
        return 0;
    }

    compare_imm_reg(OpndSize_32, 0, reg, isPhysical);

    // Get a label for exception handling restore state
    char * newStreamLabel =
            singletonPtr<ExceptionHandlingRestoreState>()->getUniqueLabel();

    // Since we are not doing the exception handling restore state inline, in case of
    // ZF=1 we must jump to the BB that restores the state
    conditional_jump(Condition_E, newStreamLabel, true);

    // We can save stream pointer now since this follows a jump and ensures that
    // scheduler already flushed stream
    char * originalStream = stream;

    if (gDvm.executionMode == kExecutionModeNcgO1) {
        rememberState(exceptionNum);
        if (exceptionNum > 1) {
            nextVersionOfHardReg(PhysicalReg_EDX, 2); //next version has 2 ref count
        }
    }

    export_pc(); //use %edx

    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("exception"); //dump GG, GL VRs
    }

    // We must flush scheduler queue now before we copy to exception handling
    // stream.
    if(gDvmJit.scheduling)
        singletonPtr<Scheduler>()->signalEndOfNativeBasicBlock();

    // Move all instructions to a deferred stream that will be dumped later
    singletonPtr<ExceptionHandlingRestoreState>()->createExceptionHandlingStream(
            originalStream, stream, errorName);

    if(gDvm.executionMode == kExecutionModeNcgO1) {
        goToState(exceptionNum);
        retCode = setVRNullCheck(vr, OpndSize_32);
        if (retCode < 0)
            return retCode;
    }

    return 0;
}

/**
 * @brief Generates code to handle potential exception
 * @param code_excep Condition code to take exception path
 * @param code_okay Condition code to skip exception
 * @param exceptionNum
 * @param errName Name of exception to handle
 * @return >= 0 on success
 */
int handlePotentialException(
                             ConditionCode code_excep, ConditionCode code_okay,
                             int exceptionNum, const char* errName) {
    // Get a label for exception handling restore state
    char * newStreamLabel =
            singletonPtr<ExceptionHandlingRestoreState>()->getUniqueLabel();

    // Since we are not doing the exception handling restore state inline, in case of
    // code_excep we must jump to the BB that restores the state
    conditional_jump(code_excep, newStreamLabel, true);

    // We can save stream pointer now since this follows a jump and ensures that
    // scheduler already flushed stream
    char * originalStream = stream;

    if (gDvm.executionMode == kExecutionModeNcgO1) {
        rememberState(exceptionNum);
        if (exceptionNum > 1) {
            nextVersionOfHardReg(PhysicalReg_EDX, 2); //next version has 2 ref count
        }
    }

    export_pc(); //use %edx

    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("exception"); //dump GG, GL VRs
    }

    if(!strcmp(errName, "common_throw_message")) {
        move_imm_to_reg(OpndSize_32, LstrInstantiationErrorPtr, PhysicalReg_ECX, true);
    }

    // We must flush scheduler queue now before we copy to exception handling
    // stream.
    if(gDvmJit.scheduling)
        singletonPtr<Scheduler>()->signalEndOfNativeBasicBlock();

    // Move all instructions to a deferred stream that will be dumped later
    singletonPtr<ExceptionHandlingRestoreState>()->createExceptionHandlingStream(
            originalStream, stream, errName);

    if(gDvm.executionMode == kExecutionModeNcgO1) {
        goToState(exceptionNum);
    }

    return 0;
}

//!generate native code to get the self pointer from glue

//!It uses one scratch register
int get_self_pointer(int reg, bool isPhysical) {
    move_mem_to_reg(OpndSize_32, offEBP_self, PhysicalReg_EBP, true, reg, isPhysical);
    return 0;
}

int get_res_classes(int reg, bool isPhysical)
{
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);

    move_mem_to_reg(OpndSize_32, offsetof(Thread, interpSave.methodClassDex), C_SCRATCH_1, isScratchPhysical, C_SCRATCH_2, isScratchPhysical);

    move_mem_to_reg(OpndSize_32, OFFSETOF_MEMBER(DvmDex, pResClasses), C_SCRATCH_2, isScratchPhysical, reg, isPhysical);

    return 0;
}

//!generate native code to get the current class object from glue

//!It uses two scratch registers
int get_glue_method_class(int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    move_mem_to_reg(OpndSize_32, offsetof(Thread, interpSave.method), C_SCRATCH_1, isScratchPhysical, C_SCRATCH_2, isScratchPhysical);
    move_mem_to_reg(OpndSize_32, OFFSETOF_MEMBER(Method, clazz), C_SCRATCH_2, isScratchPhysical, reg, isPhysical);
    return 0;
}
//!generate native code to get the current method from glue

//!It uses one scratch register
int get_glue_method(int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    move_mem_to_reg(OpndSize_32, offsetof(Thread, interpSave.method), C_SCRATCH_1, isScratchPhysical, reg, isPhysical);
    return 0;
}

//!generate native code to get SuspendCount from glue

//!It uses one scratch register
int get_suspendCount(int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    move_mem_to_reg(OpndSize_32, offsetof(Thread, suspendCount), C_SCRATCH_1, isScratchPhysical, reg, isPhysical);
    return 0;
}

//!generate native code to get retval from glue

//!It uses one scratch register
int get_return_value(OpndSize size, int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    move_mem_to_reg(size, offsetof(Thread, interpSave.retval), C_SCRATCH_1, isScratchPhysical, reg, isPhysical);
    return 0;
}

//!generate native code to set retval in glue

//!It uses one scratch register
int set_return_value(OpndSize size, int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    move_reg_to_mem(size, reg, isPhysical, offsetof(Thread, interpSave.retval), C_SCRATCH_1, isScratchPhysical);
    return 0;
}

/**
 * @brief Sets self Thread's retval.
 * @details This needs a scratch register to hold pointer to self.
 * @param size Size of return value
 * @param sourceReg Register that holds the return value.
 * @param isSourcePhysical Flag that determines if the source register is
 * physical or not. For example, the source register can be a temporary.
 * @param scratchRegForSelfThread Scratch register to use for self pointer
 * @param isScratchPhysical Marks whether the scratch register is physical
 * or not.
 * @todo Is retval set as expected for 64-bit? If retval is set as 64 bit
 * but read as 32-bit, is this correct?
 */
void set_return_value(OpndSize size, int sourceReg, bool isSourcePhysical,
        int scratchRegForSelfThread, int isScratchPhysical) {
    // Get self pointer
    get_self_pointer(scratchRegForSelfThread, isScratchPhysical);

    // Now set Thread.retval with the source register's value
    move_reg_to_mem(size, sourceReg, isSourcePhysical,
            offsetof(Thread, interpSave.retval), scratchRegForSelfThread, isScratchPhysical);
}
//!generate native code to clear exception object in glue

//!It uses two scratch registers
int clear_exception() {
    get_self_pointer(C_SCRATCH_2, isScratchPhysical);
    move_imm_to_mem(OpndSize_32, 0, offsetof(Thread, exception), C_SCRATCH_2, isScratchPhysical);
    return 0;
}
//!generate native code to get exception object in glue

//!It uses two scratch registers
int get_exception(int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_2, isScratchPhysical);
    move_mem_to_reg(OpndSize_32, offsetof(Thread, exception), C_SCRATCH_2, isScratchPhysical, reg, isPhysical);
    return 0;
}
//!generate native code to set exception object in glue

//!It uses two scratch registers
int set_exception(int reg, bool isPhysical) {
    get_self_pointer(C_SCRATCH_2, isScratchPhysical);
    move_reg_to_mem(OpndSize_32, reg, isPhysical, offsetof(Thread, exception), C_SCRATCH_2, isScratchPhysical);
    return 0;
}

#ifdef DEBUG_CALL_STACK3
int call_debug_dumpSwitch() {
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = debug_dumpSwitch;
    callFuncPtr((int)funcPtr, "debug_dumpSwitch");
    return 0;
}
#endif

int call_dvmQuasiAtomicSwap64() {
    typedef int64_t (*vmHelper)(int64_t, volatile int64_t*);
    vmHelper funcPtr = dvmQuasiAtomicSwap64;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmQuasiAtomicSwap64");
        callFuncPtr((int)funcPtr, "dvmQuasiAtomicSwap64");
        afterCall("dvmQuasiAtomicSwap64");
    } else {
        callFuncPtr((int)funcPtr, "dvmQuasiAtomicSwap64");
    }
    return 0;
}

int call_dvmQuasiAtomicRead64() {
    typedef int64_t (*vmHelper)(volatile const int64_t*);
    vmHelper funcPtr = dvmQuasiAtomicRead64;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmQuasiAtomiRead64");
        callFuncPtr((int)funcPtr, "dvmQuasiAtomicRead64");
        afterCall("dvmQuasiAtomicRead64");
        touchEax(); //for return value
        touchEdx();
    } else {
        callFuncPtr((int)funcPtr, "dvmQuasiAtomicRead64");
    }
    return 0;
}

int call_dvmJitToInterpPunt() {
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = dvmJitToInterpPunt;
    callFuncPtr((int)funcPtr, "dvmJitToInterpPunt");
    return 0;
}

void call_dvmJitToInterpNormal() {
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = dvmJitToInterpNormal;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitToInterpNormal");
        callFuncPtrImm((int)funcPtr);
        afterCall("dvmJitToInterpNormal");
        touchEbx();
    } else {
        callFuncPtrImm((int)funcPtr);
    }
    return;
}

/*
 * helper function for generating the call to dvmJitToInterpBackwardBranch
 * This transition to the interpreter is also required for
 * self-verification, in particular, in order to check
 * for control or data divergence for each loop iteration.
 */
void call_dvmJitToInterpBackwardBranch() {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitToInterpBackwardBranch");
    }
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = dvmJitToInterpBackwardBranch;
    callFuncPtrImm((int)funcPtr);
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        afterCall("dvmJitToInterpBackwardBranch");
   }
   return;
 }

int call_dvmJitToInterpTraceSelectNoChain() {
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = dvmJitToInterpTraceSelectNoChain;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitToInterpTraceSelectNoChain");
        callFuncPtr((int)funcPtr, "dvmJitToInterpTraceSelectNoChain");
        afterCall("dvmJitToInterpTraceSelectNoChain");
        touchEbx();
    } else {
        callFuncPtr((int)funcPtr, "dvmJitToInterpTraceSelectNoChain");
    }
    return 0;
}

void call_dvmJitToInterpTraceSelect() {
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = dvmJitToInterpTraceSelect;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitToInterpTraceSelect");
        callFuncPtrImm((int)funcPtr);
        afterCall("dvmJitToInterpTraceSelect");
        touchEbx();
    } else {
        callFuncPtrImm((int)funcPtr);
    }
    return;
}

int call_dvmJitToPatchPredictedChain() {
    typedef const Method * (*vmHelper)(const Method *method,
                                       Thread *self,
                                       PredictedChainingCell *cell,
                                       const ClassObject *clazz);
    vmHelper funcPtr = dvmJitToPatchPredictedChain;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitToPatchPredictedChain");
        callFuncPtr((int)funcPtr, "dvmJitToPatchPredictedChain");
        afterCall("dvmJitToPatchPredictedChain");
    } else {
        callFuncPtr((int)funcPtr, "dvmJitToPatchPredictedChain");
    }
    return 0;
}

//!generate native code to call __moddi3

//!
int call_moddi3() {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("moddi3");
        callFuncPtr((intptr_t)__moddi3, "__moddi3");
        afterCall("moddi3");
    } else {
        callFuncPtr((intptr_t)__moddi3, "__moddi3");
    }
    return 0;
}
//!generate native code to call __divdi3

//!
int call_divdi3() {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("divdi3");
        callFuncPtr((intptr_t)__divdi3, "__divdi3");
        afterCall("divdi3");
    } else {
        callFuncPtr((intptr_t)__divdi3, "__divdi3");
    }
    return 0;
}

//!generate native code to call fmod

//!
int call_fmod() {
    typedef double (*libHelper)(double, double);
    libHelper funcPtr = fmod;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("fmod");
        callFuncPtr((int)funcPtr, "fmod");
        afterCall("fmod");
    } else {
        callFuncPtr((int)funcPtr, "fmod");
    }
    return 0;
}
//!generate native code to call fmodf

//!
int call_fmodf() {
    typedef float (*libHelper)(float, float);
    libHelper funcPtr = fmodf;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("fmodf");
        callFuncPtr((int)funcPtr, "fmodf");
        afterCall("fmodf");
    } else {
        callFuncPtr((int)funcPtr, "fmodf");
    }
    return 0;
}
//!generate native code to call dvmFindCatchBlock

//!
int call_dvmFindCatchBlock() {
    //int dvmFindCatchBlock(Thread* self, int relPc, Object* exception,
    //bool doUnroll, void** newFrame)
    typedef int (*vmHelper)(Thread*, int, Object*, int, void**);
    vmHelper funcPtr = dvmFindCatchBlock;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmFindCatchBlock");
        callFuncPtr((int)funcPtr, "dvmFindCatchBlock");
        afterCall("dvmFindCatchBlock");
    } else {
        callFuncPtr((int)funcPtr, "dvmFindCatchBlock");
    }
    return 0;
}
//!generate native code to call dvmThrowVerificationError

//!
int call_dvmThrowVerificationError() {
    typedef void (*vmHelper)(const Method*, int, int);
    vmHelper funcPtr = dvmThrowVerificationError;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmThrowVerificationError");
        callFuncPtr((int)funcPtr, "dvmThrowVerificationError");
        afterCall("dvmThrowVerificationError");
    } else {
        callFuncPtr((int)funcPtr, "dvmThrowVerificationError");
    }
    return 0;
}

//!generate native code to call dvmResolveMethod

//!
int call_dvmResolveMethod() {
    //Method* dvmResolveMethod(const ClassObject* referrer, u4 methodIdx, MethodType methodType);
    typedef Method* (*vmHelper)(const ClassObject*, u4, MethodType);
    vmHelper funcPtr = dvmResolveMethod;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmResolveMethod");
        callFuncPtr((int)funcPtr, "dvmResolveMethod");
        afterCall("dvmResolveMethod");
    } else {
        callFuncPtr((int)funcPtr, "dvmResolveMethod");
    }
    return 0;
}
//!generate native code to call dvmResolveClass

//!
int call_dvmResolveClass() {
    //ClassObject* dvmResolveClass(const ClassObject* referrer, u4 classIdx, bool fromUnverifiedConstant)
    typedef ClassObject* (*vmHelper)(const ClassObject*, u4, bool);
    vmHelper funcPtr = dvmResolveClass;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmResolveClass");
        callFuncPtr((int)funcPtr, "dvmResolveClass");
        afterCall("dvmResolveClass");
    } else {
        callFuncPtr((int)funcPtr, "dvmResolveClass");
    }
    return 0;
}

//!generate native code to call dvmInstanceofNonTrivial

//!
int call_dvmInstanceofNonTrivial() {
    typedef int (*vmHelper)(const ClassObject*, const ClassObject*);
    vmHelper funcPtr = dvmInstanceofNonTrivial;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmInstanceofNonTrivial");
        callFuncPtr((int)funcPtr, "dvmInstanceofNonTrivial");
        afterCall("dvmInstanceofNonTrivial");
    } else {
        callFuncPtr((int)funcPtr, "dvmInstanceofNonTrivial");
    }
    return 0;
}
//!generate native code to call dvmThrowException

//!
int call_dvmThrow() {
    typedef void (*vmHelper)(ClassObject* exceptionClass, const char*);
    vmHelper funcPtr = dvmThrowException;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmThrowException");
        callFuncPtr((int)funcPtr, "dvmThrowException");
        afterCall("dvmThrowException");
    } else {
        callFuncPtr((int)funcPtr, "dvmThrowException");
    }
    return 0;
}
//!generate native code to call dvmThrowExceptionWithClassMessage

//!
int call_dvmThrowWithMessage() {
    typedef void (*vmHelper)(ClassObject* exceptionClass, const char*);
    vmHelper funcPtr = dvmThrowExceptionWithClassMessage;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmThrowExceptionWithClassMessage");
        callFuncPtr((int)funcPtr, "dvmThrowExceptionWithClassMessage");
        afterCall("dvmThrowExceptionWithClassMessage");
    } else {
        callFuncPtr((int)funcPtr, "dvmThrowExceptionWithClassMessage");
    }
    return 0;
}
//!generate native code to call dvmCheckSuspendPending

//!
int call_dvmCheckSuspendPending() {
    typedef bool (*vmHelper)(Thread*);
    vmHelper funcPtr = dvmCheckSuspendPending;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmCheckSuspendPending");
        callFuncPtr((int)funcPtr, "dvmCheckSuspendPending");
        afterCall("dvmCheckSuspendPending");
    } else {
        callFuncPtr((int)funcPtr, "dvmCheckSuspendPending");
    }
    return 0;
}
//!generate native code to call dvmLockObject

//!
int call_dvmLockObject() {
    typedef void (*vmHelper)(struct Thread*, struct Object*);
    vmHelper funcPtr = dvmLockObject;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmLockObject");
        callFuncPtr((int)funcPtr, "dvmLockObject");
        afterCall("dvmLockObject");
    } else {
        callFuncPtr((int)funcPtr, "dvmLockObject");
    }
    return 0;
}
//!generate native code to call dvmUnlockObject

//!
int call_dvmUnlockObject() {
    typedef bool (*vmHelper)(Thread*, Object*);
    vmHelper funcPtr = dvmUnlockObject;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmUnlockObject");
        callFuncPtr((int)funcPtr, "dvmUnlockObject");
        afterCall("dvmUnlockObject");
    } else {
        callFuncPtr((int)funcPtr, "dvmUnlockObject");
    }
    return 0;
}
//!generate native code to call dvmInitClass

//!
int call_dvmInitClass() {
    typedef bool (*vmHelper)(ClassObject*);
    vmHelper funcPtr = dvmInitClass;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmInitClass");
        callFuncPtr((int)funcPtr, "dvmInitClass");
        afterCall("dvmInitClass");
    } else {
        callFuncPtr((int)funcPtr, "dvmInitClass");
    }
    return 0;
}
//!generate native code to call dvmAllocObject

//!
int call_dvmAllocObject() {
    typedef Object* (*vmHelper)(ClassObject*, int);
    vmHelper funcPtr = dvmAllocObject;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmAllocObject");
        callFuncPtr((int)funcPtr, "dvmAllocObject");
        afterCall("dvmAllocObject");
    } else {
        callFuncPtr((int)funcPtr, "dvmAllocObject");
    }
    return 0;
}
//!generate native code to call dvmAllocArrayByClass

//!
int call_dvmAllocArrayByClass() {
    typedef ArrayObject* (*vmHelper)(ClassObject*, size_t, int);
    vmHelper funcPtr = dvmAllocArrayByClass;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmAllocArrayByClass");
        callFuncPtr((int)funcPtr, "dvmAllocArrayByClass");
        afterCall("dvmAllocArrayByClass");
    } else {
        callFuncPtr((int)funcPtr, "dvmAllocArrayByClass");
    }
    return 0;
}
//!generate native code to call dvmAllocPrimitiveArray

//!
int call_dvmAllocPrimitiveArray() {
    typedef ArrayObject* (*vmHelper)(char, size_t, int);
    vmHelper funcPtr = dvmAllocPrimitiveArray;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmAllocPrimitiveArray");
        callFuncPtr((int)funcPtr, "dvmAllocPrimitiveArray");
        afterCall("dvmAllocPrimitiveArray");
    } else {
        callFuncPtr((int)funcPtr, "dvmAllocPrimitiveArray");
    }
    return 0;
}
//!generate native code to call dvmInterpHandleFillArrayData

//!
int call_dvmInterpHandleFillArrayData() {
    typedef bool (*vmHelper)(ArrayObject*, const u2*);
    vmHelper funcPtr = dvmInterpHandleFillArrayData;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmInterpHandleFillArrayData"); //before move_imm_to_reg to avoid spilling C_SCRATCH_1
        callFuncPtr((int)funcPtr, "dvmInterpHandleFillArrayData");
        afterCall("dvmInterpHandleFillArrayData");
    } else {
        callFuncPtr((int)funcPtr, "dvmInterpHandleFillArrayData");
    }
    return 0;
}

//!generate native code to call dvmNcgHandlePackedSwitch

//!
int call_dvmNcgHandlePackedSwitch() {
    typedef s4 (*vmHelper)(const s4*, s4, u2, s4);
    vmHelper funcPtr = dvmNcgHandlePackedSwitch;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmNcgHandlePackedSwitch");
        callFuncPtr((int)funcPtr, "dvmNcgHandlePackedSwitch");
        afterCall("dvmNcgHandlePackedSwitch");
    } else {
        callFuncPtr((int)funcPtr, "dvmNcgHandlePackedSwitch");
    }
    return 0;
}

int call_dvmJitHandlePackedSwitch() {
    typedef s4 (*vmHelper)(const s4*, s4, u2, s4);
    vmHelper funcPtr = dvmJitHandlePackedSwitch;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitHandlePackedSwitch");
        callFuncPtr((int)funcPtr, "dvmJitHandlePackedSwitch");
        afterCall("dvmJitHandlePackedSwitch");
    } else {
        callFuncPtr((int)funcPtr, "dvmJitHandlePackedSwitch");
    }
    return 0;
}

//!generate native code to call dvmNcgHandleSparseSwitch

//!
int call_dvmNcgHandleSparseSwitch() {
    typedef s4 (*vmHelper)(const s4*, u2, s4);
    vmHelper funcPtr = dvmNcgHandleSparseSwitch;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmNcgHandleSparseSwitch");
        callFuncPtr((int)funcPtr, "dvmNcgHandleSparseSwitch");
        afterCall("dvmNcgHandleSparseSwitch");
    } else {
        callFuncPtr((int)funcPtr, "dvmNcgHandleSparseSwitch");
    }
    return 0;
}

int call_dvmJitHandleSparseSwitch() {
    typedef s4 (*vmHelper)(const s4*, const s4*, u2, s4);
    vmHelper funcPtr = dvmJitHandleSparseSwitch;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitHandleSparseSwitch");
        callFuncPtr((int)funcPtr, "dvmJitHandleSparseSwitch");
        afterCall("dvmJitHandleSparseSwitch");
    } else {
        callFuncPtr((int)funcPtr, "dvmJitHandleSparseSwitch");
    }
    return 0;
}

/*
 * @brief helper function to call dvmJitLookUpBigSparseSwitch
 */
void call_dvmJitLookUpBigSparseSwitch(void) {
    typedef s4 (*vmHelper)(const s4*, u2, s4);

    // get function pointer of dvmJitLookUpBigSparseSwitch
    vmHelper funcPtr = dvmJitLookUpBigSparseSwitch;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmJitLookUpBigSparseSwitch");
        callFuncPtr((int)funcPtr, "dvmJitLookUpBigSparseSwitch");
        afterCall("dvmJitLookUpBigSparseSwitch");
    } else {
        callFuncPtr((int)funcPtr, "dvmJitLookUpBigSparseSwitch");
    }
}
//!generate native code to call dvmCanPutArrayElement

//!
int call_dvmCanPutArrayElement() {
    typedef bool (*vmHelper)(const ClassObject*, const ClassObject*);
    vmHelper funcPtr = dvmCanPutArrayElement;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmCanPutArrayElement");
        callFuncPtr((int)funcPtr, "dvmCanPutArrayElement");
        afterCall("dvmCanPutArrayElement");
    } else {
        callFuncPtr((int)funcPtr, "dvmCanPutArrayElement");
    }
    return 0;
}

//!generate native code to call dvmFindInterfaceMethodInCache

//!
int call_dvmFindInterfaceMethodInCache() {
    typedef Method* (*vmHelper)(ClassObject*, u4, const Method*, DvmDex*);
    vmHelper funcPtr = dvmFindInterfaceMethodInCache;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmFindInterfaceMethodInCache");
        callFuncPtr((int)funcPtr, "dvmFindInterfaceMethodInCache");
        afterCall("dvmFindInterfaceMethodInCache");
    } else {
        callFuncPtr((int)funcPtr, "dvmFindInterfaceMethodInCache");
    }
    return 0;
}

//!generate native code to call dvmHandleStackOverflow

//!
int call_dvmHandleStackOverflow() {
    typedef void (*vmHelper)(Thread*, const Method*);
    vmHelper funcPtr = dvmHandleStackOverflow;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmHandleStackOverflow");
        callFuncPtr((int)funcPtr, "dvmHandleStackOverflow");
        afterCall("dvmHandleStackOverflow");
    } else {
        callFuncPtr((int)funcPtr, "dvmHandleStackOverflow");
    }
    return 0;
}
//!generate native code to call dvmResolveString

//!
int call_dvmResolveString() {
    //StringObject* dvmResolveString(const ClassObject* referrer, u4 stringIdx)
    typedef StringObject* (*vmHelper)(const ClassObject*, u4);
    vmHelper funcPtr = dvmResolveString;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmResolveString");
        callFuncPtr((int)funcPtr, "dvmResolveString");
        afterCall("dvmResolveString");
    } else {
        callFuncPtr((int)funcPtr, "dvmResolveString");
    }
    return 0;
}
//!generate native code to call dvmResolveInstField

//!
int call_dvmResolveInstField() {
    //InstField* dvmResolveInstField(const ClassObject* referrer, u4 ifieldIdx)
    typedef InstField* (*vmHelper)(const ClassObject*, u4);
    vmHelper funcPtr = dvmResolveInstField;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmResolveInstField");
        callFuncPtr((int)funcPtr, "dvmResolveInstField");
        afterCall("dvmResolveInstField");
    } else {
        callFuncPtr((int)funcPtr, "dvmResolveInstField");
    }
    return 0;
}
//!generate native code to call dvmResolveStaticField

//!
int call_dvmResolveStaticField() {
    //StaticField* dvmResolveStaticField(const ClassObject* referrer, u4 sfieldIdx)
    typedef StaticField* (*vmHelper)(const ClassObject*, u4);
    vmHelper funcPtr = dvmResolveStaticField;
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("dvmResolveStaticField");
        callFuncPtr((int)funcPtr, "dvmResolveStaticField");
        afterCall("dvmResolveStaticField");
    } else {
        callFuncPtr((int)funcPtr, "dvmResolveStaticField");
    }
    return 0;
}

#define P_GPR_2 PhysicalReg_ECX
/*!
\brief This function is used to resolve a string reference

INPUT: const pool index in %eax

OUTPUT: resolved string in %eax

The registers are hard-coded, 2 physical registers %esi and %edx are used as scratch registers;
It calls a C function dvmResolveString;
The only register that is still live after this function is ebx
*/
int const_string_resolve() {
    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EDX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;
    if (insertLabel(".const_string_resolve", false) == -1)
        return -1;
    //method stored in glue structure as well as on the interpreted stack
    get_glue_method_class(P_GPR_2, true);
    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EAX, true, 4, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, P_GPR_2, true, 0, PhysicalReg_ESP, true);
    call_dvmResolveString();
    load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    compare_imm_reg( OpndSize_32, 0, PhysicalReg_EAX, true);
    conditional_jump(Condition_E, "common_exceptionThrown", false);
    x86_return();
    return 0;
}
#undef P_GPR_2
/*!
\brief This function is used to resolve a class

INPUT: const pool index in argument "indexReg" (%eax)

OUTPUT: resolved class in %eax

The registers are hard-coded, 3 physical registers (%esi, %edx, startLR:%eax) are used as scratch registers.
It calls a C function dvmResolveClass;
The only register that is still live after this function is ebx
*/
int resolve_class2(
           int startLR/*scratch register*/, bool isPhysical, int indexReg/*const pool index*/,
           bool indexPhysical, int thirdArg) {
    if (insertLabel(".class_resolve", false) == -1)
        return -1;

    //Get call back
    void (*backEndSymbolCreationCallback) (const char *, void *) =
        gDvmJit.jitFramework.backEndSymbolCreationCallback;

    //Call it if we have one
    if (backEndSymbolCreationCallback != 0)
    {
        backEndSymbolCreationCallback (".class_resolve", (void*) stream);
    }

    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EDX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;

    //push index to stack first, to free indexReg
    load_effective_addr(-12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, indexReg, indexPhysical, 4, PhysicalReg_ESP, true);
    get_glue_method_class(startLR, isPhysical);
    move_imm_to_mem(OpndSize_32, thirdArg, 8, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, startLR, isPhysical, 0, PhysicalReg_ESP, true);
    call_dvmResolveClass();
    load_effective_addr(12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    conditional_jump(Condition_E, "common_exceptionThrown", false);

    x86_return();
    return 0;
}
/*!
\brief This function is used to resolve a method, and it is called once with %eax for both indexReg and startLR

INPUT: const pool index in argument "indexReg" (%eax)

OUTPUT: resolved method in %eax

The registers are hard-coded, 3 physical registers (%esi, %edx, startLR:%eax) are used as scratch registers.
It calls a C function dvmResolveMethod;
The only register that is still live after this function is ebx
*/
int resolve_method2(
            int startLR/*logical register index*/, bool isPhysical, int indexReg/*const pool index*/,
            bool indexPhysical,
            int thirdArg/*VIRTUAL*/) {
    if(thirdArg == METHOD_VIRTUAL) {
        if (insertLabel(".virtual_method_resolve", false) == -1)
            return -1;
    }
    else if(thirdArg == METHOD_DIRECT) {
        if (insertLabel(".direct_method_resolve", false) == -1)
            return -1;
    }
    else if(thirdArg == METHOD_STATIC) {
        if (insertLabel(".static_method_resolve", false) == -1)
            return -1;
    }

    load_effective_addr(-12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, indexReg, indexPhysical, 4, PhysicalReg_ESP, true);

    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EDX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;
    get_glue_method_class(startLR, isPhysical);

    move_imm_to_mem(OpndSize_32, thirdArg, 8, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, startLR, isPhysical, 0, PhysicalReg_ESP, true);
    call_dvmResolveMethod();
    load_effective_addr(12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    conditional_jump(Condition_E, "common_exceptionThrown", false);

    x86_return();
    return 0;
}
/*!
\brief This function is used to resolve an instance field

INPUT: const pool index in argument "indexReg" (%eax)

OUTPUT: resolved field in %eax

The registers are hard-coded, 3 physical registers (%esi, %edx, startLR:%eax) are used as scratch registers.
It calls a C function dvmResolveInstField;
The only register that is still live after this function is ebx
*/
int resolve_inst_field2(
            int startLR/*logical register index*/, bool isPhysical,
            int indexReg/*const pool index*/, bool indexPhysical) {
    if (insertLabel(".inst_field_resolve", false) == -1)
        return -1;
    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EDX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;

    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, indexReg, indexPhysical, 4, PhysicalReg_ESP, true);
    //method stored in glue structure as well as interpreted stack
    get_glue_method_class(startLR, isPhysical);
    move_reg_to_mem(OpndSize_32, startLR, isPhysical, 0, PhysicalReg_ESP, true);
    call_dvmResolveInstField();
    load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    conditional_jump(Condition_E, "common_exceptionThrown", false);

    x86_return();
    return 0;
}
/*!
\brief This function is used to resolve a static field

INPUT: const pool index in argument "indexReg" (%eax)

OUTPUT: resolved field in %eax

The registers are hard-coded, 3 physical registers (%esi, %edx, startLR:%eax) are used as scratch registers.
It calls a C function dvmResolveStaticField;
The only register that is still live after this function is ebx
*/
int resolve_static_field2(
              int startLR/*logical register index*/, bool isPhysical, int indexReg/*const pool index*/,
              bool indexPhysical) {
    if (insertLabel(".static_field_resolve", false) == -1)
        return -1;
    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EDX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;

    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, indexReg, indexPhysical, 4, PhysicalReg_ESP, true);
    get_glue_method_class(startLR, isPhysical);
    move_reg_to_mem(OpndSize_32, startLR, isPhysical, 0, PhysicalReg_ESP, true);
    call_dvmResolveStaticField();
    load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    conditional_jump(Condition_E, "common_exceptionThrown", false);

    x86_return();
    return 0;
}

int pushAllRegs() {
    load_effective_addr(-28, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_EAX, true, 24, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_EBX, true, 20, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_ECX, true, 16, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_EDX, true, 12, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_ESI, true, 8, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_EDI, true, 4, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    move_reg_to_mem_noalloc(OpndSize_32, PhysicalReg_EBP, true, 0, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1);
    return 0;
}
int popAllRegs() {
    move_mem_to_reg_noalloc(OpndSize_32, 24, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_EAX, true);
    move_mem_to_reg_noalloc(OpndSize_32, 20, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_EBX, true);
    move_mem_to_reg_noalloc(OpndSize_32, 16, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_ECX, true);
    move_mem_to_reg_noalloc(OpndSize_32, 12, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_EDX, true);
    move_mem_to_reg_noalloc(OpndSize_32, 8, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_ESI, true);
    move_mem_to_reg_noalloc(OpndSize_32, 4, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_EDI, true);
    move_mem_to_reg_noalloc(OpndSize_32, 0, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, PhysicalReg_EBP, true);
    load_effective_addr(28, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    return 0;
}

/* align the relative offset of jmp/jcc and movl within 16B */
void alignOffset(int offset) {
    int rem, nop_size;

    if ((uint)(stream + offset) % 16 > 12) {
        rem = (uint)(stream + offset) % 16;
        nop_size = (16 - rem) % 16;
        stream = encoder_nops(nop_size, stream);
    }
}

/**
  * @brief align a pointer to n-bytes aligned
  * @param addr the pointer need to be aligned
  * @param n n-bytes aligned
  * @return aligned address
  */
char* align(char* addr, int n) {
    char* alignedAddr = reinterpret_cast<char*>((reinterpret_cast<unsigned int>(addr) + (n-1)) & ~(n-1));
    return alignedAddr;
}
/**
 * @brief Returns whether the jump to BB needs alignment
 * because it might be patched later on.
 * @param bb Basic Block to look at
 * @return Returns true for all chaining cells and also for
 * the prebackward block.
 */
bool doesJumpToBBNeedAlignment(BasicBlock * bb) {
    // Get type for this BB
    int type = static_cast<int>(bb->blockType);

    if ((type >= static_cast<int> (kChainingCellNormal)
            && type < static_cast<int> (kChainingCellLast))
            && type != static_cast<int> (kChainingCellBackwardBranch))
    {
        //We always return true if BB is a chaining cell except if it is
        //backward branch chaining cell. The reason we make exception for
        //BBCC is because we always patch the jump to preBackwardBlock and
        //not the jump to the chaining cell
        return true;
    }
    else if (type == static_cast<int> (kPreBackwardBlock))
    {
        //Since the prebackward block is always used in front of
        //backward branch chaining cell and the jump to it is
        //the one being patched, we also return true.
        return true;
    }
    else
    {
        return false;
    }
}

#ifdef WITH_SELF_VERIFICATION
int selfVerificationLoad(int addr, int opndSize) {
    assert (opndSize != OpndSize_64);
    assert(addr != 0);

    Thread *self = dvmThreadSelf();
    ShadowSpace *shadowSpace = self->shadowSpace;
    ShadowHeap *heapSpacePtr;

    assert(shadowSpace != 0);
    assert(shadowSpace->heapSpace != 0);
    int data = 0;

    for (heapSpacePtr = shadowSpace->heapSpace;
        heapSpacePtr != shadowSpace->heapSpaceTail; heapSpacePtr++) {
        if (heapSpacePtr->addr == addr) {
            addr = (unsigned int)(&(heapSpacePtr->data));
            break;
        }
    }

    /* load addr from the shadow heap, native addr-> shadow heap addr
     * if not found load the data from the native heap
     */
    switch (opndSize) {
        case OpndSize_8:
            data = *(reinterpret_cast<u1*> (addr));
            break;
        case OpndSize_16:
            data = *(reinterpret_cast<u2*> (addr));
            break;
        //signed versions
        case 0x11:  //signed OpndSize_8
            data = *(reinterpret_cast<s1*> (addr));
            break;
        case 0x22:  //signed OpndSize_16
            data = *(reinterpret_cast<s2*> (addr));
            break;
        case OpndSize_32:
            data = *(reinterpret_cast<u4*> (addr));
            break;
        default:
            ALOGE("*** ERROR: BAD SIZE IN selfVerificationLoad: %d", opndSize);
            data = 0;
            dvmAbort();
            break;
    }

#if defined(SELF_VERIFICATION_LOG)
    ALOGD("*** HEAP LOAD: Addr: %#x Data: %d Size: %d", addr, data, opndSize);
#endif
    return data;
}

void selfVerificationStore(int addr, int data, int opndSize)
{
    assert(addr != 0);
    Thread *self = dvmThreadSelf();
    ShadowSpace *shadowSpace = self->shadowSpace;
    ShadowHeap *heapSpacePtr;

    assert(shadowSpace != 0);
    assert(shadowSpace->heapSpace != 0);
#if defined(SELF_VERIFICATION_LOG)
    ALOGD("*** HEAP STORE: Addr: %#x Data: %d Size: %d", addr, data, opndSize);
#endif
    for (heapSpacePtr = shadowSpace->heapSpace;
         heapSpacePtr != shadowSpace->heapSpaceTail; heapSpacePtr++) {
        if (heapSpacePtr->addr == addr) {
            break;
        }
    }

    //If the store addr is requested for the first time, its not present in the
    //heap so add it to the shadow heap.
    if (heapSpacePtr == shadowSpace->heapSpaceTail) {
        heapSpacePtr->addr = addr;
        shadowSpace->heapSpaceTail++;
        // shadow heap can contain HEAP_SPACE(JIT_MAX_TRACE_LEN) number of entries
        if(shadowSpace->heapSpaceTail > &(shadowSpace->heapSpace[HEAP_SPACE])) {
            ALOGD("*** Shadow HEAP store ran out of space, aborting VM");
            dvmAbort();
        }
    }

    addr = ((unsigned int) &(heapSpacePtr->data));
    switch (opndSize) {
        case OpndSize_8:
            *(reinterpret_cast<u1*>(addr)) = data;
            break;
        case OpndSize_16:
            *(reinterpret_cast<u2*>(addr)) = data;
            break;
        case OpndSize_32:
            *(reinterpret_cast<u4*>(addr)) = data;
            break;
        default:
            ALOGE("*** ERROR: BAD SIZE IN selfVerificationSave: %d", opndSize);
            dvmAbort();
            break;
    }
}

void selfVerificationLoadDoubleword(int addr)
{
    assert(addr != 0);
    Thread *self = dvmThreadSelf();
    ShadowSpace* shadowSpace = self->shadowSpace;
    ShadowHeap* heapSpacePtr;
    int byte_count = 0;

    assert(shadowSpace != 0);
    assert(shadowSpace->heapSpace != 0);
    //TODO: do a volatile GET_WIDE implementation

    int addr2 = addr+4;
    /* load data and data2 from the native heap
     * so in case this address is not stored in the shadow heap
     * the value loaded from the native heap is used, else
     * it is overwritten with the value from the shadow stack
     */
    unsigned int data = *(reinterpret_cast<unsigned int*> (addr));
    unsigned int data2 = *(reinterpret_cast<unsigned int*> (addr2));

    for (heapSpacePtr = shadowSpace->heapSpace;
         heapSpacePtr != shadowSpace->heapSpaceTail; heapSpacePtr++) {
        if (heapSpacePtr->addr == addr) {
            data = heapSpacePtr->data;
            byte_count++;
        } else if (heapSpacePtr->addr == addr2) {
            data2 = heapSpacePtr->data;
            byte_count++;
        }
        if(byte_count == 2) break;
    }

#if defined(SELF_VERIFICATION_LOG)
    ALOGD("*** HEAP LOAD DOUBLEWORD: Addr: %#x Data: %#x Data2: %#x",
        addr, data, data2);
#endif

    // xmm6 is scratch; passing value back to aget_common_nohelper in xmm7
    asm volatile (
            "movd %0, %%xmm6\n\t"
            "movd %1, %%xmm7\n\t"
            "psllq $32, %%xmm6\n\t"
            "paddq %%xmm6, %%xmm7"
            :
            : "rm" (data2), "rm" (data)
            : "xmm6", "xmm7");
}

void selfVerificationStoreDoubleword(int addr, s8 double_data)
{
    assert(addr != 0);

    Thread *self = dvmThreadSelf();
    ShadowSpace *shadowSpace = self->shadowSpace;
    ShadowHeap *heapSpacePtr;

    assert(shadowSpace != 0);
    assert(shadowSpace->heapSpace != 0);

    int addr2 = addr+4;
    int data = double_data;
    int data2 = double_data >> 32;
    bool store1 = false, store2 = false;

#if defined(SELF_VERIFICATION_LOG)
    ALOGD("*** HEAP STORE DOUBLEWORD: Addr: %#x Data: %#x, Data2: %#x",
        addr, data, data2);
#endif

    //data++; data2++;  // test case for SV detection

    for (heapSpacePtr = shadowSpace->heapSpace;
         heapSpacePtr != shadowSpace->heapSpaceTail; heapSpacePtr++) {
        if (heapSpacePtr->addr == addr) {
            heapSpacePtr->data = data;
            store1 = true;
        } else if (heapSpacePtr->addr == addr2) {
            heapSpacePtr->data = data2;
            store2 = true;
        }
        if(store1 && store2) {
            break;
        }
    }

    // shadow heap can contain HEAP_SPACE(JIT_MAX_TRACE_LEN) number of entries
    int additions = store1 ? 1 : 0;
    additions += store2 ? 1 : 0;
    if((shadowSpace->heapSpaceTail + additions) >= &(shadowSpace->heapSpace[HEAP_SPACE])) {
        ALOGD("*** Shadow HEAP store ran out of space, aborting VM");
        dvmAbort();
    }

    if (store1 == false) {
        shadowSpace->heapSpaceTail->addr = addr;
        shadowSpace->heapSpaceTail->data = data;
        shadowSpace->heapSpaceTail++;
    }
    if (store2 == false) {
        shadowSpace->heapSpaceTail->addr = addr2;
        shadowSpace->heapSpaceTail->data = data2;
        shadowSpace->heapSpaceTail++;
    }
}

int call_selfVerificationLoad(void) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("selfVerificationLoad");
    }
    typedef int (*vmHelper)(int, int);
    vmHelper funcPtr = selfVerificationLoad;
    callFuncPtr((int)funcPtr, "selfVerificationLoad");
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        afterCall("selfVerificationLoad");
    }
    return 0;
}

int call_selfVerificationLoadDoubleword(void) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("selfVerificationLoadDoubleword");
    }
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = selfVerificationLoadDoubleword;
    callFuncPtr((int)funcPtr, "selfVerificationLoadDoubleword");
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        afterCall("selfVerificationLoadDoubleword");
    }
    return 0;
}

int call_selfVerificationStore(void) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("selfVerificationStore");
    }
    typedef void (*vmHelper)(int, int, int);
    vmHelper funcPtr = selfVerificationStore;
    callFuncPtr((int)funcPtr, "selfVerificationStore");
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        afterCall("selfVerificationStore");
    }
    return 0;
}

int call_selfVerificationStoreDoubleword(void) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall("selfVerificationStoreDoubleword");
    }
    typedef void (*vmHelper)(int, s8);
    vmHelper funcPtr = selfVerificationStoreDoubleword;
    callFuncPtr((int)funcPtr, "selfVerificationStoreDoubleword");
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        afterCall("selfVerificationStoreDoubleword");
    }
    return 0;
}
#endif

void pushCallerSavedRegs(void) {
    load_effective_addr(-12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EAX, true, 8, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_ECX, true, 4, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EDX, true, 0, PhysicalReg_ESP, true);
}

void popCallerSavedRegs(void) {
    move_mem_to_reg(OpndSize_32, 8, PhysicalReg_ESP, true,  PhysicalReg_EAX, true);
    move_mem_to_reg(OpndSize_32, 4, PhysicalReg_ESP, true,  PhysicalReg_ECX, true);
    move_mem_to_reg(OpndSize_32, 0, PhysicalReg_ESP, true,  PhysicalReg_EDX, true);
    load_effective_addr(12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
}

//! \brief compareAndExchange with one reg operand and one mem operand
//! used for implementing monitor-enter
//! \param size operand size
//! \param reg src register
//! \param isPhysical if reg is a physical register
//! \param disp displacement offset
//! \param base_reg physical register (PhysicalReg type) or a logical register
//! \param isBasePhysical if base_reg is a physical register
void compareAndExchange(OpndSize size,
             int reg, bool isPhysical,
             int disp, int base_reg, bool isBasePhysical) {
    dump_reg_mem(Mnemonic_CMPXCHG, ATOM_NORMAL, size, reg, isPhysical, disp, base_reg, isBasePhysical, MemoryAccess_Unknown, -1, getTypeFromIntSize(size));
}

bool vec_shuffle_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize,
        unsigned short mask)
{
    Mnemonic opLow = Mnemonic_Null, opHigh = Mnemonic_Null;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            opLow = Mnemonic_PSHUFLW;
            //We use a PSHUFD for the high because it will ensure to duplicate the lower half
            opHigh = Mnemonic_PSHUFD;
            break;
        case OpndSize_32:
            opLow = Mnemonic_PSHUFD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized shuffle for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    //We are applying a vector operation so it must be on xmm
    OpndSize registerSize = OpndSize_128;
    LowOpndRegType registerType = LowOpndRegType_xmm;

    //Shuffles take immediate of size 8
    OpndSize immediateSize = OpndSize_8;

    //Check if we need to shuffle the low 64-bits
    if (opLow != Mnemonic_Null)
    {
        dump_imm_reg_reg (opLow, ATOM_NORMAL_ALU, mask, immediateSize, srcReg, isSrcPhysical, registerType,
                registerSize, destReg, isDestPhysical, registerType, registerSize);
    }

    //Now check if we need to shuffle the high 64-bits
    if (opHigh != Mnemonic_Null)
    {
        dump_imm_reg_reg (opHigh, ATOM_NORMAL_ALU, mask, immediateSize, srcReg, isSrcPhysical, registerType,
                registerSize, destReg, isDestPhysical, registerType, registerSize);
    }

    //If we get here everything went well
    return true;
}

bool vec_add_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_8:
            op = Mnemonic_PADDB;
            break;
        case OpndSize_16:
            op = Mnemonic_PADDW;
            break;
        case OpndSize_32:
            op = Mnemonic_PADDD;
            break;
        case OpndSize_64:
            op = Mnemonic_PADDQ;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized addition for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_mul_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PMULLW;
            break;
        case OpndSize_32:
            if (dvmCompilerArchitectureSupportsSSE41 () == false)
            {
                ALOGD ("JIT_INFO: Architecture does not have SSE4.1 so there is no pmulld support");
                SET_JIT_ERROR (kJitErrorUnsupportedInstruction);
                return false;
            }
            op = Mnemonic_PMULLD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized multiplication for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_and_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical)
{
    Mnemonic op = Mnemonic_PAND;

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_or_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical)
{
    Mnemonic op = Mnemonic_POR;

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_xor_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical)
{
    Mnemonic op = Mnemonic_PXOR;

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_sub_reg_reg (int subtrahend, bool isSubtrahendPhysical, int minuend, bool isMinuendPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_8:
            op = Mnemonic_PSUBB;
            break;
        case OpndSize_16:
            op = Mnemonic_PSUBW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSUBD;
            break;
        case OpndSize_64:
            op = Mnemonic_PSUBQ;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized subtract for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    // minuend - subtrahend = dest (result of difference is stored in dest)
    const int src = subtrahend;
    const int dest = minuend;

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, src, isSubtrahendPhysical, dest, isMinuendPhysical,
            LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_shift_left_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PSLLW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSLLD;
            break;
        case OpndSize_64:
            op = Mnemonic_PSLLQ;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized shift left for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_shift_left_imm_reg (int numBits, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PSLLW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSLLD;
            break;
        case OpndSize_64:
            op = Mnemonic_PSLLQ;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized shift left for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_imm_reg (op, ATOM_NORMAL_ALU, OpndSize_128, numBits, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_signed_shift_right_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PSRAW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSRAD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized signed shift right for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_signed_shift_right_imm_reg (int numBits, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PSRAW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSRAD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized signed shift right for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_imm_reg (op, ATOM_NORMAL_ALU, OpndSize_128, numBits, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_unsigned_shift_right_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PSRLW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSRLD;
            break;
        case OpndSize_64:
            op = Mnemonic_PSRLQ;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized unsigned shift right for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_unsigned_shift_right_imm_reg (int numBits, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PSRLW;
            break;
        case OpndSize_32:
            op = Mnemonic_PSRLD;
            break;
        case OpndSize_64:
            op = Mnemonic_PSRLQ;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized unsigned shift right for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_imm_reg (op, ATOM_NORMAL_ALU, OpndSize_128, numBits, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_horizontal_add_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PHADDW;
            break;
        case OpndSize_32:
            op = Mnemonic_PHADDD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized horizontal add for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_horizontal_sub_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PHSUBW;
            break;
        case OpndSize_32:
            op = Mnemonic_PHSUBD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized horizontal subtract for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    dump_reg_reg (op, ATOM_NORMAL_ALU, OpndSize_128, srcReg, isSrcPhysical, destReg, isDestPhysical, LowOpndRegType_xmm);

    //If we get here everything went well
    return true;
}

bool vec_extract_imm_reg_reg (int index, int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize)
{
    Mnemonic op;

    switch (vectorUnitSize)
    {
        case OpndSize_16:
            op = Mnemonic_PEXTRW;
            break;
        case OpndSize_32:
            if (dvmCompilerArchitectureSupportsSSE41 () == false)
            {
                ALOGD ("JIT_INFO: Architecture does not have SSE4.1 so there is no pextrd support");
                SET_JIT_ERROR (kJitErrorUnsupportedInstruction);
                return false;
            }
            op = Mnemonic_PEXTRD;
            break;
        default:
            ALOGD ("JIT_INFO: Cannot support vectorized extract for size %d", vectorUnitSize);
            SET_JIT_ERROR (kJitErrorUnsupportedVectorization);
            return false;
            break;
    }

    //We are applying a vector operation so source must be xmm
    OpndSize sourceSize = OpndSize_128;
    LowOpndRegType srcPhysicalType = LowOpndRegType_xmm;

    //However we are extracting to a GP
    OpndSize destSize = OpndSize_32;
    LowOpndRegType destPhysicalType = LowOpndRegType_gp;

    //Extract take immediate of size 8
    OpndSize immediateSize = OpndSize_8;

    //Now generate the extract
    dump_imm_reg_reg (op, ATOM_NORMAL_ALU, index, immediateSize, srcReg, isSrcPhysical, srcPhysicalType, sourceSize,
            destReg, isDestPhysical, destPhysicalType, destSize);

    //If we get here everything went well
    return true;
}

int getVirtualRegOffsetRelativeToFP (int vR)
{
    //Each virtual register is 32-bit and thus we multiply its size with the VR number
    int offset = vR * sizeof (u4);

    //We may have had a frame pointer change for our compilation unit so we need to take into account
    offset += gCompilationUnit->getFPAdjustment ();

    return offset;
}
