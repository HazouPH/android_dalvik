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

#ifndef DALVIK_VM_COMPILATIONERRORPCGCG_H_
#define DALVIK_VM_COMPILATIONERRORPCGCG_H_

#include "CompilationErrorX86.h"

/**
 * @enum JitPCGCompilationErrors
 * @brief Possible errors which can happen during compilation
 */
enum JitPCGCompilationErrors {
    /** @brief First extended error */
    kJitFirstErrorPCG = kJitErrorMaxDefinedX86,
    /** @brief Unknown chaining block type seen in PCG GL */
    kJitErrorPcgUnknownChainingBlockType = kJitFirstErrorPCG,
    /** @brief Unexpected data type seen in PCG GL */
    kJitErrorPcgUnexpectedDataType,
    /** @brief Unsupported call data type in PCG GL */
    kJitErrorPcgUnsupportedCallDataType,
    /** @brief Unknown Block type seen */
    kJitErrorPcgUnknownBlockType,
    /** @brief Problem with the PBWCC */
    kJitErrorPcgPreBackward,
    /** @brief Problem while handling an Aget */
    kJitErrorPcgAgetUnknownType,
    /** @brief Problem while handling an Aput */
    kJitErrorPcgAputUnknownType,
    /** @brief Error at the generation of the Jsr */
    kJitErrorPcgJsrCreation,
    /** @brief Error while getting a symbol */
    kJitErrorPcgUnknownSymbol,
    /** @brief Error while getting a callback */
    kJitErrorPcgUnknownCallback,
    /** @brief Indicates "some" error happened */
    kJitErrorPcgCodegen,
    /** @brief The post-invoke entry block was not found */
    kJitErrorPcgPostInvokeEntryNotFound,
    /** @brief Error when creating a relocation */
    kJitErrorPcgRelocationCreation,
    /** @brief Error when a scratch register fails to be registerized */
    kJitErrorPcgScratchFailedRegisterization,
    /** @brief Error when SSA information was requested for non-reference SSA number */
    kJitErrorPcgBadSsaReference,
    /** @brief Maximum defines */
    kJitErrorMaxDefinedPCG
};

/**
 * @class CompilationErrorHandlerPCG
 * @brief The compilation error framework for the PCG backend
 */
class CompilationErrorHandlerPCG: public CompilationErrorHandlerX86
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
};

/**
 * @brief PCG Compilation Error Handler allocation
 * @return the new CompilationErrorHandler
 */
CompilationErrorHandler *dvmCompilerPcgNewCompilationErrorHandler (void);
#endif
