/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_INVARIANTREMOVAL
#define H_INVARIANTREMOVAL

//Forward declarations
struct CompilationUnit;
class Pass;

/**
 * @brief InvariantRemovaling pass: remove any invariants from the loop
 * @param cUnit the CompilationUnit
 * @param curPass the current pass
 */
void dvmCompilerInvariantRemoval (CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Hoist any Iget/Iput couples out of the loop
 * @param cUnit the CompilationUnit
 * @param curPass the current pass
 */
void dvmCompilerIgetIputRemoval (CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Checks to make sure that the invariant removal optimization can be done.
 * @details Does not approve any loops that can throw exceptions and are not
 * guaranteed to throw the exception on first iteration.
 * @param cUnit the CompilationUnit
 * @param curPass the current pass
 * @return Returns whether invariant removal optimization can be done.
 */
bool dvmCompilerInvariantRemovalGate (const CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Attempts to sink invariants as far out of loop as it can: to loop exits or after the loop
 * @param cUnit The compilation unit
 * @param curPass The current pass
 */
void dvmCompilerInvariantSinking (CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Checks whether invariant sinking can be done
 * @param cUnit The compilation unit
 * @param curPass The current pass
 * @return Returns whether we can do invariant sinking
 */
bool dvmCompilerInvariantSinkingGate (const CompilationUnit *cUnit, Pass *curPass);

#endif
