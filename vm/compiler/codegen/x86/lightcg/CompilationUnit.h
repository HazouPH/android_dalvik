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

#ifndef H_COMPILATIONUNIT
#define H_COMPILATIONUNIT

#include "Lower.h"
#include "compiler/CompilerIR.h"

class CompilationUnit_O1: public CompilationUnit
{
    protected:
        /** @brief Physical registers that should not be spilled */
        bool canSpillRegister[PhysicalReg_Null];

        /** @brief pointer to data structure for switch bytecode lowering*/
        struct SwitchInfo *switchInfo;

    public:

       /**
        * @brief Default constructor
        */
        CompilationUnit_O1(void) {
            switchInfo = 0;
        }

        /**
         * @brief get switchInfo pointer
         * @return switchInfo pointer
         */
        SwitchInfo * getSwitchInfo(void) const {
            return switchInfo;
        }

        /**
         * @brief set switchInfo pointer
         * @param switchInformation set switchInfo with switchInformation
         */
        void setSwitchInfo(SwitchInfo * switchInformation) {
            switchInfo = switchInformation;
        }

        /**
         * @brief Can we spill a register?
         * @param reg the register we care about
         * @return true if reg can be spilled, false if outside of the range of the array or should not spill
         */
        bool getCanSpillRegister (int reg);

        /**
         * @brief Set whether we can spill a register? Does nothing if reg would overflow the array
         * @param reg the register we care about
         * @param value if we should spill or not
         * @return whether the update was successful
         */
        bool setCanSpillRegister (int reg, bool value);

        void resetCanSpillRegisters (void)
        {
            for(int k = 0; k < PhysicalReg_Null; k++) {
                canSpillRegister[k] = true;
            }
        }

        /**
         * @brief If the compilation unit has a register window shift, it returns the relative change of FP
         * @return The frame pointer adjustment
         */
        int getFPAdjustment (void);
};

#endif
