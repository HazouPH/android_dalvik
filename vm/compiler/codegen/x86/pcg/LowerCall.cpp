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
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "LowerCall.h"
#include "LowerCall.h"
#include "LowerJump.h"
#include "LowerMemory.h"
#include "LowerOther.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "UtilityPCG.h"
#include "Utility.h"

CGSymbol dvmCompilerPcgGetInvokeTarget (CompilationUnitPCG *cUnit, const BasicBlockPCG *bb, bool *needsCfgArc)
{
    CGSymbol sym;
    BBType blockType = bb->blockType;

    if (needsCfgArc != 0)
    {
        *needsCfgArc = true;
    }

    if (blockType < kChainingCellLast)
    {
        sym = bb->chainingCellSymbol;

        if (needsCfgArc != 0)
        {
            *needsCfgArc = false;
        }
    }
    else
    {
        sym = cUnit->getBlockSymbol (bb->cgLabel);
    }

    return sym;
}

/**
 * @brief Generate the predicted chaining cell
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlockPCG
 * @param tmp
 * @param IMMC the immediate constant
 * @param isInterface is it for an interface
 * @param inputReg the input register (the this pointer)
 * @param mir the MIR instruction
 */
 //TODO Find out what tmp is...
static void generatePredictedChain (CompilationUnitPCG *cUnit, BasicBlockPCG *bb, u2 tmp, int IMMC, bool isInterface, CGInst inputReg, MIR *mir)
{
    int traceTakenId = bb->taken ? bb->taken->id : 0;

    //Paranoid
    assert (traceTakenId != 0);

    BasicBlockPCG *target = cUnit->getBasicBlockPCG (traceTakenId);

    //Paranoid check
    if (target == 0)
    {
        //For the moment just make it fail with the generic error
        cUnit->errorHandler->setError (kJitErrorPcgCodegen);

        //Just return because this is already a bad enough situation
        return;
    }

    CGInst takenTarget = CGCreateNewInst ("movltc", "n", dvmCompilerPcgGetInvokeTarget (cUnit, target));

    // Be careful here, we must ensure that clazz is loaded first
    // It is required by Predicted Chaining logic, please see inlineCachePatchEnqueue for details
    // In short inlineCachePatchEnqueue updates method and then clazz. To avoid race condition
    // we must read the clazz first and if it is a valid we can read method, otherwise we are risky
    // to read invalid method while clazz will be valid.
    // To acheive this with pcg we use volatile semantic
    CGAddr predictedClazzAddr = CGCreateAddr (takenTarget, CGInstInvalid, 0, CGSymbolInvalid, OFFSETOF_MEMBER (PredictedChainingCell, clazz));
    CGInst predictedClazz = CGCreateNewInst ("mov", "v", predictedClazzAddr, 4, (void*)1);

    CGAddr predictedMethodAddr = CGCreateAddr (takenTarget, CGInstInvalid, 0, CGSymbolInvalid, OFFSETOF_MEMBER (PredictedChainingCell, method));
    CGInst predictedMethod = CGCreateNewInst ("mov", "v", predictedMethodAddr, 4, (void*)1);

    // Compare current class object against predicted clazz
    // if equal, prediction is still valid, jump to .invokeChain
    CGLabel invokeChainLabel = CGCreateLabel ();

    // we need to coalesce the fallthrough symbol instruction, because it gets patched in the hot cc
    CGInst fallthroughTargetSymbInst = CGInstInvalid;

    // get the fallthrough target instruction
    fallthroughTargetSymbInst = dvmCompilerPcgGetFallthroughTargetSymbolInst (cUnit, bb);

    // get thisPtr->clazz
    CGInst clazz = dvmCompilerPcgCreateSimpleLoad (inputReg, OFFSETOF_MEMBER (Object,clazz));

    CGCreateNewInst ("cjcc", "rcrb", clazz, "eq", predictedClazz, invokeChainLabel);

    //Increment the next temporary
    //TODO: ask why we do it first?
    cUnit->getCurrentTemporaryVR (true);
    CGTemp methodTemp = cUnit->getCurrentTemporaryVR ();

    // get callee method and update predicted method if necessary
    if (isInterface == true)
    {
        // set up arguments to dvmFindInterfaceMethodInCache
        // ESP = ESP - 12
        CGInst spIl =
            CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
        CGSetRreg (spIl, "esp");

        CGInst immIl;
        dvmCompilerPcgCreateSimpleStore (spIl, 0, clazz);

        immIl = CGCreateNewInst ("mov", "i", tmp);
        dvmCompilerPcgCreateSimpleStore (spIl, 4, immIl);

        immIl = CGCreateNewInst ("mov", "i", (int) (cUnit->method));
        dvmCompilerPcgCreateSimpleStore (spIl, 8, immIl);

        immIl = CGCreateNewInst ("mov", "i", (int) (cUnit->method->clazz->pDvmDex));
        dvmCompilerPcgCreateSimpleStore (spIl, 12, immIl);

        CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
        CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
        CGInst parms_2[3] = {parmEdi, parmEbp, CGInstInvalid};
        CGInst theCall = CGCreateNewInst ("icall", "nl",
                singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmFindInterfaceMethodInCache"),
                parms_2);
        CGSetRreg (theCall, "eax");

        spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
        CGSetRreg (spIl, "esp");

        CGInst retVal = CGCreateNewInst ("mov", "r", theCall);
        CGAddTempDef(methodTemp, retVal);

        // if dvmFindInterfaceMethodInCache returns 0, throw exception
        // otherwise, jump to .findInterfaceDone
        CGLabel findInterfaceDoneLabel = CGCreateLabel ();
        CGInst zero = CGCreateNewInst ("mov", "i", 0);
        CGCreateNewInst ("cjcc", "rcrb", retVal, "ne", zero, findInterfaceDoneLabel);

        dvmCompilerPcgExportPC (cUnit);
        dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit);

        CGBindLabel (findInterfaceDoneLabel);

        takenTarget = CGCreateNewInst ("movltc", "n", dvmCompilerPcgGetInvokeTarget (cUnit, target));

        CGInst chainingCell = dvmCompilerPcgCreateSimpleLoad (takenTarget, OFFSETOF_MEMBER (PredictedChainingCell, clazz));
        zero = CGCreateNewInst ("mov", "i", 0);
        CGInst selfPointer = dvmCompilerPcgGetSelfPointer (cUnit);
        CGInst rechainCountIl = dvmCompilerPcgCreateSimpleLoad (selfPointer, offsetof (Thread, icRechainCount));
        CGInst countMinusOne = CGCreateNewInst ("sub", "ri", rechainCountIl, 1);

        CGInst select_1 = CGCreateNewInst ("islcc", "rcrrr", zero, "ne", chainingCell, countMinusOne, zero);
        CGInst select_2 = CGCreateNewInst ("islcc", "rcrrr", zero, "ne", chainingCell, countMinusOne, rechainCountIl);

        dvmCompilerPcgCreateSimpleStore (selfPointer, offsetof (Thread, icRechainCount), select_2);

        CGLabel skipPredictionLabel = CGCreateLabel ();
        zero = CGCreateNewInst ("mov", "i", 0);
        CGCreateNewInst ("cjcc", "rcrb", select_1, "sgt", zero, skipPredictionLabel);

        // call dvmJitToPatchPredictedChain to update predicted method.
        // set up arguments for dvmJitToPatchPredictedChain.
        //
        // ESP = ESP - 16
        //
        // get thisPtr->clazz
        spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
        CGSetRreg (spIl, "esp");
        dvmCompilerPcgCreateSimpleStore (spIl, 0, retVal);
        dvmCompilerPcgCreateSimpleStore (spIl, 4, selfPointer);
        dvmCompilerPcgCreateSimpleStore (spIl, 8, takenTarget);
        dvmCompilerPcgCreateSimpleStore (spIl, 12, clazz);
        parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
        parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
        CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

        //Get call back
        //Get symbol to the dvmJitToPatchPredictedChain callback
        CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToPatchPredictedChain");

        //Paranoid
        assert (callback != 0);

        theCall = CGCreateNewInst ("icall", "nl", callback, parms);
        CGSetRreg (theCall, "eax");

        // ESP = ESP + 16
        spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
        CGSetRreg (spIl, "esp");

        // callee method in %ecx for invoke virtual
        retVal = CGCreateNewInst ("mov", "r", theCall);
        CGAddTempDef (methodTemp, retVal);

        CGBindLabel (skipPredictionLabel);

        // now create the trampoline to get to the singleton chaining cell
        dvmCompilerPcgCommonInvokeMethodJmp (cUnit, mir, ArgsDone_Full, CGGetTempUseInst (methodTemp), fallthroughTargetSymbInst);
    }
    else
    {
        // predictedChainVirtual_O1 (IMMC);
        CGInst selfPointer = dvmCompilerPcgGetSelfPointer (cUnit);
        CGInst zero = CGCreateNewInst ("mov", "i", 0);
        CGInst rechainCount = dvmCompilerPcgCreateSimpleLoad (selfPointer, offsetof (Thread, icRechainCount));
        CGInst rechainCountCopy = CGCreateNewInst ("mov", "r", rechainCount);
        rechainCount = CGCreateNewInst ("sub", "ri", rechainCount, 1);

        CGInst select_1 = CGCreateNewInst ("islcc", "rcrrr", zero, "ne", predictedClazz, rechainCount, zero);
        CGInst select_2 = CGCreateNewInst ("islcc", "rcrrr", zero, "ne", predictedClazz, rechainCount, rechainCountCopy);

        // get thisPtr->clazz
        CGInst clazz = dvmCompilerPcgCreateSimpleLoad (inputReg, OFFSETOF_MEMBER (Object,clazz));
        CGInst vtable = dvmCompilerPcgCreateSimpleLoad (clazz, OFFSETOF_MEMBER (ClassObject, vtable));

        CGInst immc  = dvmCompilerPcgCreateSimpleLoad (vtable, IMMC);
        CGAddTempDef (methodTemp, immc);

        dvmCompilerPcgCreateSimpleStore (selfPointer, offsetof (Thread, icRechainCount), select_2);

        CGLabel skipPredictionLabel = CGCreateLabel ();
        CGCreateNewInst ("cjcc", "rcrb", select_1, "sgt", zero, skipPredictionLabel);

        // call dvmJitToPatchPredictedChain to update predicted method.
        // set up arguments for dvmJitToPatchPredictedChain.
        //
        // ESP = ESP - 16

        CGInst takenTarget = CGCreateNewInst ("movltc", "n", dvmCompilerPcgGetInvokeTarget (cUnit, target));

        CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 16);
        CGSetRreg (spIl, "esp");
        dvmCompilerPcgCreateSimpleStore (spIl, 0, immc);
        dvmCompilerPcgCreateSimpleStore (spIl, 4, selfPointer);
        dvmCompilerPcgCreateSimpleStore (spIl, 8, takenTarget);
        dvmCompilerPcgCreateSimpleStore (spIl, 12, clazz);
        CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
        CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
        CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

        //Get symbol to the dvmJitToPatchPredictedChain callback
        CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToPatchPredictedChain");

        //Paranoid
        assert (callback != 0);

        CGInst theCall = CGCreateNewInst ("icall", "nl", callback, parms);

        CGSetRreg (theCall, "eax");

        // ESP = ESP + 16
        spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 16);
        CGSetRreg (spIl, "esp");

        // callee method in %ecx for invoke virtual
        CGInst retVal = CGCreateNewInst ("mov", "r", theCall);

        CGAddTempDef (methodTemp, retVal);

        CGBindLabel (skipPredictionLabel);

        // now create the trampoline to get to the singleton chaining cell
        dvmCompilerPcgCommonInvokeMethodJmp (cUnit, mir, ArgsDone_Full, CGGetTempUseInst (methodTemp), fallthroughTargetSymbInst);
    }

    CGBindLabel (invokeChainLabel);

    takenTarget = CGCreateNewInst ("movltc", "n", dvmCompilerPcgGetInvokeTarget (cUnit, target));

    dvmCompilerPcgCommonInvokeMethodJmp (cUnit, mir, ArgsDone_Normal, predictedMethod, fallthroughTargetSymbInst);
}

void dvmCompilerPcgTranslateInvokeVirtual (CompilationUnitPCG *cUnit, MIR *mir)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    if (mir->OptimizationFlags & MIR_INLINED)
    {
        return;
    }

    dvmCompilerPcgExportPC (cUnit);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    dvmCompilerPcgGenerateSimpleNullCheck (cUnit, C);

    Opcode dalvikOpCode = mir->dalvikInsn.opcode;

    if (dalvikOpCode == OP_INVOKE_VIRTUAL_QUICK || dalvikOpCode == OP_INVOKE_VIRTUAL_QUICK_RANGE)
    {
        // constVREndOfBB
        // beforeCall
        // ZZZ:JIT does the above. I believe these are just internal RA
        // optimizations. Temporarily writing these down in case we
        // see problems with this.
        generatePredictedChain (cUnit, bb, -1, (mir->dalvikInsn.vB * 4), false, C, mir);
    }
    else
    {
        // OP_INVOKE_VIRTUAL and OP_INVOKE_VIRTUAL_RANGE

        int methodIndex =
                    cUnit->method->clazz->pDvmDex->pResMethods[mir->dalvikInsn.vB]->methodIndex;
        generatePredictedChain (cUnit, bb, mir->dalvikInsn.vB, (methodIndex * 4), false, C, mir);
    }
}

ArgsDoneType dvmCompilerPcgTranslateConvertCalleeToType (const Method* calleeMethod)
{
    if (calleeMethod == 0)
    {
        return ArgsDone_Full;
    }

    if (dvmIsNativeMethod (calleeMethod))
    {
        return ArgsDone_Native;
    }

    return ArgsDone_Normal;
}

void dvmCompilerPcgTranslateInvokeStaticSuper (CompilationUnitPCG *cUnit, MIR *mir)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    Opcode dalvikOpCode = mir->dalvikInsn.opcode;
    const Method *calleeMethod;

    if (mir->OptimizationFlags & MIR_INLINED)
    {
        return;
    }

    dvmCompilerPcgExportPC (cUnit);

    if (dalvikOpCode == OP_INVOKE_STATIC || dalvikOpCode == OP_INVOKE_STATIC_RANGE)
    {
        u2 tmp = mir->dalvikInsn.vB;

        // method is already resolved in trace-based JIT
        calleeMethod = cUnit->method->clazz->pDvmDex->pResMethods[tmp];
    }
    else
    {
        if (dalvikOpCode == OP_INVOKE_SUPER || dalvikOpCode == OP_INVOKE_SUPER_RANGE)
        {
            u2 tmp = mir->dalvikInsn.vB;

            // for trace-based JIT, callee is already resolved
            int mIndex = cUnit->method->clazz->pDvmDex->
                    pResMethods[tmp]->methodIndex;
            calleeMethod =
                    cUnit->method->clazz->super->vtable[mIndex];
        }
        else
        {
            // OP_INVOKE_SUPER_QUICK and OP_INVOKE_SUPER_QUICK_RANGE

            // for trace-based JIT, callee is already resolved
            u2 IMMC = 4 * mir->dalvikInsn.vB;
            int mIndex = IMMC/4;
            calleeMethod = cUnit->method->clazz->super->vtable[mIndex];
        }
    }

    CGInst methodIl = CGCreateNewInst ("mov", "i", (int) calleeMethod);

    ArgsDoneType methodType = dvmCompilerPcgTranslateConvertCalleeToType (calleeMethod);

    // get the fallthrough target instruction
    CGInst fallthroughTargetSymbInst = dvmCompilerPcgGetFallthroughTargetSymbolInst (cUnit, bb);

    dvmCompilerPcgCommonInvokeMethodJmp (cUnit, mir, methodType, methodIl, fallthroughTargetSymbInst);
}

void dvmCompilerPcgTranslateInvokeInterface (CompilationUnitPCG *cUnit, MIR *mir)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int ssaNum = ssaRep->uses[0];
    u2 tmp = mir->dalvikInsn.vB;

    if (mir->OptimizationFlags & MIR_INLINED)
    {
        return;
    }

    dvmCompilerPcgExportPC (cUnit);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum, "mov", 4);
    dvmCompilerPcgGenerateSimpleNullCheck (cUnit, C);
    generatePredictedChain (cUnit, bb, tmp, -1, true, C, mir);
}

void dvmCompilerPcgTranslateInvokeDirect (CompilationUnitPCG *cUnit, MIR *mir)
{
    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    u2 tmp = mir->dalvikInsn.vB;
    u2 vC = ssaRep->uses[0];

    if (mir->OptimizationFlags & MIR_INLINED)
    {
        return;
    }

    dvmCompilerPcgExportPC (cUnit);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, vC, "mov", 4);
    dvmCompilerPcgGenerateSimpleNullCheck (cUnit, C);

    const Method *calleeMethod = cUnit->method->clazz->pDvmDex->pResMethods[tmp];

    // TODO - This is an optimization opportunity.  We know the address of the
    //        target.  We should be able to branch to it directly.
    CGInst methodIl = CGCreateNewInst ("mov", "i", calleeMethod);
    ArgsDoneType methodType = dvmCompilerPcgTranslateConvertCalleeToType (calleeMethod);

    // get the fallthrough target instruction
    CGInst fallthroughTargetSymbInst = dvmCompilerPcgGetFallthroughTargetSymbolInst (cUnit, bb);

    dvmCompilerPcgCommonInvokeMethodJmp (cUnit, mir, methodType, methodIl, fallthroughTargetSymbInst);
}

void dvmCompilerPcgTranslateReturn (CompilationUnitPCG *cUnit, MIR *mir, bool isVoid)
{
    Opcode dalvikOpCode = mir->dalvikInsn.opcode;
    CGInst selfIl = dvmCompilerPcgGetSelfPointer (cUnit);

    if (isVoid == false)
    {
        //Get the SSARepresentation
        SSARepresentation *ssaRep = mir->ssaRep;

        assert (ssaRep != 0);

        int ssaNum = ssaRep->uses[0];
        pcgDtype resDtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
        int32_t dtypeSize = (dalvikOpCode == OP_RETURN_WIDE) ? 8 : 4;
        const char *opcode;
        resDtype = dvmCompilerPcgApplyDefaultDtype (resDtype, dtypeSize);
        dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, resDtype, &opcode);

        // self->interpSave.retval = vA
        int32_t offset = offsetof (Thread, interpSave.retval);

        CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum, opcode, dtypeSize);
        CGAddr addr = CGCreateAddr (selfIl, CGInstInvalid, 0,
                CGSymbolInvalid, offset);
        CGCreateNewInst (opcode, "mr", addr, dtypeSize, (void*)1, A);
    }

    CGInst saveAreaIl = CGCreateNewInst ("mov", "r", cUnit->getVMPtr ());
    saveAreaIl = CGCreateNewInst ("add", "ri", saveAreaIl,
            -sizeof (StackSaveArea));

    CGInst prevFrameIl =
        dvmCompilerPcgCreateSimpleLoad (cUnit->getVMPtr (),
                OFFSETOF_MEMBER (StackSaveArea,prevFrame) -
                sizeof (StackSaveArea));

    saveAreaIl = CGCreateNewInst ("mov", "r", saveAreaIl);
    CGSetRreg (saveAreaIl, "edx");

    prevFrameIl = CGCreateNewInst ("mov", "r", prevFrameIl);
    CGSetRreg (prevFrameIl, "edi");

    selfIl = CGCreateNewInst ("mov", "r", selfIl);
    CGSetRreg (selfIl, "ecx");

    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);

    CGInst parms[5] = {parmEbp, selfIl, saveAreaIl, prevFrameIl, CGInstInvalid};

    CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitHelper_returnFromMethod");
    dvmCompilerPcgCreateJsr (cUnit, target, parms);
    return;
}

void dvmCompilerPcgTranslateExecuteInline (CompilationUnitPCG *cUnit, MIR *mir)
{
    int ssaNum;
    u2 tmp = mir->dalvikInsn.vB;
    CGInst self;
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    // The purpose of returning vs breaking in this switch statement is that it
    // *truly* inlines the implementation of these bytcodes, while bytecodes
    // not in the switch statement simply call special functions in InlineNative.cpp
    switch (tmp) {
        case INLINE_EMPTYINLINEMETHOD:
            return;  // NOP

        case INLINE_STRING_LENGTH:
        case INLINE_STRING_IS_EMPTY:
            {
                CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
                dvmCompilerPcgGenerateNullCheck (cUnit, C, mir);
                CGInst length = dvmCompilerPcgCreateSimpleLoad (C, 0x14);
                CGInst result;

                self = dvmCompilerPcgGetSelfPointer (cUnit);
                if (tmp == INLINE_STRING_LENGTH)
                {
                    result = length;
                }
                else
                {
                    CGInst zero = CGCreateNewInst ("mov", "i", 0);
                    CGInst one = CGCreateNewInst ("mov", "i", 1);
                    result = CGCreateNewInst ("islcc", "rcrrr", length, "eq", zero, one, zero);
                }

                dvmCompilerPcgCreateSimpleStore (self, offsetof (Thread, interpSave.retval), result);
                return;
            }

        case INLINE_STRING_CHARAT:
            {
                CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
                CGInst D = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);
                CGInst length = dvmCompilerPcgCreateSimpleLoad (C, 0x14);
                CGInst self = dvmCompilerPcgGetSelfPointer (cUnit);
                CGLabel offsetOkLabel = CGCreateLabel ();
                CGLabel exceptionLabel = CGCreateLabel ();
                dvmCompilerPcgGenerateNullCheck (cUnit, C, mir);
                CGCreateNewInst ("cjcc", "rcrbp", D, "slt", length, offsetOkLabel, 100);
                CGBindLabel (exceptionLabel);
                dvmCompilerPcgGenerateRaiseException (cUnit);
                CGBindLabel (offsetOkLabel);
                CGInst zero = CGCreateNewInst ("mov", "i", 0);
                CGCreateNewInst ("cjcc", "rcrbp", D, "slt", zero, exceptionLabel, 0);
                CGInst offsetBias = dvmCompilerPcgCreateSimpleLoad (C, 0x10);
                CGInst offsetInst = CGCreateNewInst ("add", "rr", offsetBias, D);
                CGInst stringPtr = dvmCompilerPcgCreateSimpleLoad (C, 0x8);
                CGAddr addr = CGCreateAddr (stringPtr, offsetInst, 2, CGSymbolInvalid, OFFSETOF_MEMBER (ArrayObject, contents));
                CGInst result = CGCreateNewInst ("hldz", "m", addr, 2, (void*)1);

                self = dvmCompilerPcgGetSelfPointer (cUnit);
                dvmCompilerPcgCreateSimpleStore (self, offsetof (Thread, interpSave.retval), result);
                return;
            }

        case INLINE_MATH_ABS_INT:
            {
                CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
                CGInst shiftedC = CGCreateNewInst ("sar", "ri", C, 31);
                CGInst xoredC = CGCreateNewInst ("xor", "rr", C, shiftedC);
                CGInst result = CGCreateNewInst ("sub", "rr", xoredC, shiftedC);
                self = dvmCompilerPcgGetSelfPointer (cUnit);
                dvmCompilerPcgCreateSimpleStore (self,
                    offsetof (Thread, interpSave.retval), result);
                return;
            }
        case INLINE_MATH_ABS_LONG:
            {
                ssaNum = ssaRep->uses[0];
                CGInst load = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum,
                                                           "lmov", 8);
                CGInst shiftedLoad = CGCreateNewInst ("lsar", "ri", load, 63);
                CGInst xoredLoad = CGCreateNewInst ("lxor", "rr", load,
                        shiftedLoad);
                CGInst result = CGCreateNewInst ("lsub", "rr", xoredLoad,
                        shiftedLoad);
                self = dvmCompilerPcgGetSelfPointer (cUnit);
                dvmCompilerPcgCreateTypedStore (cUnit, self, CGInstInvalid, 0,
                    CGSymbolInvalid, offsetof (Thread, interpSave.retval),
                    LLreg, result);
                return;
            }

        case INLINE_MATH_MAX_INT:
        case INLINE_MATH_MIN_INT:
            {
                const char *cond = (tmp == INLINE_MATH_MAX_INT) ? "sgt" : "slt";
                CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
                CGInst D = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);
                CGInst result = CGCreateNewInst ("islcc", "rcrrr", C, cond, D, C, D);

                self = dvmCompilerPcgGetSelfPointer (cUnit);
                dvmCompilerPcgCreateSimpleStore (self, offsetof (Thread, interpSave.retval), result);
                return;
            }

        case INLINE_MATH_ABS_FLOAT:
            {
                __attribute__ ( (aligned (16))) static u4 mask[4] =
                    {0x7fffffff, 0, 0, 0};

                CGInst load = dvmCompilerPcgGetVirtualReg (cUnit,
                    ssaRep->uses[0], "movss1", 4);

                CGAddr addr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
                    CGSymbolInvalid, (int32_t)&mask);
                CGInst result = CGCreateNewInst ("andps1", "rm", load, addr,
                    16, (void*)1);

                // Store the return value
                self = dvmCompilerPcgGetSelfPointer (cUnit);
                addr = CGCreateAddr (self, CGInstInvalid, 0, CGSymbolInvalid, offsetof (Thread, interpSave.retval));
                CGCreateNewInst ("movss1", "mr", addr, 4, (void*)1, result);
                return;
            }
        case INLINE_MATH_ABS_DOUBLE:
            {
                __attribute__ ( (aligned (16))) static u4 mask[4] = {0xffffffff, 0x7fffffff, 0, 0};

                CGInst load = dvmCompilerPcgGetVirtualReg(cUnit, ssaRep->uses[0], "movsd1", 8);
                CGAddr addr = CGCreateAddr(CGInstInvalid, CGInstInvalid, 0, CGSymbolInvalid, (int32_t)&mask);
                CGInst result = CGCreateNewInst("andpd1", "rm", load, addr, 16, (void*)1);

                MIR* mirNext = mir->next;

                // if next bytecode is a move-result-wide, then we can handle them together here to remove the return value storing
                if (mirNext != 0 && mirNext->dalvikInsn.opcode == OP_MOVE_RESULT_WIDE)
                {
                    //Get the SSARepresentation
                    Opcode dalvikOpCode = mirNext->dalvikInsn.opcode;
                    SSARepresentation *ssaRep = mirNext->ssaRep;

                    assert (ssaRep != 0);

                    int ssaNum = ssaRep->defs[0];
                    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
                    const char *opcode;
                    int objectSize = (dalvikOpCode == OP_MOVE_RESULT_WIDE) ? 8 : 4;

                    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, objectSize);
                    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);
                    if (dtype == LLreg)
                    {
                        result = CGCreateNewInst("movsd12sd", "r", result);
                        result = CGCreateNewInst("emovdtl", "r", result);
                    }
                    dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, opcode, objectSize, result);
                    mirNext->OptimizationFlags |= MIR_OPTIMIZED_AWAY;
                }
                else
                {
                    // Store the return value
                    self = dvmCompilerPcgGetSelfPointer (cUnit);
                    addr = CGCreateAddr (self, CGInstInvalid, 0, CGSymbolInvalid, offsetof (Thread, interpSave.retval));
                    CGCreateNewInst ("movsd1", "mr", addr, 8, (void*)1, result);
                }
                return;
            }
        case INLINE_STRING_FASTINDEXOF_II:
            {
                CGInst stringObject = dvmCompilerPcgGetVirtualReg (cUnit,
                    ssaRep->uses[0], "mov", 4);

                // Bail if the String pointer is null
                dvmCompilerPcgGenerateNullCheck (cUnit, stringObject, mir);

                // Get the matchChar and the startIndex
                CGInst matchChar = dvmCompilerPcgGetVirtualReg (cUnit,
                    ssaRep->uses[1], "mov", 4);
                CGInst startIndex = dvmCompilerPcgGetVirtualReg (cUnit,
                    ssaRep->uses[2], "mov", 4);

                // Get the string length (I think)
                CGAddr countAddr = CGCreateAddr (stringObject,
                    CGInstInvalid, 0, CGSymbolInvalid,
                    gDvm.offJavaLangString_count);
                CGInst count = CGCreateNewInst ("mov", "m", countAddr, 4,
                    (void*)1);

                // Get the offset (not sure what that is, exactly) from
                // the StringObject reference
                CGAddr offsetAddr = CGCreateAddr (stringObject, CGInstInvalid,
                    0, CGSymbolInvalid, gDvm.offJavaLangString_offset);
                CGInst offset = CGCreateNewInst ("mov", "m", offsetAddr, 4,
                    (void*)1);

                // Precalculate the "actual" value address
                // Java chars are 2-bytes, btw
                CGInst charsAddrPlusOffset = CGCreateNewInst ("add", "rr",
                    stringObject, CGCreateNewInst ("mul", "ri", offset, 2));

                CGInst zero = CGCreateNewInst ("mov", "i", 0);

                // clamp startIndex to [0,count]
                startIndex = CGCreateNewInst ("islcc", "rcrrr", startIndex,
                    "slt", zero, zero, startIndex);
                startIndex = CGCreateNewInst ("islcc", "rcrrr", startIndex,
                    "sge", count, count, startIndex);

                // Loop
                CGLabel loopLabel = CGCreateLabel ();
                CGLabel exitLabel = CGCreateLabel ();
                CGLabel exitFalseLabel = CGCreateLabel ();

                CGTemp loopCounterTemp = cUnit->getCurrentTemporaryVR(true);
                CGInst loopCounter = CGCreateNewInst ("mov", "r", startIndex);
                CGAddTempDef(loopCounterTemp, loopCounter);

                CGBindLabel (loopLabel);

                // Get out of the loop, when we've incremented the
                // index past the end of the array
                CGCreateNewInst ("cjcc", "rcrbp", CGGetTempUseInst(loopCounterTemp), "uge",
                    count, exitFalseLabel, 0);

                // Check if the indexed character is equal to the matchChar
                CGAddr indexedCharsAddr = CGCreateAddr (charsAddrPlusOffset,
                    CGGetTempUseInst(loopCounterTemp), 2, CGSymbolInvalid, 0);
                CGInst indexedChar = CGCreateNewInst ("hldz", "m",
                    indexedCharsAddr, 2, (void*)1);
                CGCreateNewInst ("cjcc", "rcrbp", matchChar, "eq",
                    indexedChar, exitLabel, 0);

                // increment the array index and jump back to the loop label
                CGInst newStartIndex = CGCreateNewInst ("add", "ri", CGGetTempUseInst(loopCounterTemp), 1);
                CGAddTempDef(loopCounterTemp, newStartIndex);
                CGCreateNewInst ("jmp", "b", loopLabel);

                // Lay down the unsuccessful exit label
                CGBindLabel (exitFalseLabel);

                // Set the return value to -1 to denote not finding the char in the string
                CGInst falseResult = CGCreateNewInst ("mov", "i", -1);
                CGAddTempDef(loopCounterTemp, falseResult);

                // Lay down the successful exit label
                CGBindLabel (exitLabel);

                // Get the return value
                CGInst result = CGCreateNewInst ("mov", "r", CGGetTempUseInst(loopCounterTemp));

                // Store the return value
                self = dvmCompilerPcgGetSelfPointer (cUnit);
                dvmCompilerPcgCreateSimpleStore (self,
                    offsetof (Thread, interpSave.retval), result);
                return;
            }
        case INLINE_INT_BITS_TO_FLOAT:
        case INLINE_DOUBLE_TO_RAW_LONG_BITS:
        case INLINE_FLOAT_TO_RAW_INT_BITS:
        case INLINE_LONG_BITS_TO_DOUBLE:
            {
                const char *opcode;
                int32_t dtypeSize = (tmp == INLINE_DOUBLE_TO_RAW_LONG_BITS ||
                                     tmp == INLINE_LONG_BITS_TO_DOUBLE) ? 8 : 4;
                ssaNum = ssaRep->uses[0];
                pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
                dtype = dvmCompilerPcgApplyDefaultDtype (dtype, dtypeSize);
                dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);
                CGInst load = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum, opcode, dtypeSize);
                self = dvmCompilerPcgGetSelfPointer (cUnit);
                CGAddr addr = CGCreateAddr (self, CGInstInvalid, 0, CGSymbolInvalid, offsetof (Thread, interpSave.retval));
                CGCreateNewInst (opcode, "mr", addr, dtypeSize, (void*)1, load);
                return;
            }
        default:
            dvmCompilerPcgExportPC (cUnit);
            break;
    }

    CGInst selfPlusRetval = dvmCompilerPcgGetSelfPointer (cUnit);
    selfPlusRetval = CGCreateNewInst ("add", "ri", selfPlusRetval, offsetof (Thread, interpSave.retval));

    CGInst spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), 32);
    CGSetRreg (spIl, "esp");

    dvmCompilerPcgCreateSimpleStore (spIl, 16, selfPlusRetval);

    // Store the outgoing arguments to the stack.  The SSA information gives us the dtypes to use.
    int32_t dtypeSize = 0;
    for (int i = 0; i < mir->ssaRep->numUses; i += dtypeSize / 4)
    {
        const char *opcode;
        int ssaNum = ssaRep->uses[i];
        pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);

        dtype = dvmCompilerPcgApplyDefaultDtype (dtype, 4);
        dtypeSize = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);
        CGInst load = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[i], opcode, dtypeSize);
        CGAddr addr = CGCreateAddr (spIl, CGInstInvalid, 0, CGSymbolInvalid, i * 4);
        CGCreateNewInst (opcode, "mr", addr, dtypeSize, (void*)1, load);
    }

    CGInst tableAddrBase = dvmCompilerPcgCreateSimpleLoad (CGInstInvalid, (int)gDvmInlineOpsTable + (tmp * 16));

    // Create new moves for edi & ebp.
    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

    CGInst theCall = CGCreateNewInst ("icall", "rl", tableAddrBase, parms);
    CGSetRreg (theCall, "eax");
    spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), 32);
    CGSetRreg (spIl, "esp");
    theCall = CGCreateNewInst ("mov", "r", theCall);

    CGLabel doneLabel = CGCreateLabel ();
    CGCreateNewInst ("cjcc", "rcrb", theCall, "ne", CGCreateNewInst ("mov", "i", 0), doneLabel);

    dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit); // ZZZ TODO : exception number = 1

    CGBindLabel (doneLabel);
}

void dvmCompilerPcgTranslateMoveResult (CompilationUnitPCG *cUnit, MIR *mir)
{
    if ( (mir->OptimizationFlags & MIR_INLINED) != 0)
    {
        return;
    }

    // when removal of return value in execute-inline is done, current mir should be skipped and nothing need to be done here
    if (mir->OptimizationFlags & MIR_OPTIMIZED_AWAY)
    {
        return;
    }

    //Get the SSARepresentation
    Opcode dalvikOpCode = mir->dalvikInsn.opcode;
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int ssaNum = ssaRep->defs[0];
    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
    const char *opcode;
    int objectSize = (dalvikOpCode == OP_MOVE_RESULT_WIDE) ? 8 : 4;

    dtype = dvmCompilerPcgApplyDefaultDtype (dtype, objectSize);
    dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    CGInst selfPointer = dvmCompilerPcgGetSelfPointer (cUnit);
    CGAddr addr = CGCreateAddr (selfPointer, CGInstInvalid, 0, CGSymbolInvalid, offsetof (Thread, interpSave.retval));
    CGInst retVal = CGCreateNewInst (opcode, "m", addr, objectSize, (void*)1);

    dvmCompilerPcgSetVirtualReg (cUnit, ssaNum, opcode, objectSize, retVal);
}

/**
 * @brief Generate the code for before an invoke
 */
static void common_invokeArgsDone_airThunk(void)
{
    // sub 8, esp
    // mov eax, (esp)
    // mov ebx, 4(esp)
    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EAX, true, 0, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EBX, true, 4, PhysicalReg_ESP, true);
}

const char *dvmCompilerPcgHandleInvokeArgsHeader (int value)
{
    ArgsDoneType form = static_cast<ArgsDoneType> (value);

    void (*backEndSymbolCreationCallback) (const char *, void *) = gDvmJit.jitFramework.backEndSymbolCreationCallback;

    // Insert different labels for the various forms
    const char * sectionLabel = 0;

    //Look at the form
    switch (form)
    {
        case ArgsDone_Full:
            backEndSymbolCreationCallback (".invokeArgsDone_jit_airthunk", (void*)stream);
            common_invokeArgsDone_airThunk ();
            sectionLabel = ".invokeArgsDone_jit";
            backEndSymbolCreationCallback (".invokeArgsDone_jit", (void*) stream);
            break;
        case ArgsDone_Normal:
            backEndSymbolCreationCallback (".invokeArgsDone_normal_airthunk", (void*)stream);
            common_invokeArgsDone_airThunk ();
            sectionLabel = ".invokeArgsDone_normal";
            backEndSymbolCreationCallback (".invokeArgsDone_normal", (void*) stream);
            break;
        default:
            // form == ArgsDone_Native
            backEndSymbolCreationCallback (".invokeArgsDone_native_airthunk", (void*)stream);
            common_invokeArgsDone_airThunk ();
            sectionLabel = ".invokeArgsDone_native";
            backEndSymbolCreationCallback (".invokeArgsDone_native", (void*) stream);
            break;
    }

    return sectionLabel;
}

/**
 * @brief Used to look for a fromInterpreter node in predecessors of blockPostInvoke
 * @param cUnit The compilation unit
 * @param blockPostInvoke The basic block that follows the invoke block
 * @return Returns the from interpreter node if one is found. Otherwise returns 0.
 */
static BasicBlockPCG *findFromInterpNode (CompilationUnitPCG *cUnit, BasicBlock *blockPostInvoke)
{
    BasicBlockPCG *fromInterp = 0;
    BitVectorIterator predIter;
    dvmBitVectorIteratorInit (blockPostInvoke->predecessors, &predIter);

    //Now go through the predecessors
    for (BasicBlock *predBB = dvmCompilerGetNextBasicBlockViaBitVector (predIter, cUnit->blockList);
            predBB != 0;
            predBB = dvmCompilerGetNextBasicBlockViaBitVector (predIter, cUnit->blockList))
    {
        if (predBB->blockType == kFromInterpreter)
        {
            //We found it
            fromInterp = reinterpret_cast<BasicBlockPCG *> (predBB);
            break;
        }
    }

    return fromInterp;
}

CGInst dvmCompilerPcgGetFallthroughTargetSymbolInst (CompilationUnitPCG *cUnit, BasicBlockPCG *bb)
{
    bool fallthroughNeedsCfgEdge = false;
    CGSymbol fallthroughTargetSym = CGSymbolInvalid;

    //Now determine the fallthrough symbol and whether edge is needed
    BasicBlockPCG *fallthrough = (BasicBlockPCG *) bb->fallThrough;
    fallthroughTargetSym = dvmCompilerPcgGetInvokeTarget (cUnit, fallthrough, &fallthroughNeedsCfgEdge);

    // If the fallthrough edge is needed, then we need get there via path that will
    // re-setup any state needed as if block post-invoke is a possible entry.
    if (fallthroughNeedsCfgEdge == true)
    {
        //So what we do is that we look for the fromInterpreter node that is guaranteed to exist.
        BasicBlockPCG *fromInterp = findFromInterpNode (cUnit, fallthrough);

        //In the assert world, we fail because we really expect to find the entry
        assert (fromInterp != 0);

        //But in case we don't, then we set error and bail out early
        if (fromInterp == 0)
        {
            cUnit->errorHandler->setError (kJitErrorPcgPostInvokeEntryNotFound);
            return CGInstInvalid;
        }

        //Now obtain the target symbol of the fromInterpreter block so we can jump to it when
        //returning from our invoke
        fallthroughTargetSym = cUnit->getBlockSymbol (fromInterp->cgLabel);
    }

    // return the CGInst which loads the fallthrough target
    CGInst inst = CGCreateNewInst("movltc", "n", fallthroughTargetSym);
    return inst;
}

void dvmCompilerPcgCommonInvokeMethodJmp (CompilationUnitPCG *cUnit, const MIR *mir, ArgsDoneType form, CGInst methodToCall, CGInst fallThroughTargetSymInst)
{
    const BasicBlockPCG * bb = static_cast <const BasicBlockPCG *> (mir->bb);
    //  arguments needed in ArgsDone:
    //      start of HotChainingCell for next bytecode: -4 (%esp)
    //      start of InvokeSingletonChainingCell for callee: -8 (%esp)
    CGSymbol takenTargetSym = CGSymbolInvalid;
    bool takenNeedsCfgEdge = false;

    BasicBlockPCG *taken = (BasicBlockPCG *) bb->taken;

    if (taken != 0)
    {
        takenTargetSym = dvmCompilerPcgGetInvokeTarget (cUnit, taken, &takenNeedsCfgEdge);
    }

    // The taken edge must always be a supporting chaining cell for invoke or it must not exist
    assert (takenNeedsCfgEdge == false);

    // We will remove VRs associated with any inlined method
    // because they're dead, now that we're calling a different method
    BitVector *inlinedVRs = cUnit->getTemporaryBitVector ();
    dvmClearAllBits (inlinedVRs);

    for (int j = 0; j < cUnit->registerWindowShift; j++)
    {
        //Get bitvector associated to it
        BitVector *vrDefsBv = cUnit->getSSANumSet (j);

        if (vrDefsBv != 0)
        {
            // Add all the defs of the inlined VR to the inlinedVRs set
            dvmUnifyBitVectors (inlinedVRs, inlinedVRs, vrDefsBv);
        }
    }

    // Remove all defs of all inlined VRs from the currently tracked VRs
    BitVector *currentlyTrackedVRs = cUnit->getCurrMod ();
    dvmSubtractBitVectors (currentlyTrackedVRs, currentlyTrackedVRs, inlinedVRs);

    //Free temporary bitvector
    cUnit->freeTemporaryBitVector (inlinedVRs);

    // Now generate the necessary writebacks because we are leaving trace
    dvmCompilerPcgGenerateWritebacks (cUnit, currentlyTrackedVRs);

    // Now set up the arguments for the invoke
    dvmCompilerPcgStoreInvokeArgs (cUnit, mir);

    // sets up parameters in eax, ebx, ecx, and edx
    CGInst fallthroughTarget = CGCreateNewInst ("mov", "r", fallThroughTargetSymInst);

    CGInst takenTarget;
    if (bb->taken != 0)
    {
        takenTarget = CGCreateNewInst("movltc", "n", takenTargetSym);
    }
    else
    {
        takenTarget = CGCreateNewInst("mov", "i", 0);
    }
    CGInst rPCValue = CGCreateNewInst ("mov", "i", (int32_t)rPC);

    CGInst takenTargetReg = CGCreateNewInst ("mov", "r", takenTarget);
    CGSetRreg (takenTargetReg, "eax");
    CGInst fallthroughTargetReg = CGCreateNewInst ("mov", "r", fallthroughTarget);
    CGSetRreg (fallthroughTargetReg, "ebx");
    CGInst methodToCallReg = CGCreateNewInst ("mov", "r", methodToCall);
    CGSetRreg (methodToCallReg, "ecx");
    CGInst rPCValueReg = CGCreateNewInst ("mov", "r", rPCValue);
    CGSetRreg (rPCValueReg, "edx");

    CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);

    CGInst parms[7] = {rPCValueReg, methodToCallReg, parmEdi, parmEbp, fallthroughTargetReg, takenTargetReg, CGInstInvalid};
    const char *targetName;

    switch (form)
    {
        case ArgsDone_Full:
            targetName = ".invokeArgsDone_jit_airthunk";
            break;
        case ArgsDone_Native:
            targetName = ".invokeArgsDone_native_airthunk";
            break;
        default:
            targetName = ".invokeArgsDone_normal_airthunk";
            break;
    }

    CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, targetName);
    dvmCompilerPcgCreateJsr (cUnit, target, parms);
}

void dvmCompilerPcgStoreInvokeArgs (CompilationUnitPCG *cUnit, const MIR *mir)
{
    int offset = - sizeof (StackSaveArea) - (4 * mir->ssaRep->numUses);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    for (int i = 0; i < mir->ssaRep->numUses; )
    {
        const char *opcode;
        int32_t dtypeSize;
        int ssaNum = ssaRep->uses[i];
        pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);

        dtype = dvmCompilerPcgApplyDefaultDtype (dtype, 4);
        dtypeSize = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);
        CGInst load = dvmCompilerPcgGetVirtualReg (cUnit, ssaNum, opcode, dtypeSize);
        CGAddr addr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid,
                offset);
        CGCreateNewInst (opcode, "mr", addr, dtypeSize, (void*)1, load);
        offset += dtypeSize;
        i += dtypeSize / 4;
    }
}

