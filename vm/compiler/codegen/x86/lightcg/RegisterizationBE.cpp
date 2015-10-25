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

#include <map>
#include <set>
#include <algorithm>
#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "Dalvik.h"
#include "Lower.h"
#include "AnalysisO1.h"
#include "RegisterizationBE.h"

//#define DEBUG_REGISTERIZATION

#ifdef DEBUG_REGISTERIZATION
#define DEBUG_ASSOCIATION(X) X
#define DEBUG_SPILLING(X) X
#define DEBUG_ASSOCIATION_MERGE(X) X
#define DEBUG_COMPILETABLE_UPDATE(X) X
#else
#define DEBUG_ASSOCIATION(X)
#define DEBUG_SPILLING(X)
#define DEBUG_ASSOCIATION_MERGE(X)
#define DEBUG_COMPILETABLE_UPDATE(X)
#endif

AssociationTable::AssociationTable(void) {
    //Call clear function, it will reset everything
    clear();
}

AssociationTable::~AssociationTable(void) {
    //Call clear function, it will reset everything
    clear();
}

void AssociationTable::clear(void) {
    DEBUG_ASSOCIATION (ALOGD ("Clearing association table\n"));

    //Clear maps
    associations.clear();
    inMemoryTracker.clear();
    constTracker.clear();

    //Have we finalized the table
    isFinal = false;
}

bool AssociationTable::copy(AssociationTable &source) {
    //We cannot copy anything if we are finalized
    assert (hasBeenFinalized() == false);

    //Insert all associations from source
    associations.insert(source.associations.begin(), source.associations.end());

    //Insert all memory trackers
    inMemoryTracker.insert(source.inMemoryTracker.begin(),
            source.inMemoryTracker.end());

    //Insert all constants
    constTracker.insert(source.constTracker.begin(), source.constTracker.end());

    //Finalize the current table and return success
    finalize();
    return true;
}

bool AssociationTable::associate(const CompileTableEntry &compileEntry)
{
    // We cannot update once the association table has been finalized
    assert (hasBeenFinalized() == false);

    // Paranoid: this must be a virtual register
    assert (compileEntry.isVirtualReg () == true);

    //Get local versions of the compileEntry
    int VR = compileEntry.regNum;
    int physicalReg = compileEntry.physicalReg;

    bool safeToUpdate = true;

    // Check if we are overwriting an existing association
    iterator assocEntry = associations.find(VR);
    if (assocEntry != associations.end())
    {
        int oldPhysicalReg = assocEntry->second.physicalReg;

        //If the new physical register is null, then we don't want to update the
        //association that we saved already.
        if (physicalReg == PhysicalReg_Null)
        {
            safeToUpdate = false;
        }

        // We might be saving VRs even when they don't have physical register
        // associated and thus we don't care for overwriting unless one has
        // physical register
        if (oldPhysicalReg != PhysicalReg_Null && physicalReg != PhysicalReg_Null) {
            // Overwriting an association must mean that we are reading from a source
            // that has the duplicate entries for the same VR. Most likely this can
            // happen when a VR is associated with XMM and GP in same trace
            ALOGI ("JIT_INFO: Overwriting association of v%d:%s with %s\n", VR,
                    physicalRegToString(static_cast<PhysicalReg>(oldPhysicalReg)),
                    physicalRegToString(static_cast<PhysicalReg>(physicalReg)));
            SET_JIT_ERROR(kJitErrorBERegisterization);
            return false;
        }
    }

    //We only do the update if it is safe
    if (safeToUpdate == true)
    {
        if (assocEntry != associations.end())
        {
            //If we already have an entry for this VR then simply update its compile entry
            assocEntry->second = compileEntry;
        }
        else
        {
            //Otherwise we insert it into our associations
            associations.insert (std::make_pair (VR, compileEntry));
        }

        DEBUG_ASSOCIATION (ALOGD ("Associating v%d with %s\n", VR,
                physicalRegToString(static_cast<PhysicalReg> (physicalReg))));
    }

    //Report success
    return true;
}

bool AssociationTable::associate(const MemoryVRInfo &memVRInfo) {
    // We cannot update once the association table has been finalized
    assert (hasBeenFinalized() == false);

    int VR = memVRInfo.regNum;

    // Make a copy of the in memory information
    inMemoryTracker[VR] = memVRInfo;

    return true;
}

bool AssociationTable::associate(const ConstVRInfo &constVRInfo) {
    // We cannot update once the association table has been finalized
    assert (hasBeenFinalized() == false);

    int VR = constVRInfo.regNum;

    // Make a copy of the in memory information
    constTracker[VR] = constVRInfo;

    return true;
}

bool AssociationTable::wasVRInMemory(int VR) const
{
    //Find the VR in the inMemoryTracker map
    inMemoryTrackerConstIterator entry = inMemoryTracker.find(VR);

    //If we cannot find then it must be in memory. Our parent would have kept track of it
    //if it used it. Since it did not use it, it must be in memory.
    if (entry == inMemoryTracker.end())
    {
        return true;
    }
    else
    {
        //Return what the entry tells us
        return entry->second.inMemory;
    }
}

bool AssociationTable::wasVRConstant(int VR) const
{
    //Find the VR in the constTracker map
    constantTrackerConstIterator entry = constTracker.find(VR);

    //Return whether we found it
    return (entry != constTracker.end());
}

int AssociationTable::getVRConstValue(int VR) const
{
    //Find the VR in the constTracker map
    constantTrackerConstIterator entry = constTracker.find(VR);

    //Paranoid: this function should not be called if wasVRConstant returns false
    assert(entry != constTracker.end());

    //Return value
    return entry->second.value;
}

void AssociationTable::finalize() {
    //Set to final
    isFinal = true;
}

void AssociationTable::findUsedRegisters (std::set<PhysicalReg> & outUsedRegisters) const
{
    // Go through all association table entries and find used registers
    for (const_iterator iter = begin(); iter != end(); iter++)
    {
        //Get a local version of the register used
        PhysicalReg regUsed = iter->second.getPhysicalReg ();

        //If not the Null register, insert it
        if (regUsed != PhysicalReg_Null)
        {
            outUsedRegisters.insert(regUsed);
        }
    }
}

void AssociationTable::printToDot(FILE * file) {
    DEBUG_ASSOCIATION(ALOGD("Printing association table to dot file"));

    if (associations.size() == 0) {
        fprintf(file, " {Association table is empty} |\\\n");
    }
    else
    {
        fprintf(file, " {Association table at entry:}|\\\n");

        //Now go through the iteration
        for (const_iterator iter = associations.begin(); iter != associations.end(); iter++) {
            //If it's a constant, print it out using %d
            if (wasVRConstant(iter->second.regNum)) {
                fprintf(file, "{v%d : %d} | \\\n", iter->first,
                        getVRConstValue(iter->second.regNum));
            } else {
                //Otherwise, use the physicalRegToString function
                fprintf(file, "{v%d : %s} | \\\n", iter->first,
                        physicalRegToString(
                            static_cast<PhysicalReg>(iter->second.physicalReg)));
            }
        }
    }
}

static bool shouldSaveAssociation(const CompileTableEntry &compileEntry)
{
    int vR = compileEntry.regNum;
    LowOpndRegType type = compileEntry.getPhysicalType ();

    // We want to save association if the VR is either in a physical register or is a constant
    bool res = compileEntry.inPhysicalRegister () || isVirtualRegConstant (vR, type, 0, false) != VR_IS_NOT_CONSTANT;

    return res;
}

bool AssociationTable::syncAssociationsWithCompileTable (AssociationTable & associationsToUpdate)
{
    if (associationsToUpdate.hasBeenFinalized ())
    {
        ALOGI ("JIT_INFO: Association table has been finalized but we want to update it.");
        SET_JIT_ERROR(kJitErrorBERegisterization);
        return false;
    }

    // Go through each entry of the compile table
    for (int entry = 0; entry < compileTable.size (); entry++)
    {
        // Update associations for every VR entry we find
        if (compileTable[entry].isVirtualReg () == true && shouldSaveAssociation (compileTable[entry]) == true)
        {
            if (associationsToUpdate.associate (compileTable[entry]) == false)
            {
                return false;
            }
        }
    }

    // Go through each entry in memVRTable to save whether VR is in memory
    for (int entry = 0; entry < num_memory_vr; entry++)
    {
        if (associationsToUpdate.associate (memVRTable[entry]) == false)
        {
            return false;
        }
    }

    // Go through each entry in constVRTable
    for (int entry = 0; entry < num_const_vr; entry++)
    {
        // Only save entry if it is actually a constant
        if (constVRTable[entry].isConst == true)
        {
            if (associationsToUpdate.associate (constVRTable[entry]) == false)
            {
                return false;
            }
        }
    }

    //Finalize the table and report success
    associationsToUpdate.finalize();

    return true;
}

bool AssociationTable::syncCompileTableWithAssociations(AssociationTable & associationsToUse) {
    DEBUG_COMPILETABLE_UPDATE(ALOGD("There are %d associations to merge",
            associationsToUse.size()));

    // Go through every association we saved
    for (AssociationTable::const_iterator assocIter = associationsToUse.begin();
            assocIter != associationsToUse.end(); assocIter++)
    {
        //Suppose we will not find the entry
        bool foundCompileTableEntry = false;

        DEBUG_COMPILETABLE_UPDATE(ALOGD("Starting to search through compile "
                "table that has %d entries", compileTable.size ()));

        int vR = assocIter->first;
        const CompileTableEntry &associationEntry = assocIter->second;

        // Now search the compile table for an appropriate entry
        for (int entry = 0; entry < compileTable.size (); entry++)
        {
            //If it is a virtual register and the right register
            if (compileTable[entry].isVirtualReg () == true
                    && compileTable[entry].getPhysicalType () == associationEntry.getPhysicalType ()
                    && compileTable[entry].getRegisterNumber () == vR)
            {
                DEBUG_COMPILETABLE_UPDATE(ALOGD("Found that v%d is in compile "
                        "table already.", assocIter->first));

                // The only relevant part we care about updating is the physical register
                compileTable[entry].setPhysicalReg (associationEntry.getPhysicalReg ());

                //Mark that we found it
                foundCompileTableEntry = true;
                break;
            }
        }

        // If we did not find an entry, we must insert it
        if (foundCompileTableEntry == false && associationEntry.isVirtualReg () == true)
        {
            DEBUG_COMPILETABLE_UPDATE(ALOGD("We have not found v%d in compile "
                        "table so we will make a new entry.", assocIter->first));

            CompileTableEntry newEntry = assocIter->second;

            //Since we added it ourselves and it wasn't there before, lets reset it
            newEntry.reset ();

            //Now set its physical register correctly
            newEntry.setPhysicalReg (assocIter->second.getPhysicalReg ());

            //Add it to the global compileTable
            compileTable.insert (newEntry);
        }
    }

    // In case we have updated the compile table, we must also update the
    // state of registers to match what compile table believes
    if (associationsToUse.size() > 0) {
        syncAllRegs();
    }

    DEBUG_COMPILETABLE_UPDATE (ALOGD ("Finished merging associations into compile table"));

    //Report success
    return true;
}

/**
 * @brief Used to represent the possibilities of the state of a virtual register.
 */
enum VirtualRegisterState
{
    VRState_InMemory = 0, //!< In memory.
    VRState_InGP,         //!< In general purpose register.
    VRState_Constant,     //!< Constant value.
    VRState_NonWideInXmm, //!< Non-wide VR in xmm register.
    VRState_WideInXmm,    //!< Wide VR in xmm register.
    VRState_HighOfWideVR, //!< The high bits when we have wide VR.
};

#ifdef DEBUG_REGISTERIZATION
/**
 * @brief Provides a mapping between the virtual register state and representative string.
 * @param state The virtual register state.
 * @return Returns string representation of virtual register state.
 */
static const char* convertVirtualRegisterStateToString (VirtualRegisterState state)
{
    switch (state)
    {
        case VRState_InMemory:
            return "in memory";
        case VRState_InGP:
            return "in GP";
        case VRState_Constant:
            return "constant";
        case VRState_NonWideInXmm:
            return "non-wide in xmm";
        case VRState_WideInXmm:
            return "wide in xmm";
        case VRState_HighOfWideVR:
            return "high of wide";
        default:
            break;
    }

    return "invalid state";
}
#endif

/**
 * @brief Container for keeping track of actions attributed with a VR when state mismatch
 * is found between two basic blocks.
 */
struct VirtualRegisterStateActions
{
    std::set<int> virtualRegistersToStore;        //!< Set of VRs to store on stack.
    std::set<int> virtualRegistersToLoad;         //!< Set of VRs to load into registers.
    std::set<int> virtualRegistersRegToReg;       //!< Set of VRs that must be moved to different registers.
    std::set<int> virtualRegistersCheckConstants; //!< Set of VRs that are constants but must be checked for consistency.
    std::set<int> virtualRegistersImmToReg;       //!< Set of VRs that are constant but must be moved to register.
};

/**
 * @brief Fills the set of virtual registers with the union of all VRs used in both parent and child.
 * @param parentAssociations The association table of parent.
 * @param childAssociations The association table of child.
 * @param virtualRegisters Updated by function to contain the set of all VRs used in both parent and child.
 */
static void filterVirtualRegisters (const AssociationTable &parentAssociations,
                                    const AssociationTable &childAssociations,
                                    std::set<int> &virtualRegisters)
{
    AssociationTable::const_iterator assocIter;

    //Simply look through all of parent's associations and save all those VRs
    for (assocIter = parentAssociations.begin (); assocIter != parentAssociations.end (); assocIter++)
    {
        virtualRegisters.insert (assocIter->first);
    }

    //Now look through all of child's associations and save all those VRs
    for (assocIter = childAssociations.begin (); assocIter != childAssociations.end (); assocIter++)
    {
        virtualRegisters.insert (assocIter->first);
    }
}

/**
 * @brief Looks through the association table to determine the state of each VR of interest.
 * @param associations The association table to look at.
 * @param virtualRegisters The virtual registers to determine state for.
 * @param vrState Updated by function
 * @return True if we can determine state of all VRs of interest. Otherwise error is set and false is returned.
 */
static bool determineVirtualRegisterState (const AssociationTable &associations,
                                           const std::set<int> &virtualRegisters,
                                           std::map<int, VirtualRegisterState> &vrState)
{
    //We iterate through every VR of interest
    for (std::set<int>::const_iterator iter = virtualRegisters.begin (); iter != virtualRegisters.end (); iter++)
    {
        int vR = *iter;

        //We are iterating over a set which is sorted container. So if we are dealing with
        //a wide VR, then we have already set the mapping for the low bits to contain information
        //about the wideness.
        std::map<int, VirtualRegisterState>::const_iterator wideIter = vrState.find (vR - 1);

        //Do we have an entry for the low VR?
        if (wideIter != vrState.end())
        {
            VirtualRegisterState lowState = wideIter->second;

            //If we have a wide VR, then set the high bits correspondingly
            if (lowState == VRState_WideInXmm)
            {
                vrState[vR] = VRState_HighOfWideVR;
                continue;
            }
        }

        //Look for the compile table entry for this VR
        AssociationTable::const_iterator assocIter = associations.find (vR);

        if (assocIter != associations.end ())
        {
            const CompileTableEntry &compileEntry = assocIter->second;

            bool inPhysicalReg = compileEntry.inPhysicalRegister ();
            bool inGP = compileEntry.inGeneralPurposeRegister ();
            bool inXMM = compileEntry.inXMMRegister ();

            //In order to have saved it, it must have been in either GP or XMM
            //It also can be in a contant, which doesn't associated with physical reg
            assert (inPhysicalReg == false || (inGP || inXMM) == true);

            if (inGP == true)
            {
                vrState[vR] = VRState_InGP;
                continue;
            }
            else if (inXMM == true)
            {
                //If it is in XMM, let's figure out if the VR is wide or not
                OpndSize size = compileEntry.getSize ();

                if (size == OpndSize_64)
                {
                    vrState[vR] = VRState_WideInXmm;
                    continue;
                }
                else if (size == OpndSize_32)
                {
                    vrState[vR] = VRState_NonWideInXmm;
                    continue;
                }
            }
            else if (inPhysicalReg == true)
            {
                ALOGI ("JIT_INFO: We failed to satisfy BB associations because we found a VR that "
                        "is in physical register but not in GP or XMM.");
                SET_JIT_ERROR(kJitErrorBERegisterization);
                return false;
            }
        }

        //Let's figure out if it is believed that this VR is constant.
        //We do this before checking if it was in memory because even if it was in memory,
        //a child generated code using the assumptions of constant.
        if (associations.wasVRConstant (vR) == true)
        {
            vrState[vR] = VRState_Constant;
            continue;
        }

        //When we get here, we have tried our best to determine what physical register was used
        //for this VR or if it was a constant. Only thing left is to see if this VR was marked as
        //in memory
        if (associations.wasVRInMemory (vR) == true)
        {
            vrState[vR] = VRState_InMemory;
            continue;
        }

        //If we make it here it means we have not figured out the state of the VR
        ALOGI ("JIT_INFO: We failed to satisfy BB associations because we couldn't figure "
                "out state of virtual register v%d.", vR);
        SET_JIT_ERROR(kJitErrorBERegisterization);
        return false;
    }

    //If we make it here we are all good
    return true;
}

/**
 * @brief For every virtual register, it compares state in parent and child and then makes a decision on action to take.
 * @param parentState Map of virtual register to its state in the parent association table.
 * @param childState Map of virtual register to its state in the child association table.
 * @param virtualRegisters List of virtual registers to make a decision for.
 * @param actions Updated by function to contain the actions to take in order to merge the two states.
 * @return Returns true if all state merging can be handled. Return false if mismatch of state is detected which
 * cannot be handled safely.
 */
static bool decideOnMismatchAction (const std::map<int, VirtualRegisterState> &parentState,
                                    const std::map<int, VirtualRegisterState> &childState,
                                    const std::set<int> &virtualRegisters,
                                    VirtualRegisterStateActions &actions)
{
    //We iterate through every VR of interest
    for (std::set<int>::const_iterator iter = virtualRegisters.begin (); iter != virtualRegisters.end (); iter++)
    {
        //Get the VR
        int vR = *iter;

        //Create an iterator so we can look through child state and parent state for the VR
        std::map<int, VirtualRegisterState>::const_iterator vrStateIter;

        //Get the state of this VR in parent
        vrStateIter = parentState.find (vR);

        //Paranoid because parentState should contain all VRs in set of virtualRegisters
        assert (vrStateIter != parentState.end());

        //Save state of this VR in parent
        VirtualRegisterState vrStateInParent = vrStateIter->second;

        //Get the state of this VR in child
        vrStateIter = childState.find (vR);

        //Paranoid because childState should contain all VRs in set of virtualRegisters
        assert (vrStateIter != childState.end());

        //Save state of this VR in child
        VirtualRegisterState vrStateInChild = vrStateIter->second;

        DEBUG_ASSOCIATION_MERGE (ALOGD ("We are looking at v%d that is %s for parent and "
                "%s for child", vR, convertVirtualRegisterStateToString (vrStateInParent),
                convertVirtualRegisterStateToString (vrStateInChild)));

        bool mismatched = (vrStateInParent != vrStateInChild);

        if (mismatched == true)
        {
            //First let's check to see if child believes VR is constant
            if (vrStateInChild == VRState_Constant)
            {
                //So we have a state mismatch and child believes that VR is a constant
                ALOGI ("JIT_INFO: Child believes VR is constant but we don't. Without a runtime check "
                        "we cannot confirm.");
                SET_JIT_ERROR(kJitErrorBERegisterization);
                return false;
            }
            //Now check if parent has it in memory
            else if (vrStateInParent == VRState_InMemory)
            {
                //The high bits of this VR will be taken care of along with the low bits since
                //we know we have a wide VR.
                if (vrStateInChild == VRState_HighOfWideVR)
                {
                    continue;
                }

                //Paranoid because we are expecting to load it into register
                assert (vrStateInChild == VRState_InGP || vrStateInChild == VRState_NonWideInXmm
                        || vrStateInChild == VRState_WideInXmm);

                DEBUG_ASSOCIATION_MERGE (ALOGD (">> We need to load v%d into register", vR));

                //If parent has it in memory but child has it in register then we need
                //to load it.
                actions.virtualRegistersToLoad.insert (vR);
            }
            else if (vrStateInChild == VRState_InMemory)
            {
                //The high bits of this VR will be taken care of along with the low bits since
                //we know we have a wide VR.
                if (vrStateInParent == VRState_HighOfWideVR)
                {
                    continue;
                }

                DEBUG_ASSOCIATION_MERGE (ALOGD (">> We need to store v%d back on stack", vR));

                //Add it to set to store back
                actions.virtualRegistersToStore.insert (vR);
            }
            else if (vrStateInParent == VRState_Constant && vrStateInChild == VRState_InGP)
            {
                DEBUG_ASSOCIATION_MERGE (ALOGD (">> We need to move immediate into GP for v%d", vR));

                //Add it to set to do imm to reg move
                actions.virtualRegistersImmToReg.insert (vR);
            }
            else
            {
                DEBUG_ASSOCIATION_MERGE (ALOGD (">> We must store v%d in memory and then reload in "
                        "proper place due to mismatch", vR));

                //On state mismatch, the easiest solution is to store the VR into memory and then load
                //it back into proper state
                actions.virtualRegistersToStore.insert (vR);

                //If child believes that this VR is the high part of the wide VR,
                //then the load of the low part into xmm will take care of this case
                if (vrStateInChild != VRState_HighOfWideVR)
                {
                    actions.virtualRegistersToLoad.insert (vR);
                }
            }
        }
        else
        {
            if (vrStateInParent == VRState_InGP || vrStateInParent == VRState_NonWideInXmm
                    || vrStateInParent == VRState_WideInXmm)
            {
                DEBUG_ASSOCIATION_MERGE (ALOGD(">> We need to do a reg to reg move for v%d", vR));

                // Insert it into set that needs to be handled via reg to reg moves
                actions.virtualRegistersRegToReg.insert (vR);
            }
            else if (vrStateInParent == VRState_Constant)
            {
                DEBUG_ASSOCIATION_MERGE (ALOGD (">> We need to check constants to ensure "
                        "consistency for v%d", vR));

                //We will need to do a constant check to make sure we have same constant
                actions.virtualRegistersCheckConstants.insert (vR);
            }
            else
            {
                //We have nothing to do
                DEBUG_ASSOCIATION_MERGE (ALOGD (">> We have nothing to do because state matches for v%d", vR));
            }
        }
    }

    //If we make it here everything went okay
    return true;
}

/**
 * @brief Compares the constant in each virtual register in order to figure out that they match.
 * @param parentAssociations The association table of parent.
 * @param childAssociations The association table of child.
 * @param virtualRegistersCheckConstants
 * @return Returns false if it finds a case when the constant value for same VR differs between parent and child.
 */
static bool checkConstants (const AssociationTable &parentAssociations,
                            const AssociationTable &childAssociations,
                            const std::set<int> &virtualRegistersCheckConstants)
{
    //Iterate through all VRs that are constants in both parent and child to check that
    //the constant value matches
    for (std::set<int>::const_iterator iter = virtualRegistersCheckConstants.begin ();
            iter != virtualRegistersCheckConstants.end (); iter++)
    {
        int vR = *iter;

        //Get value parent believes for this VR
        int parentValue = parentAssociations.getVRConstValue (vR);

        //Get value child believes for this VR
        int childValue = childAssociations.getVRConstValue (vR);

        if (parentValue != childValue)
        {
            //If there is a mismatch, there's nothing we can do about it
            ALOGI ("JIT_INFO: Both child and parent believe VR is constant but each believes "
                        "it is a different value");
            SET_JIT_ERROR(kJitErrorBERegisterization);
            return false;
        }
    }

    //If we make it here, all constants match in value
    return true;
}

/**
 * @brief Decides whether merging state of parent to match its child can be done safely.
 * @param childAssociations The association table of child.
 * @param actions Updated by function to contain the actions to take in order to merge the two states.
 * @return Returns true if all state merging can be handled. Return false if mismatch of state is detected which
 * cannot be handled safely.
 */
static bool canHandleMismatch (const AssociationTable &childAssociations,
                               VirtualRegisterStateActions &actions)
{
    //We want to make it easy to compare state of child and state of parent and thus
    //we load the compile table into an association table. The parent associations
    //will no longer be valid once we start actioning on mismatch
    AssociationTable parentAssociations;
    if (AssociationTable::syncAssociationsWithCompileTable (parentAssociations) == false)
    {
        //When loading from compile table problems were found. It's best to bail early.
        return false;
    }

    //Now figure out which virtual registers are used in each state so we can start
    //figuring out any state mismatch
    std::set<int> virtualRegisters;
    filterVirtualRegisters (parentAssociations, childAssociations, virtualRegisters);

    //For each virtual register we want to figure out the state in both parent and child
    std::map<int, VirtualRegisterState> childState, parentState;
    if (determineVirtualRegisterState(childAssociations, virtualRegisters, childState) == false)
    {
        return false;
    }
    if (determineVirtualRegisterState(parentAssociations, virtualRegisters, parentState) == false)
    {
        return false;
    }

    //Now we need to make a decision when we have a mismatch

    bool result = decideOnMismatchAction (parentState, childState, virtualRegisters, actions);

    if (result == false)
    {
        //While searching for action on mismatch, we found a state we couldn't deal with
        //so we now return false.
        return false;
    }

    // Now that we figured out mismatch and also actions for each, let's look at constants
    // for both parent and child. We want to make sure that if child believes a VR is constant,
    // the parent believes it is the same constant.
    result = checkConstants (parentAssociations, childAssociations, actions.virtualRegistersCheckConstants);

    if (result == false)
    {
        //If we found non matching constants, we must bail out because there's nothing we can do
        return false;
    }

    //If we make it here, we can handle the mismatch
    return true;
}

/**
 * @brief Sets up mapping between virtual registers and their physical registers.
 * @param associationsToUse the association to compare ourselves with
 * @param otherVRToPhysicalReg what is the associationsToUse having as associations (updated by the function)
 * @param currentVRToPhysicalReg what is the current association state between VRs to Physical (updated by the function)
 */
static void initAssociationHelperTables (const AssociationTable &associationsToUse,
                                         std::map<int, PhysicalReg> &otherVRToPhysicalReg,
                                         std::map<int, PhysicalReg> &currentVRToPhysicalReg)
{
    // First we need to go through each of the child's association entries
    // to figure out each VR's association with physical register
    for (AssociationTable::const_iterator assocIter = associationsToUse.begin();
            assocIter != associationsToUse.end(); assocIter++)
    {
        if (assocIter->second.physicalReg != PhysicalReg_Null)
        {
            // Save the mapping to physical register
            otherVRToPhysicalReg[assocIter->first] =
                    static_cast<PhysicalReg>(assocIter->second.physicalReg);
        }
    }

    // Now go through current compile table to figure out what VRs are in
    // physical registers
    for (int entry = 0; entry < compileTable.size (); entry++)
    {
        if (isVirtualReg(compileTable[entry].physicalType)
                && compileTable[entry].physicalReg != PhysicalReg_Null)
        {
            // Save the mapping to physical register
            currentVRToPhysicalReg[compileTable[entry].regNum] =
                    static_cast<PhysicalReg>(compileTable[entry].physicalReg);
        }
    }
}

/**
 * @brief Writes back virtual registers to stack.
 * @param virtualRegistersToStore Set of virtual registers to write back.
 * @param trySkipWriteBack This enables an optimization where the write backs are only handled
 * if VR is in the BB's write back requests. This can be useful when spilling to memory for loop
 * entry but loop never reads the written value.
 * @param writeBackRequests Used when trySkipWriteBack is true. This is a vector of VR writeback
 * requests from the basic block.
 * @param registersToFree Used when trySkipWriteBack is true. This is a set of physical registers
 * to ensure that the VR associated with it gets written back.
 * @return Returns true if successfully writes back all VRs to memory.
 */
static bool writeBackVirtualRegistersToMemory (const std::set<int> &virtualRegistersToStore,
                                               bool trySkipWriteBack = false,
                                               const BitVector *writeBackRequests = 0,
                                               const std::set<PhysicalReg> *registersToFree = 0)
{
    //Write back anything that is in the set of VRs to store
    for (std::set<int>::const_iterator setOpIter = virtualRegistersToStore.begin ();
            setOpIter != virtualRegistersToStore.end ();
            setOpIter++)
    {
        int vR = *setOpIter;

        //Now look through compile table to find the matching entry
        for (int entry = 0; entry < compileTable.size (); entry++)
        {
            CompileTableEntry &compileEntry = compileTable[entry];

            //Do we have a match in compile table with this VR we want to write back?
            if (compileEntry.isVirtualReg () == true && compileEntry.getRegisterNumber () == vR)
            {
                //We want to skip the write back if the optimization is enabled.
                bool skipWriteBack = (trySkipWriteBack == true);

                //However, we do NOT want to skip writeback if it is in set of registers to free
                //because someone wants this VR out of that physical register.
                if (skipWriteBack == true && registersToFree != 0)
                {
                    skipWriteBack = registersToFree->find (compileTable[entry].getPhysicalReg ())
                            == registersToFree->end ();
                }

                //Finally we do NOT want to skip writeback if this VR is in vector of writeback requests
                if (skipWriteBack == true && writeBackRequests != 0)
                {
                    skipWriteBack = dvmIsBitSet (writeBackRequests, vR) == false;
                }

                //If we are not skipping the write back, then we actually need to do it
                if (skipWriteBack == false)
                {
                    DEBUG_ASSOCIATION_MERGE (ALOGD ("Writing v%d back to memory", vR));

                    //Handle the write back when in physical register
                    if (compileEntry.inPhysicalRegister () == true)
                    {
                        //Try to write back the virtual register
                        if (spillLogicalReg (entry, true) < 0)
                        {
                            return false;
                        }
                    }
                    else
                    {
                        //We make it here if the VR is not in physical register. Try figuring out
                        //if this is a constant. If it isn't a constant, we are okay because there's
                        //nothing we need to write back.
                        bool res = writeBackVRIfConstant (vR, LowOpndRegType_gp);

                        //If this VR was constant, then since we wrote it back we mark it as non-constant.
                        if (res == true)
                        {
                            setVRToNonConst (vR, OpndSize_32);
                        }
                    }
                }
            }
        }
    }

    //Since we have spilled VRs, lets make sure we properly keep track
    //of which physical registers are currently being used
    syncAllRegs();

    //If we make it here, everything is okay.
    return true;
}

/**
 * @brief Find scratch registers and fill the scratchReg set
 * @param childUsedReg registers used by the child at entrance
 * @param scratchRegs any scratch register at the end of parent's code generation (updated by the function)
 */
static void findScratchRegisters (const std::set<PhysicalReg> &childUsedReg,
                                  std::set<PhysicalReg> &scratchRegs)
{
    // All free registers are candidates for use as scratch
    std::set<PhysicalReg> parentFreeReg;
    findFreeRegisters (parentFreeReg);

    // Subtract child used registers from parent free registers
    // so we can figure out what we can use as scratch
    std::set_difference(parentFreeReg.begin(), parentFreeReg.end(),
            childUsedReg.begin(), childUsedReg.end(),
            std::inserter(scratchRegs, scratchRegs.end()));

#ifdef DEBUG_REGISTERIZATION
    //Debugging purposes
    std::set<PhysicalReg>::const_iterator scratchRegIter;
    for (scratchRegIter = scratchRegs.begin();
            scratchRegIter != scratchRegs.end(); scratchRegIter++) {
        DEBUG_ASSOCIATION_MERGE(ALOGD("%s is free for use as scratch\n",
                physicalRegToString(*scratchRegIter)));
    }
#endif
}

/**
 * @brief Find the registers to be moved and fill the regToRegMoves map
 * @param virtualRegistersRegToReg Set of virtual registers that must be moved to new registers
 * @param childVRToPhysicalReg child association between VRs and physical registers at child code generation entrance
 * @param currentVRToPhysicalReg child association between VRs and physical registers at current code generation exit
 * @param regToRegMoves register to register moves to be done (updated by function)
 * @return Returns true if we successfully can determine the moves to be done.
 */
static bool findRegistersToMove (const std::set<int> &virtualRegistersRegToReg,
                                 std::map<int, PhysicalReg> &childVRToPhysicalReg,
                                 std::map<int, PhysicalReg> &currentVRToPhysicalReg,
                                 std::map<PhysicalReg, PhysicalReg> &regToRegMoves)
{
    std::set<int>::const_iterator setOpIter;

    //Now we need to filter the reg to reg moves so walk through all of them
    for (setOpIter = virtualRegistersRegToReg.begin ();
            setOpIter != virtualRegistersRegToReg.end ();
            setOpIter++)
    {
        int VR = *setOpIter;
        PhysicalReg childReg = childVRToPhysicalReg[VR];
        PhysicalReg currentReg = currentVRToPhysicalReg[VR];

        // Check whether they are in the same physical register
        if(childReg != currentReg) {
            DEBUG_ASSOCIATION_MERGE(ALOGD("We are moving %s to %s",
                    physicalRegToString(currentReg),
                    physicalRegToString(childReg)));

            if (regToRegMoves.find(currentReg) != regToRegMoves.end()) {
                ALOGI ("JIT_INFO: We are overwriting the reg to reg move from %s",
                        physicalRegToString(currentReg));
                SET_JIT_ERROR(kJitErrorBERegisterization);
                return false;
            }

            // We want to generate a move that takes value from current physical
            // register and puts it in the physical register child expects it
            regToRegMoves[currentReg] = childReg;
        }
    }

    //Report success
    return true;
}

/**
 * @brief Move registers following the regToReg map
 * @param regToRegMoves the register to register move order
 * @param scratchRegs the registers that are scratch and can be safely used for moving registers
 * @param currentVRToPhysicalReg current VR to physical registers at end of current BasicBlock generation
 * @return whether or not the move registers succeeds
 */
static bool moveRegisters (std::map<PhysicalReg, PhysicalReg> &regToRegMoves,
                           const std::set<PhysicalReg> &scratchRegs,
                           std::map<int, PhysicalReg> currentVRToPhysicalReg)
{
    std::map<PhysicalReg, PhysicalReg>::const_iterator regToRegIter;

    //Go through each register to register request
    for (regToRegIter = regToRegMoves.begin (); regToRegIter != regToRegMoves.end (); regToRegIter++)
    {
        std::vector<PhysicalReg> toBeMoved;
        std::vector<PhysicalReg>::reverse_iterator moveIter;
        PhysicalReg source = regToRegIter->first;
        PhysicalReg dest = regToRegIter->second;

        // Paranoid because we cannot move to null register
        if (dest == PhysicalReg_Null)
        {
            continue;
        }

        if (regToRegMoves.count (source) > 1)
        {
            ALOGI ("JIT_INFO: We have the same physical register as source "
                    "multiple times.");
            SET_JIT_ERROR(kJitErrorBERegisterization);
            return false;
        }

        DEBUG_ASSOCIATION_MERGE (ALOGD ("We want to move from %s to %s", physicalRegToString(source),
                physicalRegToString(dest)));

        toBeMoved.push_back (source);
        toBeMoved.push_back (dest);

        // We eagerly assume that we won't find a cycle but we want to do something special if we do
        bool cycleFound = false;

        // Now look through the rest of the moves to see if anyone
        // is going to replace source register
        std::map<PhysicalReg, PhysicalReg>::const_iterator regToRegFinder;
        for (regToRegFinder = regToRegMoves.find (dest); regToRegFinder != regToRegMoves.end (); regToRegFinder =
                regToRegMoves.find (regToRegFinder->second))
        {

            // If we already have this register in the toBeMoved list,
            // it means we found a cycle. Instead of doing a find,
            // keeping track of this information in a bit vector would be
            // better. However, PhysicalReg enum contains invalid
            // physical registers so it is hard to decide which ones we
            // need to keep track of.
            if (std::find (toBeMoved.begin (), toBeMoved.end (), regToRegFinder->second) != toBeMoved.end ())
            {
                cycleFound = true;
                // Save this because we will need to move value into it
                toBeMoved.push_back (regToRegFinder->second);
                break;
            }

            // Save this because we will need to move value into it
            toBeMoved.push_back (regToRegFinder->second);
        }

        // If we have a cycle, we might need to use memory for doing the swap
        bool useMemoryForSwap = false;

        if (cycleFound == true)
        {
            // If we find a cycle, the last value in the toBeMoved list
            // is the register that caused cycle. Lets pop it off
            // for now so we can use std::replace below but we will
            // reinsert it
            PhysicalReg cycleCause = toBeMoved.back ();
            toBeMoved.pop_back ();

            // Lets hope we have a scratch register to break the cycle
            PhysicalReg scratch = getScratch (scratchRegs, getTypeOfRegister (source));

            if (scratch != PhysicalReg_Null)
            {
                // When we get here we found a scratch register

                // Thus if we had C->A B->C A->B
                // Now we want to have A->T C->A B->C T->B

                // Which means that toBeMoved contains A B C when we get here and now we
                // want it to have T B C A T

                // Replace A with T so we have T B C instead of A B C
                std::replace (toBeMoved.begin (), toBeMoved.end (), cycleCause, scratch);

                // Now add A so we have T B C A
                toBeMoved.push_back (cycleCause);

                // Now add T so we have T B C A T
                toBeMoved.push_back (scratch);
            }
            else
            {
                useMemoryForSwap = true;
            }
        }

        if (useMemoryForSwap == true)
        {
            ALOGI ("JIT_INFO: We have no scratch registers so we must use memory for swap");
            SET_JIT_ERROR(kJitErrorBERegisterization);
            return false;
        }

        // Now handle the actual reg to reg moves
        PhysicalReg tmpIter = PhysicalReg_Null;
        for (moveIter = toBeMoved.rbegin (); moveIter != toBeMoved.rend (); moveIter++)
        {
            PhysicalReg dest = tmpIter;
            PhysicalReg source = *(moveIter);

            //Remember source
            tmpIter = source;

            //If destination is null, next iteration
            if (dest == PhysicalReg_Null)
            {
                continue;
            }

            DEBUG_ASSOCIATION_MERGE(ALOGD("Moving %s to %s", physicalRegToString(source), physicalRegToString(dest)));

            OpndSize regSize = OpndSize_32;

            //If we have an xmm to xmm move, then we set the operand size to 64-bits. The reason
            //for this is because move_reg_to_reg function expects this size so it can use a MOVQ.
            //We may be able to get away with doing a MOVD if we have a 32-bit FP loaded with a MOVSS,
            //but we don't have the API for it and we would need additional logic here.
            if (source >= PhysicalReg_StartOfXmmMarker && source <= PhysicalReg_EndOfXmmMarker)
            {
                regSize = OpndSize_64;
            }

            // Do the actual reg to reg move
            move_reg_to_reg_noalloc (regSize, source, true, dest, true);

            // We have moved from reg to reg, but we must also update the
            // entry in the compile table
            std::map<int, PhysicalReg>::iterator currentVRToRegIter;
            for (currentVRToRegIter = currentVRToPhysicalReg.begin ();
                    currentVRToRegIter != currentVRToPhysicalReg.end (); currentVRToRegIter++)
            {
                int VR = currentVRToRegIter->first;
                int oldReg = currentVRToRegIter->second;

                if (oldReg == source)
                {
                    updatePhysicalRegForVR (VR, source, dest);
                    currentVRToRegIter->second = dest;
                }
            }

            // Now remove entry from map
            regToRegMoves[source] = PhysicalReg_Null;
        }
    }

    // Since we update the physical registers for some of the VRs lets sync up register usage with compile table
    syncAllRegs();

    //Report success
    return true;
}

/**
 * @brief Load virtual registers to child's physical registers requests
 * @param virtualRegistersToLoad Set of virtual registers we need to load into physical registers.
 * @param associationsToUse the association table for the child at code generation entrance
 * @param childVRToPhysicalReg the child map of Virtual Register to physical register at start of code generation
 * @return whether or not the function succeeds
 */
static bool loadVirtualRegistersForChild (const std::set<int> &virtualRegistersToLoad,
                                          const AssociationTable &associationsToUse,
                                          const std::map<int, PhysicalReg> &childVRToPhysicalReg)
{
    std::set<int>::const_iterator setOpIter;

    //Now walk through the VRs we need to load
    for (setOpIter = virtualRegistersToLoad.begin();
            setOpIter != virtualRegistersToLoad.end(); setOpIter++)
    {
        int VR = *setOpIter;

        //Look to see if we have a physical register for this VR
        std::map<int, PhysicalReg>::const_iterator findRegIter;
        findRegIter = childVRToPhysicalReg.find (VR);

        //This should never happen but could happen in rare cases. For example,
        //if a VR is wide in child, then it might be possible we get a request
        //to load the high part of VR into a non-existent register. However if
        //the VR is wide, we should have already handled case of loading both
        //low and high parts into an XMM.
        if (findRegIter == childVRToPhysicalReg.end ())
        {
            //If we don't actually have a physical register then there's nowhere
            //to load this VR. Thus we can safely skip it.
            continue;
        }

        PhysicalReg targetReg = findRegIter->second;

        //This should never happen but it will make buffer overflow checkers happy
        if (targetReg >= PhysicalReg_Null)
        {
            continue;
        }

        const CompileTableEntry *childCompileEntry = 0;

        // Look through child's association entries to find the type of the VR
        // so we can load it properly into the physical register
        for (AssociationTable::const_iterator assocIter = associationsToUse.begin ();
                assocIter != associationsToUse.end (); assocIter++)
        {
            const CompileTableEntry &compileEntry = assocIter->second;

            if (compileEntry.getPhysicalReg () == targetReg)
            {
                //We found the proper entry so let's get some information from it
                childCompileEntry = &compileEntry;
                break;
            }
        }

        //Paranoid: this should never happen
        if (childCompileEntry == 0)
        {
            ALOGD ("JIT_INFO: Trying to load virtual register for child but cannot find compile entry");
            SET_JIT_ERROR (kJitErrorBERegisterization);
            return false;
        }

        //Paranoid
        assert (childCompileEntry->isVirtualReg () == true);

        //Get the physical type
        LowOpndRegType type = childCompileEntry->getPhysicalType ();

        DEBUG_ASSOCIATION_MERGE (ALOGD ("Loading v%d to %s", VR, physicalRegToString(targetReg)));

        // Load VR into the target physical register
        if (type == LowOpndRegType_ss)
        {
            const int vrOffset = getVirtualRegOffsetRelativeToFP (VR);
            move_ss_mem_to_reg_noalloc (vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, VR, targetReg, true);
        }
        else
        {
            OpndSize size = childCompileEntry->getSize ();
            get_virtual_reg_noalloc (VR, size, targetReg, true);
        }

        //Look for the entry to update in compile table
        CompileTable::iterator entryToUpdate = compileTable.findVirtualRegister (VR, type);

        if (entryToUpdate != compileTable.end ())
        {
            //We found a matching entry so simply update its physical register
            entryToUpdate->setPhysicalReg (targetReg);
        }
        else
        {
            //If we were not able to find an entry, then we can just copy it from child's association table

            //Make a copy of new entry
            CompileTableEntry newEntry = (*childCompileEntry);

            //Since we copied it over, let's reset it
            newEntry.reset ();

            //Make sure that the physical register is set
            newEntry.setPhysicalReg (targetReg);

            //We now copy into the compile table
            compileTable.insert (newEntry);

            //Since we just loaded it from memory, we keep it marked as being in memory and add it to the
            //memory table in order to keep track of it.
            addToMemVRTable (newEntry.regNum, true);

            //If it is a 64-bit wide operand, we also need to add its high part to the memory table.
            if (newEntry.getSize() == OpndSize_64)
            {
                addToMemVRTable (newEntry.regNum + 1, true);
            }
        }
    }

    // We loaded some VRs into physical registers. Lets keep registers synced
    syncAllRegs();

    //Report success
    return true;
}

/**
 * @brief Moves constant virtual registers values into physical registers.
 * @details The parent must believe VR is constant and child must want it in physical register.
 * @param immToRegMoves The virtual registers whose constant value must be moved to physical register.
 * @param childVRToPhysicalReg Mapping between child virtual register and the physical register they are in.
 * @return Returns true if successfully moves all immediates into physical registers. Otherwise it sets
 * error and returns false.
 */
static bool moveImmediates (const std::set<int> &immToRegMoves,
                            const std::map<int, PhysicalReg> &childVRToPhysicalReg)
{
    std::set<int>::const_iterator setOpIter;

    //Now walk through the constant VRs we need to move to physical registers
    for (setOpIter = immToRegMoves.begin (); setOpIter != immToRegMoves.end (); setOpIter++)
    {
        int vR = *setOpIter;

        //We can only handle immediate to GP moves so we can preset the type
        LowOpndRegType type = LowOpndRegType_gp;
        OpndSize size = getRegSize (static_cast<int> (LowOpndRegType_gp));

        //Make space for constant value
        int constantValue;

        //We want to get the constant value so we check if virtual register is constant.
        //Since we just care to do immediate to GP register move, we pass only enough space
        //for non-wide VR.
        if (isVirtualRegConstant (vR, type, &constantValue) == VR_IS_NOT_CONSTANT)
        {
            ALOGI ("JIT_INFO: We decided that we need to do an imm to reg move but now VR is no longer constant.");
            SET_JIT_ERROR (kJitErrorBERegisterization);
            return false;
        }

        //Look to see if we have a physical register for this VR
        std::map<int, PhysicalReg>::const_iterator findRegIter;
        findRegIter = childVRToPhysicalReg.find (vR);

        //This should never happen
        if (findRegIter == childVRToPhysicalReg.end ())
        {
            ALOGI ("JIT_INFO: We decided that we need to do an imm to reg move but we cannot find register.");
            SET_JIT_ERROR (kJitErrorBERegisterization);
            return false;
        }

        PhysicalReg targetReg = findRegIter->second;

        //Paranoid because we only support GP moves
        assert (targetReg >= PhysicalReg_StartOfGPMarker && targetReg <= PhysicalReg_EndOfGPMarker);

        //Do the actual move now
        move_imm_to_reg_noalloc (size, constantValue, targetReg, true);

        //Since we have it in physical register, lets invalidate its constantness
        setVRToNonConst (vR, size);

        //Look for the entry to update in compile table
        CompileTable::iterator entryToUpdate = compileTable.findVirtualRegister (vR, type);

        if (entryToUpdate != compileTable.end ())
        {
            //We found a matching entry so simply update its physical register
            entryToUpdate->setPhysicalReg (targetReg);
        }
        else
        {
            //Since we don't have an entry already we can make one right now
            CompileTableEntry newEntry (vR, type, LowOpndRegType_virtual);

            //We now copy into the compile table
            compileTable.insert (newEntry);

            //If the constant was already marked as being in memory, then our VR is still technically
            //in memory and thus we don't need to update its in memory state right now
        }
    }

    //Report success
    return true;
}

bool AssociationTable::satisfyBBAssociations (BasicBlock_O1 * parent,
        BasicBlock_O1 * child, bool isBackward)
{
    // To get here, it must be the case that this child's associations have
    // already been finalized
    assert (child->associationTable.hasBeenFinalized() == true);

    /**
     * This function merges associations, therefore it needs to know:
     *   - The child's associations
     *   - The parent's associations
     *   - How both associations can be synchronized
     */

    AssociationTable &childAssociations = child->associationTable;
    VirtualRegisterStateActions actions;

    // 1) Gather information on current associations and the child's and decide
    // on actions for dealing with state mismatch between VRs
    if (canHandleMismatch (childAssociations, actions) == false)
    {
        return false;
    }

    //Look at child to see what physical registers it is using
    std::set<PhysicalReg> childUsedReg;
    childAssociations.findUsedRegisters (childUsedReg);

    // 2) We write back anything child wants in memory because this will allow us to have scratch
    // registers in case we need to do reg to reg moves. The function that does the writing has
    // a flag on whether it is allowed to try to skip a write back. We allow writeback skipping
    // if we have a loop (isBackward is true or block loops back to itself). The reason we allow
    // it is because some VRs are not inter-iteration dependent and thus we don't care for them
    // to be back in memory if we're not going to read them.
    if (writeBackVirtualRegistersToMemory (actions.virtualRegistersToStore, isBackward == true || parent == child,
            parent->requestWriteBack, &childUsedReg) == false)
    {
        return false;
    }

    // 3) Prepare for doing reg to reg moves by finding scratch registers, finding mapping between
    // VRs and their physical register, and for deciding which registers to move.

    //Figure out the scratch registers we have available.
    std::set<PhysicalReg> scratchRegs;
    findScratchRegisters (childUsedReg, scratchRegs);

    //Initialize helper maps in regards to parent and child associations
    std::map<int, PhysicalReg> childVRToPhysicalReg, currentVRToPhysicalReg;
    initAssociationHelperTables (childAssociations, childVRToPhysicalReg, currentVRToPhysicalReg);

    //Find the registers that should be moved
    std::map<PhysicalReg, PhysicalReg> regToRegMoves;

    if (findRegistersToMove (actions.virtualRegistersRegToReg, childVRToPhysicalReg,
            currentVRToPhysicalReg, regToRegMoves) == false)
    {
        //If findRegistersToMove fails, we bail too
        return false;
    }

    // 4) Do the actual moving of registers to the correct physical register
    if (moveRegisters (regToRegMoves, scratchRegs, currentVRToPhysicalReg) == false)
    {
        //If moveRegisters fails, we bail too
        return false;
    }

    // 5) Load any VRs we believe is in memory because child wants it in physical register
    if (loadVirtualRegistersForChild (actions.virtualRegistersToLoad, childAssociations, childVRToPhysicalReg) == false)
    {
        //If moveToChildPhysical fails, we bail too
        return false;
    }

    // 6) Now handle any immediate to GP register moves
    if (moveImmediates (actions.virtualRegistersImmToReg, childVRToPhysicalReg) == false)
    {
        //If move immediates fails it will set error message. We simply propagate it now.
        return false;
    }

    //If we make it here, everything went okay so we report success
    return true;
}

bool AssociationTable::handleSpillRequestsFromME (BasicBlock_O1 *bb)
{
    //Initialize empty set of VRs to write back
    std::set<int> virtualRegisterToWriteBack;

    //We need to iterate through the writeback requests to add them to our set of VRs
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit(bb->requestWriteBack, &bvIterator);

    //Go through each VR so we can add it to our set
    for (int vR = dvmBitVectorIteratorNext (&bvIterator); vR != -1; vR = dvmBitVectorIteratorNext (&bvIterator))
    {
        virtualRegisterToWriteBack.insert (vR);
    }

    //Do the actual write back
    bool result = writeBackVirtualRegistersToMemory (virtualRegisterToWriteBack);

    return result;
}

/**
 * @details First we handle any spill requests for the current basic block
 * so we do not pass useless associations to child. Then if child already
 * has an existing association table, we generate instructions to match
 * our state to that. If the child does not, then we tell it what our
 * current associations are. If the child is a chaining cell or exit block,
 * we spill everything because those BBs are handled specially and are exit
 * points.
 */
bool AssociationTable::createOrSyncTable (BasicBlock_O1 * bb, bool forFallthrough)
{
    // Before we pass association tables, lets handle spill requests from ME
    // so we don't pass anything useless for associations
    if (handleSpillRequestsFromME (bb) == false)
    {
        return false;
    }

    //Get child depending on the forFallthrough boolean
    BasicBlock_O1 * child = reinterpret_cast<BasicBlock_O1 *> (forFallthrough ? bb->fallThrough : bb->taken);

    //If there is a child
    if (child != NULL) {

        //If it is not a dalvik code and it's not prebackward block,
        //then write back and free all registers because we might
        //be exiting to interpreter.
        if (child->blockType != kDalvikByteCode && child->blockType != kPreBackwardBlock)
        {
            freeReg (true);
        }
        else
        {
            if (child->associationTable.hasBeenFinalized () == false)
            {
                // If the child's association table has not been finalized then we can
                // update it now. However, if we don't have any MIRs in this BB,
                // it means the compile table has not been updated and thus we can
                // just copy associations
                if (syncAssociationsWithCompileTable (child->associationTable) == false)
                {
                    return false;
                }
            }
            else
            {
                //Otherwise, let's satisfy the associations for the child
                if (satisfyBBAssociations (bb, child) == false)
                {
                    return false;
                }
            }
        }
    }

    //Report success
    return true;
}
