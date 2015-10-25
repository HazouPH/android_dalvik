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

#ifndef H_DALVIK_INSTRUCTIONGENERATION
#define H_DALVIK_INSTRUCTIONGENERATION

#include "Dalvik.h"
#include "enc_wrapper.h"

//Forward declarations
class BasicBlock_O1;

/**
 * @brief Generate a Null check
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedNullCheck (CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generate a Bound check:
      vA arrayReg
      arg[0] -> determines whether it is a constant or a register
      arg[1] -> register or constant

      is idx < 0 || idx >= array.length ?
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedBoundCheck (CompilationUnit *cUnit, MIR *mir);

//use O0 code generator for hoisted checks outside of the loop
/**
 * @brief Generate the null and upper bound check for a count up loop
 * vA = arrayReg;
 * vB = idxReg;
 * vC = endConditionReg;
 * arg[0] = maxC
 * arg[1] = minC
 * arg[2] = loopBranchConditionCode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedChecksForCountUpLoop(CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generate the null and upper bound check for a count down loop
 * vA = arrayReg;
 * vB = idxReg;
 * vC = endConditionReg;
 * arg[0] = maxC
 * arg[1] = minC
 * arg[2] = loopBranchConditionCode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedChecksForCountDownLoop(CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generate the lower bound check
 * vA = arrayReg;
 * vB = minimum constant used for the array;
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void genHoistedLowerBoundCheck(CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generates the validation for a predicted inline.
 * @details Generates code that checks the class of inlined method against the actual class.
 * In case of mispredict it jumps to "taken" path which contains the actual invoke.
 * vC: The register that holds "this" reference
 * vB: Class object pointer
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
bool genValidationForPredictedInline (CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generate native code for the registerize extended instruction
 * @details vA of the mir has the register to set in a physical register
 * @param cUnit The Compilation Unit
 * @param bb The basic block that contains the request
 * @param mir the MIR instruction representing the registerization request
 * @return Returns whether or not it successfully handled the request
 */
bool genRegisterize (CompilationUnit *cUnit, BasicBlock_O1 *bb, MIR *mir);

/**
 * @brief Generate a move instruction for double-quadword register
 * @param cUnit The CompilationUnit
 * @param mir The MIR containing the source and destination reg numbers
 * @return whether the operation was successful
 */
bool genMove128b (CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generate a packed set of an XMM from a VR
 * @details Create a 128 bit value, with all 128 / vC values equal to vB
 * @param cUnit The CompilationUnit
 * @param mir The MIR containing the dest XMM and source VR number
 * @return whether the operation was successful
 */
bool genPackedSet (CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Generate a constant load of double-quadword size to an XMM
 * @param cUnit The CompilationUnit
 * @param mir The MIR containing the dest XMM and the data bytes
 * @return whether the operation was successful
 */
bool genMoveData128b (CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Used to generate a packed alu operation
 * @details The vectorized registers are mapped 1:1 to XMM registers
 * @param cUnit The compilation unit
 * @param mir The vectorized MIR
 * @param aluOperation The operation for which to generate vectorize operation for.
 * Add, mul, sub, or, xor, and, shl, shr, and sar are supported.
 * @return Returns whether the generation was successful
 */
bool genPackedAlu (CompilationUnit *cUnit, MIR *mir, ALU_Opcode aluOperation);

/**
 * @brief Used to generate a horizontal operation whose result will be reduced to a VR
 * @param cUnit The compilation unit
 * @param mir The vectorized MIR
 * @param horizontalOperation The horizontal operation to use for reduction. Add and sub are supported.
 * @return Returns whether the generation was successful
 */
bool genPackedHorizontalOperationWithReduce (CompilationUnit *cUnit, MIR *mir, ALU_Opcode horizontalOperation);

/**
 * @brief Used to generate a reduction from XMM to virtual register
 * @param cUnit The compilation unit
 * @param mir The vectorized MIR
 * @return Returns whether the generation was successful
 */
bool genPackedReduce (CompilationUnit *cUnit, MIR *mir);

/**
 * @brief Used to generate stack overflow check
 * @param cUnit The compilation unit
 * @param mir The MIR with extended opcode kMirOpCheckStackOverflow
 * @return Returns whether code generation was successful
 */
bool genCheckStackOverflow (CompilationUnit *cUnit, MIR *mir);

#endif
