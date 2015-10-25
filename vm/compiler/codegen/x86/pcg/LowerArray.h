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

#ifndef H_LOWERARRAY
#define H_LOWERARRAY

/**
 * @brief Translate the new array bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateNewArray (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the array length the bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateArrayLength (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the fill array data
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateFillArrayData (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the filled new array
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateFilledNewArray (CompilationUnitPCG *cUnit, MIR *mir);
#endif
