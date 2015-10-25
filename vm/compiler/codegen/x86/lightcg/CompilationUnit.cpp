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

#include "CompilationUnit.h"

bool CompilationUnit_O1::getCanSpillRegister (int reg)
{
    //Check overflow first
    if (reg < 0 || reg >= PhysicalReg_Null)
    {
        return false;
    }

    //Otherwise, use what is in the array
    return canSpillRegister[reg];
}

bool CompilationUnit_O1::setCanSpillRegister (int reg, bool value)
{
    //Check overflow first
    if (reg < 0 || reg >= PhysicalReg_Null)
    {
        //Cannot update it
        return false;
    }

    //Otherwise, use what is in the array
    canSpillRegister[reg] = value;

    //Update succeeded
    return true;
}

int CompilationUnit_O1::getFPAdjustment (void)
{
    //In order to get adjustment we multiply the window shift by size of VR
    int adjustment = registerWindowShift * sizeof (u4);

    //Stack grows in a negative direction and when we have a register window shift we push the
    //stack up. Thus taking that into account, the shift is negative.
    //Namely desiredFP = actualFP - adjustment
    adjustment = adjustment * (-1);

    return adjustment;
}
