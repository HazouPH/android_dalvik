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

#ifndef H_LOWERCALL
#define H_LOWERCALL

#include "libpcg.h"
#include "Lower.h"

//Forward Declaration
struct BasicBlockPCG;
class CompilationUnitPCG;
struct MIR;

/**
 * @brief Translate the invoke virtual opcodes
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateInvokeVirtual (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Convert callee to a type
 * @param calleeMethod the Method
 * @return the type of the argument
 */
ArgsDoneType dvmCompilerPcgTranslateConvertCalleeToType (const Method* calleeMethod);

/**
 * @brief Translate the invoke super bytecodes
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateInvokeStaticSuper (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the invoke virtual interface
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateInvokeInterface (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the new invoke direct bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateInvokeDirect (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the return bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param isVoid is the return void?
 */
void dvmCompilerPcgTranslateReturn (CompilationUnitPCG *cUnit, MIR *mir, bool isVoid);

/**
 * @brief Translate an execute inline
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateExecuteInline (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate a move result
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateMoveResult (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Handle the invoke argument header
 * @param value the ArgsDoneType
 * @return the section label's name
 */
const char *dvmCompilerPcgHandleInvokeArgsHeader (int value);

/**
 * @brief Get the fallthrough symbol
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlockPCG
 * @return the PCG instruction which is the load of the symbol
 */
CGInst dvmCompilerPcgGetFallthroughTargetSymbolInst (CompilationUnitPCG *cUnit, BasicBlockPCG *bb);

/**
 * @brief Invoke a method
 * @param cUnit the CompilationUnitPCG
 * @param mir the invoke MIR
 * @param form the form type of the argument
 * @param methodToCall what method are we calling
 * @param fallThroughTargetSymInst a CGInst containing a load of the fall through target symbol
 */
void dvmCompilerPcgCommonInvokeMethodJmp (CompilationUnitPCG *cUnit, const MIR *mir, ArgsDoneType form, CGInst methodToCall, CGInst fallThroughTargetSymInst);

/**
 * @brief Handle the storing of invoke arguments
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgStoreInvokeArgs (CompilationUnitPCG *cUnit, const MIR *mir);

/**
 * @brief Get an invoke target
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlockPCG
 * @param needsCfgArc do we need a CFG arc?
 * @return the invoke target
 */
CGSymbol dvmCompilerPcgGetInvokeTarget (CompilationUnitPCG *cUnit, const BasicBlockPCG *bb, bool *needsCfgArc = 0);
#endif
