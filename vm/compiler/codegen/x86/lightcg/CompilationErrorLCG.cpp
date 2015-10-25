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

#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "Lower.h"

/**
 * @brief Table that stores information about errors defined in JitCompilationErrors
 */
START_ERRORS
    NEW_ERROR (kJitErrorMaxVR,                "Exceeded maximum allowed VRs in a basic block.",               false, false),
    NEW_ERROR (kJitErrorShortJumpOffset,      "Jump offset greater than 8-bits.",                              true, false),
    NEW_ERROR (kJitErrorUnresolvedField,      "Trace contains SGET / SPUT bytecode with unresolved field.",   false, false),
    NEW_ERROR (kJitErrorInvalidBBId,          "Cannot find BasicBlock_O1 corresponding to a BasicBlock.",     false, false),
    NEW_ERROR (kJitErrorRegAllocFailed,       "Failure in register allocator or register tables.",            false, false),
    NEW_ERROR (kJitErrorMallocFailed,         "Malloc failure during trace compilation.",                      false, true),
    NEW_ERROR (kJitErrorMaxXferPoints,        "Exceeded maximum number of transfer points per BB.",           false, false),
    NEW_ERROR (kJitErrorMaxDestRegPerSource,  "Exceeded number of destination regs for a source reg.",        false, false),
    NEW_ERROR (kJitErrorStateTransfer,        "Problem with state transfer in JIT.",                          false, false),
    NEW_ERROR (kJitErrorTraceFormation,       "Problem with trace formation.",                                false, false),
    NEW_ERROR (kJitErrorNullBoundCheckFailed, "Problem while performing null or bound check.",                false, false),
    NEW_ERROR (kJitErrorMergeLiveRange,       "Problem while merging live ranges  (mergeLiveRange).",          false, false),
    NEW_ERROR (kJitErrorGlobalData,           "Global data not defined.",                                     false, false),
    NEW_ERROR (kJitErrorInsScheduling,        "Problem during instruction scheduling.",                       false, false),
    NEW_ERROR (kJitErrorBERegisterization,    "Issue registerizing the trace in the backend.",                 true, false),
    NEW_ERROR (kJitErrorSpill,                "The trace provoked a spill.",                                   true, false),
    NEW_ERROR (kJitErrorBBCannotBeHandled,    "The backend decided it cannot safely handle the Basic Block.", false, false),
    NEW_ERROR (kJitErrorConstInitFail,        "Patching of Double/Long constants failed.",                     true, false),
    NEW_ERROR (kJitErrorChainingCell,         "An issue was encountered while generating chaining cell.",     false, false),
    NEW_ERROR (kJitErrorInvalidOperandSize,   "Invalid Operand Size was encountered.",                        false, false),
    NEW_ERROR (kJitErrorPlugin,               "Problem with the plugin system.",                              false, false),
    NEW_ERROR (kJitErrorConstantFolding,      "Constant folding failed due to unhandled case.",               false, false),
    NEW_ERROR (kJitErrorCodegen,              "Undefined issues in trace formation.",                         false, false),
END_ERRORS


//CompilationErrorHandlerLCG implementation is below


void CompilationErrorHandlerLCG::resolveError (CompilationUnit *cUnit, const CompilationError *error)
{
    //If the error isn't kJitErrorCodegen, we can clear it here
    // However: If kJitErrorCodegen is the first error we encounter, somebody forgot to raise an error flag somewhere.
    // Otherwise, we should clear the flag because another non-generic message will be printed out.
    if (error->type != kJitErrorCodegen)
    {
        clearError (kJitErrorCodegen);
    }

    switch (error->type)
    {
        case kJitErrorShortJumpOffset:
            gDvmJit.disableOpt |= (1 << kShortJumpOffset);
            ALOGI ("JIT_INFO: Successfully resolved short jump offset issue");
            //Clear the error:
            clearError (kJitErrorShortJumpOffset);
            break;

        case kJitErrorSpill:
            {
                //Clear the error:
                clearError (kJitErrorSpill);

                //Ok we are going to see if we are registerizing something
                int max = cUnit->maximumRegisterization;

                // We should only get this error if maximum registerization is > 0
                assert (max > 0);

                // Divide it by 2, fastest way to get to 0 if we have issues across the board
                // If it is a last attempt then force setting to 0
                // It is better to compile instead of give a last try for registerization
                int newMax = (getTopRetryCount () == 0) ? 0 : max / 2;
                cUnit->maximumRegisterization = newMax;
                ALOGI ("Trying less registerization from %d to %d", max, newMax);
            }
            break;

        case kJitErrorBERegisterization:
            //If registerization in the Backend is on
            if (gDvmJit.backEndRegisterization == true) {

                //Turn off registerization
                gDvmJit.backEndRegisterization = false;

                //Set maximum registerization for this cUnit to 0
                //since we disabled registerization
                cUnit->maximumRegisterization = 0;

                //Clear the error.
                //Registerization can cause other errors
                //Let's clear all errors for now, and see if they
                //re-occur without registerization.
                clearErrors ();

                //Notify about this special action
                ALOGI ("Ignoring other issues and retrying without backend registerization");
            }
            break;

        case kJitErrorConstInitFail:
            gDvmJit.disableOpt |= (1 << kElimConstInitOpt);
            ALOGI ("Resolved error due to constant initialization failure");
            //Clear the error:
            clearError (kJitErrorConstInitFail);
            break;

        default:
            //We don't know about it but the X86 might might
            CompilationErrorHandlerX86::resolveError (cUnit, error);
            break;
    }
}

unsigned int CompilationErrorHandlerLCG::getMaximumErrors (void) const
{
    //Take the minimum of the size of the array and kJitErrorMaxME but without the offset of the ME's enumeration
    unsigned int enumMax = kJitErrorMaxDefinedLCG - kJitFirstErrorLCG;
    unsigned int arrayMax = sizeof (gErrorInformation) / sizeof (gErrorInformation[0]);
    unsigned int min = (enumMax < arrayMax) ? enumMax : arrayMax;

    //Now add back the ME's enumeration
    return min + kJitFirstErrorLCG;
}

const CompilationError *CompilationErrorHandlerLCG::getError (unsigned int index) const
{
    //Check the index
    if (index < kJitFirstErrorLCG)
    {
        //Not ours, send it to our parent
        return CompilationErrorHandlerX86::getError (index);
    }

    //Is it too much?
    if (index >= kJitErrorMaxDefinedLCG)
    {
        return 0;
    }

    //Safe to hit the index with the delta
    return gErrorInformation + (index - kJitFirstErrorLCG);
}
