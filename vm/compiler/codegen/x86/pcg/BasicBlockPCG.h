
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

#ifndef H_BASICBLOCKPCG
#define H_BASICBLOCKPCG

#include "CompilerIR.h"
#include "libpcg.h"

/**
 * @class BasicBlockPCG
 * @brief BasicBlockPCG extends the common BasicBlock
 */
struct BasicBlockPCG: public BasicBlock
{
    //TODO: change this to protected and add methods for accessing and updating the fields
    public:
        /** @brief A CGLabel that can be used to branch to the block */
        CGLabel cgLabel;

        /**
         * @brief A CGLabel used for the block containing any possible writeback code on a taken branch
         * @details It basically represents the block resulting from splitting the taken edge
         */
        CGLabel takenLabel;

        /**
         * @brief  A CGSymbol that represents the address for the start of the actual chaining cell
         * @details The cgSymbol for a chaining cell block actually  refers to the start address of the exit trampoline into that chaining cell
         */
        CGSymbol chainingCellSymbol;

        /**
         * @brief A CGSymbol for the block containing the writeback code for a backward branch chaining cell
         * @details This symbol represents the address of the pre-backward block for the loop.
         */
        CGSymbol writebackTargetSymbol;

        /**
         * @brief A CGSymbol for the switch table.
         * @details This is a CGSymbol representing the location of the switch table entries, which are used to chain switch statements.
         */
        CGSymbol switchTableSymbol;

        /** @brief Has this block been bound?  This information is needed to determine whether we can query for the block address */
        bool blockBound;

        /** @brief This boolean exists so that we can avoid binding block labels, if possible to maximize block local optimization opportunities */
        bool possiblyReferenced;

        /** @brief Dirty-ins */
        BitVector *dirtyIns;

        /** @brief Dirty-outs */
        BitVector *dirtyOuts;

        /** @brief Dirty-gens */
        BitVector *dirtyGens;

        /** @brief Kills */
        BitVector *kills;

        /**
         * @brief Available ins
         * @details The available ins and outs sets specify whether a CGTemp
         *          is available on entry to and exit from a block.  A CGTemp
         *          is available at a particular point iff it has been defined
         *          on every path leading to that point.
         */
        BitVector *availIns;

        /** @brief Available outs */
        BitVector *availOuts;

        /** @brief Available gens */
        BitVector *availGens;
};

#endif
