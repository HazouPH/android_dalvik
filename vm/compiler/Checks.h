/*
 * Copyright (C) 2012 Intel Corporation
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

#ifndef DALVIK_VM_CHECKS_H_
#define DALVIK_VM_CHECKS_H_

#include "Dalvik.h"

//Forward declaration
class Pass;

/**
 * @brief Used to generate and add a hoisted null check
 * @param hoistToBB The basic block to which to append the hoisted null check
 * @param objectReg The dalvik register on which to do a null check
 * @return Returns if null check was successfully generated
 */
bool dvmCompilerGenerateNullCheckHoist (BasicBlock *hoistToBB, int objectReg);

/**
 * @brief Remove redundant checks start function
 * @param cUnit the CompilationUnit
 * @param curPass the current pass Pass
 */
void dvmCompilerStartCheckRemoval (CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Remove redundant checks end function
 * @param cUnit the CompilationUnit
 * @param curPass the current pass Pass
 */
void dvmCompilerEndCheckRemoval (CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Remove redundant checks
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns whether we changed anything in the BasicBlock
 */
bool dvmCompilerCheckRemoval (CompilationUnit *cUnit, BasicBlock *bb);

#endif
