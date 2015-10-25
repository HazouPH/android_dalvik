/*
 * Copyright (C) 2010-2013 Intel Corporation
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
#include "NcgHelper.h"
#include "interp/InterpDefs.h"


/*
 * Find the matching case.  Returns the offset to the handler instructions.
 *
 * Returns 3 if we don't find a match (it's the size of the packed-switch
 * instruction).
 */
s4 dvmNcgHandlePackedSwitch(const s4* entries, s4 firstKey, u2 size, s4 testVal)
{
    //skip add_reg_reg (ADD_REG_REG_SIZE) and jump_reg (JUMP_REG_SIZE)
    const int kInstrLen = 4; //default to next bytecode
    if (testVal < firstKey || testVal >= firstKey + size) {
        LOGVV("Value %d not found in switch (%d-%d)",
            testVal, firstKey, firstKey+size-1);
        return kInstrLen;
    }

    assert(testVal - firstKey >= 0 && testVal - firstKey < size);
    LOGVV("Value %d found in slot %d (goto 0x%02x)",
        testVal, testVal - firstKey,
        s4FromSwitchData(&entries[testVal - firstKey]));
    return s4FromSwitchData(&entries[testVal - firstKey]);

}

/*
 * @brief return a target address stored in switch table based on index value
 * @param pSwTbl the switch table address
 * @param firstKey first case value for packed switch
 * @param size number of cases in switch bytecode
 * @param testVal switch argument
 * @return return the target that execution will jump to
 */
s4 dvmJitHandlePackedSwitch(const s4* pSwTbl, s4 firstKey, u2 size, s4 testVal)
{
    if (testVal < firstKey || testVal >= firstKey + size) {
        LOGVV("Value %d not found in switch (%d-%d)",
            testVal, firstKey, firstKey+size-1);
        return pSwTbl[size]; // default case
    }

    LOGVV("Value %d found in slot %d", testVal, testVal - firstKey);
    return pSwTbl[testVal - firstKey];
}
/*
 * Find the matching case.  Returns the offset to the handler instructions.
 *
 * Returns 3 if we don't find a match (it's the size of the sparse-switch
 * instruction).
 */
s4 dvmNcgHandleSparseSwitch(const s4* keys, u2 size, s4 testVal)
{
    const int kInstrLen = 4; //CHECK
    const s4* entries = keys + size;
    int i;
    for (i = 0; i < size; i++) {
        s4 k = s4FromSwitchData(&keys[i]);
        if (k == testVal) {
            LOGVV("Value %d found in entry %d (goto 0x%02x)",
                testVal, i, s4FromSwitchData(&entries[i]));
            return s4FromSwitchData(&entries[i]);
        } else if (k > testVal) {
            break;
        }
    }

    LOGVV("Value %d not found in switch", testVal);
    return kInstrLen;
}

/*
 * @brief return the index if keys[index] == testval
 * @param keys the start address of the case constants area
 * @param size number of cases in switch bytecode
 * @param testVal switch argument
 * @return return the index if keys[index] == testVal, otherwise, return size
 */
s4 dvmJitLookUpBigSparseSwitch(const s4* keys, u2 size, s4 testVal) {
    int i;
    for (i = 0; i < size; i++) {
        s4 k = s4FromSwitchData(&keys[i]);
        if (k == testVal) {
            LOGVV("Value %d found in entry %d", testVal, i);
            return i;
        } else if (k > testVal) {
            break;
        }
    }

    LOGVV("Value %d not found in switch", testVal);
    return size; // default case
}

/*
 * @brief return a target address stored in switch table based on index value
 * @param pSwTbl the switch table address
 * @param keys the start address of the case constants area
 * @param size number of cases in switch bytecode
 * @param testVal switch argument
 * @return return the target that execution will jump to
 */
s4 dvmJitHandleSparseSwitch(const s4* pSwTbl, const s4* keys, u2 size, s4 testVal)
{
    int i;

    for (i = 0; i < size; i++) {
        s4 k = s4FromSwitchData(&keys[i]);
        if (k == testVal) {
            LOGVV("Value %d found in entry %d", testVal, i);
            return pSwTbl[i];
        } else if (k > testVal) {
            break;
        }
    }

    LOGVV("Value %d not found in switch", testVal);
    return pSwTbl[size]; // default case
}
