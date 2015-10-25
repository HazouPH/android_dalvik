/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "Dalvik.h"
#include "CompilationError.h"
#include "CompilationUnit.h"
#include "X86Common.h"

void dvmCompilerMIR2LIR (CompilationUnit *cUnit, JitTranslationInfo *info)
{
    CompilationErrorHandler *meHandler = cUnit->errorHandler;

    //Create the error handler for the backend
    CompilationErrorHandler *errorHandler = dvmCompilerArchSpecificNewCompilationErrorHandler ();

    if (errorHandler == 0)
    {
        // This is a fatal error to the compilation (and likely to the JIT)
        ALOGE ("JIT: Could not create an error handler.");
        dvmCompilerAbort (cUnit);
    }

    //Set it in the cUnit
    cUnit->errorHandler = errorHandler;

    //Get number of base retries
    int baseRetries = gDvmJit.backEndRetries;

    //Push maximum
    errorHandler->pushRetryCount (baseRetries);

    //Try to lower MIR
    bool retry = true;
    while (retry == true)
    {
        //Be optimist
        retry = false;

        //Get backend compiler entry
        void (*backEndCompiler) (CompilationUnit *, JitTranslationInfo *) = gDvmJit.jitFramework.backEndFunction;

        if (backEndCompiler != 0)
        {
            //Do the compilation
            backEndCompiler (cUnit, info);
        }
        else
        {
            ALOGD ("JIT INFO: No backend defined");
        }

        //Check for errors
        bool success = (errorHandler->isAnyErrorSet () == false);

        //If there was a problem
        if (success == false)
        {
            //Resolve any errors
            errorHandler->fixErrors (cUnit);

            //Should we try again?
            retry = errorHandler->decideOnRemainingErrors ();

            //If so, we should reset the handler
            errorHandler->reset ();
        }

        //If we want to retry
        if (retry == true)
        {
            //Do we have a retry at least?
            if (errorHandler->getTopRetryCount () == 0)
            {
                retry = false;

                //Abort if the flag is set.
                if (gDvmJit.abortOnCompilerError == true) {
                    ALOGE("Too many retries for trace  %s%s, offset %d", cUnit->method->clazz->descriptor,
                            cUnit->method->name, cUnit->traceDesc->trace[0].info.frag.startOffset);

                    //This will cause a full abort due to the flag
                    dvmCompilerAbort(cUnit);
                }

                ALOGD("Too many retries while compiling trace  %s%s, offset %d", cUnit->method->clazz->descriptor,
                        cUnit->method->name, cUnit->traceDesc->trace[0].info.frag.startOffset);
                ALOGD("Rejecting Trace");

            }
            else
            {
                //Decrement the recount
                errorHandler->decrementTopRetryCount ();
            }

            //Ignore errors in previous compilations
            errorHandler->clearErrors ();
        }
    }

    //Call the destructor, don't free it because it's allocated on the arena but might require some clean-up
    errorHandler->~CompilationErrorHandler ();

    //Set the error handler of the cUnit back
    cUnit->errorHandler = meHandler;
}

