/*
 * Copyright  (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0  (the "License");
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

#ifndef H_DATASTRUCTURES
#define H_DATASTRUCTURES

#include "libpcg.h"
#include <list>

/**
 * @class pcgDtype
 * @brief Enumeration for data types, a change here requires a change to dtypeName
 */
typedef enum pcgDtype
{
    NOreg = 0,  /**< @brief No register */
    INTreg,     /**< @brief Integer */
    LLreg,      /**< @brief Long type */
    VXreg32,    /**< @brief Vectorial 32-bit type */
    DPVXreg64,  /**< @brief Vectorial 64-bit type */
    FPreg32,    /**< @brief x87 32-bit type */
    FPreg64,    /**< @brief x87 64-bit type */
    Any,        /**< @brief Any is fine */
    Any4,       /**< @brief Any 4-byte */
    Any8,       /**< @brief Any 8-byte */
    LLregHi,    /**< @brief Upper half of a Long type */
    DPVXreg64Hi,/**< @brief Upper half of a Double type */
    Any8Hi,     /**< @brief Upper half of any 8-byte type */
    MaxType     /**< @brief Max enumeration type */
} pcgDtype;

/**
 * @class SSANumInfo
 * @brief SSANumInfo provides information to a given SSA number
 */
typedef struct SSANumInfo
{
    pcgDtype dtype;         /**< @brief the type of the SSA register */
    MIR *mir;               /**< @brief the MIR defining the SSA register */
    int parentSSANum;       /**< @brief the parent SSA, i.e the previous subscript */
    int numUses;            /**< @brief How many uses does it have? */
    int pairSSANum;         /**< @brief SSA number of the other half of an 8-byte value */
    bool registerize;       /**< @brief Should we registerize it? */
    bool needsNullCheck;    /**< @brief Does it need a null check? */
    bool checkedForNull;    /**< @brief Has it been checked for null? */
    bool deferWriteback;    /**< @brief Defer its write back */
} SSANumInfo;

/**
 * @class SClientSymbolInfo
 * @brief SClientSymbolInfo provides information associated to a CGSymbol
 */
typedef struct sClientSymbolInfo
{
    std::string name;   /**< @brief Name of the symbol */
    void *address;      /**< @brief Address of the symbol */
    CGSymbol cgSymbol; /**< @brief CGSymbol of the symbol */
} SClientSymbolInfo;

// forward declaration
struct BasicBlockPCG;
class CRelocation;

/**
 * @class SwitchTableCCXRef
 * @brief SwitchTableCCXRef provides information for how to map a switch table entry to its chaining cell
 */
struct SwitchTableCCXRef {
    CRelocation * relocation;          /**< @brief A ptr to a relocation to keep track of the chaining cell / switch table entry x-ref */
    BasicBlockPCG * chainingCellBB;     /**< @brief A ptr to the chaining cell this relocation is associated with */
};

#define MAX_MEMCONST_SIZE 16
/**
 * @brief Data structure to define a memory constant.
 */
struct MemConstType {
    uint8_t value[MAX_MEMCONST_SIZE]; /**< @brief value we want stored */
    size_t length;     /**< @brief size of the memory in bytes */
    uint32_t align;    /**< @brief the alignment requirement in bytes */

    /**
     * @brief < operator for memory constant data structure
     */
    bool operator<(const MemConstType &r) const
        {
            //
            // Check alignment first, then size, then value. We use
            // this order to keep similar aligned values together to
            // minimize padding when laying them down in memory.
            //
            if (this->align == r.align) {
                if (this->length == r.length) {
                    int val_cmp = memcmp(this->value, r.value, r.length);
                    return (val_cmp < 0);
                }
                return (this->length < r.length);
            }
            return (this->align < r.align);
        }
};

#endif
