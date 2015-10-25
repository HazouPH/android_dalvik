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

#ifndef DALVIK_VM_REGISTERIZATIONME_H_
#define DALVIK_VM_REGISTERIZATIONME_H_

//Forward declarations
struct BasicBlock;
struct CompilationUnit;
class Pass;

/**
 * @brief Perform middle-end registerization on loops and pass registerization hints to backend
 * @param cUnit the CompilationUnit
 * @param pass the current Pass
 */
void dvmCompilerRegisterize (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Add the writeback hints for the backend, the pass sets all registers to be spilled
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns false because nothing in the BB changes per se
 */
bool dvmCompilerWriteBackAll (CompilationUnit *cUnit, BasicBlock *bb);

#endif
