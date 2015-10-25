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

#ifndef VTUNE_SUPPORT_H_
#define VTUNE_SUPPORT_H_

#include "vtune/JitProfiling.h"

//Forward declarations
struct CompilationUnit;
struct JitTraceDescription;

/*
 * @brief Wrapper of VTune's iJIT_NotifyEvent needed for Update events
 *        (event_type == iJVM_EVENT_TYPE_METHOD_UPDATE) to find
 *        appropriate method_id set by a preceded Method Load event
 *        (event_type == iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED) by an address
 *        inside the method range.
 * @param event_type kind of event
 * @param eventSpecificData data about the event
 * @return see iJIT_NotifyEvent
 *
 * method_id is required by VTune iJIT_NotifyEvent/Update.
 * The new upcoming VTune Update event is designed to not require the method_id.
 * When the new Update will come, all the mapping can be removed.
 */
int notifyVTune(iJIT_JVM_EVENT event_type, void *eventSpecificData);

/*
 * @brief Prepare data about the compiled trace and send it to VTune.
 * @param cUnit pointer to the CompilationUnit
 * @param desc pointer to the JitTraceDescription
 */
void sendTraceInfoToVTune(CompilationUnit *cUnit, JitTraceDescription *desc);
#endif
