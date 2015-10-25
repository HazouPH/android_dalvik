/*
 * Copyright (C) 2013 The Android Open Source Project
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

#ifndef DALVIK_VM_UTILITY_H_
#define DALVIK_VM_UTILITY_H_

//Forward declarations
struct CompilationUnit;
struct GrowableList;
struct SSARepresentation;

/**
 * @brief Find the entry points of the CompilationUnit
 * @param cUnit the CompilationUnit
 * @param list the GrowableList to contain all the entry-point BasicBlocks
 */
void dvmCompilerFindEntries (CompilationUnit *cUnit, GrowableList *list);

/**
 * @brief Get the next BasicBlock when considering a BasicBlock index BitVector
 * @param bvIterator the BitVector iterator
 * @param blockList The list of basic blocks
 * @return 0 if finished, the BasicBlock otherwise
 */
BasicBlock *dvmCompilerGetNextBasicBlockViaBitVector (BitVectorIterator &bvIterator, const GrowableList &blockList);

/**
 * @brief Used to check if invoke is polymorphic and as a result needs prediction for inlining
 * @param opcode The opcode to check
 * @return Returns true if a prediction for inlining needs to be made
 */
bool dvmCompilerDoesInvokeNeedPrediction (Opcode opcode);

/**
 * @brief Checks if invoke calls fully resolved method.
 * @details Since we are including instructions from possibly a cold method into the current trace,
 * we need to make sure that all the associated information with the callee is properly initialized.
 * @param methodContainingInvoke The method that contains the invoke bytecode
 * @param invokeInstr The decoded instruction of invoke that we are examining
 * @param tryToResolve In case it hasn't been resolved, try to resolve it now. (Default: false)
 * @return Returns the resolved method or null if cannot resolve or has not been resolved
 */
const Method *dvmCompilerCheckResolvedMethod (const Method *methodContainingInvoke,
        const DecodedInstruction *invokeInstr, bool tryToResolve = false);

/**
 * @brief Checks if bytecodes in method reference fully resolved classes, methods, and fields.
 * @details Since we are including instructions from possibly a cold method into the current trace,
 * we need to make sure that all the associated information with the callee is properly initialized.
 * @param method The method that contains the bytecode
 * @param insn The decoded instruction that we are examining
 * @param tryToResolve In case it hasn't been resolved, try to resolve it now. (Default: false)
 * @return Returns whether class/method/field is fully resolved.
 */
bool dvmCompilerCheckResolvedReferences (const Method *method, const DecodedInstruction *insn, bool tryToResolve = false);

/**
 * @brief Used to update the number of dalvik registers in a cUnit.
 * @details The update is only done if the new number is larger than old number of dalvik registers.
 * Function ensures that all structures dependent on this are invalidated.
 * @param cUnit The compilation unit
 * @param newNumberDalvikRegisters The new number of dalvik registers to use
 */
void dvmCompilerUpdateCUnitNumDalvikRegisters (CompilationUnit *cUnit, int newNumberDalvikRegisters);

/**
 * @brief Used the obtain the maximum number of scratch registers that can be used
 * @return Returns the maximum number of scratch registers
 */
unsigned int dvmCompilerGetMaxScratchRegisters (void);

/**
 * @brief Get a scratch register if possible
 * @param cUnit The compilation unit
 * @param consecutives how many consecutive registers do you want?
 * This allows the request for consecutive VRs for wide or range cases (default: 1)
 * @return Returns the lowest new VR scratch register allocated; if consecutives is > 1,
 * suppose VR, VR + 1, ..., VR + consecutives - 1 are allocated. Returns -1 if none found.
 */
int dvmCompilerGetFreeScratchRegister (CompilationUnit *cUnit, unsigned int consecutives = 1);

/**
 * @brief Used to determine whether a given virtual register is a pure local scratch
 * @param cUnit The compilation unit
 * @param reg The register to check (can be virtual register or ssa register)
 * @param isSsa True if "reg" parameter is ssa register
 * @return Returns if the the virtual register is actually scratch
 */
bool dvmCompilerIsPureLocalScratch (const CompilationUnit *cUnit, int reg, bool isSsa = false);

/**
 * @brief Used post-optimization pass to commit the pending scratch registers
 * @param cUnit The compilation unit
 */
void dvmCompilerCommitPendingScratch (CompilationUnit *cUnit);

/**
 * @brief Returns whether there is a loop in the CFG
 * @param blockList The blocks in the CFG
 * @param entry The entry block into CFG
 * @return Returns whether a loop exists
 */
bool dvmCompilerDoesContainLoop (GrowableList &blockList, BasicBlock *entry);

/**
 * @brief Resets optimization flags for all MIRs in BasicBlock
 * @param bb The basic block to act on
 * @param resetFlags The flags to use for the reset
 */
void dvmCompilerResetOptimizationFlags (const BasicBlock *bb, int resetFlags);

/**
 * @brief Removes all bytecode blocks that are not reachable from entry
 * @param cUnit the Compilation Unit
 */
void dvmCompilerRemoveUnreachableBlocks (CompilationUnit *cUnit);

/**
 * @brief Checks if an ssa register is a constant value
 * @param cUnit The compilation unit
 * @param ssaReg The ssa register to check for constantness
 * @return Returns true if the register holds a constant value
 */
bool dvmCompilerIsRegConstant (const CompilationUnit *cUnit, int ssaReg);

/**
 * @brief Used to obtain the first 32-bit constant used by the MIR
 * @details First looks if the MIR uses hardcoded constants encoded in instruction.
 * Then looks through each use in order to find the first VR use that is constant.
 * @param cUnit The compilation unit
 * @param mir The mir to look into for constants.
 * @param constantValue Updated by function to contain constant value if one is found
 * @return Returns whether or not a constant was found for the MIR.
 */
bool dvmCompilerGetFirstConstantUsed (const CompilationUnit *cUnit, const MIR *mir, int &constantValue);

/**
 * @brief Check if code cache will overflow after adding more code to code cache
 * @param moreCode the number of bytes to add to code cache
 * @return true if overflow
 */
bool dvmCompilerWillCodeCacheOverflow(unsigned int moreCode);

/**
 * @brief Check if data cache will overflow after adding more data to data cache
 * @param moreData the number of bytes to add to data cache
 * @return true if overflow
 */
bool dvmCompilerWillDataCacheOverflow(unsigned int moreData);

/**
 * @brief Set code cache and data cache full
 */
void dvmCompilerSetCodeAndDataCacheFull(void);

/**
 * @brief Set data cache full
 */
void dvmCompilerSetDataCacheFull(void);

#endif
