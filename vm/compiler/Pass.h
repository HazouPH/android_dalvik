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

#ifndef DALVIK_VM_PASS
#define DALVIK_VM_PASS

#include <string>

//Forward declarations
struct BasicBlock;
struct CompilationUnit;
class Pass;

//We need a typedef for returning a work function pointer
typedef bool (*fctWorkPtr) (CompilationUnit *, BasicBlock *);
//We need a typedef for returning a gate function pointer
typedef bool (*fctGatePtr) (const CompilationUnit *, Pass *);

/**
 * @brief OptimizationFlag is an enumeration to perform certain tasks for a given pass.
 * @details Each enum should be a power of 2 to be correctly used.
 */
enum OptimizationFlag
{
    kOptimizationBasicBlockChange = 1,   /**< @brief Recalculate BasicBlock information */
    kLoopStructureChange          = 2,   /**< @brief Recalculate LoopInformation nest information */
    kOptimizationDefUsesChange    = 4,   /**< @brief Additonal information about def-uses discovered */
    kOptimizationNeedIterative    = 8,   /**< @brief Pass must be run until there are no more updates */
};

/**
  * @class Pass
  * @brief Pass is the Pass structure for the optimizations
  * The following structure has the different optimization passes that we are going to do
  */
class Pass
{
    protected:
        /** @brief Pass name */
        std::string passName;

        /** @brief Type of traversal */
        DataFlowAnalysisMode traversalType;

        /** @brief Specific data for the pass */
        void *data;

        /** @brief Gate for the pass, taking the CompilationUnit and the pass information */
        bool (*gatePtr) (const CompilationUnit *cUnit, Pass *curPass);

        /** @brief Start of the pass function */
        void (*startPtr) (CompilationUnit *cUnit, Pass *curPass);

        /** @brief End of the pass function */
        void (*endPtr) (CompilationUnit *cUnit, Pass *curPass);

        /**
         * @brief Per basic block work
         * @return returns whether the BasicBlock has been changed
         */
        bool (*doWorkPtr) (CompilationUnit *, BasicBlock *bb);

        /**
         * @brief Free the data
         * @param data the data to be freed
         */
        void (*freeDataPtr) (void *data);

        /** @brief Flags for additional directives */
        unsigned int flags;

        /** @brief Next Pass */
        Pass *next;

        /** @brief Previous Pass */
        Pass *previous;

    public:
        /**
         * @brief Constructor
         * @param name the Pass' name
         * @param traversalType the traversal type
         * @param data the data
         * @param gatePtr the gate function pointer
         * @param startPtr the start function pointer
         * @param endPtr the end function pointer
         * @param doWorkPtr the doWork function pointer
         * @param freeDataPtr the freeData function pointer
         * @param flag the post-pass requests using the OptimizationFlag enum bit-ored
         */
         Pass (const std::string &name,
               DataFlowAnalysisMode traversalType,
               void *data,
               bool (*gatePtr) (const CompilationUnit *, Pass *),
               void (*startPtr) (CompilationUnit *, Pass *),
               void (*endPtr) (CompilationUnit *, Pass *),
               bool (*doWorkPtr) (CompilationUnit *, BasicBlock *),
               void (*freeDataPtr) (void *),
               unsigned int flag);

         /**
          * @brief Destructor
          */
         ~Pass (void) {}

         /**
          * @brief Get the Pass name
          * @return the name
          */
        const std::string &getName (void) const;

        /**
         * @brief Get the traversal type
         * @return the traversal type
         */
        DataFlowAnalysisMode getTraversal (void) const;

        /**
         * @brief Get the data for the Pass
         * @return the data
         */
        void *getData (void) const;

        /**
         * @brief Set the data
         * @param data the new data
         */
        void setData (void *data);

        /**
         * @brief Free the memory of the data pointer
         */
        void freePassData (void);

        /**
         * @brief Get the Pass' flags
         * @param flag the flag we want to test
         * @return whether the flag is set
         */
        bool getFlag (OptimizationFlag flag) const;

        /**
         * @brief Set a given flag
         * @param flag the new flag
         * @param value to set the flag (default: true)
         */
       void setFlag (OptimizationFlag flag, bool value = true);

        /**
         * @brief Gate for the pass, taking the CompilationUnit and the pass information
         * @param cUnit the CompilationUnit
         * @param curPass the current Pass
         */
        bool gate (const CompilationUnit *cUnit, Pass *curPass) const;

        /**
         * @brief Start of the pass function
         * @param cUnit the CompilationUnit
         * @param curPass the current Pass
         */
        void start (CompilationUnit *cUnit, Pass *curPass) const;

        /**
         * @brief End of the pass function
         * @param cUnit the CompilationUnit
         * @param curPass the current Pass
         */
        void end (CompilationUnit *cUnit, Pass *curPass) const;

        /**
         * @brief Get the work function
         * @return returns the work function pointer (can be NULL)
         */
        fctWorkPtr getWork (void) const;

        /**
         * @brief Set next Pass
         * @param pass the Pass to be set as next
         */
        void setNext (Pass *pass) {next = pass;}

        /**
         * @brief Get the next Pass
         * @return the next Pass
         */
        Pass *getNext (void) const {return next;}

        /**
         * @brief Set previous Pass
         * @param pass the Pass to be set as previous
         */
        void setPrevious (Pass *pass) {previous = pass;}

        /**
         * @brief Get the previous Pass
         * @return the previous Pass
         */
        Pass *getPrevious (void) const {return previous;}

        /**
         * @brief Set gate
         * @param newGate the new gate pointer
         */
        void setGate (bool (*newGate) (const CompilationUnit *cUnit, Pass *curPass)) {gatePtr = newGate;}

        /**
         * @brief Set end work function
         * @param newEndWorkFunc the new function to be run at end of pass
         */
        void setEndWork (void (*newEndWorkFunc) (CompilationUnit *cUnit, Pass *curPass)) {endPtr = newEndWorkFunc;}

        /**
         * @brief Get the gate
         * @return the gate pointer
         */
        fctGatePtr getGate (void) const {return gatePtr;}
};
#endif
