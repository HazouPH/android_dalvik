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


/*! \file AnalysisO1.h
    \brief A header file to define data structures used by register allocator & const folding
*/
#ifndef _DALVIK_NCG_ANALYSISO1_H
#define _DALVIK_NCG_ANALYSISO1_H

#include <set>
#include "Dalvik.h"
#include "enc_wrapper.h"
#include "Lower.h"
#ifdef WITH_JIT
#include "compiler/CompilerIR.h"
#endif
#include "RegisterizationBE.h"

//! maximal number of edges per basic block
#define MAX_NUM_EDGE_PER_BB 300
//! maximal number of virtual registers per basic block
#define MAX_REG_PER_BASICBLOCK 140
//! maximal number of virtual registers per bytecode
#define MAX_REG_PER_BYTECODE 40
//! maximal number of virtual registers per method
#define MAX_REG_PER_METHOD 200
//! maximal number of temporaries per bytecode
#define MAX_TEMP_REG_PER_BYTECODE 30

#define MAX_CONST_REG 150
#define NUM_MEM_VR_ENTRY 140

#define MASK_FOR_TYPE 7 //last 3 bits 111

#define LOOP_COUNT 10

//! maximal number of transfer points per basic block
#define MAX_XFER_PER_BB 1000  //on Jan 4
#define PC_FOR_END_OF_BB -999
#define PC_FOR_START_OF_BB -998

//! various cases of overlapping between 2 variables
typedef enum OverlapCase {
  OVERLAP_ALIGN = 0,
  OVERLAP_B_IS_LOW_OF_A,
  OVERLAP_B_IS_HIGH_OF_A,
  OVERLAP_LOW_OF_A_IS_HIGH_OF_B,
  OVERLAP_HIGH_OF_A_IS_LOW_OF_B,
  OVERLAP_A_IS_LOW_OF_B,
  OVERLAP_A_IS_HIGH_OF_B,
  OVERLAP_B_COVER_A,
  OVERLAP_B_COVER_LOW_OF_A,
  OVERLAP_B_COVER_HIGH_OF_A,
  OVERLAP_NO
} OverlapCase;

//!access type of a variable
typedef enum RegAccessType {
  REGACCESS_D = 0,
  REGACCESS_U,
  REGACCESS_DU,
  REGACCESS_UD,
  REGACCESS_L,
  REGACCESS_H,
  REGACCESS_UL,
  REGACCESS_UH,
  REGACCESS_LU,
  REGACCESS_HU,
  REGACCESS_N, //no access
  REGACCESS_UNKNOWN
} RegAccessType;

//! helper state indexes to determine if freeing VRs needs to be delayed
enum VRDelayFreeCounters {
  VRDELAY_NULLCHECK = 0, // used when VR is used for null check and freeing must be delayed
  VRDELAY_BOUNDCHECK = 1, // used when VR is used for bound check and freeing must be delayed
  VRDELAY_CAN_THROW = 2, // used when bytecode can throw exception, in fact delays freeing any VR
  VRDELAY_COUNT = 3, // Count of delay reasons
};

//!information about a physical register
typedef struct RegisterInfo {
  PhysicalReg physicalReg;
  bool isUsed;
  bool isCalleeSaved;
  int freeTimeStamp;
} RegisterInfo;

//!specifies the weight of a VR allocated to a specific physical register
//!it is used for GPR VR only
typedef struct RegAllocConstraint {
  PhysicalReg physicalReg;
  int count;
} RegAllocConstraint;

typedef enum XferType {
  XFER_MEM_TO_XMM, //for usage
  XFER_DEF_TO_MEM, //def is gp
  XFER_DEF_TO_GP_MEM,
  XFER_DEF_TO_GP,
  XFER_DEF_IS_XMM //def is xmm
} XferType;
typedef struct XferPoint {
  int tableIndex; //generated from a def-use pair
  XferType xtype;
  int offsetPC;
  int regNum; //get or set VR at offsetPC
  LowOpndRegType physicalType;

  //if XFER_DEF_IS_XMM
  int vr_gpl; //a gp VR that uses the lower half of the def
  int vr_gph;
  bool dumpToXmm;
  bool dumpToMem;
} XferPoint;

//!for def: accessType means which part of the VR defined at offestPC is live now
//!for use: accessType means which part of the usage comes from the reachingDef
typedef struct DefOrUse {
  int offsetPC; //!the program point
  int regNum; //!access the virtual reg
  LowOpndRegType physicalType; //!xmm or gp or ss
  RegAccessType accessType; //!D, L, H, N
} DefOrUse;
//!a link list of DefOrUse
typedef struct DefOrUseLink {
  int offsetPC;
  int regNum; //access the virtual reg
  LowOpndRegType physicalType; //xmm or gp
  RegAccessType accessType; //D, L, H, N
  struct DefOrUseLink* next;
} DefOrUseLink;
//!pair of def and uses
typedef struct DefUsePair {
  DefOrUseLink* uses;
  DefOrUseLink* useTail;
  int num_uses;
  DefOrUse def;
  struct DefUsePair* next;
} DefUsePair;

//!information associated with a virtual register
//!the pair <regNum, physicalType> uniquely determines a variable
typedef struct VirtualRegInfo {
   VirtualRegInfo () : regNum (-1), physicalType (LowOpndRegType_invalid), refCount (0),
           accessType (REGACCESS_UNKNOWN), num_reaching_defs (0)
  {
        //Set up allocation constraints for hardcoded registers
        for (int reg = PhysicalReg_StartOfGPMarker; reg <= PhysicalReg_EndOfGPMarker; reg++)
        {
            PhysicalReg pReg = static_cast<PhysicalReg> (reg);

            //For now we know of no constraints for each reg so we set count to zero
            allocConstraints[reg].physicalReg = pReg;
            allocConstraints[reg].count = 0;
            allocConstraintsSorted[reg].physicalReg = pReg;
            allocConstraintsSorted[reg].count = 0;
        }
  }

  int regNum;
  LowOpndRegType physicalType;
  int refCount;
  RegAccessType accessType;
  RegAllocConstraint allocConstraints[PhysicalReg_EndOfGPMarker + 1];
  RegAllocConstraint allocConstraintsSorted[PhysicalReg_EndOfGPMarker + 1];

  DefOrUse reachingDefs[3]; //!reaching defs to the virtual register
  int num_reaching_defs;
} VirtualRegInfo;

//!information of whether a VR is constant and its value
typedef struct ConstVRInfo {
  int regNum;
  int value;
  bool isConst;
} ConstVRInfo;

/**
 * @class constInfo
 * @brief information on 64 bit constants and their locations in a trace
*/
typedef struct ConstInfo {
    int valueL;             /**< @brief The lower 32 bits of the constant */
    int valueH;             /**< @brief The higher 32 bits of the constant */
    int regNum;             /**< @brief The register number of the constant */
    int offsetAddr;         /**< @brief The offset from start of instruction */
    char* streamAddr;       /**< @brief The address of instruction in stream */
    char* constAddr;        /**< @brief The address of the constant at the end of trace */
    bool constAlign;        /**< @brief Decide whether to Align constAddr to 16 bytes */
    struct ConstInfo *next; /**< @brief The pointer to the next 64 bit constant */
} ConstInfo;

#define NUM_ACCESS_IN_LIVERANGE 10
//!specifies one live range
typedef struct LiveRange {
  int start;
  int end; //inclusive
  //all accesses in the live range
  int num_access;
  int num_alloc;
  int* accessPC;
  struct LiveRange* next;
} LiveRange;
typedef struct BoundCheckIndex {
  int indexVR;
  bool checkDone;
} BoundCheckIndex;

/**
 * @brief Used to keep track of virtual register's in-memory state.
 */
typedef struct regAllocStateEntry2 {
  int regNum;    //!< The virtual register
  bool inMemory; //!< Whether 4-byte virtual register is in memory
} regAllocStateEntry2;

/**
 * @class MemoryVRInfo
 * @brief information for a virtual register such as live ranges, in memory
 */
typedef struct MemoryVRInfo {
    int regNum;                   /**< @brief The register number */
    bool inMemory;                /**< @brief Is it in memory or not */
    bool nullCheckDone;           /**< @brief Has a null check been done for it? */
    BoundCheckIndex boundCheck;   /**< @brief Bound check information for the VR */
    int num_ranges;               /**< @brief Number of ranges, used as a size for ranges */
    LiveRange* ranges;            /**< @brief Live range information for the entry */
    int delayFreeCounters[VRDELAY_COUNT]; /**< @brief Used with indexes defined by VRDelayFreeCounters enum to delay freeing */

    /**
     * @brief Default constructor which initializes all fields but sets an invalid virtual register.
     */
    MemoryVRInfo (void)
    {
        reset ();
    }

    /**
     * @brief Initializes all fields and sets a virtual register associated with this information.
     */
    MemoryVRInfo (int vR)
    {
        reset ();
        regNum = vR;
    }

    /**
     * @brief Used to reset information about the VR to default values.
     * @details Creates a logically invalid entry.
     */
    void reset (void);

    /**
     * @brief Returns the virtual register represented by this entry.
     */
    int getVirtualRegister (void)
    {
        return regNum;
    }

    /**
     * @brief Sets the virtual register represented by this entry.
     * @param regNum The virtual register number.
     */
    void setVirtualRegister (int regNum)
    {
        this->regNum = regNum;
    }

    /**
     * @brief Sets the in memory state of this entry which represent specific virtual register.
     * @param inMemory The in memory state to set to this entry.
     */
    void setInMemoryState (bool inMemory)
    {
        this->inMemory = inMemory;
    }
} MemoryVRInfo;

//!information of a temporary
//!the pair <regNum, physicalType> uniquely determines a variable
typedef struct TempRegInfo {
  int regNum;
  int physicalType;
  int refCount;
  int linkageToVR;
  int versionNum;
  bool shareWithVR; //for temp. regs updated by get_virtual_reg
  bool is8Bit;
} TempRegInfo;
struct BasicBlock_O1;

//Forward declaration
struct LowOpBlockLabel;

//!information associated with a basic block
struct BasicBlock_O1 : BasicBlock {
  int pc_start;       //!inclusive
  int pc_end;
  char *streamStart;        //Where the code generation started for the BasicBlock

  std::vector<VirtualRegInfo> infoBasicBlock;

  RegAllocConstraint allocConstraints[PhysicalReg_EndOfGPMarker+1]; //# of times a hardcoded register is used in this basic block
  //a physical register that is used many times has a lower priority to get picked in getFreeReg
  RegAllocConstraint allocConstraintsSorted[PhysicalReg_EndOfGPMarker+1]; //count from low to high

  DefUsePair* defUseTable;
  DefUsePair* defUseTail;

  std::vector <XferPoint> xferPoints; //program points where the transfer is required

  AssociationTable associationTable;    //Association table to keep track of physical registers beyond a BasicBlock

  LowOpBlockLabel *label;               //Label for the BasicBlock

  //Constructor
  BasicBlock_O1 (void);
  //Clear function: do we allocate the label (default: false)
  void clear (bool allocateLabel = false);

  //Clear and free everything
  void freeIt (void);
};

extern MemoryVRInfo memVRTable[NUM_MEM_VR_ENTRY];
extern int num_memory_vr;
extern TempRegInfo infoByteCodeTemp[MAX_TEMP_REG_PER_BYTECODE];
extern int num_temp_regs_per_bytecode;
extern VirtualRegInfo infoMethod[MAX_REG_PER_METHOD];
extern int num_regs_per_method;
extern BasicBlock_O1* currentBB;

extern int num_const_vr;
extern ConstVRInfo constVRTable[MAX_CONST_REG];

/**
 * @brief Provides a mapping between physical type and the size represented.
 * @param type The physical type represented by LowOpndRegType.
 * @return Returns size represented by the physical type.
 */
OpndSize getRegSize (int type);

/**
 * @class SwitchNormalCCInfo
 * @brief Data structure that contains related info of each normal chaining cell for switch bytecode
 */
typedef struct SwitchNormalCCInfo {
    char *patchAddr;                 /**< @brief  address in normal CC where codePtr in normal CC stored */
    char *normalCCAddr;                     /**< @brief  start address of a normal CC for switch bytcode */
} SwitchNormalCCInfo;

/**
 * @class SwitchInfo
 * @brief Information related to switch bytecode lowering
 */
typedef struct SwitchInfo {
    char *immAddr;           /**< @brief  address of the imm location in the first move instruction which pass in switch table address */
    char *immAddr2;          /**< @brief  address of the imm location in the second move instruction which pass in switch table address */
    u2 tSize;                /**< @brief  size of the switch case */
    std::vector<SwitchNormalCCInfo> switchNormalCCList; /**< @brief list that contains all normal chaining cell info for a switch bytecode */
} SwitchInfo;

/**
 * @class SwitchInfoScheduler
 * @brief Data structure that contains related switchInfo to pass to instruction scheduler
 */
typedef struct SwitchInfoScheduler {
    bool isFirst; /**< @brief TRUE for first move instruction which pass in switch table address*/
    int offset;   /**< @brief offset need to be added from the start of instruction */
    SwitchInfo * switchInfo; /**< @brief switch info for current switch bytecode */
} SwitchInfoScheduler;

void forwardAnalysis(int type);

//functions in bc_visitor.c
int getConstInfo(BasicBlock_O1* bb, const MIR * currentMIR);
int getVirtualRegInfo(VirtualRegInfo* infoArray, const MIR * currentMIR, bool updateBBConstraints = false);

/*
 * @brief Updates infoArray with temporaries accessed when lowering the bytecode
 * @param infoArray array of TempRegInfo to store temporaries accessed by a single bytecode
 * @param currentMIR current mir handled
 * @param dalvikPC PC pointer to bytecode
 * @return Returns size represented by the physical type.
 */
int getTempRegInfo(TempRegInfo* infoArray, const MIR * currentMIR, const u2* dalvikPC);
int createCFGHandler(Method* method);

int findVirtualRegInTable(int vA, LowOpndRegType type);
int searchCompileTable(int type, int regNum);
void handleJump(BasicBlock_O1* bb_prev, int relOff);
void connectBasicBlock(BasicBlock_O1* src, BasicBlock_O1* dst);
int insertWorklist(BasicBlock_O1* bb_prev, int targetOff);

//Collects virtual register usage for BB and sets up defuse tables
int collectInfoOfBasicBlock (BasicBlock_O1* bb);

void updateCurrentBBWithConstraints(PhysicalReg reg);
void updateConstInfo(BasicBlock_O1*);
void invalidateVRDueToConst(int reg, OpndSize size);

//Set a VR to a constant value
bool setVRToConst(int regNum, OpndSize size, int* tmpValue);

//Set that VR is not a constant
void setVRToNonConst(int regNum, OpndSize size);

/**
 * @brief Used the represent the constantness of a virtual register.
 */
enum VirtualRegConstantness
{
    VR_IS_NOT_CONSTANT = 0,  //!< virtual register is not constant
    VR_LOW_IS_CONSTANT = 1,  //!< only the low 32-bit of virtual register is constant
    VR_HIGH_IS_CONSTANT = 2, //!< only the high 32-bit of virtual register is constant
    VR_IS_CONSTANT = 3       //!< virtual register is entirely constant
};

//Checks if VR is constant
VirtualRegConstantness isVirtualRegConstant (int regNum, int opndRegType, int *valuePtr, bool updateRef = false);

//If VR is dirty, it writes the constant value to the VR on stack
void writeBackConstVR (int vR, int value);

//Writes virtual register back to memory if it holds a constant value
bool writeBackVRIfConstant (int vR, LowOpndRegType type);

//Write back virtual register to memory when it is in given physical register
void writeBackVR (int vR, LowOpndRegType type, int physicalReg);

//Check if VR is in memory
bool isInMemory(int regNum, OpndSize size);

//Update the in memory state of the virtual register
void setVRMemoryState (int vR, OpndSize size, bool inMemory);

//Find free registers and update the set
void findFreeRegisters (std::set<PhysicalReg> &outFreeRegisters, bool includeGPs = true, bool includeXMMs = true);

//Get a scratch register of a given type
PhysicalReg getScratch(const std::set<PhysicalReg> &scratchCandidates, LowOpndRegType type);

//Synchronize all registers in the compileTable
void syncAllRegs(void);

//Get a type for a given register
LowOpndRegType getTypeOfRegister(PhysicalReg reg);

//Update the physical register in the compile table from oldReg to newReg
bool updatePhysicalRegForVR(int vR, PhysicalReg oldReg, PhysicalReg newReg);

//Check whether a type is a virtual register type
bool isVirtualReg(int type);

//Spill a logical register using a index in the compileTable
int spillLogicalReg(int spill_index, bool updateTable);

//Reset all entries of vR in CompileTable to
//PhysicalReg_Null
void resetVRInCompileTable(int vR);

//Search in the memory table for a register
int searchMemTable(int regNum);

//Adds a virtual register to the memory table
bool addToMemVRTable (int vR, bool inMemory);

//! check whether the current bytecode is the last access to a VR within a live
bool isLastByteCodeOfLiveRange(int compileIndex);

//This function changes location of transfer points
void relocateXferPoints (BasicBlock_O1 *bb, int oldOffset, int newOffset);
#endif

