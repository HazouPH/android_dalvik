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

#ifndef H_LOWERALU
#define H_LOWERALU

//Forward Declaration
class CompilationUnitPCG;
struct MIR;

/**
 * @brief Translate a move instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateMove (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a move wide instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateMoveWide (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a const instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateConst (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a const 16 instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateConst16 (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a const 4 instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateConst4 (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a const high 16 instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateConstHigh16 (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate constant wide
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param val the value
 */
void dvmCompilerPcgTranslateConstWide (CompilationUnitPCG *cUnit, MIR *mir, u8 val);

/**
 * @brief Translate a 2addr or 3addr LL reg instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateLLreg (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate a LLreg operation
 * @param cUnit the CompilationUnitPCG
 * @param opcode the opcode
 * @param ssaA the first SSA register
 * @param ssaB the second SSA register
 */
void dvmCompilerPcgTranslateLLregOp (CompilationUnitPCG *cUnit, const char *opcode, int ssaA, int ssaB);

/**
 * @brief Translate a LLreg shift operation
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateLLregShift (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate a 2addr or 3addr float
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateFloat (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate OP_REM_FLOAT
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @return the result
 */
void dvmCompilerPcgTranslateRemFloat (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate OP_REM_DOUBLE
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @return the result
 */
void dvmCompilerPcgTranslateRemDouble (CompilationUnitPCG *cUnit, MIR *mir);
/**
 * @brief Translate a 2addr or 3addr double
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateDouble (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate an integer to a floating point
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param size the size of the floating point
 */
void dvmCompilerPcgTranslateIntToFP (CompilationUnitPCG *cUnit, MIR *mir, int32_t size);

/**
 * @brief Translate an long to a floating point
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param size the size of the floating point
 */
void dvmCompilerPcgTranslateLongToFP (CompilationUnitPCG *cUnit, MIR *mir, int32_t size);

/**
 * @brief Translate a floating point to an integer
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param size the size of the floating point
 */
void dvmCompilerPcgTranslateFPToInt (CompilationUnitPCG *cUnit, MIR *mir, int32_t size);

/**
 * @brief Translate a floating point to an long integer
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param size the size of the floating point
 */
void dvmCompilerPcgTranslateFPToLong (CompilationUnitPCG *cUnit, MIR *mir, int32_t size);

/**
 * @brief Translate an float to a double
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateFloatToDouble (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a double to a float
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateDoubleToFloat (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the int to long
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateIntToLong (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the long to int
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateLongToInt (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a float negation
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateNegFloat (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a double negation
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateNegDouble (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate an integer op op instruction: vA = vB op vC
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateIntOpOp (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate an integer op op instruction: vA = vB op literal
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateIntOpLit (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate the rsub instruction
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateRsub (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate an integer operation
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param opcode the opcode
 */
void dvmCompilerPcgTranslateIntOp (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode);

/**
 * @brief Translate the extension of an integer
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param opcode the opcode
 * @param imm the number of bits
 */
void dvmCompilerPcgTranslateIntExtend (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode, int imm);

/**
 * @brief Translate the cmp long opcode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateCmpLong (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the div rem opcode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateDivRemInt (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the div rem int opcode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateDivRemIntLit (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the div rem long opcode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateDivRemLong (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate constant helper
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param val the value
 */
void dvmCompilerPcgTranslateConstHelper (CompilationUnitPCG *cUnit, MIR *mir, u4 val);

/**
 * @brief Translate the const string bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateConstString (CompilationUnitPCG *cUnit, MIR *mir);
#endif
