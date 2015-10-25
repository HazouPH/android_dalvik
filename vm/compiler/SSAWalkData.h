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

#ifndef DALVIK_VM_SSAWALKDATA_H_
#define DALVIK_VM_SSAWALKDATA_H_

#include <map>

#include "Dataflow.h"

/**
 * @class SWalkDataNoDefine
 * @brief Used as a helper structure to handle SSA registers without a definition during the parsing
 */
typedef struct sWalkDataNoDefine
{
    MIR *mir;   /**< @brief the MIR containing the use without a definition */
    int index;  /**< @brief the index in the ssaRep->uses array for the SSA register */
}SWalkDataNoDefine;

/**
 * @class SSAWalkData
 * @brief SSAWalkData contains any data required inter BasicBlock
 */
class SSAWalkData
{
    public:
        /**
         * @brief Constructor
         * @param cUnit the CompilationUnit
         */
        SSAWalkData (CompilationUnit *cUnit);
        ~SSAWalkData (void);    /**< @brief Destructor */

        /**
         * @brief Get a SUsedChain
         * @return a SUsedChain node
         */
        SUsedChain *getUsedChain (void);

        /**
         * @brief Get last chain node for a particular SSA register
         * @param value the SSA register
         * @return the last chain node for the value SSA register or 0 if none are found
         */
         SUsedChain *getLastChain (int value);

         /**
          * @brief Set the last chain for a given SSA register
          * @param chain the last chain node for the SSA register
          * @param value the SSA register
          */
          void setLastChain (SUsedChain *chain, int value);

         /**
          * @brief Associate a defined register and the instruction
          * @param insn the instruction where the register is defined
          * @param value the SSA register getting defined
          */
          void setDefinition (MIR *insn, int value);

          /**
           * @brief Get the instruction containing the definition
           * @param value the SSA register
           * @return the MIR containing the definition, 0 if none found
           */
          MIR *getDefinition (int value) const;

          /**
           * @brief Handle the SSA registers without a definition during the parsing
           */
          void handleNoDefinitions (void);

          /**
           * @brief Add a SSA register that does not have a definition during the parsing
           * @param mir the MIR containing the SSA register without definition
           * @param idx the index in the ssaRep->uses array for the register
           */
          void addNoDefine (MIR  *mir, int idx);

          /**
           * @brief update Def chain with new use
           * @param useIdx the uses index
           * @param used the MIR corresponding to Use
           * @param defined the MIR corresponding to Def
           */
          void addUseToDefChain (int useIdx, MIR *used, MIR *defined);

    protected:
          /** @brief Association SSA register <-> where it is defined */
          std::map<int, MIR *> definitions;

          /** @brief Association SSA register <-> the last use chain node */
          std::map<int, SUsedChain *> lastChain;

          /** @brief Free chain node list */
          SUsedChain **freeChainsList;

          /** @brief Current free chain node */
          SUsedChain *freeChains;

          /** @brief Any MIR not having a defintion during the parsing */
          std::vector<SWalkDataNoDefine> noDefine;
};

#endif
