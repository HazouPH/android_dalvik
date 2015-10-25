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


/*! \file AnalysisO1.cpp
  \brief This file implements register allocator, constant folding
*/
#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "libdex/DexOpcodes.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "interp/InterpState.h"
#include "interp/InterpDefs.h"
#include "libdex/Leb128.h"
#include "../../RegisterizationME.h"
#include "Scheduler.h"
#include "Singleton.h"
#include <set>
#include "Utility.h"

/* compilation flags to turn on debug printout */
//#define DEBUG_COMPILE_TABLE
//#define DEBUG_ALLOC_CONSTRAINT
//#define DEBUG_REGALLOC
//#define DEBUG_REG_USED
//#define DEBUG_REFCOUNT
//#define DEBUG_REACHING_DEF2
//#define DEBUG_REACHING_DEF
//#define DEBUG_LIVE_RANGE
//#define DEBUG_ENDOFBB
//#define DEBUG_CONST
//#define DEBUG_XFER_POINTS
//#define DEBUG_DSE
//#define DEBUG_CFG
//#define DEBUG_GLOBALTYPE
//#define DEBUG_STATE
//#define DEBUG_COMPILE_TABLE
//#define DEBUG_VIRTUAL_INFO
//#define DEBUG_MERGE_ENTRY
//#define DEBUG_INVALIDATE
#define DEBUG_MEMORYVR(X)
#define DEBUG_MOVE_OPT(X)
#define DEBUG_SPILL(X)

#include "AnalysisO1.h"
#include "CompileTable.h"

void dumpCompileTable();

/**
 * @brief Check whether a type is a virtual register type
 * @param type the logical type of the register
 * @return whether type is a virtual register or not
 */
bool isVirtualReg(int type) {
    return ((type & LowOpndRegType_virtual) != 0);
}

/**
 * @brief Check whether the logical type represents a temporary
 * @param type the logical type of the register
 * @param regNum the register number
 * @return Returns true if we are looking at a temporary, scratch, or hardcoded register.
 */
bool isTemporary (int type, int regNum)
{
    //Create a compile entry in order to ask it if we have a temporary
    CompileTableEntry compileEntry (regNum, type);

    return compileEntry.isTemporary ();
}

/**
 * @brief Given a physical register it determines if it is scratch type.
 * @param reg The physical register.
 * @return Returns whether the register is scratch
 */
static bool isScratchReg (int reg)
{
    //A register is a scratch register if its physical type is one of the scratch ones
    bool isScratch = (reg >= static_cast<int> (PhysicalReg_SCRATCH_1)
            && reg <= static_cast<int> (PhysicalReg_SCRATCH_10));

    return isScratch;
}

/** convert type defined in lowering module to type defined in register allocator
    in lowering module <type, isPhysical>
    in register allocator: LowOpndRegType_hard LowOpndRegType_virtual LowOpndRegType_scratch
*/
int convertType(int type, int reg, bool isPhysical) {
    int newType = type;
    if(isPhysical) newType |= LowOpndRegType_hard;
    if(isVirtualReg(type)) newType |= LowOpndRegType_virtual;
    else {
        /* reg number for a VR can exceed PhysicalReg_SCRATCH_1 */
        if(isScratchReg (reg) == true)
        {
            newType |= LowOpndRegType_scratch;
        }
    }
    return newType;
}

/** return the size of a variable
 */
OpndSize getRegSize(int type) {
    if((type & MASK_FOR_TYPE) == LowOpndRegType_xmm) return OpndSize_64;
    if((type & MASK_FOR_TYPE) == LowOpndRegType_fs) return OpndSize_64;
    /* for type _gp, _fs_s, _ss */
    return OpndSize_32;
}

/*
   Overlapping cases between two variables A and B
   layout for A,B   isAPartiallyOverlapB  isBPartiallyOverlapA
   1> |__|  |____|         OVERLAP_ALIGN        OVERLAP_B_COVER_A
      |__|  |____|
   2> |____|           OVERLAP_B_IS_LOW_OF_A    OVERLAP_B_COVER_LOW_OF_A
        |__|
   3> |____|           OVERLAP_B_IS_HIGH_OF_A   OVERLAP_B_COVER_HIGH_OF_A
      |__|
   4> |____|      OVERLAP_LOW_OF_A_IS_HIGH_OF_B OVERLAP_B_COVER_LOW_OF_A
         |____|
   5>    |____|   OVERLAP_HIGH_OF_A_IS_LOW_OF_B OVERLAP_B_COVER_HIGH_OF_A
      |____|
   6>   |__|           OVERLAP_A_IS_LOW_OF_B    OVERLAP_B_COVER_A
      |____|
   7> |__|             OVERLAP_A_IS_HIGH_OF_B   OVERLAP_B_COVER_A
      |____|
*/
/** determine the overlapping between variable B and A
*/
OverlapCase getBPartiallyOverlapA(int regB, LowOpndRegType tB, int regA, LowOpndRegType tA) {
    if(getRegSize(tA) == getRegSize(tB) && regA == regB) return OVERLAP_B_COVER_A;
    if(getRegSize(tA) == OpndSize_64 && getRegSize(tB) == OpndSize_32 && regA == regB) return OVERLAP_B_COVER_LOW_OF_A;
    if(getRegSize(tA) == OpndSize_64 && getRegSize(tB) == OpndSize_32 && regB == regA + 1) return OVERLAP_B_COVER_HIGH_OF_A;
    if(getRegSize(tA) == OpndSize_32 && getRegSize(tB) == OpndSize_64 && (regA == regB || regA == regB+1)) return OVERLAP_B_COVER_A;
    if(getRegSize(tB) == OpndSize_64 && getRegSize(tA) == OpndSize_64 && regA == regB+1) return OVERLAP_B_COVER_LOW_OF_A;
    if(getRegSize(tB) == OpndSize_64 && getRegSize(tA) == OpndSize_64 && regB == regA+1) return OVERLAP_B_COVER_HIGH_OF_A;
    return OVERLAP_NO;
}

/** determine overlapping between variable A and B
*/
OverlapCase getAPartiallyOverlapB(int regA, LowOpndRegType tA, int regB, LowOpndRegType tB) {
    if(getRegSize(tA) == getRegSize(tB) && regA == regB) return OVERLAP_ALIGN;
    if(getRegSize(tA) == OpndSize_64 && getRegSize(tB) == OpndSize_32 && regA == regB)
        return OVERLAP_B_IS_LOW_OF_A;
    if(getRegSize(tA) == OpndSize_64 && getRegSize(tB) == OpndSize_32 && regB == regA+1)
        return OVERLAP_B_IS_HIGH_OF_A;
    if(getRegSize(tB) == OpndSize_64 && getRegSize(tA) == OpndSize_64 && regA == regB+1)
        return OVERLAP_LOW_OF_A_IS_HIGH_OF_B;
    if(getRegSize(tB) == OpndSize_64 && getRegSize(tA) == OpndSize_64 && regB == regA+1)
        return OVERLAP_HIGH_OF_A_IS_LOW_OF_B;
    if(getRegSize(tA) == OpndSize_32 && getRegSize(tB) == OpndSize_64 && regA == regB)
        return OVERLAP_A_IS_LOW_OF_B;
    if(getRegSize(tA) == OpndSize_32 && getRegSize(tB) == OpndSize_64 && regA == regB+1)
        return OVERLAP_A_IS_HIGH_OF_B;
    return OVERLAP_NO;
}

/** determine whether variable A fully covers B
 */
bool isAFullyCoverB(int regA, LowOpndRegType tA, int regB, LowOpndRegType tB) {
    if(getRegSize(tB) == OpndSize_32) return true;
    if(getRegSize(tA) == getRegSize(tB) && regA == regB) return true;
    return false;
}

/*
   RegAccessType accessType
   1> DefOrUse.accessType
      can only be D(VR), L(low part of VR), H(high part of VR), N(none)
      for def, it means which part of the VR is live
      for use, it means which part of the VR comes from the def
   2> VirtualRegInfo.accessType
      for currentInfo, it can only be a combination of U & D
      for entries in infoBasicBlock, it can be a combination of U & D|L|H
*/

/*
   Key data structures used:
   1> BasicBlock_O1
      VirtualRegInfo infoBasicBlock[]
      DefUsePair* defUseTable
      XferPoint xferPoints[]
   2> MemoryVRInfo memVRTable[]
      LiveRange* ranges
   3> CompileTableEntry compileTable[]
   4> VirtualRegInfo
      DefOrUse reachingDefs[3]
   5> DefUsePair, LiveRange
*/

//! one entry for each variable used

//! a variable can be virtual register, or a temporary (can be hard-coded)
CompileTable compileTable;
//! tables to save the states of register allocation
regAllocStateEntry2 stateTable2_1[NUM_MEM_VR_ENTRY];
regAllocStateEntry2 stateTable2_2[NUM_MEM_VR_ENTRY];
regAllocStateEntry2 stateTable2_3[NUM_MEM_VR_ENTRY];
regAllocStateEntry2 stateTable2_4[NUM_MEM_VR_ENTRY];

//! array of TempRegInfo to store temporaries accessed by a single bytecode
TempRegInfo infoByteCodeTemp[MAX_TEMP_REG_PER_BYTECODE];
int num_temp_regs_per_bytecode;
//! array of MemoryVRInfo to store whether a VR is in memory
MemoryVRInfo memVRTable[NUM_MEM_VR_ENTRY];
int num_memory_vr;

CompilationUnit* currentUnit = NULL;

//! the current basic block
BasicBlock_O1* currentBB = NULL;

/**
 * @brief Information for each physical register
 * @details Initialized during code generation
 */
RegisterInfo allRegs[PhysicalReg_Last+1];

//! this array says whether a spill location is used (0 means not used, 1 means used)
int spillIndexUsed[MAX_SPILL_JIT_IA];

int inGetVR_num = -1;
int inGetVR_type;

///////////////////////////////////////////////////////////////////////////////
// FORWARD FUNCTION DECLARATION
void addExceptionHandler(s4 tmp);

int createCFG(Method* method);
void dumpVirtualInfoOfBasicBlock(BasicBlock_O1* bb);
void setTypeOfVR();
void dumpVirtualInfoOfMethod();

/**
 * @brief entry point to generate native code for a O1 basic block
 * @param method the method that compiled trace belong to
 * @param bb current O1 basic block
 * @param cUnit current O1 compilation unit
 * @return 0 if generate code successfully, return value < 0 if error occured
 */
int codeGenBasicBlock(const Method* method, BasicBlock_O1* bb, CompilationUnit_O1* cUnit);

//used in collectInfoOfBasicBlock: getVirtualRegInfo
int mergeEntry2 (BasicBlock_O1* bb, VirtualRegInfo &currentInfo);
int sortAllocConstraint(RegAllocConstraint* allocConstraints,
                        RegAllocConstraint* allocConstraintsSorted, bool fromHighToLow);

//Updates compile table with information about virtual register usage
static void insertFromVirtualInfo (const VirtualRegInfo &regInfo);

//Updates compile table with information about temporary usage
static void insertFromTempInfo (const TempRegInfo &tempRegInfo);

static int updateXferPoints (BasicBlock_O1 *bb);
static int updateLiveTable (BasicBlock_O1 *bb);
static void handleStartOfBBXferPoints (BasicBlock_O1 *bb);
void printDefUseTable();
bool isFirstOfHandler(BasicBlock_O1* bb);

//used in mergeEntry2
//following functions will not update global data structure
RegAccessType mergeAccess2(RegAccessType A, RegAccessType B, OverlapCase isBPartiallyOverlapA);
RegAccessType updateAccess1(RegAccessType A, OverlapCase isAPartiallyOverlapB); //will not update global data structure
RegAccessType updateAccess2(RegAccessType C1, RegAccessType C2);
RegAccessType updateAccess3(RegAccessType C, RegAccessType B);

void updateDefUseTable (VirtualRegInfo &currentInfo);
static int updateReachingDefA (VirtualRegInfo &currentInfo, int indexToA, OverlapCase isBPartiallyOverlapA);
static int updateReachingDefB1 (VirtualRegInfo &currentInfo, VirtualRegInfo &tmpInfo, int indexToA);
static int updateReachingDefB2 (VirtualRegInfo &currentInfo, VirtualRegInfo &tmpInfo);
void updateReachingDefB3 (VirtualRegInfo &currentInfo);

RegAccessType insertAUse(DefUsePair* ptr, int offsetPC, int regNum, LowOpndRegType physicalType);
DefUsePair* insertADef(BasicBlock_O1 *bb, int offsetPC, int regNum, LowOpndRegType pType, RegAccessType rType);
RegAccessType insertDefUsePair (VirtualRegInfo &currentInfo, int reachingDefIndex);

//used in updateXferPoints
int fakeUsageAtEndOfBB (BasicBlock_O1* bb, int vR, int physicalAndLogicalType);
static int insertLoadXfer(int offset, int regNum, LowOpndRegType pType);
int searchMemTable(int regNum);
static int mergeLiveRange(int tableIndex, int rangeStart, int rangeEnd);
//used in updateLiveTable
RegAccessType setAccessTypeOfUse(OverlapCase isDefPartiallyOverlapUse, RegAccessType reachingDefLive);
DefUsePair* searchDefUseTable(int offsetPC, int regNum, LowOpndRegType pType);
void insertAccess(int tableIndex, LiveRange* startP, int rangeStart);

/**
 * @brief Checks whether the opcode can branch or switch
 * @param opcode the Dalvik mnemonic
 * @return true if opcode can branch or switch
 */
static inline bool isCurrentByteCodeJump(Opcode opcode)
{
    //Get the opcode flags
    int flags = dvmCompilerGetOpcodeFlags (opcode);

    //Check whether it can branch or switch
    return (flags & (kInstrCanBranch | kInstrCanSwitch)) != 0;
}

/* this function is called before code generation of basic blocks
   initialize data structure allRegs, which stores information for each physical register,
   whether it is used, when it was last freed, whether it is callee-saved */
void initializeAllRegs() {
    //Initialize entire array
    memset (allRegs, PhysicalReg_Null, sizeof (allRegs));

    int k;
    for(k = PhysicalReg_EAX; k <= PhysicalReg_EBP; k++) {
        allRegs[k].physicalReg = (PhysicalReg) k;
        if(k == PhysicalReg_EDI || k == PhysicalReg_ESP || k == PhysicalReg_EBP)
            allRegs[k].isUsed = true;
        else {
            allRegs[k].isUsed = false;
            allRegs[k].freeTimeStamp = -1;
        }
        if(k == PhysicalReg_EBX || k == PhysicalReg_EBP || k == PhysicalReg_ESI || k == PhysicalReg_EDI)
            allRegs[k].isCalleeSaved = true;
        else
            allRegs[k].isCalleeSaved = false;
    }
    for(k = PhysicalReg_XMM0; k <= PhysicalReg_XMM7; k++) {
        allRegs[k].physicalReg = (PhysicalReg) k;
        allRegs[k].isUsed = false;
        allRegs[k].freeTimeStamp = -1;
        allRegs[k].isCalleeSaved = false;
    }
}

/** sync up allRegs (isUsed & freeTimeStamp) with compileTable
    global data: RegisterInfo allRegs[PhysicalReg_Null]
    update allRegs[EAX to XMM7] except EDI,ESP,EBP
    update RegisterInfo.isUsed & RegisterInfo.freeTimeStamp
        if the physical register was used and is not used now
*/
void syncAllRegs() {
    int k, k2;
    for(k = PhysicalReg_EAX; k <= PhysicalReg_XMM7; k++) {
        if(k == PhysicalReg_EDI || k == PhysicalReg_ESP || k == PhysicalReg_EBP)
            continue;
        //check whether the physical register is used by any logical register
        bool stillUsed = false;
        for(k2 = 0; k2 < compileTable.size (); k2++) {
            if(compileTable[k2].physicalReg == k) {
                stillUsed = true;
                break;
            }
        }
        if(stillUsed && !allRegs[k].isUsed) {
            allRegs[k].isUsed = true;
        }
        if(!stillUsed && allRegs[k].isUsed) {
            allRegs[k].isUsed = false;
        }
    }
    return;
}

/**
 * @brief Looks through all physical registers and determines what is used
 * @param outFreeRegisters is a set that is updated with the unused physical registers
 * @param includeGPs Whether or not to include general purpose registers
 * @param includeXMMs Whether or not to include XMM registers
 */
void findFreeRegisters (std::set<PhysicalReg> &outFreeRegisters, bool includeGPs, bool includeXMMs)
{
    if (includeGPs == true)
    {
        // Go through all GPs
        for (int reg = PhysicalReg_StartOfGPMarker; reg <= PhysicalReg_EndOfGPMarker; reg++)
        {
            //If it is not used, then we can add it to the list of free registers
            if (allRegs[reg].isUsed == false)
            {
                outFreeRegisters.insert (static_cast<PhysicalReg> (reg));
            }
        }
    }

    if (includeXMMs == true)
    {
        // Go through all XMMs
        for (int reg = PhysicalReg_StartOfXmmMarker; reg <= PhysicalReg_EndOfXmmMarker; reg++)
        {
            if (allRegs[reg].isUsed == false)
            {
                outFreeRegisters.insert (static_cast<PhysicalReg> (reg));
            }
        }
    }
}

/**
 * @brief Given a list of scratch register candidates and a register type,
 * it returns a scratch register of that type
 * @param scratchCandidates registers that can be used for scratch
 * @param type xmm or gp
 * @return physical register which can be used as scratch
 */
PhysicalReg getScratch(const std::set<PhysicalReg> &scratchCandidates, LowOpndRegType type) {
    if (type != LowOpndRegType_gp && type != LowOpndRegType_xmm) {
        return PhysicalReg_Null;
    }

    int start = (
            type == LowOpndRegType_gp ?
                    PhysicalReg_StartOfGPMarker : PhysicalReg_StartOfXmmMarker);
    int end = (
            type == LowOpndRegType_gp ?
                    PhysicalReg_EndOfGPMarker : PhysicalReg_EndOfXmmMarker);

    PhysicalReg candidate = PhysicalReg_Null;
    std::set<PhysicalReg>::const_iterator iter;
    for (iter = scratchCandidates.begin(); iter != scratchCandidates.end();
            iter++) {
        PhysicalReg scratch = *iter;
        if (scratch >= start && scratch <= end) {
            candidate = scratch;
            break;
        }
    }

    return candidate;
}

/**
 * @brief Given a physical register, it returns its physical type
 * @param reg physical register to check
 * @return Returns LowOpndRegType_gp if register general purpose.
 * Returns LowOpndRegType_xmm if register is XMM. Returns
 * LowOpndRegType_fs is register is stack register for x87.
 * Otherwise, returns LowOpndRegType_invalid for anything else.
 */
LowOpndRegType getTypeOfRegister(PhysicalReg reg) {
    if (reg >= PhysicalReg_StartOfGPMarker && reg <= PhysicalReg_EndOfGPMarker)
        return LowOpndRegType_gp;
    else if (reg >= PhysicalReg_StartOfXmmMarker
            && reg <= PhysicalReg_EndOfXmmMarker)
        return LowOpndRegType_xmm;
    else if (reg >= PhysicalReg_StartOfX87Marker
            && reg <= PhysicalReg_EndOfX87Marker)
        return LowOpndRegType_fs;

    return LowOpndRegType_invalid;
}

/**
 * @brief Synchronize the spillIndexUsed table with the informatino from compileTable
 * @return 0 on success, -1 on error
 */
static int updateSpillIndexUsed(void) {
    int k;

    /* First: for each entry, set the index used to 0, in order to reset it */
    for(k = 0; k <= MAX_SPILL_JIT_IA - 1; k++) {
        spillIndexUsed[k] = 0;
    }

    /* Second: go through each compile entry */
    for(k = 0; k < compileTable.size (); k++) {
        /** If it is a virtual register, we skip it, we don't need a special spill region for VRs */
        if(isVirtualReg(compileTable[k].physicalType)) {
            continue;
        }

        /** It might have been spilled, let's see if we have the spill_loc_index filled */
        if(compileTable[k].spill_loc_index >= 0) {

            /* We do have it, but is the index correct (and will fit in our table) */
            if(compileTable[k].spill_loc_index > 4 * (MAX_SPILL_JIT_IA - 1)) {
                ALOGI("JIT_INFO: spill_loc_index is wrong for entry %d: %d\n",
                      k, compileTable[k].spill_loc_index);
                SET_JIT_ERROR(kJitErrorRegAllocFailed);
                return -1;
            }

            /* The spill index is correct, we use the higher bits as a hash for it and set it to 1 */
            spillIndexUsed[compileTable[k].spill_loc_index >> 2] = 1;
        }
    }
    return 0;
}

/**
 * @brief Inserts high VR entries into compile table.
 * @details Looks through compile table and for all wide VRs it finds, it ensures that an entry exists for the
 * high part of the VR. Namely, if it finds that v1 is wide, it ensures that there is entry in compile table for v2.
 * @return Returns true if it succeed insert high VR for all wide VRs.
 */
static bool addHighOfWideVRToCompileTable (void)
{
    for (int entry = 0; entry < compileTable.size (); entry++)
    {
        //We only need to do the correction for wide VRs
        if (compileTable[entry].isVirtualReg () == false)
        {
            continue;
        }

        //If we have a 64-bit VR, we should also insert the high bits into the compile table
        if (compileTable[entry].getSize () == OpndSize_64)
        {
            //The high bits of a wide virtual register are available in following register number
            //For example: wide v0 occupies space on stack for v0 and v1.
            int highVR = compileTable[entry].getRegisterNumber () + 1;

            int indexHigh = searchCompileTable (LowOpndRegType_virtual | LowOpndRegType_gp, highVR);

            //If we don't have an entry for the high bits, we insert it now.
            if (indexHigh < 0)
            {
                //Create a new entry for the high VR. Since we just care about 32-bits we make it GP type
                CompileTableEntry newEntry (highVR, LowOpndRegType_virtual | LowOpndRegType_gp);

                //We now copy it to the table
                compileTable.insert (newEntry);
            }
        }
    }

    //If we make it here we were successful at inserting the high VR
    return true;
}

void MemoryVRInfo::reset (void)
{
    //Set this entry to invalid register
    regNum = -1;

    //Now set its state to being in memory because it's certainly not in a
    //location we are keeping track of
    inMemory = true;

    //Set null check and bound check to false
    nullCheckDone = false;
    boundCheck.checkDone = false;

    //Use invalid register for index VR for bound check
    boundCheck.indexVR = -1;

    //Zero out information about live ranges
    num_ranges = 0;
    ranges = 0;

    //Initalize all delay free requests
    for (int c = 0; c < VRDELAY_COUNT; c++)
    {
        delayFreeCounters[c] = 0;
    }
}

/**
 * @brief Updates the table that keeps the in memory state of VRs to contain new VR.
 * @param vR The virtual register
 * @param inMemory The initial inMemory state
 * @return Returns whether the VR's addition was successful.
 */
bool addToMemVRTable (int vR, bool inMemory)
{
    //We want to keep track of index in memory table
    int index = 0;

    //Search mem table for the virtual register we are interested in adding
    for (; index < num_memory_vr; index++)
    {
        if (memVRTable[index].regNum == vR)
        {
            break;
        }
    }

    //If the index is not the size of table, then it means we have found an entry
    if (index != num_memory_vr)
    {
        //We already have entry for this VR so simply update its memory state
        memVRTable[index].setInMemoryState (inMemory);
    }
    else
    {
        //Let's make sure we won't overflow the table if we make this insertion
        if (num_memory_vr >= NUM_MEM_VR_ENTRY)
        {
            ALOGI("JIT_INFO: Index %d exceeds size of memVRTable during addToMemVRTable\n", num_memory_vr);
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return false;
        }

        //Create the new entry
        MemoryVRInfo memInfo (vR);

        //Now set the in memory state for our new entry
        memInfo.setInMemoryState (inMemory);

        //We access the index at end of the table
        index = num_memory_vr;

        //We are adding an entry so increment the number of entries
        num_memory_vr++;

        //Finally add it to the table
        memVRTable[index] = memInfo;
    }

    //If we made it here everything went well
    return true;
}

/**
 * @brief Initializes the in memory tracking table for virtual registers.
 * @param bb The basic block which we are looking at for initialization.
 * @return Returns true if all initialization completed successfully. Otherwise it returns false.
 */
static bool initializeMemVRTable (BasicBlock_O1 *bb)
{
    //Reset number of entries in table
    num_memory_vr = 0;

    //Now walk through compile entries so we can track in memory state for every VR.
    //For wide VRs, the compile table must guarantee an entry for both the low and high VR.
    for(int entry = 0; entry < compileTable.size (); entry++)
    {
        //We can skip any entry that is not a virtual register
        if (compileTable[entry].isVirtualReg () == false)
        {
            continue;
        }

        //Make it easier to refer to the VR number
        int vR = compileTable[entry].getRegisterNumber ();

        //Determine if parent said that the VR was in memory
        bool setToInMemory = bb->associationTable.wasVRInMemory (vR);

        //Now let's add it to the table
        bool result = addToMemVRTable (vR, setToInMemory);

        if (result == false)
        {
            //We simply pass along failure since addToMemVRTable has already set error code
            return false;
        }

        DEBUG_MEMORYVR(ALOGD("Initializing state of v%d %sin memory",
                vR, (setToInMemory ? "" : "NOT ")));
    }

    //If we made it here we were successful
    return true;
}

/**
 * @brief Initializes constant table.
 * @param bb The basic block which we are looking at for initialization.
 * @return Returns true if all initialization completed successfully. Otherwise it returns false.
 */
static bool initializeConstVRTable (BasicBlock_O1 *bb)
{
    //Reset the number of entries to zero since we are initializing
    num_const_vr = 0;

    //Now walk through compile entries so we can track the constantness for every VR.
    //For wide VRs, the compile table must guarantee an entry for both the low and high VR.
    for (int entry = 0; entry < compileTable.size (); entry++)
    {
        //We can skip any entry that is not a virtual register
        if (compileTable[entry].isVirtualReg () == false)
        {
            continue;
        }

        //Make it easier to refer to the VR number
        int vR = compileTable[entry].getRegisterNumber ();

        //Determine if the virtual register was constant
        if (bb->associationTable.wasVRConstant (vR) == true)
        {
            //We make space for two value because setVRToConst might access high bits
            int constValue[2];

            //It was constant so let's get its value
            constValue[0] = bb->associationTable.getVRConstValue (vR);

            //Paranoid so we set high bits to 0
            constValue[1] = 0;

            //Set it to constant
            bool result = setVRToConst (vR, OpndSize_32, constValue);

            //We bail out if we failed to set VR to constant. If setVRToConst failed, it already
            //set an error message.
            if (result == false)
            {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Used to add registerized virtual registers as defined at entry into basic block
 */
static bool initializeRegisterizeDefs (BasicBlock_O1* bb)
{
    //Walk through the compile entries
    for (CompileTable::const_iterator it = compileTable.begin (); it != compileTable.end (); it++)
    {
        const CompileTableEntry &compileEntry = *it;

        //Did we find a virtual register that is in physical register? If yes then we must add a def for it
        if (compileEntry.isVirtualReg () == true && compileEntry.inPhysicalRegister() == true)
        {
            //Add a def for this virtual register coming into the BB
            VirtualRegInfo regDefineInfo;
            regDefineInfo.regNum = compileEntry.getRegisterNumber ();
            regDefineInfo.physicalType = compileEntry.getPhysicalType ();
            regDefineInfo.accessType = REGACCESS_D;
            offsetPC = PC_FOR_START_OF_BB;

            //Now add it to the defuse tables
            int res = mergeEntry2 (bb, regDefineInfo);

            if (res < 0)
            {
                //We just pass along the error information
                return false;
            }
        }
    }

    //If we made it here everything went okay
    return true;
}

/**
 * @brief Initializes entries in the compile table at start of BB.
 * @details It ensures that it updates compile table based on the given associations from its parent.
 * @param bb The basic block whose virtual register state should be initialized.
 * @return Returns true if all initialization completed successfully. Otherwise it returns false.
 */
static bool initializeRegStateOfBB (BasicBlock_O1* bb)
{
    assert (bb != 0);

    //First we clear the compile table
    compileTable.clear ();

    //Load associations into compile table
    if (AssociationTable::syncCompileTableWithAssociations (bb->associationTable) == false)
    {
        return false;
    }

    //Since we loaded associations into compile table now we may have virtual registers that are
    //in physical registers. Thus we set up defines of all those VRs at entry to the BB.
    if (initializeRegisterizeDefs (bb) == false)
    {
        //Just pass along error information
        return false;
    }

    //Collect information about the virtual registers in current BB
    collectInfoOfBasicBlock (bb);

    //Update compileTable with virtual register information from current BB
    for (std::vector<VirtualRegInfo>::const_iterator vrInfoIter = bb->infoBasicBlock.begin ();
            vrInfoIter != bb->infoBasicBlock.end (); vrInfoIter++)
    {
        insertFromVirtualInfo (*vrInfoIter);
    }

    //For each virtual register, we insert fake usage at end of basic block to keep it live
    for (CompileTable::const_iterator tableIter = compileTable.begin (); tableIter != compileTable.end (); tableIter++)
    {
        //Get the compile entry
        const CompileTableEntry &compileEntry = *tableIter;

        if (compileEntry.isVirtualReg ())
        {
            //Calling fakeUsageAtEndOfBB uses offsetPC so we switch it to point to end of basic block
            offsetPC = PC_FOR_END_OF_BB;

            //Update the defUseTable by assuming a fake usage at end of basic block
            fakeUsageAtEndOfBB (bb, compileEntry.getRegisterNumber (), compileEntry.getLogicalAndPhysicalTypes ());
        }
    }

    //Ensure that we also have an entry in compile table for high bits of a VR
    if (addHighOfWideVRToCompileTable () == false)
    {
        return false;
    }

    //We are ready to initialize the MemVRTable since compile table has been updated
    if (initializeMemVRTable (bb) == false)
    {
        return false;
    }

    //We are ready to initialize the constant table since compile table has been updated
    if (initializeConstVRTable (bb) == false)
    {
        return false;
    }

    //Now let's make sure we synchronize all registers being used
    syncAllRegs ();

    return true;
}


/**
 * @brief Constructor for BasicBlock_O1
 */
BasicBlock_O1::BasicBlock_O1 (void)
{
    //We set the defUseTable to 0 to make sure it doesn't try to free it in the clear function
    defUseTable = 0;
    clear (true);
}

/**
 * @brief Clear function for BasicBlock_O1
 * @param allocateLabel do we allocate the label
 */
void BasicBlock_O1::clear (bool allocateLabel)
{
    // Free defUseTable
    DefUsePair* ptr = defUseTable;
    defUseTable = 0;

    //Go through each entry
    while(ptr != NULL) {

        //Get next
        DefUsePair* tmp = ptr->next;

        //Free its uses
        DefOrUseLink* ptrUse = ptr->uses;

        while(ptrUse != NULL) {
            DefOrUseLink* tmp2 = ptrUse->next;
            free(ptrUse), ptrUse = 0;
            ptrUse = tmp2;
        }

        free(ptr), ptr = 0;
        ptr = tmp;
    }

    //Reset variables
    pc_start = 0;
    pc_end = 0;
    streamStart = 0;
    defUseTable = 0;
    defUseTail = 0;
    defUseTail = 0;

    //Clear the vectors
    xferPoints.clear ();
    associationTable.clear ();
    infoBasicBlock.clear ();

    //Allocate label
    if (allocateLabel == true)
    {
        label = static_cast<LowOpBlockLabel *> (dvmCompilerNew (sizeof (*label), true));
    }

    //Paranoid
    assert (label != NULL);

    //Default value for the offset
    label->lop.generic.offset = -1;

    //! The logic below assumes that PhysicalReg_EAX is first entry in
    //! PhysicalReg enum so let's assert it
    assert(static_cast<int>(PhysicalReg_EAX) == 0);

    // Initialize allocation constraints
    for (PhysicalReg reg = PhysicalReg_StartOfGPMarker;
            reg <= PhysicalReg_EndOfGPMarker;
            reg = static_cast<PhysicalReg>(reg + 1)) {
        allocConstraints[static_cast<int>(reg)].physicalReg = reg;
        allocConstraints[static_cast<int>(reg)].count = 0;
    }
}

/**
 * @brief Free everything in the BasicBlock that requires it
 */
void BasicBlock_O1::freeIt (void) {
    //First call clear
    clear (false);

    //Now free anything that is C++ oriented
    std::vector<XferPoint> emptyXfer;
    xferPoints.swap(emptyXfer);

    std::vector<VirtualRegInfo> emptyVRI;
    infoBasicBlock.swap(emptyVRI);
}

/**
 * @brief Do we have enough of a given class of registers to registerize
 * @param cUnit the BasicBlock
 * @param reg the RegisterClass to consider
 * @param cnt the number of this type we have right now
 */
static bool isEnoughRegisterization (CompilationUnit *cUnit, RegisterClass reg, int cnt)
{
    // Get the max set in the cUnit
    const int max = cUnit->maximumRegisterization;

    return cnt > max;
}

/**
 * @brief Backend specific checker for possible bail out to VM
 * @details Returns true if bail out from JIT code is possible
 *          A bit complex logic:
 *              If someone proved that bail out is not possible we trust him
 *              Handle not special cases
 *              Handle special cases: null check elimination, bound check elimination.
 * @param cUnit the CompilationUnit
 * @param mir the MIR to check
 * @return true if bail out from JIT code is possible
 */
bool backendCanBailOut(CompilationUnit *cUnit, MIR *mir)
{
    if ((mir->OptimizationFlags & MIR_IGNORE_BAIL_OUT_CHECK) != 0) {
        return false;
    }

    /* We miss here some entries which makes sense only for method JIT
     * TODO: update table if method JIT is enabled
     */
    switch (mir->dalvikInsn.opcode) {

    /* Monitor enter/exit - there is a call to dvmLockObject */
    case OP_MONITOR_ENTER:
    case OP_MONITOR_EXIT:
        return true;

    /* possible call to class resolution */
    case OP_CHECK_CAST:
    case OP_INSTANCE_OF:
    case OP_SGET:
    case OP_SGET_WIDE:
    case OP_SGET_OBJECT:
    case OP_SGET_BOOLEAN:
    case OP_SGET_BYTE:
    case OP_SGET_CHAR:
    case OP_SGET_SHORT:
    case OP_SPUT:
    case OP_SPUT_WIDE:
    case OP_SPUT_OBJECT:
    case OP_SPUT_BOOLEAN:
    case OP_SPUT_BYTE:
    case OP_SPUT_CHAR:
    case OP_SPUT_SHORT:
    case OP_SGET_VOLATILE:
    case OP_SPUT_VOLATILE:
    case OP_SGET_WIDE_VOLATILE:
    case OP_SPUT_WIDE_VOLATILE:
    case OP_SGET_OBJECT_VOLATILE:
    case OP_SPUT_OBJECT_VOLATILE:
        return true;

    /* memory allocation */
    case OP_NEW_INSTANCE:
    case OP_NEW_ARRAY:
    case OP_FILLED_NEW_ARRAY:
    case OP_FILLED_NEW_ARRAY_RANGE:
        return true;

    /* implicit throw */
    case OP_THROW:
    case OP_THROW_VERIFICATION_ERROR:
        return true;

    /* invocation */
    case OP_INVOKE_VIRTUAL:
    case OP_INVOKE_SUPER:
    case OP_INVOKE_DIRECT:
    case OP_INVOKE_STATIC:
    case OP_INVOKE_INTERFACE:
    case OP_INVOKE_VIRTUAL_RANGE:
    case OP_INVOKE_SUPER_RANGE:
    case OP_INVOKE_DIRECT_RANGE:
    case OP_INVOKE_STATIC_RANGE:
    case OP_INVOKE_INTERFACE_RANGE:
    case OP_EXECUTE_INLINE:
    case OP_EXECUTE_INLINE_RANGE:
    case OP_INVOKE_OBJECT_INIT_RANGE:
    case OP_INVOKE_VIRTUAL_QUICK:
    case OP_INVOKE_VIRTUAL_QUICK_RANGE:
    case OP_INVOKE_SUPER_QUICK:
    case OP_INVOKE_SUPER_QUICK_RANGE:
        return true;

    /* Division By Zero */
    case OP_DIV_INT:
    case OP_REM_INT:
    case OP_DIV_LONG:
    case OP_REM_LONG:
    case OP_DIV_INT_2ADDR:
    case OP_REM_INT_2ADDR:
    case OP_DIV_LONG_2ADDR:
    case OP_REM_LONG_2ADDR:
        return true;

    case OP_DIV_INT_LIT16:
    case OP_REM_INT_LIT16:
    case OP_DIV_INT_LIT8:
    case OP_REM_INT_LIT8:
        return mir->dalvikInsn.vC == 0;

    /* Access an Array index */
    case OP_AGET:
    case OP_AGET_WIDE:
    case OP_AGET_OBJECT:
    case OP_AGET_BOOLEAN:
    case OP_AGET_BYTE:
    case OP_AGET_CHAR:
    case OP_AGET_SHORT:
    case OP_APUT:
    case OP_APUT_WIDE:
    case OP_APUT_OBJECT:
    case OP_APUT_BOOLEAN:
    case OP_APUT_BYTE:
    case OP_APUT_CHAR:
    case OP_APUT_SHORT:
        return ((mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK) == 0) || ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0);

    /* Access Object field */
    case OP_ARRAY_LENGTH:
    case OP_IGET:
    case OP_IGET_WIDE:
    case OP_IGET_OBJECT:
    case OP_IGET_BOOLEAN:
    case OP_IGET_BYTE:
    case OP_IGET_CHAR:
    case OP_IGET_SHORT:
    case OP_IPUT:
    case OP_IPUT_WIDE:
    case OP_IPUT_OBJECT:
    case OP_IPUT_BOOLEAN:
    case OP_IPUT_BYTE:
    case OP_IPUT_CHAR:
    case OP_IPUT_SHORT:
    case OP_IGET_VOLATILE:
    case OP_IPUT_VOLATILE:
    case OP_IGET_OBJECT_VOLATILE:
    case OP_IGET_WIDE_VOLATILE:
    case OP_IPUT_WIDE_VOLATILE:
    case OP_IGET_QUICK:
    case OP_IGET_WIDE_QUICK:
    case OP_IGET_OBJECT_QUICK:
    case OP_IPUT_QUICK:
    case OP_IPUT_WIDE_QUICK:
    case OP_IPUT_OBJECT_QUICK:
    case OP_IPUT_OBJECT_VOLATILE:
        return (mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0;

    default:
        /* All other cases do not bail out
         * For non-trace JIT the following opcodes should be checked:
         * OP_CONST_STRING, OP_CONST_STRING_JUMBO, OP_CONST_CLASS.
         */
        break;
    }

    return false;
}

/**
 * @brief Handles registerization decision before lowering.
 * @details If registerization is disabled, set the writeBack vector to all 1s.
 * Registerization extended MIRs are then removed in order to only registerize
 * maximum set by the cUnit.
 * @param cUnit the BasicBlock
 * @param bb the BasicBlock
 */
static void handleRegisterizationPrework (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Handle no registerization option first
    if (gDvmJit.backEndRegisterization == false)
    {
        //In this case, we are going to rewrite the requestWriteBack to spilling everything
        dvmCompilerWriteBackAll (cUnit, bb);
    }

    //A counter for the kOpRegisterize requests
    MIR *mir = bb->firstMIRInsn;
    std::map<RegisterClass, int> counters;

    //Go through the instructions
    while (mir != 0)
    {
        //Did we remove an instruction? Suppose no
        bool removed = false;

        //If it's a registerize request, we might have to ignore it
        if (mir->dalvikInsn.opcode == static_cast<Opcode> (kMirOpRegisterize))
        {
            //Get the class for it
            RegisterClass reg = static_cast<RegisterClass> (mir->dalvikInsn.vB);

            //Increment counter
            counters[reg]++;

            //If we've had enough
            if (isEnoughRegisterization (cUnit, reg, counters[reg]) == true)
            {
                //We are going to remove it, remember it
                MIR *toRemove = mir;
                //Remove the instruction but first remember the next one
                //Note: we are removing this registerization request knowing that the system might request a recompile
                //TODO: Most likely a flag to ignore it would be better
                mir = mir->next;
                //Call the helper function
                dvmCompilerRemoveMIR (bb, toRemove);
                //Set removed
                removed = true;
            }
        }

        if (removed == false)
        {
            //Go to next
            mir = mir->next;
        }
    }
}

/**
 * @brief Parse the BasicBlock and perform pre-lowering work
 * @param cUnit the BasicBlock
 * @param bb the BasicBlock
 */
static void parseBlock (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Always handle registerization request
    handleRegisterizationPrework (cUnit, bb);
}

/**
 * @brief Pre-process BasicBlocks
 * @details This parses the block to perform some pre-code generation tasks
 * @param cUnit the Compilation Unit
 * @param bb the BasicBlock
 * @return -1 if error happened, 0 otherwise
 */
int preprocessingBB (CompilationUnit *cUnit, BasicBlock* bb)
{
    //Parse the BasicBlock, we might have some pre work to do
    parseBlock (cUnit, bb);

    //Everything went well
    return 0;
}

void printJitTraceInfoAtRunTime(const Method* method, int offset) {
    ALOGI("execute trace for %s%s at offset %x", method->clazz->descriptor, method->name, offset);
}

void startOfTraceO1(const Method* method, int exceptionBlockId, CompilationUnit *cUnit) {
    compileTable.clear ();
    currentBB = NULL;
    currentUnit = cUnit;

    /* initialize data structure allRegs */
    initializeAllRegs();

// dumpDebuggingInfo is gone in CompilationUnit struct
#if 0
    /* add code to dump debugging information */
    if(cUnit->dumpDebuggingInfo) {
        move_imm_to_mem(OpndSize_32, cUnit->startOffset, -4, PhysicalReg_ESP, true); //2nd argument: offset
        move_imm_to_mem(OpndSize_32, (int)currentMethod, -8, PhysicalReg_ESP, true); //1st argument: method
        load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);

        typedef void (*vmHelper)(const Method*, int);
        vmHelper funcPtr = printJitTraceInfoAtRunTime;
        move_imm_to_reg(OpndSize_32, (int)funcPtr, PhysicalReg_ECX, true);
        call_reg(PhysicalReg_ECX, true);

        load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    }
#endif
}


/* Code generation for a basic block defined for JIT
   We have two data structures for a basic block:
       BasicBlock defined in vm/compiler by JIT
       BasicBlock_O1 defined in o1 */
int codeGenBasicBlockJit(const Method* method, BasicBlock* bb, CompilationUnit_O1* cUnit) {

    // For x86, the BasicBlock should be the specialized one
    currentBB = reinterpret_cast<BasicBlock_O1 *> (bb);

    // Basic block here also means new native basic block
    if (gDvmJit.scheduling)
    {
        singletonPtr<Scheduler> ()->signalEndOfNativeBasicBlock ();
    }

    // Finalize this block's association table because we are generating
    // it and thus any parent of it that hasn't been generated yet must
    // be aware of this fact.
    currentBB->associationTable.finalize ();

    // Generate code for this basic block
    int result = codeGenBasicBlock (method, currentBB, cUnit);

    // End of managed basic block means end of native basic block
    if (gDvmJit.scheduling)
    {
        singletonPtr<Scheduler> ()->signalEndOfNativeBasicBlock ();
    }

    currentBB = NULL;

    return result;
}
void endOfBasicBlock(BasicBlock* bb) {
    isScratchPhysical = true;
    currentBB = NULL;
}

/**
 * @brief decide if skip the extended Op whose implementation uses NcgO0 mode
 * @param opcode opcode of the extended MIR
 * @return return false if the opcode doesn't use NcgO0, otherwise return true
 */
bool skipExtendedMir (int opcode)
{
    switch (opcode)
    {
        case kMirOpBoundCheck:
        case kMirOpNullNRangeUpCheck:
        case kMirOpNullNRangeDownCheck:
        case kMirOpLowerBound:
            return true;
        default:
            break;
    }

    //By default all implementations use NCGO1 mode
    return false;
}

/** entry point to collect information about virtual registers used in a basic block
    Initialize data structure BasicBlock_O1
    The usage information of virtual registers is stoerd in bb->infoBasicBlock

    Global variables accessed: offsetPC, rPC
*/
int collectInfoOfBasicBlock (BasicBlock_O1* bb)
{
    int seqNum = 0;
    /* traverse the MIR in basic block
       sequence number is used to make sure next bytecode will have a larger sequence number */
    for(MIR * mir = bb->firstMIRInsn; mir; mir = mir->next) {
        offsetPC = seqNum;
        mir->seqNum = seqNum++;

        // Skip extended MIRs whose implementation uses NcgO0 mode
        if ( isExtendedMir(mir->dalvikInsn.opcode) == true &&
             skipExtendedMir(mir->dalvikInsn.opcode) == true) {
            continue;
        }

        //Get information about the VRs in current bytecode
        VirtualRegInfo infoByteCode[MAX_REG_PER_BYTECODE];
        int numVRs = getVirtualRegInfo (infoByteCode, mir, true);

        for(int kk = 0; kk < numVRs; kk++) {
            //Make a copy of current bytecode
            VirtualRegInfo currentInfo = infoByteCode[kk];
#ifdef DEBUG_MERGE_ENTRY
            ALOGI("Call mergeEntry2 at offsetPC %x kk %d VR %d %d\n", offsetPC, kk,
                  currentInfo.regNum, currentInfo.physicalType);
#endif
            int retCode = mergeEntry2(bb, currentInfo); //update defUseTable of the basic block
            if (retCode < 0)
                return retCode;
        }

        //dumpVirtualInfoOfBasicBlock(bb);
    }//for each bytecode

    bb->pc_end = seqNum;

    //sort allocConstraints of each basic block
    unsigned int max = bb->infoBasicBlock.size ();
    for(unsigned int kk = 0; kk < max; kk++) {
#ifdef DEBUG_ALLOC_CONSTRAINT
        ALOGI("Sort virtual reg %d type %d -------", bb->infoBasicBlock[kk].regNum,
              bb->infoBasicBlock[kk].physicalType);
#endif
        sortAllocConstraint(bb->infoBasicBlock[kk].allocConstraints,
                            bb->infoBasicBlock[kk].allocConstraintsSorted, true);
    }
#ifdef DEBUG_ALLOC_CONSTRAINT
    ALOGI("Sort constraints for BB %d --------", bb->id);
#endif
    sortAllocConstraint(bb->allocConstraints, bb->allocConstraintsSorted, false);
    return 0;
}

/**
 * @brief Looks through a basic block for any reasons to reject it
 * @param bb Basic Block to look at
 * @return true if Basic Block cannot be handled safely by Backend
 */
static bool shouldRejectBasicBlock(BasicBlock_O1* bb) {
    // Assume that we do not want to reject the BB
    bool shouldReject = false;

    // Set a generic error message in case someone forgets to set a proper one
    JitLCGCompilationErrors errorIfRejected = static_cast<JitLCGCompilationErrors> (kJitErrorCodegen);

    /**
     * Rejection Scenario 1:
     * If the basic block has incoming virtual registers that are in physical
     * registers but we have a usage of that VR in x87, we should reject trace
     * until we properly handle the Xfer point.
     */

    // We will need to call getVirtualRegInfo but we do not want to update
    // register constraints so temporarily set the global currentBB to null
    BasicBlock_O1 * savedCurrentBB = currentBB;
    currentBB = NULL;

    std::set<int> registerizedVRs;

    // Find all of the VRs that have been registerized at entry into this BB
    for (AssociationTable::const_iterator iter = bb->associationTable.begin();
            iter != bb->associationTable.end(); iter++) {
        // If there is a physical register for this VR, then it has been
        // registerized
        if (iter->second.physicalReg != PhysicalReg_Null) {
            registerizedVRs.insert(iter->first);
        }
    }

    for (MIR * mir = bb->firstMIRInsn; mir != NULL; mir = mir->next) {

        if ( isExtendedMir(mir->dalvikInsn.opcode) == true &&
             skipExtendedMir(mir->dalvikInsn.opcode) == true) {
            continue;
        }

        //Get information about the VRs in current bytecode
        VirtualRegInfo infoByteCode[MAX_REG_PER_BYTECODE];
        int numVRs = getVirtualRegInfo (infoByteCode, mir);

        // Go through each VR of the MIR
        for (int vrIter = 0; vrIter < numVRs; vrIter++) {
            int VR = infoByteCode[vrIter].regNum;
            LowOpndRegType type = infoByteCode[vrIter].physicalType;

            // Has this VR been registerized?
            if (registerizedVRs.find(VR) != registerizedVRs.end()) {
                // If we will be using x87 for this registerized VR, we cannot
                // handle
                if (type == LowOpndRegType_fs || type == LowOpndRegType_fs_s) {
                    ALOGI("JIT_INFO: Found x87 usage for VR that has been registerized.");
                    errorIfRejected = kJitErrorBERegisterization;
                    shouldReject = true;
                    break;
                }
            }
        }

        // We already know we need to reject, break out of loop
        if (shouldReject == true) {
            break;
        }
    }

    // Restore currentBB
    currentBB = savedCurrentBB;

    if (shouldReject) {
        SET_JIT_ERROR (errorIfRejected);
    }

    return shouldReject;
}

/** entry point to generate native code for a O1 basic block
    There are 3 kinds of virtual registers in a O1 basic block:
    1> L VR: local within the basic block
    2> GG VR: is live in other basic blocks,
              its content is in a pre-defined GPR at the beginning of a basic block
    3> GL VR: is live in other basic blocks,
              its content is in the interpreted stack at the beginning of a basic block
    compileTable is updated with infoBasicBlock at the start of the basic block;
    Before lowering each bytecode, compileTable is updated with infoByteCodeTemp;
    At end of the basic block, right before the jump instruction, handles constant VRs and GG VRs
*/
int codeGenBasicBlock(const Method* method, BasicBlock_O1* bb, CompilationUnit_O1* cUnit)
{
    //Eagerly set retCode to 0 since most likely everything will be okay
    int retCode = 0;

    //We have no MIRs if first MIR pointer is null
    bool noMirs = (currentBB->firstMIRInsn == NULL);

    // If we should reject the BB, return that it has not been handled
    if (shouldRejectBasicBlock (bb) == true)
    {
        //If rejected, an error message will have been set so we just pass along the error
        return -1;
    }

    //We have already loaded the information about VR from each bytecode in this basic block.
    //Thus we are now ready to finish initializing virtual register state.
    if (initializeRegStateOfBB (bb) == false)
    {
        return -1;
    }

    //If we do have MIRs, we must update the transfer points and the live table
    if (noMirs == false)
    {
        //Now we update any transfer points between virtual registers that are represented by different types
        //throughout this same BB
        retCode = updateXferPoints (bb);

        if (retCode < 0)
        {
            //Someone else has set the error so we just pass it along
            return retCode;
        }

        //Since we have set up the transfer points, check to see if there are any points at the start of the BB
        //so that we can handle them right now.
        handleStartOfBBXferPoints (bb);

        retCode = updateLiveTable (bb);

        if (retCode < 0)
        {
            //Someone else has set the error so we just pass it along
            return retCode;
        }
    }

#ifdef DEBUG_REACHING_DEF
    printDefUseTable();
#endif

#ifdef DEBUG_COMPILE_TABLE
    ALOGI("At start of basic block %d (num of VRs %d) -------", bb->id, bb->infoBasicBlock.size ());
    dumpCompileTable();
#endif

    //Assume that the last bytecode in this block is not a jump unless proven otherwise
    bool lastByteCodeIsJump = false;

    //Now walk through the bytecodes to generate code for each
    for (MIR * mir = bb->firstMIRInsn; mir; mir = mir->next)
    {
        int k;
        offsetPC = mir->seqNum;

        //Update rPC to contain the dalvik PC for this bytecode
        rPC = dvmCompilerGetDalvikPC (cUnit, mir);

        //Skip mirs tagged as being no-ops
        if ((mir->OptimizationFlags & MIR_INLINED) != 0)
        {
            continue;
        }

        // Handle extended MIRs whose implementation uses NcgO0 mode
        if ( isExtendedMir(mir->dalvikInsn.opcode) == true &&
             skipExtendedMir(mir->dalvikInsn.opcode) == true) {
            handleExtendedMIR (currentUnit, bb, mir);
            // The rest of logic is for handling mirs that use NCG01 so
            // we can safely skip
            continue;
        }

        //before handling a bytecode, import info of temporary registers to compileTable including refCount
        num_temp_regs_per_bytecode = getTempRegInfo(infoByteCodeTemp, mir, rPC);
        for(k = 0; k < num_temp_regs_per_bytecode; k++) {
            if(infoByteCodeTemp[k].versionNum > 0) continue;
            insertFromTempInfo (infoByteCodeTemp[k]);
        }
        startNativeCode(-1, -1);
        for(k = 0; k <= MAX_SPILL_JIT_IA - 1; k++) spillIndexUsed[k] = 0;

#ifdef DEBUG_COMPILE_TABLE
        ALOGI("compile table size after importing temporary info %d", compileTable.size ());
        ALOGI("before one bytecode %d (num of VRs %d) -------", bb->id, bb->infoBasicBlock.size ());
#endif
        //set isConst to true for CONST & MOVE MOVE_OBJ?
        //clear isConst to true for MOVE, MOVE_OBJ, MOVE_RESULT, MOVE_EXCEPTION ...
        bool isConst = false;
        int retCode = getConstInfo(bb, mir); //will return 0 if a VR is updated by the bytecode
        //if the bytecode generates a constant
        if (retCode == 1)
            isConst = true;
        //if something went wrong at getConstInfo. getConstInfo has logged it
        else if (retCode == -1)
            return retCode;
        //otherwise, bytecode does not generate a constant

        //Get information about the VRs in current bytecode
        VirtualRegInfo infoByteCode[MAX_REG_PER_BYTECODE];
        int numVRs = getVirtualRegInfo (infoByteCode, mir);

        //call something similar to mergeEntry2, but only update refCount
        //clear refCount
        for(k = 0; k < numVRs; k++) {
            int indexT = searchCompileTable(LowOpndRegType_virtual | infoByteCode[k].physicalType,
                                            infoByteCode[k].regNum);
            if(indexT >= 0)
                compileTable[indexT].refCount = 0;
        }
        for(k = 0; k < numVRs; k++) {
            int indexT = searchCompileTable(LowOpndRegType_virtual | infoByteCode[k].physicalType,
                                            infoByteCode[k].regNum);
            if(indexT >= 0)
                compileTable[indexT].refCount += infoByteCode[k].refCount;
        } //for k
        lastByteCodeIsJump = false;
        if(isConst == false)
        {
#ifdef DEBUG_COMPILE_TABLE
            dumpCompileTable();
#endif
            freeShortMap();
            if (isCurrentByteCodeJump(mir->dalvikInsn.opcode))
                lastByteCodeIsJump = true;

            //We eagerly assume we don't handle unless proven otherwise.
            bool notHandled = true;

            if ((int) mir->dalvikInsn.opcode >= (int) kMirOpFirst)
            {
                notHandled = (handleExtendedMIR (currentUnit, bb, mir) == false);
            }
            else
            {
                notHandled = lowerByteCodeJit(method, mir, rPC, cUnit);
            }

            if (dvmCompilerWillCodeCacheOverflow((stream - streamStart) +
                 CODE_CACHE_PADDING) == true) {
                 ALOGI("JIT_INFO: Code cache full while lowering bytecode %s", dvmCompilerGetOpcodeName (mir->dalvikInsn.opcode));
                 dvmCompilerSetCodeAndDataCacheFull();
                 SET_JIT_ERROR(kJitErrorCodeCacheFull);
                 return -1;
            }

            if (notHandled){
                SET_JIT_ERROR(kJitErrorCodegen);
                return -1;
            }

            //Check if an error happened while in the bytecode
            if (IS_ANY_JIT_ERROR_SET()) {
                SET_JIT_ERROR(kJitErrorCodegen);
                return -1;
            }

            updateConstInfo(bb);
            freeShortMap();
        } else { //isConst
            //if this bytecode is the target of a jump, the mapFromBCtoNCG should be updated
            offsetNCG = stream - streamMethodStart;
            mapFromBCtoNCG[mir->offset] = offsetNCG;
#ifdef DEBUG_COMPILE_TABLE
            ALOGI("Bytecode %s generates a constant and has no side effect\n", dvmCompilerGetOpcodeName (mir->dalvikInsn.opcode));
#endif
        }

        //After each bytecode, make sure the temporaries have refCount of zero.
        for(k = 0; k < compileTable.size (); k++)
        {
            if(compileTable[k].isTemporary ())
            {
#ifdef PRINT_WARNING
                //If warnings are enabled, we need to print a message because remaining ref count greater
                //than zero means that bytecode visitor reference counts are not correct
                if (compileTable[k].refCount > 0)
                {
                    ALOGD ("JIT_INFO: refCount for a temporary reg %d %d is %d after a bytecode", compileTable[k].regNum,
                            compileTable[k].physicalType, compileTable[k].refCount);
                }
#endif
                compileTable[k].updateRefCount (0);
            }
        }

        //Now that we updated reference counts, let's clear the physical register associations
        freeReg (false);

#ifdef DEBUG_COMPILE_TABLE
        ALOGI("After one bytecode BB %d (num of VRs %d)", bb->id, bb->infoBasicBlock.size ());
#endif
    }//for each bytecode

#ifdef DEBUG_COMPILE_TABLE
    dumpCompileTable();
#endif

    //At the end of a basic block we want to handle VR information. If the BB ended with
    //jump or switch, then we have nothing to handle because it has already been handled
    //in the corresponding jumping bytecode.
    retCode = handleRegistersEndOfBB (lastByteCodeIsJump == false);

    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    // We are done with compile table so clear it now
    compileTable.clear ();

    //Free live table
    for (int k = 0; k < num_memory_vr; k++)
    {
        LiveRange* ptr2 = memVRTable[k].ranges;
        while (ptr2 != NULL)
        {
            LiveRange* tmpP = ptr2->next;
            free (ptr2->accessPC);
            free (ptr2);
            ptr2 = tmpP;
        }
    }

    return 0;
}

/** update infoBasicBlock & defUseTable
    input: currentInfo
    side effect: update currentInfo.reachingDefs

    update entries in infoBasicBlock by calling updateReachingDefA
    if there is no entry in infoBasicBlock for B, an entry will be created and inserted to infoBasicBlock

    defUseTable is updated to account for the access at currentInfo
    if accessType of B is U or UD, we call updateReachingDefB to update currentInfo.reachingDefs
        in order to correctly insert the usage to defUseTable
*/
int mergeEntry2 (BasicBlock_O1* bb, VirtualRegInfo &currentInfo)
{
    LowOpndRegType typeB = currentInfo.physicalType;
    int regB = currentInfo.regNum;
    int jj, k;
    int jjend = bb->infoBasicBlock.size ();
    bool isMerged = false;
    bool hasAlias = false;
    OverlapCase isBPartiallyOverlapA, isAPartiallyOverlapB;
    RegAccessType tmpType = REGACCESS_N;
    currentInfo.num_reaching_defs = 0;

    /* traverse variable A in infoBasicBlock */
    for(jj = 0; jj < jjend; jj++) {
        int regA = bb->infoBasicBlock[jj].regNum;
        LowOpndRegType typeA = bb->infoBasicBlock[jj].physicalType;
        isBPartiallyOverlapA = getBPartiallyOverlapA(regB, typeB, regA, typeA);
        isAPartiallyOverlapB = getAPartiallyOverlapB(regA, typeA, regB, typeB);
        if(regA == regB && typeA == typeB) {
            /* variable A and B are aligned */
            bb->infoBasicBlock[jj].accessType = mergeAccess2(bb->infoBasicBlock[jj].accessType, currentInfo.accessType,
                                                             OVERLAP_B_COVER_A);
            bb->infoBasicBlock[jj].refCount += currentInfo.refCount;
            /* copy reaching defs of variable B from variable A */
            currentInfo.num_reaching_defs = bb->infoBasicBlock[jj].num_reaching_defs;
            for(k = 0; k < currentInfo.num_reaching_defs; k++)
                currentInfo.reachingDefs[k] = bb->infoBasicBlock[jj].reachingDefs[k];
            updateDefUseTable (currentInfo); //use currentInfo to update defUseTable
            int retCode = updateReachingDefA (currentInfo, jj, OVERLAP_B_COVER_A); //update reachingDefs of A
            if (retCode < 0)
                return -1;
            isMerged = true;
            hasAlias = true;
            if(typeB == LowOpndRegType_gp) {
                //merge allocConstraints
                for(k = 0; k < 8; k++) {
                    bb->infoBasicBlock[jj].allocConstraints[k].count += currentInfo.allocConstraints[k].count;
                }
            }
        }
        else if(isBPartiallyOverlapA != OVERLAP_NO) {
            tmpType = updateAccess2(tmpType, updateAccess1(bb->infoBasicBlock[jj].accessType, isAPartiallyOverlapB));
            bb->infoBasicBlock[jj].accessType = mergeAccess2(bb->infoBasicBlock[jj].accessType, currentInfo.accessType,
                                                             isBPartiallyOverlapA);
#ifdef DEBUG_MERGE_ENTRY
            ALOGI("Update accessType in case 2: VR %d %d accessType %d", regA, typeA, bb->infoBasicBlock[jj].accessType);
#endif
            hasAlias = true;
            if(currentInfo.accessType == REGACCESS_U || currentInfo.accessType == REGACCESS_UD) {
                VirtualRegInfo tmpInfo;

                /* update currentInfo.reachingDefs */
                int retCode = updateReachingDefB1 (currentInfo, tmpInfo, jj);
                if (retCode < 0)
                    return retCode;
                retCode = updateReachingDefB2 (currentInfo, tmpInfo);
                if (retCode < 0)
                    return retCode;
            }
            int retCode = updateReachingDefA (currentInfo, jj, isBPartiallyOverlapA);
            if (retCode < 0)
                return retCode;
        }
        else {
            //even if B does not overlap with A, B can affect the reaching defs of A
            //for example, B is a def of "v0", A is "v1"
            //  B can kill some reaching defs of A or affect the accessType of a reaching def
            int retCode = updateReachingDefA (currentInfo, jj, OVERLAP_NO); //update reachingDefs of A
            if (retCode < 0)
                return -1;
        }
    }//for each variable A in infoBasicBlock
    if(!isMerged) {
        /* create a new entry in infoBasicBlock */
        VirtualRegInfo info;
        info.refCount = currentInfo.refCount;
        info.physicalType = typeB;
        if(hasAlias)
            info.accessType = updateAccess3(tmpType, currentInfo.accessType);
        else
            info.accessType = currentInfo.accessType;
#ifdef DEBUG_MERGE_ENTRY
        ALOGI("Update accessType in case 3: VR %d %d accessType %d", regB, typeB, info.accessType);
#endif
        info.regNum = regB;
        for(k = 0; k < 8; k++)
            info.allocConstraints[k] = currentInfo.allocConstraints[k];
#ifdef DEBUG_MERGE_ENTRY
        ALOGI("isMerged is false, call updateDefUseTable");
#endif
        updateDefUseTable (currentInfo); //use currentInfo to update defUseTable
        updateReachingDefB3 (currentInfo); //update currentInfo.reachingDefs if currentInfo defines variable B

        //copy from currentInfo.reachingDefs to info
        info.num_reaching_defs = currentInfo.num_reaching_defs;
        for(k = 0; k < currentInfo.num_reaching_defs; k++)
            info.reachingDefs[k] = currentInfo.reachingDefs[k];
#ifdef DEBUG_MERGE_ENTRY
        ALOGI("Try to update reaching defs for VR %d %d", regB, typeB);
        for(k = 0; k < info.num_reaching_defs; k++)
            ALOGI("reaching def %d @ %d for VR %d %d access %d", k, currentInfo.reachingDefs[k].offsetPC,
                  currentInfo.reachingDefs[k].regNum, currentInfo.reachingDefs[k].physicalType,
                  currentInfo.reachingDefs[k].accessType);
#endif
        //Push it in the vector
        bb->infoBasicBlock.push_back (info);

        if(bb->infoBasicBlock.size () >= MAX_REG_PER_BASICBLOCK) {
            ALOGI("JIT_INFO: Number of VRs (%d) in a basic block, exceed maximum (%d)\n", bb->infoBasicBlock.size (), MAX_REG_PER_BASICBLOCK);
            SET_JIT_ERROR(kJitErrorMaxVR);
            return -1;
        }
    }
    return 0;
}

/**
 * @brief update reaching defs for infoBasicBlock[indexToA]
 * @details use currentInfo.reachingDefs to update reaching defs for variable A
 * @param currentInfo the current considered VirtualRegInfo
 * @param indexToA Index of variable A
 * @param isBPartiallyOverlapA the type of overlap
 * @return -1 if error, 0 otherwise
 */
static int updateReachingDefA (VirtualRegInfo &currentInfo, int indexToA, OverlapCase isBPartiallyOverlapA)
{
    if(indexToA < 0) return 0;
    int k, k2;
    OverlapCase isBPartiallyOverlapDef;
    if(currentInfo.accessType == REGACCESS_U) {
        return 0; //no update to reachingDefs of the VR
    }
    /* access in currentInfo is DU, D, or UD */
    if(isBPartiallyOverlapA == OVERLAP_B_COVER_A) {
        /* from this point on, the reachingDefs for variable A is a single def to currentInfo at offsetPC */
        currentBB->infoBasicBlock[indexToA].num_reaching_defs = 1;
        currentBB->infoBasicBlock[indexToA].reachingDefs[0].offsetPC = offsetPC;
        currentBB->infoBasicBlock[indexToA].reachingDefs[0].regNum = currentInfo.regNum;
        currentBB->infoBasicBlock[indexToA].reachingDefs[0].physicalType = currentInfo.physicalType;
        currentBB->infoBasicBlock[indexToA].reachingDefs[0].accessType = REGACCESS_D;
#ifdef DEBUG_REACHING_DEF
        ALOGI("Single reaching def @ %d for VR %d %d", offsetPC, currentInfo.regNum, currentInfo.physicalType);
#endif
        return 0;
    }
    /* update reachingDefs for variable A to get rid of dead defs */
    /* Bug fix: it is possible that more than one reaching defs need to be removed
                after one reaching def is removed, num_reaching_defs--, but k should not change
    */
    for(k = 0; k < currentBB->infoBasicBlock[indexToA].num_reaching_defs; ) {
        /* remove one reaching def in one interation of the loop */
        //check overlapping between def & B
        isBPartiallyOverlapDef = getBPartiallyOverlapA(currentInfo.regNum, currentInfo.physicalType,
                                                       currentBB->infoBasicBlock[indexToA].reachingDefs[k].regNum,
                                                       currentBB->infoBasicBlock[indexToA].reachingDefs[k].physicalType);
#ifdef DEBUG_REACHING_DEF
        ALOGI("DEBUG B %d %d def %d %d %d", currentInfo.regNum, currentInfo.physicalType,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].regNum,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].physicalType,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType);
#endif
        /* cases where one def nees to be removed:
           if B fully covers def, def is removed
           if B overlaps high half of def & def's accessType is H, def is removed
           if B overlaps low half of def & def's accessType is L, def is removed
        */
        if((isBPartiallyOverlapDef == OVERLAP_B_COVER_HIGH_OF_A &&
            currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType == REGACCESS_H) ||
           (isBPartiallyOverlapDef == OVERLAP_B_COVER_LOW_OF_A &&
            currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType == REGACCESS_L) ||
           isBPartiallyOverlapDef == OVERLAP_B_COVER_A
           ) { //remove def
            //shift from k+1 to end
            for(k2 = k+1; k2 < currentBB->infoBasicBlock[indexToA].num_reaching_defs; k2++)
                currentBB->infoBasicBlock[indexToA].reachingDefs[k2-1] = currentBB->infoBasicBlock[indexToA].reachingDefs[k2];
            currentBB->infoBasicBlock[indexToA].num_reaching_defs--;
        }
        /*
           if B overlaps high half of def & def's accessType is not H --> update accessType of def
        */
        else if(isBPartiallyOverlapDef == OVERLAP_B_COVER_HIGH_OF_A &&
                currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType != REGACCESS_H) {
            //low half is still valid
            if(getRegSize(currentBB->infoBasicBlock[indexToA].reachingDefs[k].physicalType) == OpndSize_32)
                currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType = REGACCESS_D;
            else
                currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType = REGACCESS_L;
#ifdef DEBUG_REACHING_DEF
            ALOGI("DEBUG: set accessType of def to L");
#endif
            k++;
        }
        /*
           if B overlaps low half of def & def's accessType is not L --> update accessType of def
        */
        else if(isBPartiallyOverlapDef == OVERLAP_B_COVER_LOW_OF_A &&
                currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType != REGACCESS_L) {
            //high half of def is still valid
            currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType = REGACCESS_H;
#ifdef DEBUG_REACHING_DEF
            ALOGI("DEBUG: set accessType of def to H");
#endif
            k++;
        }
        else {
            k++;
        }
    }//for k
    if(isBPartiallyOverlapA != OVERLAP_NO) {
        //insert the def to variable @ currentInfo
        k = currentBB->infoBasicBlock[indexToA].num_reaching_defs;
        if(k >= 3) {
            ALOGI("JIT_INFO: more than 3 reaching defs at updateReachingDefA");
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return -1;
        }
        currentBB->infoBasicBlock[indexToA].reachingDefs[k].offsetPC = offsetPC;
        currentBB->infoBasicBlock[indexToA].reachingDefs[k].regNum = currentInfo.regNum;
        currentBB->infoBasicBlock[indexToA].reachingDefs[k].physicalType = currentInfo.physicalType;
        currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType = REGACCESS_D;
        currentBB->infoBasicBlock[indexToA].num_reaching_defs++;
    }
#ifdef DEBUG_REACHING_DEF2
    ALOGI("IN updateReachingDefA for VR %d %d", currentBB->infoBasicBlock[indexToA].regNum,
          currentBB->infoBasicBlock[indexToA].physicalType);
    for(k = 0; k < currentBB->infoBasicBlock[indexToA].num_reaching_defs; k++)
        ALOGI("Reaching def %d @ %d for VR %d %d access %d", k,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].offsetPC,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].regNum,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].physicalType,
              currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType);
#endif
    return 0;
}

/**
 * @brief updateReachingDefB1
 * @details Given a variable B in currentInfo, updates its reaching defs
 * by checking reaching defs of variable A in currentBB->infoBasicBlock[indexToA]
 * The result is stored in tmpInfo.reachingDefs
 * @param currentInfo the current considered VirtualRegInfo
 * @param tmpInfo the temporary information
 * @param indexToA Index of variable A
 * @return -1 if error, 0 otherwise
 */
static int updateReachingDefB1 (VirtualRegInfo &currentInfo, VirtualRegInfo &tmpInfo, int indexToA)
{
    if(indexToA < 0) return 0;
    int k;
    tmpInfo.num_reaching_defs = 0;
    for(k = 0; k < currentBB->infoBasicBlock[indexToA].num_reaching_defs; k++) {
        /* go through reachingDefs of variable A @currentBB->infoBasicBlock[indexToA]
           for each def, check whether it overlaps with variable B @currentInfo
               if the def overlaps with variable B, insert it to tmpInfo.reachingDefs
        */
        OverlapCase isDefPartiallyOverlapB = getAPartiallyOverlapB(
                                                 currentBB->infoBasicBlock[indexToA].reachingDefs[k].regNum,
                                                 currentBB->infoBasicBlock[indexToA].reachingDefs[k].physicalType,
                                                 currentInfo.regNum, currentInfo.physicalType
                                                 );
        bool insert1 = false; //whether to insert the def to tmpInfo.reachingDefs
        if(isDefPartiallyOverlapB == OVERLAP_ALIGN ||
           isDefPartiallyOverlapB == OVERLAP_A_IS_LOW_OF_B ||
           isDefPartiallyOverlapB == OVERLAP_A_IS_HIGH_OF_B) {
            /* B aligns with def */
            /* def is low half of B, def is high half of B
               in these two cases, def is 32 bits */
            insert1 = true;
        }
        RegAccessType deftype = currentBB->infoBasicBlock[indexToA].reachingDefs[k].accessType;
        if(isDefPartiallyOverlapB == OVERLAP_B_IS_LOW_OF_A ||
           isDefPartiallyOverlapB == OVERLAP_LOW_OF_A_IS_HIGH_OF_B) {
            /* B is the low half of def */
            /* the low half of def is the high half of B */
            if(deftype != REGACCESS_H) insert1 = true;
        }
        if(isDefPartiallyOverlapB == OVERLAP_B_IS_HIGH_OF_A ||
           isDefPartiallyOverlapB == OVERLAP_HIGH_OF_A_IS_LOW_OF_B) {
            /* B is the high half of def */
            /* the high half of def is the low half of B */
            if(deftype != REGACCESS_L) insert1 = true;
        }
        if(insert1) {
            if(tmpInfo.num_reaching_defs >= 3) {
                ALOGI("JIT_INFO: more than 3 reaching defs for tmpInfo at updateReachingDefB1");
                SET_JIT_ERROR(kJitErrorRegAllocFailed);
                return -1;
            }
            tmpInfo.reachingDefs[tmpInfo.num_reaching_defs] = currentBB->infoBasicBlock[indexToA].reachingDefs[k];
            tmpInfo.num_reaching_defs++;
#ifdef DEBUG_REACHING_DEF2
            ALOGI("Insert from entry %d %d: index %d", currentBB->infoBasicBlock[indexToA].regNum,
                  currentBB->infoBasicBlock[indexToA].physicalType, k);
#endif
        }
    }
    return 0;
}

//! \brief updateReachingDefB2
//! \details update currentInfo.reachingDefs by merging
//! currentInfo.reachingDefs with tmpInfo.reachingDefs
//! \return -1 if error, 0 otherwise
static int updateReachingDefB2 (VirtualRegInfo &currentInfo, VirtualRegInfo &tmpInfo)
{
    int k, k2;
    for(k2 = 0; k2 < tmpInfo.num_reaching_defs; k2++ ) {
        bool merged = false;
        for(k = 0; k < currentInfo.num_reaching_defs; k++) {
            /* check whether it is the same def, if yes, do nothing */
            if(currentInfo.reachingDefs[k].regNum == tmpInfo.reachingDefs[k2].regNum &&
               currentInfo.reachingDefs[k].physicalType == tmpInfo.reachingDefs[k2].physicalType) {
                merged = true;
                if(currentInfo.reachingDefs[k].offsetPC != tmpInfo.reachingDefs[k2].offsetPC) {
                    ALOGI("JIT_INFO: defs on the same VR %d %d with different offsetPC %d vs %d",
                          currentInfo.reachingDefs[k].regNum, currentInfo.reachingDefs[k].physicalType,
                          currentInfo.reachingDefs[k].offsetPC, tmpInfo.reachingDefs[k2].offsetPC);
                    SET_JIT_ERROR(kJitErrorRegAllocFailed);
                    return -1;
                }
                if(currentInfo.reachingDefs[k].accessType != tmpInfo.reachingDefs[k2].accessType) {
                    ALOGI("JIT_INFO: defs on the same VR %d %d with different accessType\n",
                          currentInfo.reachingDefs[k].regNum, currentInfo.reachingDefs[k].physicalType);
                    SET_JIT_ERROR(kJitErrorRegAllocFailed);
                    return -1;
                }
                break;
            }
        }
        if(!merged) {
            if(currentInfo.num_reaching_defs >= 3) {
                ALOGI("JIT_INFO: more than 3 reaching defs for currentInfo at updateReachingDefB2\n");
                SET_JIT_ERROR(kJitErrorRegAllocFailed);
                return -1;
            }
            currentInfo.reachingDefs[currentInfo.num_reaching_defs] = tmpInfo.reachingDefs[k2];
            currentInfo.num_reaching_defs++;
        }
    }
    return 0;
}

//!update currentInfo.reachingDefs with currentInfo if variable is defined in currentInfo

//!
void updateReachingDefB3 (VirtualRegInfo &currentInfo)
{
    if(currentInfo.accessType == REGACCESS_U) {
        return; //no need to update currentInfo.reachingDefs
    }
    currentInfo.num_reaching_defs = 1;
    currentInfo.reachingDefs[0].regNum = currentInfo.regNum;
    currentInfo.reachingDefs[0].physicalType = currentInfo.physicalType;
    currentInfo.reachingDefs[0].offsetPC = offsetPC;
    currentInfo.reachingDefs[0].accessType = REGACCESS_D;
}

/** update defUseTable by checking currentInfo
*/
void updateDefUseTable (VirtualRegInfo &currentInfo)
{
    /* no access */
    if(currentInfo.accessType == REGACCESS_N) return;
    /* define then use, or define only */
    if(currentInfo.accessType == REGACCESS_DU || currentInfo.accessType == REGACCESS_D) {
        /* insert a definition at offsetPC to variable @ currentInfo */
        DefUsePair* ptr = insertADef(currentBB, offsetPC, currentInfo.regNum, currentInfo.physicalType, REGACCESS_D);
        if(currentInfo.accessType != REGACCESS_D) {
             /* if access is define then use, insert a use at offsetPC */
            insertAUse(ptr, offsetPC, currentInfo.regNum, currentInfo.physicalType);
        }
        return;
    }
    /* use only or use then define
       check the reaching defs for the usage */
    int k;
    bool isLCovered = false, isHCovered = false, isDCovered = false;
    for(k = 0; k < currentInfo.num_reaching_defs; k++) {
        /* insert a def currentInfo.reachingDefs[k] and a use of variable at offsetPC */
        RegAccessType useType = insertDefUsePair (currentInfo, k);
        if(useType == REGACCESS_D) isDCovered = true;
        if(useType == REGACCESS_L) isLCovered = true;
        if(useType == REGACCESS_H) isHCovered = true;
    }
    OpndSize useSize = getRegSize(currentInfo.physicalType);
    if((!isDCovered) && (!isLCovered)) {
        /* the low half of variable is not defined in the basic block
           so insert a def to the low half at START of the basic block */
        insertDefUsePair(currentInfo, -1);
    }
    if(useSize == OpndSize_64 && (!isDCovered) && (!isHCovered)) {
        /* the high half of variable is not defined in the basic block
           so insert a def to the high half at START of the basic block */
        insertDefUsePair(currentInfo, -2);
    }
    if(currentInfo.accessType == REGACCESS_UD) {
        /* insert a def at offsetPC to variable @ currentInfo */
        insertADef(currentBB, offsetPC, currentInfo.regNum, currentInfo.physicalType, REGACCESS_D);
        return;
    }
}

//! \brief insertAUse
//! \details Insert a use at offsetPC of given variable at end of DefUsePair
//! \param ptr The DefUsePair
//! \param offsetPC
//! \param regNum
//! \param physicalType
//! \return useType
RegAccessType insertAUse(DefUsePair* ptr, int offsetPC, int regNum, LowOpndRegType physicalType) {
    DefOrUseLink* tLink = (DefOrUseLink*)malloc(sizeof(DefOrUseLink));
    if(tLink == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertAUse");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return REGACCESS_UNKNOWN;
    }
    tLink->offsetPC = offsetPC;
    tLink->regNum = regNum;
    tLink->physicalType = physicalType;
    tLink->next = NULL;
    if(ptr->useTail != NULL)
        ptr->useTail->next = tLink;
    ptr->useTail = tLink;
    if(ptr->uses == NULL)
        ptr->uses = tLink;
    ptr->num_uses++;

    //check whether the def is partially overlapping with the variable
    OverlapCase isDefPartiallyOverlapB = getBPartiallyOverlapA(ptr->def.regNum,
                                                       ptr->def.physicalType,
                                                       regNum, physicalType);
    RegAccessType useType = setAccessTypeOfUse(isDefPartiallyOverlapB, ptr->def.accessType);
    tLink->accessType = useType;
    return useType;
}

/**
 * @brief Insert a definition
 * @details insert a def to currentBB->defUseTable, update currentBB->defUseTail if necessary
 * @param bb the BasicBlock_O1
 * @param offsetPC the PC offset
 * @param regNum the register number
 * @param pType Physical type
 * @param rType Register access type
 * @return the new inserted DefUsePair
 */
DefUsePair* insertADef (BasicBlock_O1 *bb, int offsetPC, int regNum, LowOpndRegType pType, RegAccessType rType)
{
    DefUsePair* ptr = (DefUsePair*)malloc(sizeof(DefUsePair));
    if(ptr == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertADef");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return NULL;
    }

    //First initialize the information we keep for defuse
    ptr->next = NULL;
    ptr->def.offsetPC = offsetPC;
    ptr->def.regNum = regNum;
    ptr->def.physicalType = pType;
    ptr->def.accessType = rType;
    ptr->num_uses = 0;
    ptr->useTail = NULL;
    ptr->uses = NULL;

    //Now add this to the end of our defUse chain
    if (bb->defUseTail != NULL)
    {
        bb->defUseTail->next = ptr;
    }

    bb->defUseTail = ptr;

    //If this is the first entry, we must make the start point to it
    if (bb->defUseTable == NULL)
    {
        bb->defUseTable = ptr;
    }

#ifdef DEBUG_REACHING_DEF
    ALOGI("Insert a def at %d to defUseTable for VR %d %d", offsetPC,
          regNum, pType);
#endif
    return ptr;
}

/** insert a def to defUseTable, then insert a use of variable @ currentInfo
    if reachingDefIndex >= 0, the def is currentInfo.reachingDefs[index]
    if reachingDefIndex is -1, the low half is defined at START of the basic block
    if reachingDefIndex is -2, the high half is defined at START of the basic block
*/
RegAccessType insertDefUsePair (VirtualRegInfo &currentInfo, int reachingDefIndex)
{
    int k = reachingDefIndex;
    DefUsePair* tableIndex = NULL;
    DefOrUse theDef;
    theDef.regNum = 0;
    if(k < 0) {
        /* def at start of the basic blcok */
        theDef.offsetPC = PC_FOR_START_OF_BB;
        theDef.accessType = REGACCESS_D;
        if(k == -1) //low half of variable
            theDef.regNum = currentInfo.regNum;
        if(k == -2) //high half of variable
            theDef.regNum = currentInfo.regNum+1;
        theDef.physicalType = LowOpndRegType_gp;
    }
    else {
        theDef = currentInfo.reachingDefs[k];
    }
    tableIndex = searchDefUseTable(theDef.offsetPC, theDef.regNum, theDef.physicalType);
    if(tableIndex == NULL) //insert an entry
        tableIndex = insertADef(currentBB, theDef.offsetPC, theDef.regNum, theDef.physicalType, theDef.accessType);
    else
        tableIndex->def.accessType = theDef.accessType;
    RegAccessType useType = insertAUse(tableIndex, offsetPC, currentInfo.regNum, currentInfo.physicalType);
    return useType;
}

/* @brief insert a XFER_MEM_TO_XMM to currentBB->xferPoints
 *
 * @params offset offsetPC of the transfer location
 * @params regNum Register number
 * @params pType Physical type of the reg
 *
 * @return -1 if error occurred, 0 otherwise
 */
static int insertLoadXfer(int offset, int regNum, LowOpndRegType pType) {
    //check whether it is already in currentBB->xferPoints
    unsigned int k, max;
    max = currentBB->xferPoints.size ();
    for(k = 0; k < max; k++) {
        if(currentBB->xferPoints[k].xtype == XFER_MEM_TO_XMM &&
           currentBB->xferPoints[k].offsetPC == offset &&
           currentBB->xferPoints[k].regNum == regNum &&
           currentBB->xferPoints[k].physicalType == pType)
            return 0;
    }

    //We are going to create a new one
    XferPoint point;
    point.xtype = XFER_MEM_TO_XMM;
    point.regNum = regNum;
    point.offsetPC = offset;
    point.physicalType = pType;
#ifdef DEBUG_XFER_POINTS
    ALOGI("Insert to xferPoints %d: XFER_MEM_TO_XMM of VR %d %d at %d", max, regNum, pType, offset);
#endif

    //Insert new point
    currentBB->xferPoints.push_back (point);

    //Paranoid
    if(max + 1 >= MAX_XFER_PER_BB) {
        ALOGI("JIT_INFO: Number of transfer points (%d) exceed maximum (%d)", max + 1, MAX_XFER_PER_BB);
        SET_JIT_ERROR(kJitErrorMaxXferPoints);
        return -1;
    }
    return 0;
}

/** update defUseTable by assuming a fake usage at END of a basic block for variable @ currentInfo
    create a fake usage at end of a basic block for variable B (currentInfo.physicalType, currentInfo.regNum)
    get reaching def info for variable B and store the info in currentInfo.reachingDefs
        for each virtual register (variable A) accessed in the basic block
            update reaching defs of B by checking reaching defs of variable A
    update defUseTable
*/
int fakeUsageAtEndOfBB (BasicBlock_O1* bb, int vR, int physicalAndLogicalType)
{
    VirtualRegInfo currentInfo;

    //Get the register number
    currentInfo.regNum = vR;

    //TODO The cast here is invalid because it creates an invalid LowOpndRegType.
    //However, this invalidity seems to be required for properly creating usedef chains.
    currentInfo.physicalType = static_cast<LowOpndRegType> (physicalAndLogicalType);

    currentInfo.accessType = REGACCESS_U;
    LowOpndRegType typeB = currentInfo.physicalType;
    int regB = currentInfo.regNum;
    unsigned int jj;
    int k;
    currentInfo.num_reaching_defs = 0;
    unsigned int max = bb->infoBasicBlock.size ();
    for(jj = 0; jj < max; jj++) {
        int regA = bb->infoBasicBlock[jj].regNum;
        LowOpndRegType typeA = bb->infoBasicBlock[jj].physicalType;
        OverlapCase isBPartiallyOverlapA = getBPartiallyOverlapA(regB, typeB, regA, typeA);
        if(regA == regB && typeA == typeB) {
            /* copy reachingDefs from variable A */
            currentInfo.num_reaching_defs = bb->infoBasicBlock[jj].num_reaching_defs;
            for(k = 0; k < currentInfo.num_reaching_defs; k++)
                currentInfo.reachingDefs[k] = bb->infoBasicBlock[jj].reachingDefs[k];
            break;
        }
        else if(isBPartiallyOverlapA != OVERLAP_NO) {
            VirtualRegInfo tmpInfo;

            /* B overlaps with A */
            /* update reaching defs of variable B by checking reaching defs of bb->infoBasicBlock[jj] */
            int retCode = updateReachingDefB1 (currentInfo, tmpInfo, jj);
            if (retCode < 0)
                return retCode;
            retCode = updateReachingDefB2 (currentInfo, tmpInfo); //merge currentInfo with tmpInfo
            if (retCode < 0)
                return retCode;
        }
    }
    /* update defUseTable by checking currentInfo */
    updateDefUseTable (currentInfo);
    return 0;
}

/**
 * @brief Update xferPoints of currentBB
 * @return -1 on error, 0 otherwise
*/
int updateXferPoints (BasicBlock_O1 *bb)
{
    //First clear the XferPoints
    bb->xferPoints.clear ();

    //Get a local version of defUseTable
    DefUsePair* ptr = bb->defUseTable;

    /* Traverse the def-use chain of the basic block */
    while(ptr != NULL) {
        LowOpndRegType defType = ptr->def.physicalType;
        //if definition is for a variable of 32 bits
        if(getRegSize(defType) == OpndSize_32) {
            /* check usages of the definition, whether it reaches a GPR, a XMM, a FS, or a SS */
            bool hasGpUsage = false;
            bool hasGpUsage2 = false; //not a fake usage
            bool hasXmmUsage = false;
            bool hasFSUsage = false;
            bool hasSSUsage = false;

            //Get the uses
            DefOrUseLink* ptrUse = ptr->uses;
            while(ptrUse != NULL) {
                if(ptrUse->physicalType == LowOpndRegType_gp) {
                    hasGpUsage = true;
                    if(ptrUse->offsetPC != PC_FOR_END_OF_BB)
                        hasGpUsage2 = true;
                }
                if(ptrUse->physicalType == LowOpndRegType_ss) hasSSUsage = true;
                if(ptrUse->physicalType == LowOpndRegType_fs ||
                   ptrUse->physicalType == LowOpndRegType_fs_s)
                    hasFSUsage = true;
                if(ptrUse->physicalType == LowOpndRegType_xmm) {
                    hasXmmUsage = true;
                }
                if(ptrUse->physicalType == LowOpndRegType_xmm ||
                   ptrUse->physicalType == LowOpndRegType_ss) {
                    /* if a 32-bit definition reaches a xmm usage or a SS usage,
                       insert a XFER_MEM_TO_XMM */
                    int retCode = insertLoadXfer(ptrUse->offsetPC,
                                   ptrUse->regNum, LowOpndRegType_xmm);
                    if (retCode < 0)
                        return retCode;
                }
                ptrUse = ptrUse->next;
            }
            if(((hasXmmUsage || hasFSUsage || hasSSUsage) && defType == LowOpndRegType_gp) ||
               (hasGpUsage && defType == LowOpndRegType_fs) ||
               (defType == LowOpndRegType_ss && (hasGpUsage || hasXmmUsage || hasFSUsage))) {
                /* insert a transfer if def is on a GPR, usage is on a XMM, FS or SS
                                     if def is on a FS, usage is on a GPR
                                     if def is on a SS, usage is on a GPR, XMM or FS
                   transfer type is XFER_DEF_TO_GP_MEM if a real GPR usage exisits
                   transfer type is XFER_DEF_TO_GP otherwise*/
                XferPoint point;
                point.offsetPC = ptr->def.offsetPC;
                point.regNum = ptr->def.regNum;
                point.physicalType = ptr->def.physicalType;
                if(hasGpUsage2) { //create an entry XFER_DEF_TO_GP_MEM
                    point.xtype = XFER_DEF_TO_GP_MEM;
                }
                else { //create an entry XFER_DEF_TO_MEM
                    point.xtype = XFER_DEF_TO_MEM;
                }
                point.tableIndex = 0;
#ifdef DEBUG_XFER_POINTS
                ALOGI("Insert XFER %d at def %d: V%d %d", bb->xferPoints.size (), ptr->def.offsetPC, ptr->def.regNum, defType);
#endif

                //Push new point
                bb->xferPoints.push_back (point);

                if(bb->xferPoints.size () >= MAX_XFER_PER_BB) {
                    ALOGI("JIT_INFO: Number of transfer points (%d) exceed maximum (%d)", bb->xferPoints.size (), MAX_XFER_PER_BB);
                    SET_JIT_ERROR(kJitErrorMaxXferPoints);
                    return -1;
                }
            }
        }
        else { /* def is on 64 bits */
            bool hasGpUsageOfL = false; //exist a GPR usage of the low half
            bool hasGpUsageOfH = false; //exist a GPR usage of the high half
            bool hasGpUsageOfL2 = false;
            bool hasGpUsageOfH2 = false;
            bool hasMisaligned = false;
            bool hasAligned = false;
            bool hasFSUsage = false;
            bool hasSSUsage = false;

            //Get the uses
            DefOrUseLink* ptrUse = ptr->uses;
            while(ptrUse != NULL) {
                if(ptrUse->physicalType == LowOpndRegType_gp &&
                   ptrUse->regNum == ptr->def.regNum) {
                    hasGpUsageOfL = true;
                    if(ptrUse->offsetPC != PC_FOR_END_OF_BB)
                        hasGpUsageOfL2 = true;
                }
                if(ptrUse->physicalType == LowOpndRegType_gp &&
                   ptrUse->regNum == ptr->def.regNum + 1) {
                    hasGpUsageOfH = true;
                    if(ptrUse->offsetPC != PC_FOR_END_OF_BB)
                        hasGpUsageOfH2 = true;
                }
                if(ptrUse->physicalType == LowOpndRegType_xmm &&
                   ptrUse->regNum == ptr->def.regNum) {
                    hasAligned = true;
                    /* if def is on FS and use is on XMM, insert a XFER_MEM_TO_XMM */
                    if(defType == LowOpndRegType_fs) {
                        int retCode = insertLoadXfer(ptrUse->offsetPC,
                                       ptrUse->regNum, LowOpndRegType_xmm);
                        if (retCode < 0)
                            return retCode;
                    }
                }
                if(ptrUse->physicalType == LowOpndRegType_fs ||
                   ptrUse->physicalType == LowOpndRegType_fs_s)
                    hasFSUsage = true;
                if(ptrUse->physicalType == LowOpndRegType_xmm &&
                   ptrUse->regNum != ptr->def.regNum) {
                    hasMisaligned = true;
                    /* if use is on XMM and use and def are misaligned, insert a XFER_MEM_TO_XMM */
                    int retCode = insertLoadXfer(ptrUse->offsetPC,
                                   ptrUse->regNum, LowOpndRegType_xmm);
                    if (retCode < 0)
                        return retCode;
                }
                if(ptrUse->physicalType == LowOpndRegType_ss) {
                    hasSSUsage = true;
                    /* if use is on SS, insert a XFER_MEM_TO_XMM */
                    int retCode = insertLoadXfer(ptrUse->offsetPC,
                                   ptrUse->regNum, LowOpndRegType_ss);
                    if (retCode < 0)
                        return retCode;
                }
                ptrUse = ptrUse->next;
            }
            if(defType == LowOpndRegType_fs && !hasGpUsageOfL && !hasGpUsageOfH) {
                ptr = ptr->next;
                continue;
            }
            if(defType == LowOpndRegType_xmm && !hasFSUsage &&
               !hasGpUsageOfL && !hasGpUsageOfH && !hasMisaligned && !hasSSUsage) {
                ptr = ptr->next;
                continue;
            }
            /* insert a XFER_DEF_IS_XMM */
            XferPoint point;

            point.regNum = ptr->def.regNum;
            point.offsetPC = ptr->def.offsetPC;
            point.physicalType = ptr->def.physicalType;
            point.xtype = XFER_DEF_IS_XMM;
            point.vr_gpl = -1;
            point.vr_gph = -1;
            if(hasGpUsageOfL2) {
                point.vr_gpl = ptr->def.regNum;
            }
            if(hasGpUsageOfH2) {
                point.vr_gph = ptr->def.regNum+1;
            }
            point.dumpToMem = true;
            point.dumpToXmm = false; //not used in updateVirtualReg
            if(hasAligned) {
                point.dumpToXmm = true;
            }
            point.tableIndex = 0;
#ifdef DEBUG_XFER_POINTS
            ALOGI("Insert XFER %d at def %d: V%d %d", bb->xferPoints.size (), ptr->def.offsetPC, ptr->def.regNum, defType);
#endif
            //Push new point
            bb->xferPoints.push_back (point);

            if(bb->xferPoints.size () >= MAX_XFER_PER_BB) {
                ALOGI("JIT_INFO: Number of transfer points (%d) exceed maximum (%d)", bb->xferPoints.size (), MAX_XFER_PER_BB);
                SET_JIT_ERROR(kJitErrorMaxXferPoints);
                return -1;
            }
        }

        //Get next pointer
        ptr = ptr->next;
    } //while ptr

#ifdef DEBUG_XFER_POINTS
    ALOGI("XFER points for current basic block ------");
    unsigned int max = bb->xferPoints.size ();
    for(unsigned int k = 0; k < max; k++) {
        ALOGI("  at offset %x, VR %d %d: type %d, vr_gpl %d, vr_gph %d, dumpToMem %d, dumpToXmm %d",
              bb->xferPoints[k].offsetPC, bb->xferPoints[k].regNum,
              bb->xferPoints[k].physicalType, bb->xferPoints[k].xtype,
              bb->xferPoints[k].vr_gpl, bb->xferPoints[k].vr_gph,
              bb->xferPoints[k].dumpToMem, bb->xferPoints[k].dumpToXmm);
    }
#endif

    //Report success
    return 0;
}

/**
 * @brief This function changes location of transfer points. It should be used in inline optimizations
 * for cases when some MIRs are optimized away but their transfer points have to be retained.
 * @param bb The current basic block
 * @param oldOffset is a previous offset
 * @param newOffset is a new offset
 */
void relocateXferPoints (BasicBlock_O1 *bb, int oldOffset, int newOffset)
{
    unsigned int max = bb->xferPoints.size ();

    for(unsigned int k = 0; k < max; k++)
    {
        if(bb->xferPoints[k].offsetPC == oldOffset)
        {
           bb->xferPoints[k].offsetPC = newOffset;
        }
    }
}

/**
 * @brief Used to handle type transfer points at the start of the BB
 * @param bb The current basic block
 */
void handleStartOfBBXferPoints (BasicBlock_O1 *bb)
{
    //Walk through the BB's transfer points
    for (std::vector<XferPoint>::const_iterator xferIter = bb->xferPoints.begin (); xferIter != bb->xferPoints.end ();
            xferIter++)
    {
        const XferPoint &transfer = *xferIter;

        //If we have a transfer of VR defined at start of BB and the transfer involves storing back to memory
        //then we handle it right now.
        if (transfer.offsetPC == PC_FOR_START_OF_BB
                && (transfer.xtype == XFER_DEF_TO_MEM
                        || transfer.xtype == XFER_DEF_TO_GP_MEM
                        || transfer.xtype == XFER_DEF_IS_XMM))
        {
            int vR = transfer.regNum;

            //Look through compile table to find the physical register for this VR
            for (CompileTable::iterator compileIter = compileTable.begin (); compileIter != compileTable.end ();
                    compileIter++)
            {
                CompileTableEntry &compileEntry = *compileIter;

                //We found what we're looking for when we find the VR we care about in a physical register
                if (compileEntry.isVirtualReg () == true
                        && compileEntry.getRegisterNumber () == vR
                        && compileEntry.inPhysicalRegister( ) == true)
                {
                    //Write back the VR to memory
                    int index = searchCompileTable(compileEntry.getPhysicalType () | LowOpndRegType_virtual, vR);
                    if(index < 0) {
                        ALOGI("JIT_INFO: Cannot find VR %d %d in spillVirtualReg", vR, compileEntry.getPhysicalType ());
                        SET_JIT_ERROR(kJitErrorRegAllocFailed);
                        return;
                    }
                    spillLogicalReg(index, true);
                }
            }
        }
    }
}

/* @brief update memVRTable[].ranges by browsing the defUseTable
 *
 * @details each virtual register has a list of live ranges, and
 * each live range has a list of PCs that access the VR
 *
 * @return -1 if error happened, 0 otherwise
 */
int updateLiveTable (BasicBlock_O1 *bb)
{
    int retCode = 0;
    DefUsePair* ptr = bb->defUseTable;
    while(ptr != NULL) {
        bool updateUse = false;
        if(ptr->num_uses == 0) {
            ptr->num_uses = 1;
            ptr->uses = (DefOrUseLink*)malloc(sizeof(DefOrUseLink));
            if(ptr->uses == NULL) {
                ALOGI("JIT_INFO: Memory allocation failed in updateLiveTable");
                SET_JIT_ERROR(kJitErrorMallocFailed);
                return -1;
            }
            ptr->uses->accessType = REGACCESS_D;
            ptr->uses->regNum = ptr->def.regNum;
            ptr->uses->offsetPC = ptr->def.offsetPC;
            ptr->uses->physicalType = ptr->def.physicalType;
            ptr->uses->next = NULL;
            ptr->useTail = ptr->uses;
            updateUse = true;
        }
        DefOrUseLink* ptrUse = ptr->uses;
        while(ptrUse != NULL) {
            RegAccessType useType = ptrUse->accessType;
            if(useType == REGACCESS_L || useType == REGACCESS_D) {
                int indexL = searchMemTable(ptrUse->regNum);
                if(indexL >= 0) {
                    retCode = mergeLiveRange(indexL, ptr->def.offsetPC,
                                   ptrUse->offsetPC); //tableIndex, start PC, end PC
                    if (retCode < 0)
                        return retCode;
                }
            }
            if(getRegSize(ptrUse->physicalType) == OpndSize_64 &&
               (useType == REGACCESS_H || useType == REGACCESS_D)) {
                int indexH = searchMemTable(ptrUse->regNum+1);
                if(indexH >= 0) {
                    retCode = mergeLiveRange(indexH, ptr->def.offsetPC,
                                   ptrUse->offsetPC);
                    if (retCode < 0)
                        return retCode;
                }
            }
            ptrUse = ptrUse->next;
        }//while ptrUse
        if(updateUse) {
            ptr->num_uses = 0;
            free(ptr->uses);
            ptr->uses = NULL;
            ptr->useTail = NULL;
        }
        ptr = ptr->next;
    }//while ptr
#ifdef DEBUG_LIVE_RANGE
    ALOGI("LIVE TABLE");
    for(int k = 0; k < num_memory_vr; k++) {
        ALOGI("VR %d live ", memVRTable[k].regNum);
        LiveRange* ptr = memVRTable[k].ranges;
        while(ptr != NULL) {
            ALOGI("[%x %x] (", ptr->start, ptr->end);
            for(int k3 = 0; k3 < ptr->num_access; k3++)
                ALOGI("%x ", ptr->accessPC[k3]);
            ALOGI(") ");
            ptr = ptr->next;
        }
        ALOGI("");
    }
#endif
    return 0;
}

/* @brief Add a live range [rangeStart, rangeEnd] to ranges of memVRTable,
 * merge to existing live ranges if necessary
 *
 * @details ranges are in increasing order of startPC
 *
 * @param tableIndex index into memVRTable
 * @param rangeStart start of live range
 * @param rangeEnd end of live range
 *
 * @return -1 if error, 0 otherwise
 */
static int mergeLiveRange(int tableIndex, int rangeStart, int rangeEnd) {
    if(rangeStart == PC_FOR_START_OF_BB) rangeStart = currentBB->pc_start;
    if(rangeEnd == PC_FOR_END_OF_BB) rangeEnd = currentBB->pc_end;
#ifdef DEBUG_LIVE_RANGE
    ALOGI("LIVERANGE call mergeLiveRange on tableIndex %d with [%x %x]", tableIndex, rangeStart, rangeEnd);
#endif
    int startIndex = -1, endIndex = -1;
    bool startBeforeRange = false, endBeforeRange = false; //before the index or in the range
    bool startDone = false, endDone = false;
    LiveRange* ptr = memVRTable[tableIndex].ranges;
    LiveRange* ptrStart = NULL;
    LiveRange* ptrStart_prev = NULL;
    LiveRange* ptrEnd = NULL;
    LiveRange* ptrEnd_prev = NULL;
    int k = 0;
    while(ptr != NULL) {
        if(!startDone) {
            if(ptr->start <= rangeStart &&
               ptr->end >= rangeStart) {
                startIndex = k;
                ptrStart = ptr;
                startBeforeRange = false;
                startDone = true;
            }
            else if(ptr->start > rangeStart) {
                startIndex = k;
                ptrStart = ptr;
                startBeforeRange = true;
                startDone = true;
            }
        }
        if(!startDone) ptrStart_prev = ptr;
        if(!endDone) {
            if(ptr->start <= rangeEnd &&
               ptr->end >= rangeEnd) {
                endIndex = k;
                ptrEnd = ptr;
                endBeforeRange = false;
                endDone = true;
            }
            else if(ptr->start > rangeEnd) {
                endIndex = k;
                ptrEnd = ptr;
                endBeforeRange = true;
                endDone = true;
            }
        }
        if(!endDone) ptrEnd_prev = ptr;
        ptr = ptr->next;
        k++;
    } //while
    if(!startDone) { //both can be NULL
        startIndex = memVRTable[tableIndex].num_ranges;
        ptrStart = NULL; //ptrStart_prev should be the last live range
        startBeforeRange = true;
    }
    //if endDone, ptrEnd is not NULL, ptrEnd_prev can be NULL
    if(!endDone) { //both can be NULL
        endIndex = memVRTable[tableIndex].num_ranges;
        ptrEnd = NULL;
        endBeforeRange = true;
    }
    if(startIndex == endIndex && startBeforeRange && endBeforeRange) { //insert at startIndex
        //3 cases depending on BeforeRange when startIndex == endIndex
        //insert only if both true
        //merge otherwise
        /////////// insert before ptrStart
        LiveRange* currRange = (LiveRange *)malloc(sizeof(LiveRange));
        if(ptrStart_prev == NULL) {
            currRange->next = memVRTable[tableIndex].ranges;
            memVRTable[tableIndex].ranges = currRange;
        } else {
            currRange->next = ptrStart_prev->next;
            ptrStart_prev->next = currRange;
        }
        currRange->start = rangeStart;
        currRange->end = rangeEnd;
        currRange->accessPC = (int *)malloc(sizeof(int) * NUM_ACCESS_IN_LIVERANGE);
        currRange->num_alloc = NUM_ACCESS_IN_LIVERANGE;
        if(rangeStart != rangeEnd) {
            currRange->num_access = 2;
            currRange->accessPC[0] = rangeStart;
            currRange->accessPC[1] = rangeEnd;
        } else {
            currRange->num_access = 1;
            currRange->accessPC[0] = rangeStart;
        }
        memVRTable[tableIndex].num_ranges++;
#ifdef DEBUG_LIVE_RANGE
        ALOGI("LIVERANGE insert one live range [%x %x] to tableIndex %d", rangeStart, rangeEnd, tableIndex);
#endif
        return 0;
    }
    if(!endBeforeRange) { //here ptrEnd is not NULL
        endIndex++; //next
        ptrEnd_prev = ptrEnd; //ptrEnd_prev is not NULL
        ptrEnd = ptrEnd->next; //ptrEnd can be NULL
    }

    if(endIndex < startIndex+1) {
        ALOGI("JIT_INFO: mergeLiveRange endIndex %d is less than startIndex %d\n", endIndex, startIndex);
        SET_JIT_ERROR(kJitErrorMergeLiveRange);
        return -1;
    }
    ///////// use ptrStart & ptrEnd_prev
    if(ptrStart == NULL || ptrEnd_prev == NULL) {
        ALOGI("JIT_INFO: mergeLiveRange ptr is NULL\n");
        SET_JIT_ERROR(kJitErrorMergeLiveRange);
        return -1;
    }
    //endIndex > startIndex (merge the ranges between startIndex and endIndex-1)
    //update ptrStart
    if(ptrStart->start > rangeStart)
        ptrStart->start = rangeStart; //min of old start & rangeStart
    ptrStart->end = ptrEnd_prev->end; //max of old end & rangeEnd
    if(rangeEnd > ptrStart->end)
        ptrStart->end = rangeEnd;
#ifdef DEBUG_LIVE_RANGE
    ALOGI("LIVERANGE merge entries for tableIndex %d from %d to %d", tableIndex, startIndex+1, endIndex-1);
#endif
    if(ptrStart->num_access <= 0) {
        ALOGI("JIT_INFO: mergeLiveRange number of access");
        SET_JIT_ERROR(kJitErrorMergeLiveRange);
    }
#ifdef DEBUG_LIVE_RANGE
    ALOGI("LIVERANGE tableIndex %d startIndex %d num_access %d (", tableIndex, startIndex, ptrStart->num_access);
    for(k = 0; k < ptrStart->num_access; k++)
        ALOGI("%x ", ptrStart->accessPC[k]);
    ALOGI(")");
#endif
    ///// go through pointers from ptrStart->next to ptrEnd
    //from startIndex+1 to endIndex-1
    ptr = ptrStart->next;
    while(ptr != NULL && ptr != ptrEnd) {
        int k2;
        for(k2 = 0; k2 < ptr->num_access; k2++) { //merge to startIndex
            insertAccess(tableIndex, ptrStart, ptr->accessPC[k2]);
        }//k2
        ptr = ptr->next;
    }
    insertAccess(tableIndex, ptrStart, rangeStart);
    insertAccess(tableIndex, ptrStart, rangeEnd);
    //remove startIndex+1 to endIndex-1
    if(startIndex+1 < endIndex) {
        ptr = ptrStart->next;
        while(ptr != NULL && ptr != ptrEnd) {
            LiveRange* tmpP = ptr->next;
            free(ptr->accessPC);
            free(ptr);
            ptr = tmpP;
        }
        ptrStart->next = ptrEnd;
    }
    memVRTable[tableIndex].num_ranges -= (endIndex - startIndex - 1);
#ifdef DEBUG_LIVE_RANGE
    ALOGI("num_ranges for VR %d: %d", memVRTable[tableIndex].regNum, memVRTable[tableIndex].num_ranges);
#endif
    return 0;
}

//! insert an access to a given live range, in order

//!
void insertAccess(int tableIndex, LiveRange* startP, int rangeStart) {
    int k3, k4;
#ifdef DEBUG_LIVE_RANGE
    ALOGI("LIVERANGE insertAccess %d %x", tableIndex, rangeStart);
#endif
    int insertIndex = -1;
    for(k3 = 0; k3 < startP->num_access; k3++) {
        if(startP->accessPC[k3] == rangeStart) {
            return;
        }
        if(startP->accessPC[k3] > rangeStart) {
            insertIndex = k3;
            break;
        }
    }

    //insert here
    k3 = insertIndex;
    if(insertIndex == -1) {
        k3 = startP->num_access;
    }
    if(startP->num_access == startP->num_alloc) {
        int currentAlloc = startP->num_alloc;
        startP->num_alloc += NUM_ACCESS_IN_LIVERANGE;
        int* tmpPtr = (int *)malloc(sizeof(int) * startP->num_alloc);
        for(k4 = 0; k4 < currentAlloc; k4++)
            tmpPtr[k4] = startP->accessPC[k4];
        free(startP->accessPC);
        startP->accessPC = tmpPtr;
    }
    //insert accessPC
    for(k4 = startP->num_access-1; k4 >= k3; k4--)
        startP->accessPC[k4+1] = startP->accessPC[k4];
    startP->accessPC[k3] = rangeStart;
#ifdef DEBUG_LIVE_RANGE
    ALOGI("LIVERANGE insert %x to tableIndex %d", rangeStart, tableIndex);
#endif
    startP->num_access++;
    return;
}

/////////////////////////////////////////////////////////////////////
bool isVRLive(int vA);
int getSpillIndex (OpndSize size);
void clearVRToMemory(int regNum, OpndSize size);
void clearVRNullCheck(int regNum, OpndSize size);

inline int getSpillLocDisp(int offset) {
#ifdef SPILL_IN_THREAD
    return offset+offsetof(Thread, spillRegion);;
#else
    return offset+offEBP_spill;
#endif
}
#if 0
/* used if we keep self pointer in a physical register */
inline int getSpillLocReg(int offset) {
    return PhysicalReg_Glue;
}
#endif
#ifdef SPILL_IN_THREAD
inline void loadFromSpillRegion_with_self(OpndSize size, int reg_self, bool selfPhysical, int reg, int offset) {
    /* only 1 instruction is generated by move_mem_to_reg_noalloc */
    move_mem_to_reg_noalloc(size,
                            getSpillLocDisp(offset), reg_self, selfPhysical,
                            MemoryAccess_SPILL, offset,
                            reg, true);
}
inline void loadFromSpillRegion(OpndSize size, int reg, int offset) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    int reg_self = registerAlloc(LowOpndRegType_scratch, C_SCRATCH_1, isScratchPhysical, false, true);
    /* only 1 instruction is generated by move_mem_to_reg_noalloc */
    move_mem_to_reg_noalloc(size,
                            getSpillLocDisp(offset), reg_self, true,
                            MemoryAccess_SPILL, offset,
                            reg, true);
}
inline void saveToSpillRegion_with_self(OpndSize size, int selfReg, bool selfPhysical, int reg, int offset) {
    move_reg_to_mem_noalloc(size,
                            reg, true,
                            getSpillLocDisp(offset), selfReg, selfPhysical,
                            MemoryAccess_SPILL, offset);
}
inline void saveToSpillRegion(OpndSize size, int reg, int offset) {
    get_self_pointer(C_SCRATCH_1, isScratchPhysical);
    int reg_self = registerAlloc(LowOpndRegType_scratch, C_SCRATCH_1, isScratchPhysical, false);
    move_reg_to_mem_noalloc(size,
                            reg, true,
                            getSpillLocDisp(offset), reg_self, true,
                            MemoryAccess_SPILL, offset);
}
#else
inline void loadFromSpillRegion(OpndSize size, int reg, int offset) {
    /* only 1 instruction is generated by move_mem_to_reg_noalloc */
    move_mem_to_reg_noalloc(size,
                            getSpillLocDisp(offset), PhysicalReg_EBP, true,
                            MemoryAccess_SPILL, offset,
                            reg, true);
}
inline void saveToSpillRegion(OpndSize size, int reg, int offset) {
    move_reg_to_mem_noalloc(size,
                            reg, true,
                            getSpillLocDisp(offset), PhysicalReg_EBP, true,
                            MemoryAccess_SPILL, offset);
}
#endif

/**
 * @brief reset physical reg for a VR in CompileTable
 * @details The method is used when there is a need to
 * operate on VR in memory directly. In that case we
 * want to be sure all physical registers are spilled
 * @param vR Virtual Register to reset in CompileTable
 */

void resetVRInCompileTable(int vR)
{
    int k;
    for(k = 0; k < compileTable.size (); k++)
    {
        if(compileTable[k].regNum == vR && compileTable[k].physicalReg != PhysicalReg_Null)
        {
            if (compileTable[k].isVirtualReg ()) {
                spillLogicalReg(k, true);
            }
        }

    }

}

/**
 * @brief If VR is dirty, it writes the constant value to the VR on stack
 * @details If VR is wide, this function should be called separately twice,
 * once with the low bits and once with the high bits.
 * @param vR virtual register
 * @param value constant value of the VR
 */
void writeBackConstVR(int vR, int value) {
    // If VR is already in memory, we do not need to write it back
    if(isInMemory(vR, OpndSize_32)) {
        DEBUG_SPILL(ALOGD("Skip dumpImmToMem v%d size %d", vR, size));
        return;
    }

    // Do the actual move to set constant to VR in stack
    set_VR_to_imm_noalloc(vR, OpndSize_32, value);

    // Mark the VR as in memory now
    setVRMemoryState(vR, OpndSize_32, true);
}

/**
 * @brief Writes back VR to memory if dirty.
 * @param vR virtual register
 * @param type physical type as noted by compile table
 * @param physicalReg physical register
 */
void writeBackVR(int vR, LowOpndRegType type, int physicalReg) {
    int physicalType = type & MASK_FOR_TYPE;

    // Paranoid check because we only handle writing back if in either
    // GP or XMM registers
    assert((physicalReg >= PhysicalReg_StartOfGPMarker &&
            physicalReg <= PhysicalReg_EndOfGPMarker) ||
            (physicalReg >= PhysicalReg_StartOfXmmMarker &&
                    physicalReg <= PhysicalReg_EndOfXmmMarker));

    // If VR is already in memory, we can skip writing it back
    if (isInMemory(vR, getRegSize(physicalType))) {
        DEBUG_SPILL(ALOGD("Skip writeBackVR v%d type %d", vR, physicalType));
        return;
    }

    // Handle writing back different types of VRs
    if (physicalType == LowOpndRegType_gp || physicalType == LowOpndRegType_xmm)
    {
        set_virtual_reg_noalloc(vR, getRegSize(physicalType), physicalReg,
                true);
    }
    if (physicalType == LowOpndRegType_ss)
    {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vR);
        move_ss_reg_to_mem_noalloc(physicalReg, true, vrOffset, PhysicalReg_FP,
                true, MemoryAccess_VR, vR);
    }

    // Mark it in memory because we have written it back
    setVRMemoryState (vR, getRegSize (physicalType), true);
}
//! dump part of a 64-bit VR to memory and update inMemory

//! isLow tells whether low half or high half is dumped
void dumpPartToMem(int reg /*xmm physical reg*/, int vA, bool isLow) {
    if(isLow) {
        if(isInMemory(vA, OpndSize_32)) {
            DEBUG_SPILL(ALOGD("Skip dumpPartToMem isLow %d v%d", isLow, vA));
            return;
        }
    }
    else {
        if(isInMemory(vA+1, OpndSize_32)) {
            DEBUG_SPILL(ALOGD("Skip dumpPartToMem isLow %d v%d", isLow, vA));
            return;
        }
    }
    if(isLow) {
        if(!isVRLive(vA)) return;
    }
    else {
        if(!isVRLive(vA+1)) return;
    }
    //move part to vA or vA+1
    if(isLow) {
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA);
        move_ss_reg_to_mem_noalloc(reg, true,
                                   vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA);
    } else {
#ifdef SPILL_IN_THREAD
        int k = getSpillIndex (OpndSize_64);

        //H, L in 4*k+4 & 4*k
        get_self_pointer(PhysicalReg_SCRATCH_1, isScratchPhysical);
        saveToSpillRegion_with_self(OpndSize_64, PhysicalReg_SCRATCH_1, isScratchPhysical, reg, 4*k);
        //update low 32 bits of xmm reg from 4*k+4
        move_ss_mem_to_reg(NULL,
                                   getSpillLocDisp(4*k+4), PhysicalReg_SCRATCH_1, isScratchPhysical,
                                   reg, true);
#else
        // right shift high half of xmm to low half xmm
        dump_imm_reg_noalloc_alu(Mnemonic_PSRLQ, OpndSize_64, 32, reg, true, LowOpndRegType_xmm);
#endif
        //move low 32 bits of xmm reg to vA+1
        const int vrOffset = getVirtualRegOffsetRelativeToFP (vA + 1);
        move_ss_reg_to_mem_noalloc(reg, true, vrOffset, PhysicalReg_FP, true, MemoryAccess_VR, vA+1);
    }

    if (isLow)
    {
        setVRMemoryState (vA, OpndSize_32, true);
    }
    else
    {
        setVRMemoryState (vA + 1, OpndSize_32, true);
    }
}
void clearVRBoundCheck(int regNum, OpndSize size);
//! the content of a VR is no longer in memory or in physical register if the latest content of a VR is constant

//! clear nullCheckDone; if another VR is overlapped with the given VR, the content of that VR is no longer in physical register
void invalidateVRDueToConst(int reg, OpndSize size) {
    clearVRToMemory(reg, size); //memory content is out-dated
    clearVRNullCheck(reg, size);
    clearVRBoundCheck(reg, size);
    //check reg,gp reg,ss reg,xmm reg-1,xmm
    //if size is 64: check reg+1,gp|ss reg+1,xmm
    int index;
    //if VR is xmm, check whether we need to dump part of VR to memory
    index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_xmm, reg);
    if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
        ALOGI("INVALIDATE virtual reg %d type %d", reg, LowOpndRegType_xmm);
#endif
        if(size == OpndSize_32)
            dumpPartToMem(compileTable[index].physicalReg, reg, false); //dump high of xmm to memory

        compileTable[index].setPhysicalReg (PhysicalReg_Null);
    }
    index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_xmm, reg-1);
    if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
        ALOGI("INVALIDATE virtual reg %d type %d", reg-1, LowOpndRegType_xmm);
#endif
        dumpPartToMem(compileTable[index].physicalReg, reg-1, true); //dump low of xmm to memory

        compileTable[index].setPhysicalReg (PhysicalReg_Null);
    }
    index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_gp, reg);
    if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
        ALOGI("INVALIDATE virtual reg %d type %d", reg, LowOpndRegType_gp);
#endif

        compileTable[index].setPhysicalReg (PhysicalReg_Null);
    }
    index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_ss, reg);
    if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
        ALOGI("INVALIDATE virtual reg %d type %d", reg, LowOpndRegType_ss);
#endif

        compileTable[index].setPhysicalReg (PhysicalReg_Null);
    }
    if(size == OpndSize_64) {
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_xmm, reg+1);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg+1, LowOpndRegType_xmm);
#endif
            dumpPartToMem(compileTable[index].physicalReg, reg+1, false); //dump high of xmm to memory

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_gp, reg+1);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg+1, LowOpndRegType_gp);
#endif

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_ss, reg+1);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg+1, LowOpndRegType_ss);
#endif

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
}
//! check which physical registers hold out-dated content if there is a def

//! if another VR is overlapped with the given VR, the content of that VR is no longer in physical register
//! should we update inMemory?
void invalidateVR(int reg, LowOpndRegType pType) {
    //def at fs: content of xmm & gp & ss are out-dated (reg-1,xmm reg,xmm reg+1,xmm) (reg,gp|ss reg+1,gp|ss)
    //def at xmm: content of misaligned xmm & gp are out-dated (reg-1,xmm reg+1,xmm) (reg,gp|ss reg+1,gp|ss)
    //def at fs_s: content of xmm & gp are out-dated (reg-1,xmm reg,xmm) (reg,gp|ss)
    //def at gp:   content of xmm is out-dated (reg-1,xmm reg,xmm) (reg,ss)
    //def at ss:   content of xmm & gp are out-dated (reg-1,xmm reg,xmm) (reg,gp)
    int index;
    if(pType != LowOpndRegType_xmm) { //check xmm @reg
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_xmm, reg);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg, LowOpndRegType_xmm);
#endif
            if(getRegSize(pType) == OpndSize_32)
                dumpPartToMem(compileTable[index].physicalReg, reg, false); //dump high of xmm to memory

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
    //check misaligned xmm @ reg-1
    index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_xmm, reg-1);
    if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
        ALOGI("INVALIDATE virtual reg %d type %d", reg-1, LowOpndRegType_xmm);
#endif
        dumpPartToMem(compileTable[index].physicalReg, reg-1, true); //dump low of xmm to memory

        compileTable[index].setPhysicalReg (PhysicalReg_Null);
    }
    //check misaligned xmm @ reg+1
    if(pType == LowOpndRegType_xmm || pType == LowOpndRegType_fs) {
        //check reg+1,xmm
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_xmm, reg+1);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg+1, LowOpndRegType_xmm);
#endif
            dumpPartToMem(compileTable[index].physicalReg, reg+1, false); //dump high of xmm to memory

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
    if(pType != LowOpndRegType_gp) {
        //check reg,gp
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_gp, reg);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg, LowOpndRegType_gp);
#endif

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
    if(pType == LowOpndRegType_xmm || pType == LowOpndRegType_fs) {
        //check reg+1,gp
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_gp, reg+1);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg+1, LowOpndRegType_gp);
#endif

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
    if(pType != LowOpndRegType_ss) {
        //check reg,ss
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_ss, reg);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg, LowOpndRegType_ss);
#endif

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
    if(pType == LowOpndRegType_xmm || pType == LowOpndRegType_fs) {
        //check reg+1,ss
        index = searchCompileTable(LowOpndRegType_virtual | LowOpndRegType_ss, reg+1);
        if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_INVALIDATE
            ALOGI("INVALIDATE virtual reg %d type %d", reg+1, LowOpndRegType_ss);
#endif

            compileTable[index].setPhysicalReg (PhysicalReg_Null);
        }
    }
}
//! bookkeeping when a VR is updated

//! invalidate contents of some physical registers, clear nullCheckDone, and update inMemory;
//! check whether there exist tranfer points for this bytecode, if yes, perform the transfer
int updateVirtualReg(int reg, LowOpndRegType pType) {
    OpndSize size = getRegSize(pType);
    //WAS only invalidate xmm VRs for the following cases:
    //if def reaches a use of vA,xmm and (the def is not xmm or is misaligned xmm)
    //  invalidate "vA,xmm"
    invalidateVR(reg, pType);
    clearVRNullCheck(reg, size);
    clearVRBoundCheck(reg, size);
    if (pType == LowOpndRegType_fs || pType == LowOpndRegType_fs_s)
    {
        setVRMemoryState (reg, size, true);
    }
    else
    {
        clearVRToMemory (reg, size);
    }

    unsigned int max = currentBB->xferPoints.size ();
    for(unsigned int k = 0; k < max; k++) {
        if(currentBB->xferPoints[k].offsetPC == offsetPC &&
           currentBB->xferPoints[k].regNum == reg &&
           currentBB->xferPoints[k].physicalType == pType &&
           currentBB->xferPoints[k].xtype != XFER_MEM_TO_XMM) {
            //perform the corresponding action for the def
            if(currentBB->xferPoints[k].xtype == XFER_DEF_IS_XMM) {
                //def at fs: content of xmm is out-dated
                //def at xmm: content of misaligned xmm is out-dated
                //invalidateXmmVR(currentBB->xferPoints[k].tableIndex);
#ifdef DEBUG_XFER_POINTS
                if(currentBB->xferPoints[k].dumpToXmm)
                    ALOGI("XFER set_virtual_reg to xmm: xmm VR %d", reg);
#endif
                if(pType == LowOpndRegType_xmm)  {
#ifdef DEBUG_XFER_POINTS
                    ALOGI("XFER set_virtual_reg to memory: xmm VR %d", reg);
#endif
                    int index = searchCompileTable(pType | LowOpndRegType_virtual, reg);
                    if(index < 0) {
                        ALOGI("JIT_INFO: Cannot find VR %d %d in spillVirtualReg", reg, pType);
                        SET_JIT_ERROR(kJitErrorRegAllocFailed);
                        return -1;
                    }
                    spillLogicalReg(index, true);
                }
                if(currentBB->xferPoints[k].vr_gpl >= 0) { //
                }
                if(currentBB->xferPoints[k].vr_gph >= 0) {
                }
            }
            if((pType == LowOpndRegType_gp || pType == LowOpndRegType_ss) &&
               (currentBB->xferPoints[k].xtype == XFER_DEF_TO_MEM ||
                currentBB->xferPoints[k].xtype == XFER_DEF_TO_GP_MEM)) {
                //the defined gp VR already in register
                //invalidateXmmVR(currentBB->xferPoints[k].tableIndex);
#ifdef DEBUG_XFER_POINTS
                ALOGI("XFER set_virtual_reg to memory: gp VR %d", reg);
#endif
                int index = searchCompileTable(pType | LowOpndRegType_virtual, reg);
                if(index < 0) {
                    ALOGI("JIT_INFO: Cannot find VR %d %d in spillVirtualReg", reg, pType);
                    SET_JIT_ERROR(kJitErrorRegAllocFailed);
                    return -1;
                }
                spillLogicalReg(index, true);
            }
            if((pType == LowOpndRegType_fs_s || pType == LowOpndRegType_ss) &&
               currentBB->xferPoints[k].xtype == XFER_DEF_TO_GP_MEM) {
            }
        }
    }
    return 0;
}
////////////////////////////////////////////////////////////////
//REGISTER ALLOCATION
int spillForHardReg(int regNum, int type);
void decreaseRefCount(int index);
int getFreeReg(int type, int reg, int indexToCompileTable);
PhysicalReg spillForLogicalReg(int type, int reg, int indexToCompileTable);
int unspillLogicalReg(int spill_index, int physicalReg);
int searchVirtualInfoOfBB(LowOpndRegType type, int regNum, BasicBlock_O1* bb);
bool isTemp8Bit(int type, int reg);
bool matchType(int typeA, int typeB);
int getNextAccess(int compileIndex);
void dumpCompileTable();

//! allocate a register for a variable

//!if no physical register is free, call spillForLogicalReg to free up a physical register;
//!if the variable is a temporary and it was spilled, call unspillLogicalReg to load from spill location to the allocated physical register;
//!if updateRefCount is true, reduce reference count of the variable by 1
//!if isDest is true, we inform the compileTable about it
int registerAlloc(int type, int reg, bool isPhysical, bool updateRefCount, bool isDest) {
#ifdef DEBUG_REGALLOC
    ALOGI("%p: try to allocate register %d type %d isPhysical %d", currentBB, reg, type, isPhysical);
#endif
    if(currentBB == NULL) {
        if(type & LowOpndRegType_virtual) {
            return PhysicalReg_Null;
        }
        if(isPhysical) return reg; //for helper functions
        return PhysicalReg_Null;
    }
    //ignore EDI, ESP, EBP (glue)
    if(isPhysical && (reg == PhysicalReg_EDI || reg == PhysicalReg_ESP ||
                      reg == PhysicalReg_EBP || reg == PhysicalReg_Null))
        return reg;

    int newType = convertType(type, reg, isPhysical);
    if(newType & LowOpndRegType_scratch) reg = reg - PhysicalReg_SCRATCH_1 + 1;
    int tIndex = searchCompileTable(newType, reg);
    if(tIndex < 0) {
      ALOGI("JIT_INFO: reg %d type %d not found in registerAlloc\n", reg, newType);
      SET_JIT_ERROR(kJitErrorRegAllocFailed);
      return PhysicalReg_Null;
    }

    //physical register
    if(isPhysical) {
        if(allRegs[reg].isUsed) { //if used by a non hard-coded register
            spillForHardReg(reg, newType);
        }
        allRegs[reg].isUsed = true;
#ifdef DEBUG_REG_USED
        ALOGI("REGALLOC: allocate a reg %d", reg);
#endif

        //Update the physical register
        compileTable[tIndex].setPhysicalReg (reg);
        //Update the isWritten field, if isDest is true, set it to true
        if (isDest == true) {
            compileTable[tIndex].isWritten = true;
        }

        if(updateRefCount)
            decreaseRefCount(tIndex);
#ifdef DEBUG_REGALLOC
        ALOGI("REGALLOC: allocate register %d for logical register %d %d",
               compileTable[tIndex].physicalReg, reg, newType);
#endif
        return reg;
    }
    //already allocated
    if(compileTable[tIndex].physicalReg != PhysicalReg_Null) {
#ifdef DEBUG_REGALLOC
        ALOGI("already allocated to physical register %d", compileTable[tIndex].physicalReg);
#endif
        //Update the isWritten field, if isDest is true, set it to true
        if (isDest == true) {
            compileTable[tIndex].isWritten = true;
        }
        if(updateRefCount)
            decreaseRefCount(tIndex);
        return compileTable[tIndex].physicalReg;
    }

    //at this point, the logical register is not hard-coded and is mapped to Reg_Null
    //first check whether there is a free reg
    //if not, call spillForLogicalReg
    int index = getFreeReg(newType, reg, tIndex);
    if(index >= 0 && index < PhysicalReg_Null) {
        //update compileTable & allRegs
        compileTable[tIndex].setPhysicalReg (allRegs[index].physicalReg);
        allRegs[index].isUsed = true;
#ifdef DEBUG_REG_USED
        ALOGI("REGALLOC: register %d is free", allRegs[index].physicalReg);
#endif
    } else {
        PhysicalReg allocR = spillForLogicalReg(newType, reg, tIndex);
        compileTable[tIndex].setPhysicalReg (allocR);
    }
    if(compileTable[tIndex].spill_loc_index >= 0) {
        unspillLogicalReg(tIndex, compileTable[tIndex].physicalReg);
    }

    //In this case, it's a new register, set isWritten to isDest
    compileTable[tIndex].isWritten = isDest;
    if(updateRefCount)
        decreaseRefCount(tIndex);
#ifdef DEBUG_REGALLOC
    ALOGI("REGALLOC: allocate register %d for logical register %d %d",
           compileTable[tIndex].physicalReg, reg, newType);
#endif
    return compileTable[tIndex].physicalReg;
}
//!a variable will use a physical register allocated for another variable

//!This is used when MOVE_OPT is on, it tries to alias a virtual register with a temporary to remove a move
int registerAllocMove(int reg, int type, bool isPhysical, int srcReg, bool isDest) {
    if(srcReg == PhysicalReg_EDI || srcReg == PhysicalReg_ESP || srcReg == PhysicalReg_EBP) {
        ALOGI("JIT_INFO: Cannot move from srcReg EDI or ESP or EBP");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
#ifdef DEBUG_REGALLOC
    ALOGI("in registerAllocMove: reg %d type %d srcReg %d", reg, type, srcReg);
#endif
    int newType = convertType(type, reg, isPhysical);
    if(newType & LowOpndRegType_scratch) reg = reg - PhysicalReg_SCRATCH_1 + 1;
    int index = searchCompileTable(newType, reg);
    if(index < 0) {
        ALOGI("JIT_INFO: reg %d type %d not found in registerAllocMove", reg, newType);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }

    //Update the isWritten field, if isDest is true, set it to true
    if (isDest == true) {
        compileTable[index].isWritten = true;
    }
    decreaseRefCount(index);
    compileTable[index].setPhysicalReg (srcReg);

#ifdef DEBUG_REGALLOC
    ALOGI("REGALLOC: registerAllocMove %d for logical register %d %d",
           compileTable[index].physicalReg, reg, newType);
#endif
    return srcReg;
}

//! check whether a physical register is available to be used by a variable

//! data structures accessed:
//! 1> currentBB->infoBasicBlock[index].allocConstraintsSorted
//!    sorted from high count to low count
//! 2> currentBB->allocConstraintsSorted
//!    sorted from low count to high count
//! 3> allRegs: whether a physical register is available, indexed by PhysicalReg
//! NOTE: if a temporary variable is 8-bit, only %eax, %ebx, %ecx, %edx can be used
int getFreeReg(int type, int reg, int indexToCompileTable) {
    syncAllRegs();
    /* handles requests for xmm or ss registers */
    int k;
    if(((type & MASK_FOR_TYPE) == LowOpndRegType_xmm) ||
       ((type & MASK_FOR_TYPE) == LowOpndRegType_ss)) {
        for(k = PhysicalReg_XMM0; k <= PhysicalReg_XMM7; k++) {
            if(!allRegs[k].isUsed) return k;
        }
        return -1;
    }
#ifdef DEBUG_REGALLOC
    ALOGI("USED registers: ");
    for(k = 0; k < 8; k++)
        ALOGI("%d used: %d time freed: %d callee-saveld: %d", k, allRegs[k].isUsed,
             allRegs[k].freeTimeStamp, allRegs[k].isCalleeSaved);
    ALOGI("");
#endif

    /* a VR is requesting a physical register */
    if(isVirtualReg(type)) { //find a callee-saved register
        int index = searchVirtualInfoOfBB((LowOpndRegType)(type & MASK_FOR_TYPE), reg, currentBB);
        if(index < 0) {
            ALOGI("JIT_INFO: VR %d %d not found in infoBasicBlock of currentBB %d (num of VRs %d)",
                  reg, type, currentBB->id, currentBB->infoBasicBlock.size ());
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            //Error trickles down to dvmCompilerMIR2LIR, trace is rejected
            return -1;
        }

        /* check allocConstraints for this VR,
           return an available physical register with the highest constraint > 0 */
        for(k = 0; k < 8; k++) {
            if(currentBB->infoBasicBlock[index].allocConstraintsSorted[k].count == 0) break;
            int regCandidateT = currentBB->infoBasicBlock[index].allocConstraintsSorted[k].physicalReg;
            assert(regCandidateT < PhysicalReg_Null);
            if(!allRegs[regCandidateT].isUsed) return regCandidateT;
        }

        /* WAS: return an available physical register with the lowest constraint
           NOW: consider a new factor (freeTime) when there is a tie
                if 2 available physical registers have the same number of constraints
                choose the one with smaller free time stamp */
        int currentCount = -1;
        int index1 = -1;
        int smallestTime = -1;
        for(k = 0; k < 8; k++) {
            int regCandidateT = currentBB->allocConstraintsSorted[k].physicalReg;
            assert(regCandidateT < PhysicalReg_Null);
            if(index1 >= 0 && currentBB->allocConstraintsSorted[k].count > currentCount)
                break; //candidate has higher count than index1
            if(!allRegs[regCandidateT].isUsed) {
                if(index1 < 0) {
                    index1 = k;
                    currentCount = currentBB->allocConstraintsSorted[k].count;
                    smallestTime = allRegs[regCandidateT].freeTimeStamp;
                } else if(allRegs[regCandidateT].freeTimeStamp < smallestTime) {
                    index1 = k;
                    smallestTime = allRegs[regCandidateT].freeTimeStamp;
                }
            }
        }
        if(index1 >= 0) return currentBB->allocConstraintsSorted[index1].physicalReg;
        return -1;
    }
    /* handle request from a temporary variable */
    else {
        bool is8Bit = isTemp8Bit(type, reg);

        /* if the temporary variable is linked to a VR and
              the VR is not yet allocated to any physical register */
        int vr_num = compileTable[indexToCompileTable].getLinkedVR ();
        if(vr_num >= 0) {
            int index3 = searchCompileTable(LowOpndRegType_gp | LowOpndRegType_virtual, vr_num);
            if(index3 < 0) {
                ALOGI("JIT_INFO: Inavlid linkage VR for temporary register %d", vr_num);
                SET_JIT_ERROR(kJitErrorRegAllocFailed);
                //Error trickles down to dvmCompilerMIR2LIR, trace is rejected
                return -1;
            }

            if(compileTable[index3].physicalReg == PhysicalReg_Null) {
                int index2 = searchVirtualInfoOfBB(LowOpndRegType_gp, vr_num, currentBB);
                if(index2 < 0) {
                    ALOGI("JIT_INFO: In tracing linkage to VR %d", vr_num);
                    SET_JIT_ERROR(kJitErrorRegAllocFailed);
                    //Error trickles down to dvmCompilerMIR2LIR, trace is rejected
                    return -1;
                }
#ifdef DEBUG_REGALLOC
                ALOGI("In getFreeReg for temporary reg %d, trace the linkage to VR %d",
                     reg, vr_num);
#endif

                /* check allocConstraints on the VR
                   return an available physical register with the highest constraint > 0
                */
                for(k = 0; k < 8; k++) {
                    if(currentBB->infoBasicBlock[index2].allocConstraintsSorted[k].count == 0) break;
                    int regCandidateT = currentBB->infoBasicBlock[index2].allocConstraintsSorted[k].physicalReg;
#ifdef DEBUG_REGALLOC
                    ALOGI("check register %d with count %d", regCandidateT,
                          currentBB->infoBasicBlock[index2].allocConstraintsSorted[k].count);
#endif
                    /* if the requesting variable is 8 bit */
                    if(is8Bit && regCandidateT > PhysicalReg_EDX) continue;
                    assert(regCandidateT < PhysicalReg_Null);
                    if(!allRegs[regCandidateT].isUsed) return regCandidateT;
                }
            }
        }
        /* check allocConstraints of the basic block
           if 2 available physical registers have the same constraint count,
              return the non callee-saved physical reg */
        /* enhancement: record the time when a register is freed (freeTimeStamp)
                        the purpose is to reduce false dependency
           priority: constraint count, non callee-saved, time freed
               let x be the lowest constraint count
               set A be available callee-saved physical registers with count == x
               set B be available non callee-saved physical registers with count == x
               if set B is not null, return the one with smallest free time
               otherwise, return the one in A with smallest free time
           To ignore whether it is callee-saved, add all candidates to set A
        */
        int setAIndex[8];
        int num_A = 0;
        int setBIndex[8];
        int num_B = 0;
        int index1 = -1; //points to an available physical reg with lowest count
        int currentCount = -1;
        for(k = 0; k < 8; k++) {
            int regCandidateT = currentBB->allocConstraintsSorted[k].physicalReg;
            if(is8Bit && regCandidateT > PhysicalReg_EDX) continue;

            if(index1 >= 0 && currentBB->allocConstraintsSorted[k].count > currentCount)
                break; //candidate has higher count than index1
            assert(regCandidateT < PhysicalReg_Null);
            if(!allRegs[regCandidateT].isUsed) {
                /*To ignore whether it is callee-saved, add all candidates to set A */
                if(false) {//!allRegs[regCandidateT].isCalleeSaved) { //add to set B
                    setBIndex[num_B++] = k;
                } else { //add to set A
                    setAIndex[num_A++] = k;
                }
                if(index1 < 0) {
                    /* index1 points to a physical reg with lowest count */
                    index1 = k;
                    currentCount = currentBB->allocConstraintsSorted[k].count;
                }
            }
        }

        int kk;
        int smallestTime = -1;
        index1 = -1;
        for(kk = 0; kk < num_B; kk++) {
            k = setBIndex[kk];
            int regCandidateT = currentBB->allocConstraintsSorted[k].physicalReg;
            assert(regCandidateT < PhysicalReg_Null);
            if(kk == 0 || allRegs[regCandidateT].freeTimeStamp < smallestTime) {
                index1 = k;
                smallestTime = allRegs[regCandidateT].freeTimeStamp;
            }
        }
        if(index1 >= 0)
            return currentBB->allocConstraintsSorted[index1].physicalReg;
        index1 = -1;
        for(kk = 0; kk < num_A; kk++) {
            k = setAIndex[kk];
            int regCandidateT = currentBB->allocConstraintsSorted[k].physicalReg;
            if(kk == 0 || allRegs[regCandidateT].freeTimeStamp < smallestTime) {
                index1 = k;
                smallestTime = allRegs[regCandidateT].freeTimeStamp;
            }
        }
        if(index1 >= 0) return currentBB->allocConstraintsSorted[index1].physicalReg;
        return -1;
    }
    return -1;
}

//! find a candidate physical register for a variable and spill all variables that are mapped to the candidate

//!
PhysicalReg spillForLogicalReg(int type, int reg, int indexToCompileTable) {
    //choose a used register to spill
    //when a GG virtual register is spilled, write it to interpretd stack, set physicalReg to Null
    //  at end of the basic block, load spilled GG VR to physical reg
    //when other types of VR is spilled, write it to interpreted stack, set physicalReg to Null
    //when a temporary (non-virtual) register is spilled, write it to stack, set physicalReg to Null
    //can we spill a hard-coded temporary register? YES
    int k, k2;
    PhysicalReg allocR;

    //do not try to free a physical reg that is used by more than one logical registers
    //fix on sep 28, 2009
    //do not try to spill a hard-coded logical register
    //do not try to free a physical reg that is outside of the range for 8-bit logical reg
    /* for each physical register,
       collect number of non-hardcode entries that are mapped to the physical register */
    int numOfUses[PhysicalReg_Null];
    for(k = PhysicalReg_EAX; k < PhysicalReg_Null; k++)
        numOfUses[k] = 0;
    for(k = 0; k < compileTable.size (); k++) {
        if((compileTable[k].physicalReg != PhysicalReg_Null) &&
           matchType(type, compileTable[k].physicalType) &&
           (compileTable[k].physicalType & LowOpndRegType_hard) == 0) {
            numOfUses[compileTable[k].physicalReg]++;
        }
    }

    /* candidates: all non-hardcode entries that are mapped to
           a physical register that is used by only one entry*/
    bool is8Bit = isTemp8Bit(type, reg);
    int candidates[compileTable.size ()];
    int num_cand = 0;
    for(k = 0; k < compileTable.size (); k++) {
        if(matchType(type, compileTable[k].physicalType) && compileTable[k].physicalReg != PhysicalReg_Null) {
            //If we care about 8 bits, we can't have a register over EDX
            if(is8Bit == true && compileTable[k].physicalReg > PhysicalReg_EDX)
            {
                continue;
            }

            //If we can spill it, ignore it
            if(gCompilationUnit->getCanSpillRegister (compileTable[k].physicalReg) == false)
            {
                continue;
            }

            //If it isn't a hard register or it only a few uses left, it can be a candidate
            if((compileTable[k].physicalType & LowOpndRegType_hard) == 0 && numOfUses[compileTable[k].physicalReg] <= 1) {
                candidates[num_cand++] = k;
            }
        }
    }

    int spill_index = -1;

    /* out of the candates, find a VR that has the furthest next use */
    int furthestUse = offsetPC;
    for(k2 = 0; k2 < num_cand; k2++) {
        k = candidates[k2];
        if((compileTable[k].physicalReg != PhysicalReg_Null) &&
           matchType(type, compileTable[k].physicalType) &&
           isVirtualReg(compileTable[k].physicalType)) {
            int nextUse = getNextAccess(k);
            if(spill_index < 0 || nextUse > furthestUse) {
                spill_index = k;
                furthestUse = nextUse;
            }
        }
    }

    /* spill the VR with the furthest next use */
    if(spill_index >= 0) {
        allocR = (PhysicalReg)spillLogicalReg(spill_index, true);
        return allocR; //the register is still being used
    }

    /* spill an entry with the smallest refCount */
    int baseLeftOver = 0;
    int index = -1;
    for(k2 = 0; k2 < num_cand; k2++) {
        k = candidates[k2];
        if((compileTable[k].physicalReg != PhysicalReg_Null) &&
           (compileTable[k].physicalType & LowOpndRegType_hard) == 0 && //not hard-coded
           matchType(type, compileTable[k].physicalType)) {
            if((index < 0) || (compileTable[k].refCount < baseLeftOver)) {
                baseLeftOver = compileTable[k].refCount;
                index = k;
            }
        }
    }
    if(index < 0) {
        dumpCompileTable();
        ALOGI("JIT_INFO: no register to spill for logical %d %d\n", reg, type);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        //Error trickles down to dvmCompilerMIR2LIR, trace is rejected
        return PhysicalReg_Null;
    }
    allocR = (PhysicalReg)spillLogicalReg(index, true);
#ifdef DEBUG_REGALLOC
    ALOGI("SPILL register used by num %d type %d it is a temporary register with refCount %d",
           compileTable[index].regNum, compileTable[index].physicalType, compileTable[index].refCount);
#endif
    return allocR;
}

/**
 * @brief Spill a variable to memory, the variable is specified by an index to compileTable
 * @details If the variable is a temporary, get a spill location that is not in use and spill the content to the spill location;
 *       If updateTable is true, set physicalReg to Null;
 * @param spill_index the index into the compile table
 * @param updateTable do we update the table?
 * @return Return the physical register that was allocated to the variable
 */
int spillLogicalReg(int spill_index, bool updateTable) {
    if((compileTable[spill_index].physicalType & LowOpndRegType_hard) != 0) {
        ALOGI("JIT_INFO: can't spill a hard-coded register");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        //Error trickles down to dvmCompilerMIR2LIR, trace is rejected
        return -1;
    }

#ifdef PRINT_WARNING
    int physicalReg = compileTable[spill_index].physicalReg;

    //If we can't spill it, print out a warning
    if(gCompilationUnit->getCanSpillReg (physicalReg) == false) {
        // This scenario can occur whenever a VR is allocated to the
        // same physical register as a hardcoded temporary
        ALOGW("Shouldn't spill register %s but going to do it anyway.",
                physicalRegToString(static_cast<PhysicalReg>(physicalReg)));
    }
#endif

    if (compileTable[spill_index].isVirtualReg ())
    {
        //Write VR back to memory
        writeBackVR (compileTable[spill_index].getRegisterNumber (), compileTable[spill_index].getPhysicalType (),
                compileTable[spill_index].getPhysicalReg ());
    }
    else {
        //If the gCompilationUnit has maximumRegisterization set
        if (gCompilationUnit->maximumRegisterization > 0)
        {
            //Signal the error framework we spilled: this is a warning that can help recompile if possible
            SET_JIT_ERROR (kJitErrorSpill);
        }
        //update spill_loc_index
        int k = getSpillIndex (compileTable[spill_index].getSize ());
        compileTable[spill_index].spill_loc_index = 4*k;
        if(k >= 0)
            spillIndexUsed[k] = 1;
        saveToSpillRegion(getRegSize(compileTable[spill_index].physicalType),
                          compileTable[spill_index].physicalReg, 4*k);
    }
    //compileTable[spill_index].physicalReg_prev = compileTable[spill_index].physicalReg;
#ifdef DEBUG_REGALLOC
    ALOGI("REGALLOC: SPILL logical reg %d %d with refCount %d allocated to %d",
           compileTable[spill_index].regNum,
           compileTable[spill_index].physicalType, compileTable[spill_index].refCount,
           compileTable[spill_index].physicalReg);
#endif
    if(!updateTable) return PhysicalReg_Null;

    int allocR = compileTable[spill_index].physicalReg;
    compileTable[spill_index].setPhysicalReg (PhysicalReg_Null);

    return allocR;
}
//! load a varible from memory to physical register, the variable is specified with an index to compileTable

//!If the variable is a temporary, load from spill location and set the flag for the spill location to not used
int unspillLogicalReg(int spill_index, int physicalReg) {
    //can't un-spill to %eax in afterCall!!!
    //what if GG VR is allocated to %eax!!!
    if(isVirtualReg(compileTable[spill_index].physicalType)) {
        get_virtual_reg_noalloc(compileTable[spill_index].regNum,
                                getRegSize(compileTable[spill_index].physicalType),
                                physicalReg, true);
    }
    else {
        loadFromSpillRegion(getRegSize(compileTable[spill_index].physicalType),
                            physicalReg, compileTable[spill_index].spill_loc_index);
        spillIndexUsed[compileTable[spill_index].spill_loc_index >> 2] = 0;
        compileTable[spill_index].spill_loc_index = -1;
    }
#ifdef DEBUG_REGALLOC
    ALOGI("REGALLOC: UNSPILL logical reg %d %d with refCount %d", compileTable[spill_index].regNum,
           compileTable[spill_index].physicalType, compileTable[spill_index].refCount);
#endif
    return PhysicalReg_Null;
}

//!spill a virtual register to memory

//!if the current value of a VR is constant, write immediate to memory;
//!if the current value of a VR is in a physical register, call spillLogicalReg to dump content of the physical register to memory;
//!ifupdateTable is true, set the physical register for VR to Null and decrease reference count of the virtual register
int spillVirtualReg(int vrNum, LowOpndRegType type, bool updateTable) {
    int index = searchCompileTable(type | LowOpndRegType_virtual, vrNum);
    if(index < 0) {
        ALOGI("JIT_INFO: Cannot find VR %d %d in spillVirtualReg", vrNum, type);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
    //check whether it is const
    int value[2];
    int isConst = isVirtualRegConstant(vrNum, type, value, false); //do not update refCount
    if(isConst == 1 || isConst == 3) {
        writeBackConstVR(vrNum, value[0]);
    }
    if(getRegSize(type) == OpndSize_64 && (isConst == 2 || isConst == 3)) {
        writeBackConstVR(vrNum+1, value[1]);
    }
    if(isConst != 3 && compileTable[index].physicalReg != PhysicalReg_Null)
        spillLogicalReg(index, updateTable);
    if(updateTable) decreaseRefCount(index);
    return -1;
}

/**
 * @brief Writes virtual register back to memory if it holds a constant value
 * @param vR virtual register number
 * @param type the physical type register type that can be associated with
 * this VR
 * @return true if the entire VR was written back to memory
 */
bool writeBackVRIfConstant(int vR, LowOpndRegType type) {
    int constantValue[2];
    bool writtenBack = false;

    // Check if the VR is a constant. This function returns 3 if VR
    // is 32-bit and constant, or if VR is 64-bit and both high order
    // bits and low order bits are constant
    int isConst = isVirtualRegConstant(vR, type, constantValue, false);

    // If the VR is a constant, then write it back to memory
    if (isConst == 3) {
        writeBackConstVR(vR, constantValue[0]);
        writtenBack |= true;
    }

    // If VR is wide and high order bits are constant, then write them to memory
    if (getRegSize(type) == OpndSize_64 && isConst == 3) {
        writeBackConstVR(vR + 1, constantValue[1]);
        writtenBack |= true;
    }

    return writtenBack;
}

//! spill variables that are mapped to physical register (regNum)

//!
int spillForHardReg(int regNum, int type) {
    //find an entry that uses the physical register
    int spill_index = -1;
    int k;
    for(k = 0; k < compileTable.size (); k++) {
        if(compileTable[k].physicalReg == regNum &&
           matchType(type, compileTable[k].physicalType)) {
            spill_index = k;
            if(compileTable[k].regNum == regNum && compileTable[k].physicalType == type)
                continue;
            if(inGetVR_num >= 0 && compileTable[k].regNum == inGetVR_num && compileTable[k].physicalType == (type | LowOpndRegType_virtual))
                continue;
#ifdef DEBUG_REGALLOC
            ALOGI("SPILL logical reg %d %d to free hard-coded reg %d %d",
                   compileTable[spill_index].regNum, compileTable[spill_index].physicalType,
                   regNum, type);
            if(compileTable[spill_index].physicalType & LowOpndRegType_hard) dumpCompileTable();
#endif
            assert(spill_index < compileTable.size ());
            spillLogicalReg(spill_index, true);
        }
    }
    return regNum;
}
////////////////////////////////////////////////////////////////
//! update allocConstraints of the current basic block

//! allocConstraints specify how many times a hardcoded register is used in this basic block
void updateCurrentBBWithConstraints(PhysicalReg reg) {
    if (currentBB != 0)
    {
        if(reg > PhysicalReg_EBP) {
            ALOGI("JIT_INFO: Register %d out of range in updateCurrentBBWithConstraints\n", reg);
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return;
        }
        currentBB->allocConstraints[reg].count++;
    }
}
//! sort allocConstraints and save the result in allocConstraintsSorted

//! allocConstraints specify how many times a virtual register is linked to a hardcode register
//! it is updated in getVirtualRegInfo and merged by mergeEntry2
int sortAllocConstraint(RegAllocConstraint* allocConstraints,
                        RegAllocConstraint* allocConstraintsSorted, bool fromHighToLow) {
    int ii, jj;
    int num_sorted = 0;
    for(jj = 0; jj < 8; jj++) {
        //figure out where to insert allocConstraints[jj]
        int count = allocConstraints[jj].count;
        int regT = allocConstraints[jj].physicalReg;
        assert(regT < PhysicalReg_Null);
        int insertIndex = -1;
        for(ii = 0; ii < num_sorted; ii++) {
            int regT2 = allocConstraintsSorted[ii].physicalReg;
            assert(regT2 < PhysicalReg_Null);
            if(allRegs[regT].isCalleeSaved &&
               count == allocConstraintsSorted[ii].count) {
                insertIndex = ii;
                break;
            }
            if((!allRegs[regT].isCalleeSaved) &&
               count == allocConstraintsSorted[ii].count &&
               (!allRegs[regT2].isCalleeSaved)) { //skip until found one that is not callee-saved
                insertIndex = ii;
                break;
            }
            if((fromHighToLow && count > allocConstraintsSorted[ii].count) ||
               ((!fromHighToLow) && count < allocConstraintsSorted[ii].count)) {
                insertIndex = ii;
                break;
            }
        }
        if(insertIndex < 0) {
            allocConstraintsSorted[num_sorted].physicalReg = (PhysicalReg)regT;
            allocConstraintsSorted[num_sorted].count = count;
            num_sorted++;
        } else {
            for(ii = num_sorted-1; ii >= insertIndex; ii--) {
                allocConstraintsSorted[ii+1] = allocConstraintsSorted[ii];
            }
            allocConstraintsSorted[insertIndex] = allocConstraints[jj];
            num_sorted++;
        }
    } //for jj
#ifdef DEBUG_ALLOC_CONSTRAINT
    for(jj = 0; jj < 8; jj++) {
        if(allocConstraintsSorted[jj].count > 0)
            ALOGI("%d: register %d has count %d", jj, allocConstraintsSorted[jj].physicalReg, allocConstraintsSorted[jj].count);
    }
#endif
    return 0;
}

//! \brief find the entry for a given virtual register in compileTable
//! \param vA The VR to search for
//! \param type Register type
//! \return the virtual reg if found, else -1 as error.
int findVirtualRegInTable(int vA, LowOpndRegType type) {
    int k = searchCompileTable(type | LowOpndRegType_virtual, vA);
    if(k < 0) {
        ALOGI("JIT_INFO: Couldn't find virtual register %d type %d in compiler table\n", vA, type);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        //Error trickles down to dvmCompilerMIR2LIR, trace is rejected
        return -1;
    }
    return k;
}

/**
 * @brief Checks whether the virtual register is constant.
 * @param regNum The virtual register to check.
 * @param opndRegType The physical type of this virtual register.
 * @param valuePtr If non-null, this is updated by function to contain the constant values.
 * @param updateRefCount When set, lowers reference count in compile table for this VR.
 * @return Returns information about the constantness of this VR.
 */
VirtualRegConstantness isVirtualRegConstant(int regNum, int opndRegType, int *valuePtr, bool updateRefCount)
{
    //Determine the size of the VR by looking at its physical type
    OpndSize size = getRegSize (opndRegType);

    //Use these to keep track of index in the constant table of the VR we are looking for
    int indexL = -1;
    int indexH = -1;

    //Iterate through constant table to find the desired virtual register
    for(int k = 0; k < num_const_vr; k++)
    {
#ifdef DEBUG_CONST
        ALOGI("constVRTable VR %d isConst %d value %x", constVRTable[k].regNum, constVRTable[k].isConst, constVRTable[k].value);
#endif
        if(constVRTable[k].regNum == regNum)
        {
            indexL = k;
            continue;
        }

        if(constVRTable[k].regNum == regNum + 1 && size == OpndSize_64)
        {
            indexH = k;
            continue;
        }
    }

    //Eagerly assume that we won't find constantness of this VR
    bool isConstL = false;
    bool isConstH = false;

    //If we found an entry in constant table for this VR, check if it is constant.
    if(indexL >= 0)
    {
        isConstL = constVRTable[indexL].isConst;
    }

    //If we found an entry in constant table for the high part of VR, check if it is constant.
    if(size == OpndSize_64 && indexH >= 0)
    {
        isConstH = constVRTable[indexH].isConst;
    }

    //Only tell the caller the values of constant if space has been provided for this purpose
    if (valuePtr != 0)
    {
        //Are either the low bits or high bits constant?
        if (isConstL == true || isConstH == true)
        {
            //If the high bits are constant and we care about them, then set value.
            if (size == OpndSize_64 && isConstH == true)
            {
                valuePtr[1] = constVRTable[indexH].value;
            }

            //Now see if we can set value for the low bits
            if (isConstL == true)
            {
                valuePtr[0] = constVRTable[indexL].value;
            }
        }
    }

    //If we are looking at non-wide VR that is constant or wide VR whose low and high parts are both constant,
    //then we say that this VR is constant.
    if((isConstL == true && size == OpndSize_32) || (isConstL == true && isConstH == true))
    {
        if(updateRefCount)
        {
            //We want to find entry in the compile table that matches the physical type we want.
            //Since compile table keeps track of physical type along with logical type in same field,
            //we do a binary bitwise inclusive or including the virtual register type.
            int indexOrig = searchCompileTable(opndRegType | LowOpndRegType_virtual, regNum);

            if(indexOrig < 0)
            {
                //We were not able to find the virtual register in compile table so just set an error
                //and say that it is not constant.
                ALOGI("JIT_INFO: Cannot find VR in isVirtualRegConstant num %d type %d\n", regNum, opndRegType);
                SET_JIT_ERROR(kJitErrorRegAllocFailed);
                return VR_IS_NOT_CONSTANT;
            }

            //Decrement the reference count for this VR
            decreaseRefCount(indexOrig);
        }
#ifdef DEBUG_CONST
        ALOGI("VR %d %d is const case", regNum, type);
#endif
        return VR_IS_CONSTANT;
    }
    else if (isConstL == true && size != OpndSize_32)
    {
        //If the VR is wide and only low part is constant, we return saying that
        return VR_LOW_IS_CONSTANT;
    }
    else if (isConstH == true && size != OpndSize_32)
    {
        //If the VR is wide and only high part is constant, we return saying that
        return VR_HIGH_IS_CONSTANT;
    }
    else
    {
        //If we make it here, this VR is not constant
        return VR_IS_NOT_CONSTANT;
    }
}

//!update RegAccessType of virtual register vB given RegAccessType of vA

//!RegAccessType can be D, L, H
//!D means full definition, L means only lower-half is defined, H means only higher half is defined
//!we say a VR has no exposed usage in a basic block if the accessType is D or DU
//!we say a VR has exposed usage in a basic block if the accessType is not D nor DU
//!we say a VR has exposed usage in other basic blocks (hasOtherExposedUsage) if
//!  there exists another basic block where VR has exposed usage in that basic block
//!A can be U, D, L, H, UD, UL, UH, DU, LU, HU (merged result)
//!B can be U, D, UD, DU (an entry for the current bytecode)
//!input isAPartiallyOverlapB can be any value between -1 to 6
//!if A is xmm: gp B lower half of A, (isAPartiallyOverlapB is 1)
//!             gp B higher half of A, (isAPartiallyOverlapB is 2)
//!             lower half of A covers the higher half of xmm B  (isAPartiallyOverlapB is 4)
//!             higher half of A covers the lower half of xmm B   (isAPartiallyOverlapB is 3)
//!if A is gp:  A covers the lower half of xmm B, (isAPartiallyOverlapB is 5)
//!             A covers the higher half of xmm B (isAPartiallyOverlapB is 6)
RegAccessType updateAccess1(RegAccessType A, OverlapCase isAPartiallyOverlapB) {
    if(A == REGACCESS_D || A == REGACCESS_DU || A == REGACCESS_UD) {
        if(isAPartiallyOverlapB == OVERLAP_ALIGN) return REGACCESS_D;
        if(isAPartiallyOverlapB == OVERLAP_B_IS_LOW_OF_A || isAPartiallyOverlapB == OVERLAP_B_IS_HIGH_OF_A)
            return REGACCESS_D;
        if(isAPartiallyOverlapB == OVERLAP_LOW_OF_A_IS_HIGH_OF_B || isAPartiallyOverlapB == OVERLAP_A_IS_LOW_OF_B)
            return REGACCESS_L;
        return REGACCESS_H;
    }
    if(A == REGACCESS_L || A == REGACCESS_LU || A == REGACCESS_UL) {
        if(isAPartiallyOverlapB == OVERLAP_ALIGN || isAPartiallyOverlapB == OVERLAP_A_IS_LOW_OF_B)
            return REGACCESS_L;
        if(isAPartiallyOverlapB == OVERLAP_B_IS_LOW_OF_A) return REGACCESS_D;
        if(isAPartiallyOverlapB == OVERLAP_B_IS_HIGH_OF_A || isAPartiallyOverlapB == OVERLAP_LOW_OF_A_IS_HIGH_OF_B)
            return REGACCESS_N;
        if(isAPartiallyOverlapB == OVERLAP_HIGH_OF_A_IS_LOW_OF_B || isAPartiallyOverlapB == OVERLAP_A_IS_HIGH_OF_B)
            return REGACCESS_H;
    }
    if(A == REGACCESS_H || A == REGACCESS_HU || A == REGACCESS_UH) {
        if(isAPartiallyOverlapB == OVERLAP_ALIGN || isAPartiallyOverlapB == OVERLAP_A_IS_HIGH_OF_B)
            return REGACCESS_H;
        if(isAPartiallyOverlapB == OVERLAP_B_IS_LOW_OF_A || isAPartiallyOverlapB == OVERLAP_HIGH_OF_A_IS_LOW_OF_B)
            return REGACCESS_N;
        if(isAPartiallyOverlapB == OVERLAP_B_IS_HIGH_OF_A) return REGACCESS_D;
        if(isAPartiallyOverlapB == OVERLAP_LOW_OF_A_IS_HIGH_OF_B || isAPartiallyOverlapB == OVERLAP_A_IS_LOW_OF_B)
            return REGACCESS_L;
    }
    return REGACCESS_N;
}
//! merge RegAccessType C1 with RegAccessType C2

//!C can be N,L,H,D
RegAccessType updateAccess2(RegAccessType C1, RegAccessType C2) {
    if(C1 == REGACCESS_D || C2 == REGACCESS_D) return REGACCESS_D;
    if(C1 == REGACCESS_N) return C2;
    if(C2 == REGACCESS_N) return C1;
    if(C1 == REGACCESS_L && C2 == REGACCESS_H) return REGACCESS_D;
    if(C1 == REGACCESS_H && C2 == REGACCESS_L) return REGACCESS_D;
    return C1;
}
//! merge RegAccessType C with RegAccessType B

//!C can be N,L,H,D
//!B can be U, D, UD, DU
RegAccessType updateAccess3(RegAccessType C, RegAccessType B) {
    if(B == REGACCESS_D || B == REGACCESS_DU) return B; //no exposed usage
    if(B == REGACCESS_U || B == REGACCESS_UD) {
        if(C == REGACCESS_N) return B;
        if(C == REGACCESS_L) return REGACCESS_LU;
        if(C == REGACCESS_H) return REGACCESS_HU;
        if(C == REGACCESS_D) return REGACCESS_DU;
    }
    return B;
}
//! merge RegAccessType A with RegAccessType B

//!argument isBPartiallyOverlapA can be any value between -1 and 2
//!0 means fully overlapping, 1 means B is the lower half, 2 means B is the higher half
RegAccessType mergeAccess2(RegAccessType A, RegAccessType B, OverlapCase isBPartiallyOverlapA) {
    if(A == REGACCESS_UD || A == REGACCESS_UL || A == REGACCESS_UH ||
       A == REGACCESS_DU || A == REGACCESS_LU || A == REGACCESS_HU) return A;
    if(A == REGACCESS_D) {
        if(B == REGACCESS_D) return REGACCESS_D;
        if(B == REGACCESS_U) return REGACCESS_DU;
        if(B == REGACCESS_UD) return REGACCESS_DU;
        if(B == REGACCESS_DU) return B;
    }
    if(A == REGACCESS_U) {
        if(B == REGACCESS_D && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_UL;
        if(B == REGACCESS_D && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_UH;
        if(B == REGACCESS_D && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_UD;
        if(B == REGACCESS_U) return A;
        if(B == REGACCESS_UD && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_UL;
        if(B == REGACCESS_UD && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_UH;
        if(B == REGACCESS_UD && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_UD;
        if(B == REGACCESS_DU && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_UL;
        if(B == REGACCESS_DU && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_UH;
        if(B == REGACCESS_DU && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_UD;
    }
    if(A == REGACCESS_L) {
        if(B == REGACCESS_D && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_L;
        if(B == REGACCESS_D && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_D;
        if(B == REGACCESS_D && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_D;
        if(B == REGACCESS_U) return REGACCESS_LU;
        if(B == REGACCESS_UD) return REGACCESS_LU;
        if(B == REGACCESS_DU && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_LU;
        if(B == REGACCESS_DU && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_DU;
        if(B == REGACCESS_DU && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_DU;
    }
    if(A == REGACCESS_H) {
        if(B == REGACCESS_D && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_D;
        if(B == REGACCESS_D && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_H;
        if(B == REGACCESS_D && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_D;
        if(B == REGACCESS_U) return REGACCESS_HU;
        if(B == REGACCESS_UD) return REGACCESS_HU;
        if(B == REGACCESS_DU && isBPartiallyOverlapA == OVERLAP_B_COVER_LOW_OF_A) return REGACCESS_DU;
        if(B == REGACCESS_DU && isBPartiallyOverlapA == OVERLAP_B_COVER_HIGH_OF_A) return REGACCESS_HU;
        if(B == REGACCESS_DU && (isBPartiallyOverlapA == OVERLAP_B_COVER_A)) return REGACCESS_DU;
    }
    return REGACCESS_N;
}

//!determines which part of a use is from a given definition

//!reachingDefLive tells us which part of the def is live at this point
//!isDefPartiallyOverlapUse can be any value between -1 and 2
RegAccessType setAccessTypeOfUse(OverlapCase isDefPartiallyOverlapUse, RegAccessType reachingDefLive) {
    if(isDefPartiallyOverlapUse == OVERLAP_B_COVER_A)
        return reachingDefLive;
    if(isDefPartiallyOverlapUse == OVERLAP_B_COVER_LOW_OF_A) { //def covers the low half of use
        return REGACCESS_L;
    }
    if(isDefPartiallyOverlapUse == OVERLAP_B_COVER_HIGH_OF_A) {
        return REGACCESS_H;
    }
    return REGACCESS_N;
}

//! search currentBB->defUseTable to find a def for regNum at offsetPC

//!
DefUsePair* searchDefUseTable(int offsetPC, int regNum, LowOpndRegType pType) {
    DefUsePair* ptr = currentBB->defUseTable;
    while(ptr != NULL) {
        if(ptr->def.offsetPC == offsetPC &&
           ptr->def.regNum == regNum &&
           ptr->def.physicalType == pType) {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}
void printDefUseTable() {
    ALOGI("PRINT defUseTable --------");
    DefUsePair* ptr = currentBB->defUseTable;
    while(ptr != NULL) {
        ALOGI("  def @ %x of VR %d %d has %d uses", ptr->def.offsetPC,
              ptr->def.regNum, ptr->def.physicalType,
              ptr->num_uses);
        DefOrUseLink* ptr2 = ptr->uses;
        while(ptr2 != NULL) {
            ALOGI("    use @ %x of VR %d %d accessType %d", ptr2->offsetPC,
                  ptr2->regNum,
                  ptr2->physicalType,
                  ptr2->accessType);
            ptr2 = ptr2->next;
        }
        ptr = ptr->next;
    }
}

/**
 * @brief Update a VR use
 * @param reg the register
 * @param pType the type we want for the register
 * @param regAll
 */
void updateVRAtUse(int reg, LowOpndRegType pType, int regAll) {

    //Get a local size of xferPoints' size
    unsigned int max = currentBB->xferPoints.size ();

    //Go through each element
    for(unsigned int k = 0; k < max; k++) {

        //If the xferPoint matches and says we want memory to xmm
        if(currentBB->xferPoints[k].offsetPC == offsetPC &&
           currentBB->xferPoints[k].xtype == XFER_MEM_TO_XMM &&
           currentBB->xferPoints[k].regNum == reg &&
           currentBB->xferPoints[k].physicalType == pType) {
#ifdef DEBUG_XFER_POINTS
            ALOGI("XFER from memory to xmm %d", reg);
#endif
            // If we get to this point, it is possible that we believe we need
            // to load the wide VR from memory, but in reality this VR might
            // already be in a physical register.

            // TODO Figure out why this transfer point is inserted even when we already
            // have VR in xmm.

            int xmmVRType = static_cast<int>(LowOpndRegType_virtual)
                    | static_cast<int>(LowOpndRegType_xmm);
            int ssVRType = static_cast<int>(LowOpndRegType_virtual)
                    | static_cast<int>(LowOpndRegType_ss);
            bool loadFromMemory = true;

            // Look in compile table for this VR
            int entry = searchCompileTable(xmmVRType, reg);

            if (entry == -1) {
                // Single FP VRs can also use xmm, so try looking for this
                // as well if we haven't already found an entry
                entry = searchCompileTable(ssVRType, reg);
            }

            if (entry != -1) {
                // If we found an entry, check whether its physical register
                // is not null. If we have a physical register, we shouldn't be
                // loading from memory
                if (compileTable[entry].physicalReg != PhysicalReg_Null)
                    loadFromMemory = false;
            }

            // Load from memory into the physical register
            if (loadFromMemory) {
                const int vrOffset = getVirtualRegOffsetRelativeToFP (currentBB->xferPoints[k].regNum);
                move_mem_to_reg_noalloc(OpndSize_64,
                        vrOffset, PhysicalReg_FP,
                        true, MemoryAccess_VR, currentBB->xferPoints[k].regNum,
                        regAll, true);
            }
        }
    }
}

/////////////////////////////////////////////////////////////
//!search memVRTable for a given virtual register

/**
 * @brief Search in the memory table for a register
 * @param regNum the register we are looking for
 * @return the index for the register, -1 if not found
 */
int searchMemTable(int regNum) {
    int k;
    for(k = 0; k < num_memory_vr; k++) {
        if(memVRTable[k].regNum == regNum) {
            return k;
        }
    }
    ALOGI("JIT_INFO: Can't find VR %d num_memory_vr %d at searchMemTable", regNum, num_memory_vr);
    return -1;
}
/////////////////////////////////////////////////////////////////////////
// A VR is already in memory && NULL CHECK
//!check whether the latest content of a VR is in memory

//!
bool isInMemory(int regNum, OpndSize size) {
    int indexL = searchMemTable(regNum);
    int indexH = -1;
    if(size == OpndSize_64) indexH = searchMemTable(regNum+1);
    if(indexL < 0) return false;
    if(size == OpndSize_64 && indexH < 0) return false;
    if(!memVRTable[indexL].inMemory) return false;
    if(size == OpndSize_64 && (!memVRTable[indexH].inMemory)) return false;
    return true;
}

/**
 * @brief Used to update the in memory state of a virtual register
 * @param vR The virtual register
 * @param size The size of the virtual register
 * @param inMemory The new in memory state of the virtual register
 */
void setVRMemoryState (int vR, OpndSize size, bool inMemory)
{
    //Look for the index in the mem table for the virtual register
    int indexL = searchMemTable (vR);

    //If virtual register is wide, we want to find the index for the high part as well
    int indexH = -1;
    if (size == OpndSize_64)
    {
        indexH = searchMemTable (vR + 1);
    }

    if (indexL < 0)
    {
        ALOGI ("JIT_INFO: VR %d not in memVRTable at setVRToMemory", vR);
        SET_JIT_ERROR (kJitErrorRegAllocFailed);
        return;
    }

    //Update the in memory state of the VR
    memVRTable[indexL].setInMemoryState (inMemory);

    DEBUG_MEMORYVR (ALOGD ("Setting state of v%d %sin memory", memVRTable[indexL].vR,
            (memVRTable[indexL].inMemory ? "" : "NOT ")));

    if (size == OpndSize_64)
    {
        if (indexH < 0)
        {
            ALOGI ("JIT_INFO: VR %d not in memVRTable at setVRToMemory for upper 64-bits", vR+1);
            SET_JIT_ERROR (kJitErrorRegAllocFailed);
            return;
        }

        //Update the in memory state of the upper 64-bits of the VR
        memVRTable[indexH].setInMemoryState (inMemory);

        DEBUG_MEMORYVR (ALOGD ("Setting state of v%d %sin memory", memVRTable[indexH].vR,
                (memVRTable[indexH].inMemory ? "" : "NOT ")));
    }
}

//! check whether null check for a VR is performed previously

//!
bool isVRNullCheck(int regNum, OpndSize size) {
    if(size != OpndSize_32) {
        ALOGI("JIT_INFO: isVRNullCheck size is not 32 for register %d", regNum);
        SET_JIT_ERROR(kJitErrorNullBoundCheckFailed);
        return false;
    }
    int indexL = searchMemTable(regNum);
    if(indexL < 0) {
        ALOGI("JIT_INFO: VR %d not in memVRTable at isVRNullCheck", regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    return memVRTable[indexL].nullCheckDone;
}
bool isVRBoundCheck(int vr_array, int vr_index) {
    int indexL = searchMemTable(vr_array);
    if(indexL < 0) {
        ALOGI("JIT_INFO: VR %d not in memVRTable at isVRBoundCheck", vr_array);
        SET_JIT_ERROR(kJitErrorNullBoundCheckFailed);
        return false;
    }
    if(memVRTable[indexL].boundCheck.indexVR == vr_index)
        return memVRTable[indexL].boundCheck.checkDone;
    return false;
}
//! \brief set nullCheckDone in memVRTable to true
//!
//! \param regNum the register number
//! \param size the register size
//!
//! \return -1 if error happened, 0 otherwise
int setVRNullCheck(int regNum, OpndSize size) {
    if(size != OpndSize_32) {
        ALOGI("JIT_INFO: setVRNullCheck size should be 32\n");
        SET_JIT_ERROR(kJitErrorNullBoundCheckFailed);
        return -1;
    }
    int indexL = searchMemTable(regNum);
    if(indexL < 0) {
        ALOGI("JIT_INFO: VR %d not in memVRTable at setVRNullCheck", regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
    memVRTable[indexL].nullCheckDone = true;
    return 0;
}
void setVRBoundCheck(int vr_array, int vr_index) {
    int indexL = searchMemTable(vr_array);
    if(indexL < 0) {
        ALOGI("JIT_INFO: VR %d not in memVRTable at setVRBoundCheck", vr_array);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
    memVRTable[indexL].boundCheck.indexVR = vr_index;
    memVRTable[indexL].boundCheck.checkDone = true;
}
void clearVRBoundCheck(int regNum, OpndSize size) {
    int k;
    for(k = 0; k < num_memory_vr; k++) {
        if(memVRTable[k].regNum == regNum ||
           (size == OpndSize_64 && memVRTable[k].regNum == regNum+1)) {
            memVRTable[k].boundCheck.checkDone = false;
        }
        if(memVRTable[k].boundCheck.indexVR == regNum ||
           (size == OpndSize_64 && memVRTable[k].boundCheck.indexVR == regNum+1)) {
            memVRTable[k].boundCheck.checkDone = false;
        }
    }
}
//! set inMemory of memVRTable to false

//!
void clearVRToMemory(int regNum, OpndSize size) {
    int indexL = searchMemTable(regNum);
    int indexH = -1;
    if(size == OpndSize_64) indexH = searchMemTable(regNum+1);
    if(indexL >= 0) {
        memVRTable[indexL].inMemory = false;

        DEBUG_MEMORYVR(ALOGD("Setting state of v%d %sin memory",
                memVRTable[indexL].regNum,
                (memVRTable[indexL].inMemory ? "" : "NOT ")));
    }
    if(size == OpndSize_64 && indexH >= 0) {
        memVRTable[indexH].inMemory = false;

        DEBUG_MEMORYVR(ALOGD("Setting state of v%d %sin memory",
                memVRTable[indexH].regNum,
                (memVRTable[indexH].inMemory ? "" : "NOT ")));
    }
}
//! set nullCheckDone of memVRTable to false

//!
void clearVRNullCheck(int regNum, OpndSize size) {
    int indexL = searchMemTable(regNum);
    int indexH = -1;
    if(size == OpndSize_64) indexH = searchMemTable(regNum+1);
    if(indexL >= 0) {
        memVRTable[indexL].nullCheckDone = false;
    }
    if(size == OpndSize_64 && indexH >= 0) {
        memVRTable[indexH].nullCheckDone = false;
    }
}

//! Extend life for all VRs

//! Affects only VRs, stored in physical reg on last bytecode of their live range
//! @see VRFreeDelayCounters
//! @param reason explains what freeing delay request should be canceled.
//! A single VRFreeDelayCounters index should be used.
//! @return true if at least one VR changed it's state
bool requestVRFreeDelayAll(u4 reason) {
    bool state_changed = false;
    // Delay only VRs, which could be freed by freeReg
    for(int k = 0; k < compileTable.size (); k++) {

        if(compileTable[k].physicalReg != PhysicalReg_Null) {

            if(isVirtualReg(compileTable[k].physicalType) == true) {
                bool freeCrit = isLastByteCodeOfLiveRange(k);

                if(freeCrit == true) {
                    int res = requestVRFreeDelay(compileTable[k].regNum, reason);
                    if(res >= 0) {
                        state_changed = true;
                    }
                }
            }
        }
    }
#ifdef DEBUG_REGALLOC
    if(state_changed) {
        ALOGI("requestVRFreeDelayAll: state_changed=%i", state_changed);
    }
#endif
    return state_changed;
}

//! Cancel request for all VR life extension

//! Affects only VRs, stored in physical reg on last bytecode of theirs live range
//! @see VRFreeDelayCounters
//! @param reason explains what freeing delay request should be canceled.
//! A single VRFreeDelayCounters index should be used.
//! @return true if at least one VR changed it's state
bool cancelVRFreeDelayRequestAll(u4 reason) {
    bool state_changed = false;
    // Cancel delay for VRs only
    for(int k = 0; k < compileTable.size (); k++) {
        if(isVirtualReg(compileTable[k].physicalType) == true) {
            bool freeCrit = isLastByteCodeOfLiveRange(k);

            if(freeCrit == true) {
                int res = cancelVRFreeDelayRequest(compileTable[k].regNum, reason);
                if(res >= 0) {
                    state_changed = true;
                }
            }
        }
    }
#ifdef DEBUG_REGALLOC
    if(state_changed) {
        ALOGI("cancelVRFreeDelayRequestAll: state_changed=%i", state_changed);
    }
#endif
    return state_changed;
}

//! Extend Virtual Register life

//! Requests that the life of a specific virtual register be extended. This ensures
//! that its mapping to a physical register won't be canceled while the extension
//! request is valid. NOTE: This does not support 64-bit values (when two adjacent
//! VRs are used)
//! @see cancelVRFreeDelayRequest
//! @see getVRFreeDelayRequested
//! @see VRFreeDelayCounters
//! @param regNum is the VR number
//! @param reason explains why freeing must be delayed.
//! A single VRFreeDelayCounters index should be used.
//! @return negative value if request failed
int requestVRFreeDelay(int regNum, u4 reason) {
    // TODO Add 64-bit operand support when needed
    int indexL = searchMemTable(regNum);
    if(indexL >= 0) {
        if(reason < VRDELAY_COUNT) {
#ifdef DEBUG_REGALLOC
            ALOGI("requestFreeDelay: reason=%i VR=%d count=%i", reason, regNum, memVRTable[indexL].delayFreeCounters[reason]);
#endif
            memVRTable[indexL].delayFreeCounters[reason]++;
        } else {
            ALOGI("JIT_INFO: At requestVRFreeDelay: reason %i is unknown (VR=%d)", reason, regNum);
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return -1;
        }
    } else {
        ALOGI("JIT_INFO: At requestVRFreeDelay: VR %d not in memVRTable", regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }

    return indexL;
}

//! Cancel request for virtual register life extension

//! Cancels any outstanding requests to extended liveness of VR. Additionally,
//! this ensures that if the VR is no longer life after this point, it will
//! no longer be associated with a physical register which can then be reused.
//! NOTE: This does not support 64-bit values (when two adjacent VRs are used)
//! @see requestVRFreeDelay
//! @see getVRFreeDelayRequested
//! @see VRFreeDelayCounters
//! @param regNum is the VR number
//! @param reason explains what freeing delay request should be canceled.
//! A single VRFreeDelayCounters index should be used.
//! @return negative value if request failed
int cancelVRFreeDelayRequest(int regNum, u4 reason) {
    //TODO Add 64-bit operand support when needed
    bool needCallToFreeReg = false;
    int indexL = searchMemTable(regNum);
    if(indexL >= 0) {
        if(reason < VRDELAY_COUNT) { // don't cancel delay if it wasn't requested
            if(memVRTable[indexL].delayFreeCounters[reason] > 0) {
#ifdef DEBUG_REGALLOC
                ALOGI("cancelVRFreeDelay: reason=%i VR=%d count=%i", reason, regNum, memVRTable[indexL].delayFreeCounters[reason]);
#endif
                memVRTable[indexL].delayFreeCounters[reason]--; // only cancel this particular reason, not all others

                // freeReg might want to free this VR now if there is no longer a valid delay
                needCallToFreeReg = !getVRFreeDelayRequested(regNum);
            } else {
                return -1;
            }
        } else {
            ALOGI("JIT_INFO: At cancelVRFreeDelay: reason %i is unknown (VR: %d)", reason, regNum);
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return -1;
        }
    }
    if(needCallToFreeReg)
        freeReg(false);

    return indexL;
}

//! Gets status of virtual register free delay request

//! Finds out if there was a delay request for freeing this VR.
//! NOTE: This does not support 64-bit values (when two adjacent VRs are used)
//! @see requestVRFreeDelay
//! @see cancelVRFreeDelayRequest
//! @param regNum is the VR number
//! @return true if VR has an active delay request
bool getVRFreeDelayRequested(int regNum) {
    //TODO Add 64-bit operand support when needed
    int indexL = searchMemTable(regNum);
    if(indexL >= 0) {
        for(int c=0; c<VRDELAY_COUNT; c++) {
            if(memVRTable[indexL].delayFreeCounters[c] != 0) {
                return true;
            }
        }
        return false;
    }
    return false;
}

int current_bc_size = -1;

/**
 * @brief Search the basic block information for a register number and type
 * @param type the register type we are looking for (masked with MASK_FOR_TYPE)
 * @param regNum the register number we are looking for
 * @param bb the BasicBlock
 * @return true if found
 */
bool isUsedInBB(int regNum, int type, BasicBlock_O1* bb) {
    unsigned int k, max;

    //Get a local version of the infoBasicBlock's size
    max = bb->infoBasicBlock.size ();

    //Go through each element, if it matches, return true
    for(k = 0; k < max; k++) {
        if(bb->infoBasicBlock[k].physicalType == (type & MASK_FOR_TYPE) && bb->infoBasicBlock[k].regNum == regNum) {
            return true;
        }
    }

    //Report failure
    return false;
}

/**
 * @brief Search the basic block information for a register number and type
 * @param type the register type we are looking for
 * @param regNum the register number we are looking for
 * @param bb the BasicBlock
 * @return the index in the infoBasicBlock table, -1 if not found
 */
int searchVirtualInfoOfBB(LowOpndRegType type, int regNum, BasicBlock_O1* bb) {
    unsigned int k, max;

    //Get a local version of the infoBasicBlock's size
    max = bb->infoBasicBlock.size ();

    //Go through each element, if it matches, return the index
    for(k = 0; k < max; k++) {
        if(bb->infoBasicBlock[k].physicalType == type && bb->infoBasicBlock[k].regNum == regNum) {
            return k;
        }
    }

    //Not found is not always an error, so don't set any
    return -1;
}
//! return the index to compileTable for a given virtual register

//! return -1 if not found
int searchCompileTable(int type, int regNum) { //returns the index
    int k;
    for(k = 0; k < compileTable.size (); k++) {
        if(compileTable[k].physicalType == type && compileTable[k].regNum == regNum)
            return k;
    }

    //Returning -1 might not always be an error, so we don't set any
    return -1;
}

/**
 * @brief Update the compileTable entry with the new register
 * @param vR what virtual register are we interested in
 * @param oldReg the old register that used to be used
 * @param newReg the new register that is now used
 * @return whether the update was successful
 */
bool updatePhysicalRegForVR(int vR, PhysicalReg oldReg, PhysicalReg newReg) {
    //Go through the entries in the compile table
    for (int entry = 0; entry < compileTable.size (); entry++) {

        //If it is a virtual register, the vR we are looking for, and is associated to the old register
        if (isVirtualReg(compileTable[entry].physicalType)
                && compileTable[entry].regNum == vR
                && compileTable[entry].physicalReg == oldReg) {

            //Update it and report success
            compileTable[entry].setPhysicalReg (newReg);

            return true;
        }
    }

    //We did not find it
    return false;
}

//!check whether a physical register for a variable with typeA will work for another variable with typeB

//!Type LowOpndRegType_ss is compatible with type LowOpndRegType_xmm
bool matchType(int typeA, int typeB) {
    if((typeA & MASK_FOR_TYPE) == (typeB & MASK_FOR_TYPE)) return true;
    if((typeA & MASK_FOR_TYPE) == LowOpndRegType_ss &&
       (typeB & MASK_FOR_TYPE) == LowOpndRegType_xmm) return true;
    if((typeA & MASK_FOR_TYPE) == LowOpndRegType_xmm &&
       (typeB & MASK_FOR_TYPE) == LowOpndRegType_ss) return true;
    return false;
}

//! obsolete
bool defineFirst(int atype) {
    if(atype == REGACCESS_D || atype == REGACCESS_L || atype == REGACCESS_H || atype == REGACCESS_DU)
        return true;
    return false;
}
//!check whether a virtual register is updated in a basic block

//!
bool notUpdated(RegAccessType atype) {
    if(atype == REGACCESS_U) return true;
    return false;
}
//!check whether a virtual register has exposed usage within a given basic block

//!
bool hasExposedUsage2(BasicBlock_O1* bb, int index) {
    RegAccessType atype = bb->infoBasicBlock[index].accessType;
    if(atype == REGACCESS_D || atype == REGACCESS_L || atype == REGACCESS_H || atype == REGACCESS_DU)
        return false;
    return true;
}
//! return the spill location that is not used

//!
int getSpillIndex (OpndSize size) {
    int k;
    for(k = 1; k <= MAX_SPILL_JIT_IA - 1; k++) {
        if(size == OpndSize_64) {
            if(k < MAX_SPILL_JIT_IA - 1 && spillIndexUsed[k] == 0 && spillIndexUsed[k+1] == 0)
                return k;
        }
        else if(spillIndexUsed[k] == 0) {
            return k;
        }
    }
    ALOGI("JIT_INFO: Cannot find spill position in spillLogicalReg\n");
    SET_JIT_ERROR(kJitErrorRegAllocFailed);
    return -1;
}

//!this is called before generating a native code, it resets the spill information
//!startNativeCode must be paired with endNativeCode
void startNativeCode(int vr_num, int vr_type) {
    //Reset the spilling information
    gCompilationUnit->resetCanSpillRegisters ();

    //Set the inGetVR_num and type now
    inGetVR_num = vr_num;
    inGetVR_type = vr_type;
}

//! called right after generating a native code
//!It resets the spill information and resets inGetVR_num to -1
void endNativeCode(void) {
    //Reset the spilling information
    gCompilationUnit->resetCanSpillRegisters ();

    //Reset the inGetVR_num now
    inGetVR_num = -1;
}

//! touch hardcoded register %ecx and reduce its reference count

//!
int touchEcx() {
    //registerAlloc will spill logical reg that is mapped to ecx
    //registerAlloc will reduce refCount
    registerAlloc(LowOpndRegType_gp, PhysicalReg_ECX, true, true);
    return 0;
}
//! touch hardcoded register %eax and reduce its reference count

//!
int touchEax() {
    registerAlloc(LowOpndRegType_gp, PhysicalReg_EAX, true, true);
    return 0;
}
int touchEsi() {
    registerAlloc(LowOpndRegType_gp, PhysicalReg_ESI, true, true);
    return 0;
}
int touchXmm1() {
    registerAlloc(LowOpndRegType_xmm, XMM_1, true, true);
    return 0;
}
int touchEbx() {
    registerAlloc(LowOpndRegType_gp, PhysicalReg_EBX, true, true);
    return 0;
}

//! touch hardcoded register %edx and reduce its reference count

//!
int touchEdx() {
    registerAlloc(LowOpndRegType_gp, PhysicalReg_EDX, true, true);
    return 0;
}

//! this function is called before calling a helper function or a vm function
int beforeCall(const char* target) { //spill all live registers
    if(currentBB == NULL) return -1;

    /* special case for ncgGetEIP: this function only updates %edx */
    if(!strcmp(target, "ncgGetEIP")) {
        touchEdx();
        return -1;
    }

    /* these functions use %eax for the return value */
    if((!strcmp(target, "dvmInstanceofNonTrivial")) ||
       (!strcmp(target, "dvmUnlockObject")) ||
       (!strcmp(target, "dvmAllocObject")) ||
       (!strcmp(target, "dvmAllocArrayByClass")) ||
       (!strcmp(target, "dvmAllocPrimitiveArray")) ||
       (!strcmp(target, "dvmInterpHandleFillArrayData")) ||
       (!strcmp(target, "dvmFindInterfaceMethodInCache")) ||
       (!strcmp(target, "dvmNcgHandlePackedSwitch")) ||
       (!strcmp(target, "dvmNcgHandleSparseSwitch")) ||
       (!strcmp(target, "dvmCanPutArrayElement")) ||
       (!strcmp(target, "moddi3")) || (!strcmp(target, "divdi3")) ||
       (!strcmp(target, "execute_inline"))
       || (!strcmp(target, "dvmJitToPatchPredictedChain"))
       || (!strcmp(target, "dvmJitHandlePackedSwitch"))
       || (!strcmp(target, "dvmJitHandleSparseSwitch"))
#if defined(WITH_SELF_VERIFICATION)
       || (!strcmp(target, "selfVerificationLoad"))
#endif
       ) {
        touchEax();
    }

    //these two functions also use %edx for the return value
    if((!strcmp(target, "moddi3")) || (!strcmp(target, "divdi3"))) {
        touchEdx();
    }
    if((!strcmp(target, ".new_instance_helper"))) {
        touchEsi(); touchEax();
    }
#if defined(ENABLE_TRACING)
    if((!strcmp(target, "common_periodicChecks4"))) {
        touchEdx();
    }
#endif
    if((!strcmp(target, ".const_string_helper"))) {
        touchEcx(); touchEax();
    }
    if((!strcmp(target, ".check_cast_helper"))) {
        touchEbx(); touchEsi();
    }
    if((!strcmp(target, ".instance_of_helper"))) {
        touchEbx(); touchEsi(); touchEcx();
    }
    if((!strcmp(target, ".monitor_enter_helper"))) {
        touchEbx();
    }
    if((!strcmp(target, ".monitor_exit_helper"))) {
        touchEbx();
    }
    if((!strcmp(target, ".aget_wide_helper"))) {
        touchEbx(); touchEcx(); touchXmm1();
    }
    if((!strcmp(target, ".aget_helper")) || (!strcmp(target, ".aget_char_helper")) ||
       (!strcmp(target, ".aget_short_helper")) || (!strcmp(target, ".aget_bool_helper")) ||
       (!strcmp(target, ".aget_byte_helper"))) {
        touchEbx(); touchEcx(); touchEdx();
    }
    if((!strcmp(target, ".aput_helper")) || (!strcmp(target, ".aput_char_helper")) ||
       (!strcmp(target, ".aput_short_helper")) || (!strcmp(target, ".aput_bool_helper")) ||
       (!strcmp(target, ".aput_byte_helper")) || (!strcmp(target, ".aput_wide_helper"))) {
        touchEbx(); touchEcx(); touchEdx();
    }
    if((!strcmp(target, ".sput_helper")) || (!strcmp(target, ".sput_wide_helper"))) {
        touchEdx(); touchEax();
    }
    if((!strcmp(target, ".sget_helper"))) {
        touchEdx(); touchEcx();
    }
    if((!strcmp(target, ".sget_wide_helper"))) {
        touchEdx(); touchXmm1();
    }
    if((!strcmp(target, ".aput_obj_helper"))) {
        touchEdx(); touchEcx(); touchEax();
    }
    if((!strcmp(target, ".iput_helper")) || (!strcmp(target, ".iput_wide_helper"))) {
        touchEbx(); touchEcx(); touchEsi();
    }
    if((!strcmp(target, ".iget_helper"))) {
        touchEbx(); touchEcx(); touchEdx();
    }
    if((!strcmp(target, ".iget_wide_helper"))) {
        touchEbx(); touchEcx(); touchXmm1();
    }
    if((!strcmp(target, ".new_array_helper"))) {
        touchEbx(); touchEdx(); touchEax();
    }
    if((!strcmp(target, ".invoke_virtual_helper"))) {
        touchEbx(); touchEcx();
    }
    if((!strcmp(target, ".invoke_direct_helper"))) {
        touchEsi(); touchEcx();
    }
    if((!strcmp(target, ".invoke_super_helper"))) {
        touchEbx(); touchEcx();
    }
    if((!strcmp(target, ".invoke_interface_helper"))) {
        touchEbx(); touchEcx();
    }
    if((!strcmp(target, ".invokeMethodNoRange_5_helper")) ||
       (!strcmp(target, ".invokeMethodNoRange_4_helper"))) {
        touchEbx(); touchEsi(); touchEax(); touchEdx();
    }
    if((!strcmp(target, ".invokeMethodNoRange_3_helper"))) {
        touchEbx(); touchEsi(); touchEax();
    }
    if((!strcmp(target, ".invokeMethodNoRange_2_helper"))) {
        touchEbx(); touchEsi();
    }
    if((!strcmp(target, ".invokeMethodNoRange_1_helper"))) {
        touchEbx();
    }
    if((!strcmp(target, ".invokeMethodRange_helper"))) {
        touchEdx(); touchEsi();
    }
#ifdef DEBUG_REGALLOC
    ALOGI("enter beforeCall");
#endif

    freeReg(true); //to avoid spilling dead logical registers
    int k;
    for(k = 0; k < compileTable.size (); k++) {
        if(compileTable[k].physicalReg != PhysicalReg_Null &&
           (compileTable[k].physicalType & LowOpndRegType_hard) == 0) {
            /* handles non hardcoded variables that are in physical registers */
            if(!strcmp(target, "exception")) {
                /* before throwing an exception
                   update contents of all VRs in Java stack */
                if(!isVirtualReg(compileTable[k].physicalType)) continue;
                /* to have correct GC, we should update contents for L VRs as well */
                //if(compileTable[k].gType == GLOBALTYPE_L) continue;
            }
            if((!strcmp(target, ".const_string_resolve")) ||
               (!strcmp(target, ".static_field_resolve")) ||
               (!strcmp(target, ".inst_field_resolve")) ||
               (!strcmp(target, ".class_resolve")) ||
               (!strcmp(target, ".direct_method_resolve")) ||
               (!strcmp(target, ".virtual_method_resolve")) ||
               (!strcmp(target, ".static_method_resolve"))) {
               /* physical register %ebx will keep its content
                  but to have correct GC, we should dump content of a VR
                     that is mapped to %ebx */
                if(compileTable[k].physicalReg == PhysicalReg_EBX &&
                   (!isVirtualReg(compileTable[k].physicalType)))
                    continue;
            }
            if((!strncmp(target, "dvm", 3)) || (!strcmp(target, "moddi3")) ||
               (!strcmp(target, "divdi3")) ||
               (!strcmp(target, "fmod")) || (!strcmp(target, "fmodf"))) {
                /* callee-saved registers (%ebx, %esi, %ebp, %edi) will keep the content
                   but to have correct GC, we should dump content of a VR
                      that is mapped to a callee-saved register */
                if((compileTable[k].physicalReg == PhysicalReg_EBX ||
                    compileTable[k].physicalReg == PhysicalReg_ESI) &&
                   (!isVirtualReg(compileTable[k].physicalType)))
                    continue;
            }

            if(strncmp(target, "dvmUnlockObject", 15) == 0) {
               continue;
            }

#ifdef DEBUG_REGALLOC
            ALOGI("SPILL logical register %d %d in beforeCall",
                  compileTable[k].regNum, compileTable[k].physicalType);
#endif
            spillLogicalReg(k, true);
        }
    }

    cancelVRFreeDelayRequestAll(VRDELAY_CAN_THROW);

#ifdef DEBUG_REGALLOC
    ALOGI("exit beforeCall");
#endif
    return 0;
}
int getFreeReg(int type, int reg, int indexToCompileTable);
//! after calling a helper function or a VM function

//!
int afterCall(const char* target) { //un-spill
    if(currentBB == NULL) return -1;
    if(!strcmp(target, "ncgGetEIP")) return -1;

    return 0;
}
//! check whether a temporary is 8-bit

//!
bool isTemp8Bit(int type, int reg) {
    if(currentBB == NULL) return false;
    if(!isTemporary(type, reg)) return false;
    int k;
    for(k = 0; k < num_temp_regs_per_bytecode; k++) {
        if(infoByteCodeTemp[k].physicalType == type &&
           infoByteCodeTemp[k].regNum == reg) {
            return infoByteCodeTemp[k].is8Bit;
        }
    }
    ALOGI("JIT_INFO: Could not find reg %d type %d at isTemp8Bit", reg, type);
    SET_JIT_ERROR(kJitErrorRegAllocFailed);
    return false;
}

/* functions to access live ranges of a VR
   Live range info is stored in memVRTable[].ranges, which is a linked list
*/
//! check whether a VR is live at the current bytecode

//!
bool isVRLive(int vA) {
    int index = searchMemTable(vA);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find VR %d in memTable at isVRLive", vA);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    LiveRange* ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC >= ptr->start && offsetPC <= ptr->end) return true;
        ptr = ptr->next;
    }
    return false;
}

//! check whether the current bytecode is the last access to a VR within a live range

//!for 64-bit VR, return true only when true for both low half and high half
bool isLastByteCodeOfLiveRange(int compileIndex) {
    int k = compileIndex;
    OpndSize tSize = getRegSize(compileTable[k].physicalType);
    int index;
    LiveRange* ptr = NULL;
    if(tSize == OpndSize_32) {
        /* check live ranges for the VR */
        index = searchMemTable(compileTable[k].regNum);
        if(index < 0) {
            ALOGI("JIT_INFO: Could not find 32-bit VR %d in memTable at isLastByteCodeOfLiveRange", compileTable[k].regNum);
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return false;
        }
        ptr = memVRTable[index].ranges;
        while(ptr != NULL) {
            if(offsetPC == ptr->end) return true;
            ptr = ptr->next;
        }
        return false;
    }
    /* size of the VR is 64 */
    /* check live ranges of the low half */
    index = searchMemTable(compileTable[k].regNum);
    bool tmpB = false;
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d (lower 32) in memTable at isLastByteCodeOfLiveRange", compileTable[k].regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC == ptr->end) {
            tmpB = true;
            break;
        }
        ptr = ptr->next;
    }
    if(!tmpB) return false;
    /* check live ranges of the high half */
    index = searchMemTable(compileTable[k].regNum+1);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d (upper 32) in memTable at isLastByteCodeOfLiveRange", compileTable[k].regNum+1);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC == ptr->end) {
            return true;
        }
        ptr = ptr->next;
    }
    return false;
}

// check if virtual register has loop independent dependence
bool loopIndepUse(int compileIndex) {
    int k = compileIndex;
    OpndSize tSize = getRegSize(compileTable[k].physicalType);
    int index;
    bool retCode = false;

    /* check live ranges of the low half */
    index = searchMemTable(compileTable[k].regNum);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 32-bit VR %d in memTable at loopIndepUse", compileTable[k].regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    LiveRange* ptr = memVRTable[index].ranges;
    if (ptr != NULL && ptr->start > 0)
        retCode = true;
    if(!retCode) return false;
    if(tSize == OpndSize_32) return true;

    /* check for the high half */
    index = searchMemTable(compileTable[k].regNum+1);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d in memTable at loopIndepUse", compileTable[k].regNum+1);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    ptr = memVRTable[index].ranges;
    if (ptr != NULL && ptr->start > 0)
       return true;
    return false;
}


//! check whether the current bytecode is in a live range that extends to end of a basic block

//!for 64 bit, return true if true for both low half and high half
bool reachEndOfBB(int compileIndex) {
    int k = compileIndex;
    OpndSize tSize = getRegSize(compileTable[k].physicalType);
    int index;
    bool retCode = false;
    /* check live ranges of the low half */
    index = searchMemTable(compileTable[k].regNum);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 32-bit VR %d in memTable at reachEndOfBB", compileTable[k].regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    LiveRange* ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC >= ptr->start &&
           offsetPC <= ptr->end) {
            if(ptr->end == currentBB->pc_end) {
                retCode = true;
            }
            break;
        }
        ptr = ptr->next;
    }
    if(!retCode) return false;
    if(tSize == OpndSize_32) return true;
    /* check live ranges of the high half */
    index = searchMemTable(compileTable[k].regNum+1);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d in memTable at reachEndOfBB", compileTable[k].regNum+1);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC >= ptr->start &&
           offsetPC <= ptr->end) {
            if(ptr->end == currentBB->pc_end) return true;
            return false;
        }
        ptr = ptr->next;
    }
#ifdef PRINT_WARNING
    ALOGW("offsetPC %d not in live range of VR %d", offsetPC, compileTable[k].regNum+1);
#endif
    return false;
}

//!check whether the current bytecode is the next to last access to a VR within a live range

//!for 64 bit, return true if true for both low half and high half
bool isNextToLastAccess(int compileIndex) {
    int k = compileIndex;
    OpndSize tSize = getRegSize(compileTable[k].physicalType);
    int index;
    /* check live ranges for the low half */
    bool retCode = false;
    index = searchMemTable(compileTable[k].regNum);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 32-bit VR %d in memTable at isNextToLastAccess", compileTable[k].regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    LiveRange* ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        int num_access = ptr->num_access;

        if(num_access < 2) {
           ptr = ptr->next;
           continue;
        }

        if(offsetPC == ptr->accessPC[num_access-2]) {
           retCode = true;
           break;
        }
        ptr = ptr->next;
    }
    if(!retCode) return false;
    if(tSize == OpndSize_32) return true;
    /* check live ranges for the high half */
    index = searchMemTable(compileTable[k].regNum+1);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d in memTable at isNextToLastAccess", compileTable[k].regNum+1);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return false;
    }
    ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        int num_access = ptr->num_access;

        if(num_access < 2) {
           ptr = ptr->next;
           continue;
        }

        if(offsetPC == ptr->accessPC[num_access-2]) return true;
        ptr = ptr->next;
    }
    return false;
}

/** return bytecode offset corresponding to offsetPC
*/
int convertOffsetPCtoBytecodeOffset(int offPC) {
    if(offPC == PC_FOR_START_OF_BB)
        return currentBB->pc_start;
    if(offPC == PC_FOR_END_OF_BB)
        return currentBB->pc_end;
    for(MIR * mir = currentBB->firstMIRInsn; mir; mir = mir->next) {
       if(mir->seqNum == offPC)
         return mir->offset;
    }
    return currentBB->pc_end;
}

/** return the start of the next live range
    if there does not exist a next live range, return pc_end of the basic block
    for 64 bits, return the larger one for low half and high half
    Assume live ranges are sorted in order
*/
int getNextLiveRange(int compileIndex) {
    int k = compileIndex;
    OpndSize tSize = getRegSize(compileTable[k].physicalType);
    /* check live ranges of the low half */
    int index;
    index = searchMemTable(compileTable[k].regNum);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 32-bit VR %d in memTable at getNextLiveRange", compileTable[k].regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return offsetPC;
    }
    bool found = false;
    int nextUse = offsetPC;
    LiveRange* ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(ptr->start > offsetPC) {
            nextUse = ptr->start;
            found = true;
            break;
        }
        ptr = ptr->next;
    }
    if(!found) return PC_FOR_END_OF_BB;
    if(tSize == OpndSize_32) return nextUse;

    /* check live ranges of the high half */
    found = false;
    index = searchMemTable(compileTable[k].regNum+1);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d in memTable at getNextLiveRange", compileTable[k].regNum+1);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return offsetPC;
    }
    int nextUse2 = offsetPC;
    ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(ptr->start > offsetPC) {
            nextUse2 = ptr->start;
            found = true;
            break;
        }
        ptr = ptr->next;
    }
    if(!found) return PC_FOR_END_OF_BB;
    /* return the larger one */
    return (nextUse2 > nextUse ? nextUse2 : nextUse);
}

/** return the next access to a variable
    If variable is 64-bit, get the next access to the lower half and the high half
        return the eariler one
*/
int getNextAccess(int compileIndex) {
    int k = compileIndex;
    OpndSize tSize = getRegSize(compileTable[k].physicalType);
    int index, k3;
    /* check live ranges of the low half */
    index = searchMemTable(compileTable[k].regNum);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 32-bit VR %d in memTable at getNextAccess", compileTable[k].regNum);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return offsetPC;
    }
    bool found = false;
    int nextUse = offsetPC;
    LiveRange* ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC >= ptr->start &&
           offsetPC <= ptr->end) {
            /* offsetPC belongs to this live range */
            for(k3 = 0; k3 < ptr->num_access; k3++) {
                if(ptr->accessPC[k3] > offsetPC) {
                    nextUse = ptr->accessPC[k3];
                    break;
                }
            }
            found = true;
            break;
        }
        ptr = ptr->next;
    }
    if(!found) {
#ifdef PRINT_WARNING
        ALOGW("offsetPC %d not in live range of VR %d", offsetPC, compileTable[k].regNum);
#endif
    }
    if(tSize == OpndSize_32) return nextUse;

    /* check live ranges of the high half */
    found = false;
    index = searchMemTable(compileTable[k].regNum+1);
    if(index < 0) {
        ALOGI("JIT_INFO: Could not find 64-bit VR %d in memTable at getNextAccess", compileTable[k].regNum+1);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return offsetPC;
    }
    int nextUse2 = offsetPC;
    ptr = memVRTable[index].ranges;
    while(ptr != NULL) {
        if(offsetPC >= ptr->start &&
           offsetPC <= ptr->end) {
            for(k3 = 0; k3 < ptr->num_access; k3++) {
                if(ptr->accessPC[k3] > offsetPC) {
                    nextUse2 = ptr->accessPC[k3];
                    break;
                }
            }
            found = true;
            break;
        }
        ptr = ptr->next;
    }
    if(!found) {
#ifdef PRINT_WARNING
        ALOGW("offsetPC %d not in live range of VR %d", offsetPC, compileTable[k].regNum+1);
#endif
    }
    /* return the earlier one */
    if(nextUse2 < nextUse) return nextUse2;
    return nextUse;
}

/**
 * @brief Free variables that are no longer in use.
 * @param writeBackAllVRs When true, writes back all dirty VRs including
 * constants
 * @return Returns value >= 0 on success.
*/
int freeReg(bool writeBackAllVRs) {
    //If the current BasicBlock is 0, we have nothing to do
    if(currentBB == NULL) {
        return 0;
    }

    //If writeBackAllVRs is true, we also spill the constants
    if (writeBackAllVRs == true) {
        for (int k = 0; k < num_const_vr; k++) {
            if (constVRTable[k].isConst) {
                writeBackConstVR(constVRTable[k].regNum, constVRTable[k].value);
            }
        }
    }

    for(int k = 0; k < compileTable.size (); k++) {
        if (writeBackAllVRs && isVirtualReg(compileTable[k].physicalType)
                && compileTable[k].inPhysicalRegister () == true) {
#ifdef DEBUG_REGALLOC
            ALOGI("FREE v%d with type %d allocated to %s",
                    compileTable[k].regNum, compileTable[k].physicalType,
                    physicalRegToString(static_cast<PhysicalReg>(
                            compileTable[k].physicalReg)));
#endif

            spillLogicalReg(k, true);
        }

        if(compileTable[k].refCount == 0 && compileTable[k].inPhysicalRegister () == true) {
            bool isTemp = (isVirtualReg(compileTable[k].physicalType) == false);

            if (isTemp) {
#ifdef DEBUG_REGALLOC
                ALOGI("FREE temporary %d with type %d allocated to %s",
                       compileTable[k].regNum, compileTable[k].physicalType,
                       physicalRegToString(static_cast<PhysicalReg>(
                               compileTable[k].physicalReg)));
#endif

                compileTable[k].setPhysicalReg (PhysicalReg_Null);

                if(compileTable[k].spill_loc_index >= 0) {
                    /* update spill info for temporaries */
                    spillIndexUsed[compileTable[k].spill_loc_index >> 2] = 0;
                    compileTable[k].spill_loc_index = -1;
                    ALOGI("JIT_INFO: free a temporary register with TRSTATE_SPILLED\n");
                    SET_JIT_ERROR(kJitErrorRegAllocFailed);
                    return -1;
                }
            }
        }
    }
    syncAllRegs(); //sync up allRegs (isUsed & freeTimeStamp) with compileTable
    return 0;
}

//! reduce the reference count by 1

//! input: index to compileTable
void decreaseRefCount(int index) {
#ifdef DEBUG_REFCOUNT
    ALOGI("REFCOUNT: %d in decreaseRefCount %d %d", compileTable[index].refCount,
            compileTable[index].regNum, compileTable[index].physicalType);
#endif
    compileTable[index].refCount--;
    if(compileTable[index].refCount < 0) {
        ALOGI("JIT_INFO: refCount is negative for REG %d %d at decreaseRefCount",
                compileTable[index].regNum, compileTable[index].physicalType);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return;
    }
}
//! reduce the reference count of a VR by 1

//! input: reg & type
int updateRefCount(int reg, LowOpndRegType type) {
    if(currentBB == NULL) return 0;
    int index = searchCompileTable(LowOpndRegType_virtual | type, reg);
    if(index < 0) {
        ALOGI("JIT_INFO: virtual reg %d type %d not found in updateRefCount\n", reg, type);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
    decreaseRefCount(index);
    return 0;
}
//! reduce the reference count of a variable by 1

//! The variable is named with lowering module's naming mechanism
int updateRefCount2(int reg, int type, bool isPhysical) {
    if(currentBB == NULL) return 0;
    int newType = convertType(type, reg, isPhysical);
    if(newType & LowOpndRegType_scratch) reg = reg - PhysicalReg_SCRATCH_1 + 1;
    int index = searchCompileTable(newType, reg);
    if(index < 0) {
        ALOGI("JIT_INFO: reg %d type %d not found in updateRefCount\n", reg, newType);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
    decreaseRefCount(index);
    return 0;
}

//! check whether a virtual register is in a physical register

//! If updateRefCount is 0, do not update reference count;
//!If updateRefCount is 1, update reference count only when VR is in a physical register
//!If updateRefCount is 2, update reference count
int checkVirtualReg(int reg, LowOpndRegType type, int updateRefCount) {
    if(currentBB == NULL) return PhysicalReg_Null;
    int index = searchCompileTable(LowOpndRegType_virtual | type, reg);
    if(index < 0) {
        ALOGI("JIT_INFO: virtual reg %d type %d not found in checkVirtualReg\n", reg, type);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return PhysicalReg_Null;
    }
    //reduce reference count
    if(compileTable[index].physicalReg != PhysicalReg_Null) {
        if(updateRefCount != 0) decreaseRefCount(index);
        return compileTable[index].physicalReg;
    }
    if(updateRefCount == 2) decreaseRefCount(index);
    return PhysicalReg_Null;
}
//!check whether a temporary can share the same physical register with a VR

//!This is called in get_virtual_reg
//!If this function returns false, new register will be allocated for this temporary
bool checkTempReg2(int reg, int type, bool isPhysical, int physicalRegForVR, int vB) {
    if (isPhysical) {
        // If temporary is already physical, we cannot share with VR
        return false;
    }

    int newType = convertType(type, reg, isPhysical);
    if(newType & LowOpndRegType_scratch) reg = reg - PhysicalReg_SCRATCH_1 + 1;

    // Look through all of the temporaries used by this bytecode implementation
    for(int k = 0; k < num_temp_regs_per_bytecode; k++) {

        if(infoByteCodeTemp[k].physicalType == newType &&
           infoByteCodeTemp[k].regNum == reg) {
            // We found a matching temporary

            if (!infoByteCodeTemp[k].is8Bit
                    || (physicalRegForVR >= PhysicalReg_EAX
                            && physicalRegForVR <= PhysicalReg_EDX)) {
                DEBUG_MOVE_OPT(ALOGD("Temp%d can%s share %s with v%d",
                        reg, infoByteCodeTemp[k].shareWithVR ? "" : " NOT",
                        physicalRegToString(static_cast<PhysicalReg>(
                                physicalRegForVR)), vB));

                return infoByteCodeTemp[k].shareWithVR;
            } else {
                DEBUG_MOVE_OPT(ALOGD("Temp%d can NOT share %s with v%d",
                        reg, physicalRegToString(static_cast<PhysicalReg>(
                                physicalRegForVR)), vB));

                // We cannot share same physical register as VR
                return false;
            }
        }
    }
    ALOGI("JIT_INFO: in checkTempReg2 %d %d\n", reg, newType);
    SET_JIT_ERROR(kJitErrorRegAllocFailed);
    return false;
}
//!check whether a temporary can share the same physical register with a VR

//!This is called in set_virtual_reg
int checkTempReg(int reg, int type, bool isPhysical, int vrNum) {
    if(currentBB == NULL) return PhysicalReg_Null;

    int newType = convertType(type, reg, isPhysical);
    if(newType & LowOpndRegType_scratch) reg = reg - PhysicalReg_SCRATCH_1 + 1;
    int index = searchCompileTable(newType, reg);
    if(index < 0) {
        ALOGI("JIT_INFO: temp reg %d type %d not found in checkTempReg\n", reg, newType);
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return PhysicalReg_Null;
    }

    //a temporary register can share the same physical reg with a VR if registerAllocMove is called
    //this will cause problem with move bytecode
    //get_VR(v1, t1) t1 and v1 point to the same physical reg
    //set_VR(t1, v2) t1 and v2 point to the same physical reg
    //this will cause v1 and v2 point to the same physical reg
    //FIX: if this temp reg shares a physical reg with another reg
    if(compileTable[index].physicalReg != PhysicalReg_Null) {
        int k;
        for(k = 0; k < compileTable.size (); k++) {
            if(k == index) continue;
            if(compileTable[k].physicalReg == compileTable[index].physicalReg) {
                return PhysicalReg_Null; //will allocate a register for VR
            }
        }
        decreaseRefCount(index);
        return compileTable[index].physicalReg;
    }
    if(compileTable[index].spill_loc_index >= 0) {
        //registerAlloc will call unspillLogicalReg (load from memory)
#ifdef DEBUG_REGALLOC
        ALOGW("in checkTempReg, the temporary register %d %d was spilled", reg, type);
#endif
        //No need for written, we don't write in it yet, just aliasing at worse
        int regAll = registerAlloc(type, reg, isPhysical, true/* updateRefCount */);
        return regAll;
    }
    return PhysicalReg_Null;
}
//!check whether a variable has exposed usage in a basic block

//!It calls hasExposedUsage2
bool hasExposedUsage(LowOpndRegType type, int regNum, BasicBlock_O1* bb) {
    int index = searchVirtualInfoOfBB(type, regNum, bb);
    if(index >= 0 && hasExposedUsage2(bb, index)) {
        return true;
    }
    return false;
}

/**
 * @brief Handle the spilling of registers at the end of a BasicBlock.
 * @param syncChildren If sync children is set to true, then we create or sync association table for child.
 * @return -1 if error, 0 otherwise
 */
int handleRegistersEndOfBB (bool syncChildren)
{
    //First we call freeReg to get rid of any temporaries that might be using physical registers.
    //Since we are at the end of the BB, we don't need to spill the temporaries because we
    //are done using them.
    freeReg (false);

    //If it's a jump, then we don't update the association tables. The reason is
    //that the implementation of jumping bytecode (for example "if" bytecode) will
    //create association tables for children
    if (syncChildren == true)
    {
        //Update association tables of child. There should technically be just one child
        //but we generically try to pass our information to all children.

        if (AssociationTable::createOrSyncTable(currentBB, true) == false)
        {
            //If syncing failed, the error code will be already set so we just pass along error information
            return -1;
        }

        if (AssociationTable::createOrSyncTable(currentBB, false) == false)
        {
            //If syncing failed, the error code will be already set so we just pass along error information
            return -1;
        }
    }

    syncAllRegs();

    return 0;
}

//! get ready for the next version of a hard-coded register

//!set its physicalReg to Null and update its reference count
int nextVersionOfHardReg(PhysicalReg pReg, int refCount) {
    int indexT = searchCompileTable(LowOpndRegType_gp | LowOpndRegType_hard, pReg);
    if(indexT < 0) {
        ALOGI("JIT_INFO: Physical reg not found at nextVersionOfHardReg");
        SET_JIT_ERROR(kJitErrorRegAllocFailed);
        return -1;
    }
    compileTable[indexT].setPhysicalReg (PhysicalReg_Null);
#ifdef DEBUG_REFCOUNT
    ALOGI("REFCOUNT: to %d in nextVersionOfHardReg %d", refCount, pReg);
#endif
    compileTable[indexT].refCount = refCount;
    return 0;
}

/**
 * @brief Updates compile table with virtual register information.
 * @details If compile table already contains information about virtual register, only the
 * reference counts are updated. Otherwise a new entry is created in the compile table.
 * @param regInfo Information about the virtual register.
 */
void insertFromVirtualInfo (const VirtualRegInfo &regInfo)
{
    int vR = regInfo.regNum;

    //We want to find entry in the compile table that matches the physical type we want.
    //Since compile table keeps track of physical type along with logical type in same field,
    //we do a binary bitwise inclusive or including the virtual register type.
    int index = searchCompileTable (LowOpndRegType_virtual | regInfo.physicalType, vR);

    if (index < 0)
    {
        //If we get here it means that the VR is not in the compile table

        //Create the new entry
        CompileTableEntry newEntry (regInfo);

        //Create the new entry and then copy it to the table
        compileTable.insert (newEntry);
    }
    else
    {
        //Just update the ref count when we already have an entry
        compileTable[index].updateRefCount (regInfo.refCount);
    }
}

/**
 * @brief Updates compile table with temporary information.
 * @param tempRegInfo Information about the temporary.
 */
static void insertFromTempInfo (const TempRegInfo &tempRegInfo)
{
    int index = searchCompileTable(tempRegInfo.physicalType, tempRegInfo.regNum);

    //If we did not find it in compile table simply insert it.
    if (index < 0)
    {
        CompileTableEntry newEntry (tempRegInfo);

        compileTable.insert (newEntry);
    }
    else
    {
        //Set the physical register
        compileTable[index].setPhysicalReg (PhysicalReg_Null);

        //Update the reference count for this temporary
        compileTable[index].updateRefCount (tempRegInfo.refCount);

        //Create link with the corresponding VR if needed
        compileTable[index].linkToVR (tempRegInfo.linkageToVR);

        //Reset spill location because this is a new temp which has not been spilled
        compileTable[index].resetSpillLocation ();
    }
}

/** print infoBasicBlock of the given basic block
*/
void dumpVirtualInfoOfBasicBlock(BasicBlock_O1* bb) {
    unsigned int jj, max;
    ALOGI("Virtual Info for BB%d --------", bb->id);
    max = bb->infoBasicBlock.size ();
    for(jj = 0; jj < max; jj++) {
        ALOGI("regNum %d physicalType %d accessType %d refCount %d def ",
               bb->infoBasicBlock[jj].regNum, bb->infoBasicBlock[jj].physicalType,
               bb->infoBasicBlock[jj].accessType, bb->infoBasicBlock[jj].refCount);
        int k;
        for(k = 0; k < bb->infoBasicBlock[jj].num_reaching_defs; k++)
            ALOGI("[%x %d %d %d] ", bb->infoBasicBlock[jj].reachingDefs[k].offsetPC,
                   bb->infoBasicBlock[jj].reachingDefs[k].regNum,
                   bb->infoBasicBlock[jj].reachingDefs[k].physicalType,
                   bb->infoBasicBlock[jj].reachingDefs[k].accessType);
    }
}

/** print compileTable
*/
void dumpCompileTable() {
    ALOGD("+++++++++++++++++++++ Compile Table +++++++++++++++++++++");
    ALOGD("%d entries\t%d memory_vr\t%d const_vr", compileTable.size (),
            num_memory_vr, num_const_vr);
    for(int entry = 0; entry < compileTable.size (); entry++) {
        ALOGD("regNum %d physicalType %d refCount %d physicalReg %s",
               compileTable[entry].regNum, compileTable[entry].physicalType,
               compileTable[entry].refCount, physicalRegToString(
                       static_cast<PhysicalReg>(compileTable[entry].physicalReg)));
    }
    for(int entry = 0; entry < num_memory_vr; entry++) {
        ALOGD("v%d inMemory:%s", memVRTable[entry].regNum,
                memVRTable[entry].inMemory ? "yes" : "no");
    }

    for(int entry = 0; entry < num_const_vr; entry++) {
        ALOGD("v%d isConst:%s value:%d", constVRTable[entry].regNum,
                constVRTable[entry].isConst ? "yes" : "no",
                constVRTable[entry].value);
    }
    ALOGD("---------------------------------------------------------");
}

/* BEGIN code to handle state transfers */
//! save the current state of register allocator to a state table

//!
void rememberState(int stateNum) {
#ifdef DEBUG_STATE
    ALOGI("STATE: remember state %d", stateNum);
#endif
    int k;
    for(k = 0; k < compileTable.size (); k++) {
        compileTable[k].rememberState (stateNum);
#ifdef DEBUG_STATE
        ALOGI("logical reg %d %d mapped to physical reg %d with spill index %d refCount %d",
               compileTable[k].regNum, compileTable[k].physicalType, compileTable[k].physicalReg,
               compileTable[k].spill_loc_index, compileTable[k].refCount);
#endif
    }
    for(k = 0; k < num_memory_vr; k++) {
        if(stateNum == 1) {
            stateTable2_1[k].regNum = memVRTable[k].regNum;
            stateTable2_1[k].inMemory = memVRTable[k].inMemory;
        }
        else if(stateNum == 2) {
            stateTable2_2[k].regNum = memVRTable[k].regNum;
            stateTable2_2[k].inMemory = memVRTable[k].inMemory;
        }
        else if(stateNum == 3) {
            stateTable2_3[k].regNum = memVRTable[k].regNum;
            stateTable2_3[k].inMemory = memVRTable[k].inMemory;
        }
        else if(stateNum == 4) {
            stateTable2_4[k].regNum = memVRTable[k].regNum;
            stateTable2_4[k].inMemory = memVRTable[k].inMemory;
        }
        else {
            ALOGI("JIT_INFO: state table overflow at goToState for compileTable\n");
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return;
        }
#ifdef DEBUG_STATE
        ALOGI("virtual reg %d in memory %d", memVRTable[k].regNum, memVRTable[k].inMemory);
#endif
    }
}

//!update current state of register allocator with a state table

//!
void goToState(int stateNum) {
    int k;
#ifdef DEBUG_STATE
    ALOGI("STATE: go to state %d", stateNum);
#endif
    for(k = 0; k < compileTable.size (); k++) {
        compileTable[k].goToState (stateNum);
    }
    int retCode = updateSpillIndexUsed();
    if (retCode < 0)
        return;
    syncAllRegs(); //to sync up allRegs CAN'T call freeReg here
    //since it will change the state!!!
    for(k = 0; k < num_memory_vr; k++) {
        if(stateNum == 1) {
            memVRTable[k].regNum = stateTable2_1[k].regNum;
            memVRTable[k].inMemory = stateTable2_1[k].inMemory;
        }
        else if(stateNum == 2) {
            memVRTable[k].regNum = stateTable2_2[k].regNum;
            memVRTable[k].inMemory = stateTable2_2[k].inMemory;
        }
        else if(stateNum == 3) {
            memVRTable[k].regNum = stateTable2_3[k].regNum;
            memVRTable[k].inMemory = stateTable2_3[k].inMemory;
        }
        else if(stateNum == 4) {
            memVRTable[k].regNum = stateTable2_4[k].regNum;
            memVRTable[k].inMemory = stateTable2_4[k].inMemory;
        }
        else {
            ALOGI("JIT_INFO: state table overflow at goToState for memVRTable\n");
            SET_JIT_ERROR(kJitErrorRegAllocFailed);
            return;
        }
        DEBUG_MEMORYVR(ALOGD("Updating state of v%d %sin memory",
                memVRTable[k].regNum, (memVRTable[k].inMemory ? "" : "NOT ")));
    }
}
typedef struct TransferOrder {
    int targetReg;
    int targetSpill;
    int compileIndex;
} TransferOrder;
#define MAX_NUM_DEST 20
//! a source register is used as a source in transfer
//! it can have a maximum of MAX_NUM_DEST destinations
typedef struct SourceReg {
    int physicalReg;
    int num_dests; //check bound
    TransferOrder dsts[MAX_NUM_DEST];
} SourceReg;
int num_src_regs = 0; //check bound
//! physical registers that are used as a source in transfer
//! we allow a maximum of MAX_NUM_DEST sources in a transfer
SourceReg srcRegs[MAX_NUM_DEST];
//! tell us whether a source register is handled already
bool handledSrc[MAX_NUM_DEST];
//! in what order should the source registers be handled
int handledOrder[MAX_NUM_DEST];

//! \brief insert a source register with a single destination
//!
//! \param srcPhysical
//! \param targetReg
//! \param targetSpill
//! \param index
//!
//! \return -1 on error, 0 otherwise
int insertSrcReg(int srcPhysical, int targetReg, int targetSpill, int index) {
    int k = 0;
    for(k = 0; k < num_src_regs; k++) {
        if(srcRegs[k].physicalReg == srcPhysical) { //increase num_dests
            if(srcRegs[k].num_dests >= MAX_NUM_DEST) {
                ALOGI("JIT_INFO: Exceed number dst regs for a source reg\n");
                SET_JIT_ERROR(kJitErrorMaxDestRegPerSource);
                return -1;
            }
            srcRegs[k].dsts[srcRegs[k].num_dests].targetReg = targetReg;
            srcRegs[k].dsts[srcRegs[k].num_dests].targetSpill = targetSpill;
            srcRegs[k].dsts[srcRegs[k].num_dests].compileIndex = index;
            srcRegs[k].num_dests++;
            return 0;
        }
    }
    if(num_src_regs >= MAX_NUM_DEST) {
        ALOGI("JIT_INFO: Exceed number of source regs\n");
        SET_JIT_ERROR(kJitErrorMaxDestRegPerSource);
        return -1;
    }
    srcRegs[num_src_regs].physicalReg = srcPhysical;
    srcRegs[num_src_regs].num_dests = 1;
    srcRegs[num_src_regs].dsts[0].targetReg = targetReg;
    srcRegs[num_src_regs].dsts[0].targetSpill = targetSpill;
    srcRegs[num_src_regs].dsts[0].compileIndex = index;
    num_src_regs++;
    return 0;
}

//! check whether a register is a source and the source is not yet handled

//!
bool dstStillInUse(int dstReg) {
    if(dstReg == PhysicalReg_Null) return false;
    int k;
    int index = -1;
    for(k = 0; k < num_src_regs; k++) {
        if(dstReg == srcRegs[k].physicalReg) {
            index = k;
            break;
        }
    }
    if(index < 0) return false; //not in use
    if(handledSrc[index]) return false; //not in use
    return true;
}

//! construct a legal order of the source registers in this transfer

//!
void constructSrcRegs(int stateNum) {
    int k;
    num_src_regs = 0;
#ifdef DEBUG_STATE
    ALOGI("IN constructSrcRegs");
#endif

    for(k = 0; k < compileTable.size (); k++) {
#ifdef DEBUG_STATE
        ALOGI("logical reg %d %d mapped to physical reg %d with spill index %d refCount %d",
               compileTable[k].regNum, compileTable[k].physicalType, compileTable[k].physicalReg,
               compileTable[k].spill_loc_index, compileTable[k].refCount);
#endif

        int pType = compileTable[k].physicalType;
        //ignore hardcoded logical registers
        if((pType & LowOpndRegType_hard) != 0) continue;
        //ignore type _fs
        if((pType & MASK_FOR_TYPE) == LowOpndRegType_fs) continue;
        if((pType & MASK_FOR_TYPE) == LowOpndRegType_fs_s) continue;

        //GL VR refCount is zero, can't ignore
        //L VR refCount is zero, ignore
        //GG VR refCount is zero, can't ignore
        //temporary refCount is zero, ignore

        /* get the target state */
        int targetReg = compileTable[k].getStatePhysicalRegister (stateNum);
        int targetSpill = compileTable[k].getStateSpillLocation (stateNum);

        /* there exists an ordering problem
           for example:
             for a VR, move from memory to a physical reg esi
             for a temporary regsiter, from esi to ecx
             if we handle VR first, content of the temporary reg. will be overwritten
           there are 4 cases:
             I: a variable is currently in memory and its target is in physical reg
             II: a variable is currently in a register and its target is in memory
             III: a variable is currently in a different register
             IV: a variable is currently in a different memory location (for non-VRs)
           For now, case IV is not handled since it didn't show
        */
        if(compileTable[k].physicalReg != targetReg &&
           isVirtualReg(compileTable[k].physicalType)) {
            /* handles VR for case I to III */

            if(compileTable[k].physicalReg == PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                /* handles VR for case I:
                   insert a xfer order from PhysicalReg_Null to targetReg */
                 if (insertSrcReg(PhysicalReg_Null, targetReg, targetSpill, k) == -1)
                     return;
#ifdef DEBUG_STATE
                ALOGI("insert for VR Null %d %d %d", targetReg, targetSpill, k);
#endif
            }

            if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                /* handles VR for case III
                   insert a xfer order from srcReg to targetReg */
                if (insertSrcReg(compileTable[k].physicalReg, targetReg, targetSpill, k) == -1)
                    return;
            }

            if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg == PhysicalReg_Null) {
                /* handles VR for case II
                   insert a xfer order from srcReg to memory */
                if (insertSrcReg(compileTable[k].physicalReg, targetReg, targetSpill, k) == -1)
                    return;
            }
        }

        if(compileTable[k].physicalReg != targetReg &&
           !isVirtualReg(compileTable[k].physicalType)) {
            /* handles non-VR for case I to III */

            if(compileTable[k].physicalReg == PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                /* handles non-VR for case I */
                if(compileTable[k].spill_loc_index < 0) {
                    /* this variable is freed, no need to transfer */
#ifdef DEBUG_STATE
                    ALOGW("in transferToState spill_loc_index is negative for temporary %d", compileTable[k].regNum);
#endif
                } else {
                    /* insert a xfer order from memory to targetReg */
#ifdef DEBUG_STATE
                    ALOGI("insert Null %d %d %d", targetReg, targetSpill, k);
#endif
                    if (insertSrcReg(PhysicalReg_Null, targetReg, targetSpill, k) == -1)
                        return;
                }
            }

            if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                /* handles non-VR for case III
                   insert a xfer order from srcReg to targetReg */
                if (insertSrcReg(compileTable[k].physicalReg, targetReg, targetSpill, k) == -1)
                    return;
            }

            if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg == PhysicalReg_Null) {
                /* handles non-VR for case II */
                if(targetSpill < 0) {
                    /* this variable is freed, no need to transfer */
#ifdef DEBUG_STATE
                    ALOGW("in transferToState spill_loc_index is negative for temporary %d", compileTable[k].regNum);
#endif
                } else {
                    /* insert a xfer order from srcReg to memory */
                    if (insertSrcReg(compileTable[k].physicalReg, targetReg, targetSpill, k) == -1)
                        return;
                }
            }

        }
    }//for compile entries

    int k2;
#ifdef DEBUG_STATE
    for(k = 0; k < num_src_regs; k++) {
        ALOGI("SRCREG %d: ", srcRegs[k].physicalReg);
        for(k2 = 0; k2 < srcRegs[k].num_dests; k2++) {
            int index = srcRegs[k].dsts[k2].compileIndex;
            ALOGI("[%d %d %d: %d %d %d] ", srcRegs[k].dsts[k2].targetReg,
                   srcRegs[k].dsts[k2].targetSpill, srcRegs[k].dsts[k2].compileIndex,
                   compileTable[index].regNum, compileTable[index].physicalType,
                   compileTable[index].spill_loc_index);
        }
        ALOGI("");
    }
#endif

    /* construct an order: xfers from srcReg first, then xfers from memory */
    int num_handled = 0;
    int num_in_order = 0;
    for(k = 0; k < num_src_regs; k++) {
        if(srcRegs[k].physicalReg == PhysicalReg_Null) {
            handledSrc[k] = true;
            num_handled++;
        } else {
            handledSrc[k] = false;
        }
    }
    while(num_handled < num_src_regs) {
        int prev_handled = num_handled;
        for(k = 0; k < num_src_regs; k++) {
            if(handledSrc[k]) continue;
            bool canHandleNow = true;
            for(k2 = 0; k2 < srcRegs[k].num_dests; k2++) {
                if(dstStillInUse(srcRegs[k].dsts[k2].targetReg)) {
                    canHandleNow = false;
                    break;
                }
            }
            if(canHandleNow) {
                handledSrc[k] = true;
                num_handled++;
                handledOrder[num_in_order] = k;
                num_in_order++;
            }
        } //for k
        if(num_handled == prev_handled) {
            ALOGI("JIT_INFO: No progress in selecting order while in constructSrcReg");
            SET_JIT_ERROR(kJitErrorStateTransfer);
            return;
        }
    } //while
    for(k = 0; k < num_src_regs; k++) {
        if(srcRegs[k].physicalReg == PhysicalReg_Null) {
            handledOrder[num_in_order] = k;
            num_in_order++;
        }
    }
    if(num_in_order != num_src_regs) {
        ALOGI("JIT_INFO: num_in_order != num_src_regs while in constructSrcReg");
        SET_JIT_ERROR(kJitErrorStateTransfer);
        return;
    }
#ifdef DEBUG_STATE
    ALOGI("ORDER: ");
    for(k = 0; k < num_src_regs; k++) {
        ALOGI("%d ", handledOrder[k]);
    }
#endif
}
//! transfer the state of register allocator to a state specified in a state table

//!
void transferToState(int stateNum) {
    freeReg(false); //do not spill GL
    int k;
#ifdef DEBUG_STATE
    ALOGI("STATE: transfer to state %d", stateNum);
#endif
    if(stateNum > 4 || stateNum < 1) {
        ALOGI("JIT_INFO: State table overflow at transferToState");
        SET_JIT_ERROR(kJitErrorStateTransfer);
        return;
    }
    constructSrcRegs(stateNum);
    int k4, k3;
    for(k4 = 0; k4 < num_src_regs; k4++) {
        int k2 = handledOrder[k4]; //index to srcRegs
        for(k3 = 0; k3 < srcRegs[k2].num_dests; k3++) {
            k = srcRegs[k2].dsts[k3].compileIndex;
            int targetReg = srcRegs[k2].dsts[k3].targetReg;
            int targetSpill = srcRegs[k2].dsts[k3].targetSpill;
            if(compileTable[k].physicalReg != targetReg && isVirtualReg(compileTable[k].physicalType)) {
                OpndSize oSize = getRegSize(compileTable[k].physicalType);
                bool isSS = ((compileTable[k].physicalType & MASK_FOR_TYPE) == LowOpndRegType_ss);
                if(compileTable[k].physicalReg == PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                    if(isSS)
                    {
                        const int vrOffset = getVirtualRegOffsetRelativeToFP (compileTable[k].regNum);
                        move_ss_mem_to_reg_noalloc(vrOffset,
                                                   PhysicalReg_FP, true,
                                                   MemoryAccess_VR, compileTable[k].regNum,
                                                   targetReg, true);
                    }
                    else
                    {
                        const int vrOffset = getVirtualRegOffsetRelativeToFP (compileTable[k].regNum);
                        move_mem_to_reg_noalloc(oSize, vrOffset,
                                                PhysicalReg_FP, true,
                                                MemoryAccess_VR, compileTable[k].regNum,
                                                targetReg, true);
                    }
                }
                if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                    move_reg_to_reg_noalloc((isSS ? OpndSize_64 : oSize),
                                            compileTable[k].physicalReg, true,
                                            targetReg, true);
                }
                if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg == PhysicalReg_Null) {
                    writeBackVR(compileTable[k].regNum, (LowOpndRegType)(compileTable[k].physicalType & MASK_FOR_TYPE),
                              compileTable[k].physicalReg);
                }
            } //VR
            if(compileTable[k].physicalReg != targetReg && !isVirtualReg(compileTable[k].physicalType)) {
                OpndSize oSize = getRegSize(compileTable[k].physicalType);
                if(compileTable[k].physicalReg == PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                    loadFromSpillRegion(oSize, targetReg,
                                        compileTable[k].spill_loc_index);
                }
                //both are not null, move from one to the other
                if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg != PhysicalReg_Null) {
                    move_reg_to_reg_noalloc(oSize, compileTable[k].physicalReg, true,
                                            targetReg, true);
                }
                //current is not null, target is null (move from reg to memory)
                if(compileTable[k].physicalReg != PhysicalReg_Null && targetReg == PhysicalReg_Null) {
                    saveToSpillRegion(oSize, compileTable[k].physicalReg, targetSpill);
                }
            } //temporary
        }//for
    }//for
    for(k = 0; k < num_memory_vr; k++) {
        bool targetBool = false;
        int targetReg = -1;
        if(stateNum == 1) {
            targetReg = stateTable2_1[k].regNum;
            targetBool = stateTable2_1[k].inMemory;
        }
        else if(stateNum == 2) {
            targetReg = stateTable2_2[k].regNum;
            targetBool = stateTable2_2[k].inMemory;
        }
        else if(stateNum == 3) {
            targetReg = stateTable2_3[k].regNum;
            targetBool = stateTable2_3[k].inMemory;
        }
        else if(stateNum == 4) {
            targetReg = stateTable2_4[k].regNum;
            targetBool = stateTable2_4[k].inMemory;
        }
        if(targetReg != memVRTable[k].regNum) {
            ALOGI("JIT_INFO: regNum mismatch in transferToState");
            SET_JIT_ERROR(kJitErrorStateTransfer);
            return;
        }
        if(targetBool && (!memVRTable[k].inMemory)) {
            //dump to memory, check entries in compileTable: vA gp vA xmm vA ss
#ifdef DEBUG_STATE
            ALOGW("inMemory mismatch for VR %d in transferToState", targetReg);
#endif
            bool doneXfer = false;

            int index = searchCompileTable(LowOpndRegType_xmm | LowOpndRegType_virtual, targetReg);
            if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
                writeBackVR(targetReg, LowOpndRegType_xmm, compileTable[index].physicalReg);
                doneXfer = true;
            } else if (index >= 0 && writeBackVRIfConstant(targetReg, LowOpndRegType_xmm) == true) {
                doneXfer = true;
            }

            if(!doneXfer) { //vA-1, xmm
                index = searchCompileTable(LowOpndRegType_xmm | LowOpndRegType_virtual, targetReg-1);
                if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
                    writeBackVR(targetReg-1, LowOpndRegType_xmm, compileTable[index].physicalReg);
                    doneXfer = true;
                }
                else if (index >= 0 && writeBackVRIfConstant(targetReg - 1, LowOpndRegType_xmm) == true) {
                    doneXfer = true;
                }
            }
            if(!doneXfer) { //vA gp
                index = searchCompileTable(LowOpndRegType_gp | LowOpndRegType_virtual, targetReg);
                if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
                    writeBackVR(targetReg, LowOpndRegType_gp, compileTable[index].physicalReg);
                    doneXfer = true;
                }
                else if (index >= 0 && writeBackVRIfConstant(targetReg, LowOpndRegType_gp) == true) {
                    doneXfer = true;
                }
            }
            if(!doneXfer) { //vA, ss
                index = searchCompileTable(LowOpndRegType_ss | LowOpndRegType_virtual, targetReg);
                if(index >= 0 && compileTable[index].physicalReg != PhysicalReg_Null) {
                    writeBackVR(targetReg, LowOpndRegType_ss, compileTable[index].physicalReg);
                    doneXfer = true;
                }
                else if (index >= 0 && writeBackVRIfConstant(targetReg, LowOpndRegType_ss) == true) {
                    doneXfer = true;
                }
            }
            if(!doneXfer) {
                ALOGI("JIT_INFO: Can't match inMemory state of v%d in "
                        "transferToState.", targetReg);
                SET_JIT_ERROR(kJitErrorStateTransfer);
                return;
            }
        }
        if((!targetBool) && memVRTable[k].inMemory) {
            //do nothing
        }
    }
#ifdef DEBUG_STATE
    ALOGI("END transferToState %d", stateNum);
#endif
    goToState(stateNum);
}
/* END code to handle state transfers */
