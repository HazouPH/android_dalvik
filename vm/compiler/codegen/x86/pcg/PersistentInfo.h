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

#ifndef H_PERSISTENTINFO
#define H_PERSISTENTINFO

#include <map>

#include "libpcg.h"
#include "UtilityPCG.h"

//Forward declaration
class CompilationUnitPCG;

class PersistentInfo
{
    protected:
        /** @brief CGSymbol to SClientSymbolInfo* map */
        std::map<CGSymbol, SClientSymbolInfo> symbolToSymbolInfo;

        /** @brief CGSymbol Call back map */
        std::map<std::string, CGSymbol> callBacks;

        /** @brief Dummy symbol */
        CGSymbol dummySymbol;

    public:
        /**
         * @brief Constructor
         */
        PersistentInfo (void);

        /**
         * @brief Get a SClientSymbolInfo Symbol using a name
         * @param cUnit CompilationUnitPCG
         * @param symbol the symbol we are looking for
         * @param failOnNotFound if true, we fail the code generation if name is not found, otherwise we return 0
         * @return the symbol info if found, 0 if not found and failOnNotFound == false
         */
         //TODO: most likely these symbols are CUnit related and should be moved there, that should be checked
        SClientSymbolInfo* getSymbolInfo (CompilationUnitPCG *cUnit, const CGSymbol &symbol, bool failOnNotFound = false);

        /**
         * @brief Set a SClientSymbolInfo
         * @param symbol the CGSymbol associated to info
         * @param info the SClientSymbolInfor associated to symbol
         */
        void setSymbolInfo (const CGSymbol &symbol, const SClientSymbolInfo &info) {symbolToSymbolInfo[symbol] = info;}

        /**
         * @brief Erase a SClientSymbolInfo
         * @param symbol the CGSymbol whose info we want to erase
         */
        void eraseSymbolInfo (const CGSymbol &symbol) {symbolToSymbolInfo.erase(symbol);}

        /**
         * @brief Get a CGSymbol call back using its name name
         * @param cUnit CompilationUnitPCG
         * @param name the associated name for the callback CGSymbol we are looking for
         * @return the symbol if found, 0 if not found
         */
        CGSymbol getCallBack (CompilationUnitPCG *cUnit, const char *name);

        /**
         * @brief Set a CGSymbol call back
         * @param name the name of the associated CGSymbol
         * @param symbol the CGSymbol associated to name
         */
        void setCallBack (const char * name, CGSymbol symbol) {std::string s(name); callBacks[s] = symbol;}
};

#endif
