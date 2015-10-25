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

#include "CompilationUnitPCG.h"
#include "CompilerIR.h"
#include "Lower.h"
#include "LowerJump.h"
#include "UtilityPCG.h"
#include "CompilationErrorPCG.h"

//TODO see how to remove this
// Self PTR from ebp (cUnit->getFramePtr ())
#define offEBPSelf 8

// Create a store of an arbitrarily typed value.
CGInst dvmCompilerPcgCreateTypedStore (CompilationUnitPCG *cUnit, CGInst base, CGInst index, uint32_t scale,
                                       CGSymbol ltbase, int32_t offset, pcgDtype dtype, CGInst r)
{
    const char *opc;
    int32_t size = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opc);

    CGAddr addr = CGCreateAddr (base, index, scale, ltbase, offset);
    return CGCreateNewInst (opc, "mr", addr, size, (void*)1, r);
}

// Create a 4B load with base and offset only
CGInst dvmCompilerPcgCreateSimpleLoad (CGInst base, int32_t offset)
{
    CGAddr addr = CGCreateAddr (base, CGInstInvalid, 0, CGSymbolInvalid, offset);
    return CGCreateNewInst ("mov", "m", addr, 4, (void*)1);
}

// Create a 4B store of a "r" parameter
CGInst dvmCompilerPcgCreateStore (CGInst base, CGInst index, uint32_t scale,
        CGSymbol ltbase, int32_t offset, CGInst r)
{
    CGAddr addr = CGCreateAddr (base, index, scale, ltbase, offset);
    return CGCreateNewInst ("mov", "mr", addr, 4, (void*)1, r);
}

// Create a 4B store of a "r" parameter passed in with base and offset only
CGInst dvmCompilerPcgCreateSimpleStore (CGInst base, int32_t offset, CGInst r)
{
    CGAddr addr = CGCreateAddr (base, CGInstInvalid, 0, CGSymbolInvalid, offset);
    return CGCreateNewInst ("mov", "mr", addr, 4, (void*)1, r);
}

CGInst dvmCompilerPcgGetSelfPointer (const CompilationUnitPCG *cUnit)
{
    CGAddr addr = CGCreateAddr (cUnit->getFramePtr (), CGInstInvalid, 0, CGSymbolInvalid, offEBPSelf);
    return CGCreateNewInst ("mov", "m", addr, 4, (void*)1);
}

void dvmCompilerPcgExportPC (CompilationUnitPCG *cUnit)
{
    // check rPC !=0 as it can lead to failure during exception thrown
    if (rPC == 0) {
        ALOGD ("JIT_INFO: The JIT is exporting a PC of 0. This is likely \
                        incorrect thus we reject trace to prevent semantic problem");
        assert(false);
        cUnit->errorHandler->setError (kJitErrorZeroPC);
        return;
    }
    // exportPc
    CGAddr pcAddr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid,
           (OFFSETOF_MEMBER (StackSaveArea,xtra) - sizeof (StackSaveArea)));
    CGCreateNewInst ("mov", "mi", pcAddr, 4, (void*)1, (int32_t)rPC);
}

void dvmCompilerPcgStoreVirtualReg (CompilationUnitPCG *cUnit, int ssaNum, int storeMask)
{
    //Get the information
    SSANumInfo &info = cUnit->getSSANumInformation (ssaNum);

    CGInst storeVal = CGGetTempUseInst (info.parentSSANum);

    pcgDtype dtype = dvmCompilerPcgGetDtypeForSSANum (cUnit, ssaNum);
    const char *opcode;
    u2 virtualReg = dvmExtractSSARegister (cUnit, ssaNum);
    int32_t storeSize = dvmCompilerPcgGetOpcodeAndSizeForDtype (cUnit, dtype, &opcode);

    if (storeSize == 8 && storeMask == 1)
    {
        // This is a case where are only writing back the lower 4 bytes
        // of an 8 byte value.  In this case, we need to convert the input
        // value (which might either be DPVXreg64 or LLreg) and adjust the
        // store size.
        //
        if (dtype == LLreg)
        {
            storeVal = CGCreateNewInst ("xtract", "r", storeVal);
            opcode = "st";
        }
        else
        {
            storeVal = CGCreateNewInst ("movsd12sd", "r", storeVal);
            opcode = "stss";
        }
        storeSize = 4;
    }
    else
    {
        if (storeSize == 8 && storeMask == 2)
        {
            // This is the ugly case!  We are only writing back the upper 4 bytes
            // of an 8 byte value.  In this case, we need to somehow shift &
            // convert the input value and adjust the store size.  We also need
            // to adjust the VR by 1.
            //
            if (dtype == LLreg)
            {
                storeVal = CGCreateNewInst ("lshri", "ri", storeVal, 32);
                storeVal = CGCreateNewInst ("xtract", "r", storeVal);
                opcode = "st";
            }
            else
            {
                storeVal = CGCreateNewInst ("movsd12sd", "r", storeVal);
                storeVal = CGCreateNewInst ("shufps", "rri", storeVal, storeVal,
                        0x1);
                opcode = "stss";
            }
            storeSize = 4;
            virtualReg += 1;
        }
    }

    void *handle = dvmCompilerPcgGetVRHandle (virtualReg, storeSize);
    int vrOffset = dvmCompilerPcgGetVROffsetRelativeToVMPtr (cUnit, virtualReg);
    CGAddr addr = CGCreateAddr (cUnit->getVMPtr (), CGInstInvalid, 0, CGSymbolInvalid, vrOffset);

    CGCreateNewInst (opcode, "mr", addr, storeSize, handle, storeVal);
}
