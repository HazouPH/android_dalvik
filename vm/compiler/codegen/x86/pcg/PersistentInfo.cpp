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
#include <dlfcn.h>

#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "Labels.h"
#include "PersistentInfo.h"

PersistentInfo::PersistentInfo (void)
{
    dummySymbol = 0;
}

SClientSymbolInfo* PersistentInfo::getSymbolInfo (CompilationUnitPCG *cUnit, const CGSymbol &symbol, bool failOnNotFound)
{
    std::map<CGSymbol, SClientSymbolInfo>::iterator it = symbolToSymbolInfo.find (symbol);

    if (it == symbolToSymbolInfo.end ())
    {
        if (failOnNotFound == true)
        {
            assert (cUnit != 0);

            //TODO: this should be removed when the client signature is fixed
            if (cUnit != 0)
            {
                cUnit->errorHandler->setError (kJitErrorPcgUnknownSymbol);
            }
        }

        //Report not found
        return 0;
    }

    //Return associated SClientSymbolInfo
    return &(it->second);
}

CGSymbol PersistentInfo::getCallBack (CompilationUnitPCG *cUnit, const char * c_name)
{
    std::string name(c_name);
    std::map<std::string, CGSymbol>::const_iterator it = callBacks.find (name);

    if (it == callBacks.end ())
    {
        //Can we find it?
        void *fctPtr = dlsym (RTLD_DEFAULT, c_name);

        //If found, add it to callBacks
        if (fctPtr != 0)
        {
            CGSymbol symbol = dvmCompilerPcgCreateSymbol (cUnit, name, fctPtr, false, false, true);
            callBacks[name] = symbol;

            //Return it
            return symbol;
        }
        else
        {
            //Let's return a dummy symbol and then signal we will bail in the end, this will:
            // - Will let PCG handle it correctly
            // - We signal so that we ignore this code generation afterwards

            //Do we have a dummy symbol?
            if (dummySymbol == 0)
            {
                //Create one
                std::string dummyName = "a";

                //Let's create a name that does not exist here
                while (true)
                {
                    //Can we find it?
                    void *fctPtr = dlsym (0, dummyName.c_str ());

                    //If not found, we have our dummy name
                    if (fctPtr == 0)
                    {
                        dummySymbol = dvmCompilerPcgCreateSymbol (cUnit, dummyName, 0, false, false, true);
                        break;
                    }

                    //We actually have a function with that name, let's add a letter
                    dummyName += 'a';
                }
            }

            //TODO: this should be removed when the client signature is fixed
            if (cUnit != 0)
            {
                cUnit->errorHandler->setError (kJitErrorPcgUnknownCallback);
            }
            return dummySymbol;
        }
    }

    //Return associated CGSymbol
    return it->second;
}
