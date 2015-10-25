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
#include "CompilerIR.h"
#include "Dataflow.h"
#include "LowerExtended.h"
#include "LowerMemory.h"
#include "UtilityPCG.h"
#include "LowerJump.h"

/*
 * uses[0] = idxReg;
 * vB = minC;
 */
void dvmCompilerPcgTranslateLowerBoundCheck (CompilationUnitPCG *cUnit, MIR *mir)
{
    // Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    CGInst arrayIndex = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst minC = CGCreateNewInst ("movi", "i", -mir->dalvikInsn.vB);
    CGLabel boundCheckPassedLabel = CGCreateLabel ();
    CGCreateNewInst ("cjcc", "rcrbp", arrayIndex, "sge", minC, boundCheckPassedLabel, 100);
    dvmCompilerPcgGenerateRaiseException (cUnit);
    CGBindLabel (boundCheckPassedLabel);
}

/**
 * uses[0] arrayReg
 * arg[0] -> determines whether it is a constant or a register
 * arg[1] -> constant, if applicable
 * uses[1] indexReg, if applicable
 *
 * Generate code to check idx < 0 || idx >= array.length.
 */
void dvmCompilerPcgTranslateBoundCheck (CompilationUnitPCG *cUnit, MIR *mir)
{
    // Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    CGInst array = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst index;

    if (mir->dalvikInsn.arg[0] == MIR_BOUND_CHECK_REG) {
        index = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);
    }
    else {
        index = CGCreateNewInst ("mov", "i", mir->dalvikInsn.arg[1]);
    }

    CGLabel upperBoundCheckPassedLabel = CGCreateLabel ();
    CGLabel checkFailedLabel = CGCreateLabel ();

    CGInst arrayLength = dvmCompilerPcgCreateSimpleLoad (array, OFFSETOF_MEMBER(ArrayObject, length));
    CGCreateNewInst("cjcc", "rcrbp", index, "slt", arrayLength, upperBoundCheckPassedLabel, 100);
    CGBindLabel(checkFailedLabel);
    dvmCompilerPcgGenerateRaiseException (cUnit);

    CGBindLabel (upperBoundCheckPassedLabel);
    CGInst zero = CGCreateNewInst ("mov", "i", 0);
    CGCreateNewInst("cjcc", "rcrbp", index, "slt", zero, checkFailedLabel, 0);
}

void dvmCompilerPcgTranslateNullCheck (CompilationUnitPCG *cUnit, MIR *mir)
{
    // Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    CGInst object = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    dvmCompilerPcgGenerateNullCheck (cUnit, object, mir);
}

/*
 * uses[0] = arrayReg;
 * uses[1] = indexReg;
 * arg[0] = maxC
 * arg[1] = minC
 * arg[2] = loopBranchConditionCode
 */
void dvmCompilerPcgTranslateLoopChecks (CompilationUnitPCG *cUnit, MIR *mir, bool countUp)
{
    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);
    CGLabel nullCheckPassedLabel = CGCreateLabel ();
    CGLabel checkFailedLabel = CGCreateLabel ();

    CGInst array = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    CGInst zero = CGCreateNewInst ("movi", "i", 0);

    // Generate the null check explicitly rather than calling dvmCompilerPcgGenerateNullCheck
    // to do it.  That way, we can reuse the block that raises the exception.

    CGCreateNewInst ("cjcc", "rcrbp", array, "ne", zero, nullCheckPassedLabel, 100);
    CGBindLabel (checkFailedLabel);
    dvmCompilerPcgGenerateRaiseException (cUnit);
    CGBindLabel (nullCheckPassedLabel);

    CGInst startIndex = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[1], "mov", 4);
    int maxC = mir->dalvikInsn.arg[0];
    // If the loop end condition is ">=" instead of ">", then the largest value
    // of the index is "endCondition - 1".
    if (countUp && mir->dalvikInsn.arg[2] == OP_IF_GE) {
        maxC--;
    }
    CGInst endIndex = CGCreateNewInst ("add", "ri", startIndex, maxC);
    CGInst arrayLength = dvmCompilerPcgCreateSimpleLoad (array, OFFSETOF_MEMBER(ArrayObject, length));
    CGCreateNewInst ("cjcc", "rcrbp", endIndex, "uge", arrayLength, checkFailedLabel, 0);
}

void dvmCompilerPcgTranslatePredictionInlineCheck (CompilationUnitPCG *cUnit, MIR *mir)
{
    //This function should only be called when generating inline prediction
    assert (static_cast<ExtendedMIROpcode> (mir->dalvikInsn.opcode) == kMirOpCheckInlinePrediction);

    BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

    //Paranoid
    assert (bb != 0);

    //Instruction has conditional branching semantics so it should be block ending
    assert (mir->next == 0 && bb->lastMIRInsn == mir && bb->fallThrough != 0 && bb->taken != 0);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    //Get the "this" pointer
    CGInst thisPtr = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    //Check it for null
    dvmCompilerPcgGenerateNullCheck (cUnit, thisPtr, mir, ssaRep->uses[0]);

    //The class literal is in vB
    CGInst clazzLiteral = CGCreateNewInst ("mov", "i", mir->dalvikInsn.vB);

    //Get the class from "this"
    CGInst clazz = dvmCompilerPcgCreateSimpleLoad (thisPtr, OFFSETOF_MEMBER (Object, clazz));

    //We take the taken branch if the class of this doesn't match our expected class
    dvmCompilerPcgTranslateConditionalJump (bb, clazz, "ne", clazzLiteral);
}

void dvmCompilerPcgTranslateCheckStackOverflow (CompilationUnitPCG *cUnit, MIR *mir)
{
    assert (static_cast<ExtendedMIROpcode> (mir->dalvikInsn.opcode) == kMirOpCheckStackOverflow);

    //vB holds the size of space of frame needed relative to frame pointer
    int spaceNeeded = mir->dalvikInsn.vB;

    //Stack grows in negative direction so subtract the size from the frame pointer
    CGInst stackUsedEnd = CGCreateNewInst ("sub", "ri", cUnit->getVMPtr(), spaceNeeded);

    //Obtain the self pointer
    CGInst selfPtr = dvmCompilerPcgGetSelfPointer (cUnit);

    //Create label for case when we don't overflow
    CGLabel noOverflow = CGCreateLabel ();

    //Load the interpStackEnd from thread
    CGInst interpStackEnd = dvmCompilerPcgCreateSimpleLoad (selfPtr, OFFSETOF_MEMBER (Thread, interpStackEnd));

    //If not below or equal, then we do not overflow. Overflowing is a rare condition.
    CGCreateNewInst ("cjcc", "rcrbp", stackUsedEnd, "ugt", interpStackEnd, noOverflow, 100);

    //Now generate an exception if we overflow so we can punt
    dvmCompilerPcgGenerateRaiseException (cUnit);

    //Bind label so we can get here when we don't take the overflow path
    CGBindLabel (noOverflow);
}

void dvmCompilerPcgTranslatePackedSet (CompilationUnitPCG *cUnit, MIR *mir)
{
    int vecUnitSize = mir->dalvikInsn.vC;
    int destXMM = mir->dalvikInsn.vA;
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);
    CGInst val = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);

    // Broadcast val into an XMM register
    val = CGCreateNewInst ("emovdfi", "r", val);
    if (vecUnitSize == 2)
    {
        // PCG really ought to have a utility opcode for pbroadcastwx.  For the
        // time being, unpack the word to dword and use pbroadcastdx.
        val = CGCreateNewInst ("epunpcklwd", "rr", val, val);
    }
    val = CGCreateNewInst ("pbroadcastdx", "r", val);

    dvmCompilerPcgSetXMMReg (cUnit, destXMM, val);
}

void dvmCompilerPcgTranslatePackedConst (CompilationUnitPCG *cUnit, MIR *mir)
{
    int destXMM = mir->dalvikInsn.vA;
    int constVal[4];
    CGInst val;

    // The constant value is stored in reverse order in the MIR.
    constVal[0] = mir->dalvikInsn.arg[3];
    constVal[1] = mir->dalvikInsn.arg[2];
    constVal[2] = mir->dalvikInsn.arg[1];
    constVal[3] = mir->dalvikInsn.arg[0];

    // Check for 0 as a special case.  PCG could do this conversion for us,
    // but we would like to avoid generation of the memory constant as well.
    //
    // TODO: In the future, we could handle this in the same way that we
    //       handle unreferenced chaining cells.  That is, we can always create
    //       the memConst here, let PCG optimize away special values like 0,
    //       and then look for references to each memory constant in the
    //       PCG-generated code and only generate constants that are referenced.
    //
    if (constVal[0] == 0 && constVal[1] == 0 && constVal[2] == 0 && constVal[3] == 0)
    {
        val = CGCreateNewInst ("epzero", "");
    }
    else
    {
        // Create the constant in memory
        CGSymbol memConstSymbol = cUnit->getMemConstSymbol ( (uint8_t*) constVal, 16, 16);
        CGAddr addr = CGCreateAddr (CGInstInvalid, CGInstInvalid, 0, memConstSymbol, 0);
        val = CGCreateNewInst ("ldps", "m", addr, 16, (void*)1);
    }
    dvmCompilerPcgSetXMMReg (cUnit, destXMM, val);
}

void dvmCompilerPcgTranslatePackedMove (CompilationUnitPCG *cUnit, MIR *mir)
{
    int sourceXMM = mir->dalvikInsn.vB;
    int destXMM = mir->dalvikInsn.vA;

    CGInst src = dvmCompilerPcgGetXMMReg (cUnit, sourceXMM);
    CGInst copy = CGCreateNewInst ("movps", "r", src);
    dvmCompilerPcgSetXMMReg (cUnit, destXMM, copy);
}

/**
 * @brief Used to obtain the opcode string for vector extended opcodes
 * @param opcode The extended vector opcode
 * @param vecUnitSize The unit size of vector operations
 * @return Returns the string if one can be determined. Otherwise returns 0.
 */
static const char *getPcgOpcodeForPackedExtendedOp (int opcode, int vecUnitSize)
{
    //For bitwise operations, we do not care about the vector unit size
    switch (opcode)
    {
        case kMirOpPackedXor:
            return "epxor";
        case kMirOpPackedOr:
            return "epor";
        case kMirOpPackedAnd:
            return "epand";
        default:
            break;
    }

    if (vecUnitSize == 2)
    {
        switch (opcode)
        {
            case kMirOpPackedMultiply:
                return "epmullw";
            case kMirOpPackedAddition:
                return "epaddw";
            case kMirOpPackedSubtract:
                return "epsubw";
            case kMirOpPackedShiftLeft:
                return "epsllwi";
            case kMirOpPackedSignedShiftRight:
                return "epsrawi";
            case kMirOpPackedUnsignedShiftRight:
                return "epsrlwi";
            default:
                break;
        }
    }
    else if (vecUnitSize == 4)
    {
        switch (opcode)
        {
            case kMirOpPackedMultiply:
                return "epmulldx";
            case kMirOpPackedAddition:
                return "epaddd";
            case kMirOpPackedSubtract:
                return "epsubd";
            case kMirOpPackedShiftLeft:
                return "epslldi";
            case kMirOpPackedSignedShiftRight:
                return "epsradi";
            case kMirOpPackedUnsignedShiftRight:
                return "epsrldi";
            default:
                break;
        }
    }

    //If we get here, we do not know what opcode to use
    return 0;
}

bool dvmCompilerTranslatePackedAlu (CompilationUnitPCG *cUnit, MIR *mir)
{
    int vecUnitSize = mir->dalvikInsn.vC;
    const char *pcgOpcode = getPcgOpcodeForPackedExtendedOp (mir->dalvikInsn.opcode, vecUnitSize);

    if (pcgOpcode == 0)
    {
        ALOGD ("JIT_INFO: Could not find opcode string for extended MIR %x", mir->dalvikInsn.opcode);
        return false;
    }

    int sourceXMM = mir->dalvikInsn.vB;
    int destXMM = mir->dalvikInsn.vA;
    CGInst op1 = dvmCompilerPcgGetXMMReg (cUnit, destXMM);
    CGInst op2 = dvmCompilerPcgGetXMMReg (cUnit, sourceXMM);
    CGInst inst = CGCreateNewInst (pcgOpcode, "rr", op1, op2);
    dvmCompilerPcgSetXMMReg (cUnit, destXMM, inst);

    return true;
}

void dvmCompilerPcgTranslatePackedAddReduce (CompilationUnitPCG *cUnit, MIR *mir)
{
    SSARepresentation *ssaRep = mir->ssaRep;
    assert (ssaRep != 0);

    int sourceXMM = mir->dalvikInsn.vB;
    int vecUnitSize = mir->dalvikInsn.vC;
    const int vectorBytes = 16;
    int vecElems = vectorBytes / vecUnitSize;
    CGInst sum = dvmCompilerPcgGetXMMReg (cUnit, sourceXMM);
    const char *pcgOpcode;

    if (vecUnitSize == 2)
    {
        pcgOpcode = "ephaddw";
    }
    else
    {
        pcgOpcode = "ephaddd";
    }

    while (vecElems > 1)
    {
        sum = CGCreateNewInst (pcgOpcode, "rr", sum, sum);
        vecElems >>= 1;
    }

    sum = CGCreateNewInst ("emovdti", "r", sum);
    if (vecUnitSize == 2)
    {
        sum = CGCreateNewInst ("zext", "ri", sum, 16);
    }

    CGInst origVR = dvmCompilerPcgGetVirtualReg (cUnit, ssaRep->uses[0], "mov", 4);
    sum = CGCreateNewInst ("add", "rr", sum, origVR);

    dvmCompilerPcgSetVirtualReg (cUnit, ssaRep->defs[0], "mov", 4, sum);
}
