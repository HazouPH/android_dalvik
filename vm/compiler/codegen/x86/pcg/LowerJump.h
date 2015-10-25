/*
 * Copyright (C); 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");;
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

#ifndef H_LOWERJUMP
#define H_LOWERJUMP

#include "libpcg.h"

//Forward declarations
struct BasicBlockPCG;
class CompilationUnitPCG;

/**
 * @brief Generate a jump instruction
 * @param bb the BasicBlockPCG
 */
void dvmCompilerPcgTranslateDirectJumpToBlock (BasicBlockPCG *bb);

/**
 * @brief Create JSR
 * @param cUnit the CompilationUnitPCG
 * @param symbol the CGSymbol
 * @param parms the instruction (default: 0)
 * @param reg the register (default: CGInstInvalid)
 * @return the instruction generated
 */
CGInst dvmCompilerPcgCreateJsr (CompilationUnitPCG *cUnit, CGSymbol symbol, CGInst *parms = 0, CGInst reg = CGInstInvalid);

#if defined(WITH_JIT_TUNING)
/**
 * @brief Create JSR and pass kSwitchOverflow on the stack
 * @param symbol the CGSymbol
 * @param parms the instruction (default: 0)
 * @param reg the register (default: CGInstInvalid)
 * @return the instruction generated
 */
CGInst dvmCompilerPcgCreateJsrWithKSwitchOverflow (CompilationUnitPCG * cUnit, CGSymbol symbol, CGInst *parms = 0, CGInst reg = CGInstInvalid);
#endif

/**
 * @brief Create a conditional jump
 * @param bb the BasicBlockPCG
 * @param a left side of the condition
 * @param cond the condition
 * @param b the right side of the condition
 */
void dvmCompilerPcgTranslateConditionalJump (BasicBlockPCG *bb, CGInst a, const char *cond, CGInst b);

/**
 * @brief Translate if
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param cond the condition
 */
void dvmCompilerPcgTranslateIf (CompilationUnitPCG *cUnit, MIR *mir, const char *cond);

/**
 * @brief Translate a if-zero instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param cond the condition
 */
void dvmCompilerPcgTranslateIfZero (CompilationUnitPCG *cUnit, MIR *mir, const char *cond);

/**
 * @brief Translate a if-floating instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param opSize the size of the operation
 * @param nanVal the nan value
 */
void dvmCompilerPcgTranslateIfFp (CompilationUnitPCG *cUnit, MIR *mir, uint32_t opSize, int nanVal);

/**
 * @brief Translate a goto instruction
 * @param bb the BasicBlockPCG
 */
void dvmCompilerPcgTranslateGoto (BasicBlockPCG *bb);

/**
 * @brief Generate the write backs
 * @param cUnit the CompilationUnitPCG
 * @param bv the BitVector representing what to write back
 * @details Generate code for writing back all members of the set, bv, to their home virtual registers. This routine modifies tempBV.  Callers may use tempBV to pass the input set but must be aware that its contents will change.
 */
void dvmCompilerPcgGenerateWritebacks (CompilationUnitPCG *cUnit, BitVector *bv);

/**
 * @brief Generate the write backs
 * @param cUnit the CompilationUnitPCG
 * @param from the BasicBlockPCG from which we are coming
 * @param to the BasicBlockPCG from which we are going to
 * @details This function specially handles "to" blocks that are kPreBackwardBlocks. It calls dvmCompilerPcgRemoveNonPhiNodes, such that only phi nodes are written back to their home VRs inside the loop.
 */
void dvmCompilerPcgDoWritebacksOnEdge (CompilationUnitPCG *cUnit, BasicBlockPCG *from, BasicBlockPCG *to);

#endif
