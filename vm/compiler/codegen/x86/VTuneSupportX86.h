/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef VTUNE_SUPPORT_X86_H_
#define VTUNE_SUPPORT_X86_H_

#include "VTuneSupport.h"

/*
 * @brief Report all chain cells formats of the trace to VTune.
 * @param cUnit pointer to CompilationUnit
 * @param method_id VTune's method id got from previous call
 *                  notifyVTune(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, ...)
 */
void dvmCompilerReportChainCellDataToVTune(CompilationUnit *cUnit, unsigned int method_id);

/* @brief Calculates line number information and fills jitMethod.
 * @param cUnit pointer to CompilationUnit
 * @param jitMethod iJIT_Method_Load to get the line info
 * @param lineInfoList storage of the line info, which must persist before return
 *                     from VTune Method Load event
 */
void getLineInfo(CompilationUnit *cUnit, iJIT_Method_Load &jitMethod, std::vector<LineNumberInfo> &lineInfoList);

#endif
