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

#include "CompilationErrorPCG.h"
#include "CompilationUnit.h"
#include "Lower.h"

/**
 * @brief Table that stores information about errors defined in JitCompilationErrors
 */
START_ERRORS
    NEW_ERROR (kJitErrorPcgUnknownChainingBlockType,    "Unknown chaining block type seen in PCG GL.",                  false, false),
    NEW_ERROR (kJitErrorPcgUnexpectedDataType,          "Unexpected data type seen in PCG GL.",                         false, false),
    NEW_ERROR (kJitErrorPcgUnsupportedCallDataType,     "Unsupported call data type in PCG GL.",                        false, false),
    NEW_ERROR (kJitErrorPcgUnknownBlockType,            "Unknown basic block type in PCG GL.",                          false, false),
    NEW_ERROR (kJitErrorPcgPreBackward,                 "Problem when handling the pre-backward branch in PCG GL.",     false, false),
    NEW_ERROR (kJitErrorPcgAgetUnknownType,             "Unknown type when handling the Aget bytecode in PCG GL.",      false, false),
    NEW_ERROR (kJitErrorPcgAputUnknownType,             "Unknown type when handling the Aput bytecode in PCG GL.",      false, false),
    NEW_ERROR (kJitErrorPcgJsrCreation,                 "Issue when handling the Jsr creation in PCG GL.",              false, false),
    NEW_ERROR (kJitErrorPcgUnknownSymbol,               "Unknown symbol request in PCG GL.",                            false, false),
    NEW_ERROR (kJitErrorPcgUnknownCallback,             "Unknown callback request in PCG GL.",                          false, false),
    NEW_ERROR (kJitErrorPcgCodegen,                     "Undefined issues in trace formation.",                         false, false),
    NEW_ERROR (kJitErrorPcgPostInvokeEntryNotFound,     "The post-invoke entry block was not found.",                   false, false),
    NEW_ERROR (kJitErrorPcgRelocationCreation,          "Error when creating a relocation in PCG GL.",                  false, false),
    NEW_ERROR (kJitErrorPcgScratchFailedRegisterization,"Scratch register failed registerization.",                     false, false),
    NEW_ERROR (kJitErrorPcgBadSsaReference,             "SSA information was requested for non-referenced SSA register.", false, false),
END_ERRORS

unsigned int CompilationErrorHandlerPCG::getMaximumErrors (void) const
{
    //Take the minimum of the size of the array and kJitErrorMaxME but without the offset of the ME's enumeration
    unsigned int enumMax = kJitErrorMaxDefinedPCG - kJitFirstErrorPCG;
    unsigned int arrayMax = sizeof (gErrorInformation) / sizeof (gErrorInformation[0]);
    unsigned int min = (enumMax < arrayMax) ? enumMax : arrayMax;

    //Now add back the ME's enumeration
    return min + kJitFirstErrorPCG;
}

const CompilationError *CompilationErrorHandlerPCG::getError (unsigned int index) const
{
    //Check the index
    if (index < kJitFirstErrorPCG)
    {
        //Not ours, send it to our parent
        return CompilationErrorHandlerX86::getError (index);
    }

    //Is it too much?
    if (index >= kJitErrorMaxDefinedPCG)
    {
        return 0;
    }

    //Safe to hit the index with the delta
    return gErrorInformation + (index - kJitFirstErrorPCG);
}

CompilationErrorHandler *dvmCompilerPcgNewCompilationErrorHandler (void)
{
    CompilationErrorHandlerPCG *res;

    //Make space for it
    void *space = dvmCompilerNew (sizeof (*res), true);

    //Ensure the constructor is called
    res = new (space) CompilationErrorHandlerPCG ();

    //Return it
    return res;
}
