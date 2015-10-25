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
#include "CompilerIR.h"
#include "X86Common.h"

BasicBlock *dvmCompilerArchSpecificNewBB(void)
{
    BasicBlock * (*fctPtr) (void) = gDvmJit.jitFramework.backEndBasicBlockAllocation;

    //If we have a function pointer, call it
    if (fctPtr != 0)
    {
        return fctPtr ();
    }

    return 0;
}

CompilationErrorHandler *dvmCompilerArchSpecificNewCompilationErrorHandler (void)
{
    CompilationErrorHandler* (*fctPtr) (void) = gDvmJit.jitFramework.backEndCompilationErrorHandlerAllocation;

    //If we have a function pointer, call it
    if (fctPtr != 0)
    {
        return fctPtr ();
    }

    // TODO return some reasonable default error handler
    return 0;
}

void dvmCompilerDumpArchSpecificBB(CompilationUnit *cUnit, BasicBlock *bb, FILE *file, bool beforeMIRs)
{
    void (*fctPtr) (CompilationUnit *, BasicBlock *, FILE *, bool) = gDvmJit.jitFramework.backEndDumpSpecificBB;

    if (fctPtr != 0)
    {
        fctPtr (cUnit, bb, file, beforeMIRs);
    }
}

bool dvmCompilerArchitectureSupportsSSE41 (void)
{
    const int sse41Mask = 1 << 19;
    bool supportsSSE41 = ( (gDvmJit.featureInformation[0] & sse41Mask) != 0);

    return supportsSSE41;
}

bool dvmCompilerArchitectureSupportsSSE42 (void)
{
    const int sse42Mask = 1 << 20;
    bool supportsSSE42 = ( (gDvmJit.featureInformation[0] & sse42Mask) != 0);

    return supportsSSE42;
}
