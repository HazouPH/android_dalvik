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
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "LowerArray.h"
#include "LowerCall.h"
#include "LowerMemory.h"
#include "UtilityPCG.h"
#include "LowerOther.h"

void dvmCompilerPcgTranslateNewArray (CompilationUnitPCG *cUnit, MIR *mir)
{
    u4 tmp = mir->dalvikInsn.vC;

    void *classPtr = (void*) (cUnit->method->clazz->pDvmDex->pResClasses[tmp]);
    assert (classPtr != 0);

    dvmCompilerPcgExportPC (cUnit);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst length = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    // check size of the array, if negative, throw exception.
    // PCG : We just punt to the interpreter in this case, because it's
    // easier.
    CGLabel noException = CGCreateLabel ();

    CGCreateNewInst ("cjcc", "rcrbp", length, "sge", CGCreateNewInst ("mov", "i", 0), noException, 100);

    dvmCompilerPcgGenerateRaiseExceptionSimple (cUnit);

    CGBindLabel (noException);

    // call dvmAllocArrayByClass with inputs:
    // classObject, length, flag ALLOC_DONT_TRACK
    CGInst theCall = dvmCompilerPcgGenerateX86Call (cUnit, "dvmAllocArrayByClass", INTreg, 3,
            INTreg, CGCreateNewInst ("mov", "i", (int)classPtr),
            INTreg, CGCreateNewInst ("mov", "r", length),
            INTreg, CGCreateNewInst ("mov", "i", ALLOC_DONT_TRACK));

    CGLabel notNull = CGCreateLabel ();
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", theCall, "ne", zero, notNull, 100);
    dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit);

    CGBindLabel (notNull);

    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, theCall);
}

void dvmCompilerPcgTranslateArrayLength (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    dvmCompilerPcgGenerateNullCheck (cUnit, B, mir);
    CGInst lengthInst = dvmCompilerPcgCreateSimpleLoad (B, OFFSETOF_MEMBER (ArrayObject,length));
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, lengthInst);
}

void dvmCompilerPcgTranslateFillArrayData (CompilationUnitPCG *cUnit, MIR *mir)
{
    u4 tmp = mir->dalvikInsn.vB;
    CGLabel doneLabel = CGCreateLabel ();

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst pc = CGCreateNewInst ("mov", "i", (int32_t) (rPC + tmp));

    CGInst callResult = dvmCompilerPcgGenerateX86Call (cUnit, "dvmInterpHandleFillArrayData", INTreg, 2, INTreg, A, INTreg, pc);

    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", callResult, "ne", zero, doneLabel, 100);
    dvmCompilerPcgGenerateRaiseException (cUnit);
    CGBindLabel (doneLabel);
}

/**
 * @brief Common function to translate a filled new array
 * @param cUnit the CompilationUnitPCG
 * @param length the length of the new array
 * @param classIdx class object index
 * @return the instruction defining the result of dvmAllocArrayByClass
 */
static CGInst dvmCompilerPcgTranslateFilledNewArrayCommon (CompilationUnitPCG *cUnit, u2 length, u4 classIdx)
{
    ClassObject *classPtr =
        (cUnit->method->clazz->pDvmDex->pResClasses[classIdx]);
    if (classPtr != NULL) {
        ALOGI("FILLED_NEW_ARRAY class %s", classPtr->descriptor);
    }

    // resolve class
    CGInst classPtrInst = dvmCompilerPcgResolveClass(cUnit, classIdx);

    CGInst descriptor =
        dvmCompilerPcgCreateSimpleLoad (classPtrInst, OFFSETOF_MEMBER (ClassObject,descriptor));

    CGAddr addr = CGCreateAddr (descriptor, CGInstInvalid, 0,
                                CGSymbolInvalid, 1);

    // Load a single byte of the descriptor.
    CGInst descriptorByte = CGCreateNewInst ("bldz", "m", addr, 1, (void*)1);
    CGLabel arrayImpL = CGCreateLabel ();

    CGCreateNewInst ("cjcc", "rcrb", descriptorByte, "eq",
                     CGCreateNewInst ("mov", "i", 'I'), arrayImpL);
    CGCreateNewInst ("cjcc", "rcrb", descriptorByte, "eq",
                     CGCreateNewInst ("mov", "i", 'L'), arrayImpL);
    CGCreateNewInst ("cjcc", "rcrb", descriptorByte, "eq",
                     CGCreateNewInst ("mov", "i", '['), arrayImpL);
    dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit);

    CGBindLabel (arrayImpL);

    // call dvmAllocArrayByClass with inputs:
    // classPtr, length, flag ALLOC_DONT_TRACK
    CGInst theCall = dvmCompilerPcgGenerateX86Call (cUnit, "dvmAllocArrayByClass", INTreg, 3,
            INTreg, CGCreateNewInst ("mov", "i", (int)classPtr),
            INTreg, CGCreateNewInst ("mov", "i", length),
            INTreg, CGCreateNewInst ("mov", "i", ALLOC_DONT_TRACK));

    CGLabel notNull = CGCreateLabel ();
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", theCall, "ne", zero, notNull, 100);
    dvmCompilerPcgGenerateJsrdvmJitToExceptionThrown (cUnit);

    CGBindLabel (notNull);

    // We need to mark the card of the new array, if it's not an int.
    CGLabel dontMarkCardL = CGCreateLabel ();
    CGCreateNewInst ("cjcc", "rcrb", descriptorByte, "eq",
                     CGCreateNewInst ("mov", "i", 'I'), dontMarkCardL);
    dvmCompilerPcgTranslateMarkCardNotNull (cUnit, theCall);
    CGBindLabel (dontMarkCardL);

    // Set the return value.
    CGInst self = dvmCompilerPcgGetSelfPointer (cUnit);
    dvmCompilerPcgCreateSimpleStore (self, offsetof (Thread, interpSave.retval), theCall);

    return (theCall);
}

void dvmCompilerPcgTranslateFilledNewArray (CompilationUnitPCG *cUnit, MIR *mir)
{
    u2 length = mir->dalvikInsn.vA;
    u4 tmp = mir->dalvikInsn.vB;

    CGInst base = dvmCompilerPcgTranslateFilledNewArrayCommon (cUnit, length, tmp);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    int max = mir->ssaRep->numUses;
    CGInst temp;
    for (int i = 0; i < max; i++) {
        temp = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[i], "mov", 4);
        dvmCompilerPcgCreateSimpleStore (base, OFFSETOF_MEMBER(ArrayObject, contents) +
                                         (i * 4), temp);
    }
}
