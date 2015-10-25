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

#ifndef DALVIK_VM_BBOPTIMIZATION
#define DALVIK_VM_BBOPTIMIZATION

//Forward declarations
struct BasicBlock;
struct CompilationUnit;
class Pass;
class LoopInformation;

/**
 * @brief Merge BasicBlocks together to reduce unnecessary jumps
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns whether we provide new opportunity for merging
 */
bool dvmCompilerMergeBasicBlocks (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Form a loop
 * @details make sure it is a bottom formed
 * loop (or make it so), add a preloop block and an exit block
 * @param cUnit the CompilationUnit
 * @param pass the current pass
 * @return whether we changed anything (always false)
 */
void dvmCompilerFormLoop (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Test if the loop has been formed properly
 * @param cUnit the CompilationUnit
 * @param pass the current pass
 */
void dvmCompilerTestLoop (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Form the loop by inserting pre/post blocks and backward branches for loops
 * @param cUnit the CompilationUnit
 * @param pass the Pass
 */
void dvmCompilerFormOldLoop (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Reorder BasicBlocks
 * @param cUnit the CompilationUnit
 * @param pass the Pass
 */
void dvmCompilerReorder (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Converts the bytecodes that use the 2addr opcode to their normal equivalents.
 * @details For example, it converts "add-int/2addr v1, v2" to "add-int v1, v1, v2".
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return Returns whether anything was changed in the BasicBlock.
 */
bool dvmCompilerConvert2addr (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Goes through the basic block and ensures that for all invokes there is a Singleton/Predicted
 * chaining cell on taken branch.
 * @details May split basic block so therefore it is necessary to use an "all nodes and new" approach
 * @param cUnit The compilation unit
 * @param bb The basic block through which to look for invokes
 * @return Returns true if the CFG was at all updated
 */
bool dvmCompilerAddInvokeSupportBlocks (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Goes through the given basic block and tries to inline invokes
 * @param cUnit The compilation unit
 * @param bb The basic block through which to look for invokes
 * @return Returns true if any method inlining was done
 */
bool dvmCompilerMethodInlining (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Ensures that
 *  (a) each chaining cell has only one predecessor
 *  (b) each hot chaining cell has pre-hot chaining cell predecessor
 * @param cUnit the CompilationUnit.
 * @param bb The BasicBlock to look at. Anything other than chaining cells is ignored.
 */
bool dvmCompilerFixChainingCells (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Peel the loop if needed
 * @param cUnit the CompilationUnit
 * @param info the loop information
 * @return whether peel was successful
 */
bool dvmCompilerPeel (CompilationUnit *cUnit, LoopInformation *info);

/**
 * @brief Perform local value numbering
 * @param cUnit the CompilationUnit
 */
void dvmCompilerLocalValueNumbering (CompilationUnit *cUnit);

/**
 * @brief Remove the gotos
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return returns whether we changed anything in the BasicBlock
 */
bool dvmCompilerRemoveGoto (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Copy propagation for move and return
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return did we change the BasicBlock
 */
bool dvmCompilerCopyPropagationMoveReturn (CompilationUnit *cUnit, BasicBlock *bb);

#endif
