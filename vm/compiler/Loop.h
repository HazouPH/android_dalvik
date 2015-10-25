/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef DALVIK_VM_LOOP_H_
#define DALVIK_VM_LOOP_H_

#include "Dalvik.h"
#include "CompilerInternals.h"

typedef struct LoopAnalysis {
    BitVector *isIndVarV;               // length == numSSAReg
    GrowableList *ivList;               // induction variables
    GrowableList *arrayAccessInfo;      // hoisted checks for array accesses
    int numBasicIV;                     // number of basic induction variables
    int ssaBIV;                         // basic IV in SSA name
    bool isCountUpLoop;                 // count up or down loop
    Opcode loopBranchOpcode;            // OP_IF_XXX for the loop back branch
    int loopBranchMIROffset;            // MIR offset in method for loop back branch
    int endConditionReg;                // vB in "vA op vB"
    LIR *branchToBody;                  // branch over to the body from entry
    LIR *branchToPCR;                   // branch over to the PCR cell
    bool bodyIsClean;                   // loop body cannot throw any exceptions
} LoopAnalysis;

bool dvmCompilerFilterLoopBlocks(CompilationUnit *cUnit);

/**
 * @brief Mark off any non loop header block and register them in the gDvm.knownNonLoopHeaderCache
 * @param cUnit the CompilationUnit
 */
void dvmCompilerLoopMarkOffNonHeaderBlocks (CompilationUnit *cUnit);

/*
 * An unexecuted code path may contain unresolved fields or classes. Before we
 * have a quiet resolver we simply bail out of the loop compilation mode.
 */
#define BAIL_LOOP_COMPILATION() if (cUnit->jitMode == kJitLoop) {       \
                                    cUnit->quitLoopMode = true;         \
                                    return false;                       \
                                }

#ifdef ARCH_IA32
//Externalizing certain Loop.cpp functionalities
/**
 * @brief Whether or not the loop is a simple counted loop
 * @param cUnit the CompilationUnit
 * @return whether or not the loop is a simple counted loop
 */
bool dvmCompilerSimpleCountedLoop (CompilationUnit *cUnit);

/**
 * @brief Handle the detection of Induction Variable Array Accesses
 * @param cUnit the CompilationUnit
 * @return whether or not the optimization has been performed
 */
bool dvmCompilerIVArrayAccess (CompilationUnit *cUnit);

/**
 * @brief Hoist the access checks that are detected by the dvmCompielrIVArrayAccess function
 * @param cUnit the CompilationUnit
 */
void dvmCompilerHoistIVArrayAccess (CompilationUnit *cUnit);

/**
 * @brief Dump the Induction Variable list, only if DEBUG_LOOP is defined in Loop.cpp
 * @param cUnit the CompilationUnit
 */
void dvmCompilerDumpIVList (CompilationUnit *cUnit);

/**
 * @brief Dump the detected constants, only if DEBUG_LOOP is defined in Loop.cpp
 * @param cUnit the CompilationUnit
 */
void dvmCompilerDumpConstants (CompilationUnit *cUnit);

/**
 * @brief Dump the hoisted checks, only if DEBUG_LOOP is defined in Loop.cpp
 * @param cUnit the CompilationUnit
 */
void dvmCompilerDumpHoistedChecks (CompilationUnit *cUnit);

/**
 * @brief do the body code motion including hoisting of checks
 * @param cUnit the CompilationUnit
 */
void dvmCompilerBodyCodeMotion (CompilationUnit *cUnit, Pass *currentPass);

/**
 * @brief generate hoisted range and NULL checks
 * @param cUnit the CompilationUnit
 * @param pass the Compilation pass
 */
void dvmCompilerGenHoistedChecks(CompilationUnit *cUnit, Pass* pass);

#endif

/**
 * @brief Inserts a basic block before Backward Chaining Cell, and one before the preheader
 * @details The newly inserted basic blocks takes the write back requests and
 * MIRs from chaining cell in order to help backend which cannot handle
 * Backward Chaining Cell like a bytecode block. It also ensures that the
 * newly inserted block is the taken branch, so if the backward was fallthrough
 * it flips the condition.
 * @param cUnit the CompilationUnit
 * @param currentPass the Pass
 */
void dvmCompilerInsertLoopHelperBlocks (CompilationUnit *cUnit, Pass *currentPass);
#endif  // DALVIK_VM_LOOP_H_
