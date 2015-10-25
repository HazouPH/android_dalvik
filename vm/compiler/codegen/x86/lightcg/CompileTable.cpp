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

#include "CompileTable.h"
#include <algorithm>

void CompileTableEntry::reset (void)
{
    //We do not reset regNum and physicalType because those uniquely represent an entry.
    //If we reset those we would be creating an invalid CompileTableEntry so we do not.

    //Initialize size based on physical type
    size = getRegSize (physicalType);

    //Reset physical register to null
    physicalReg = PhysicalReg_Null;

    //Unknown number of references
    refCount = 0;

    //If temporary, we don't know the VR it represents
    linkageToVR = -1;

    //We have not spilled this entry so no spill index
    spill_loc_index = -1;

    //We have not written to this
    isWritten = false;
}

bool CompileTableEntry::rememberState (int stateNum)
{
    RegisterState newState;

    newState.physicalReg = physicalReg;
    newState.spill_loc_index = spill_loc_index;

    state[stateNum] = newState;
    return true;
}

bool CompileTableEntry::goToState (int stateNum)
{
    //Look to see if we have the state requested
    std::map<int, RegisterState>::const_iterator stateIter = state.find (stateNum);

    if (stateIter == state.end ())
    {
        //We do not have the state and therefore we cannot go to it. Fail now.
        return false;
    }

    //Now load data from state
    physicalReg = state[stateNum].physicalReg;
    spill_loc_index = state[stateNum].spill_loc_index;

    return true;
}

bool CompileTableEntry::isTemporary (void) const
{
    //If we do not have a logical type simply assume we have a temporary
    if (logicalType == 0)
    {
        return true;
    }

    bool isHardcoded = ((logicalType & LowOpndRegType_hard) != 0);
    bool isScratch = ((logicalType & LowOpndRegType_scratch) != 0);
    bool isTemp = ((logicalType & LowOpndRegType_temp) != 0);

    //We have a temporary if hardcoded reg, scratch, or temp
    return (isHardcoded == true || isScratch == true || isTemp == true);
}

CompileTable::iterator CompileTable::find (int regNum, int physicalType)
{
    CompileTableEntry lookupEntry (regNum, physicalType);

    return std::find (compileTable.begin (), compileTable.end (), lookupEntry);
}

CompileTable::const_iterator CompileTable::find (int regNum, int physicalType) const
{
    CompileTableEntry lookupEntry (regNum, physicalType);

    return std::find (compileTable.begin (), compileTable.end (), lookupEntry);
}

CompileTable::iterator CompileTable::find (int regNum, LowOpndRegType physicalType, LogicalRegType logicalType)
{
    CompileTableEntry lookupEntry (regNum, physicalType, logicalType);

    return std::find (compileTable.begin (), compileTable.end (), lookupEntry);
}

CompileTable::const_iterator CompileTable::find (int regNum, LowOpndRegType physicalType, LogicalRegType logicalType) const
{
    CompileTableEntry lookupEntry (regNum, physicalType, logicalType);

    return std::find (compileTable.begin (), compileTable.end (), lookupEntry);
}

CompileTable::iterator CompileTable::findVirtualRegister (int regNum, LowOpndRegType physicalType)
{
    CompileTableEntry lookupEntry (regNum, LowOpndRegType_virtual | physicalType);

    return std::find (compileTable.begin (), compileTable.end (), lookupEntry);
}

CompileTable::const_iterator CompileTable::findVirtualRegister (int regNum, LowOpndRegType physicalType) const
{
    CompileTableEntry lookupEntry (regNum, LowOpndRegType_virtual | physicalType);

    return std::find (compileTable.begin (), compileTable.end (), lookupEntry);
}
