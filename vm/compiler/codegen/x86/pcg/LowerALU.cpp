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

#include "ChainingCellException.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "CompilerIR.h"
#include "Dataflow.h"
#include "LowerALU.h"
#include "UtilityPCG.h"
#include "CompilationUnit.h"
#include "MethodContext.h"
#include "MethodContextHandler.h"
#include "limits.h"

void dvmCompilerPcgTranslateMove (CompilationUnitPCG *cUnit, MIR *mir)
{
    bool srcIsFloat = false;
    bool dstIsFloat = false;
    const char *srcOpcode = "mov";
    const char *dstOpcode = "mov";

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[0]) == VXreg32)
    {
        srcIsFloat = true;
        srcOpcode = "movss1";
    }

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], srcOpcode, 4);

    if (dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]) == VXreg32)
    {
        dstIsFloat = true;
        dstOpcode = "movss1";
    }

    // Do a conversion if the types mismatch.
    if (dstIsFloat == false && srcIsFloat == true)
    {
        B = CGCreateNewInst ("movss1dti", "r", B);
    }

    if (dstIsFloat == true && srcIsFloat == false)
    {
        B = CGCreateNewInst ("emovdfi", "r", B);
        B = CGCreateNewInst ("movss2ss1", "r", B);
    }

    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], dstOpcode, 4, B);
}

void dvmCompilerPcgTranslateMoveWide (CompilationUnitPCG *cUnit, MIR *mir)
{
    bool srcIsDouble = false;
    bool dstIsDouble = false;
    const char *srcOpcode = "lmov";
    const char *dstOpcode = "lmov";

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->uses[0]) == DPVXreg64)
    {
        srcIsDouble = true;
        srcOpcode = "movsd1";
    }

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], srcOpcode, 8);

    if (dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]) == DPVXreg64)
    {
        dstIsDouble = true;
        dstOpcode = "movsd1";
    }

    // Do a conversion if the types mismatch.
    if (dstIsDouble == false && srcIsDouble == true)
    {
        B = CGCreateNewInst ("movsd1dtl", "r", B);
    }

    if (dstIsDouble == true && srcIsDouble == false)
    {
        B = CGCreateNewInst ("emovdfi", "r", B);
        B = CGCreateNewInst ("movsd2sd1", "r", B);
    }

    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], dstOpcode, 8, B);
}

void dvmCompilerPcgTranslateConstHelper (CompilationUnitPCG *cUnit, MIR *mir, u4 val)
{
    bool isFloat = false;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]) == VXreg32)
    {
        isFloat = true;
    }

    if (isFloat == true)
    {
        CGSymbol mem_const_symbol;
        mem_const_symbol = cUnit->getMemConstSymbol((uint8_t *)&val, 4, 4);

        CGAddr addr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
                                    mem_const_symbol, 0);
        CGInst constMov = CGCreateNewInst ("movss1", "m", addr, 4,
               (void*)1);
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movss1", 4, constMov);
        return;
    }

    CGInst movi = CGCreateNewInst ("mov", "i", val);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, movi);
}

void dvmCompilerPcgTranslateConst (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgTranslateConstHelper (cUnit, mir, mir->dalvikInsn.vB);
}

void dvmCompilerPcgTranslateConst16 (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgTranslateConstHelper (cUnit, mir, (s2) mir->dalvikInsn.vB);
}

void dvmCompilerPcgTranslateConst4 (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgTranslateConstHelper (cUnit, mir, mir->dalvikInsn.vB);
}

void dvmCompilerPcgTranslateConstHigh16 (CompilationUnitPCG *cUnit, MIR *mir)
{
    dvmCompilerPcgTranslateConstHelper (cUnit, mir, ( (s4) mir->dalvikInsn.vB) << 16);
}

/**
 * @brief Translate constant wide
 * @param cUnit the CompilationUnitPCG
 * @param mir the MIR instruction
 * @param val the value
 */
void dvmCompilerPcgTranslateConstWide (CompilationUnitPCG *cUnit, MIR *mir, u8 val)
{
    bool isDouble = false;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaRep->defs[0]) == DPVXreg64)
    {
        isDouble = true;
    }

    if (isDouble == true)
    {
        CGSymbol mem_const_symbol;
        mem_const_symbol = cUnit->getMemConstSymbol((uint8_t *)&val, 8, 8);

        CGAddr addr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
                                    mem_const_symbol, 0);
        CGInst constMov = CGCreateNewInst ("movsd1", "m", addr, 8, (void*)1);
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movsd1", 8, constMov);
        return;
    }

    CGInst movi = CGCreateNewInst ("lmov", "j", val);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, movi);
}

void dvmCompilerPcgTranslateConstString (CompilationUnitPCG *cUnit, MIR *mir)
{
    u4 tmp = mir->dalvikInsn.vB;
    const Method *method = (mir->OptimizationFlags & MIR_CALLEE) ?  mir->meta.calleeMethod : cUnit->method;
    void *strPtr = (void*) (method->clazz->pDvmDex->pResStrings[tmp]);
    assert (strPtr != 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);
    CGInst strInst = CGCreateNewInst ("mov", "i", (int32_t)strPtr);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, strInst);
}

void dvmCompilerPcgTranslateLLreg (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "lmov", 8);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "lmov", 8);
    CGInst op = CGCreateNewInst (opcode, "rr", A, B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, op);
}

void dvmCompilerPcgTranslateLLregOp (CompilationUnitPCG *cUnit, const char *opcode, int ssaA, int ssaB)
{
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaB, "lmov", 8);
    CGInst op = CGCreateNewInst (opcode, "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaA, "lmov", 8, op);
}

void dvmCompilerPcgTranslateLLregShift (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "lmov", 8);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "mov", 4);
    C = CGCreateNewInst("and", "ri", C, 63);
    CGInst op = CGCreateNewInst (opcode, "rr", B, C);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, op);
}

void dvmCompilerPcgTranslateFloat (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movss1", 4);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "movss1", 4);
    CGInst op = CGCreateNewInst (opcode, "rr", A, B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movss1", 4, op);
}

void dvmCompilerPcgTranslateRemFloat (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movss1", 4);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "movss1", 4);
    CGInst ret = dvmCompilerPcgGenerateX86Call(cUnit, "fmodf", FPreg32, 2, VXreg32, A, VXreg32, B);
    ret = CGCreateNewInst ("movf2ss1", "r", ret);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movss1", 4, ret);
}

void dvmCompilerPcgTranslateRemDouble (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movsd1", 8);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "movsd1", 8);
    CGInst ret = dvmCompilerPcgGenerateX86Call(cUnit, "fmod", FPreg64, 2, DPVXreg64, A, DPVXreg64, B);
    ret = CGCreateNewInst ("movf2sd1", "r", ret);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movsd1", 8, ret);
}

void dvmCompilerPcgTranslateDouble (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst A = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movsd1", 8);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "movsd1", 8);
    CGInst op = CGCreateNewInst (opcode, "rr", A, B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movsd1", 8, op);
}

void dvmCompilerPcgTranslateIntToFP (CompilationUnitPCG *cUnit, MIR *mir, int32_t size)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    const char *cvtOpcode;
    const char *movOpcode;

    if (size == 8) {
        cvtOpcode = "cvtsi2sd1";
        movOpcode = "movsd1";
    }
    else {
        cvtOpcode = "cvtsi2ss1";
        movOpcode = "movss1";
    }

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst cvt = CGCreateNewInst (cvtOpcode, "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], movOpcode, size, cvt);
}

void dvmCompilerPcgTranslateLongToFP (CompilationUnitPCG *cUnit, MIR *mir, int32_t size)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    const char *cvt1Opcode;
    const char *cvt2Opcode;
    const char *movOpcode;

    // We have to use x87 for the conversions, because on IA-32, there are no
    // SSE/SSE2 conversions involving 64-bit integers.
    if (size == 8)
    {
        cvt1Opcode = "f64ild64";
        cvt2Opcode = "movf2sd1";
        movOpcode = "movsd1";
    }
    else
    {
        cvt1Opcode = "f32ild64";
        cvt2Opcode = "movf2ss1";
        movOpcode = "movss1";
    }

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "lmov", 8);
    CGInst cvt1 = CGCreateNewInst (cvt1Opcode, "r", B);
    CGInst cvt2 = CGCreateNewInst (cvt2Opcode, "r", cvt1);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], movOpcode, size, cvt2);
}

// The semantics of the FP to integer conversions are as follows. For input
// value x, there are 4 possibilities:
//
// (1) For NaN values of x, the result is 0
// (2) For x > 0x7fffffff, the result is 0x7fffffff
// (3) For x < 0x80000000, the result is 0x80000000.
// (4) For other values of x, the result is rounded toward zero.
void dvmCompilerPcgTranslateFPToInt (CompilationUnitPCG *cUnit, MIR *mir, int32_t size)
{
    static double doubleMaxInt32 = (double)0x7fffffff; // 52-bit mantissa (no loss)
    static float floatMaxInt32 = (float)0x7fffff80; // 23-bit mantissa, 1 bit exponent (31-23-1 = 7 bit loss)
    CGLabel endLabel = CGCreateLabel ();
    CGLabel nanOrMaxLabel = CGCreateLabel ();
    const char *movOpcode;
    const char *cvtOpcode;
    int32_t maxInt32Addr;

    if (size == 8)
    {
        movOpcode = "movsd1";
        cvtOpcode = "cvttsd2si";
        maxInt32Addr = (int32_t)&doubleMaxInt32;
    }
    else
    {
        movOpcode = "movss1";
        cvtOpcode = "cvttss2si";
        maxInt32Addr = (int32_t)&floatMaxInt32;
    }

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    // If the input value is <= 0x7fffffff, we can use a simple cvttsd2si
    // instruction to do the conversion.  Note that cvttsd2si has the desired
    // behavior if the input value is < 0x80000000.  It sets the result to
    // 0x80000000 in that case. (It does signal invalid, though.  Do we care?)
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], movOpcode, size);
    CGAddr addr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
            CGSymbolInvalid, maxInt32Addr);
    CGInst maxInt32Inst = CGCreateNewInst (movOpcode, "m", addr,
            size, (void*)1);
    CGCreateNewInst ("cjcc", "rcrbp", B, "ufnle", maxInt32Inst,
            nanOrMaxLabel, 0);
    CGInst cvt = CGCreateNewInst (cvtOpcode, "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, cvt);
    CGCreateNewInst ("jmp", "b", endLabel);

    // Handle large positive values and NaN values here.
    CGBindLabel (nanOrMaxLabel);
    CGInst nanResult = CGCreateNewInst ("mov", "i", 0);
    CGInst maxResult = CGCreateNewInst ("mov", "i", 0x7fffffff);
    CGInst result = CGCreateNewInst ("islcc", "rcrrr", B, "ufgt",
            maxInt32Inst, maxResult, nanResult);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, result);

    CGBindLabel (endLabel);
}

// The semantics of the FP to long integer conversions are as follows. For input
// value x, there are 4 possibilities:
//
// (1) For NaN values of x, the result is 0
// (2) For x > 0x7ffffffffffffc00, the result is 0x7fffffffffffffff
// (3) For x < 0x8000000000000000, the result is 0x8000000000000000.
// (4) For other values of x, the result is rounded toward zero.
void dvmCompilerPcgTranslateFPToLong (CompilationUnitPCG *cUnit, MIR *mir, int32_t size)
{
    static double doubleMaxInt64 = ((double)0x7ffffffffffffc00LL); // 52-bit mantissa, 1 bit exponent (63-52-1 = 10 bit loss)
    static float floatMaxInt64 = ((float)0x7fffff8000000000LL); // 23-bit mantissa, 1 bit exponent (63-23-1 = 39 bit loss)

    CGLabel endLabel = CGCreateLabel ();
    CGLabel nanOrMaxLabel = CGCreateLabel ();
    const char *movOpcode;
    const char *cvtOpcode1;
    const char *cvtOpcode2;
    int32_t maxInt64Addr;

    if (size == 8)
    {
        movOpcode = "movsd1";
        cvtOpcode1 = "movsd12f64";
        cvtOpcode2 = "fcvttdl";
        maxInt64Addr = (int32_t)&doubleMaxInt64;
    }
    else
    {
        movOpcode = "movss1";
        cvtOpcode1 = "movss12f32";
        cvtOpcode2 = "fcvttsl";
        maxInt64Addr = (int32_t)&floatMaxInt64;
    }

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    // If the input value is <= 0x7fffffffffffffff, we can use a simple fcvttsxl
    // instruction to do the conversion.  Note that fcvttsxl has the desired
    // behavior if the input value is < 0x8000000000000000.  It sets the result to
    // 0x8000000000000000 in that case. (It does signal invalid, though.  Do we care?)
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], movOpcode, size);

    CGAddr addr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
            CGSymbolInvalid, maxInt64Addr);
    CGInst maxInt64Inst = CGCreateNewInst (movOpcode, "m", addr,
            size, (void*)1);
    CGCreateNewInst ("cjcc", "rcrbp", B, "ufnle", maxInt64Inst,
            nanOrMaxLabel, 0);
    CGInst cvt1 = CGCreateNewInst (cvtOpcode1, "r", B);
    CGInst cvt2 = CGCreateNewInst (cvtOpcode2, "r", cvt1);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, cvt2);

    CGCreateNewInst ("jmp", "b", endLabel);
    // Handle large positive values and NaN values here.
    CGBindLabel (nanOrMaxLabel);

    CGInst nanResult = CGCreateNewInst ("lmovl", "j", 0LL);
    CGInst maxResult = CGCreateNewInst ("lmovl", "j", 0x7fffffffffffffffLL);
    CGInst result = CGCreateNewInst ("lslcc", "rcrrr", B, "ufgt",
            maxInt64Inst, maxResult, nanResult);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, result);

    CGBindLabel (endLabel);
}

void dvmCompilerPcgTranslateFloatToDouble (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movss1", 4);
    CGInst cvt = CGCreateNewInst ("cvtss2sd1", "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movsd1", 8, cvt);
}

void dvmCompilerPcgTranslateDoubleToFloat (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movsd1", 8);
    CGInst cvt = CGCreateNewInst ("cvtsd2ss1", "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movss1", 4, cvt);
}

void dvmCompilerPcgTranslateNegFloat (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    __attribute__ ( (aligned (16))) static u4 sign[4] = {0x80000000, 0, 0, 0};

    // This method of just reading sign from the above static variable is
    // probably not the best idea, but it should work for now.
    // TODO Revisit this implementation.
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movss1", 4);
    CGAddr signAddr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
            CGSymbolInvalid, (int32_t)&sign);
    CGInst neg = CGCreateNewInst ("xorps1", "rm", B, signAddr, 16, (void*)1);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movss1", 4, neg);
}

void dvmCompilerPcgTranslateNegDouble (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    __attribute__ ( (aligned (16))) static u8 sign[2] = {0x8000000000000000, 0};

    // This method of just reading sign from the above static variable is
    // probably not the best idea, but it should work for now.
    // TODO Revisit this implementation.
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "movsd1", 8);
    CGAddr signAddr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0,
            CGSymbolInvalid, (int32_t)&sign);
    CGInst neg = CGCreateNewInst ("xorpd1", "rm", B, signAddr, 16, (void*)1);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "movsd1", 8, neg);
}

void dvmCompilerPcgTranslateIntOpOp (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //TODO: We should handle correctly the case of 2addr but they don't exist in the general case anymore
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int vA = ssaRep->defs[0];
    int vB = ssaRep->uses[0];
    int vC = ssaRep->uses[1];

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, vB, "mov", 4);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, vC, "mov", 4);

    CGInst op = CGCreateNewInst (opcode, "rr", B, C);

    dvmCompilerPcgSetVirtualReg (cUnit, vA, "mov", 4, op);
}

void dvmCompilerPcgTranslateIntOpLit (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int vA = ssaRep->defs[0];
    int vB = ssaRep->uses[0];
    int literal = mir->dalvikInsn.vC;

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, vB, "mov", 4);
    CGInst op = CGCreateNewInst (opcode, "ri", B, literal);
    dvmCompilerPcgSetVirtualReg (cUnit, vA, "mov", 4, op);
}

void dvmCompilerPcgTranslateRsub (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    s4 literal = mir->dalvikInsn.vC;
    CGInst A = CGCreateNewInst ("mov", "i", literal);
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst sub = CGCreateNewInst ("sub", "rr", A, B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, sub);
}

void dvmCompilerPcgTranslateIntOp (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    int def = ssaRep->defs[0];
    int use = ssaRep->uses[0];

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, use, "mov", 4);
    CGInst op = CGCreateNewInst (opcode, "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, def, "mov", 4, op);
}

void dvmCompilerPcgTranslateIntExtend (CompilationUnitPCG *cUnit, MIR *mir, const char *opcode, int imm)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst sext = CGCreateNewInst (opcode, "ri", B, imm);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, sext);
}

void dvmCompilerPcgTranslateIntToLong (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst lsext = CGCreateNewInst ("lsext", "ri", B, 32);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, lsext);
}

void dvmCompilerPcgTranslateLongToInt (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "lmov", 8);
    CGInst xtract = CGCreateNewInst ("xtract", "r", B);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, xtract);
}

void dvmCompilerPcgTranslateCmpLong (CompilationUnitPCG *cUnit, MIR *mir)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    CGLabel negOneLabel = CGCreateLabel ();
    CGLabel oneLabel = CGCreateLabel ();
    CGLabel endLabel = CGCreateLabel ();
    CGTemp resultTemp = cUnit->getCurrentTemporaryVR (true);

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "lmov", 8);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "lmov", 8);
    CGCreateNewInst ("lcjcc", "rcrbp", B, "slt", C, negOneLabel, 40);
    CGCreateNewInst ("lcjcc", "rcrbp", B, "sgt", C, oneLabel, 60);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGAddTempDef (resultTemp, zero);
    CGCreateNewInst ("jmp", "b", endLabel);
    CGBindLabel (oneLabel);
    CGInst one = CGCreateNewInst ("mov", "i", 1);
    CGAddTempDef (resultTemp, one);
    CGCreateNewInst ("jmp", "b", endLabel);
    CGBindLabel (negOneLabel);
    CGInst negOne = CGCreateNewInst ("mov", "i", -1);
    CGAddTempDef (resultTemp, negOne);
    CGBindLabel (endLabel);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));
}

void dvmCompilerPcgTranslateDivRemInt (CompilationUnitPCG *cUnit, MIR *mir)
{
    //TODO: We should handle correctly the case of 2addr but they don't exist in the general case anymore
    const char *opcode;
    const char *opcodeB;
    const char *opcodeW;
    const char *opcodeMove;
    const char *divbRes;
    u4 specialResult;  // This is the defined result for 0x80000000 / -1
    bool skipZeroNumDiv = false;

    Opcode dalvikOpCode = mir->dalvikInsn.opcode;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);


    if (dalvikOpCode == OP_DIV_INT || dalvikOpCode == OP_DIV_INT_2ADDR)
    {
        opcode = "idiv";
        opcodeB = "divb";
        opcodeW = "divw";
        opcodeMove = "bmov";
        divbRes = "al";
        specialResult = 0x80000000;
        skipZeroNumDiv = true;
    }
    else
    {
        opcode = "irem";
        opcodeB = "remb";
        opcodeW = "remw";
        opcodeMove = "mov";
        divbRes = "eax";
        specialResult = 0;
    }

    // The division expansion is fairly complex.  It implements the following
    // logic.
    // if (C == 0) {
    //     throwDivideByZero ();
    // }
    // else if (B == 0) { // This check is only performed for division, not remainder
    //      r = 0;
    // }
    // else if ( ( (B | C) & 0xffffff00) == 0) {
    //     r = B [byte /] C
    // }
    // else if ( ( (B | C) & 0xffff0000) == 0) {
    //     r = B [word /] C
    // }
    // else if (C == -1 && B == 0x80000000) {
    //     r = specialResult;
    // }
    // else {
    //     r = B / C;
    // }
    CGLabel zeroCheckOk = CGCreateLabel ();
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);


    u8 value;
    int v2;

    // Get the divisor
    if (dalvikOpCode == OP_DIV_INT || dalvikOpCode == OP_REM_INT)
    {
        v2 = mir->dalvikInsn.vC;
    }
    else
    {
        v2 = mir->dalvikInsn.vB;
    }

    // Get constant info with method context, value is passed as reference to return constant value of the v2
    ConstVRType isConstVRContext = dvmCompilerGetConstValueOfVR(const_cast<MIR*>(mir), v2, value);

    // if VR is recognized as a non-wide constant with method context,
    // load the constant using a mov to let PCG backend utilize the constant info
    // to do the optimization for div/rem operation when divisor is constant.
    if (isConstVRContext == kVRNonWideConst)
    {
        C = CGCreateNewInst ("mov", "i", value);
    }

    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst ("cjcc", "rcrbp", C, "ne", zero, zeroCheckOk, 100);

    // Divide by zero.  Issue an error.
    dvmCompilerPcgGenerateRaiseException (cUnit);

    CGBindLabel (zeroCheckOk);

    CGTemp resultTemp = cUnit->getCurrentTemporaryVR (true);
    CGLabel codeEnd = CGCreateLabel ();
    CGLabel specialLabel_1 = CGCreateLabel ();
    CGLabel specialLabel_2 = CGCreateLabel ();
    CGLabel specialLabel_3 = CGCreateLabel ();
    CGLabel divLabel = CGCreateLabel ();

    if (skipZeroNumDiv == true)
    {
        // Short circuit check for zero numerator.
        CGLabel numer_zeroCheckOk = CGCreateLabel ();
        CGCreateNewInst ("cjcc", "rcrbp", B, "ne", zero, numer_zeroCheckOk, 99);
        CGAddTempDef (resultTemp, zero);
        CGCreateNewInst ("jmp", "b", codeEnd);
        CGBindLabel (numer_zeroCheckOk);
    }

    // if VR is recognized as a non-wide constant with method context
    if (isConstVRContext == kVRNonWideConst)
    {
        if ((s8)value == -1)
        {
            CGInst minInt = CGCreateNewInst ("mov", "i", INT_MIN);
            CGCreateNewInst ("cjcc", "rcrbp", B, "ne", minInt, divLabel, 99);
            CGInst specialVal = CGCreateNewInst ("mov", "i", specialResult);
            CGAddTempDef (resultTemp, specialVal);
            CGCreateNewInst ("jmp", "b", codeEnd);
        }
        CGBindLabel (divLabel);
        CGInst div = CGCreateNewInst (opcode, "rr", B, C);
        CGAddTempDef (resultTemp, div);

        CGBindLabel (codeEnd);
        dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));
        return;
    }

    CGInst orVal = CGCreateNewInst ("or", "rr", B, C);
    CGInst andVal = CGCreateNewInst ("and", "ri", orVal, 0xffffff00);
    CGCreateNewInst ("cjcc", "rcrb", andVal, "ne", zero, specialLabel_1);

    // Do byte div/rem
    CGInst bext = CGCreateNewInst ("bxtract", "r", C);
    CGInst movEax = CGCreateNewInst ("mov", "r", B);
    CGSetRreg (movEax, "eax");
    CGInst divb = CGCreateNewInst (opcodeB, "rr", movEax, bext);
    CGSetRreg (divb, divbRes);
    divb = CGCreateNewInst(opcodeMove, "r", divb);
    if (dalvikOpCode == OP_REM_INT || dalvikOpCode == OP_REM_INT_2ADDR) {
        divb = CGCreateNewInst ("shri", "ri", divb, 8);
    }
    else {
        divb = CGCreateNewInst ("zext", "ri", divb, 24);
    }
    CGAddTempDef (resultTemp, divb);
    CGCreateNewInst ("jmp", "b", codeEnd);

    // Not byte.. Check for word
    CGBindLabel(specialLabel_1);
    andVal = CGCreateNewInst("and", "ri", orVal, 0xffff0000);
    CGCreateNewInst("cjcc", "rcrb", andVal, "ne", zero, specialLabel_3);

    // Do word div/rem
    // This will look similar to the dword version, except for the
    // opcode. This is because PCG doesn't support WORDreg results.
    CGInst div = CGCreateNewInst(opcodeW, "rr", B, C);
    div = CGCreateNewInst("zext", "ri", div, 16);
    CGAddTempDef(resultTemp, div);
    CGCreateNewInst("jmp", "b", codeEnd);

    CGBindLabel(specialLabel_3);
    // Not byte, and not word.. Do dword
    CGInst negOne = CGCreateNewInst ("mov", "i", -1);
    CGCreateNewInst ("cjcc", "rcrb", C, "eq", negOne, specialLabel_2);

    CGBindLabel (divLabel);
    div = CGCreateNewInst (opcode, "rr", B, C);
    CGAddTempDef (resultTemp, div);
    CGCreateNewInst ("jmp", "b", codeEnd);

    CGBindLabel (specialLabel_2);
    CGInst minInt = CGCreateNewInst ("mov", "i", 0x80000000);
    CGCreateNewInst ("cjcc", "rcrb", B, "ne", minInt, divLabel);
    CGInst specialVal = CGCreateNewInst ("mov", "i", specialResult);
    CGAddTempDef (resultTemp, specialVal);

    CGBindLabel (codeEnd);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, CGGetTempUseInst (resultTemp));
}

void dvmCompilerPcgTranslateDivRemIntLit (CompilationUnitPCG *cUnit, MIR *mir)
{
    const char *opcode;
    u4 specialResult;  // This is the defined result for 0x80000000 / -1
    int dalvikOpCode = mir->dalvikInsn.opcode;
    int divisor = mir->dalvikInsn.vC;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (dalvikOpCode == OP_DIV_INT_LIT8 || dalvikOpCode == OP_DIV_INT_LIT16)
    {
        opcode = "idiv";
        specialResult = 0x80000000;
    }
    else
    {
        opcode = "irem";
        specialResult = 0;
    }

    if (divisor == 0)
    {
        // Divide by zero.  Issue an error.
        // Generates check '0 != 0'. Note, this check and the code after this check
        // should be deleted by PCG during code generation.
        CGLabel zeroCheckOk = CGCreateLabel ();
        CGInst zero = CGCreateNewInst ("mov", "i", 0);
        CGCreateNewInst ("cjcc", "rcrbp", zero, "ne", zero, zeroCheckOk, 100);
        dvmCompilerPcgGenerateRaiseException (cUnit);
        CGBindLabel (zeroCheckOk);
    }

    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGLabel codeEnd, specialLabel, divLabel;
    CGTemp resultTemp;

    if (divisor == -1)
    {
        // The labels and temp are only needed for the divisor == -1 case.
        // The generated code implements this logic:
        // if (B == 0x8000000) {
        //     r = specialResult;
        // }
        // else {
        //     r = B / divisor;
        // }
        specialLabel = CGCreateLabel ();
        divLabel = CGCreateLabel ();
        codeEnd = CGCreateLabel ();
        resultTemp = cUnit->getCurrentTemporaryVR (true);

        CGInst minInt = CGCreateNewInst ("mov", "i", 0x80000000);
        CGCreateNewInst ("cjcc", "rcrb", B, "eq", minInt, specialLabel);
        CGBindLabel (divLabel);
    }

    CGInst C = CGCreateNewInst ("mov", "i", divisor);
    CGInst divrem = CGCreateNewInst (opcode, "rr", B, C);

    if (divisor == -1)
    {
        //TODO Seems like resultTemp can be used but not defined here
        CGAddTempDef (resultTemp, divrem);
        CGCreateNewInst ("jmp", "b", codeEnd);

        CGBindLabel (specialLabel);
        CGInst specialVal = CGCreateNewInst ("mov", "i", specialResult);
        CGAddTempDef (resultTemp, specialVal);

        divrem = CGGetTempUseInst (resultTemp);

        CGBindLabel (codeEnd);
    }

    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, divrem);
}

void dvmCompilerPcgTranslateDivRemLong (CompilationUnitPCG *cUnit, MIR *mir)
{
    //TODO: We should handle correctly the case of 2addr but they don't exist in the general case anymore
    const char *opcode;

    int dalvikOpCode = mir->dalvikInsn.opcode;

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (dalvikOpCode == OP_DIV_LONG || dalvikOpCode == OP_DIV_LONG_2ADDR)
    {
        opcode = "lidiv";
    }
    else
    {
        opcode = "lirem";
    }

    // The long division expansion is simpler than the int one.  I suspect
    // that is because the library routines handle the special denominator
    // value of -1.  This is the logic:
    //
    // if (C == 0) {
    //     throwDivideByZero ();
    // }
    // else {
    //     r = B / C;
    // }

    CGLabel zeroCheckOk = CGCreateLabel ();
    CGInst B = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "lmov", 8);
    CGInst C = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[2], "lmov", 8);
    CGInst zero = CGCreateNewInst ("lmov", "i", 0);
    CGCreateNewInst ("lcjcc", "rcrbp", C, "ne", zero, zeroCheckOk, 100);

    // Divide by zero.  Issue an error.
    dvmCompilerPcgGenerateRaiseException (cUnit);

    CGBindLabel (zeroCheckOk);
    CGInst div = CGCreateNewInst (opcode, "rr", B, C);
    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "lmov", 8, div);
}
