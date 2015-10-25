/*
 * Copyright (C) 2010-2011 Intel Corporation
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
#ifndef _DALVIK_PCGINTERFACE
#define _DALVIK_PCGINTERFACE

#include "compiler/CompilerIR.h"
#include "libpcg.h"
#include "interp/InterpDefs.h"

// PCG module level initialization and cleanup.
extern void pcgModuleBegin(void);
extern void pcgModuleEnd(void);

/* PCG version of the function to lower middle-level IR to low-level IR */
void pcgDvmCompilerMIR2LIR(CompilationUnit *cUnit, JitTranslationInfo* info);

// Filter to allow us to selectively use PCG or the existing dalvik JIT.
extern bool usePcgForTrace(CompilationUnit *cUnit);

extern "C" void dvmJitToInterpNormal(int targetpc); //in %ebx
extern "C" void dvmJitToInterpBackwardBranch(int targetpc);
extern "C" void dvmJitToInterpTraceSelect(int targetpc); //in %ebx
extern "C" void dvmJitToInterpTraceSelectNoChain(int targetpc); //in %ebx
extern "C" void dvmJitToExceptionThrown(int targetpc); //in currentPc
extern "C" void dvmJitToInterpPunt(int targetpc); //in currentPc
extern "C" void dvmJitToInterpNoChain(int targetpc); //in %eax
extern "C" void dvmJitToInterpNoChainNoProfile(int); //in currentPc
extern const Method *dvmJitToPatchPredictedChain(const Method*, Thread*,
						 PredictedChainingCell*,
						 const ClassObject*);
extern "C" const Method *dvmFindInterfaceMethodInCache(ClassObject*, u4,
						       const Method*, DvmDex*);
// It seems like this function could be implemented in the JIT code itself.
extern s4 dvmJitHandlePackedSwitch(const s4*, s4, u2, s4);

// TODO (DLK): This declaration and use in PcgInterface.cpp is temporary.
//             See the comment in PcgInterface.cpp for more details.
//
extern char* stream; //current stream pointer

extern CGSymbol pcgClassResolveSymbol;

#endif // _DALVIK_PCGINTERFACE
