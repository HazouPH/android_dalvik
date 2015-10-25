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

#ifndef DALVIK_VM_COMPILATIONERRORLCG_H_
#define DALVIK_VM_COMPILATIONERRORLCG_H_

#include "CompilationErrorX86.h"
#include "Dalvik.h"

//Macros used in order to limit the changes to the BE
#define SET_JIT_ERROR_MANUAL(X,Y) \
    do \
    {\
        if (X != 0) \
        { \
            X->errorHandler->setError (Y); \
        } \
        else \
        { \
            /* Make if fail in assert world */ \
            assert (0); \
            ALOGD ("JIT_INFO: Setting an error flag without context"); \
        } \
    } while (0)

#define SET_JIT_ERROR(X) SET_JIT_ERROR_MANUAL (gCompilationUnit, X)
#define IS_ANY_JIT_ERROR_SET(X) gCompilationUnit->errorHandler->isAnyErrorSet ()
#define CLEAR_ALL_JIT_ERRORS(X) gCompilationUnit->errorHandler->clearErrors ()

/**
 * @enum JitLCGCompilationErrors
 * @brief Possible errors which can happen during compilation
 */
enum JitLCGCompilationErrors {
    /** @brief First extended error */
    kJitFirstErrorLCG = kJitErrorMaxDefinedX86,
    /** @brief Exceeded maximum allowed VRs in a basic block */
    kJitErrorMaxVR = kJitFirstErrorLCG,
    /** @brief 8-bit jump offset not enough to reach label */
    kJitErrorShortJumpOffset,
    /** @brief Field ptr unresolved for SGET/SPUT bytecodes */
    kJitErrorUnresolvedField,
    /** @brief Cannot find BasicBlock_O1 corresponding to a BasicBlock */
    kJitErrorInvalidBBId,
    /** @brief Failures while allocating registers or error
     *  in locating / putting registers in register tables
     */
    kJitErrorRegAllocFailed,
    /** @brief Malloc failed. */
    kJitErrorMallocFailed,
    /** @brief Exceeded maximum number of transfer points per BB */
    kJitErrorMaxXferPoints,
    /** @brief Exceeded number of destination regs for a source reg */
    kJitErrorMaxDestRegPerSource,
    /** @brief Problem with state transfer in JIT */
    kJitErrorStateTransfer,
    /** @brief General trace formation issues */
    kJitErrorTraceFormation,
    /** @brief Errors while performing Null and Bound checks */
    kJitErrorNullBoundCheckFailed,
    /** @brief Errors while merging LiveRanges */
    kJitErrorMergeLiveRange,
    /** @brief Errors while accessing global data */
    kJitErrorGlobalData,
    /** @brief Errors while scheduling instructions */
    kJitErrorInsScheduling,
    /** @brief Errors due to backend registerization */
    kJitErrorBERegisterization,
    /** @brief Errors due to spilling logical registers */
    kJitErrorSpill,
    /** @brief Set when a basic block is reject by backend */
    kJitErrorBBCannotBeHandled,
    /** @brief Errors while performing double/long constant initialization */
    kJitErrorConstInitFail,
    /** @brief Error while generating chaining cell */
    kJitErrorChainingCell,
    /** @brief Invalid operand size */
    kJitErrorInvalidOperandSize,
    /** @brief Problem with the plugin system */
    kJitErrorPlugin,
    /** @brief Unhandled case during constant folding */
    kJitErrorConstantFolding,

    /* ----- Add more errors above ---------------------------
     * ----- Don't add new errors beyond this point ----------
     * When adding more errors, update error information table
     * in CodegenErrors.cpp
     */

    /** @brief Indicates "some" error happened
     * Specifically, the purpose is that if someone forgets
     * to use error setting at the specific error location,
     * but does throw a return, the function handling that
     * return can set this generic error. Also useful if a
     * function can set multiple errors, the calling function
     * won't have to worry about which one to set. Hopefully
     * all of the errors have been individually set too.
     *
     * This should be the last error
     */
    kJitErrorCodegen,

    /** @brief Guarding value
     * THIS NEEDS TO BE THE LAST VALUE
     */
    kJitErrorMaxDefinedLCG
};

/**
 * @class CompilationErrorHandlerLCG
 * @brief The compilation error framework for the LightCG
 */
class CompilationErrorHandlerLCG: public CompilationErrorHandlerX86
{
    public:

        /**
         * @brief Returns the maximum number of errors
         * @return the maximum number of errors
         */
        virtual unsigned int getMaximumErrors (void) const;

        /**
         * @brief Get a JitCompilationError
         * @param index the index we are interested in
         * @return a pointer to the corresponding JitCompilationError or 0 if invalid
         */
        virtual const CompilationError *getError (unsigned int index) const;

        /**
         * @brief Resolve the errors
         * @param cUnit the CompilationUnit
         * @param error the error we wish to handle
         */
        virtual void resolveError (CompilationUnit *cUnit, const CompilationError *error);
};

#endif
