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

#ifndef H_CHAININGCELLEXCEPTION
#define H_CHAININGCELLEXCEPTION

#include "Common.h"
#include "CompilerIR.h"
#include "DataStructures.h"
#include "libpcg.h"
#include <string>
#include <list>

//Forward Definitions
struct BasicBlockPCG;
struct CompilationUnitPCG;
struct MIR;

/**
 * @brief Raise an exception without exporting the PC
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgGenerateRaiseExceptionSimple (CompilationUnitPCG *cUnit);

/**
 * @brief Raise an exception with exporting the PC
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgGenerateRaiseException (CompilationUnitPCG *cUnit);

/**
 * @brief Generate the null check
 * @param cUnit the CompilationUnitPCG
 * @param base the base instruction
 * @param mir the MIR instruction
 * @param ssaNum the SSA to 0 check (default: -1)
 */
void dvmCompilerPcgGenerateNullCheck (CompilationUnitPCG *cUnit, CGInst base, MIR *mir, int ssaNum = -1);

/**
 * @brief Generate a JSR to the dvmJitToExceptionThrown function
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (CompilationUnitPCG *cUnit);

/**
 * @brief Call the dvmJitToExceptionThrown function
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgGenerateCalldvmJitToExceptionThrown (CompilationUnitPCG *cUnit);

/**
 * @brief Generate a range check
 * @details Check to see if "index" is <= base[OFFSETOF_MEMBER (ArrayObject,length)] which holds the length of the array at "base".
 * @param cUnit the CompilationUnitPCG
 * @param base the base array
 * @param index the index for the range check
 * @param mir the MIR instruction
 */
void dvmCompilerPcgGenerateRangeCheck (CompilationUnitPCG *cUnit, CGInst base, CGInst index, MIR *mir);

/**
 * @brief Generate the null check
 * @param cUnit the CompilationUnitPCG
 * @param inst the instruction
 */
void dvmCompilerPcgGenerateSimpleNullCheck (CompilationUnitPCG *cUnit, CGInst inst);


/**
 * @brief Emit the chaining cells
 * @param cUnit the CompilationUnitPCG
 * @param pcgChainCellCounts the ChainCellCounts
 * @param start_addr is the routine start address
 * @param cache_ptr is a pointer to the next available byte in the code cache.
 * @param freeSpace free space still available
 * @return This routine returns the next available byte in the code cache, or 0 if there isn't enough available space to lay down the chaining cells
 */
uint8_t *dvmCompilerPcgEmitChainingCells (CompilationUnitPCG *cUnit,
        ChainCellCounts *pcgChainCellCounts,
        uint8_t *start_addr,
        uint8_t *cache_ptr,
        size_t freeSpace);

/**
 * @brief Emit the switch tables
 * @param cUnit the CompilationUnitPCG
 * @param currCachePtr is a pointer to the next available byte in the code cache.
 * @param freeSpace free space still available
 * @return This routine returns the next available byte in the code cache, or 0 if there isn't enough available space to lay down the chaining cells
 */
uint8_t* dvmCompilerPcgEmitSwitchTables(CompilationUnitPCG *cUnit,
        uint8_t *currCachePtr,
        size_t freeSpace);

/**
 * @brief Generate a speculative null checks
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgGenerateSpeculativeNullChecks (CompilationUnitPCG *cUnit);
#endif
