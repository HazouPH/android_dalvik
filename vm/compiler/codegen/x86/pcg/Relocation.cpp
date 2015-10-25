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

#include "CompilerIR.h"
#include "Relocation.h"

CRelocation *CRelocation::create (const SClientSymbolInfo *targetSymbol, int32_t addend, uint32_t codeOffset, CGRelocationType relocationType)
{
    if (targetSymbol == 0)
    {
        return 0;
    }

    // Make space on arena for this instance
    void * space = dvmCompilerNew(sizeof(CRelocation), true);

    // Ensure that constructor is called
    CRelocation *newRelocation = new (space) CRelocation (targetSymbol, addend, codeOffset, relocationType);

    // Paranoid because dvmCompilerNew should never return 0
    assert(newRelocation != 0);

    return newRelocation;
}

void CRelocation::resolve (uint8_t *codePtr)
{
    uint8_t *symbolAddress = (uint8_t*) targetSymbol->address;
    void *ip = codePtr + codeOffset;

    switch (relocationType)
    {
       case CGRelocationType32:
           {
               //Get uint32_t data types
               uint32_t reloc = (uint32_t) (symbolAddress + addend);
               uint32_t *ptr = static_cast<uint32_t *> (ip);
               *ptr = reloc;
           }
           break;

       case CGRelocationTypePC32:
           {
               //Get uint32_t data types
               uint32_t *ptr = static_cast<uint32_t *> (ip);
               uint8_t *ptr8uint = static_cast<uint8_t *> (ip);
               uint32_t reloc = (uint32_t) (symbolAddress - ptr8uint + addend);
               *ptr = reloc;
           }
           break;

       default:
           // Unsupported relocation type.
           ALOGI ("JIT_INFO: PCG: Unsupported relocation type %d in CRelocation::resolve", relocationType);
           //TODO: Change this to use the error framework
           assert (0);
           break;
    }
}

