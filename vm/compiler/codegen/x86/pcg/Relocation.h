/*
 * Copyright  (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0  (the "License");
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

#ifndef H_RELOCATION
#define H_RELOCATION

#include "libpcg.h"
#include "DataStructures.h"

/**
 * @class CRelocation
 * @brief CRelocation provides information about a relocation and how to patch it
 */
class CRelocation
{
private:
    /** @brief The symbol that needs to be referenced */
    const SClientSymbolInfo *targetSymbol;

    /** @brief A constant offset to be added to &targetSymbol in the calculation of the symbol reference */
    const int32_t addend;

    /** @brief The offset from the start of the routine where the reference is needed */
    const uint32_t codeOffset;

    /** @brief The type of relocation  (absolute vs. PC-relative) */
    const CGRelocationType relocationType;

    /**
     * @brief private Constructor
     * @param _targetSymbol the symbol that needs to be referenced
     * @param _addend a constant offset to be added to &targetSymbol in the calculation of the symbol reference
     * @param _codeOffset the offset from the start of the routine where the reference is needed
     * @param _relocationType the type of relocation  (absolute vs. PC-relative)
     */
    CRelocation (const SClientSymbolInfo *_targetSymbol, const int32_t _addend, const uint32_t _codeOffset, const CGRelocationType _relocationType) :
        targetSymbol (_targetSymbol), addend (_addend), codeOffset (_codeOffset), relocationType (_relocationType) {}

public:
    /**
     * @brief static method to create an instance of CRelocation
     * @details allocates memory in compile arena
     * @param targetSymbol the symbol that needs to be referenced
     * @param addend a constant offset to be added to &targetSymbol in the calculation of the symbol reference
     * @param codeOffset the offset from the start of the routine where the reference is needed
     * @param relocationType the type of relocation  (absolute vs. PC-relative)
     * @return new instance of CRelocation or null if parameters are invalid
     */
    static CRelocation *create (const SClientSymbolInfo *targetSymbol, int32_t addend, uint32_t codeOffset, CGRelocationType relocationType);

    /**
     * @brief returns the symbol that needs to be referenced
     * @return the symbol that needs to be referenced
     */
    const SClientSymbolInfo *getSymbolInfo (void) const { return targetSymbol; }

    /**
     * @brief returns the offset from the start of the routine where the reference is needed
     * @return the offset from the start of the routine where the reference is needed
     */
    uint32_t getCodeOffset (void) const {return codeOffset;}

    /**
     * @brief returns the type of relocation  (absolute vs. PC-relative)
     * @return the type of relocation  (absolute vs. PC-relative)
     */
    CGRelocationType getType (void) const {return relocationType;}

    /**
     * @brief Resolve this relocation
     * @param codePtr the code section for the trace generation
     */
    void resolve (uint8_t *codePtr);
};
#endif
