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

#ifndef H_LOWERGETPUT
#define H_LOWERGETPUT

/**
 * @brief Translate an Aget
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateAget (CompilationUnitPCG *cUnit, MIR *mir);


/**
 * @brief Translate an Aput
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateAput (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate an Aput Object
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateAputObject (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate an Iput
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateIput (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate an Sput/Sget
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param isGet is the instruction a get?
 * @param isObj is the instruction an object related instruction?
 * @param isWide is the instruction an 8-byte get/put?
 * @param isVolatile is the instruction volatile?
 * @return whether or not the lowering succeeded
 */
bool dvmCompilerPcgTranslateSgetSput (CompilationUnitPCG *cUnit, MIR *mir, bool isGet, bool isObj, bool isWide, bool isVolatile);

/**
 * @brief Translate an Iget Object Quick
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateIgetObjectQuick (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate an Iget Wide Quick
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateIgetWideQuick (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the iget/iput bytecodes
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param isWide is the iget/iput wide?
 * @param isGet is the bytecode a get ?
 * @param isObj is the bytecode getting/setting objects?
 * @param isVolatile is the bytecode volatile?
 */
void dvmCompilerPcgTranslateIgetIput (CompilationUnitPCG *cUnit, MIR *mir, bool isGet, bool isObj, bool isWide, bool isVolatile);
#endif
