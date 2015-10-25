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
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "CompilerIR.h"
#include "Dataflow.h"
#include "LowerGetPut.h"
#include "LowerMemory.h"
#include "LowerOther.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "UtilityPCG.h"

// TODO: This function should be removed.  The bytecodes currently translated
// by this routine should instead use dvmCompilerPcgTranslateIgetIput.
void dvmCompilerPcgTranslateIput (CompilationUnitPCG *cUnit, MIR *mir)
{
    Opcode dalvikOpCode = mir->dalvikInsn.opcode;
    u2 fieldByteOffset = mir->dalvikInsn.vC;
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[0]);
    int32_t dtypeSize = (dalvikOpCode == OP_IPUT_WIDE_QUICK) ? 8 : 4;
    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, dtypeSize);
    const char *opcode;

    int baseN = (dalvikOpCode == OP_IPUT_WIDE_QUICK) ? 2 : 1;
    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);
    CGInst base = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[baseN], "mov", 4);

    dvmCompilerPcgGenerateNullCheck (cUnit, base, mir, ssaRep->uses[baseN]);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], opcode, dtypeSize);
    CGAddr addr = CGCreateAddr (base, CGInstInvalid, 0, CGSymbolInvalid, fieldByteOffset);
    CGCreateNewInst (opcode, "mr", addr, dtypeSize, (void*)1, A);

    if (dalvikOpCode == OP_IPUT_OBJECT_QUICK)
    {
        dvmCompilerPcgTranslateMarkCard (cUnit, A, base);
    }
}

// vA = vB[vC]
void dvmCompilerPcgTranslateAget (CompilationUnitPCG *cUnit, MIR *mir)
{
    Opcode dalvikOpCode = mir->dalvikInsn.opcode;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst base = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    int ssaNum = ssaRep->defs[0];
    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
    const char *opcode;
    int objectSize = (dalvikOpCode == OP_AGET_WIDE) ? 8 : 4;

    dtype = dvmCompilerPcgApplyDefaultDtype(dtype, objectSize);
    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    dvmCompilerPcgGenerateNullCheck (cUnit, base, mir, ssaRep->uses[0]);

    CGInst index = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);

    if ((mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK) == 0)
    {
        dvmCompilerPcgGenerateRangeCheck (cUnit, base, index, mir);
    }

    switch (mir->dalvikInsn.opcode)
    {
        case OP_AGET:
        case OP_AGET_OBJECT:
        case OP_AGET_WIDE:
            {
                CGAddr addr = CGCreateAddr (base, index, objectSize, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGInst objectLoad = CGCreateNewInst (opcode, "m", addr, objectSize, (void*)1);
                dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, opcode, objectSize, objectLoad);
            }
            break;
        case OP_AGET_BYTE:
            {
                CGAddr addr = CGCreateAddr (base, index, 1, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGInst objectLoad = CGCreateNewInst ("blds", "m", addr, 1, (void*)1);
                dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, "mov", 4, objectLoad);
            }
            break;
        case OP_AGET_BOOLEAN:
            {
                CGAddr addr = CGCreateAddr (base, index, 1, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGInst objectLoad = CGCreateNewInst ("bldz", "m", addr, 1, (void*)1);
                dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, "mov", 4, objectLoad);
            }
            break;
        case OP_AGET_CHAR:
            {
                CGAddr addr = CGCreateAddr (base, index, 2, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGInst objectLoad = CGCreateNewInst ("hldz", "m", addr, 2, (void*)1);
                dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, "mov", 4, objectLoad);
            }
            break;
        case OP_AGET_SHORT:
            {
                CGAddr addr = CGCreateAddr (base, index, 2, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGInst objectLoad = CGCreateNewInst ("hlds", "m", addr, 2, (void*)1);
                dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, "mov", 4, objectLoad);
            }
            break;
        default:
            cUnit->errorHandler->setError (kJitErrorPcgAgetUnknownType);
            break;
    }
}

// vB[vC] = vA
void dvmCompilerPcgTranslateAput (CompilationUnitPCG *cUnit, MIR *mir)
{
    Opcode dalvikOpCode = mir->dalvikInsn.opcode;

    int baseN = (dalvikOpCode == OP_APUT_WIDE) ? 2 : 1;
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst base = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[baseN], "mov", 4);
    int ssaNum = ssaRep->uses[0];
    const char *opcode;
    int objectSize = (dalvikOpCode == OP_APUT_WIDE) ? 8 : 4;
    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, objectSize);

    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    dvmCompilerPcgGenerateNullCheck (cUnit, base, mir, ssaRep->uses[baseN]);

    CGInst index = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[baseN + 1], "mov", 4);

    if ((mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK) == 0)
    {
        dvmCompilerPcgGenerateRangeCheck (cUnit, base, index, mir);
    }

    switch (dalvikOpCode)
    {
        case OP_APUT:
        case OP_APUT_WIDE:
            {
                CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum, opcode, objectSize);
                CGAddr addr = CGCreateAddr (base, index, objectSize, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGCreateNewInst (opcode, "mr", addr, objectSize, (void*)1, A);
            }
            break;
        case OP_APUT_BYTE:
        case OP_APUT_BOOLEAN:
            {
                CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
                CGAddr addr = CGCreateAddr (base, index, 1, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGCreateNewInst ("mov", "mr", addr, 1, (void*)1, A);
            }
            break;
        case OP_APUT_CHAR:
        case OP_APUT_SHORT:
            {
                CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
                CGAddr addr = CGCreateAddr (base, index, 2, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents));
                CGCreateNewInst ("mov", "mr", addr, 2, (void*)1, A);
            }
            break;
        default:
            cUnit->errorHandler->setError (kJitErrorPcgAputUnknownType);
            break;
    }
}

void dvmCompilerPcgTranslateAputObject (CompilationUnitPCG *cUnit, MIR *mir)
{
    CGLabel endLabel = CGCreateLabel ();
    CGLabel skipCheckLabel = CGCreateLabel ();
    CGLabel okLabel = CGCreateLabel ();

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst base = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);
    dvmCompilerPcgExportPC (cUnit);

    dvmCompilerPcgGenerateNullCheck(cUnit, base, mir, ssaRep->uses[1]);

    CGInst index = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "mov", 4);
    dvmCompilerPcgGenerateRangeCheck (cUnit, base, index, mir);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", A, "eq", zero, skipCheckLabel, 5);

    CGInst t0 = dvmCompilerPcgCreateSimpleLoad (A, OFFSETOF_MEMBER (Object,clazz));
    CGInst t1 = dvmCompilerPcgCreateSimpleLoad (base, OFFSETOF_MEMBER (Object,clazz));

    CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmCanPutArrayElement");
    CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
    CGInst parms[3];
    CGSetRreg (spIl, "esp");
    dvmCompilerPcgCreateSimpleStore (spIl, 0, t0);
    dvmCompilerPcgCreateSimpleStore (spIl, 4, t1);

    parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    parms[2] = CGInstInvalid;
    CGInst call = CGCreateNewInst ("icall", "nl", target, parms);
    CGSetRreg (call, "eax");
    spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
    CGSetRreg (spIl, "esp");
    call = CGCreateNewInst ("mov", "r", call);

    CGInst zero2 = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", call, "ne", zero2, okLabel, 100);
    dvmCompilerPcgGenerateRaiseException (cUnit);
    CGBindLabel (okLabel);

    dvmCompilerPcgCreateStore (base, index, 4, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents), A);
    dvmCompilerPcgTranslateMarkCardNotNull (cUnit, base);
    CGCreateNewInst ("jmp", "b", endLabel);

    CGBindLabel (skipCheckLabel);
    dvmCompilerPcgCreateStore (base, index, 4, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject,contents), A);

    CGBindLabel (endLabel);
}

bool dvmCompilerPcgTranslateSgetSput (CompilationUnitPCG *cUnit, MIR *mir, bool isGet, bool isObj, bool isWide, bool isVolatile)
{
    u2 referenceIndex = mir->dalvikInsn.vB;
    const Method *method = (mir->OptimizationFlags & MIR_CALLEE) ?  mir->meta.calleeMethod : cUnit->method;
    void *fieldPtr = (void*) (method->clazz->pDvmDex->pResFields[referenceIndex]);
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int ssaNum = (isGet) ? ssaRep->defs[0] : ssaRep->uses[0];
    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
    const char *opcode;
    int objectSize = (isWide) ? 8 : 4;
    CGInst parms[3];

    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, objectSize);
    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    // This comment is from LowerGetPut.cpp.  We need to figure out how to
    // handle this.  In general, having a mechanism for PCG to report a
    // failure is a nice improvement anyhow.
    //
    // Usually, fieldPtr should not be null. The interpreter should resolve
    // it before we come here, or not allow this opcode in a trace. However,
    // we can be in a loop trace and this opcode might have been picked up
    // by exhaustTrace. Sending a -1 here will terminate the loop formation
    // and fall back to normal trace, which will not have this opcode.
    //
    if (fieldPtr == 0)
    {
        return false;
    }

    CGInst fieldPtrInst = CGCreateNewInst ("mov", "i", (int32_t)fieldPtr);
    CGAddr addr = CGCreateAddr (fieldPtrInst, CGInstInvalid, 0, CGSymbolInvalid, OFFSETOF_MEMBER (StaticField,value));

    if (isGet == true)
    {
        if (isWide == true && isVolatile == true) {
            CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmQuasiAtomicRead64");
            CGInst add = CGCreateNewInst("lea", "d", addr);
            CGInst spIl = CGCreateNewInst("sub", "ri",
                                          CGGetStackPointerDef(), 4);
            CGSetRreg (spIl, "esp");
            dvmCompilerPcgCreateSimpleStore (spIl, 0, add);

            CGInst parms[3];
            parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
            parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
            parms[2] = CGInstInvalid;
            CGInst call = CGCreateNewInst("lcall", "nl", target, parms);
            CGSetRreg(call, "eax");
            spIl = CGCreateNewInst("add", "ri", CGGetStackPointerDef(), 4);
            CGSetRreg(spIl, "esp");
            call = CGCreateNewInst("lmov", "r", call);

            if (dtype == DPVXreg64) {
                call = CGCreateNewInst("emovdfi", "r", call);
                call = CGCreateNewInst("movsd2sd1", "r", call);
            }

            dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], opcode, 8, call);
        }
        else {
            const char *opDescr = (isVolatile == true) ? "v" : "m";
            CGInst load = CGCreateNewInst (opcode, opDescr, addr, objectSize, (void*)1);
            dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, opcode, objectSize, load);
        }
    }
    else
    {
        CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum, opcode, objectSize);
        if (isWide == true && isVolatile == true) {
            // Implement the 8-byte volatile put with a call to dvmQuasiAtomicSwap64.
            // The basic code sequence is:
            //     lea r1, <target address for the put>
            //     sub esp, 12
            //     movq [esp], <value to put>
            //     movl [esp + 8], r1
            //     call dvmQuasiAtomicSwap64
            //     add esp, 12
            //
            CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmQuasiAtomicSwap64");
            CGInst add = CGCreateNewInst("lea", "d", addr);
            CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 12);
            CGSetRreg (spIl, "esp");
            CGAddr argAddr = CGCreateAddr(spIl, CGInstInvalid, 0, CGSymbolInvalid, 0);
            CGCreateNewInst(opcode, "mr", argAddr, 8, (void*)1, A);
            dvmCompilerPcgCreateSimpleStore (spIl, 8, add);

            parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
            parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
            parms[2] = CGInstInvalid;

            CGCreateNewInst("call", "nl", target, parms);
            spIl = CGCreateNewInst("add", "ri", CGGetStackPointerDef(), 12);
            CGSetRreg(spIl, "esp");
        }
        else {
            if (isVolatile == true)
            {
                //xchg requires INTreg dtype
                CGInst typeCast = (dtype == VXreg32) ? CGCreateNewInst("emovdti", "r", A) : A;
                CGCreateNewInst ("xchg", "vr", addr, objectSize, (void*)1, typeCast);
            }
            else
            {
                CGCreateNewInst (opcode, "mr", addr, objectSize, (void*)1, A);
            }
            if (isObj == true)
            {
                CGInst clazzInst = dvmCompilerPcgCreateSimpleLoad (fieldPtrInst, OFFSETOF_MEMBER (Field,clazz));
                dvmCompilerPcgTranslateMarkCard (cUnit, A, clazzInst);
            }
        }
    }

    return true;
}

void dvmCompilerPcgTranslateIgetObjectQuick (CompilationUnitPCG *cUnit, MIR *mir)
{
    u2 fieldOffset = mir->dalvikInsn.vC;
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int ssaNum = ssaRep->defs[0];
    const char *opcode;
    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, 4);

    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    dvmCompilerPcgGenerateNullCheck (cUnit, B, mir, ssaRep->uses[0]);

    CGAddr addr = CGCreateAddr (B, CGInstInvalid, 0, CGSymbolInvalid, fieldOffset);
    CGInst objectLoad = CGCreateNewInst (opcode, "m", addr, 4, (void*)1);

    dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, opcode, 4, objectLoad);
}

void dvmCompilerPcgTranslateIgetWideQuick (CompilationUnitPCG *cUnit, MIR *mir)
{
    // Now that this routine has been revised to be ssaNum based, it can
    // probably be consolidated with other iget/iput routines.
    u2 fieldOffset = mir->dalvikInsn.vC;
    const char *opcode;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);
    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]);
    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, 8);
    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    dvmCompilerPcgGenerateNullCheck (cUnit, B, mir, ssaRep->uses[0]);

    // (TODO) The memory disambiguation information needs to be improved.
    CGAddr addr = CGCreateAddr (B, CGInstInvalid, 0, CGSymbolInvalid, fieldOffset);
    CGInst objectLoad = CGCreateNewInst (opcode, "m", addr, 8, (void*)1);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], opcode, 8, objectLoad);
}

void dvmCompilerPcgTranslateIgetIput (CompilationUnitPCG *cUnit, MIR *mir, bool isGet, bool isObj, bool isWide, bool isVolatile)
{
    u2 referenceIndex = mir->dalvikInsn.vC;
    const Method *method = (mir->OptimizationFlags & MIR_CALLEE) ?  mir->meta.calleeMethod : cUnit->method;
    InstField *pInstField = (InstField *)method->clazz->pDvmDex->pResFields[referenceIndex];
    int fieldOffset;
    CGInst parms[3];
    const char *opcode;
    int memSize;

    assert (pInstField != 0);
    fieldOffset = pInstField->byteOffset;

    int bIndex = (isGet) ? 0 : ( (isWide) ? 2 : 1);
    CGInst fieldOffsetInst = CGCreateNewInst ("mov", "i", fieldOffset);
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[bIndex], "mov", 4);
    dvmCompilerPcgGenerateNullCheck (cUnit, B, mir, ssaRep->uses[bIndex]);

    if (isGet == true)
    {
        pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]);
        dtype = dvmCompilerPcgApplyDefaultDtype (dtype, (isWide) ? 8 : 4);
        memSize = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

        if (isWide == true && isVolatile == true)
        {
            CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmQuasiAtomicRead64");
            CGInst add = CGCreateNewInst ("add", "rr", B, fieldOffsetInst);
            CGInst spIl = CGCreateNewInst ("sub", "ri",
                    CGGetStackPointerDef (), 16);
            CGSetRreg (spIl, "esp");
            dvmCompilerPcgCreateSimpleStore (spIl, 0, add);
            parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
            parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
            parms[2] = CGInstInvalid;
            CGInst call = CGCreateNewInst ("lcall", "nl", target, parms);
            CGSetRreg (call, "eax");
            spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
            CGSetRreg (spIl, "esp");
            call = CGCreateNewInst ("lmov", "r", call);

            if (dtype == DPVXreg64)
            {
                call = CGCreateNewInst ("emovdfi", "r", call);
                call = CGCreateNewInst ("movsd2sd1", "r", call);
            }

            dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], opcode, 8, call);
        }
        else
        {
            CGAddr addr = CGCreateAddr (B, fieldOffsetInst, 1, CGSymbolInvalid, 0);
            const char *opDescr = (isVolatile == true) ? "v" : "m";
            CGInst load = CGCreateNewInst (opcode, opDescr, addr, memSize, (void*)1);
            dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], opcode, memSize, load);
        }
    }
    else
    {
        pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[0]);
        dtype = dvmCompilerPcgApplyDefaultDtype (dtype, (isWide) ? 8 : 4);
        memSize = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

        CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], opcode, memSize);
        CGAddr addr = CGCreateAddr (B, fieldOffsetInst, 1, CGSymbolInvalid, 0);

        if (isWide == true && isVolatile == true) {
            CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmQuasiAtomicSwap64");
            CGInst add = CGCreateNewInst("lea", "d", addr);
            CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 12);
            CGSetRreg (spIl, "esp");
            CGAddr argAddr = CGCreateAddr(spIl, CGInstInvalid, 0, CGSymbolInvalid, 0);
            CGCreateNewInst(opcode, "mr", argAddr, 8, (void*)1, A);
            dvmCompilerPcgCreateSimpleStore (spIl, 8, add);

            parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
            parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
            parms[2] = CGInstInvalid;

            CGCreateNewInst("call", "nl", target, parms);
            spIl = CGCreateNewInst("add", "ri", CGGetStackPointerDef(), 12);
            CGSetRreg(spIl, "esp");
        }
        else {
            if (isVolatile == true)
            {
                //xchg requires INTreg dtype
                CGInst typeCast = (dtype == VXreg32) ? CGCreateNewInst("emovdti", "r", A) : A;
                CGCreateNewInst ("xchg", "vr", addr, memSize, (void*)1, typeCast);
            }
            else
            {
                CGCreateNewInst (opcode, "mr", addr, memSize, (void*)1, A);
            }
            if (isObj == true)
            {
                dvmCompilerPcgTranslateMarkCard (cUnit, A, B);
            }
        }
    }
}
