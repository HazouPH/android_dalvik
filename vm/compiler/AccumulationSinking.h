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

#ifndef DALVIK_VM_ACCUMULATIONSINKING_H_
#define DALVIK_VM_ACCUMULATIONSINKING_H_

#include "CompilerUtility.h"

//Forward declarations
struct BasicBlock;
struct CompilationUnit;
class Pass;
class LoopInformation;
class Expression;

/**
 * @brief Perform accumulation sinking optimization
 * @param cUnit the CompilationUnit
 * @param pass the current Pass
 */
void dvmCompilerAccumulationSinking (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Verify that the loop is capable of having the accumulations sunk
 * @param cUnit the CompilationUnit
 * @param curPass the Pass
 * @return whether the sink accumulation sinking
 */
bool dvmCompilerSinkAccumulationsGate (const CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Get the Expression values for all the inter-iteration variables of the loop
 * @param cUnit The CompilationUnit containing the loop
 * @param info The LoopInformation corresponding to the loop
 * @param[out] ivExpressions A vector of Expression to be filled up by this function
 */
void dvmCompilerGetLoopExpressions (CompilationUnit *cUnit, LoopInformation *info, std::vector<Expression *> &ivExpressions);
#endif
