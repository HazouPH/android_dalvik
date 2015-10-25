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
 * @file ExceptionHandling.h
 * @brief Defines interfaces and implements utilities used for managed exception handling.
 */

#include <vector>
#include <utility> // for std::pair

/**
 * @brief Used to defer committing instructions for exception handling
 * restore state before punting to interpreter or common exception handler
 */
class ExceptionHandlingRestoreState {
private:
    /**
     * @brief List of streams created for exception handling restore state
     * along with their sizes.
     * @details This is a list of stream pointers and their corresponding sizes.
     */
    std::vector<std::pair<char *, size_t> > streams;

    /**
     * @brief For each exception handling stream, contains a pair of stream's
     * name and its target.
     */
    std::vector<std::pair<char *, char *> > targets;

    /**
     * @brief Counter to ensure some uniqueness for labels generated.
     */
    unsigned int uniqueStreamId;

    /**
     * @brief Keeps track of last label generated.
     */
    char * lastLabelGenerated;

    // Declare the copy constructor and the equal operator as private to
    // prevent copying
    ExceptionHandlingRestoreState(ExceptionHandlingRestoreState const&);
    void operator=(ExceptionHandlingRestoreState const&);

public:
    /**
     * @brief Default constructor
     */
    ExceptionHandlingRestoreState(void);

    /**
     * @brief Default destructor
     */
    ~ExceptionHandlingRestoreState(void);

    /**
     * @brief Generates a label which will be used to tag
     * exception handling restore state.
     * @details Does not guarantee uniqueness across instances of this
     * class. Does not guarantee uniqueness after
     * dumpAllExceptionHandlingRestoreState is called.
     * @return label for exception handling restore state
     */
    char * getUniqueLabel(void);

    /**
     * @brief Creates stream for exception handling and copies all
     * instructions for the restore state to this stream.
     * @details It uses the last label generated as the label for this
     * stream. It resets stream pointer to be beginningOfStream.
     * @param beginningOfStream Pointer to stream before exception handling
     * restore state was dumped.
     * @param endOfStream Pointer to stream after exception handling
     * restore state was dumped.
     * @param targetLabel label of target error
     */
    void createExceptionHandlingStream(char * beginningOfStream,
            char * endOfStream, const char * targetLabel);

    /**
     * @brief Copies all of the buffered exception handling restore states
     * to the instruction stream.
     * @details After dumping each of the exception handling restore states to the
     * stream, it generates a jump to the error name label (which ends up punting
     * to interpreter).
     */
    void dumpAllExceptionHandlingRestoreState(void);

    /**
     * @brief Resets state of instance.
     */
    void reset(void);
};
