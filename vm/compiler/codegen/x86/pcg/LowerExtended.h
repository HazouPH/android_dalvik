/*
 * Copyright  (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0  (the "License");
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

#ifndef H_LOWEREXTENDED
#define H_LOWEREXTENDED

// Forward declarations
struct BasicBlockPCG;
class CompilationUnitPCG;
struct MIR;

/**
 * @brief Translate a lower bound check instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateLowerBoundCheck (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a bound check instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateBoundCheck (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a null check instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateNullCheck (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate safety checks for array accesses in a loop.
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param countUp distinguishes between countUp and countDown loops.
 */
void dvmCompilerPcgTranslateLoopChecks (CompilationUnitPCG *cUnit, MIR *mir, bool countUp);

/**
 * @brief Translate the extended prediction inline check MIR
 * @details Does a class check to verify if the inlined path should be taken or the path with invoke in case of mispredict.
 * @param cUnit The Compilation Unit
 * @param mir The MIR with opcode kMirOpCheckInlinePrediction
 */
void dvmCompilerPcgTranslatePredictionInlineCheck (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translates the extended MIR used for doing a stack overflow check
 * @param cUnit The compilation unit
 * @param mir The MIR with opcode kMirOpCheckStackOverflow
 */
void dvmCompilerPcgTranslateCheckStackOverflow (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a packed set instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslatePackedSet (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a packed constant instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslatePackedConst (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a packed move instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslatePackedMove (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate vectorized bytecodes of the form XMMdest = XMMdest "op" XMMsrc which operate on packed values
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
bool dvmCompilerTranslatePackedAlu (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a packed add reduce instruction
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslatePackedAddReduce (CompilationUnitPCG *cUnit, MIR *mir);

#endif // H_LOWEREXTENDED
