/*
* Copyright (C) 2012 Intel Corporation
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

#ifndef DALVIK_VM_PASSDRIVER_H_
#define DALVIK_VM_PASSDRIVER_H_

/**
 * @brief PassInstrumentation allows the user to modify existing passes and add new ones
 */
enum PassInstrumentation
{
    kPassInstrumentationInsertBefore,   /**< @brief Insert before the Pass */
    kPassInstrumentationInsertAfter,    /**< @brief Insert after the Pass */
    kPassInstrumentationReplace,        /**< @brief Replace a Pass */
};

//Forward Declaration
struct CompilationUnit;
class Pass;

/**
 * @brief Run the pass on the cUnit
 * @details The pass is run completely, including the gate, pre and post functions
 * @param cUnit The CompilationUnit to run the pass on
 * @param pass The Pass which needs to be run
 * @return Whether the pass could be successfully applied
 */
bool dvmCompilerRunPass (CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Find the pass with the passName and run it with the cUnit
 * @param cUnit The CompilationUnit to run the pass on
 * @param passName The name of the pass which should be run
 * @return Whether the pass could be successfully applied
 */
bool dvmCompilerRunPass (CompilationUnit *cUnit, const char *passName);

/**
  * @brief The loop optimization driver: calls each pass from the gPasses array
  * @brief The pass driver: calls each pass from the gPasses array
  * @param cUnit the Compilation Unit
  */
void dvmCompilerLaunchPassDriver (CompilationUnit *cUnit);

/**
  * @brief Is the trace a loop?
  * @param cUnit the CompilationUnit
  * @param curPass the Pass
  * @return whether or not the cUnit represents a loop
  */
bool dvmCompilerTraceIsLoop (const CompilationUnit *cUnit, Pass *curPass);

/**
  * @brief Is the trace a loop formed by the new system?
  * @param cUnit the CompilationUnit
  * @param curPass the Pass
  * @return whether or not the cUnit represents a loop
  */
bool dvmCompilerTraceIsLoopNewSystem (const CompilationUnit *cUnit, Pass *curPass);

/**
  * @brief Is the trace a loop formed by the old system?
  * @param cUnit the CompilationUnit
  * @param curPass the Pass
  * @return whether or not the cUnit represents a loop
  */
bool dvmCompilerTraceIsLoopOldSystem (const CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Print the pass names
 */
void dvmCompilerPrintPassNames (void);

/**
 * @brief Print the pass names we are going to ignore
 */
void dvmCompilerPrintIgnorePasses (void);

/**
 * @brief Used to check whether instructions in basic block have resolved references.
 * @details If unresolved references have been found then cUnit->quitLoopMode is set to true.
 * @param cUnit The compilation unit
 * @param bb The basic block whose instructions to check
 * @return Always returns false because the CFG is not updated
 */
bool dvmCompilerCheckReferences (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Verify that hoisted checks optimization is applicable
 * @param cUnit the CompilationUnit
 * @param curPass the Pass
 * @return Whether to generate hoisted checks for the loop
 */
bool dvmCompilerHoistedChecksGate (const CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Remove an optimization Pass
 * @param name the name of the Pass
 * @return whether removal was successful
 */
bool dvmCompilerRemovePass (const char *name);

/**
 * @brief Get an optimization pass
 * @param name the name of the Pass
 * @return pointer to the Pass, 0 if not found
 */
Pass *dvmCompilerGetPass (const char *name);

/**
 * @brief Used to replace the gate of an optimization pass
 * @param name the name of the Pass
 * @param gate the new gate
 * @return whether the gate replacement was successful
 */
bool dvmCompilerReplaceGate (const char *name, bool (*gate) (const CompilationUnit *, Pass*));

/**
 * @brief Used to replace the end work function of a pass
 * @param name The name of the Pass to update
 * @param endWork The new end work function to use
 * @return Returns true if replacement was successful
 */
bool dvmCompilerReplaceEnd (const char *name, void (*endWork) (CompilationUnit *, Pass*));

/**
 * @brief Insert the user pass
 * @param newPass the Pass information we want to add/modify
 * @param name the name of the current pass we want to be adding/modifying
 * @param mode what we want to do/change
 * @return whether we succeeded or not
 */
bool dvmCompilerInsertUserPass (Pass *newPass, const char *name, enum PassInstrumentation mode);

/**
 * @brief Handle User Plugin Library
 * @param fileName the name of the library
 */
void dvmCompilerHandleUserPlugin (const char *fileName);

/**
 * @brief Create the Pass list
 */
void dvmCompilerBuildPassList (void);

#endif
