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

#ifndef H_CODEGENERATION
#define H_CODEGENERATION

struct CompilationUnitPCG;
struct JitTranslationInfo;

/**
 * @brief Code generation for a trace
 * @param cUnit the CompilationUnitPCG
 * @param info the JitTranslationInfo
 * @return whether the code generation succeeded
 */
bool dvmCompilerPcgGenerateIlForTrace (CompilationUnitPCG *cUnit, JitTranslationInfo* info);

/**
 * @brief Emits the compiled code and chaining cells into the code cache.
 * @param cUnit the CompilationUnitPCG
 * @param info the JitTranslationInfo
 */
void dvmCompilerPcgEmitCode (CompilationUnitPCG *cUnit, JitTranslationInfo* info);

/**
 * @brief Create the hook for debugging
 */
void *dvmCompilerPcgCreateHookFunction (void);

#endif
