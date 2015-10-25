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

#ifndef COMPILETABLE_H_
#define COMPILETABLE_H_

#include <vector>
#include <map>
#include "AnalysisO1.h"

/**
 * @brief Represents an entry to the compilation table, helping the compiler follow what register is where.
 * @details The pair <regNum, physicalType> uniquely determines a variable
 */
class CompileTableEntry {
public:
    /**
     * @brief Constructor which initializes an entry with its register number and type.
     * @param regNum The register number: vr number, temp number, or hardcoded register number.
     * @param physicalType the LowOpndRegType for this register. Should reflect both its physical
     * type and its logical type.
     */
    CompileTableEntry (int regNum, int physicalType) :
            regNum (regNum), physicalType (physicalType), physicalReg (PhysicalReg_Null),
            refCount(0), spill_loc_index (-1), isWritten (false), linkageToVR (0)
    {
        logicalType = static_cast<LogicalRegType> (physicalType & ~MASK_FOR_TYPE);
        size = getRegSize (physicalType);
    }

    /**
     * @brief Constructor which initializes an entry with its register number, its logical type, and
     * its physical type.
     * @param regNum The register number: vr number, temp number, or hardcoded register number.
     * @param physicalType The physical type for this register.
     * @param logicalType The logical type for this register.
     */
    CompileTableEntry (int regNum, LowOpndRegType physicalType, LogicalRegType logicalType) :
            regNum (regNum), physicalReg (PhysicalReg_Null), refCount(0), spill_loc_index (-1),
            isWritten (false), logicalType (logicalType), linkageToVR (0)
    {
        this->physicalType = logicalType | physicalType;
        size = getRegSize (physicalType);
    }

    /**
     * @brief Constructs a compile table entry which represents a virtual register.
     * @param vrInfo The virtual register info to use for initialization.
     */
    CompileTableEntry (const VirtualRegInfo &vrInfo) :
            regNum (vrInfo.regNum), physicalType (LowOpndRegType_virtual | vrInfo.physicalType),
            physicalReg (PhysicalReg_Null), refCount(vrInfo.refCount), spill_loc_index (-1),
            isWritten (false), logicalType (LowOpndRegType_virtual), linkageToVR (0)
    {
        size = getRegSize (vrInfo.physicalType);
    }

    /**
     * @brief Constructs a compile table entry which represents a temporary.
     * @param tempInfo The temporary info to use for initialization.
     */
    CompileTableEntry (const TempRegInfo &tempInfo) :
            regNum (tempInfo.regNum), physicalType (tempInfo.physicalType),
            physicalReg (PhysicalReg_Null), refCount(tempInfo.refCount),
            spill_loc_index (-1), isWritten (false), linkageToVR (tempInfo.linkageToVR)
    {
        logicalType = static_cast<LogicalRegType> (physicalType & ~MASK_FOR_TYPE);
        size = getRegSize (tempInfo.physicalType);
    }

    /**
     * @brief Destructor.
     */
    ~CompileTableEntry (void)
    {
        reset ();
    }

    /**
     * @brief Get the register number.
     * @return Returns the register number for this entry.
     */
    int getRegisterNumber (void) const
    {
        return regNum;
    }

    /**
     * @brief Used to get the physical type for this entry.
     * @details This returns only type of physical register usable for this entry.
     * @return Returns the physical type for this entry
     */
    LowOpndRegType getPhysicalType (void) const
    {
        return static_cast<LowOpndRegType> (physicalType & MASK_FOR_TYPE);
    }

    /**
     * @brief Used to get the logical type.
     * @return Returns the logical type for the entry.
     */
    LogicalRegType getLogicalType (void) const
    {
        return logicalType;
    }

    /**
     * @brief Used to get an integer whose low 3 bits represent the physical type and the high bits
     * represent the logical type.
     * @return Returns the representation of logical and physical types.
     */
    int getLogicalAndPhysicalTypes (void) const
    {
        //For now the physical type field holds both the logical and physical types so we return that
        return physicalType;
    }

    /**
     * @brief Used to get the physical register.
     * @return Returns the physical register used for this entry. If no register is used, it returns
     * PhysicalReg_Null.
     */
    PhysicalReg getPhysicalReg (void) const
    {
        return static_cast<PhysicalReg> (physicalReg);
    }

    /**
     * @brief Used to get the size of this entry which depends on the physical type.
     * @return Returns the size of the physical type for this entry.
     */
    OpndSize getSize (void) const
    {
        return size;
    }

    /**
     * @brief Sets a new physical register for this entry.
     * @param newReg The new physical register.
     */
    void setPhysicalReg (PhysicalReg newReg)
    {
        setPhysicalReg (static_cast<int> (newReg));
    }

    /**
     * @brief Sets a new physical register for this entry.
     * @param newReg The new physical register.
     */
    void setPhysicalReg (int newReg)
    {
        // It doesn't make sense to set physical register to a non-existent register.
        // Thus we have this check here for sanity.
        assert (newReg <= PhysicalReg_Null);
        physicalReg = newReg;
    }

    /**
     * @brief Updates the reference count for this entry.
     * @param newCount The reference count to set.
     */
    void updateRefCount (int newCount)
    {
        refCount = newCount;
    }

    /**
     * @brief Used to reset the spilled location of temporary thus marking it as non-spilled.
     */
    void resetSpillLocation ()
    {
        spill_loc_index = -1;
    }

    /**
     * @brief Checks if entry is in a physical register.
     * @return Returns whether this entry is in a physical register.
     */
    bool inPhysicalRegister (void) const
    {
        return physicalReg != PhysicalReg_Null;
    }

    /**
     * @brief Checks if entry is in a general purpose register.
     * @return Returns whether this entry is in a general purpose register.
     */
    bool inGeneralPurposeRegister (void) const
    {
        return (physicalReg >= PhysicalReg_StartOfGPMarker && physicalReg <= PhysicalReg_EndOfGPMarker);
    }

    /**
     * @brief Checks if entry is in an xmm register.
     * @return Returns whether this entry is in an xmm register.
     */
    bool inXMMRegister (void) const
    {
        return (physicalReg >= PhysicalReg_StartOfXmmMarker && physicalReg <= PhysicalReg_EndOfXmmMarker);
    }

    /**
     * @brief Checks if entry is in an X87 register.
     * @return Returns whether this entry is in an X87 register.
     */
    bool inX87Register (void) const
    {
        return (physicalReg >= PhysicalReg_StartOfX87Marker && physicalReg <= PhysicalReg_EndOfX87Marker);
    }

    /**
     * @brief Checks whether logical type represents a virtual register.
     * @return Returns whether this entry represent a virtual register.
     */
    bool isVirtualReg (void) const
    {
        return ((physicalType & LowOpndRegType_virtual) != 0);
    }

    /**
     * @brief Checks if this is a backend temporary used during bytecode generation.
     * @return Returns whether this entry represent a backend temporary.
     */
    bool isTemporary (void) const;

    /**
     * @brief Links a temporary to a corresponding virtual register.
     * @param vR The virtual register number.
     */
    void linkToVR (int vR)
    {
        assert (isTemporary () == true);
        linkageToVR = vR;
    }

    /**
     * @brief Given that the entry is a temporary, it returns the virtual register it is linked to.
     * @return Returns corresponding virtual register.
     */
    int getLinkedVR (void) const
    {
        assert (isTemporary () == true);
        return linkageToVR;
    }

    /**
     * @brief Resets properties of compile entry to default values. Does not reset the type and register represented
     * by this compile entry.
     */
    void reset (void);

    /**
     * @brief Equality operator for checking equivalence.
     * @details The pair <regNum, physicalType> uniquely determines a variable.
     * @param other The compile table entry to compare to
     * @return Returns true if the two entries are equivalent.
     */
    bool operator== (const CompileTableEntry& other) const
    {
        if (regNum == other.regNum && physicalType == other.physicalType)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * @brief For a given state number it remembers some properties about the compile entry.
     * @param stateNum The state number to associate current state with.
     * @return Returns true if it successfully remembered state.
     */
    bool rememberState (int stateNum);

    /**
     * @brief Updates the current state of the compile entry to match the state we are interested in.
     * @param stateNum The state number to look at for updating self state.
     * @return Returns true if it successfully changed to the other state.
     */
    bool goToState (int stateNum);

    /**
     * @brief Provides physical register for an entry for a specific state.
     * @param stateNum The state to look at.
     * @return Returns the physical register for that state.
     */
    int getStatePhysicalRegister (int stateNum)
    {
        return state[stateNum].physicalReg;
    }

    /**
     * @brief Provides spill location for an entry for a specific state.
     * @param stateNum The state to look at.
     * @return Returns the spill location for that state.
     */
    int getStateSpillLocation (int stateNum)
    {
        return state[stateNum].spill_loc_index;
    }

    int regNum;               /**< @brief The register number */

    /**
     * @brief This field holds BOTH physical type (like XMM register) and the logical type (like virtual register)
     * @details The low 7 bits hold LowOpndRegType and the rest of bits hold LogicalRegType
     */
    int physicalType;

    int physicalReg;          /**< @brief Which physical register was chosen */

    int refCount;             /**< @brief Number of reference counts for the entry */

    int spill_loc_index;      /**< @brief what is the spill location index (for temporary registers only) */
    bool isWritten;           /**< @brief is the entry written */

private:
    /**
     * @brief Used to save the state of register allocator.
     */
    struct RegisterState
    {
        int spill_loc_index; //!< @brief Keeps track of CompileTableEntry::spill_loc_index
        int physicalReg;     //!< @brief Keeps track of CompileTableEntry::physicalReg
    };

    /**
     * @brief Keeps track of the register state for state number.
     */
    std::map<int, RegisterState> state;

    LogicalRegType logicalType;  /**< @brief The logical type for this entry */
    OpndSize size;               /**< @brief Used to keep track of size of entry */
    int linkageToVR;             /**< @brief Linked to which VR, for temporary registers only */
};

class CompileTable
{
public:
    /**
     * @brief Used to access an element of the compile table.
     * @details If index matches the key of an element in the container, the function returns a reference to its mapped
     * value. If index does not match the key of any element in the container, the function inserts a new element with
     * that key and returns a reference to its mapped value.
     * @param index The index of the entry we want to access.
     * @return Returns a reference to the mapped value of the element with a key value equivalent to index.
     */
    CompileTableEntry& operator[] (size_t index)
    {
        return compileTable[index];
    }

    /**
     * @brief Used to access an element of the compile table in a constant fashion.
     * @details If index matches the key of an element in the container, the function returns a reference to its mapped
     * value. If index does not match the key of any element in the container, the function inserts a new element with
     * that key and returns a reference to its mapped value.
     * @param index The index of the entry we want to access.
     * @return Returns a reference to the mapped value of the element with a key value equivalent to index.
     */
    const CompileTableEntry& operator[] (size_t index) const
    {
        return compileTable[index];
    }

    /**
     * @brief Random access const iterator. This does not modify structure it is iterating.
     */
    typedef std::vector<CompileTableEntry>::const_iterator const_iterator;

    /**
     * @brief Random access iterator. This may modify structure it is iterating.
     */
    typedef std::vector<CompileTableEntry>::iterator iterator;

    /**
     * @brief Returns an iterator pointing to the first compile entry.
     * @return iterator to beginning
     */
    iterator begin (void)
    {
        return compileTable.begin ();
    }

    /**
     * @brief Returns a const iterator pointing to the first compile entry.
     * @return iterator to beginning
     */
    const_iterator begin (void) const
    {
        return compileTable.begin ();
    }

    /**
     * @brief Returns an iterator referring to the past-the-end compile entry.
     * @return iterator past end
     */
    iterator end (void)
    {
        return compileTable.end ();
    }

    /**
     * @brief Returns a const iterator referring to the past-the-end compile entry.
     * @return iterator past end
     */
    const_iterator end (void) const
    {
        return compileTable.end ();
    }

    /**
     * @brief Used to get an iterator pointing to the entry matching number and type.
     * @param regNum The register number (can be temp, virtual, or hardcoded)
     * @param physicalType The physical type and logical type representing the entry.
     * @return Returns iterator pointing to the desired entry. If one is not found, it
     * returns the past-the-end iterator.
     */
    iterator find (int regNum, int physicalType);

    /**
     * @brief Used to get a const iterator pointing to the entry matching number and type.
     * @param regNum The register number (can be temp, virtual, or hardcoded)
     * @param physicalType The physical type and logical type representing the entry.
     * @return Returns const iterator pointing to the desired entry. If one is not found, it
     * returns the past-the-end const iterator.
     */
    const_iterator find (int regNum, int physicalType) const;

    /**
     * @brief Used to get an iterator pointing to the entry matching number and type.
     * @param regNum The register number (can be temp, virtual, or hardcoded)
     * @param physicalType The physical type of entry
     * @param logicalType The logical type (virtual, temp, etc)
     * @return Returns iterator pointing to the desired entry. If one is not found, it
     * returns the past-the-end iterator.
     */
    iterator find (int regNum, LowOpndRegType physicalType, LogicalRegType logicalType);

    /**
     * @brief Used to get a const iterator pointing to the entry matching number and type.
     * @param regNum The register number (can be temp, virtual, or hardcoded)
     * @param physicalType The physical type of entry
     * @param logicalType The logical type (virtual, temp, etc)
     * @return Returns const iterator pointing to the desired entry. If one is not found, it
     * returns the past-the-end const iterator.
     */
    const_iterator find (int regNum, LowOpndRegType physicalType, LogicalRegType logicalType) const;

    /**
     * @brief Used to get an iterator pointing to the virtual register whose physical type matches.
     * @param regNum The virtual register number.
     * @param physicalType The physical type of the virtual register.
     * @return Returns iterator pointing to the desired entry. If one is not found, it
     * returns the past-the-end iterator.
     */
    iterator findVirtualRegister (int regNum, LowOpndRegType physicalType);

    /**
     * @brief Used to get a const iterator pointing to the virtual register whose physical type matches.
     * @param regNum The virtual register number.
     * @param physicalType The physical type of the virtual register.
     * @return Returns const iterator pointing to the desired entry. If one is not found, it
     * returns the past-the-end const iterator.
     */
    const_iterator findVirtualRegister (int regNum, LowOpndRegType physicalType) const;

    /**
     * @brief Used to get size of compile table.
     * @return Returns the size of the compile table.
     */
    int size (void) const
    {
        return compileTable.size ();
    }

    /**
     * @brief Used to insert a new entry into the compile table.
     * @param newEntry The compile table entry to insert into the table.
     */
    void insert (const CompileTableEntry &newEntry)
    {
        compileTable.push_back (newEntry);
    }

    /**
     * @brief Used to clear the compile table.
     */
    void clear (void)
    {
        compileTable.clear ();
    }

private:
    /**
     * @brief Used to keep track of the entries in the compile table.
     * @todo Ideally this should be a set or a map so that lookup is fast.
     */
    std::vector<CompileTableEntry> compileTable;
};

extern CompileTable compileTable;

#endif /* COMPILETABLE_H_ */
