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

#include "BasicBlockPCG.h"
#include "ChainingCellException.h"
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "Labels.h"
#include "LowerCall.h"
#include "LowerJump.h"
#include "LowerMemory.h"
#include "LowerOther.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "Utility.h"
#include "UtilityPCG.h"

CGInst dvmCompilerPcgGenerateVMPtrMov (CompilationUnitPCG *cUnit)
{
    CGInst mov = CGCreateNewInst ("mov", "r", cUnit->getVMPtr ());
    CGSetRreg (mov, "edi");
    return mov;
}

CGInst dvmCompilerPcgGenerateFramePtrMov (const CompilationUnitPCG *cUnit)
{
    CGInst mov = CGCreateNewInst ("mov", "r", cUnit->getFramePtr ());
    CGSetRreg (mov, "ebp");
    return mov;
}

void dvmCompilerPcgTranslateMonitorExit (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    dvmCompilerPcgGenerateNullCheck (cUnit, A, mir, ssaRep->uses[0]);

    CGInst self = dvmCompilerPcgGetSelfPointer (cUnit);

    // call dvmUnlockObject, inputs: object reference and self
    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

    CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
    dvmCompilerPcgCreateSimpleStore (spIl, 0, self);
    dvmCompilerPcgCreateSimpleStore (spIl, 4, A);

    //Get symbol to the dvmJitToExceptionThrown callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmUnlockObject");

    //Paranoid
    assert (callback != 0);

    CGInst theCall = CGCreateNewInst ("icall", "nl", callback, parms);
    CGSetRreg (theCall, "eax");
    spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
    theCall = CGCreateNewInst ("mov", "r", theCall);

    CGLabel doneLabel = CGCreateLabel ();
    CGCreateNewInst ("cjcc", "rcrbp", theCall, "ne", CGCreateNewInst ("mov", "i", 0), doneLabel, 100);

    dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit); // ZZZ TODO : exception number = 2

    CGBindLabel (doneLabel);
}

void dvmCompilerPcgTranslateMonitorEnter (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgExportPC (cUnit);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    dvmCompilerPcgGenerateNullCheck (cUnit, A, mir, ssaRep->uses[0]);

    CGInst self = dvmCompilerPcgGetSelfPointer (cUnit);

    // call dvmLockObject, inputs: object reference and self
    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

    CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
    dvmCompilerPcgCreateSimpleStore (spIl, 0, self);
    dvmCompilerPcgCreateSimpleStore (spIl, 4, A);

    //Get symbol to the dvmJitToExceptionThrown callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmLockObject");

    //Paranoid
    assert (callback != 0);

    CGCreateNewInst ("call", "nl", callback, parms);

    spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
}

void dvmCompilerPcgTranslateMarkCard (const CompilationUnitPCG *cUnit, CGInst val, CGInst targetAddr)
{
    CGInst self = dvmCompilerPcgGetSelfPointer (cUnit);
    CGInst cardTable = dvmCompilerPcgCreateSimpleLoad (self, offsetof (Thread, cardTable));
#ifdef WITH_CONDMARK
    CGInst cardImmuneLimit = dvmCompilerPcgCreateSimpleLoad (self, offsetof (Thread, cardImmuneLimit));
#endif
    CGLabel skipMarkCard = CGCreateLabel ();

    CGCreateNewInst ("cjcc", "rcrb", val, "eq", CGCreateNewInst ("mov", "i", 0), skipMarkCard);
#ifdef WITH_CONDMARK
    CGCreateNewInst ("cjcc", "rcrbp", cardImmuneLimit, "ule", targetAddr, skipMarkCard, 95);
#endif

    targetAddr = CGCreateNewInst ("shr", "ri", targetAddr, GC_CARD_SHIFT);

    // store cardTable in (cardTable, targetAddr, 1)
    CGAddr addr = CGCreateAddr (cardTable, targetAddr, 1, CGSymbolInvalid, 0);
    CGCreateNewInst ("mov", "mr", addr, 1, (void*)1, cardTable);

    CGBindLabel (skipMarkCard);
}

void dvmCompilerPcgTranslateMarkCardNotNull (const CompilationUnitPCG *cUnit, CGInst targetAddr)
{
    CGInst self = dvmCompilerPcgGetSelfPointer (cUnit);
    CGInst cardTable = dvmCompilerPcgCreateSimpleLoad (self, offsetof (Thread, cardTable));
#ifdef WITH_CONDMARK
    CGInst cardImmuneLimit = dvmCompilerPcgCreateSimpleLoad (self, offsetof (Thread, cardImmuneLimit));
    CGLabel skipMarkCard = CGCreateLabel ();

    CGCreateNewInst ("cjcc", "rcrbp", cardImmuneLimit, "ule", targetAddr, skipMarkCard, 95);
#endif

    targetAddr = CGCreateNewInst ("shr", "ri", targetAddr, GC_CARD_SHIFT);

    // store cardTable in (cardTable, targetAddr, 1)
    CGAddr addr = CGCreateAddr (cardTable, targetAddr, 1, CGSymbolInvalid, 0);
    CGCreateNewInst ("mov", "mr", addr, 1, (void*)1, cardTable);

#ifdef WITH_CONDMARK
    CGBindLabel (skipMarkCard);
#endif
}

/**
 * @brief Handle the common side of the check/cast/instance of instruction
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param tmp ...
 * @param isInstanceOf is it an instance of?
 */
//TODO Find out what tmp is and change its name..
void dvmCompilerPcgCommonCheckCastInstanceOf (CompilationUnitPCG *cUnit, MIR *mir, u4 tmp, bool isInstanceOf)
{
    CGLabel nullLabel;
    CGLabel equalLabel;
    CGLabel endLabel = CGCreateLabel ();
    ClassObject *classPtr = cUnit->method->clazz->pDvmDex->pResClasses[tmp];
    CGTemp resultTemp = cUnit->getCurrentTemporaryVR (true);
    CGInst classPtrInst;
    CGInst call, callResult;
    CGInst parms[4];

    if (isInstanceOf == true)
    {
        nullLabel = CGCreateLabel ();
        equalLabel = CGCreateLabel ();
    }
    else
    {
        // Check cast effectively is finished when it jumps to these labels.
        // So just jump directly to endLabel;
        nullLabel = endLabel;
        equalLabel = endLabel;
    }

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", A, "eq", zero, nullLabel, 0);

    if (classPtr != 0)
    {
        classPtrInst = CGCreateNewInst ("mov", "i", (int32_t)classPtr);
    }
    else
    {
        // The class needs to be resolved.
        classPtrInst = dvmCompilerPcgResolveClass(cUnit, tmp);
    }

    CGInst clazzInst = dvmCompilerPcgCreateSimpleLoad (A, OFFSETOF_MEMBER (Object,clazz));
    CGCreateNewInst ("cjcc", "rcrbp", clazzInst, "eq", classPtrInst, equalLabel, 50);

    CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
    dvmCompilerPcgCreateSimpleStore (spIl, 0, clazzInst);
    dvmCompilerPcgCreateSimpleStore (spIl, 4, classPtrInst);
    parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    parms[2] = CGInstInvalid;

    //Get symbol to the dvmJitToExceptionThrown callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmInstanceofNonTrivial");

    //Paranoid
    assert (callback != 0);

    call = CGCreateNewInst ("icall", "nl", callback, parms);

    CGSetRreg (call, "eax");
    spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
    callResult = CGCreateNewInst ("mov", "r", call);

    if (isInstanceOf == true)
    {
        CGAddTempDef (resultTemp, callResult);
        CGCreateNewInst ("jmp", "b", endLabel);

        CGBindLabel (nullLabel);
        CGInst zeroResult = CGCreateNewInst ("mov", "i", 0);
        CGAddTempDef (resultTemp, zeroResult);
        CGCreateNewInst ("jmp", "b", endLabel);

        CGBindLabel (equalLabel);
        CGInst oneResult = CGCreateNewInst ("mov", "i", 1);
        CGAddTempDef (resultTemp, oneResult);
    }
    else
    {
        CGCreateNewInst ("cjcc", "rcrbp", callResult, "ne", CGCreateNewInst ("mov", "i", 0), endLabel, 95);
        dvmCompilerPcgGenerateRaiseException (cUnit);
    }

    CGBindLabel (endLabel);
    if (isInstanceOf == true)
    {
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));
    }
}

/**
 * @brief Translate the instanceOf bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateInstanceOf (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgCommonCheckCastInstanceOf (cUnit, mir, mir->dalvikInsn.vC, true);
}

/**
 * @brief Translate the check cast bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateCheckCast (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgCommonCheckCastInstanceOf (cUnit, mir, mir->dalvikInsn.vB, false);
}

/**
 * @brief Translate the new instance bytecode
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
void dvmCompilerPcgTranslateNewInstance (CompilationUnitPCG *cUnit, MIR *mir)
{
    u4 tmp = mir->dalvikInsn.vB;
    ClassObject *classPtr = cUnit->method->clazz->pDvmDex->pResClasses[tmp];

    // These assertions were copied from LowerObject.cpp
    assert (classPtr != 0);
    assert (classPtr->status & CLASS_INITIALIZED);

    // If it is going to throw, it should not make to the trace to begin
    // with.  However, Alloc might throw, so we need to genExportPC ()
    assert ( (classPtr->accessFlags & (ACC_INTERFACE|ACC_ABSTRACT)) == 0);

    dvmCompilerPcgExportPC (cUnit);

    // We are calling dvmAllocObject (classPtr, ALLOC_DONT_TRACK)
    // The original code only subtracted 8 from ESP, but we really ought to
    // do our part to keep the stack aligned.
    //
    CGInst classPtrIl = CGCreateNewInst ("mov", "i", classPtr);
    CGInst dontTrackIl = CGCreateNewInst ("mov", "i", ALLOC_DONT_TRACK);
    CGInst callResult = dvmCompilerPcgGenerateX86Call (cUnit, "dvmAllocObject", INTreg, 2, INTreg, classPtrIl, INTreg, dontTrackIl);

    // Test for null.
    CGLabel doneLabel = CGCreateLabel ();
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrb", callResult, "ne", zero, doneLabel);

    dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGBindLabel (doneLabel);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, callResult);
}

/**
 * @brief Create a jump instruction by using the switch table
 * @param cUnit the CompilationUnitPCG
 * @param pSwTblInst A CGInst referring to the packed switch table address
 * @param tableIndexInst A CGInst referring to the table index load/calculation
 */
static void dvmCompilerPcgCreateJumpToPackedSwitchEntry (
    CompilationUnitPCG * cUnit,
    CGInst pSwTblInst,
    CGInst tableIndexInst)
{
    // Create the jumpTargetAddr, which is the indexed entry
    // in the switch table
    CGAddr jumpTargetAddr = CGCreateAddr (pSwTblInst, tableIndexInst,
        sizeof(void*), CGSymbolInvalid, 0);
    CGInst jumpTarget = CGCreateNewInst ("mov", "m", jumpTargetAddr,
        sizeof(void*), (void*)1);

    // Create an indirect jump to the chaining cell or the
    // chained address (in eax)
    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};
    dvmCompilerPcgCreateJsr (cUnit, CGSymbolInvalid, parms, jumpTarget);
}

/**
 * @brief Punt back to the interpreter, because the switch has too many cases to chain
 * @param cUnit the CompilationUnitPCG
 * @param entries The actual data entries in the dex file for the switch
 * @param tableIndexInst A CGInst referring to the table index load/calculation
 */
static void dvmCompilerPcgCreateSwitchPunt (
    CompilationUnitPCG * cUnit,
    const s4* entries,
    CGInst tableIndexInst)
{
    // Need to compute the correct rPC and call dvmJitToInterpNoChain
    CGInst entriesInst = CGCreateNewInst ("mov", "i", entries);
    CGAddr entriesAddr = CGCreateAddr (entriesInst, tableIndexInst,
        sizeof(size_t), CGSymbolInvalid, 0);
    CGInst entryInst = CGCreateNewInst ("mov", "m",
        entriesAddr, sizeof(size_t), (void*)1);
    CGInst shiftedEntryInst = CGCreateNewInst ("imul", "ri", entryInst, 2);

    // Add the calculated offset to the rPC and jump to the interpreter
    CGInst rPCInst = CGCreateNewInst ("mov", "i", rPC);
    CGInst newRPCInst = CGCreateNewInst ("add", "rr",
        rPCInst, shiftedEntryInst);

    CGInst rPCInEaxInst = CGCreateNewInst ("mov", "r", newRPCInst);
    CGSetRreg (rPCInEaxInst, "eax");

    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst noChainParms[4] = {parmEdi, parmEbp, rPCInEaxInst, CGInstInvalid};

    //Get symbol to the dvmJitToInterpNoChain callback
    CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpNoChain");

    //Paranoid
    assert (callback != 0);

#if defined(WITH_JIT_TUNING)
    dvmCompilerPcgCreateJsrWithKSwitchOverflow (cUnit, callback, noChainParms);
#else
    dvmCompilerPcgCreateJsr (cUnit, callback, noChainParms);
#endif
}

void dvmCompilerPcgTranslatePackedSwitch (CompilationUnitPCG *cUnit, MIR *mir)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    // Each switch instruction needs a switch table, so create a
    // symbol we can use to refer to the switch table (which
    // will be created later)
    std::string blockName;
    dvmCompilerPcgGetBlockName (bb, blockName);
    std::string switchTableName = blockName + "_switch";
    bb->switchTableSymbol = dvmCompilerPcgCreateSymbol (cUnit, switchTableName, 0, false);

    //Paranoid
    assert (bb != 0);

    u4 tmp = mir->dalvikInsn.vB;
    u2* switchData = const_cast<u2 *> (rPC) + (s4)tmp;

    if (*switchData != kPackedSwitchSignature)
    {
        // should have been caught by verifier
        dvmThrowInternalError ("bad packed switch magic");
        return;
    }

    // Advance the pointer
    switchData++;

    u2 tSize = *switchData;
    // Advance the pointer
    switchData++;
    assert (tSize > 0);
    s4 firstKey = * ( (s4*)switchData);
    switchData += 2;
    s4* entries = (s4*) switchData;
    assert ( ( (u4)entries & 0x3) == 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst pSwTblInst = CGCreateNewInst ("movltc", "n", bb->switchTableSymbol);
    CGInst firstKeyInst = CGCreateNewInst ("mov", "i", firstKey);
    CGInst tsizeInst = CGCreateNewInst ("mov", "i", (int32_t)tSize);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);

    // The table index is A - firstKeyValue
    CGInst tableIndexInst = CGCreateNewInst ("sub", "rr", A, firstKeyInst);

    CGLabel switchDefault = CGCreateLabel ();
    CGLabel switchNoChain = CGLabelInvalid;
    CGCreateNewInst ("cjcc", "rcrbp", tableIndexInst, "sge", tsizeInst,
        switchDefault, 0);
    CGCreateNewInst ("cjcc", "rcrbp", tableIndexInst, "slt", zero,
        switchDefault, 0);

    if (tSize > MAX_CHAINED_SWITCH_CASES) {
        // The big switch case handles MAX_CHAINED_SWITCH_CASES
        // cases of the switch data, and the default case
        switchNoChain = CGCreateLabel ();

        CGCreateNewInst ("cjcc", "rcrbp", tableIndexInst, "sge",
            CGCreateNewInst ("mov", "i", MAX_CHAINED_SWITCH_CASES),
            switchNoChain, 0);
    }

    // The fallthrough path is when the index is in the switch table
    // so simply use the switch table to generate the jumpTarget
    dvmCompilerPcgCreateJumpToPackedSwitchEntry (cUnit, pSwTblInst,
        tableIndexInst);

    // Now we handle the default case
    CGBindLabel (switchDefault);
    int maxChains = MIN(tSize, MAX_CHAINED_SWITCH_CASES);
    CGInst maxChainsInst = CGCreateNewInst ("mov", "i", maxChains);

    dvmCompilerPcgCreateJumpToPackedSwitchEntry (cUnit, pSwTblInst,
        maxChainsInst);

    if (tSize > MAX_CHAINED_SWITCH_CASES)
    {
        // Now we handle the punt to interpreter case
        CGBindLabel (switchNoChain);

        dvmCompilerPcgCreateSwitchPunt (cUnit, entries, tableIndexInst);
    }
}

void dvmCompilerPcgTranslateSparseSwitch (CompilationUnitPCG *cUnit, MIR *mir)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    // Each switch instruction needs a switch table, so create a
    // symbol we can use to refer to the switch table (which
    // will be created later)
    std::string blockName;
    dvmCompilerPcgGetBlockName (bb, blockName);
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s_switch",
        blockName.c_str());
    std::string switchTableName(buffer);
    bb->switchTableSymbol = dvmCompilerPcgCreateSymbol (cUnit, switchTableName, 0, false);

    //Paranoid
    assert (bb != 0);

    u4 tmp = mir->dalvikInsn.vB;
    u2* switchData = const_cast<u2 *> (rPC) + (s4)tmp;

    if (*switchData != kSparseSwitchSignature)
    {
        // should have been caught by verifier
        dvmThrowInternalError ("bad sparse switch magic");
        return;
    }

    // Advance the pointer
    switchData++;

    u2 tSize = *switchData;
    // Advance the pointer
    switchData++;
    assert (tSize > 0);
    const s4* keys = (const s4*) switchData;
    const s4* entries = keys + tSize;
    assert(((u4)keys & 0x3) == 0);
    assert((((u4) ((s4*) switchData + tSize)) & 0x3) == 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst pSwTblInst = CGCreateNewInst ("movltc", "n", bb->switchTableSymbol);
    CGInst keysInst = CGCreateNewInst ("mov", "i", keys);
    CGInst tsizeInst = CGCreateNewInst ("mov", "i", (int32_t)tSize);

    // normal switch case
    if (tSize <= MAX_CHAINED_SWITCH_CASES) {
        // The normal switch case just calls the function
        // dvmJitHandleSparseSwitch with the switch key and the pointer
        // to the switch table at the end of the trace.

        // jumpTarget will be either the start of the
        // chaining cell or the chained address
        CGInst jumpTarget = dvmCompilerPcgGenerateX86Call (cUnit, "dvmJitHandleSparseSwitch", INTreg, 4,
                INTreg, pSwTblInst,
                INTreg, keysInst,
                INTreg, tsizeInst,
                INTreg, A);

        CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
        CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
        CGInst parms[4] = {jumpTarget, parmEdi, parmEbp, CGInstInvalid};

        // Create an indirect jump to the chaining cell or the
        // chained address (in eax)
        dvmCompilerPcgCreateJsr (cUnit, CGSymbolInvalid, parms, jumpTarget);

    } else {

        // The big switch case handles MAX_CHAINED_SWITCH_CASES
        // cases of the switch data, and the default case

        // tableIndexInst will be the index in the full switch table (in the
        // dex file) which matches the switch key
        CGInst tableIndexInst = dvmCompilerPcgGenerateX86Call (cUnit,
                "dvmJitLookUpBigSparseSwitch", INTreg, 3,
                INTreg, keysInst,
                INTreg, tsizeInst,
                INTreg, A);

        CGLabel switchDefault = CGCreateLabel ();
        CGLabel switchNoChain = CGCreateLabel ();
        CGCreateNewInst ("cjcc", "rcrbp", tableIndexInst, "sge", tsizeInst,
            switchDefault, 0);
        CGCreateNewInst ("cjcc", "rcrbp", tableIndexInst, "sge",
            CGCreateNewInst ("mov", "i", MAX_CHAINED_SWITCH_CASES),
            switchNoChain, 0);

        // The fallthrough path is when the index is in the switch table
        // so simply index the switch table and jump to that address
        dvmCompilerPcgCreateJumpToPackedSwitchEntry (cUnit, pSwTblInst,
            tableIndexInst);

        // Now we handle the default case
        CGBindLabel (switchDefault);
        int maxChains = MIN(tSize, MAX_CHAINED_SWITCH_CASES);
        CGInst maxChainsInst = CGCreateNewInst ("mov", "i", maxChains);

        dvmCompilerPcgCreateJumpToPackedSwitchEntry (cUnit, pSwTblInst,
            maxChainsInst);

        // Now we handle the punt to interpreter case
        CGBindLabel (switchNoChain);

        dvmCompilerPcgCreateSwitchPunt (cUnit, entries, tableIndexInst);
    }
}

void dvmCompilerPcgAddVRInterfaceCode (CompilationUnitPCG *cUnit)
{
    if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        ALOGI ("    BEGIN Generating Entry Loads.\n    =============================\n");
    }
    // First insert loads at the entry to the trace.
    CGSetCurrentInsertionPoint (cUnit->getEntryInsertionPoint ());

    const std::set<int> &references = cUnit->getReferences ();
    for (std::set<int>::const_iterator it = references.begin (); it != references.end (); ++it)
    {
        //Get a local version
        int ssaNum = *it;

        //Now call the initial load helper
        dvmCompilerPcgHandleInitialLoad (cUnit, 0, ssaNum, true);
    }

    if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        ALOGI ("    END Generating Entry Loads.\n"
                "    ===========================\n");
    }
}
