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

#ifndef DALVIK_VM_COMPILERCODEGEN_H_
#define DALVIK_VM_COMPILERCODEGEN_H_

#include "compiler/CompilerIR.h"

/* Maximal number of switch cases to have inline chains */
#define MAX_CHAINED_SWITCH_CASES    64

/* Size of various inlined chaining cell structures, in bytes */
#define BACKWARD_BRANCH_CC_SIZE     25
#define SINGLETON_CC_SIZE           17
#define PREDICTED_CC_SIZE           20
#define HOT_CC_SIZE                 17
#define NORMAL_CC_SIZE              17

/* Work unit is architecture dependent */
bool dvmCompilerDoWork(CompilerWorkOrder *work);

/* Lower middle-level IR to low-level IR */
void dvmCompilerMIR2LIR(CompilationUnit *cUnit, JitTranslationInfo* info);

/* Lower middle-level IR to low-level IR for the whole method */
void dvmCompilerMethodMIR2LIR(CompilationUnit *cUnit);

/* Assemble LIR into machine code */
void dvmCompilerAssembleLIR(CompilationUnit *cUnit, JitTranslationInfo *info);

/* Perform translation chain operation. */
extern "C" void* dvmJitChain(void* tgtAddr, u4* branchAddr);

#ifdef ARCH_IA32
/** @brief Perform chaining operation using static address */
extern "C" void* dvmJitChain_staticAddr(void* tgtAddr, u4* branchAddr);
#endif
/* Install class objects in the literal pool */
void dvmJitInstallClassObjectPointers(CompilationUnit *cUnit,
                                      char *codeAddress);

/* Patch inline cache content for polymorphic callsites */
bool dvmJitPatchInlineCache(void *cellPtr, void *contentPtr);

/* Implemented in the codegen/<target>/ArchUtility.c */
void dvmCompilerCodegenDump(CompilationUnit *cUnit);

/* Implemented in the codegen/<target>/CodegenInterface.c */
bool dvmCompilerFindRegClass(MIR *mir, int vr, RegisterClass &regClass, bool onlyUse = false);

/* Implemented in the codegen/<target>/Assembler.c */
void dvmCompilerPatchInlineCache(void);

/* Implemented in codegen/<target>/Ralloc.c */
void dvmCompilerLocalRegAlloc(CompilationUnit *cUnit);

/* Implemented in codegen/<target>/Thumb<version>Util.c */
void dvmCompilerInitializeRegAlloc(CompilationUnit *cUnit);

/* Implemented in codegen/<target>/<target_variant>/ArchVariant.c */
JitInstructionSetType dvmCompilerInstructionSet(void);

/*
 * Implemented in codegen/<target>/<target_variant>/ArchVariant.c
 * Architecture-specific initializations and checks
 */
bool dvmCompilerArchVariantInit(void);

/* Implemented in codegen/<target>/<target_variant>/ArchVariant.c */
int dvmCompilerTargetOptHint(int key);

/* Implemented in codegen/<target>/<target_variant>/ArchVariant.c */
void dvmCompilerGenMemBarrier(CompilationUnit *cUnit, int barrierKind);

/*
 * Implemented in codegen/<target>/CodegenDriver.cpp
 * Architecture-specific BasicBlock initialization
 */
BasicBlock *dvmCompilerArchSpecificNewBB();

/*
 * Implemented in the codegen/<target>/ArchUtility.c
 * Dumps architecture specific Basic Block information into the CFG dot file.
 */
void dvmCompilerDumpArchSpecificBB(CompilationUnit *cUnit, BasicBlock *bb, FILE *file, bool beforeMIRs = false);

/*
 * Implemented in the codegen/<target>/ArchUtility.c
 * Tells whether backend can bail out from JIT code for this mir.
 */
bool backendCanBailOut(CompilationUnit *cUnit, MIR *mir);

/*
 * Implemented in codegen/<target>/CodegenDriver.cpp
 * Backend check whether vectorization of specific packed size is supported
 */
bool dvmCompilerArchSupportsVectorizedPackedSize (unsigned int size);

/*
 * Implemented in codegen/<target>/CodegenDriver.cpp
 * Used to check whether the architecture specific portion supports extended opcode
 */
bool dvmCompilerArchSupportsExtendedOp (int extendedOpcode);

#endif  // DALVIK_VM_COMPILERCODEGEN_H_
