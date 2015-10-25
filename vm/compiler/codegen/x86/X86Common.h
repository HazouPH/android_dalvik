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

#ifndef H_X86SPECIFIC
#define H_X86SPECIFIC

#include <stdlib.h>

//Forward Declarations
struct BasicBlock;
struct CompilationUnit;

/**
 * @brief Architecture specific BasicBlock creator
 * @details Initializes x86 specific BasicBlock fields
 * @return newly created BasicBlock
 */
BasicBlock *dvmCompilerArchSpecificNewBB(void);

/**
 * @brief Architecture specific CompilationErrorHandler creator
 * @details Initializes x86 specific CompilationErrorHandler fields
 * @return newly created CompilationErrorHandler
 */
CompilationErrorHandler *dvmCompilerArchSpecificNewCompilationErrorHandler (void);

/**
 * @brief Architecture specific BasicBlock printing
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @param file the File in which to dump the BasicBlock
 * @param beforeMIRs is this call performed before generating the dumps for the MIRs
 */
void dvmCompilerDumpArchSpecificBB(CompilationUnit *cUnit, BasicBlock *bb, FILE *file, bool beforeMIRs);

/**
 * @brief Does the architecture support SSE4.1?
 * @return whether or not the architecture supports SSE4.1
 */
bool dvmCompilerArchitectureSupportsSSE41 (void);

/**
 * @brief Does the architecture support SSE4.2?
 * @return whether or not the architecture supports SSE4.2
 */
bool dvmCompilerArchitectureSupportsSSE42 (void);

#endif
