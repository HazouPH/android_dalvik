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

#include <cassert>

#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "libpcg.h"
#include "PersistentInfo.h"
#include "Singleton.h"

/**
 * @brief CGGetRoutineNameFromClient requests that the client provide the code generator with the name of the specified function
 * @details The code generator passes the same clientRoutineHandle that was passed to it via CGCreateRoutine and CGCompileRoutine.
 * @param clientRoutineHandle the client routine
 * @return the name of the routine
 */
const char *CGGetRoutineNameFromClient (const void *clientRoutineHandle)
{
    const size_t bufSize = 64;
    static char routineNameBuf[bufSize];

    //Get the cUnit
    const CompilationUnitPCG *cUnit = static_cast<const CompilationUnitPCG *> (clientRoutineHandle);

    //Create a unique trace string
    snprintf(routineNameBuf, bufSize, "dalvikJitTrace_%d", cUnit->getTraceID ());

    return routineNameBuf;
}

/**
 * @brief Does a symbol require a large memory model handling
 * @details CGSymbolNeedsLargeModelFixup asks the client whether the specified symbol
 * might have an arbitrary 64-bit address.  If so, the client must return a
 * non-zero value.  If the symbol is known to reside in the lower 2GB of
 * the address space or if the symbol is known to be located within 2GB of
 * the generated code in PIC mode, then the client may return 0.
 * @param symbol the CGSymbol
 * @return whether or not the symbol is residing in the lower 2GB of the address space or if the symbol resides within 2GB of the generated code in PIC mode
 */
int CGSymbolNeedsLargeModelFixup(CGSymbol symbol)
{
   (void) symbol;

    // We are generating x86 code, so no need
    return 0;
}

/**
 * @brief Get a symbol address from a symbol
 * @details CGGetSymbolAddressFromClient requests that the client provide the absolute
 * address of the specified symbol.  PCG uses this information to process
 * relocations during calls to CGResolveSymbolReferences.  The return value
 * is defined as uint64_t to accommodate both 32-bit and 64-bit targets.
 * @param symbol the CGSymbol
 * @return the address of the symbol
 */
extern uint64_t CGGetSymbolAddressFromClient(CGSymbol symbol)
{
    // We do our own symbol relocation, so we should never reach this point.
    assert(0);

   (void) symbol;
    return 0;
}

/**
 * @brief Get a CGSymbol from a given name
 * @details CGGetSymbolForNameFromClient requests that the client provide a CGSymbol
 * that PCG can use to reference an object level symbol of the specified name.
 * This callback function is typically used for library symbols resulting from
 * intrinsic function expansions.
 * @param symbolName the name for the symbol we are looking for
 * @return the CGSymbol associated to the symbolName
 */
CGSymbol CGGetSymbolForNameFromClient(const char *symbolName)
{
    //TODO: fix the signature of this function so it can pass the client symbol
    return singletonPtr<PersistentInfo> ()->getCallBack (0, symbolName);
}

/**
 * @brief CGGetSymbolNameFromClient requests that the client provide the name of the specified symbol
 * @param symbol the CGSymbol
 * @return the name of the symbol
 */
const char *CGGetSymbolNameFromClient(CGSymbol symbol)
{
    //TODO: fix the signature of this function so it can pass the client symbol
    const SClientSymbolInfo *clientSymbolInfo = singletonPtr<PersistentInfo> ()->getSymbolInfo (0, symbol);

    //Paranoid
    assert (clientSymbolInfo != 0);

    //Return the name
    return clientSymbolInfo->name.c_str ();
}


/**
 * @brief Get information about memory aliasing
 * @details CGGetProbabilityOfOverlapFromClient requests that the client provide
 * disambiguation information about the memory references identified by
 * handle1 and handle2. (handle1 and handle2 are the handles that were passed
 * to CGCreateNewInst for 'm' operands.)
 *
 * The client must return an integer in the range [0, 100].  A return value of
 * 0 is a guarantee from the client that the memory references do not overlap.
 * A return value of 100 is a guarantee from the client that the memory
 * references do overlap.  Any other value is the client's best guess for the
 * probability that the memory references overlap.
 * @param handle1 the first handle
 * @param handle2 the second handle
 * @return a value between [0, 100]
 */
uint32_t CGGetProbabilityOfOverlapFromClient(void *handle1, void *handle2)
{
    intptr_t h1 = (intptr_t)handle1;
    intptr_t h2 = (intptr_t)handle2;
    intptr_t h1Hi, h1Lo;
    intptr_t h2Hi, h2Lo;
    int oldDis;

    if (dvmExtractBackendOption("OldDis", &oldDis) && oldDis != 0) {
        return 50;
    }

    // Bit 0x1 indicates a non-VR load or store.  A non-VR memref cannot
    // overlap a VR one.
    if ((h1 & 0x1) != (h2 & 0x1)) {
        return 0;
    }

    // Two non-VR memrefs ... just assume possible overlap.
    if ((h1 & 0x1) != 0) {
        return 50;
    }

    // Both memrefs are VR refs.  Check whether the same VR is referenced.
    // In this case, we can always give an exact answer.
    h1Lo = h1 >> 3;
    h1Hi = h1Lo + (h1 & 0x4) ? 1 : 0;
    h2Lo = h2 >> 3;
    h2Hi = h2Lo + (h2 & 0x4) ? 1 : 0;

    if (h1Lo > h2Hi || h1Hi < h2Lo) {
        return 0;
    }

    return 100;
}

/**
 * @brief Add relocation information to the client
 * @details CGAddRelocationToClient passes relocation information back to the client.
 * This routine is called as many times as necessary during the call to
 * CGCompileRoutine.
 * @param clientRoutineHandle The clientRoutineHandle identifies the routine for the client,  it is the same handle that was passed to PCG in the call to CGCreateRoutine
 * @param codeOffset gives the location in the code at which to apply the relocation action, it is an offset from the start of the function
 * @param symbol gives the CGSymbol to which the relocation must be made
 * @param relocationType gives the type of relocation.  addend specifies a constant
 * @param addend used to compute the value to be stored in the relocatable field
 */
void CGAddRelocationToClient (void *clientRoutineHandle, uint64_t codeOffset, CGSymbol symbol, CGRelocationType relocationType, int64_t addend)
{
    //Get the cUnit
    CompilationUnitPCG *cUnit = static_cast<CompilationUnitPCG *> (clientRoutineHandle);

    //Paranoid
    assert (cUnit != 0);

    SClientSymbolInfo *clientSymbolInfo = singletonPtr<PersistentInfo> ()->getSymbolInfo (cUnit, symbol);
    if (clientSymbolInfo != 0)
    {
        CRelocation *relocation = CRelocation::create (clientSymbolInfo, (int32_t)addend, (uint32_t)codeOffset, relocationType);
        if (relocation != 0)
        {
            //Add a relocation to the list
            cUnit->addRelocation (relocation);
        }
        else
        {
            cUnit->errorHandler->setError (kJitErrorPcgRelocationCreation);
        }
    }
    else
    {
        cUnit->errorHandler->setError (kJitErrorPcgUnknownSymbol);
    }
}

