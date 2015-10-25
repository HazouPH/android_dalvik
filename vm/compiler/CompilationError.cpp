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

#include "CompilationError.h"

//Middle-end Errors: currently empty :)
START_ERRORS
    NEW_ERROR (kErrorLoopCompilation,  "Loop compilation failed", true, false),
    NEW_ERROR (kErrorTraceCompilation,  "Trace compilation failed", true, false),
    NEW_ERROR (kErrorTraceTooLarge,  "Trace compilation failed", true, false),
    NEW_ERROR (kErrorQuitLoopMode,  "QuitLoopMode tripped", true, false),
END_ERRORS

//Constructor first
CompilationErrorHandler::CompilationErrorHandler (void)
{
    disableOpt = 0;
    backEndRegisterization = 0;
    errorFlags = 0;
}

//Destructor second
CompilationErrorHandler::~CompilationErrorHandler (void)
{
    //Remove everything from the retryCounts vector
    std::vector<unsigned int> dummy;
    retryCounts.swap (dummy);
}

void CompilationErrorHandler::saveOptimizationState (void)
{
    disableOpt = gDvmJit.disableOpt;
    backEndRegisterization = gDvmJit.backEndRegisterization;
}

void CompilationErrorHandler::restoreCompilationState (void) const
{
    gDvmJit.disableOpt = disableOpt;
    gDvmJit.backEndRegisterization = backEndRegisterization;
}

void CompilationErrorHandler::fixErrors (CompilationUnit *cUnit)
{
    // Checks if any error has occurred. If not, we do not need to retry.
    if (isAnyErrorSet () == false)
    {
        return;
    }

    //Have a boolean to know if we've had an error already
    bool hadError = false;

    //Get the maximum error number the system is aware of so far:
    unsigned int maxError = getMaximumErrors ();

    // Check which errors have been raised
    for (unsigned int errorIndex = 0; errorIndex <= maxError; errorIndex++) {
        //Is it set?
        if (isErrorSet (errorIndex) == true)
        {
            //Get the error from the table
            const CompilationError *error = getError (errorIndex);

            //Skip if 0, actually paranoid but we will check
            if (error == 0)
            {
                ALOGD ("JIT_INFO: Error framework could not find error entry");
                continue;
            }

            //Is it the first error?
            if (hadError == false)
            {
                //Let the error framework do some initial printing then
                signalFirstError (cUnit);

                //Set flag to true to not come back here
                hadError = true;
            }

            //Handle the error
            handleError (cUnit, error);

            //Resolve the error if possible
            if (error->canResolve == true)
            {
                resolveError (cUnit, error);
            }
        }
    }
}

unsigned int CompilationErrorHandler::getMaximumErrors (void) const
{
    //Take the minimum of the size of the array and kJitErrorMaxME
    unsigned int enumMax = kJitErrorMaxME;
    unsigned int arrayMax = sizeof (gErrorInformation) / sizeof (gErrorInformation[0]);
    unsigned int min = (enumMax < arrayMax) ? enumMax : arrayMax;

    return min;
}

const CompilationError *CompilationErrorHandler::getError (unsigned int index) const
{
    //Be paranoid
    if (index >= getMaximumErrors ())
    {
        return 0;
    }

    //Safe to access the array
    return gErrorInformation + index;
}

void CompilationErrorHandler::handleError (CompilationUnit *cUnit, const CompilationError *error) const
{
    //First real question is: is the error fatal?
    bool fatalError = error->isFatal;

    // If we are set to abort on error and error cannot be resolved, then the error is fatal.
    fatalError = (fatalError == true) || (gDvmJit.abortOnCompilerError == true && error->canResolve == false);

    // If it is fatal then we are bailing really quickly
    if (fatalError == true)
    {
        ALOGE ("\t%s", error->errorMessage);
        ALOGE ("FATAL_ERRORS in JIT. Aborting compilation.");
        dvmCompilerAbort (cUnit);
    }
    else
    {
        //Otherwise, just print out the message
        ALOGI ("\t%s", error->errorMessage);
    }
}

void CompilationErrorHandler::signalFirstError (CompilationUnit *cUnit) const
{
    ALOGI ("++++++++++++++++++++++++++++++++++++++++++++");
    ALOGI ("JIT_INFO: ME Issues while compiling trace  %s%s, offset %d",
            cUnit->method->clazz->descriptor, cUnit->method->name,
            cUnit->traceDesc->trace[0].info.frag.startOffset);
}

void CompilationErrorHandler::resolveError (CompilationUnit *cUnit, const CompilationError *error)
{
    //By default, do nothing
}

bool CompilationErrorHandler::decideOnRemainingErrors (void)
{
    //If anything is still set, the ME says to just forget it
    return (getTopRetryCount () != 0) && (isAnyErrorSet () == false);
}

void CompilationErrorHandler::reset (void)
{
    //Just clear the flags
    clearErrors ();
}

void CompilationErrorHandler::pushRetryCount (unsigned int count)
{
    retryCounts.push_back (count);
}

void CompilationErrorHandler::decrementTopRetryCount (void)
{
    unsigned int idx = retryCounts.size ();

    //If at least one element
    if (idx > 0)
    {
        //Go back one to get the actual index
        idx--;

        //Decrement
        retryCounts[idx]--;
    }
}

unsigned int CompilationErrorHandler::getTopRetryCount (void) const
{
    unsigned int idx = retryCounts.size ();

    //If at least one element
    if (idx > 0)
    {
        //Go back one
        idx--;

        return retryCounts[idx];
    }

    //By default return 0
    return 0;

}
