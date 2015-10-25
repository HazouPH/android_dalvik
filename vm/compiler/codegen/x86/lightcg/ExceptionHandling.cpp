/*
 * Copyright (C) 2010-2013 Intel Corporation
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

/**
 * @file ExceptionHandling.cpp
 * @brief Implements interfaces and utilities used for managed exception handling.
 */

#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "Lower.h"
#include "ExceptionHandling.h"
#include "NcgAot.h"
#include "Scheduler.h"
#include "Singleton.h"
#include "Utility.h"

ExceptionHandlingRestoreState::ExceptionHandlingRestoreState(void) :
        uniqueStreamId(0), lastLabelGenerated(NULL) {
    // For now, there's nothing else we need to do in constructor
}

ExceptionHandlingRestoreState::~ExceptionHandlingRestoreState(void) {
    this->reset();
}

void ExceptionHandlingRestoreState::reset(void) {
    this->streams.clear();
    this->targets.clear();
    this->lastLabelGenerated = NULL;
    this->uniqueStreamId = 0;

    // We must free the labels we inserted
    freeShortMap();
}

char * ExceptionHandlingRestoreState::getUniqueLabel(void) {
    // Allocate a label
    char * label = static_cast<char *>(dvmCompilerNew(LABEL_SIZE, false));

    // Give it a unique name
    snprintf(label, LABEL_SIZE, "exception_restore_state_%d",
            this->uniqueStreamId);

    // Ensure future ids will be unique
    this->uniqueStreamId++;

    // Save label generated
    this->lastLabelGenerated = label;

    return label;
}

void ExceptionHandlingRestoreState::createExceptionHandlingStream(
        char * beginningOfStream, char * endOfStream,
        const char * targetLabel) {
    // Just converting some pointers to unsigned ints to do some math.
    unsigned int beginning = reinterpret_cast<unsigned int>(beginningOfStream);
    unsigned int end = reinterpret_cast<unsigned int>(endOfStream);
    size_t lengthOfTargetLabel = strlen(targetLabel);

    // Developer needs to ensure that this doesn't happen
    assert(end >= beginning);

    // Calculate size of exception handling instructions
    size_t sizeOfStream = end - beginning;

    // Create the new stream using compiler arena
    char * newStream = static_cast<char *>(dvmCompilerNew(sizeOfStream, false));

    // Copy instructions to the new stream
    memcpy(newStream, beginningOfStream, sizeOfStream);

    // Reset stream pointer now
    stream = beginningOfStream;

    // Add new stream to list of exception handling stream
    this->streams.push_back(std::make_pair(newStream, sizeOfStream));

    // Create a copy of the targetLabel because we cannot assume it won't be destroyed
    // before we use it. Ensure that it can fit the entire old label, that is all zeros
    // on allocation, and that it has room for the terminating null.
    char * targetLabelCopy = static_cast<char *>(dvmCompilerNew(
            lengthOfTargetLabel + 1, true));

    // Copy string to our label copy. Allocation already ensures null termination
    strncpy(targetLabelCopy, targetLabel, lengthOfTargetLabel);

    // Save the name of own label and name of target label so we know
    // where to generate jump to
    this->targets.push_back(std::make_pair(this->lastLabelGenerated, targetLabelCopy));
}

void ExceptionHandlingRestoreState::dumpAllExceptionHandlingRestoreState(void) {
    // Flush scheduler queue before copying to stream
    if (gDvmJit.scheduling)
        singletonPtr<Scheduler>()->signalEndOfNativeBasicBlock();

    // Go through each saved restore state
    for (unsigned int i = 0; i < this->streams.size(); ++i) {
        size_t sizeOfExceptionRestore = this->streams[i].second;

        // Ensure that we won't overfill the code cache
        if (dvmCompilerWillCodeCacheOverflow((stream - streamStart)
                + sizeOfExceptionRestore + CODE_CACHE_PADDING) == true) {
            dvmCompilerSetCodeAndDataCacheFull();
            ALOGI("JIT_INFO: Code cache full while dumping exception handling restore state");
            SET_JIT_ERROR(kJitErrorCodeCacheFull);
            this->reset();
            return;
        }

        char * label = this->targets[i].first;
        char * targetLabel = this->targets[i].second;

        // JIT verbosity
        if (dump_x86_inst)
            ALOGD("LOWER %s @%p", label, stream);

        // Insert label exception_restore_state_# where # is the unique identifier
        if (insertLabel(label, true) == -1) {
            this->reset();
            return;
        }

        // Copy to instruction stream
        memcpy(stream, this->streams[i].first, sizeOfExceptionRestore);

        // After the copy, we still need to update stream pointer
        stream = stream + sizeOfExceptionRestore;

        // Jump to the target error label
        unconditional_jump (targetLabel, false);
    }

    // Since we dumped to code stream, we can clear out the data structures
    this->reset();
}
