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

#ifndef H_UTILITYPCG
#define H_UTILITYPCG

#include <string>

#include "CompilerIR.h"
#include "DataStructures.h"

//Forward declarations
struct BasicBlockPCG;
class CompilationUnitPCG;

/**
 * @brief Used to check whether PCG supports extended opcode
 * @param extendedOpcode The opcode to check
 * @return Returns whether the extended opcode is supported
 */
bool dvmCompilerPcgSupportsExtendedOp (int extendedOpcode);

/**
 * @brief Should we be using PCG for the trace
 * @param cUnit the CompilationUnit
 * @return whether we want to handle the trace
 */
bool dvmCompilerPcgSupportTrace (CompilationUnit *cUnit);

/**
 * @brief Get a pcgDtype name
 * @param dtype the pcgDtype
 * @return the const char* associated to dtype
 */
const char* dvmCompilerPcgGetDtypeName (pcgDtype dtype);

/**
 * @brief Determine whether dtype is the high half of an 8-byte type
 * @param dtype the pcgDtype of interest
 * @return true if dtype is the high half of an 8-byte type, false otherwise
 */
bool dvmCompilerPcgIsHighDtype (pcgDtype dtype);

/**
 * @brief Dump the register information
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgDumpModRegInfo (CompilationUnitPCG *cUnit);

/**
 * @brief Set the DType for a given SSA
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA number
 * @param dtype the type of the SSA register
 */
void dvmCompilerPcgSetDtypeForSSANum(CompilationUnitPCG *cUnit, int ssaNum, pcgDtype dtype);

/**
 * @brief Get the DType for a given SSA
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA number
 * @param info the SSANumInfo
 * @return the pcgDtype for the given ssaNum
 */
pcgDtype dvmCompilerPcgGetDtypeForSSANum(CompilationUnitPCG *cUnit, int ssaNum, SSANumInfo &info);

/**
 * @brief Get the DType for a given SSA
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA number
 * @return the pcgDtype for the given ssaNum
 */
pcgDtype dvmCompilerPcgGetDtypeForSSANum(CompilationUnitPCG *cUnit, int ssaNum);

/**
 * @brief Get the opcode and size for a given dtype move
 * @param cUnit the CompilationUnitPCG
 * @param dtype the pcgDtype
 * @param opc_ptr the name of the move opcode associated
 * @return the size for the dtype move
 */
//TODO: this should return a boolean and fill in size and opc via the arguments
int32_t dvmCompilerPcgGetOpcodeAndSizeForDtype (CompilationUnitPCG *cUnit, pcgDtype dtype, const char **opc_ptr);

/**
 * @brief Compute a default translation dtype in case the input dtype is unknown (NOreg)
 * @param dtype the pcgDtype
 * @param size the size in bytes of the needed pcgDtype
 * @return the pcgDtype to use in translation
 */
pcgDtype dvmCompilerPcgApplyDefaultDtype (pcgDtype dtype, int32_t size);

/**
 * @brief Return a handle for a given virtual register
 * @param virtualReg the virtual register
 * @param size the size
 * @return a non valid pointer to the handler
 * @details: the function guarantees that the combination virtualReg, size provides a non 0 unique handle
 */
void* dvmCompilerPcgGetVRHandle (u2 virtualReg, uint32_t size);

/**
 * @brief Get a virtual register
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA number
 * @param pcgOpcode the opcode
 * @param loadSize the size of the load
 * @return the load for the virtual register
 */
CGInst dvmCompilerPcgGetVirtualReg (CompilationUnitPCG *cUnit, int ssaNum, const char *pcgOpcode, uint32_t loadSize);

/**
 * @brief Set a virtual register
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA number
 * @param pcgOpcode the opcode
 * @param storeSize the size of the store
 * @param store_val what we are storing
 */
void dvmCompilerPcgSetVirtualReg (CompilationUnitPCG *cUnit, int ssaNum, const char *pcgOpcode, uint32_t storeSize, CGInst store_val);

/**
 * @brief Get an instruction that references an XMM temporary from the MIR
 * @param cUnit the CompilationUnitPCG
 * @param xmmNum the XMM temporary number
 * @return A CGInst that holds the desired XMM temp
 */
CGInst dvmCompilerPcgGetXMMReg (CompilationUnitPCG *cUnit, int xmmNum);

/**
 * @brief Define an XMM temporary from the MIR
 * @param cUnit the CompilationUnitPCG
 * @param xmmNum the XMM temporary number
 * @param val the instruction we are using to define the XMM temporary
 */
void dvmCompilerPcgSetXMMReg (CompilationUnitPCG *cUnit, int xmmNum, CGInst val);

/**
 * @brief Get a block name
 * @param bb the BasicBlockPCG
 * @param label the block name is set there
 * @return a string representation of bb
 */
void dvmCompilerPcgGetBlockName (BasicBlockPCG *bb, std::string &label);

/**
 * @brief Does the basic block finish with an invoke?
 * @param bb the BasicBlockPCG
 * @return whether or not the BasicBlockPCG ends with an invoke
 */
bool dvmCompilerPcgBlockEndsInInvoke (BasicBlockPCG *bb);

/**
 * @brief Get the ResClasses
 * @param selfPtr the self pointer
 */
CGInst dvmCompilerPcgGetResClasses (CGInst selfPtr);

/**
 * @brief Generate X86 call
 * @param cUnit the CompilationUnitPCG
 * @param targetName the target name
 * @param resultDtype the result type
 * @param nArgs the number of arguments
 */
CGInst dvmCompilerPcgGenerateX86Call (CompilationUnitPCG *cUnit, const char *targetName, pcgDtype resultDtype, int nArgs, ...);

/**
 * @brief Generate an entry stub
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgCreateEntryStub (CompilationUnitPCG *cUnit);

/**
 * @brief Handle initial load of a SSA register
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlock
 * @param ssaNum the SSA register we wish to load in
 * @param considerSpeculative should we be considering speculative loads
 */
void dvmCompilerPcgHandleInitialLoad (CompilationUnitPCG *cUnit, BasicBlock *bb, int ssaNum, bool considerSpeculative);

/**
 * @brief Used to handle initial loads for all registers live into the block we are going to
 * @param cUnit The compilation unit
 * @param blockGoingTo The block we are entering to
 */
void dvmCompilerPcgLoadLiveInVRs (CompilationUnitPCG *cUnit, BasicBlock *blockGoingTo);

/**
 * @brief Resolve a Class
 * @param cUnit the CompilationUnitPCG
 * @param classIdx the class index
 * @return the instruction containing the class information
 */
CGInst dvmCompilerPcgResolveClass(CompilationUnitPCG *cUnit, u4 classIdx);

/**
 * @brief Remove the phi nodes in the loop from the bitvector
 * @param cUnit the CompilationUnitPCG
 * @param bv The bit vector to remove phi nodes from
 * @param loopEntry The basic block which is the beginning of the loop (containing the phi nodes)
 * @details This function removes all of the SSA numbers of the arguments (uses) to the phi nodes from the bv. The phi nodes are found in the loopEntry BB.
 */
void dvmCompilerPcgRemoveNonPhiNodes (CompilationUnitPCG *cUnit, BitVector *bv, BasicBlockPCG *loopEntry);

/**
 * @brief Used to obtain the offset relative to VM frame pointer for a given VR
 * @param cUnit The compilation unit
 * @param vR The virtual register number for which to calculate offset
 * @return Returns the offset relative to VM frame pointer
 */
int dvmCompilerPcgGetVROffsetRelativeToVMPtr (CompilationUnitPCG *cUnit, int vR);

/**
 * @brief Client routine to create and return memory constants
 * @param cUnit the CompilationUnitPCG
 * @param value the constant value
 * @param length number of bytes defining the constant
 * @param align the alignment required
 */
CGSymbol pcgDvmClientGetMemConstSymbol(CompilationUnitPCG *cUnit, uint8_t *value, size_t length, uint32_t align);

/**
 * @brief Print the content of a trace to LOG.
 * @param basicCompilationUnit - pointer to the CompilationUnit
 * @param chainCellCounts - reference to the ChainCellCounts table
 * @param pCCOffsetSection - pointer to the chaining cell offset header
 */
void dvmCompilerPcgPrintTrace (CompilationUnit *basicCompilationUnit, ChainCellCounts &chainCellCounts, u2* pCCOffsetSection);
#endif
