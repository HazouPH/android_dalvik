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

#include "CompilationErrorX86.h"
#include "CompilationUnit.h"
#include "Lower.h"
#include "Utility.h"

/**
 * @brief Table that stores information about errors defined in JitCompilationErrors
 */
START_ERRORS
    NEW_ERROR (kJitErrorCodeCacheFull,        "Jit code cache is full.",                                 true, false),
    NEW_ERROR (kJitErrorUnsupportedBytecode,  "Trace contains bytecode with no implementation.",              false, false),
    NEW_ERROR (kJitErrorUnsupportedVectorization, "Requested vectorization is not supported.",                    false, false),
    NEW_ERROR (kJitErrorUnsupportedInstruction,   "Architecture does not support desired x86 instruction.",       false, false),
    NEW_ERROR (kJitErrorZeroPC,               "JIT is exporting a PC of 0.",                         false, false),
END_ERRORS

unsigned int CompilationErrorHandlerX86::getMaximumErrors (void) const
{
    //Take the minimum of the size of the array and kJitErrorMaxME but without the offset of the ME's enumeration
    unsigned int enumMax = kJitErrorMaxDefinedX86 - kJitFirstErrorX86;
    unsigned int arrayMax = sizeof (gErrorInformation) / sizeof (gErrorInformation[0]);
    unsigned int min = (enumMax < arrayMax) ? enumMax : arrayMax;

    //Now add back the ME's enumeration
    return min + kJitFirstErrorX86;
}

const CompilationError *CompilationErrorHandlerX86::getError (unsigned int index) const
{
    //Check the index
    if (index < kJitFirstErrorX86)
    {
        //Not ours, send it to our parent
        return CompilationErrorHandler::getError (index);
    }

    //Is it too much?
    if (index >= kJitErrorMaxDefinedX86)
    {
        return 0;
    }

    //Safe to hit the index with the delta
    return gErrorInformation + (index - kJitFirstErrorX86);
}

void CompilationErrorHandlerX86::resolveError (CompilationUnit *cUnit, const CompilationError *error)
{
    switch (error->type)
    {
        case kJitErrorCodeCacheFull:
            dvmCompilerSetCodeAndDataCacheFull();
            break;
        default:
            //We don't know about it maybe the base class does
            CompilationErrorHandler::resolveError (cUnit, error);
            break;
    }
}
