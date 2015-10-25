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

#include "Analysis.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "Dataflow.h"
#include "Labels.h"
#include "Utility.h"
#include "UtilityPCG.h"

uint32_t CompilationUnitPCG::traceID = 0;

CompilationUnitPCG::CompilationUnitPCG (CompilationUnit *cUnit)
{
    CompilationUnit *cUnitLimited = this;

    //Copy the ME's CUnit in
    *cUnitLimited = *cUnit;

    //Augment trace counter
    traceID++;

    //Default value is 0
    debugLevel = 0;
    optimizationLevel = 0;

    //Get the value
    int tmp = 0;
    bool res = dvmExtractBackendOption ("DebugLevel", &tmp);

    //Set if found
    if (res == true)
    {
        //Set to DebugLevel
        debugLevel = tmp;
    }

    //If we are in verbose mode, print out elements
    if (cUnit->printMe == true)
    {
        setDebugLevel (DebugMaskBytecode);
        setDebugLevel (DebugMaskDisasm);
    }

    //Get the value
    tmp = 0;
    res = dvmExtractBackendOption ("OptimizationLevel", &tmp);

    //Set if found
    if (res == true)
    {
        //TODO The optimization level setting should be normalized. For example turning on/off registerization
        //differs between CSO and PCG.

        //Set to OptimizationLevel
        optimizationLevel = tmp;
    }

    //Generate a currMod
    currModBV = getTemporaryBitVector ();

    //Make space for keeping track of referenced ssa registers
    referencedSsaRegistersBV = getTemporaryBitVector ();

    // This needs to be initialized to some value larger than the highest
    // SSA number, because we use the SSA number as the
    // temp number when we registerize VRs.
    // Also, reserve temporaries for the XMM registers.
    setCurrentTemporaryVR (numSSARegs + gDvmJit.vectorRegisters);
    vmPtrReg = getCurrentTemporaryVR (true);

    //Create the frame pointer register
    framePtrReg = getCurrentTemporaryVR (true);
    framePtr = 0;

    //For the moment no exception block has been referenced
    exceptionBlockReferenced = false;

    // Initialize various types chaining lists.
    for (unsigned int i = 0; i < kChainingCellLast; i++)
    {
        dvmInitGrowableList (&chainingListByType[i], 2);
    }
}

CGTemp CompilationUnitPCG::getCurrentTemporaryVR (bool increment)
{
    //Get a local value for nextTemp
    CGTemp res = nextTemp;

    //If increment is true, increment nextTemp before returning res
    if (increment == true)
    {
        nextTemp++;
    }

    return res;
}

bool CompilationUnitPCG::addRelocation (CRelocation *relocation)
{
    // no relocation => nothing to add
    if (relocation == 0)
    {
        return false;
    }

    const SClientSymbolInfo *info = relocation->getSymbolInfo ();

    // Relocation creation guarantees that info cannot be 0, so just assert it
    assert (info != 0);

    // Keep the correspondance of symbol to relocation in multimap
    relocations.insert (std::pair<CGSymbol, CRelocation*>(info->cgSymbol, relocation));
    return true;
}

CGTemp CompilationUnitPCG::getCGTempForXMM (int xmmNum)
{
    // We reserved the first gDvmJitGlobals.vectorRegisters available CGTemp
    // numbers after the max SSA number for the XMM registers.
    assert (xmmNum >= 0 && xmmNum < gDvmJit.vectorRegisters);

    return numSSARegs + xmmNum;
}

/**
 * @details findRelocation attempts to find a reference to the specified symbol
 * in the code.  This routine is specifically used to find references to the
 * chaining cells so that the addresses of the references may be recorded in
 * the chaining cell for fixup by the VM.
 *
 * The routine returns a Relocation with all the information about the
 * reference if at least one reference is found. (In theory, the chaining
 * cell structure expects there to be exactly one reference, but currently
 * there may be more than one.)
 *
 * If no reference is found, we return 0.
 * This situation should only be possible if all references to the chaining
 * cell were optimized away (such as can happen if a branch direction may be
 * computed at compile time).
 */
const CRelocation *CompilationUnitPCG::findRelocation (CGSymbol symbol)
{
    std::multimap<CGSymbol, CRelocation*>::iterator result = relocations.find (symbol);

    // If relocation is not found we return 0
    CRelocation *relocation = (result == relocations.end()) ? 0 : result->second;
    return relocation;
}

// Perform symbol relocation.  The input argument is the address of the start of the routine
void CompilationUnitPCG::resolveAllRelocations (uint8_t *codePtr)
{
    for (std::multimap<CGSymbol,CRelocation*>::iterator it = relocations.begin(); it != relocations.end(); ++it)
    {
        it->second->resolve (codePtr);
    }
}

/**
 * @brief Used to validate that all scratch registers have been registerized
 * @param cUnit The compilation unit
 * @param ssaNum The ssa register to check
 * @param ssaInfo The information associated with ssa register
 * @return Returns false if the scratch register is not registerized
 */
static bool validateScratchRegisterized (CompilationUnitPCG *cUnit, int ssaNum, SSANumInfo &ssaInfo)
{
    if (dvmCompilerIsPureLocalScratch (cUnit, ssaNum, true) == true)
    {
        if (ssaInfo.registerize == false || ssaInfo.deferWriteback == false)
        {
            return false;
        }
    }

    //If we get here we either were not looking at scratch or it is registerized
    return true;
}

bool CompilationUnitPCG::registerizeAnalysisDone (void)
{
    std::map<int, SSANumInfo>::iterator it;

    if (checkDebugMask (DebugMaskRegisterizeVRs) == true)
    {
        ALOGI ("\nSSANum type info for trace %d\n", traceID);
        ALOGI ("==============================\n");
    }

    for (it = ssaNumInfo.begin (); it != ssaNumInfo.end (); ++it)
    {
        //Get local versions
        int ssaNum = it->first;
        SSANumInfo &info = it->second;

        // First let us validate that if this is a scratch register that it has actually been registerized
        if (validateScratchRegisterized (this, ssaNum, info) == false)
        {
            ALOGD ("JIT_INFO: Found non-registerized scratch register, most likely due to type inconsistency");
            errorHandler->setError (kJitErrorPcgScratchFailedRegisterization);
            return false;
        }

        // Only consider top level temps.
        if (ssaNum != info.parentSSANum)
        {
            if (checkDebugMask (DebugMaskRegisterizeVRs) == true)
            {
                CompilationUnit *simpleCUnit = dynamic_cast<CompilationUnit *> (this);
                SSANumInfo &rootInfo = getRootSSANumInformation (ssaNum);
                int dalvikReg = dvmConvertSSARegToDalvik (simpleCUnit, ssaNum);
                int vrNum = DECODE_REG (dalvikReg);
                int vrSub = DECODE_SUB (dalvikReg);
                int parentReg = dvmConvertSSARegToDalvik (simpleCUnit, rootInfo.parentSSANum);
                int parentNum = DECODE_REG (parentReg);
                int parentSub = DECODE_SUB (parentReg);

                ALOGI ("v%d_%d child of v%d_%d [ssanum: %d]\n", vrNum, vrSub, parentNum, parentSub, ssaNum);
            }
            continue;
        }

        // Resolve the type.
        switch (info.dtype)
        {
            case Any:
                // We don't know the type or size, so just default to intreg.
                // We only currently expect this for invoke arguments.
                info.dtype = INTreg;
                break;

            case Any4:
                info.dtype = INTreg;
                break;

            case Any8:
                info.dtype = DPVXreg64;
                break;

            case Any8Hi:
                info.dtype = DPVXreg64Hi;
                break;

            default:
                break;
        }

        // Compute the VR number and add this SSA number to the list of
        // SSA Numbers associated with the VR.
        u2 vrNum = dvmExtractSSARegister (this, ssaNum);

        BitVector *bv = getSSANumSet (vrNum);

        if (bv == 0)
        {
            bv = dvmCompilerAllocBitVector (numSSARegs + 1, false);

            setSSANumSet (vrNum, bv);

            dvmClearAllBits (bv);
        }

        dvmSetBit (bv, ssaNum);

        dvmCompilerPcgApplyRegisterizationHeuristics (this, ssaNum, info);
    }

    //Everything went fine if we get here
    return true;
}

SSANumInfo &CompilationUnitPCG::getSSANumInformation (int ssaNum)
{
    bool createdNewElement = false;
    SSANumInfo &info = getSSANumInformation (ssaNum, createdNewElement);

    if (createdNewElement == true)
    {
        /*
         * Caller of this function was not expecting that a new element be created,
         * but one was actually created anyway. This should never be hit and thus we
         * set an error. Unfortunately we do not have a way to shortcut the error process
         * because caller of this method always expects to get ssa information. After
         * setting error we prepare information with conservative settings but know that
         * eventually this trace will be rejected by backend.
         */
        errorHandler->setError (kJitErrorPcgBadSsaReference);

        //Set up information with the most conservative settings so we can actually return something.
        info.dtype = NOreg;
        info.parentSSANum = ssaNum;
        info.pairSSANum = 0;
        info.numUses = 0;
        info.mir = 0;
        info.registerize = false;
        info.needsNullCheck = true;
        info.checkedForNull = false;
        info.deferWriteback = false;
    }

    return info;
}

SSANumInfo &CompilationUnitPCG::getSSANumInformation (int ssaNum, bool &newElement)
{
    //Get iterator
    std::map<int, SSANumInfo>::iterator it = ssaNumInfo.find (ssaNum);

    //Did we not find it?
    if (it == ssaNumInfo.end ())
    {
        //Set newElement to true
        newElement = true;

        //Create a null set version
        SSANumInfo nil;
        memset (&nil, 0, sizeof (nil));

        ssaNumInfo[ssaNum] = nil;
    }

    //Return element
    return ssaNumInfo[ssaNum];
}

SSANumInfo &CompilationUnitPCG::getRootSSANumInformation (int ssaNum)
{
    SSANumInfo *ssaNumInfoPtr = &(getSSANumInformation (ssaNum));

    while (ssaNumInfoPtr->parentSSANum != ssaNum)
    {
        ssaNum = ssaNumInfoPtr->parentSSANum;
        ssaNumInfoPtr = &(getSSANumInformation (ssaNum));
    }

    // At this point, we have the option of collapsing the parentSSANum tree.
    // That is, we can set the original ssaNumInfo[ssaNum].parentSSANum directly
    // to the final root of the tree.  That might save on compile time, but
    // leaving the tree untouched is simpler until we get everything stable.

    return *ssaNumInfoPtr;
}

// In the common case, we just use the SSA number itself as the CGTemp.  PHIs
// are the exception.  All operands of a PHI must be assigned the same CGTemp.
// We use the parentSSANum field of the SSANum info structure to handle this.
// The parentSSANum field forms a tree structure where the SSA number at the
// root of the tree is used as the CGTemp for all the SSA numbers in the tree.
// The root points back to itself.
//
CGTemp CompilationUnitPCG::getCGTempForSSANum (int ssa)
{
    int parentSSA = getRootSSANumInformation(ssa).parentSSANum;

    return static_cast<CGTemp>(parentSSA);
}

BitVector *CompilationUnitPCG::getTemporaryBitVector (void)
{
    //First iterate on the map, perhaps one is free again
    for (std::map<BitVector *, bool>::iterator it = temporaryBitVectors.begin (); it != temporaryBitVectors.end (); it++)
    {
        //Get local version
        bool isFree = it->second;

        if (isFree == true)
        {
            BitVector *bv = it->first;
            dvmClearAllBits (bv);
            return bv;
        }
    }

    //If we arrived to this point, we have no bitvector free, allocate one
    BitVector *res = dvmCompilerAllocBitVector (1, true);

    //It is not free because we are going to send it out
    temporaryBitVectors[res] = false;

    return res;
}

BasicBlockPCG *CompilationUnitPCG::getBasicBlockPCG (unsigned int index) const
{
    if (index >= blockList.numUsed)
    {
        return 0;
    }

    BasicBlockPCG *bb = (BasicBlockPCG *) dvmGrowableListGetElement (&blockList, index);

    return bb;
}

void CompilationUnitPCG::disableRegisterizationForDef (int ssaNum)
{
    std::set<int>::iterator it;

    SSANumInfo &info = getRootSSANumInformation (ssaNum);

    // Set registerize to false.  Also set the dtype to NOreg to make sure we
    // don't mistakenly read it.  If we are disabling registerization, a likely
    // reason is that we could not determine ssaNum's dtype.
    info.registerize = false;
    info.dtype = NOreg;

    it = references.find (ssaNum);

    if (it != references.end ())
    {
        references.erase (it);
    }
}

BitVector *CompilationUnitPCG::getSSANumSet (u2 vr) const
{
    std::map<u2, BitVector *>::const_iterator it = vrToSSANumSet.find (vr);

    if (it == vrToSSANumSet.end ())
    {
        return 0;
    }

    return it->second;
}

void CompilationUnitPCG::addLabelSymbolPair(CGLabel cgLabel, CGSymbol cgSymbol)
{
    label2symbol[cgLabel] = cgSymbol;
}

/**
 * @details  Attempt to find a symbol associated to a block, and return it.
 *           If one doesn't exist, create a new one and return it.
 */
CGSymbol CompilationUnitPCG::getBlockSymbol (CGLabel blockLabel)
{
    std::map<CGLabel, CGSymbol>::iterator it = label2symbol.find (blockLabel);

    CGSymbol res = CGSymbolInvalid;

    if (it == label2symbol.end ())
    {
        std::string name;
        dvmCompilerPcgGetBlockName (0, name);
        res = dvmCompilerPcgCreateSymbol (this, name, 0);
        addLabelSymbolPair(blockLabel, res);
    }
    else
    {
        res = it->second;
    }

    return res;
}

/**
 * @details Some block address might have been referenced by the block's CGSymbol.
 * Now that the code has been laid down, we can compute the addresses of those
 * symbols.  This needs to be done before we attempt to resolve references
 * to these symbols.
 */
void CompilationUnitPCG::bindBlockSymbolAddresses (uint8_t *startAddr)
{
    for (std::map<CGLabel, CGSymbol>::iterator it = label2symbol.begin(); it != label2symbol.end(); ++it)
    {
        int64_t labelOffset;
        uint8_t *labelAddr;
        CGLabel cgLabel = it->first;
        CGSymbol cgSymbol = it->second;

        CGGetLabelNameAndOffset (cgLabel, &labelOffset);
        labelAddr = startAddr + labelOffset;

        dvmCompilerPcgBindSymbolAddress (this, cgSymbol, labelAddr);
    }
}

/**
 * @details Get a symbol that will point to the memconst as described by the
 * arguments passed in. If one already exists, return it, otherwise, create a
 * new symbol to return.
 */
CGSymbol CompilationUnitPCG::getMemConstSymbol (uint8_t *value, size_t length, uint32_t align)
{
    MemConstType new_memconst;
    CGSymbol res = CGSymbolInvalid;

    memset(new_memconst.value, 0, MAX_MEMCONST_SIZE);
    memcpy(new_memconst.value, value, length);
    new_memconst.length = length;
    new_memconst.align = align;

    MemConstIterator it = memconsts.find (new_memconst);

    if (it == memconsts.end ()) {
        std::string name;

        // Clear the name
        name = "";

        // Create the name
        char buffer[128];
        snprintf (buffer, sizeof (buffer), "CGMemConst_v%x.%x.%x.%x_l%d_a%d",
                  ((uint32_t *)(new_memconst.value))[0],
                  ((uint32_t *)(new_memconst.value))[1],
                  ((uint32_t *)(new_memconst.value))[2],
                  ((uint32_t *)(new_memconst.value))[3],
                  new_memconst.length, new_memconst.align);

        name = buffer;

        res = dvmCompilerPcgCreateSymbol (this, name, 0, false, true);
        memconsts[new_memconst] = res;
        CGSetSymbolConstantValue(res, value, length);
    }
    else {
        res = it->second;
    }

    return res;
}

/**
 * @details Add a new local CGSymbol to the list
 */
void CompilationUnitPCG::addLocalSymbol(CGSymbol cgSymbol)
{
    localSymbols.push_front(cgSymbol);
}

/**
 * @details Return an iterator to the head of the local symbol list.
 */
LocalSymbolIterator CompilationUnitPCG::localSymbolBegin(void)
{
    return localSymbols.begin();
}

/**
 * @details Return an iterator to the end of the local symbol list.
 */
LocalSymbolIterator CompilationUnitPCG::localSymbolEnd(void)
{
    return localSymbols.end();
}

/**
 * @details Return the beginning of the memconsts map.
 */
MemConstIterator CompilationUnitPCG::memConstBegin(void)
{
    return memconsts.begin();
}

/**
 * @details Return the end of the memconsts map.
 */
MemConstIterator CompilationUnitPCG::memConstEnd(void)
{
    return memconsts.end();
}

unsigned int CompilationUnitPCG::getNumberOfSwitchTableEntries (void)
{
    return this->switchChainingCellEntries.size();
}

void CompilationUnitPCG::addSwitchTableEntry (CRelocation *relocation, BasicBlockPCG *chainingCellBB)
{
    SwitchTableCCXRef xref = { relocation, chainingCellBB };
    this->switchChainingCellEntries.push_back(xref);
}

SwitchTableEntryIterator CompilationUnitPCG::switchTableBegin(void)
{
    return this->switchChainingCellEntries.begin();
}

SwitchTableEntryIterator CompilationUnitPCG::switchTableEnd(void)
{
    return this->switchChainingCellEntries.end();
}
