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

#ifndef DALVIK_VM_LOOPINFORMATION
#define DALVIK_VM_LOOPINFORMATION

#include "CompilerUtility.h"

//Forward declarations
struct BasicBlock;
struct BitVector;
struct InductionVariableInfo;
class CompilationUnit;
class Pass;

/**
  * @class LoopInformation
  * @brief LoopInformation provides information about the loop structure
  */
class LoopInformation
{
    protected:
        /** @brief Parent loop, can be 0 */
        LoopInformation *parent;

        /** @brief next sibling loop, can be 0 */
        LoopInformation *siblingNext;

        /** @brief previous sibling loop, can be 0 */
        LoopInformation *siblingPrev;

        /** @brief nested loop, can be 0 */
        LoopInformation *nested;

        /** @brief Depth of the current loop */
        unsigned int depth;

        /** @brief Bitvector for the BasicBlocks of the loop */
        BitVector *basicBlocks;

        /** @brief Entry of the loop */
        BasicBlock *entry;

        /** @brief Preheader of the loop */
        BasicBlock *preHeader;

        /** @brief BasicBlock representing the entry from interpreter, it goes to the preHeader */
        BasicBlock *fromInterpreter;

        /** @brief Backward Chaining Cells of the loop */
        BitVector *backward;

        /** @brief Post loop basic block */
        BitVector *exitLoop;

        /** @brief Peeled blocks */
        BitVector *peeledBlocks;

        /** @brief Registers available */
        unsigned int scratchRegisters;

        /** @brief Inter-iteration dependent variables */
        BitVector *interIterationVariables;

        /** @brief List of Induction Variables */
        GrowableList inductionVariableList;

        /** @brief Hoisted Checks for Array Accesses */
        GrowableList *arrayAccessInfo;

        /** @brief count up or down loop flag */
        bool countUpLoop;

        /** @brief OP_IF_XXX for the loop back branch */
        Opcode loopBranchOpcode;

        /** @brief Variants registers of the loop */
        BitVector *variants;

        /** @brief Does the loop contain invariant instructions? */
        bool containsInvariantInstructions;

        /** @brief basic IV in SSA name */
        int ssaBIV;

        /** @brief vB in "vA op vB" */
        int endConditionReg;

        /**
         * @brief Fill the basicBlock vector with the BasicBlocks composing the loop
         * @param current the current BasicBlock
         */
        void fillBasicBlockVector (BasicBlock *current);

        /**
         * @brief Peel a loop
         * @param cUnit the CompilationUnit
         */
        void peelLoopHelper (CompilationUnit *cUnit);

        /**
         * @brief Handle the new copies: link any block to preheader to the entry's copy
         * @param cUnit the CompilationUnit
         * @param associations the associations between original and copy BasicBlock
         */
        void handlePredecessor (CompilationUnit *cUnit, std::map<BasicBlock*, BasicBlock *> &associations);

        /**
         * @brief Count instructions in the loop
         * @param cUnit the CompilationUnit
         * @return the number of instructions in the loop
         */
        unsigned int countInstructions (CompilationUnit *cUnit);

        /**
         * @brief sets depth for this and nested loops
         * @param depth depth to set
         */
        void setDepth (int depth);

    public:
        /**
         * @brief Build loop information for the trace
         * @param cUnit the CompilationUnit
         * @param current the previous loop information to re-use data structures, can be NULL
         * @return built loop information or NULL if no loops
         */
        static LoopInformation* getLoopInformation (CompilationUnit *cUnit, LoopInformation *current);

        /** @brief Constructor */
        LoopInformation (void);

        /** @brief Destructor */
        ~LoopInformation (void);

        /**
         * @brief Initialize
         */
        void init (void);

        /**
         * @brief Get parent loop
         * @return the parent loop
         */
        LoopInformation *getParent (void) const {return parent;}

        /**
         * @brief Get next sibling loop
         * @return the next sibling loop
         */
        LoopInformation *getNextSibling (void) const {return siblingNext;}

        /**
         * @brief Get previous sibling loop
         * @return the previous sibling loop
         */
        LoopInformation *getPrevSibling (void) const {return siblingPrev;};

        /**
         * @brief Get from interpreter block
         * @return the from interpreter block
         */
        BasicBlock *getFromInterpreter (void) const {return fromInterpreter;}

        /**
         * @brief Get nested loop
         * @return the nested loop
         */
        LoopInformation *getNested (void) const {return nested;};

        /**
         * @brief Get entry block
         * @return the entry block
         */
        BasicBlock *getEntryBlock (void) const {return entry;}

        /**
         * @brief Get array access info
         * @return the ArrayAcessInfo
         */
        GrowableList *getArrayAccessInfo (void) {return arrayAccessInfo;}
        /**
         * @brief Set entry block
         * @param bb the entry block
         */
        void setEntryBlock (BasicBlock *bb) {entry = bb;}

        /**
         * @brief Get preHeader block
         * @return the preHeader block
         */
        BasicBlock *getPreHeader (void) const {return preHeader;}

        /**
         * @brief Get Depth
         * @return the depth
         */
        int getDepth (void) const {return depth;}

        /**
         * @brief Add a loop information within nest correctness
         * @param info another LoopInformation to next
         * @return the overall LoopInformation
         */
        LoopInformation * add (LoopInformation *info);

        /**
         * @brief Get a LoopInformation, search in the nested and use entry as the ID
         * @param entry the entry BasicBlock
         * @return the found LoopInformation, 0 otherwise
         */
        LoopInformation *getLoopInformationByEntry (const BasicBlock *entry);

        /**
         * @brief Check whether BB is a helper BB for this loop.
         * @details helper BBs are pre-header, backward branch and exit of the loop
         * @param bb the BasicBlock to check
         * @return true if bb is a pre-header, backward branch or exit of the loop
         */
        bool isBasicBlockALoopHelper (const BasicBlock *bb);

        /**
         * @brief Does the Loop contain a given BasicBlock ?
         */
        bool contains (const BasicBlock *bb) const;

        /**
         * @brief Dump loop information
         * @param cUnit Compilation Unit
         * @param tab number of tabs before outputting anything
         */
        void dumpInformation (const CompilationUnit *cUnit, unsigned int tab = 0);

        /**
         * @brief Dump loop information
         * @param cUnit Compilation Unit
         * @param file file in which to dump the information
         */
        void dumpInformationDotFormat (const CompilationUnit *cUnit, FILE *file);

        /**
         * @brief Get the exit loop BasicBlocks BitVector
         * @return the exit loop BasicBlocks BitVector
         */
        BitVector *getExitLoops (void) const {return exitLoop;}

        /**
         * @brief Get the one and only exit block of the loop
         * @param cUnit the CompilationUnit
         * @return the exit basic block, or 0 if it is not exactly one
         */
        BasicBlock *getExitBlock (const CompilationUnit *cUnit);

        /**
         * @brief Get the post exit loop BasicBlocks BitVector
         * @details Be careful using this function before loop formation
         *     It will return incorrect value and assert library may be aborted.
         * @param cUnit compilation context
         * @return the post exit loop BasicBlocks BitVector
         */
        BitVector *getPostExitLoops (const CompilationUnit *cUnit);

        /**
         * @brief Get the one and only post exit block of the loop
         * @param cUnit the CompilationUnit
         * @return the post exit basic block, or 0 if it is not exactly one
         */
        BasicBlock *getPostExitBlock (const CompilationUnit *cUnit);

        /**
         * @brief Get the BasicBlocks of the loop
         * @return the BitVector that represents the BasicBlocks of the loop
         */
        BitVector *getBasicBlocks (void) const {return basicBlocks;}

        /**
         * @brief Get the Backward branch BasicBlocks of the loop
         * @return the BitVector that represents the Backward branches
         */
        BitVector *getBackwardBranches (void) const {return backward;}

        /**
         * @brief Get the one and only backward branch of the loop
         * @return the backward branch BasicBlock, or 0 if it is not exactly one
         */
        BasicBlock *getBackwardBranchBlock (const CompilationUnit *);

         /**
          * @brief Get the list with induction variables.
          * @return Returns the list containing IVs.
          */
         GrowableList & getInductionVariableList (void) {return inductionVariableList;}

         /**
          * @brief Is the instruction executed every iteration?
          * @param cUnit the CompilationUnit
          * @param mir the MIR instruction
          * @return whether mir is executed every iteration
          */
         bool executedPerIteration (const CompilationUnit *cUnit, const MIR *mir) const;

         /**
          * @brief Is the BasicBlock executed every iteration?
          * @param cUnit the CompilationUnit
          * @param bb the BasicBlock
          * @return whether BasicBlock is executed every iteration
          */
         bool executedPerIteration (const CompilationUnit *cUnit, const BasicBlock *bb) const;

         /**
          * @brief Is a register an induction variable for the loop?
          * @param cUnit the CompilationUnit
          * @param reg the register we are curious about
          * @param isSSA if the reg is SSA or Virtual register
          * @return whether reg is an induction variable
          */
         bool isAnInductionVariable (const CompilationUnit *cUnit, unsigned int reg, bool isSSA = false);

         /**
          * @brief Is a register an basic induction variable for the loop?
          * @param cUnit the CompilationUnit
          * @param reg the register we are curious about
          * @param isSSA if the reg is SSA or Virtual register
          * @return whether reg is an basic induction variable
          */
         bool isBasicInductionVariable (const CompilationUnit *cUnit, unsigned int reg, bool isSSA = false);

         /**
          * @brief Used to get the induction variable information for a requested register
          * @param cUnit The compilation unit
          * @param reg Register in ssa form or dalvik virtual register
          * @param isSSA Whether the reg is represented with the ssa number
          * @return Returns the IV info if found, otherwise returns 0.
          */
         InductionVariableInfo *getInductionVariableInfo (const CompilationUnit *cUnit, int reg,
                 bool isSSA = false);

         /**
          * @brief Get the increment for an induction variable
          * @param cUnit the CompilationUnit
          * @param reg the register we are curious about
          * @param isSSA if the reg is SSA or Virtual register
          * @return the increment for the induction variable, returns 0 if not found
          */
         int getInductionIncrement (const CompilationUnit *cUnit, unsigned int reg, bool isSSA = false);

         /**
          * @brief Get the Phi node defining a given virtual register
          * @param cUnit the CompilationUnit
          * @param vr the virtual register we want the phi node from
          * @return 0 if not found, the MIR otherwise
          */
         MIR *getPhiInstruction (const CompilationUnit *cUnit, unsigned int vr) const;

         /**
          * @brief Add a variable as an inter-iteration variable
          * @param vr the virtual register
          */
         void addInterIterationVariable (unsigned int vr);

         /**
          * @brief Is a variable an inter-iteration variable?
          * @return whether or not a variable is inter-iteration dependent
          */
         bool isInterIterationVariable (unsigned int vr) const;

         /**
          * @brief Return the inter-iteration BitVector
          * @return the inter-iteration BitVector
          */
         BitVector *getInterIterationVariables (void) const {return interIterationVariables;}

         /**
          * @brief Clear inter iteration variables
          */
         void clearInterIterationVariables (void);

         /**
          * @brief Set Variant BitVector
          * @param bv the BitVector to set as the variant registers
          */
         void setVariants (BitVector *bv) {variants = bv;}

         /**
          * @brief Gets a bitvector holding variants.
          * @return Returns bitvector holding the variant registers.
          *
          */
         BitVector *getVariants (void) const {return variants;}

         /**
          * @brief Is a SSA register an invariant for the loop?
          * @param ssa the SSA register
          * @return whether or not it is an invariant for the loop
          */
         bool isInvariant (unsigned int ssa) const;

         /**
          * @brief Can the loop throw an exception?
          * @param cUnit the CompilationUnit
          * @return whether the loop can throw an exception or not
          */
         bool canThrow (const CompilationUnit *cUnit) const;

         /**
          * @brief Can the loop throw an exception after the loop peeling?
          * @param cUnit the CompilationUnit
          * @return whether the loop can throw an exception or not after the loop peeling
          */
         bool guaranteedToThrowFirstIteration (const CompilationUnit *cUnit) const;

         /**
          * @brief Does the loop have an invoke in it?
          * @param cUnit the CompilationUnit
          * @return whether the loop has an invoke bytecode
          */
         bool hasInvoke (const CompilationUnit *cUnit) const;

         /**
          * @brief Does the loop contain invariant instructions
          * @return whether the loop contains invariant instructions
          */
         bool getContainsInvariantInstructions (void) const {return containsInvariantInstructions;}

         /**
          * @brief Set the boolean about the loop containing invariant instructions
          * @param b the value we want (default: true)
          */
         void setContainsInvariantInstructions (bool b = true) {containsInvariantInstructions = b;}

         /**
          * @brief Sink a vector of instructions
          * @param cUnit the CompilationUnit
          * @param insns the instructions to sink
          */
         void sinkInstructions (CompilationUnit *cUnit, std::vector<MIR *> &insns) const;

         /**
          * @brief Sink an instruction
          * @param cUnit the CompilationUnit
          * @param insn the instruction to sink
          */
         void sinkInstruction (CompilationUnit *cUnit, MIR *insn) const;

         /**
          * @brief Helper to peel the inner loop
          * @details After peeling a loop, loop and Dataflow information are borken
          * @param cUnit the CompilationUnit
          * @return whether the peeling succeeded
          */
         bool peelLoop (CompilationUnit *cUnit);

         /**
          * @brief Get the peeled blocks of the loop
          * @return the BitVector that represents the peeled BasicBlocks of the loop
          */
         BitVector *getPeeledBlocks (void) const {return peeledBlocks;}

         /**
          * @brief Invalidate the peeled blocks of this and nested loops
          */
         void invalidatePeeling (void);

         /**
          * @brief helper function to iterate over loop information
          * @param cUnit the CompilationUnit
          * @param func worker function
          * @param data user data passed to worker
          * @return false to stop iteration
          */
         bool iterate (CompilationUnit *cUnit, bool (*func) (CompilationUnit *, LoopInformation *, void *), void *data = 0);

         /**
           * @brief Helper function to iterate over basic blocks in loop
           * @param cUnit the CompilationUnit
           * @param func worker function
           * @param data user data passed to worker
           * @return Returns false as soon as func returns false. Returns true if all calls to func return true.
           */
        bool iterateThroughLoopBasicBlocks (CompilationUnit *cUnit, bool (*func) (CompilationUnit *cUnit, BasicBlock *, void *),
                void *data = 0);

         /**
           * @brief Helper function to iterate over loop exits
           * @param cUnit the CompilationUnit
           * @param func worker function
           * @param data user data passed to worker
           * @return Returns false as soon as func returns false. Returns true if all calls to func return true.
           */
        bool iterateThroughLoopExitBlocks (CompilationUnit *cUnit, bool (*func) (CompilationUnit *cUnit, BasicBlock *, void *),
                void *data = 0);

         /**
          * @brief helper function to iterate over loop information with const cUnit
          * @param cUnit the CompilationUnit
          * @param func worker function
          * @param data user data passed to worker
          * @return false to stop iteration
          */
         bool iterateWithConst (const CompilationUnit *cUnit, bool (*func) (const CompilationUnit *, LoopInformation *, void *), void *data = 0);

         /**
          * @brief helper function to iterate over loop information
          * @param func worker function
          * @param data user data passed to worker
          * @return false to stop iteration
          */
         bool iterate (bool (*func) (LoopInformation *, void *), void *data = 0);

         /**
          * @brief Checks if the loops is suitable for hoisting range/null checks
          * @param cUnit the CompilationUnit
          * @param pass the Pass
          */
         bool isSimpleCountedLoop (CompilationUnit *cUnit, Pass *pass);

         /**
          * @brief get the basic IV for the loop
          * @return the basic IV
          */
         int getSSABIV (void) {return ssaBIV;}

         /**
          * @brief set the basic IV for the cycle
          * @param biv basic IV
          */
         void setSSABIV (int biv) {ssaBIV = biv;}

         /**
          * @brief get number of basic IV
          * @return number of basic IV
          */
         int getNumBasicIV (const CompilationUnit* cUnit);

         /**
          * @brief Check if the loop is counted up/down
          * @return True if the loop is counted up, false - otherwise
          */
         bool isCountUpLoop (void) {return countUpLoop;}

         /**
          * @brief set if the loop is counted up/down
          * @param up true for counted up loops
          */
         void setCountUpLoop (bool up) {countUpLoop = up;}

          /**
          * @brief set if the loop is counted up/down and return countUpLoop
          * @return True if the loop is counted up, false - otherwise
          */
         bool getCountUpLoop ();

         /**
          * @brief Get loop condition end reg
          * @return loop condition end reg
          */
         int getEndConditionReg (void) {return endConditionReg;}

         /**
          * @brief set loop condition end reg
          * @param reg end condition reg
          */
         void setEndConditionReg (int reg) {endConditionReg = reg;}

         /**
          * @brief Get loop branch opcode
          * @return loop branch opcode
          */
         Opcode getLoopBranchOpcode (void) {return loopBranchOpcode;}

         /**
          * @brief Set loop branch opcode
          * @param op loop branch opcode
          */
         void setLoopBranchOpcode (Opcode op) {loopBranchOpcode = op;}

         /**
          * @brief Does the loop only have a single basic induction variable and is it incremented by 1?
          * @return whether it does or does not
          */
         bool isUniqueIVIncrementingBy1 ();

         /**
          * @brief Add the instructions to every loop exit
          * @param cUnit the CompilationUnit
          * @param insns the vector of MIR to add
          */
         void addInstructionsToExits (CompilationUnit *cUnit, const std::vector<MIR *> &insns);

         /**
          * @brief Add a single instruction to every loop exit
          * @param cUnit the CompilationUnit
          * @param mir the MIR to add
          */
         void addInstructionToExits (CompilationUnit *cUnit, MIR *mir);

         /**
          * @brief check whether given ssaReg leaves a loop
          * @param cUnit the CompilationUnit
          * @param ssaReg SSA register to check
          * @return true if ssaReg leaves a loop
          */
         bool isSSARegLeavesLoop (const CompilationUnit *cUnit, const int ssaReg) const;
};

/**
 * @brief Gate to determine if the CompilationUnit only contains a very simple loop: not nested, one BasicBlock
 * @param cUnit the CompilationUnit
 * @param curPass the Pass structure
 * @return whether to execute the pass or not: currently only very simple loops are supported
 */
bool dvmCompilerVerySimpleLoopGate (const CompilationUnit *cUnit, Pass *curPass);

/**
 * @brief Gate to determine if the LoopInformation only contains a very simple loop: not nested, one BasicBlock
 * @param cUnit the CompilationUnit
 * @param loopInfo the LoopInformation
 * @return whether to execute the pass or not: currently only very simple loops are supported
 */
bool dvmCompilerVerySimpleLoopGateWithLoopInfo (const CompilationUnit *cUnit, LoopInformation* loopInfo);

#endif
