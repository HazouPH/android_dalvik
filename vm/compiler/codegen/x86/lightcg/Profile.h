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

#ifndef H_ASSEMBLE
#define H_ASSEMBLE

struct BasicBlock_O1;
struct JitTraceDescription;
struct CompilationUnit;

/* 4 is the number  f additional bytes needed for chaining information for trace:
 * 2 bytes for chaining cell count offset and 2 bytes for chaining cell offset */
#define EXTRA_BYTES_FOR_CHAINING 4

#ifdef WITH_JIT_TPROFILE

/* 4 is the number  f additional bytes needed for loop count addr */
#define EXTRA_BYTES_FOR_LOOP_COUNT_ADDR 4
/* 4 is the number  f additional bytes needed for execution count addr */
#define EXTRA_BYTES_FOR_PROF_ADDR 4

/**
 * @brief Get the size of a jit trace description
 * @param desc the point of jit trace description we want check
 * @return The size of the jit trace description
 */
int getTraceDescriptionSize(const JitTraceDescription *desc);

/**
 * @brief Generate the loop counter profile code for loop trace
 *   Currently only handle the loop trace without nested loops, so just add code to bump up the loop counter before the loop entry basic block
 *   For loop trace with nested loops, set the loop counter's addr to -1
 * @param cUnit The compilation unit of the trace
 * @param bbO1 The current basic block being processed
 * @return the size (in bytes) of the generated code
 */
int genLoopCounterProfileCode(CompilationUnit *cUnit, BasicBlock_O1 *bbO1);

#endif /* WITH_JIT_TPROFILE */

/**
 * @brief Generate the trace counter profile code for each trace
 * @param cUnit The compilation unit of the trace
 * @return the size (in bytes) of the generated code
 */
int genTraceProfileEntry(CompilationUnit *cUnit);

#endif
