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

#ifndef STACKEXTENSION_H_
#define STACKEXTENSION_H_

/* Architecture specific stack extension */
struct ArchSpecificStackExtension;

/* Returns whether the architecture created space for scratch registers */
unsigned int dvmArchSpecGetNumberOfScratch (void);

/* Given a scratch register index, it returns >= 0 if it can find one with
 * available with that index. The return value will be the Virtual Register
 * number to be used to refer to the scratch register. Scratch register indices
 * accepted are [0 .. N-1] where N is maximum number of scratch registers
 * available. The parameter method must be the method containing the MIR
 * for which we want to use scratch register. Whenever a scratch register
 * with that index is not available, the return value will be -1.
 */
int dvmArchSpecGetPureLocalScratchRegister (const Method * method, unsigned int idx, int registerWindowShift);

/**
 * @brief Used to determine if a register is a pure local scratch that is only live within trace.
 * @param method The method from which the virtual register is from
 * @param virtualReg The virtual register to check
 * @param registerWindowShift The register window shift from the CompilationUnit
 * @return Returns true if the virtual register is a pure local scratch.
 */
bool dvmArchIsPureLocalScratchRegister (const Method * method, int virtualReg, int registerWindowShift);

#ifdef ARCH_IA32
#include "codegen/x86/StackExtensionX86.h"
#endif

#endif /* STACKEXTENSION_H_ */
