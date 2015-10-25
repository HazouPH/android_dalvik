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

#include "Dalvik.h"
#include "StackExtensionX86.h"

/**
 * @brief Gives number of available scratch registers for x86.
 * @return Total number of scratch registers
 */
unsigned int dvmArchSpecGetNumberOfScratch (void)
{
    unsigned int (*fctPtr) (void) = gDvmJit.jitFramework.scratchRegAvail;

    //If we have a function pointer, call it
    if (fctPtr != 0)
    {
        return fctPtr ();
    }

    //If we did not find a function, just say the max is 0
    return 0;
}

/**
 * @brief Given a scratch register index, it gives the VR register number.
 * @param method Method that contains the MIR for which we want to
 * use scratch register.
 * @param idx Index of scratch register. Must be in range [0 .. N-1] where
 * N is the maximum number of scratch registers available.
 * @param registerWindowShift If compilation unit uses a different register frame pointer
 * base, it shifts the register window. This is the amount that register window has shifted.
 * @return Return virtual register number when it finds one for the index.
 * Otherwise, it returns -1.
 */
int dvmArchSpecGetPureLocalScratchRegister (const Method * method, unsigned int idx, int registerWindowShift)
{
    unsigned int maxScratch = dvmArchSpecGetNumberOfScratch ();

    //Sanity check to make sure that requested index is in
    //range [0 .. maxScratch-1]
    if (idx > (maxScratch - 1))
    {
        return -1;
    }

    //We know the index is okay. Index of 0 corresponds to virtual register
    //whose number is: 0 + locals + ins
    int numLocals = method->registersSize - method->insSize;
    int numIns = method->insSize;

    //Calculate the regnum
    int regnum = idx + numLocals + numIns;

    //Take into account the register window shift
    regnum += registerWindowShift;

    return regnum;
}

bool dvmArchIsPureLocalScratchRegister (const Method * method, int virtualReg, int registerWindowShift)
{
    //For x86, we ensure that scratch registers are always in a continuous region and have a number
    //greater than the compilation unit's total register numbers. Thus it is enough to get the virtual
    //number of scratch at index 0 and then seeing if the provided register is number >= to that

    //Get the scratch register at index 0
    int minNum = dvmArchSpecGetPureLocalScratchRegister (method, 0, registerWindowShift);

    //Return if the virtual reg asked about is at least the minimum number for scratch registers
    return virtualReg >= minNum;
}
