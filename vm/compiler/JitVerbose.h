/*
* Copyright (C) 2013 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef DALVIK_VM_JITVERBOSE_H_
#define DALVIK_VM_JITVERBOSE_H_

//Forward declarations
struct CompilationUnit;
struct ChainCellCounts;

/* @brief Print the code block in code cache in the range of [startAddr, endAddr) in readable format.
 * @param startAddr the start address of the emitted code block
 * @param endAddr the end address of the emitted code block
 */
void dvmCompilerPrintEmittedCodeBlock (unsigned char *startAddr,
                                       unsigned char *endAddr);

/**
 * @brief Print the contents of the code blocks and chaining cells to the LOG
 * @param cUnit pointer to the CompilationUnit
 * @return the trace cache pointer if code was printed, or 0 otherwise
 */
char *dvmCompilerPrintTrace (CompilationUnit *cUnit);

/**
 * @brief Print the chaining cell counts for a trace to logcat
 * @param chainingCellCountAddr a pointer to the beginning of the chaining cell counts in the emitted code
 * @param chainCellCounts reference to the ChainCellCounts table
 * @return the trace cache pointer if code was printed, or 0 otherwise
 */
char *dvmCompilerPrintChainingCellCounts (char *chainingCellCountAddr, ChainCellCounts &chainCellCounts);

/**
 * @brief Print the chaining cell offset header content
 * @param pCCOffsetSection - pointer to the chaining cell offset header
 */
void dvmCompilerPrintChainingCellOffsetHeader (u2 *pCCOffsetSection);

#endif
