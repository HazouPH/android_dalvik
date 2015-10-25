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
#include "CodeGeneration.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "Labels.h"
#include "LowerMemory.h"
#include "LowerOther.h"
#include "PassDriver.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "UtilityPCG.h"
#include "BitVector.h"
#include "Utility.h"
#include "JitVerbose.h"
#include "Lower.h"

/**
 * @brief Get a call and register from a type
 * @param cUnit the CompilationUnitPCG
 * @param dtype the pcgDtype
 * @param rregPtr the register to use
 * @return the call opcode
 */
static const char *getCallOpcodeAndRreg (CompilationUnitPCG *cUnit, pcgDtype dtype, const char **rregPtr)
{
    switch (dtype)
    {
        case INTreg:
            *rregPtr = "eax";
            return "icall";

        case LLreg:
            *rregPtr = "eax";
            return "lcall";

        case NOreg:
            *rregPtr = "";
            return "call";

        case FPreg32:
            *rregPtr = "st";
            return "f32call";

        case FPreg64:
            *rregPtr = "st";
            return "f64call";

        default:
            break;
    }

    // We can add support for FP results, but they aren't currently needed, so just give an error here.
    ALOGE ("PCG Error: Unsupported call dtype");
    assert(0);
    cUnit->errorHandler->setError (kJitErrorPcgUnsupportedCallDataType);
    *rregPtr = "";
    return "";
}

int32_t dvmCompilerPcgGetOpcodeAndSizeForDtype (CompilationUnitPCG *cUnit, pcgDtype dtype, const char **opcPtr)
{
    int32_t size;
    const char *pcgOpcode;

    switch (dtype)
    {
        case INTreg:
            pcgOpcode = "mov";
            size = 4;
            break;

        case LLreg:
            pcgOpcode = "lmov";
            size = 8;
            break;

        case VXreg32:
            pcgOpcode = "movss1";
            size = 4;
            break;

        case DPVXreg64:
            pcgOpcode = "movsd1";
            size = 8;
            break;

        case FPreg32:
            pcgOpcode = "f32mov";
            size = 4;
            break;

        case FPreg64:
            pcgOpcode = "f64mov";
            size = 8;
            break;

        default:
            ALOGE ("\n+++ PCG ERROR +++ Unexpected data type seen : %d.", dtype);
            assert (0);
            cUnit->errorHandler->setError (kJitErrorPcgUnexpectedDataType);
            return -1;
    }

    *opcPtr = pcgOpcode;
    return size;
}

pcgDtype dvmCompilerPcgApplyDefaultDtype (pcgDtype dtype, int32_t size)
{
    if (dtype != NOreg)
    {
        return dtype;
    }

    return (size == 8) ? DPVXreg64 : INTreg;
}

pcgDtype dvmCompilerPcgGetDtypeForSSANum (CompilationUnitPCG *cUnit, int ssaNum)
{
    //Get information
    SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

    return info.dtype;
}

pcgDtype dvmCompilerPcgGetDtypeForSSANum (CompilationUnitPCG *cUnit, int ssaNum, SSANumInfo &info)
{
    int parentSSANum = info.parentSSANum;

    if (ssaNum != parentSSANum)
    {
        //TODO: the original code did a find without checking so this is semantically different
        //TODO: figure out if we care or use the safe version and add a check...
        SSANumInfo &parent = cUnit->getRootSSANumInformation (parentSSANum);

        return parent.dtype;
    }

    return info.dtype;
}

void dvmCompilerPcgSetDtypeForSSANum (CompilationUnitPCG *cUnit, int ssaNum, pcgDtype dtype)
{
    // Typing is needed for registerization and we must be type consistent,
    // thus we update the data type just of parent.
    SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

    info.dtype = dtype;
}

CGInst dvmCompilerPcgGetVirtualReg (CompilationUnitPCG *cUnit, int ssaNum, const char *pcgOpcode, uint32_t loadSize)
{
    // Get the root SSA number information, i.e. the information associated
    // with the CGTemp.
    SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

    //TODO handle dynamic_cast better
    int dalvikReg = dvmConvertSSARegToDalvik (dynamic_cast<CompilationUnit *> (cUnit), ssaNum);

    if (info.registerize == true)
    {
        return CGGetTempUseInst (info.parentSSANum);
    }

    u2 virtualReg = DECODE_REG (dalvikReg);

    void *handle = dvmCompilerPcgGetVRHandle (virtualReg, loadSize);
    int vrOffset = dvmCompilerPcgGetVROffsetRelativeToVMPtr (cUnit, virtualReg);
    CGAddr addr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid, vrOffset);

    return CGCreateNewInst (pcgOpcode, "m", addr, loadSize, handle);
}

void dvmCompilerPcgSetVirtualReg (CompilationUnitPCG *cUnit, int ssaNum, const char *pcgOpcode,
                             uint32_t storeSize, CGInst storeVal)
{
    // Get the root SSA number information, i.e. the information associated
    // with the CGTemp.
    SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

    if (info.registerize == true)
    {
        pcgDtype regDtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
        dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, regDtype, &pcgOpcode);
        CGInst copy = CGCreateNewInst (pcgOpcode, "r", storeVal);

        CGAddTempDef (info.parentSSANum, copy);

        if (info.deferWriteback == true)
        {
            return;
        }
    }

    u2 virtualReg = dvmExtractSSARegister (cUnit, ssaNum);

    void *handle = dvmCompilerPcgGetVRHandle (virtualReg, storeSize);
    int vrOffset = dvmCompilerPcgGetVROffsetRelativeToVMPtr (cUnit, virtualReg);
    CGAddr addr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid, vrOffset);

    CGCreateNewInst (pcgOpcode, "mr", addr, storeSize, handle, storeVal);
}

CGInst dvmCompilerPcgGetXMMReg (CompilationUnitPCG *cUnit, int xmmNum)
{
    CGTemp xmmTemp = cUnit->getCGTempForXMM (xmmNum);
    return CGGetTempUseInst (xmmTemp);
}

void dvmCompilerPcgSetXMMReg (CompilationUnitPCG *cUnit, int xmmNum, CGInst val)
{
    CGTemp xmmTemp = cUnit->getCGTempForXMM (xmmNum);
    CGAddTempDef (xmmTemp, val);
}

void* dvmCompilerPcgGetVRHandle (u2 virtualReg, uint32_t size)
{
    // Start with a non-zero handle, because PCG assigns a special meaning to a null handle.
    intptr_t handle = 0x2;

    if (size == 8)
    {
        handle |= 0x4;
    }
    handle |= virtualReg << 3;

    return (void*)handle;
}

bool dvmCompilerPcgBlockEndsInInvoke (BasicBlockPCG *bb)
{
    MIR *mir = bb->lastMIRInsn;

    //If no instruction, we are done
    if (mir == 0)
    {
        return false;
    }

    //Get the opcode
    int opcode = mir->dalvikInsn.opcode;

    //Get the flags
    int flags = dvmCompilerGetOpcodeFlags (opcode);

    if ((flags & kInstrInvoke) == 0) {
        return false;
    }

    if ( (mir->OptimizationFlags & MIR_INLINED) != 0) {
        return false;
    }

    return true;
}

void dvmCompilerPcgGetBlockName (BasicBlockPCG *bb, std::string &label)
{
    //Create a unique ID number
    static int id = 0;

    const char *baseName = 0;

    int idToEmit = (bb == 0) ? id : bb->id;

    int blockType = (bb == 0) ? kDalvikByteCode : bb->blockType;

    //Clear label
    label = "";

    switch (blockType)
    {
        case kChainingCellNormal:
            baseName = "normalChainingCell";
            break;

        case kChainingCellInvokePredicted:
            baseName = "invokePredictedChainingCell";
            break;

        case kChainingCellInvokeSingleton:
            baseName = "invokeSingletonChainingCell";
            break;

        case kChainingCellHot:
            baseName = "hotChainingCell";
            break;

        case kChainingCellBackwardBranch:
            baseName = "backwardBranchChainingCell";
            break;

        case kPreBackwardBlock:
            baseName = "preBackwardBlock";
            break;

        default:
            baseName = "dalvikBlock_CL";
            idToEmit = static_cast<int> ( (bb == 0) ? idToEmit : bb->cgLabel);
            break;
    }

    assert (baseName != 0);

    //Create the string
    char buffer[1024];
    snprintf (buffer, sizeof (buffer), "%s%d_%d", baseName, idToEmit, id);

    //Increment counter
    id++;

    //Create label
    label = buffer;
}

/**
 * @brief Dump a bitvector using SSA for the index
 * @param cUnit the CompilationUnit
 * @param bv the BitVector
 * @param n the maximum index we want to dump with
 */
static void dumpBitVector (CompilationUnit *cUnit, BitVector *bv, int n)
{
    char buffer[512];
    std::string s = "{ ";
    for (int i = 0; i < n; i ++)
    {
        if (dvmIsBitSet (bv, i) == true)
        {
            int dalvikReg = dvmConvertSSARegToDalvik (cUnit, i);

            //Decode the SSA register
            u2 vrNum = DECODE_REG (dalvikReg);
            u2 vrSub = DECODE_SUB (dalvikReg);

            //Get the numbers in a char*
            snprintf (buffer, sizeof (buffer), "%d_%d ", vrNum, vrSub);

            //Append it
            s += buffer;
        }
    }

    s += "}";

    ALOGI ("%s", s.c_str ());
}

void dvmCompilerPcgDumpModRegInfo (CompilationUnitPCG *cUnit)
{
    ALOGI ("\nModified VR info for trace %d\n", cUnit->getTraceID ());
    ALOGI ("===============================\n");
    GrowableList *blockList = &cUnit->blockList;

    for (unsigned int i = 0; i < blockList->numUsed; i++)
    {
        BasicBlockPCG *bb = (BasicBlockPCG *) blockList->elemList[i];

        ALOGI ("Block %d dirtyIns:  ", i);
        dumpBitVector (cUnit, bb->dirtyIns, cUnit->numSSARegs);
        ALOGI ("Block %d availIns:  ", i);
        dumpBitVector (cUnit, bb->availIns, cUnit->numSSARegs);
        for (MIR *mir = bb->firstMIRInsn; mir; mir = mir->next)
        {
            char mybuf[2048];
            dvmCompilerExtendedDisassembler (cUnit, mir, &mir->dalvikInsn, mybuf, sizeof (mybuf));
            ALOGI ("%s\n", mybuf);
        }
        ALOGI ("Block %d dirtyOuts: ", i);
        dumpBitVector (cUnit, bb->dirtyOuts, cUnit->numSSARegs);
        ALOGI ("Block %d availOuts: ", i);
        dumpBitVector (cUnit, bb->availOuts, cUnit->numSSARegs);

        if (bb->taken != 0 || bb->fallThrough != 0)
        {
            ALOGI ("Succs:");
            if (bb->taken != 0)
            {
                BasicBlockPCG *taken = (BasicBlockPCG *) (bb->taken);
                char takenName[BLOCK_NAME_LEN];
                dvmGetBlockName (taken, takenName);

                ALOGI (" T%d%s", bb->taken->id, takenName);
            }
            if (bb->fallThrough != 0) {
                BasicBlockPCG *ft = (BasicBlockPCG *) (bb->fallThrough);

                char ftName[BLOCK_NAME_LEN];
                dvmGetBlockName (ft, ftName);
                ALOGI (" F%d%s", bb->fallThrough->id, ftName);
            }
            ALOGI ("\n");
        }
    }
}

const char* dvmCompilerPcgGetDtypeName (pcgDtype dtype)
{
    static const char *names[] = {"NOreg", "INTreg", "LLreg", "VXreg32", "DPVXreg64", "FPreg32", "FPreg64", "Any", "Any4", "Any8", "LLregHi", "DPVXreg64Hi", "Any8Hi"};

    if (dtype >= MaxType)
    {
        return "InvalidType";
    }

    return names[dtype];
}

bool dvmCompilerPcgIsHighDtype (pcgDtype dtype)
{
    if (dtype == LLregHi || dtype == DPVXreg64Hi || dtype == Any8Hi)
    {
        return true;
    }

    return false;
}

bool dvmCompilerPcgSupportsExtendedOp (int extendedOpcode)
{
    switch (extendedOpcode)
    {
        case kMirOpPhi:
        case kMirOpRegisterize:
        case kMirOpCheckInlinePrediction:
        case kMirOpLowerBound:
        case kMirOpBoundCheck:
        case kMirOpNullCheck:
        case kMirOpNullNRangeUpCheck:
        case kMirOpNullNRangeDownCheck:
        case kMirOpPackedSet:
        case kMirOpConst128b:
        case kMirOpMove128b:
        case kMirOpPackedAddition:
        case kMirOpPackedMultiply:
        case kMirOpPackedSubtract:
        case kMirOpPackedAnd:
        case kMirOpPackedOr:
        case kMirOpPackedXor:
        case kMirOpPackedAddReduce:
        case kMirOpCheckStackOverflow:
            return true;
        default:
            break;
    }

    //If we get here it is not supported
    return false;
}

/**
 * @details Useful interface routine that allows us to selectively use PCG or the existing dalvik JIT.
 */
bool dvmCompilerPcgSupportTrace (CompilationUnit *cUnit)
{
    GrowableList *blockList = &cUnit->blockList;
    bool traceOk = true;
    int usePcg;
    int bytecodeCount = 0;

    //First check if we have a loop
    LoopInformation *info = cUnit->loopInformation;

    if (info != 0)
    {
        int tmp = 0;
        //Let us see what we have in the backend options concerning optimizations
        bool res = dvmExtractBackendOption ("OptimizationLevel", &tmp);

        if (res == true)
        {
            //If the flag is not set, we bail
            if ( (tmp & OptimizationMaskAcceptLoops) == 0)
            {
                return false;
            }
        }
    }

    //Is there an option saying don't use it?
    if (dvmExtractBackendOption ("UsePcg", &usePcg) && usePcg == 0)
    {
        return false;
    }

    for (unsigned int i = 0; i < blockList->numUsed; i++)
    {
        BasicBlockPCG* bb = (BasicBlockPCG *) blockList->elemList[i];

        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            Opcode dalvikOpCode = mir->dalvikInsn.opcode;

            MIR *faultyMIR = 0;

            //Increment the bytecode count
            bytecodeCount++;

            switch (dalvikOpCode)
            {
                case OP_BREAKPOINT:
                case OP_THROW_VERIFICATION_ERROR:
                case OP_INVOKE_OBJECT_INIT_RANGE:
                    // Not yet implemented opcodes
                    faultyMIR = mir;
                    break;

                default:
                    break;
            }

            if (dalvikOpCode >= kNumPackedOpcodes) {
                // Use an opt-in approach for extended MIRs.
                if (dvmCompilerPcgSupportsExtendedOp (dalvikOpCode) == false)
                {
                    faultyMIR = mir;
                }
            }

            if (faultyMIR != 0)
            {
                traceOk = false;

                //TODO: Probably not log in the same way or maybe not always log...
                ALOGI ("\n:::::Not Using PCG for : \n");
                char mybuf[2048];
                dvmCompilerExtendedDisassembler (cUnit, mir, &mir->dalvikInsn, mybuf, sizeof (mybuf));
                ALOGI ("%s\n", mybuf);
            }
        }
    }

    //If the count is over what we really can handle
    int zexpCount = 0;
    if (traceOk == true && dvmExtractBackendOption ("zexpCount", &zexpCount))
    {
        if (bytecodeCount > zexpCount)
        {
            return false;
        }
    }

    //Anyway we have a max authorized
    if (bytecodeCount > JIT_MAX_TRACE_LEN)
    {
        return false;
    }

    //Little debug solution to only compile certain traces
    static int counter = 0;
    int max;
    if (!dvmExtractBackendOption ("Brutus", &max))
    {
        max = -1;
    }

    if (traceOk == true && (max == -1 || counter < max))
    {
        counter++;
        return true;
    }

    ALOGI ("JIT_INFO: Refusing trace: %s - %s - %d\n",
                        cUnit->method->clazz->descriptor, cUnit->method->name,
                        cUnit->entryBlock ? cUnit->entryBlock->startOffset : -1);
    return false;
}

CGInst dvmCompilerPcgGetResClasses (CGInst selfPtr)
{
    CGInst ret = dvmCompilerPcgCreateSimpleLoad (selfPtr, offsetof (Thread, interpSave.methodClassDex));
    ret = dvmCompilerPcgCreateSimpleLoad (ret, OFFSETOF_MEMBER (DvmDex,pResClasses));
    return ret;
}

/**
 * @details  Create a call to a routine that uses the standard X86 calling convention,
 * i.e. arguments on the stack. The number of arguments is specified by
 * nArgs, and the actual arguments are specified in a variable argument list.
 * Each argument is specified by two arguments: pcgDtype, CGInst.
 * The result data type is given by resultDtype, which can be NOreg. All
 * result types are supported.
 *
 * This routine currently assumes that EDI and EBP are needed by the callee.
 * That may not be necessary.
 */
CGInst dvmCompilerPcgGenerateX86Call (CompilationUnitPCG *cUnit, const char *targetName, pcgDtype resultDtype, int nArgs, ...)
{
    CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, targetName);
    const char *rreg;
    const char *callOpc = getCallOpcodeAndRreg (cUnit, resultDtype, &rreg);
    const char *copyOpc = 0;
    int32_t stackSize, offset, i;
    va_list args;
    CGInst spIl;
    CGInst parms[3];

    // Compute the required stack size to hold outgoing arguments.
    stackSize = 0;
    va_start (args, nArgs);
    for (i = 0; i < nArgs; i++)
    {
        pcgDtype argDtype = (pcgDtype) va_arg (args, int);

        // Advance past the actual argument.  It isn't needed here.
        va_arg (args, CGInst);

        stackSize += dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, argDtype, &copyOpc);
    }
    va_end (args);

    //We must align the stack
    int32_t remainder = stackSize % 16;

    if (remainder != 0)
    {
        stackSize += (16 - remainder);
    }

    spIl = CGCreateNewInst ("sub", "ri", CGGetStackPointerDef (), stackSize);
    CGSetRreg (spIl, "esp");

    // Store the outgoing arguments to the stack.
    offset = 0;
    va_start (args, nArgs);
    for (i = 0; i < nArgs; i++)
    {
        pcgDtype argDtype = (pcgDtype)va_arg (args, int);
        CGInst arg = va_arg (args, CGInst);

        int32_t argSize = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, argDtype, &copyOpc);
        dvmCompilerPcgCreateTypedStore (cUnit, spIl, CGInstInvalid, 0, CGSymbolInvalid, offset, argDtype, arg);
        offset += argSize;
    }
    va_end (args);

    parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    parms[2] = CGInstInvalid;
    CGInst theCall = CGCreateNewInst (callOpc, "nl", target, parms);
    CGSetRreg (theCall, rreg);
    spIl = CGCreateNewInst ("add", "ri", CGGetStackPointerDef (), stackSize);
    CGSetRreg (spIl, "esp");

    if (resultDtype != NOreg)
    {
        dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, resultDtype, &copyOpc);
        theCall = CGCreateNewInst (copyOpc, "r", theCall);
    }

    return theCall;
}

void dvmCompilerPcgCreateEntryStub (CompilationUnitPCG *cUnit)
{
    CGInst entry_inst = CGCreateNewInst ("entry", "");
    CGSetRreg (entry_inst, "esp");

    CGInst inst = entry_inst;
    inst = CGCreateNewInst ("spsubi", "ri", inst, 0);
    CGSetRreg (inst, "esp");

    inst = CGCreateNewInst ("idef", "a", entry_inst);
    CGSetRreg (inst, "edi");
    inst = CGCreateNewInst ("mov", "r", inst);
    CGAddTempDef (cUnit->getVMPtrReg (), inst);

    inst = CGCreateNewInst ("idef", "a", entry_inst);
    CGSetRreg (inst, "ebp");
    inst = CGCreateNewInst ("mov", "r", inst);
    CGAddTempDef (cUnit->getFramePtrReg (), inst);
}


void dvmCompilerPcgHandleInitialLoad (CompilationUnitPCG *cUnit, BasicBlock *bb, int ssaNum, bool considerSpeculative)
{
    SSANumInfo *rootInfo = &(cUnit->getRootSSANumInformation (ssaNum));

    assert (rootInfo != 0);

    // In most cases, we can ignore ssaNum's that are the high half of a 64-bit
    // object.  The entire object will be loaded when processing the low half.
    // However, there are some cases where only the high half of the object is
    // needed.  So go ahead and generate a load here too (using the ssaNum of
    // the low half).  In many cases, this means we will generate two loads of
    // the same object, but PCG will detect this and delete the extra load.
    //
    if (dvmCompilerPcgIsHighDtype (rootInfo->dtype) == true)
    {
        //Get the pair ssa number
        ssaNum = rootInfo->pairSSANum;

        //Since we changed ssa number, look for parent information for the new one
        rootInfo = &(cUnit->getRootSSANumInformation (ssaNum));

        assert (rootInfo != 0);
    }

    if (rootInfo->registerize == false)
    {
        return;
    }

    //Get virtual register
    u2 virtualReg = dvmExtractSSARegister (cUnit, ssaNum);

    if (considerSpeculative == true)
    {
        if (rootInfo->checkedForNull == true)
        {
            // speculative null check has already loaded this ssa
            if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
            {
                ALOGD ("    Not generating entry load for v%d_%d. Already null checked.\n",
                        virtualReg,
                        DECODE_SUB (dvmConvertSSARegToDalvik (cUnit, ssaNum)));
            }

            //If already null checked, we can bail
            return;
        }
    }

    if (cUnit->checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        //Get BB identifier if we have one
        int id = (bb == 0) ? -1 : bb->id;

        ALOGD ("    Generating load for BB %d for v%d_%d (ssa%d).\n", id, virtualReg,
                DECODE_SUB (dvmConvertSSARegToDalvik (cUnit, ssaNum)), ssaNum);
    }

    int vrOffset = dvmCompilerPcgGetVROffsetRelativeToVMPtr (cUnit, virtualReg);
    CGAddr addr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid, vrOffset);

    //Get size and opcode
    const char *pcgOpcode = 0;
    int32_t size = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, rootInfo->dtype, &pcgOpcode);

    //Get the handle for the VR
    void *handle = dvmCompilerPcgGetVRHandle (virtualReg, size);

    //Create the load and add temporary definition
    CGInst load = CGCreateNewInst (pcgOpcode, "m", addr, size, handle);
    CGAddTempDef (rootInfo->parentSSANum, load);
}

void dvmCompilerPcgLoadLiveInVRs (CompilationUnitPCG *cUnit, BasicBlock *blockGoingTo)
{
    assert (blockGoingTo != 0);

    //Check dataflow info
    BasicBlockDataFlow *info = blockGoingTo->dataFlowInfo;

    assert (info != 0);

    //Get entrance
    int *dalvikToSSAMapEntrance = info->dalvikToSSAMapEntrance;

    //Get its live ins
    BitVector *ins = info->liveInV;

    //Paranoid
    assert (ins != 0);

    //Now iterate on the ins
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (ins, &bvIterator);

    //Now handle each SSA live in
    while (true)
    {
        //Get VR
        int vr = dvmBitVectorIteratorNext (&bvIterator);

        //If finished, break out
        if (vr == -1)
        {
            break;
        }

        //Get combination SSA_Subscript
        int ssaCombo = dalvikToSSAMapEntrance[vr];

        //Now we only want the SSA number
        int ssaNum = DECODE_REG (ssaCombo);

        //The live-in vector is over conservative so we only handle initial load if it really is referenced
        if (dvmIsBitSet (cUnit->getReferencedSSARegBV (), ssaNum) == false)
        {
            continue;
        }

        //Handle initial load
        dvmCompilerPcgHandleInitialLoad (cUnit, blockGoingTo, ssaNum, false);
    }
}

CGInst dvmCompilerPcgResolveClass(CompilationUnitPCG *cUnit, u4 classIdx)
{
    CGInst parms[4];
    CGLabel classResolvedLabel = CGCreateLabel ();
    CGTemp classPtrTemp = cUnit->getCurrentTemporaryVR (true);
    CGInst resClasses = dvmCompilerPcgGetResClasses (dvmCompilerPcgGetSelfPointer (cUnit));
    CGInst resClass = dvmCompilerPcgCreateSimpleLoad (resClasses, classIdx * 4);
    CGAddTempDef (classPtrTemp, resClass);

    // I am just guessing that the common case is that the class is already resolved.
    CGCreateNewInst ("cjcc", "rcrbp", resClass, "ne",
                     CGCreateNewInst ("mov", "i", 0),
                     classResolvedLabel, 95);

    dvmCompilerPcgExportPC (cUnit);
    CGInst tmpInst = CGCreateNewInst ("mov", "i", classIdx);
    parms[0] = dvmCompilerPcgGenerateVMPtrMov (cUnit);
    parms[1] = dvmCompilerPcgGenerateFramePtrMov (cUnit);
    parms[2] = CGCreateNewInst ("mov", "r", tmpInst);
    parms[3] = CGInstInvalid;
    CGSetRreg (parms[2], "eax");
    CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, ".class_resolve");

    CGInst call = CGCreateNewInst ("icall", "nl", target, parms);
    CGSetRreg (call, "eax");
    CGInst callResult = CGCreateNewInst ("mov", "r", call);
    CGAddTempDef (classPtrTemp, callResult);

    CGBindLabel (classResolvedLabel);

    //Get resulting CGInst
    CGInst res = CGGetTempUseInst (classPtrTemp);
    return res;
}

void dvmCompilerPcgRemoveNonPhiNodes (CompilationUnitPCG *cUnit, BitVector *tempBV, BasicBlockPCG *loopEntry)
{
    BitVector *phiNodes = dvmCompilerAllocBitVector (1, true);

    dvmClearAllBits (phiNodes);

    // Go through the MIR instructions in the BB looking for Phi nodes
    for (MIR *mir = loopEntry->firstMIRInsn; mir != 0; mir = mir->next)
    {
        // Get opcode
        Opcode opcode = mir->dalvikInsn.opcode;
        if (static_cast<ExtendedMIROpcode> (opcode) == kMirOpPhi)
        {
            int ssaReg = mir->ssaRep->defs[0];
            ssaReg = static_cast<int> (cUnit->getCGTempForSSANum (ssaReg));
            dvmSetBit (phiNodes, ssaReg);
        }
        else
        {
            // if the opcode isn't a phi, we can exit, all the phis are first in the block
            break;
        }
    }
    dvmIntersectBitVectors (tempBV, tempBV, phiNodes);
}

int dvmCompilerPcgGetVROffsetRelativeToVMPtr (CompilationUnitPCG *cUnit, int vR)
{
    int sizeOfVR = sizeof (u4);

    //In order to get adjustment we multiply the window shift by size of VR
    int adjustment = cUnit->registerWindowShift * sizeOfVR;

    //Stack grows in a negative direction and when we have a register window shift we push the
    //stack up. Thus taking that into account, the shift is negative.
    //Namely desiredFP = actualFP - adjustment
    adjustment = adjustment * (-1);

    //Each virtual register is 32-bit and thus we multiply its size with the VR number
    int offset = vR * sizeOfVR;

    //Now take into account any FP adjustment
    offset += adjustment;

    return offset;
}

/**
 * @brief Get a const symbol for a value
 * @details pcgDvmClientGetMemConstSymbol requests that the client allocate
 * memory to hold a constant value and then create a CGSymbol that the code
 * generator can use to reference that memory.  The memory must be at least
 * "length" bytes and have at least "align" alignment.  The client must
 * copy the first "length" bytes from "value" to the newly allocated memory.
 * @param cUnit the CompilationUnitPCG
 * @param value the constant value
 * @param length the bytes defining the constant
 * @param align the alignment required
 * @return the CGSymbol representing the constant value
 */
CGSymbol pcgDvmClientGetMemConstSymbol(
    CompilationUnitPCG *cUnit,
    uint8_t *value,
    size_t length,
    uint32_t align)
{
    return cUnit->getMemConstSymbol(value, length, align);
}

/**
 * @brief Legacy callback for getting mem const.
 * @details This routine isn't used, and here to make sure everything
 * links properly with libpcg.so. We're switching over to a system where
 * we can register callbacks per client, as we are going to do with
 * pcgDvmClientGetMemConstSymbol. This function is hardcoded and used by
 * other clients, therefore, needs to have a dummy definition.
 * @param value the constant value
 * @param length the bytes defining the constant
 * @param align the alignment required
 * @return the CGSymbol representing the constant value
 */
CGSymbol CGGetMemConstSymbolFromClient(uint8_t *value, size_t length, uint32_t align)
{
    assert(0);

    (void) value;
    (void) length;
    (void) align;

    return CGSymbolInvalid;
}

void dvmCompilerPcgPrintTrace (CompilationUnit *basicCompilationUnit, ChainCellCounts &chainCellCounts, u2* pCCOffsetSection)
{
    CompilationUnitPCG * cUnit = static_cast<CompilationUnitPCG*> (basicCompilationUnit);
    char *next_code_ptr = 0;

    next_code_ptr = dvmCompilerPrintTrace (cUnit);

    if (next_code_ptr == 0)
    {
        // simply return if there is no entry in code block
        return;
    }

    if (cUnit->getNumberOfSwitchTableEntries () > 0)
    {
        // 4 byte aligned
        next_code_ptr = reinterpret_cast<char*>((reinterpret_cast<unsigned int>(next_code_ptr) + 3) & ~0x3);
        ALOGD ("** // Switch Table section (4B aligned)");
        unsigned int *stPtr = (unsigned int *)next_code_ptr;
        int switchTableSize = MIN (cUnit->getNumberOfSwitchTableEntries (), MAX_CHAINED_SWITCH_CASES) + 1;
        for (int i = 0; i < switchTableSize; i++)
        {
            ALOGD ("**  %p: %#x", (void*) stPtr, *stPtr);
            stPtr++;
        }
        next_code_ptr = (char*)stPtr;
    }

    dvmCompilerPrintChainingCellCounts (next_code_ptr, chainCellCounts);

    // print the long/double constant section if any
    if (cUnit->memConstBegin () != cUnit->memConstEnd ())
    {
        ALOGD ("** // PCG constant section");
    }

    for (MemConstIterator it = cUnit->memConstBegin ();
         it != cUnit->memConstEnd (); ++it)
    {
        uint8_t *alignedCachePtr = (uint8_t*)align ((char*)next_code_ptr, it->first.align);
        uint16_t *shortPtr;
        uint32_t *intPtr;
        uint64_t *longPtr;
        switch (it->first.length)
        {
            case 1:
                ALOGD ("**  %p: %x", alignedCachePtr, *alignedCachePtr);
                break;
            case 2:
                shortPtr = (uint16_t*) alignedCachePtr;
                ALOGD ("**  %p: %x", shortPtr, *shortPtr);
                break;
            case 4:
                intPtr = (uint32_t*) alignedCachePtr;
                ALOGD ("**  %p: %x", intPtr, *intPtr);
                break;
            case 8:
                longPtr = (uint64_t*) alignedCachePtr;
                ALOGD ("**  %p: %llx", longPtr, *longPtr);
                break;
            default:
                ALOGD ("Couldn't decode value at %p.", alignedCachePtr);
        }
        next_code_ptr = (char *)(alignedCachePtr + it->first.length);
    }

    dvmCompilerPrintChainingCellOffsetHeader (pCCOffsetSection);
}
