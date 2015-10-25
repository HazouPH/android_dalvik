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
#include "ChainingCellException.h"
#include "CodeGeneration.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "CompilerIR.h"
#include "Dataflow.h"
#include "Labels.h"
#include "Lower.h"
#include "LowerCall.h"
#include "LowerJump.h"
#include "LowerMemory.h"
#include "LowerOther.h"
#include "Utility.h"
#include "UtilityPCG.h"
#include "NcgHelper.h"
#include "PersistentInfo.h"
#include "Singleton.h"

//TODO: If we want PCG independent of CSO, each getCallBack should be checked and the function should either be duplicated for PCG or better set in common code

/**
 * @brief Generate the JSR to dvmJitToExceptionThrown
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (CompilationUnitPCG *cUnit)
{
    dvmCompilerPcgGenerateWritebacks (cUnit, cUnit->getCurrMod ());

    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

    //Get symbol to the dvmJitToExceptionThrown callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToExceptionThrown");

    //Paranoid
    assert (callback != 0);

    dvmCompilerPcgCreateJsr (cUnit, callback, parms);
}

/**
 * @brief Generate a singleton chaining cell
 * @param cUnit the CompilationUnitPCG
 * @param startAddr the start address for the code generation
 * @param bytecodeTargetAddr the bytecode address
 * @param blockSymbol the symbol for the basic block
 * @param currCachePtr the current code cache pointer
 * @param freeSpace amount of currently available free space
 */
static uint8_t* dvmCompilerPcgEmitSingletonChainingCell (CompilationUnitPCG *cUnit,
                                              const uint8_t *startAddr,
                                              const void *bytecodeTargetAddr,
                                              CGSymbol blockSymbol,
                                              uint8_t *currCachePtr,
                                              size_t freeSpace)
{
    const uint8_t *cellBegin = currCachePtr;

    // First thing we should check whether there is a reference to our chaining cell
    const CRelocation *relocation = cUnit->findRelocation (blockSymbol);
    if (relocation == 0)
    {
        // No reference so no change in code cache
        return currCachePtr;
    }

    // Make sure we don't overflow the code cache
    if (freeSpace < SINGLETON_CC_SIZE)
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("LOWER InvokeSingletonChainingCell at @%p\n", currCachePtr);
    }

    // This is the "real" beginning of the chaining cell.  Any branches to this
    // block should land here, so bind the symbol to this address.
    dvmCompilerPcgBindSymbolAddress (cUnit, blockSymbol, currCachePtr);

    // Now lay down the call instruction.  This is
    // "call dvmJitToInterpTraceSelect".
    *currCachePtr++ = 0xe8;

    //Get symbol to the dvmJitToInterpTraceSelect callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpTraceSelect");
    void * callbackAddress = dvmCompilerPcgGetSymbolAddress (cUnit, callback);

    * ( (int32_t*)currCachePtr) = (int32_t)callbackAddress - ( (int32_t)currCachePtr + 4);
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    call dvmJitToInterpTraceSelect: 0xe8 0x%08x\n", * ( (int32_t*)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the bytecode target address.
    * ( (const void**)currCachePtr) = bytecodeTargetAddr;
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    rPC: 0x%p\n", * ( (const void**)currCachePtr));
    }
    currCachePtr += 4;

    // Finally, lay down the address in the code where this chaining cell is referenced.
    * ( (const uint8_t**)currCachePtr) = startAddr + relocation->getCodeOffset();
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    codePtr: 0x%p\n", * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    // Since singleton chaining cells are handled liked hot chaining cells
    // we need to lay down the isMove flag here, too, to match the format
    * ( (unsigned int*)currCachePtr) = 0;
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    isMove: 0x%08x\n", * ( (int *)currCachePtr));
    }
    currCachePtr += 4;

    // Since the assert doesn't always compile, avoid the 'unused' warning
    (void)cellBegin;
    assert( (size_t) (currCachePtr - cellBegin) == SINGLETON_CC_SIZE);

    return currCachePtr;
}

/*
 * Initial value of predicted chain cell
 * EB FE   : jmp -2 // self
 * 0F 1F 00: nop3
 * 0F 1F 00: nop3
 *
 * When patched with 5-byte call/jmp rel32 instruction it will be correct.
 */
#define PREDICTED_CHAIN_BX_PAIR_INIT1     0x1f0ffeeb
#define PREDICTED_CHAIN_BX_PAIR_INIT2     0x001f0f00

/**
 * @brief Emit a predicted chaining cell
 * @param cUnit the CompilationUnitPCG
 * @param blockSymbol the symbol for a given basic block
 * @param currCachePtr the current cache pointer
 * @param freeSpace amount of free space available
 * @return the pointer to the next byte after the chaining cell, 0 if a problem arose
 * @details:  Chaining cell for monomorphic method invocations.
 * This "block" contains only data. The data within this block will
 * get patched again, later.
 * This block will be referenced via the stored cgSymbol, and
 * dereferenced during predicted chaining execution.
 */
static uint8_t* dvmCompilerPcgEmitPredictedChainingCell (CompilationUnitPCG *cUnit,
                                              CGSymbol blockSymbol,
                                              uint8_t *currCachePtr,
                                              size_t freeSpace)
{
    // First thing we should check whether there is a reference to our chaining cell
    const CRelocation *relocation = cUnit->findRelocation (blockSymbol);
    if (relocation == 0)
    {
        // No reference so no change in code cache
        return currCachePtr;
    }

    unsigned alignment = (4 - ((u4) currCachePtr & 3)) & 3;

    // Make sure we don't overflow the code cache
    if (freeSpace < PREDICTED_CC_SIZE + alignment)
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }

    // 4-byte align the start of the cell
    currCachePtr += alignment;

    const uint8_t *cellBegin = currCachePtr;

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("LOWER InvokePredictedChainingCell at offsetPC @%p\n", currCachePtr);
    }

    // This is the beginning of the chaining cell. All symbol referenced will be made to this location.
    dvmCompilerPcgBindSymbolAddress (cUnit, blockSymbol, currCachePtr);
    int *intStream = (int *)currCachePtr;

    intStream[0] = PREDICTED_CHAIN_BX_PAIR_INIT1;
    intStream[1] = PREDICTED_CHAIN_BX_PAIR_INIT2;
    // To be filled: class
    intStream[2] = PREDICTED_CHAIN_CLAZZ_INIT;
    // To be filled: method
    intStream[3] = PREDICTED_CHAIN_METHOD_INIT;
    // Rechain count. The initial value of 0 here will trigger chaining upon the first invocation of this callsite.
    intStream[4] = PREDICTED_CHAIN_COUNTER_INIT;
    currCachePtr += PREDICTED_CC_SIZE;

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
                intStream[0], intStream[1], intStream[2], intStream[3],
                intStream[4]);
    }

    // Since the assert doesn't always compile, avoid the 'unused' warning
    (void)cellBegin;
    assert( (size_t) (currCachePtr - cellBegin) == PREDICTED_CC_SIZE);

    return currCachePtr;
}

/**
 * @brief Emit a hot chaining cell
 * @param cUnit the CompilationUnitPCG
 * @param startAddr the start address for the code generation
 * @param bytecodeTargetAddr the bytecode address
 * @param blockSymbol the symbol for a given basic block
 * @param currCachePtr the current cache pointer
 * @param freeSpace amount of free space available
 * @return the pointer to the next byte after the chaining cell, 0 if a problem arose
 * @details: This block will be referenced via the stored cgSymbol, and dereferenced during predicted chaining execution.
 */
static uint8_t* dvmCompilerPcgEmitHotChainingCell (CompilationUnitPCG *cUnit,
                                        const uint8_t *startAddr,
                                        const void *bytecodeTargetAddr,
                                        CGSymbol blockSymbol,
                                        uint8_t *currCachePtr,
                                        size_t freeSpace)
{
    const uint8_t *cellBegin = currCachePtr;

    // First thing we should check whether there is a reference to our chaining cell
    const CRelocation *relocation = cUnit->findRelocation (blockSymbol);
    if (relocation == 0)
    {
        // No reference so no change in code cache
        return currCachePtr;
    }

    // Make sure we don't overflow the code cache
    if (freeSpace < HOT_CC_SIZE)
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("LOWER HotChainingCell at @%p.\n", currCachePtr);
    }

    // This is the "real" beginning of the chaining cell.  Any branches to this
    // block should land here, so bind the symbol to this address.
    dvmCompilerPcgBindSymbolAddress (cUnit, blockSymbol, currCachePtr);

    // Now lay down the call instruction.
    *currCachePtr++ = 0xe8;

    //Get symbol to the dvmJitToInterpTraceSelect callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpTraceSelect");
    void * callbackAddress = dvmCompilerPcgGetSymbolAddress (cUnit, callback);

    * ( (int32_t*)currCachePtr) = (int32_t)callbackAddress - ( (int32_t)currCachePtr + 4);

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    call dvmJitToInterpTraceSelect: 0xe8 0x%08x\n", * ( (int32_t*)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the bytecode target address.
    * ( (const void**)currCachePtr) = bytecodeTargetAddr;
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    rPC: 0x%p\n", * ( (const void**)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the address in the code where this chaining cell is referenced.
    * ( (const uint8_t**)currCachePtr) = startAddr + relocation->getCodeOffset ();
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    codePtr: 0x%p\n", * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    // Finally, lay out a flag to signal whether the reference is absolute or
    // PC-relative.
    int isRelative = (relocation->getType () == CGRelocationTypePC32) ? 1 : 0;
    * ( (int *)currCachePtr) = isRelative;

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    isMove: 0x%08x\n", * ( (int *)currCachePtr));
    }
    currCachePtr += 4;

    // Since the assert doesn't always compile, avoid the 'unused' warning
    (void)cellBegin;
    assert( (size_t) (currCachePtr - cellBegin) == HOT_CC_SIZE);

    return currCachePtr;
}

/**
 * @brief Return the predecessor BB of switch-associated chaining cells
 * @param cUnit the CompilationUnitPCG
 * @param normalChainingCellBB the chaining cell which might be switch-associated
 * @return the pointer to the switch BB or 0 if the CC is not switch-associated
 * @details: Certain normal chaining cells are associated with switch statements, and they require slightly different processing from normal chaining cells that end traces. In particular, the patch address in the CC must point to the switch table, rather than into the compiled instruction stream.
 */
static BasicBlockPCG * getPredecessorSwitchBasicBlock(
    CompilationUnitPCG *cUnit,
    BasicBlockPCG * normalChainingCellBB)
{
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (normalChainingCellBB->predecessors, &bvIterator);

    // Get the precedessor BB for the chainingcell BB
    BasicBlockPCG *predBB = reinterpret_cast<BasicBlockPCG *> (
        dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList));

    // Return the predBB only if it contains a switch instruction
    if (predBB != 0 && predBB->blockType == kDalvikByteCode &&
        predBB->lastMIRInsn != 0)
    {
        if(predBB->lastMIRInsn->dalvikInsn.opcode == OP_PACKED_SWITCH ||
           predBB->lastMIRInsn->dalvikInsn.opcode == OP_SPARSE_SWITCH)
        {
            return predBB;
        }
    }

    return 0;
}

/**
 * @brief Emit a normal chaining cell
 * @param cUnit the CompilationUnitPCG
 * @param startAddr the start address for the code generation
 * @param bytecodeTargetAddr the bytecode address
 * @param bb the chaining cell's basicblock in the CFG
 * @param currCachePtr the current cache pointer
 * @param freeSpace amount of free space available
 * @return the pointer to the next byte after the chaining cell, 0 if a problem arose
 * @details Normal chaining cells are completely finalized at emission time. Switch-associated chaining cells cannot be finalized, because they require the switch table to be laid down. Thus, we use the relocation system to patch the chaining cell's relocation address to be the switch table's entry corresponding to this chaining cell.
 */
static uint8_t* dvmCompilerPcgEmitNormalChainingCell (
    CompilationUnitPCG *cUnit,
    const uint8_t *startAddr,
    const void *bytecodeTargetAddr,
    BasicBlockPCG * bb,
    uint8_t *currCachePtr,
    size_t freeSpace)
{
    CGSymbol blockSymbol = bb->chainingCellSymbol;
    const uint8_t *cellBegin = currCachePtr;

    // First thing we should check whether there is a reference to our chaining cell
    // But if predecessor is switch we should generate it anyway
    BasicBlockPCG * switchBB = getPredecessorSwitchBasicBlock(cUnit, bb);
    const CRelocation *relocation = cUnit->findRelocation (blockSymbol);
    if (relocation == 0 && switchBB == 0)
    {
        // No reference so no change in code cache
        return currCachePtr;
    }

    // Make sure we don't overflow the code cache
    if (freeSpace < NORMAL_CC_SIZE)
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("LOWER NormalChainingCell at @%p\n", currCachePtr);
    }

    // Lay down a normal chaining cell in memory.  Given that the VM relies on
    // the exact length of the code sequence, we emit the code bytes directly
    // without implicitly relying on the behavior of the encoder, which might
    // encode an instruction in an unexpected way when multiple choices are
    // available.

    // This is the "real" beginning of the chaining cell.  Any branches to this
    // block should land here, so bind the symbol to this address.
    dvmCompilerPcgBindSymbolAddress (cUnit, blockSymbol, currCachePtr);

    // Now lay down the call instruction.  This is "call dvmJitToInterpNormal".
    *currCachePtr++ = 0xe8;

    //Get symbol to the dvmJitToInterpNormal callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpNormal");
    void * callbackAddress = dvmCompilerPcgGetSymbolAddress (cUnit, callback);

    * ( (int32_t*)currCachePtr) = (int32_t)callbackAddress - ( (int32_t)currCachePtr + 4);

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    call dvmJitToInterpNormal: 0xe8 0x%08x\n",
                * ( (int32_t*)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the bytecode target address.
    * ( (const void**)currCachePtr) = bytecodeTargetAddr;
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    rPC: 0x%p\n", * ( (const void**)currCachePtr));
    }
    currCachePtr += 4;

    if (switchBB != 0)
    {
        // First we need to create a symbol for the switch table entry
        // so that we can refer to it now
        std::string switchBBName;
        dvmCompilerPcgGetBlockName (switchBB, switchBBName);

        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "%s_switch_%d",
            switchBBName.c_str(),
            cUnit->getNumberOfSwitchTableEntries ());

        std::string switchEntryName(buffer);
        CGSymbol switchEntrySymbol =
            dvmCompilerPcgCreateSymbol(cUnit, switchEntryName, 0, true);

        // Now that we have a symbol, we can create a relocation tracker for it
        const SClientSymbolInfo *clientSymbolInfo = singletonPtr<PersistentInfo> ()->getSymbolInfo (cUnit, switchEntrySymbol);
        // This chaining cell is associated with a switch statement
        CRelocation * swRelocation = CRelocation::create (clientSymbolInfo, (int32_t)0, (uint32_t)currCachePtr - (uint32_t)startAddr, CGRelocationType32);

        // That should be sufficient to accomplish the write to this location,
        // once the switch table entry symbol gets associated with an address,
        // so make sure we store it in the list
        cUnit->addSwitchTableEntry (swRelocation, bb);

        // Just put something in the cell, so we don't print anything wrong
        // in debug mode
        * ( (const uint8_t**)currCachePtr) = 0;
    }
    else
    {
        // Lay down the address in the code where this chaining cell is
        // referenced.
        * ( (const uint8_t**)currCachePtr) = startAddr + relocation->getCodeOffset ();
    }

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    codePtr: 0x%p\n", * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    // Finally, lay down the isSwitch flag (because switch cells need to cause
    // absolute targets to be patched, not relative targets
    int isSwitch = switchBB != 0 ? 1 : 0;

    * ( (int*)currCachePtr) = isSwitch;
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    isSwitch: 0x%d\n", * ( (const int*)currCachePtr));
    }
    currCachePtr += 4;

    // Since the assert doesn't always compile, avoid the 'unused' warning
    (void)cellBegin;
    assert( (size_t) (currCachePtr - cellBegin) == NORMAL_CC_SIZE);

    return currCachePtr;
}

/**
 * @brief Emit a backward branch chaining cell
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlockPCG
 * @param startAddr the start address for the code generation
 * @param bytecodeTargetAddr the bytecode address
 * @param currCachePtr the current cache pointer
 * @param freeSpace amount of free space available
 * @return the pointer to the next byte after the chaining cell, 0 if a problem arose
 */
static uint8_t* dvmCompilerPcgEmitBackwardBranchChainingCell (CompilationUnitPCG *cUnit, BasicBlockPCG *bb,
        const uint8_t *startAddr, const void *bytecodeTargetAddr, uint8_t *currCachePtr, size_t freeSpace)
{
    const uint8_t *cellBegin = currCachePtr;

    // First thing we should check whether there is a reference to our chaining cell
    CGSymbol blockSymbol = bb->chainingCellSymbol;
    CGSymbol writebackSymbol = bb->writebackTargetSymbol;

    if (cUnit->findRelocation (blockSymbol) == 0)
    {
        // No reference so no change in code cache
        return currCachePtr;
    }

    // Make sure we don't overflow the code cache
    if (freeSpace < BACKWARD_BRANCH_CC_SIZE)
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("LOWER BackwardBranchChainingCell with offsetPC @%p\n", currCachePtr);
    }

    // Lay down a backward branch chaining cell in memory.  Given that the VM
    // relies on the exact length of the code sequence, we emit the code bytes
    // directly without implicitly relying on the behavior of the encoder,
    // which might encode an instruction in an unexpected way when multiple
    // choices are available.

    // This is the "real" beginning of the chaining cell.  Any branches to this
    // block should land here, so bind the symbol to this address.
    dvmCompilerPcgBindSymbolAddress (cUnit, blockSymbol, currCachePtr);

    // Lay down the call instruction.
    // This is "call dvmJitToInterpBackwardBranch".
    *currCachePtr++ = 0xe8;

    //Get symbol to the dvmJitToInterpNormal callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpBackwardBranch");
    void * callbackAddress = dvmCompilerPcgGetSymbolAddress (cUnit, callback);

    * ( (int32_t*)currCachePtr) = (int32_t)callbackAddress -
       ( (int32_t)currCachePtr + 4);
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    call dvmJitToInterpBackwardBranch: 0xe8 0x%08x\n",
                * ( (int32_t*)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the bytecode target address, i.e. the head of the loop.
    * ( (const void**)currCachePtr) = bytecodeTargetAddr;
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    rPC: 0x%p\n", * ( (const void**)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the address in the code where this chaining cell is referenced.
    const CRelocation *relocation = cUnit->findRelocation (writebackSymbol);
    if (relocation == 0)
    {
        ALOGI ("JIT INFO: PCG: writebackSymbol is not found");

        //For the moment just make it fail with the generic error
        cUnit->errorHandler->setError (kJitErrorPcgCodegen);

        //Just return because this is already a bad enough situation
        return currCachePtr;
    }

    * ( (const uint8_t**)currCachePtr) = startAddr + relocation->getCodeOffset ();
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    codePtr: 0x%p\n", * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the code address of the loop header.
    int64_t loopHeaderOffset;

    // Find the loop header block, if applicable.  This block is the target
    // of the back edge of the loop in loop traces.
    BasicBlockPCG *loopHeader = 0;
    LoopInformation *loopInfo = cUnit->loopInformation;
    CGLabel loopHeadLabel = 0;

    if (loopInfo != 0)
    {
        loopInfo = loopInfo->getLoopInformationByEntry (bb->fallThrough);

        if (loopInfo != 0)
        {
            BasicBlockPCG *bb = (BasicBlockPCG *) loopInfo->getEntryBlock ();

            loopHeader = (BasicBlockPCG *) (bb);
        }

        //Paranoid
        assert (loopHeader != 0);

        if (loopHeader != 0)
        {
            loopHeadLabel = loopHeader->cgLabel;
        }
    }

    //Paranoid
    assert (loopHeadLabel != 0);

    CGGetLabelNameAndOffset (loopHeadLabel, &loopHeaderOffset);
    * ( (const uint8_t**)currCachePtr) = startAddr + (int32_t)loopHeaderOffset;

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    loopHeader: 0x%p\n", * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    // Lay down the code address of the jump target that must be used in the
    // event of unchaining.  This must be the address of the writeback block
    // for this chaining cell.
    * ( (const void**)currCachePtr) = dvmCompilerPcgGetSymbolAddress (cUnit, writebackSymbol);

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    unchainTarget: 0x%p\n",
                * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    //Get the label for the from interpreter node
    CGLabel fromInterpLabel = 0;

    if (loopInfo != 0)
    {
        BasicBlockPCG *bb = (BasicBlockPCG *) loopInfo->getFromInterpreter ();

        BasicBlockPCG *fromInterpBlock = (BasicBlockPCG *) (bb);

        //Paranoid
        assert (fromInterpBlock != 0);

        if (fromInterpBlock != 0)
        {
            fromInterpLabel = fromInterpBlock->cgLabel;
        }
    }

    //Paranoid
    assert (fromInterpLabel != 0);

    // Lay down the code address for the from interpreter node.  This is the
    // address to which dvmJitToInterpBackwardBranch transfers control after
    // patching the backward branch.
    int64_t fromInterpOffset = 0;
    CGGetLabelNameAndOffset (fromInterpLabel, &fromInterpOffset);
    * ( (const uint8_t**)currCachePtr) = startAddr + (int32_t) fromInterpOffset;

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("    traceBegin: 0x%p\n", * ( (const uint8_t**)currCachePtr));
    }
    currCachePtr += 4;

    // Since the assert doesn't always compile, avoid the 'unused' warning
    (void)cellBegin;
    assert ( (size_t) (currCachePtr - cellBegin) == BACKWARD_BRANCH_CC_SIZE);

    return currCachePtr;
}

/**
 * @details Utility routine to jump to the exception block, which then punts to the
 * interpreter.  This routine should be called if the PC has already been saved.
 */
void dvmCompilerPcgGenerateRaiseExceptionSimple (CompilationUnitPCG *cUnit)
{
    dvmCompilerPcgGenerateWritebacks (cUnit, cUnit->getCurrMod ());

    BasicBlockPCG *bb = cUnit->getBasicBlockPCG (cUnit->exceptionBlockId);

    //Paranoid test
    if (bb == 0)
    {
        //For the moment just make it fail with the generic error
        cUnit->errorHandler->setError (kJitErrorPcgCodegen);

        //Just return because this is already a bad enough situation
        return;
    }

    CGCreateNewInst ("jmp", "b", bb->cgLabel);
    cUnit->setExceptionBlockReferenced (true);
}

/**
 * @details Utility routine to export the PC and jump to the exception block, which then punts to the interpreter.
 */
void dvmCompilerPcgGenerateRaiseException (CompilationUnitPCG *cUnit)
{
    dvmCompilerPcgExportPC (cUnit);
    dvmCompilerPcgGenerateRaiseExceptionSimple (cUnit);
}

void dvmCompilerPcgGenerateSimpleNullCheck (CompilationUnitPCG *cUnit, CGInst inst)
{
    CGLabel notNull = CGCreateLabel ();
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", inst, "ne", zero, notNull, 100);

    dvmCompilerPcgGenerateRaiseExceptionSimple (cUnit);

    CGBindLabel (notNull);
}

void dvmCompilerPcgGenerateCalldvmJitToExceptionThrown (CompilationUnitPCG *cUnit)
{
    dvmCompilerPcgGenerateWritebacks (cUnit, cUnit->getCurrMod ());

    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

    //Get symbol to the dvmJitToExceptionThrown callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToExceptionThrown");

    //Paranoid
    assert (callback != 0);

    CGCreateNewInst ("call", "nl", callback, parms);
}

void dvmCompilerPcgGenerateNullCheck (CompilationUnitPCG *cUnit, CGInst base, MIR *mir, int ssaNum)
{
    //First check if the ME said to ignore it
    if ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) != 0)
    {
        if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
        {
            ALOGI ("    avoiding null check..\n");
        }
        return;
    }

    //Next if we have a ssaNum, look at the information
    if (ssaNum != -1)
    {
        SSANumInfo &info = cUnit->getSSANumInformation (ssaNum);

        if (info.checkedForNull == true)
        {
            if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
            {
                ALOGI ("    avoiding null check..\n");
            }
            return;
        }
    }

    //General case: generate the null check
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGLabel nullCheckPassedLabel = CGCreateLabel ();

    CGCreateNewInst ("cjcc", "rcrbp", base, "ne", zero, nullCheckPassedLabel, 100);

    // Save the current bytecode PC and jump to the exception block.
    dvmCompilerPcgGenerateRaiseException (cUnit);

    CGBindLabel (nullCheckPassedLabel);
}

/**
 * @brief Generate a speculative null check
 * @details
 * Null check done in entry block so that it's out of line. Here,
 * we're simply checking to see if we may run into a null pointer,
 * in which case we'll transfer control back to the interpreter
 * to handle.
 * @param cUnit the CompilationUnitPCG
 * @param base the base pointer
 */
static void dvmCompilerPcgGenerateSpeculativeNullCheck (CompilationUnitPCG *cUnit, CGInst base)
{
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGLabel nullCheckPassedLabel = CGCreateLabel ();

    CGCreateNewInst ("cjcc", "rcrbp", base, "ne", zero, nullCheckPassedLabel, 100);

    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

    //Get symbol to the dvmJitToInterpPunt callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpPunt");

    //Paranoid
    assert (callback != 0);

    dvmCompilerPcgCreateJsr (cUnit, callback, parms);

    CGBindLabel (nullCheckPassedLabel);
}

void dvmCompilerPcgGenerateRangeCheck (CompilationUnitPCG *cUnit, CGInst base, CGInst index, MIR *mir)
{
    if ( (mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK))
    {
        return;
    }

    CGLabel rangeCheckPassedLabel = CGCreateLabel ();

    CGInst length = dvmCompilerPcgCreateSimpleLoad (base, OFFSETOF_MEMBER (ArrayObject,length));

    CGCreateNewInst ("cjcc", "rcrbp", index, "ult", length, rangeCheckPassedLabel, 100);

    dvmCompilerPcgGenerateRaiseException (cUnit);
    CGBindLabel (rangeCheckPassedLabel);
}

void dvmCompilerPcgGenerateSpeculativeNullChecks (CompilationUnitPCG *cUnit)
{
    const char *opcode;
    int32_t size;
    CGTemp temp;
    u2 virtualReg;
    pcgDtype dtype;
    CGInst combinedNullCheck = CGInstInvalid;

    const std::set<int> &references = cUnit->getReferences ();

    for (std::set<int>::const_iterator it = references.begin (); it != references.end (); ++it)
    {
        temp = *it;
        virtualReg = dvmExtractSSARegister (cUnit, temp);
        dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, temp);

        SSANumInfo &info = cUnit->getSSANumInformation (temp);

        if ( (info.needsNullCheck == true) && (info.checkedForNull == false))
        {
            if (cUnit->checkDebugMask (DebugMaskSpeculative) == true)
            {
                ALOGI ("\n--------- generating speculative null check for SSA:%d.\n", temp);
            }

            int vrOffset = dvmCompilerPcgGetVROffsetRelativeToVMPtr (cUnit, virtualReg);
            CGAddr addr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid, vrOffset);

            size = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);
            if (cUnit->errorHandler->isAnyErrorSet () == true) {
                return;
            }

            void *handle = dvmCompilerPcgGetVRHandle (virtualReg, size);
            CGInst load = CGCreateNewInst (opcode, "m", addr, size, handle);
            CGAddTempDef (temp, load);

            if (combinedNullCheck == CGInstInvalid)
            {
                combinedNullCheck = load;
            }
            else
            {
                combinedNullCheck = CGCreateNewInst("or", "rr", combinedNullCheck, load);
            }

            info.checkedForNull = true;
        }
    }

    if (combinedNullCheck != CGInstInvalid)
    {
        dvmCompilerPcgGenerateSpeculativeNullCheck (cUnit, combinedNullCheck);
    }
}

uint8_t *dvmCompilerPcgEmitChainingCells (CompilationUnitPCG *cUnit,
        ChainCellCounts *pcgChainCellCounts,
        uint8_t *startAddr,
        uint8_t *cachePtr,
        size_t freeSpace)
{
    u4 padding, i;
    uint8_t *firstChainCellAddr;

    firstChainCellAddr = cachePtr;

    memset (pcgChainCellCounts, 0, sizeof (ChainCellCounts));

    GrowableList *chainingListByType = cUnit->getChainingList ();

    // First emit the code for the chaining cells.
    for (i = 0; i < kChainingCellGap; i++)
    {
        int *blockIdList = (int *)chainingListByType[i].elemList;
        cUnit->numChainingCells[i] = chainingListByType[i].numUsed;
        pcgChainCellCounts->u.count[i] = cUnit->numChainingCells[i];

        for (unsigned int j = 0; j < chainingListByType[i].numUsed; j++)
        {
            uint8_t *newCachePtr = 0;
            int blockId = blockIdList[j];
            BasicBlockPCG *chainingBlock = (BasicBlockPCG *) (dvmGrowableListGetElement (&cUnit->blockList, blockId));

            if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
            {
                ALOGI ("Starting translation for block %d\n", blockId);
                ALOGI ("---------------------------------\n");
            }
            switch (chainingBlock->blockType)
            {
                case kChainingCellNormal:
                    newCachePtr = dvmCompilerPcgEmitNormalChainingCell (
                            cUnit,
                            startAddr,
                            cUnit->method->insns + chainingBlock->startOffset,
                            chainingBlock,
                            cachePtr,
                            freeSpace);
                    break;

                case kChainingCellBackwardBranch:
                    newCachePtr = dvmCompilerPcgEmitBackwardBranchChainingCell (
                            cUnit,
                            chainingBlock,
                            startAddr,
                            cUnit->method->insns + chainingBlock->startOffset,
                            cachePtr,
                            freeSpace);
                    break;

                case kChainingCellInvokePredicted:
                    newCachePtr = dvmCompilerPcgEmitPredictedChainingCell (
                            cUnit,
                            chainingBlock->chainingCellSymbol,
                            cachePtr,
                            freeSpace);
                    break;

                case kChainingCellInvokeSingleton:
                    newCachePtr = dvmCompilerPcgEmitSingletonChainingCell (
                            cUnit,
                            startAddr,
                            chainingBlock->containingMethod->insns,
                            chainingBlock->chainingCellSymbol,
                            cachePtr,
                            freeSpace);
                    break;

                case kChainingCellHot:
                    newCachePtr = dvmCompilerPcgEmitHotChainingCell (
                            cUnit,
                            startAddr,
                            cUnit->method->insns + chainingBlock->startOffset,
                            chainingBlock->chainingCellSymbol,
                            cachePtr,
                            freeSpace);
                    break;

                default:
                    ALOGE ("\n+++ PCG ERROR +++ Unknown chaining block type seen : %d.", (int)chainingBlock->blockType);
                    cUnit->errorHandler->setError (kJitErrorPcgUnknownChainingBlockType);
                    assert(0);
                    return 0;
            }

            if (newCachePtr == 0)
            {
                // The code cache is full.  Return 0 to indicate this.
                cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
                return 0;
            }

            // If no code was emmitted for chaining cell we should reflect it in count
            if (newCachePtr == cachePtr)
            {
                if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
                {
                    ALOGI ("JIT_INFO: PCG: Translation for block %d skipped due to no-reference from code", blockId);
                }
                pcgChainCellCounts->u.count[i]--;
            }
            else
            {
                if (cUnit->printMe == true)
                {
                    // record the chaining cell block
                    std::pair<BBType, char*> code_blk_elem(chainingBlock->blockType, (char *)cachePtr);
                    cUnit->code_block_table->push_back(code_blk_elem);
                }

                freeSpace -= (newCachePtr - cachePtr);
                cachePtr = newCachePtr;
            }
        }
    }

    if (cUnit->checkDebugMask (DebugMaskDisasm) == true)
    {
        // Setting up the end of the trace (the mem constants and chain cell counts are separate)
        std::pair<BBType, char*> code_blk_elem(kExitBlock, (char *)cachePtr);
        cUnit->code_block_table->push_back(code_blk_elem);
    }

    // Dump section for chaining cell counts, make sure it is 4-byte aligned
    padding = (4 - ( (u4)cachePtr % 4)) % 4;

    // make sure there is enough space for the chaining cell counts and
    // padding.  Return 0 if not.
    if (freeSpace < padding + sizeof (ChainCellCounts))
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }
    freeSpace -= padding + sizeof (ChainCellCounts);
    cachePtr += padding;

    // Install the chaining information.  There are two u2 values that
    // immediately precede startAddr:
    // u2 chainingCellCountOffset: This is the offset from startAddr where
    //     the chaining cell count information is located.
    // u2 chainingCellCount: This is the offset from startAddr where the
    //     actual chaining cells are located.
    * ( (u2*)startAddr - 2) = cachePtr - startAddr;
    * ( (u2*)startAddr - 1) = (u2) (firstChainCellAddr - startAddr);

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("Routine header at 0x%p\n", (u2*)startAddr - 2);
        ALOGI ("    Chaining cell counts offset: 0x%04x\n",
                * ( (u2*)startAddr - 2));
        ALOGI ("    Chaining cells offset: 0x%04x\n",
                * ( (u2*)startAddr - 1));
    }

    memcpy (cachePtr, pcgChainCellCounts, sizeof (ChainCellCounts));
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("Chaining cell counts contents @0x%p\n   ", cachePtr);
        for (i = 0; i < sizeof (ChainCellCounts); i++)
        {
            ALOGI (" 0x%02x", cachePtr[i]);
        }
        ALOGI ("\n");
    }

    return cachePtr + sizeof (ChainCellCounts);
}

uint8_t* dvmCompilerPcgEmitSwitchTables(CompilationUnitPCG *cUnit,
        uint8_t *currCachePtr,
        size_t freeSpace)
{
    uint32_t * shiftedCachePtr = (uint32_t*)align ((char*)currCachePtr, 4);
    uint32_t num_shift_bytes = (uint32_t)shiftedCachePtr - (uint32_t)currCachePtr;
    uint32_t a = num_shift_bytes + cUnit->getNumberOfSwitchTableEntries () * 4;

    // Make sure we have enough room in the code cache for the switch table
    if (freeSpace < a) {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return 0;
    }

    // Trying to make this method-safe, i.e. by not limiting it strictly
    // to only one switch table
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList,
                                &iterator);

    while (true)
    {
        BasicBlockPCG *bb = (BasicBlockPCG *) dvmGrowableListIteratorNext(&iterator);

        if (bb == 0)
        {
             break;
        }

        // Skip all basic blocks that don't have switch statements
        if (bb->lastMIRInsn == 0 || bb->switchTableSymbol == CGSymbolInvalid)
        {
            continue;
        }

        if ((bb->lastMIRInsn->dalvikInsn.opcode == OP_PACKED_SWITCH ||
            bb->lastMIRInsn->dalvikInsn.opcode == OP_SPARSE_SWITCH) == false)
        {
            continue;
        }

        // Need four-byte alignment, to ensure safe patching
        currCachePtr = (uint8_t *) align ((char*)currCachePtr, 4);

        // So that the compiled switch instruction has a pointer to the switch
        // table, we need to bind the symbol to the code cache address
        dvmCompilerPcgBindSymbolAddress (cUnit, bb->switchTableSymbol, currCachePtr);

        // To set up the switch table, all we need to do is go through
        // switchChainingCellEntries and add the cache pointer to the
        // relocation, then add it to the relocation tracking system
        SwitchTableEntryIterator it;
        for (it = cUnit->switchTableBegin ();
             it != cUnit->switchTableEnd (); it++)
        {
            CRelocation *switchTableEntryRelocation = it->relocation;
            BasicBlockPCG *chainingCellBlock = it->chainingCellBB;

            // Now that we know where the symbol should live, bind it, and add the
            // finalized relocation to the tracking system
            dvmCompilerPcgBindSymbolAddress (cUnit, switchTableEntryRelocation->getSymbolInfo ()->cgSymbol, currCachePtr);
            cUnit->addRelocation(switchTableEntryRelocation);

            // And since the chaining cell locations have already been bound,
            // find that address, and put it here in the switch table entry
            * ( (const uint8_t**)currCachePtr) =
                (const uint8_t*) dvmCompilerPcgGetSymbolAddress (cUnit, chainingCellBlock->chainingCellSymbol);
            currCachePtr += 4;
        }
    }

    return currCachePtr;
}
