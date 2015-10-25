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

#ifndef REGISTERIZATIONBE_H_
#define REGISTERIZATIONBE_H_

#include <map>

// Forward declarations
struct BasicBlock_O1;
class CompileTableEntry;
struct ConstVRInfo;
struct MemoryVRInfo;

/**
 * @brief Used to keep track of virtual registers and their various associations.
 * @details Keeps track of compile table information associated with VR including
 * the physical register, the in memory state of a VR, and the constantness of VR.
 */
class AssociationTable {
public:
    /**
     * @brief Looks through all associations and finds used physical registers
     * @param outUsedRegisters is a set that is updated with the used physical
     * registers
     */
    void findUsedRegisters(std::set<PhysicalReg> & outUsedRegisters) const;

    /**
     * @brief Once association table is been finalized, this can be called to
     * find out if the virtual register was in memory.
     * @param vR virtual register
     * @return Returns whether or not VR was in memory
     */
    bool wasVRInMemory(int vR) const;

    /**
     * @brief Once association table is been finalized, this can be called to
     * find out if the virtual register was a constant.
     * @details For wide VRs, this should be called twice to find out if both
     * low order bits and high order bits were constant.
     * @param vR virtual register
     * @return Returns whether or not virtual register was a constant.
     */
    bool wasVRConstant(int vR) const;

    /**
     * @brief Returns the 32 bit constant value associated with VR
     * @pre wasVRConstant should return true
     * @param vR virtual register
     * @return Returns the constant value of virtual register.
     */
    int getVRConstValue(int vR) const;

    /**
     * @brief Updates association table given a compile entry from the compile table.
     * @param compileEntry compilation entry
     * @return Returns whether adding compile entry to associations was successful
     */
    bool associate(const CompileTableEntry &compileEntry);

    /**
     * @brief Updates association table given a memory VR information
     * @param memVRInfo the memory virtual register information entry
     * @return Returns whether adding compile entry to associations was successful
     */
    bool associate(const MemoryVRInfo &memVRInfo);

    /**
     * @brief Updates association table given a constant VR information
     * @param constVRInfo the constant virtual register entry
     * @return Returns whether adding compile entry to associations was successful
     */
    bool associate(const ConstVRInfo &constVRInfo);

    /**
     * @brief Used to determine whether the association table can be
     * updated anymore.
     * @return Returns whether the associations are final and cannot be updated.
     */
    bool hasBeenFinalized(void) const {
        return isFinal;
    }

    /**
     * @brief Used to tell association table that it cannot accept anymore
     * updates.
     */
    void finalize();

    /**
     * @brief Clears the association table.
     */
    void clear(void);

    /**
     * @brief Used to copy another association table into current one
     * @param source association table to read from
     * @return whether the copy was successful
     */
    bool copy(AssociationTable & source);

    /**
     * Returns number of entries in association table
     * @return size of association table
     */
    size_t size(void) const {
        return this->associations.size();
    }

    /**
     * @brief Prints the association table to a file separating entries with
     * vertical bar.
     * @param file File to print to.
     */
    void printToDot(FILE * file);

    /**
     * @brief Random access const iterator. This does not modify structure it is iterating.
     */
    typedef std::map<int, CompileTableEntry>::const_iterator const_iterator;

    /**
     * @brief Random access iterator. This may modify structure it is iterating.
     */
    typedef std::map<int, CompileTableEntry>::iterator iterator;

    /**
     * @brief Returns an iterator pointing to the first association.
     * @return iterator to beginning
     */
    iterator begin() {
        return associations.begin();
    }

    /**
     * @brief Returns a const iterator pointing to the first association.
     * @return iterator to beginning
     */
    const_iterator begin() const {
        return associations.begin();
    }

    /**
     * @brief Returns an iterator referring to the past-the-end association.
     * @return iterator past end
     */
    iterator end() {
        return associations.end();
    }

    /**
     * @brief Returns a const iterator referring to the past-the-end
     * association.
     * @return iterator past end
     */
    const_iterator end() const {
        return associations.end();
    }

    /**
     * @brief Returns a const interator to the compile table entry matching the desired VR.
     * @param vR The virtual register to look for.
     * @return Returns iterator matching the result found or iterator that equals end() when
     * no match is found.
     */
    const_iterator find (const int &vR) const {
        return associations.find(vR);
    }

    AssociationTable(void); /**< @brief Constructor */

    ~AssociationTable(void); /**< @brief Destructor */

    //Static functions of the RegisterizationBE class

    /**
     * @brief Updates a given association table using the current state of the
     * compile table.
     * @param associationsToUpdate Association table to update.
     * @return Returns whether the association table was updated successfully.
     */
    static bool syncAssociationsWithCompileTable(AssociationTable & associationsToUpdate);

    /**
     * @brief Updates the current state of the compile table to all VR entries
     * in the association table
     * @param associationsToUse Association table to use for compile table updated.
     * @return Returns whether the compile table update was successful.
     */
    static bool syncCompileTableWithAssociations(AssociationTable & associationsToUse);

    /**
     * @brief Creates association table for child or generates instructions to
     * match it.
     * @param bb Parent basic block
     * @param forFallthrough Flag on whether should update fallthrough child. Else
     * we update taken child.
     * @return Whether the sync was successful.
     */
    static bool createOrSyncTable(BasicBlock_O1 * bb, bool forFallthrough = true);

    /**
     * @brief Generates instructions to match current state of parent basic block
     * to the association table state of child.
     * @param parent Parent basic block.
     * @param child Child basic block.
     * @param isBackward Used to denote whether we are satisfying associations
     * of loop entry. If yes, then we only write back phi nodes.
     * @return Returns whether the state of parent successfully matches state of
     * child.
     */
    static bool satisfyBBAssociations (BasicBlock_O1 * parent,
            BasicBlock_O1 * child, bool isBackward = false);

    /**
     * @brief Spills virtual registers marked for spilling by the middle end.
     * @param bb Basic block whose spill requests we need to handle.
     * @return Returns whether we successfully handled spill requests.
     */
    static bool handleSpillRequestsFromME(BasicBlock_O1 * bb);

private:
    /**
     * @brief Map for every VR to its corresponding compile table entry
     * when association occurred.
     */
    std::map<int, CompileTableEntry> associations;

    /**
     * @brief Map for every VR to its state in memory when the association
     * occurred.
     */
    std::map<int, MemoryVRInfo> inMemoryTracker;

    /**
     * @typedef Iterator for use with inMemoryTracker.
     */
    typedef std::map<int, MemoryVRInfo>::const_iterator inMemoryTrackerConstIterator;

    /**
     * @brief Map for every VR to its constant value (if it had any) when the
     * association occurred.
     */
    std::map<int, ConstVRInfo> constTracker;

    /**
     * @typedef Iterator for use with constTracker
     */
    typedef std::map<int, ConstVRInfo>::const_iterator constantTrackerConstIterator;

    /**
     * @brief Keeps track whether association table has been finalized.
     */
    bool isFinal;
};

#endif /* REGISTERIZATIONBE_H_ */
