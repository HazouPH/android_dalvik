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

#include "BasicBlockPCG.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "CompilerIR.h"
#include "Labels.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "UtilityPCG.h"

/**
 * @details createSymbol creates a new CGSymbol with the specified name and address.
 *  The symbol is created and returned, and a SClientSymbolInfo structure is created and inserted into the pcgSymbolMap.
 */
CGSymbol dvmCompilerPcgCreateSymbol (CompilationUnitPCG *cUnit, const std::string &name, void *address, bool aligned, bool memconst, bool global)
{
    std::string str = "";

    //If we need the symbol aligned, or if it's a memconst, add it to the str variable
    if (aligned == true)
    {
        str += 'p';
    }
    if (memconst == true)
    {
        str += 'c';
    }
    if (global == false)
    {
        str += 'l';
    }

    const char *cstr = str.c_str();

    CGSymbol cgSymbol = CGCreateSymbol (cstr);

    //Create the symbol information
    SClientSymbolInfo clientSymbol;

    //Fill in the values
    clientSymbol.name = name;
    clientSymbol.address = address;
    clientSymbol.cgSymbol = cgSymbol;

    //Insert it into the system
    singletonPtr<PersistentInfo> ()->setSymbolInfo (cgSymbol, clientSymbol);

    if (global == false)
    {
        assert(cUnit != NULL);
        cUnit->addLocalSymbol(cgSymbol);
    }

    //Return the associated symbol
    return cgSymbol;
}

void dvmCompilerPcgBindBlockLabel (BasicBlockPCG *bb)
{
    //Bind the label
    CGBindLabel (bb->cgLabel);

    //Inform the block has been bound
    bb->blockBound = true;
}

void dvmCompilerPcgBindSymbolAddress (CompilationUnitPCG *cUnit, CGSymbol cgSymbol, void *address)
{
    //Get the right client symbol information
    SClientSymbolInfo *clientSymbol = singletonPtr<PersistentInfo> ()->getSymbolInfo (cUnit, cgSymbol);

    //Paranoid test
    if (clientSymbol == 0)
    {
        //For the moment just make it fail with the generic error
        cUnit->errorHandler->setError (kJitErrorPcgCodegen);

        //Just return because this is already a bad enough situation
        return;
    }

    //Update the address
    clientSymbol->address = address;
}

void* dvmCompilerPcgGetSymbolAddress (CompilationUnitPCG *cUnit, CGSymbol cgSymbol)
{
    //Get the right client symbol information
    SClientSymbolInfo *clientSymbol = singletonPtr<PersistentInfo> ()->getSymbolInfo (cUnit, cgSymbol, true);

    //Paranoid
    assert (clientSymbol != 0);

    if (clientSymbol == 0)
    {
        return (void*)0;
    }

    //Return the address
    return clientSymbol->address;
}

