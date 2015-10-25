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

#ifndef STACKEXTENSIONX86_H_
#define STACKEXTENSIONX86_H_

/**
 * @brief Space in frame to use for scratch registers.
 */
class StackTemporaries
{
public:
    /**
     * @brief Gives the total number of scratch VRs available for every frame.
     * @return Maximum number of scratch VRs.
     */
    static unsigned int getTotalScratchVRs (void)
    {
        return numScratch;
    }

private:
    /**
     * @brief Hardcoded number of scratch registers per frame.
     */
#ifdef EXTRA_SCRATCH_VR
    static const unsigned int numScratch = 4;
#else
    static const unsigned int numScratch = 0;
#endif

    /**
     * @brief Allocated space for the scratch registers.
     */
    u4 scratchVirtualRegisters[numScratch];
};

/**
 * @brief Stack frame extension for x86.
 */
struct ArchSpecificStackExtension
{

#ifdef EXTRA_SCRATCH_VR
    /**
     * @brief Allocated space for temporaries.
     * @warning If this structure gets moved, dvmArchSpecGetScratchRegister
     * must be updated to provide a new mapping.
     */
    StackTemporaries temps;
#endif

};

#endif /* STACKEXTENSIONX86_H_ */
