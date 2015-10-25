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

#ifndef H_ANALYSIS
#define H_ANALYSIS

//Forward Declarations
struct BasicBlock;
struct BasicBlockPCG;
struct CompilationUnit;
struct CompilationUnitPCG;
struct SSANumInfo;

/**
 * @brief Analyze the CompilationUnit for registerization
 * @param cUnit the CompilationUnitPCG
 * @return whether the analysis succeeded or not
 */
bool dvmCompilerPcgNewRegisterizeVRAnalysis (CompilationUnitPCG *cUnit);

/**
 * @brief Handle the SSA live-ins and live-outs
 * @param cUnit the CompilationUnitPCG
 */
void dvmCompilerPcgModSSANum (CompilationUnitPCG *cUnit);

/**
 * @brief Apply the registerization heuristics
 * @param cUnit the CompilationUnitPCG
 * @param ssaNum the SSA number
 * @param info the SSANumInfo associated to ssaNum
 */
void dvmCompilerPcgApplyRegisterizationHeuristics (CompilationUnitPCG *cUnit, int ssaNum, struct SSANumInfo &info);

/**
 * @brief Mark all the BasicBlockPCG that are reachable
 * @param bb the BasicBlockPCG
 */
void dvmCompilerPcgMarkPossiblyReferenced (BasicBlockPCG *bb);

/**
 * @brief Find and record all referenced ssa registers in the cUnit.
 * @details The parameters are not typed for PCG so that BB iterator from ME can be used.
 * @param cUnitPCG The PCG compilation unit of type CompilationUnitPCG.
 * @param bb The basic block to check for references.
 * @returns Always returns false because it does not update the basic block.
 */
bool dvmCompilerPcgFillReferencedSsaVector (CompilationUnit *cUnitPCG, BasicBlock *bb);

#endif
