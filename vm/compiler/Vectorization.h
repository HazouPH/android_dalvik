/*
* Copyright (C) 2013 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef DALVIK_VM_VECTORIZATION_H_
#define DALVIK_VM_VECTORIZATION_H_

//Forward declaration
struct CompilationUnit;
class Pass;

/**
 * @brief The vectorization pass entry point
 * @param cUnit the CompilationUnit
 * @param pass the Pass to the vectorization pass
 */
void dvmCompilerVectorize (CompilationUnit *cUnit, Pass *pass);

#endif
