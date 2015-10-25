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

#ifndef DALVIK_VM_COMPILATIONERRORX86_H_
#define DALVIK_VM_COMPILATIONERRORX86_H_

#include "Dalvik.h"
#include "compiler/CompilationError.h"

/**
 * @enum JitX86CompilationErrors
 * @brief Possible errors which can happen during compilation
 */
enum JitX86CompilationErrors {
    /** @brief First x86 error */
    kJitFirstErrorX86 = kJitErrorMaxME,
    /** @brief JIT code cache is full */
    kJitErrorCodeCacheFull = kJitFirstErrorX86,
    /** @brief Trace contains a bytecode with no JIT implementation */
    kJitErrorUnsupportedBytecode,
    /** @brief Unsupported case for vectorization */
    kJitErrorUnsupportedVectorization,
    /** @brief Unsupported case for vectorization */
    kJitErrorUnsupportedInstruction,
    /** @brief The JIT is exporting a PC of 0 */
    kJitErrorZeroPC,
    /** @brief Guarding value
     * THIS NEEDS TO BE THE LAST VALUE
     */
    kJitErrorMaxDefinedX86
};

/**
 * @class CompilationErrorHandlerX86
 * @brief The compilation error framework for the X86 back-ends
 */
class CompilationErrorHandlerX86: public CompilationErrorHandler
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
