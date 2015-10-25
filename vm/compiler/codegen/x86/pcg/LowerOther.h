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

#ifndef H_LOWEROTHER
#define H_LOWEROTHER

#include "libpcg.h"

//Forward declarations
struct BasicBlockPCG;
class CompilationUnitPCG;

CGInst dvmCompilerPcgGenerateVMPtrMov (CompilationUnitPCG *cUnit);

/**
 * @brief Create a move of pcgFramePtr, and set the hard register.
 * @details This is used before jsr/jmp/call to ensure pcgFramePtr is in the right place.
 * @param cUnit the CompilationUnitPCG
 */
CGInst dvmCompilerPcgGenerateFramePtrMov (const CompilationUnitPCG *cUnit);

/**
 * @brief Translate the MonitorExit bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateMonitorExit (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the MonitorEnter bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateMonitorEnter (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Mark card table
 * @param cUnit the CompilationUnitPCG
 * @param val the value
 * @param targetAddr the target address
 */
void dvmCompilerPcgTranslateMarkCard (const CompilationUnitPCG *cUnit, CGInst val, CGInst targetAddr);

/**
 * @brief Mark card table but we know targetAddr is not null
 * @param cUnit the CompilationUnitPCG
 * @param targetAddr the target address
 */
void dvmCompilerPcgTranslateMarkCardNotNull (const CompilationUnitPCG *cUnit, CGInst targetAddr);

/**
 * @brief Translate the instanceOf bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateInstanceOf (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the check cast bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateCheckCast (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the new instance bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateNewInstance (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the packed switch bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslatePackedSwitch (CompilationUnitPCG *cUnit, MIR *mir);

/**
 * @brief Translate the sparse switch bytecode
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateSparseSwitch (CompilationUnitPCG *cUnit, MIR *mir);
/**
 * @brief Add the VR interface code
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgAddVRInterfaceCode (CompilationUnitPCG *cUnit);

#endif
