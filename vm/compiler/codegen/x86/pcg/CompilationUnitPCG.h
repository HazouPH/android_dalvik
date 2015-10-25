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

#ifndef H_COMPILATIONUNIT
#define H_COMPILATIONUNIT

#include <map>
#include <set>

#include "CompilerIR.h"
#include "DataStructures.h"
#include "Relocation.h"
#include "libpcg.h"

//Forward Declaration
struct BasicBlockPCG;

/**
 * @brief Debug flags to drive the debug information of the backend
 */
enum DebugFlags
{
    DebugFlagsPil,              /**< @brief Do we want to dump the PCG IL after every major phase of PCG */
    DebugFlagsAsm,              /**< @brief Do we want to dump the generated ASM into a file */
    DebugFlagsTrace,            /**< @brief Do we want to dump the trace */
    DebugFlagsBytecode,         /**< @brief Do we want to dump the bytecode */
    DebugFlagsDisasm,           /**< @brief Do we want to dump the generate assembly in memory */
    DebugFlagsRegisterizeVRs,   /**< @brief Do we want to dump about registerization? */
    DebugFlagsSpeculative       /**< @brief Do we want to dump about speculative? */
};

/**
 * @brief Debug masks to drive the debug information of the backend
 */
enum DebugMasks
{
    DebugMaskPil = (1 << DebugFlagsPil),            /**< @brief Do we want to dump the PCG IL after every major phase of PCG */
    DebugMaskAsm = (1 << DebugFlagsAsm),            /**< @brief Do we want to dump the generated ASM into a file */
    DebugMaskTrace = (1 << DebugFlagsTrace),        /**< @brief Do we want to dump the trace */
    DebugMaskBytecode = (1 << DebugFlagsBytecode),  /**< @brief Do we want to dump the bytecode */
    DebugMaskDisasm = (1 << DebugFlagsDisasm),      /**< @brief Do we want to dump the generated assembly in memory */
    DebugMaskRegisterizeVRs = (1 << DebugFlagsRegisterizeVRs),  /**< @brief Do we want to dump about registerization? */
    DebugMaskSpeculative = (1 << DebugFlagsSpeculative)         /**< @brief Do we want to dump about speculative checks? */
};

/**
 * @brief Optimization flags to drive the backend optimizations
 */
enum OptimizationFlags
{
    OptimizationFlagsSpeculativeNullChecks, /**< @brief Do we want speculative null checks? */
    OptimizationFlagsAcceptLoops,           /**< @brief Do we want to accept loops? */
};

/**
 * @brief Optimization masks to drive the backend optimizations
 */
enum OptimizationMasks
{
    OptimizationMaskSpeculativeNullChecks = (1 << OptimizationFlagsSpeculativeNullChecks),  /**< @brief Do we want speculative null checks? */
    OptimizationMaskAcceptLoops = (1 << OptimizationFlagsAcceptLoops),                      /**< @brief Do we want to accept loops? */
};

/**
 * @brief Iterator for local CGSymbols for external use.
 */
typedef std::list<CGSymbol>::iterator LocalSymbolIterator;

/**
 * @brief Iterator for memconsts for external use.
 */
typedef std::map<MemConstType, CGSymbol>::iterator MemConstIterator;

/**
 * @brief Iterator for switch table entries, for external use.
 */
typedef std::list<SwitchTableCCXRef>::iterator SwitchTableEntryIterator;

/**
 * @class CompilationUnitPCG
 * @param the CompilationUnitPCG, extension of CompilationUnit
 */
class CompilationUnitPCG: public CompilationUnit
{
    protected:

        /** @brief label to symbol */
        std::map<CGLabel, CGSymbol> label2symbol;

        /** @brief Chaining list information, indexed by type */
        GrowableList chainingListByType[kChainingCellLast];

        /** @brief Is the exception block referenced */
        bool exceptionBlockReferenced;

        /** @brief The debug level */
        uint32_t debugLevel;

        /** @brief The optimizations to perform */
        uint32_t optimizationLevel;

        /** @brief The next temporary register we can have */
        CGTemp nextTemp;

        /** @brief Virtual Machine Pointer */
        CGInst vmPtr;

        /** @brief Virtual Machine Pointer Register */
        CGTemp vmPtrReg;

        /** @brief Relocation map */
        std::multimap<CGSymbol, CRelocation*> relocations;

        /** @brief Information about the SSA number */
        std::map<int, SSANumInfo> ssaNumInfo;

        /** @brief Map for temporary bitvectors */
        std::map<BitVector *, bool> temporaryBitVectors;

        /** @brief The frame pointer register */
        CGTemp framePtrReg ;

        /** @brief The frame pointer */
        CGInst framePtr;

        /** @brief SSA registers potential for registerization */
        std::set<int> references;

        /**
         * @brief Get Current Modifed SSA
         * @details currModBV is used to track the current mod SSANum set during block
         *        translation.   (It is needed to compute the necessary writebacks for
         *        side exits.)
         */
        BitVector *currModBV;

        /**
         * @brief Used to keep track which SSA registers have explicit uses
         */
        BitVector *referencedSsaRegistersBV;

        /** @brief The entry insertion point */
        CGInsertionPoint entryInsertionPoint;

        /** @brief Map from virtual register to SSA registers bitvector */
        std::map<u2, BitVector *> vrToSSANumSet;

        /**
         * @brief Unique ID for this trace ID
         */
         //TODO: we could try to find a better way of living without this static variable
        static uint32_t traceID;

        /** @brief list to keep track of trace-local CGSymbols */
        std::list<CGSymbol> localSymbols;

        /** @brief set of constants to dump into memory */
        std::map<MemConstType, CGSymbol> memconsts;

        /** @brief A list to keep track of switch chaining cell entries */
        std::list<SwitchTableCCXRef> switchChainingCellEntries;
    public:
        /**
         * @brief Constructor
         * @param cUnit the CompilationUnit
         */
        CompilationUnitPCG (CompilationUnit *cUnit);

        /**
         * @brief Check the debug flag for a certain property
         * @param mask the mask to compare with
         * @return whether the masked debug flag is different than 0
         */
        bool checkDebugMask (DebugMasks mask) const {return (debugLevel & mask) != 0;}

        /**
         * @brief Set the debug flag to a certain value
         * @param newLevel value to set the debug level to
         */

        void setDebugLevel (DebugMasks newLevel) { debugLevel |= newLevel; }

        /**
         * @brief Get the current trace ID
         * @return the current trace ID
         */
        uint32_t getTraceID (void) const {return traceID;}

        /**
         * @brief Get the VM Pointer temporary register
         * @return the VM Pointer temporary register
         */
        CGTemp getVMPtrReg (void) const {return vmPtrReg;}

        /**
         * @brief Get the frame pointer
         * @return the frame pointer
         */
        CGInst getFramePtr (void) const {return framePtr;}

        /**
         * @brief Set the frame pointer
         * @param fp the new frame pointer
         */
        void setFramePtr (CGInst fp) {framePtr = fp;}

        /**
         * @brief Get the frame pointer register
         * @return the frame pointer register
         */
        CGTemp getFramePtrReg (void) const {return framePtrReg;}

        /**
         * @brief Check the optimization flag for a certain property
         * @param mask the mask to compare with
         * @return whether the masked optimization flag is different than 0
         */
        bool checkOptimizationMask (OptimizationMasks mask) const {return (optimizationLevel & mask) != 0;}

        /**
         * @brief Get the current temporary with a potential increment
         * @param increment do we increment before returning (default = false)
         * @return the current temporary VR
         */
        CGTemp getCurrentTemporaryVR (bool increment = false);

        /**
         * @brief Set the current temporary virtual register value
         * @param value the value to set
         */
        void setCurrentTemporaryVR (CGTemp value) {nextTemp = value;}

        /**
         * @brief Get the temporary associated with a physical XMM register
         * @param xmmNum the XMM register of interest, must be in the range [0, 7]
         * @return the CGTemp for xmmNum
         */
        CGTemp getCGTempForXMM (int xmmNum);

        /**
         * @brief Get the virtual machine pointer
         * @return the VR containing the VM pointer
         */
        CGInst getVMPtr (void) const {return vmPtr;}

        /**
         * @brief Set the virtual machine state pointer
         * @param ptr the virtual machine state pointer
         */
        void setVMPtr (CGInst ptr) {vmPtr = ptr;}

        /**
         * @brief Add a relocation
         * @param relocation add a relocation to the map
         * @return true if relocation is added
         */
        bool addRelocation (CRelocation *relocation);

        /**
         * @brief Find any relocation corresponding to symbol reference
         * @param symbol the symbol we are looking for
         * @return the CRelocation, can return 0 if no relocations found
         */
        const CRelocation *findRelocation (CGSymbol symbol);

        /**
         * @brief Resolve all registered relocations
         * @param codePtr the code section for the trace generation
         */
        void resolveAllRelocations (uint8_t *codePtr);

        /**
         * @brief Get a SSA information structure
         * @param ssa the SSA number we care about
         * @param newElement set to true/false if it is a new element
         * @return a reference to the structure, zeroed if new
         */
        SSANumInfo &getSSANumInformation (int ssa, bool &newElement);

        /**
         * @brief Get a SSA information structure, do not care if new or not
         * @param ssa the SSA number we care about
         * @return a reference to the structure, zeroed if new
         */
        SSANumInfo &getSSANumInformation (int ssa);

        /**
         * @brief Get the root SSA information structure for an SSA number
         * @details This method differs from getSSANumInformation in that it
         *  returns a reference to the SSANumInfo structure at the root of the
         *  parentSSANum tree.  Conceptually, this means you are getting the
         *  SSANumInfo for the CGTemp that is associated with the specified SSA
         *  number.
         * @param ssa the SSA number we care about
         * @return a reference to the structure, zeroed if new
         */
        SSANumInfo &getRootSSANumInformation (int ssa);

        /**
         * @brief Get the CGTemp associated with a particular SSA number
         * @param ssa the SSA number we care about
         * @return the CGTemp to use
         */
        CGTemp getCGTempForSSANum (int ssa);

        /**
         * Registerize Analysis is done, complete the information
         * @return Returns true if post-registerization analysis completed successfully
         */
        bool registerizeAnalysisDone (void);

        /**
         * @brief Get a temporary BitVector
         * @return a temporary bitvector, it is expandable and not set to 0
         */
        BitVector *getTemporaryBitVector (void);

        /**
         * @brief Set a temporary BitVector to be able to be used again
         * @param bv the BitVector that can be recycled
         */
        void freeTemporaryBitVector (BitVector *bv) {temporaryBitVectors[bv] = true;}

        /**
         * @brief Return the currently modified registers
         * @return the BitVector containing the modified SSAs
         */
         BitVector *getCurrMod (void) const {return currModBV;}

         /**
          * @brief Returns the vector of all referenced SSA registers in cUnit
          * @return The BitVector which contains all referenced SSA registers
          */
         BitVector *getReferencedSSARegBV (void) const {return referencedSsaRegistersBV;}

         /**
          * @brief Get a BasicBlockPCG
          * @param index the id of the BasicBlockPCG we want
          * @return the BasicBlockPCG with the id, 0 if not found
          */
         BasicBlockPCG *getBasicBlockPCG (unsigned int index) const;

         /**
          * @brief Get whether or not the exception block was referenced
          * @return whether or not the exception block was referenced
          */
         bool getExceptionBlockReferenced (void) const {return exceptionBlockReferenced;}

         /**
          * @brief Set whether or not the exception block was referenced
          * @param val whether or not the exception block was referenced
          */
         void setExceptionBlockReferenced (bool val) {exceptionBlockReferenced = val;}

        /**
         * @brief Get the entry insertion point
         * @return the entry insertion point
         */
        CGInsertionPoint getEntryInsertionPoint (void) const {return entryInsertionPoint;}

        /**
         * @brief Set the entry insertion point
         * @param ip the entry insertion point
         */
        void setEntryInsertionPoint (CGInsertionPoint ip) {entryInsertionPoint = ip;}

        /**
         * @brief Insert a referenced VR
         * @param ssa the SSA register
         */
        void insertReferencedVR (int ssa) {references.insert (ssa);}

        /**
         * @brief Disable a registerized define
         * @param ssaNum the ssa register
         */
        void disableRegisterizationForDef (int ssaNum);

        /**
         * @brief Get the references
         * @return the set of references
         */
        const std::set<int> &getReferences (void) const {return references;}

        /**
         * @brief Return the chaining list information, it is indexed by type
         * @return the chaining list information
         */
         //TODO: convinced we could get rid of this list probably
        GrowableList *getChainingList (void) {return chainingListByType;}

        /**
         * @brief Get a SSA register set
         * @param vr the virtual register
         * @return the BitVector representing the ssa registers associated to VR
         */
        BitVector *getSSANumSet (u2 vr) const;

        /**
         * @brief Set a SSA register bitvector associated to a virtual register
         * @param vr the virtual rgister
         * @param bv the BitVector associated to vr
         */
        void setSSANumSet (u2 vr, BitVector *bv) {vrToSSANumSet[vr] = bv;}

        /**
         * @brief Get a CGSymbol associated to blockLabel, create one if necessary
         * @param blockLabel the label
         * @return the associated CGSymbol
         */
        CGSymbol getBlockSymbol (CGLabel blockLabel);

        /**
         * @brief Add a label and symbol pair
         * @param cgLabel the CGLabel
         * @param cgSymbol the CGSymbol
         */
        void addLabelSymbolPair (CGLabel cgLabel, CGSymbol cgSymbol);

        /**
         * @brief Bind the basic block addresses
         * @param startAddr the start address of the generation for the trace
         */
        void bindBlockSymbolAddresses (uint8_t *startAddr);

        /**
         * @brief Add a new local CGSymbol to the list
         * @param cgSymbol the CGSymbol to add
         */
        void addLocalSymbol(CGSymbol cgSymbol);

        /**
         * @brief Return an iterator to the head of the local symbol list.
         * @return Iterator to the head of the local symbol list.
         */
        LocalSymbolIterator localSymbolBegin(void);

        /**
         * @brief Return an iterator to the end of the local symbol list.
         * @return Iterator to the end of the local symbol list.
         */
        LocalSymbolIterator localSymbolEnd(void);

        /**
         * @brief Return memconst symbol with supplied attributes.
         * @param value the value of the constant represented in 16 bytes.
         * @param length the size in bytes of the constant.
         * @param align the alignment in bytes of the constant.
         * @return the symbol.
         */
        CGSymbol getMemConstSymbol(uint8_t *value, size_t length, uint32_t align);

        /**
         * @brief Return beginning of memconsts map.
         * @return Beginning of memconsts map.
         */
        MemConstIterator memConstBegin(void);

        /**
         * @brief Return end of memconsts map.
         * @return End of memconsts map.
         */
        MemConstIterator memConstEnd(void);

        /**
         * @brief Return the number of switch table entries
         * @return The number of switch table entries
         */
        unsigned int getNumberOfSwitchTableEntries (void);

        /**
         * @brief Add a switch table cross-reference to be placed into the switch table
         * @param relocation A ptr to a relocation to keep track of the chaining cell / switch table entry x-ref
         * @param chainingCellBB a ptr to the chaining cell this relocation is associated with
         */
        void addSwitchTableEntry (CRelocation *relocation, BasicBlockPCG *chainingCellBB);

        /**
         * @brief Return beginning of switchtableentry list.
         * @return Beginning of switchtableentry list.
         */
        SwitchTableEntryIterator switchTableBegin(void);

        /**
         * @brief Return end of switchtableentry list.
         * @return End of switchtableentry list.
         */
        SwitchTableEntryIterator switchTableEnd(void);

};

#endif
