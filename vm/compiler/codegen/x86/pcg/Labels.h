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

#ifndef H_LABELS
#define H_LABELS

#include <string>

#include "libpcg.h"

//Forward declaration
struct BasicBlockPCG;
class CompilationUnitPCG;

/**
 * @brief Create a symbol
 * @param cUnit the CompilationUnitPCG, which may be NULL only when creating a global symbol
 * @param name the name of the symbol
 * @param address the address of the symbol
 * @param aligned do we want the label aligned? (default: false)
 * @param memconst is this symbol a mem const? (default: false)
 * @param global is this symbol global? (default: false)
 * @return CGSymbol the Code Generation Symbol obtained
 */
CGSymbol dvmCompilerPcgCreateSymbol (CompilationUnitPCG *cUnit, const std::string &name, void *address, bool aligned = false, bool memconst = false, bool global = false);

/**
 * @brief Get the address of a symbol
 * @param cUnit the CompilationUnitPCG
 * @param cgSymbol the CGSymbol
 * @return the address of a given symbol
 */
void* dvmCompilerPcgGetSymbolAddress (CompilationUnitPCG *cUnit, CGSymbol cgSymbol);

/**
 * @brief Bind the address of a symbol
 * @param cUnit the CompilationUnitPCG
 * @param cgSymbol the CGSymbol
 * @param address the address of a given symbol
 */
void dvmCompilerPcgBindSymbolAddress (CompilationUnitPCG *cUnit, CGSymbol cgSymbol, void *address);

/**
 * @brief Bind a BasicBlock
 * @param bb the BasicBlockPCG
 */
void dvmCompilerPcgBindBlockLabel (BasicBlockPCG *bb);

#endif
