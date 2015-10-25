/*
 * Copyright (C); 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");;
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

#ifndef H_LOWERMEMORY
#define H_LOWERMEMORY

#include "libpcg.h"

/**
 * @brief Create a typed store
 * @param cUnit the CompilationUnitPCG
 * @param base the base register
 * @param index the index register
 * @param scale the scale for the store
 * @param ltbase the base symbol
 * @param offset the offset for the store
 * @param dtype the type of the store
 * @param r the register containing what to store
 */
CGInst dvmCompilerPcgCreateTypedStore (CompilationUnitPCG *cUnit, CGInst base, CGInst index, uint32_t scale,
                                       CGSymbol ltbase, int32_t offset, pcgDtype dtype, CGInst r);

/**
 * @brief Get a self pointer
 * @param cUnit the CompilationUnitPCG
 * @return the instruction containing the self pointer
 */
CGInst dvmCompilerPcgGetSelfPointer (const CompilationUnitPCG *cUnit);

/**
 * @brief Create a simple load
 * @details this relates to 32-bit loads that just use a base register to specify the address
 * @param base the base register
 * @param offset the offset
 * @return the load
 */
CGInst dvmCompilerPcgCreateSimpleLoad (CGInst base, int32_t offset);

/**
 * @brief Create a simple store
 * @details this relates to 32-bit stores that just use a base register to specify the address
 * @param base the base register
 * @param offset the offset
 * @param r what we want to store
 * @return the store
 */
CGInst dvmCompilerPcgCreateSimpleStore (CGInst base, int32_t offset, CGInst r);


/**
 * @brief Export the program counter
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgExportPC (CompilationUnitPCG *cUnit);

/**
 * @brief Store a virtual register
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA register number
 * @param storeMask the store mask to know what to store
 */
void dvmCompilerPcgStoreVirtualReg (CompilationUnitPCG *cUnit, int ssaNum, int storeMask);

/**
 * @brief Create a store
 * @brief this relates to 32-bit stores
 * @param base the base register
 * @param index the index register
 * @param scale the scale
 * @param ltbase base symbol
 * @param offset the offset
 * @param r what we want to store
 * @return the store
 */
CGInst dvmCompilerPcgCreateStore (CGInst base, CGInst index, uint32_t scale,
        CGSymbol ltbase, int32_t offset, CGInst r);
#endif
