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

#ifndef CODEGEN_COMPILATION_ERROR_H_
#define CODEGEN_COMPILATION_ERROR_H_

#include "Dalvik.h"
#include "CompilerIR.h"

/**
 * @class CompilationErrors
 * @brief Compilation Errors from the Middle-End
 */
enum CompilationErrors {
    kErrorLoopCompilation,   /**< @brief Loop compilation failed */
    kErrorTraceCompilation,
    kErrorTraceTooLarge,
    kErrorQuitLoopMode,
    /** @brief The final Middle-End error so that the Back-ends can start at this value */
    kJitErrorMaxME,
};

/**
 * @class CompilationError
 * @brief Used to keep track of information associated with an error
 */
struct CompilationError
{
    /** @brief Type of error */
    int type;

    /** @brief Error message */
    const char * errorMessage;

    /** @brief Whether we can possibly fix the error */
    bool canResolve;

    /** @brief Whether error is fatal */
    bool isFatal;
};

/**
 * @class CompilationErrorHandler
 * @brief CompilationErrorHandler is an error handler for compilation
 */
class CompilationErrorHandler
{
    protected:
        int disableOpt;                     /**< @brief Disable the optimizations */
        bool backEndRegisterization;        /**< @brief Backend registerization */
        unsigned long long errorFlags;      /**< @brief Error flags */
        std::vector<unsigned int> retryCounts; /**< @brief Retry counts */

    public:
        /**
         * @brief Constructor
         */
        CompilationErrorHandler (void);

        /**
         * @brief Destructor
         */
        virtual ~CompilationErrorHandler (void);

        /**
         * @brief Tries to fix any errors encountered and decides if retrying has a point or not
         * @param cUnit CompilationUnit context
         */
        virtual void fixErrors (CompilationUnit *cUnit);

        /**
         * @brief Save the error flags that can be changed by dvmCanFixErrorsAndRetry
         */
        void saveOptimizationState (void);

        /**
         * @brief Restore compilation state
         */
        void restoreCompilationState (void) const;

        /**
         * @brief Set an error flag
         * @param error the error we wish to set
         */
        void setError (int error) {errorFlags |= (1 << error);}

        /**
         * @brief Clear an error
         * @param error the error we wish to clear
         */
        void clearError (int error) {errorFlags &= ~ (1 << error);};

        /**
         * @brief Reset the handler except any retry information
         */
        void reset (void);

        /**
         * @brief Is an error set?
         * @param error the error we wish to check
         */
        bool isErrorSet (int error) const {return ( (errorFlags & (1 << error)) != 0);}

        /**
         * @brief Clear all errors
         */
        void clearErrors (void) {errorFlags = 0;}

        /**
         * @brief Is any error set?
         * @return if any error is set
         */
        bool isAnyErrorSet (void) const {return (errorFlags != 0);}

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
         * @brief Handle an error
         * @param cUnit the CompilationUnit
         * @param error the error we wish to handle
         */
        virtual void handleError (CompilationUnit *cUnit, const CompilationError *error) const;

        /**
         * @brief Resolve the error
         * @param cUnit the CompilationUnit
         * @param error the error we wish to resolve
         */
        virtual void resolveError (CompilationUnit *cUnit, const CompilationError *error);

        /**
         * @brief Decide if we still have an issue or not?
         * @return return if there still is an error remaining?
         */
        virtual bool decideOnRemainingErrors (void);

        /**
         * @brief Signal that a first error occured
         * @param cUnit the CompilationUnit
         */
        virtual void signalFirstError (CompilationUnit *cUnit) const;

        /**
         * @brief Check for a particular mask in the disable optimization
         * @param mask the mask we want to check for
         * @return whether or not the mask on the disableOpt field is different than 0
         */
        bool checkDisableOptimization (int mask) {return (disableOpt & mask) != 0;}

        /**
         * @brief Set a disable optimization
         * @param mask the mask we want to set
         */
        void setDisableOptimization (int mask) {disableOpt |= mask;}

        /**
         * @brief Push a new retry count
         * @param count the new retry count
         */
        void pushRetryCount (unsigned int count);

        /**
         * @brief Pop the retry count
         */
        void popRetryCount (void) {retryCounts.pop_back ();}

        /**
         * @brief Decrement the top retry count
         */
        void decrementTopRetryCount (void);

        /**
         * @brief Get the top retry count
         * @return the top retry count
         */
        unsigned int getTopRetryCount (void) const;
};


/* Three macros to help error definitions */
#define START_ERRORS \
    static CompilationError gErrorInformation[] = {

#define NEW_ERROR(TYPE, MESSAGE, CANRESOLVE, ISFATAL) \
        { \
            TYPE, MESSAGE, CANRESOLVE, ISFATAL \
        }

#define END_ERRORS \
    };

#endif
