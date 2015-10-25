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


/*! \file Lower.h
    \brief A header file to define interface between lowering, register allocator, and scheduling
*/

#ifndef _DALVIK_LOWER
#define _DALVIK_LOWER

#define CODE_CACHE_PADDING 1024 //code space for a single bytecode
// comment out for phase 1 porting
#define PREDICTED_CHAINING
#define JIT_CHAIN

#define NCG_O1
//compilaton flags used by NCG O1
#define DUMP_EXCEPTION //to measure performance, required to have correct exception handling
/*! multiple versions for hardcoded registers */
#define HARDREG_OPT
#define CFG_OPT
/*! remove redundant move ops when accessing virtual registers */
#define MOVE_OPT
/*! remove redundant spill of virtual registers */
#define SPILL_OPT
#define XFER_OPT
/*! use live range analysis to allocate registers */
#define LIVERANGE_OPT
/*! remove redundant null check */
#define NULLCHECK_OPT
//#define BOUNDCHECK_OPT
#define CALL_FIX
#define NATIVE_FIX
#define INVOKE_FIX //optimization
#define GETVR_FIX //optimization

#include "Dalvik.h"
#include "enc_wrapper.h"
#include "AnalysisO1.h"
#include "CompileTable.h"
#include "compiler/CompilerIR.h"
#include "compiler/codegen/CompilerCodegen.h"

//compilation flags for debugging
//#define DEBUG_INFO
//#define DEBUG_CALL_STACK
//#define DEBUG_IGET_OBJ
//#define DEBUG_NCG_CODE_SIZE
//#define DEBUG_NCG
//#define DEBUG_NCG_1
//#define DEBUG_LOADING
//#define USE_INTERPRETER
//#define DEBUG_EACH_BYTECODE

/*! registers for functions are hardcoded */
#define HARDCODE_REG_CALL
#define HARDCODE_REG_SHARE
#define HARDCODE_REG_HELPER

#define PhysicalReg_FP PhysicalReg_EDI
#define PhysicalReg_Glue PhysicalReg_EBP

//COPIED from interp/InterpDefs.h
#define FETCH(_offset) (rPC[(_offset)])
#define INST_INST(_inst) ((_inst) & 0xff)
#define INST_A(_inst)       (((_inst) >> 8) & 0x0f)
#define INST_B(_inst)       ((_inst) >> 12)
#define INST_AA(_inst)      ((_inst) >> 8)

#define offEBP_self 8
#define offEBP_spill -56
#define offThread_jniLocal_nextEntry 168

// Definitions must be consistent with vm/mterp/x86/header.S
#define FRAME_SIZE     124

struct SwitchInfoScheduler;

typedef enum ArgsDoneType {
    ArgsDone_Normal = 0,
    ArgsDone_Native,
    ArgsDone_Full
} ArgsDoneType;

/*! An enum type
    to list bytecodes for AGET, APUT
*/
typedef enum ArrayAccess {
    AGET, AGET_WIDE, AGET_CHAR, AGET_SHORT, AGET_BOOLEAN, AGET_BYTE,
    APUT, APUT_WIDE, APUT_CHAR, APUT_SHORT, APUT_BOOLEAN, APUT_BYTE
} ArrayAccess;
/*! An enum type
    to list bytecodes for IGET, IPUT
*/
typedef enum InstanceAccess {
    IGET, IGET_WIDE, IPUT, IPUT_WIDE
} InstanceAccess;
/*! An enum type
    to list bytecodes for SGET, SPUT
*/
typedef enum StaticAccess {
    SGET, SGET_WIDE, SPUT, SPUT_WIDE
} StaticAccess;

typedef enum JmpCall_type {
    JmpCall_uncond = 1,
    JmpCall_cond,
    JmpCall_reg, //jump reg32
    JmpCall_call
} JmpCall_type;

//! \enum AtomOpCode
//! \brief Pseudo-mnemonics for Atom
//! \details Initially included to be in sync with ArmOpCode which specifies
//! additional pseudo mnemonics for use during codegen, but it has
//! diverted. Although there are references to this everywhere,
//! very little of this is actually used for functionality.
//! \todo Either refactor to match ArmOpCode or remove dependency on this.
enum AtomOpCode {
    ATOM_PSEUDO_CHAINING_CELL_BACKWARD_BRANCH = -15,
    ATOM_NORMAL_ALU = -14,
    ATOM_PSEUDO_ENTRY_BLOCK = -13,
    ATOM_PSEUDO_EXIT_BLOCK = -12,
    ATOM_PSEUDO_TARGET_LABEL = -11,
    ATOM_PSEUDO_CHAINING_CELL_HOT = -10,
    ATOM_PSEUDO_CHAINING_CELL_INVOKE_PREDICTED = -9,
    ATOM_PSEUDO_CHAINING_CELL_INVOKE_SINGLETON = -8,
    ATOM_PSEUDO_CHAINING_CELL_NORMAL = -7,
    ATOM_PSEUDO_DALVIK_BYTECODE_BOUNDARY = -6,
    ATOM_PSEUDO_ALIGN4 = -5,
    ATOM_PSEUDO_PC_RECONSTRUCTION_CELL = -4,
    ATOM_PSEUDO_PC_RECONSTRUCTION_BLOCK_LABEL = -3,
    ATOM_PSEUDO_EH_BLOCK_LABEL = -2,
    ATOM_PSEUDO_NORMAL_BLOCK_LABEL = -1,
    ATOM_NORMAL,
};

//! \enum LowOpndType
//! \brief Defines types of operands that a LowOp can have.
//! \details The Imm, Mem, and Reg variants correspond literally to what
//! the final encoded x86 instruction will have. The others are used for
//! additional behavior needed before the x86 encoding.
//! \see LowOp
enum LowOpndType {
    //! \brief Immediate
    LowOpndType_Imm,
    //! \brief Register
    LowOpndType_Reg,
    //! \brief Memory access
    LowOpndType_Mem,
    //! \brief Used for jumps to labels
    LowOpndType_Label,
    //! \brief Used for jumps to other blocks
    LowOpndType_BlockId,
    //! \brief Used for chaining
    LowOpndType_Chain
};

//! \enum LowOpndDefUse
//! \brief Defines type of usage that a LowOpnd can have.
//! \see LowOpnd
enum LowOpndDefUse {
    //! \brief Definition
    LowOpndDefUse_Def,
    //! \brief Usage
    LowOpndDefUse_Use,
    //! \brief Usage and Definition
    LowOpndDefUse_UseDef
};

//! \enum MemoryAccessType
//! \brief Classifies type of memory access.
enum MemoryAccessType {
    //! \brief access Dalvik virtual register
    MemoryAccess_VR,
    //! \brief access spill region
    MemoryAccess_SPILL,
    //! \brief unclassified memory access
    MemoryAccess_Unknown,
    //! \brief access to read-only constant section
    MemoryAccess_Constants,
};

//! \enum UseDefEntryType
//! \brief Defines types of resources on which there can be a dependency.
enum UseDefEntryType {
    //! \brief Control flags, EFLAGS register
    UseDefType_Ctrl,
    //! \brief Floating-point stack
    //! \details This is a very generic resource for x87 operations and
    //! doesn't break down different possible resources like control word,
    //! status word, FPU flags, etc. All of x87 resources fall into this
    //! type of resource.
    UseDefType_Float,
    //! \brief Dalvik virtual register. Corresponds to MemoryAccess_VR
    UseDefType_MemVR,
    //! \brief Spill region. Corresponds to MemoryAccess_SPILL
    UseDefType_MemSpill,
    //! \brief Unclassified memory access. Corresponds to MemoryAccess_Unknown
    //! \details No memory disambiguation will be done with unknown accesses
    UseDefType_MemUnknown,
    //! \brief Register
    UseDefType_Reg
};

//! \enum DependencyType
//! \brief Defines types of dependencies on a resource.
enum DependencyType {
    //! \brief Read after Write
    Dependency_RAW,
    //! \brief Write after Write
    Dependency_WAW,
    //! \brief Write after Read
    Dependency_WAR,
    //! \brief Read after Read
    Dependency_RAR,
};

//! \enum LatencyBetweenNativeInstructions
//! \brief Defines reasons for what causes pipeline stalls
//! between two instructions.
//! \warning Make sure that if adding new reasons here,
//! the scheduler needs updated with the actual latency value.
//! \see mapLatencyReasonToValue
enum LatencyBetweenNativeInstructions {
    //! \brief No latency between the two instructions
    Latency_None = 0,
    //! \brief Stall in address generation phase of pipeline
    //! when register is not available.
    Latency_Agen_stall,
    //! \brief Stall when a memory load is blocked by a store
    //! and there is no store forwarding.
    Latency_Load_blocked_by_store,
    //! \brief Stall due to cache miss during load from memory
    Latency_Memory_Load,
};

//! \brief Defines a relationship between a resource and its producer.
struct UseDefProducerEntry {
    //! \brief Resource type on which there is a dependency.
    UseDefEntryType type;
    //! \brief Virtual or physical register this resource is
    //! associated with.
    //! \details When physical, this is of enum type PhysicalReg.
    //! When VR, this is the virtual register number.
    //! When there is no register related dependency, this is
    //! negative.
    int regNum;
    //! \brief Corresponds to LowOp::slotId to keep track of producer.
    unsigned int producerSlot;
};

//! \brief Defines a relationship between a resource and its users.
struct UseDefUserEntry {
    //! \brief Resource type on which there is a dependency.
    UseDefEntryType type;
    //! \brief Virtual or physical register this resource is
    //! associated with.
    //! \details When physical, this is of enum type PhysicalReg.
    //! When VR, this is the virtual register number.
    //! When there is no register related dependency, this is
    //! negative.
    int regNum;
    //! \brief A list of LowOp::slotId to keep track of all users
    //! of this resource.
    std::vector<unsigned int> useSlotsList;
};

//! \brief Holds information on the data dependencies
struct DependencyInformation {
    //! \brief Type of data hazard
    DependencyType dataHazard;
    //! \brief Holds the LowOp::slotId of the LIR that causes this
    //! data dependence.
    unsigned int lowopSlotId;
    //! \brief Description for what causes the edge latency
    //! \see LatencyBetweenNativeInstructions
    LatencyBetweenNativeInstructions causeOfEdgeLatency;
    //! \brief Holds latency information for edges in the
    //! dependency graph, not execute to execute latency for the
    //! instructions.
    int edgeLatency;
};

//! \brief Holds general information about an operand.
struct LowOpnd {
    //! \brief Classification of operand.
    LowOpndType type;
    //! \brief Size of operand.
    OpndSize size;
    //! \brief Usage, definition, or both of operand.
    LowOpndDefUse defuse;
};

//! \brief Holds information about a register operand.
struct LowOpndReg {
    //! \brief Classification on type of register.
    LowOpndRegType regType;
    //! \brief Register number, either logical or physical.
    int regNum;
    //! \brief When false, register is logical.
    bool isPhysical;
};

//! \brief Holds information about an immediate operand.
struct LowOpndImm {
    //! \brief Value of the immediate.
    s4 value;
    //! \brief Size of the immediate.
    OpndSize immediateSize;
};

//! \brief Holds information about an immediate operand where the immediate
//! has not been generated yet.
struct LowOpndBlock {
    //! \brief Holds id of MIR level basic block.
    s4 value;
    //! \brief Whether the immediate needs to be aligned within 16-bytes
    bool immediateNeedsAligned;
};

//! \brief Defines maximum length of string holding label name.
#define LABEL_SIZE 256

//! \brief Holds information about an immediate operand where the immediate
//! has not been generated yet from label.
struct LowOpndLabel {
    //! \brief Name of the label for which to generate immediate.
    char label[LABEL_SIZE];
    //! \brief This is true when label is short term distance from caller
    //! and an 8-bit operand is sufficient.
    bool isLocal;
};

//! \brief Holds information about a memory operand.
struct LowOpndMem {
    //! \brief Displacement
    LowOpndImm m_disp;
    //! \brief Scaling
    LowOpndImm m_scale;
    //! \brief Index Register
    LowOpndReg m_index;
    //! \brief Base Register
    LowOpndReg m_base;
    //! \brief If true, must use the scaling value.
    bool hasScale;
    //! \brief Defines type of memory access.
    MemoryAccessType mType;
    //! \brief If positive, this represents the VR number
    int index;
};

//! \brief Data structure for an x86 LIR.
//! \todo Decouple fields used for scheduling from this struct.
//! is a good idea if using it throughout the trace JIT and never
//! actually passing it for scheduling.
struct LowOp {
    //! \brief Holds general LIR information (Google's implementation)
    //! \warning Only offset information is used for x86 and the other
    //! fields are not valid except in LowOpBlockLabel.
    LIR generic;
    //! \brief x86 mnemonic for instruction
    Mnemonic opCode;
    //! \brief x86 pseudo-mnemonic
    AtomOpCode opCode2;
    //! \brief Destination operand
    //! \details This is not used when there are only 0 or 1 operands.
    LowOpnd opndDest;
    //! \brief Source operand
    //! \details This is used when there is a single operand.
    LowOpnd opndSrc;
    //! \brief Holds number of operands for this LIR (0, 1, or 2)
    unsigned short numOperands;
    //! \brief Logical timestamp for ordering.
    //! \details This value should uniquely identify an LIR and also
    //! provide natural ordering depending on when it was requested.
    //! This is used during scheduling to hold original order for the
    //! native basic block.
    unsigned int slotId;
    //! \brief Logical time for when the LIR is ready.
    //! \details This field is used only for scheduling.
    int readyTime;
    //! \brief Cycle in which LIR is scheduled for issue.
    //! \details This field is used only for scheduling.
    int scheduledTime;
    //! \brief Execute to execute time for this instruction.
    //! \details This field is used only for scheduling.
    //! \see MachineModelEntry::executeToExecuteLatency
    int instructionLatency;
    //! \brief Issue port for this instruction.
    //! \details This field is used only for scheduling.
    //! \see MachineModelEntry::issuePortType
    int portType;
    //! \brief Weight of longest path in dependency graph from
    //! current instruction to end of the basic block.
    //! \details This field is used only for scheduling.
    int longestPath;
};

//! \brief Specialized LowOp with known label operand but
//! whose offset immediate is not known yet.
struct LowOpLabel : LowOp {
    //! \brief Label operand whose immediate has not yet been
    //! generated.
    LowOpndLabel labelOpnd;
};

//! \brief Specialized LowOp for use with block operand whose id
//! is known but the offset immediate has not been generated yet.
struct LowOpBlock : LowOp {
    //! \brief Non-generated immediate operand
    LowOpndBlock blockIdOpnd;
};

//! \brief Specialized LowOp which is only used with
//! pseudo-mnemonic.
//! \see AtomOpCode
struct LowOpBlockLabel {
    //! \todo Does not use inheritance like the other LowOp
    //! data structures because of a git merge issue. In future,
    //! this can be safely updated.
    LowOp lop;
};

//! \brief Specialized LowOp with an immediate operand.
struct LowOpImm : LowOp {
    //! \brief Immediate
    LowOpndImm immOpnd;
};

//! \brief Specialized LowOp with a memory operand.
struct LowOpMem : LowOp {
    //! \brief Memory Operand
    LowOpndMem memOpnd;
};

//! \brief Specialized LowOp with register operand.
struct LowOpReg : LowOp {
    //! \brief Register
    LowOpndReg regOpnd;
};

//! \brief Specialized LowOp for immediate to register.
struct LowOpImmReg : LowOp {
    //! \brief Immediate as source.
    LowOpndImm immSrc;
    //! \brief Register as destination.
    LowOpndReg regDest;
    //! \brief switchInfo passed to scheduler
    SwitchInfoScheduler *switchInfoScheduler;
};

//! \brief Specialized LowOp for register to register.
struct LowOpRegReg : LowOp {
    //! \brief Register as source.
    LowOpndReg regSrc;
    //! \brief Register as destination.
    LowOpndReg regDest;
};

//! \brief Specialized LowOp for imm + reg to reg
struct LowOpImmRegReg : LowOpRegReg {
    //! \brief The third imm operand other than src and dest reg
    LowOpndImm imm;
};

//! \brief Specialized LowOp for memory to register.
struct LowOpMemReg : LowOp {
    //! \brief Memory as source.
    LowOpndMem memSrc;
    //! \brief Register as destination.
    LowOpndReg regDest;
   //! \brief ptr to data structure containing 64 bit constants
    ConstInfo *constLink;
};

//! \brief Specialized LowOp for immediate to memory.
struct LowOpImmMem : LowOp {
    //! \brief Immediate as source.
    LowOpndImm immSrc;
    //! \brief Memory as destination.
    LowOpndMem memDest;
    //! \brief switchInfo passed to scheduler
    SwitchInfoScheduler *switchInfoScheduler;
};

//! \brief Specialized LowOp for register to memory.
struct LowOpRegMem : LowOp {
    //! \brief Register as source.
    LowOpndReg regSrc;
    //! \brief Memory as destination.
    LowOpndMem memDest;
};

/*!
\brief data structure for labels used when lowering a method

four label maps are defined: globalMap globalShortMap globalWorklist globalShortWorklist
globalMap: global labels where codePtr points to the label
           freeLabelMap called in clearNCG
globalWorklist: global labels where codePtr points to an instruciton using the label
  standalone NCG -------
                accessed by insertLabelWorklist & performLabelWorklist
  code cache ------
                inserted by performLabelWorklist(false),
                handled & cleared by generateRelocation in NcgFile.c
globalShortMap: local labels where codePtr points to the label
                freeShortMap called after generation of one bytecode
globalShortWorklist: local labels where codePtr points to an instruction using the label
                accessed by insertShortWorklist & insertLabel
definition of local label: life time of the label is within a bytecode or within a helper function
extra label maps are used by code cache:
  globalDataWorklist VMAPIWorklist
*/
typedef struct LabelMap {
  char label[LABEL_SIZE];
  char* codePtr; //code corresponding to the label or code that uses the label
  struct LabelMap* nextItem;
  OpndSize size;
  uint  addend;
} LabelMap;
/*!
\brief data structure to handle forward jump (GOTO, IF)

accessed by insertNCGWorklist & performNCGWorklist
*/
typedef struct NCGWorklist {
  //when WITH_JIT, relativePC stores the target basic block id
  s4 relativePC; //relative offset in bytecode
  int offsetPC;  //PC in bytecode
  int offsetNCG; //PC in native code
  char* codePtr; //code for native jump instruction
  struct NCGWorklist* nextItem;
  OpndSize size;
}NCGWorklist;
/*!
\brief data structure to handle SWITCH & FILL_ARRAY_DATA

two data worklist are defined: globalDataWorklist (used by code cache) & methodDataWorklist
methodDataWorklist is accessed by insertDataWorklist & performDataWorklist
*/
typedef struct DataWorklist {
  s4 relativePC; //relative offset in bytecode to access the data
  int offsetPC;  //PC in bytecode
  int offsetNCG; //PC in native code
  char* codePtr; //code for native instruction add_imm_reg imm, %edx
  char* codePtr2;//code for native instruction add_reg_reg %eax, %edx for SWITCH
                 //                            add_imm_reg imm, %edx for FILL_ARRAY_DATA
  struct DataWorklist* nextItem;
}DataWorklist;
#ifdef ENABLE_TRACING
typedef struct MapWorklist {
  u4 offsetPC;
  u4 offsetNCG;
  int isStartOfPC; //1 --> true 0 --> false
  struct MapWorklist* nextItem;
} MapWorklist;
#endif

#define BUFFER_SIZE 1024 //# of Low Ops buffered
//the following three numbers are hardcoded, please CHECK
#define BYTECODE_SIZE_PER_METHOD 81920
#define NATIVE_SIZE_PER_DEX 19000000 //FIXME for core.jar: 16M --> 18M for O1
#define NATIVE_SIZE_FOR_VM_STUBS 100000
#define MAX_HANDLER_OFFSET 1024 //maximal number of handler offsets

extern int LstrClassCastExceptionPtr, LstrInstantiationErrorPtr, LstrInternalError, LstrFilledNewArrayNotImpl;
extern int LstrArithmeticException, LstrArrayIndexException, LstrArrayStoreException, LstrStringIndexOutOfBoundsException;
extern int LstrDivideByZero, LstrNegativeArraySizeException, LstrNoSuchMethodError, LstrNullPointerException;
extern int LdoubNeg, LvaluePosInfLong, LvalueNegInfLong, LvalueNanLong, LshiftMask, Lvalue64, L64bits, LintMax, LintMin;

extern LabelMap* globalMap;
extern LabelMap* globalShortMap;
extern LabelMap* globalWorklist;
extern LabelMap* globalShortWorklist;
extern NCGWorklist* globalNCGWorklist;
extern DataWorklist* methodDataWorklist;
#ifdef ENABLE_TRACING
extern MapWorklist* methodMapWorklist;
#endif
extern PhysicalReg scratchRegs[4];

#define C_SCRATCH_1 scratchRegs[0]
#define C_SCRATCH_2 scratchRegs[1]
#define C_SCRATCH_3 scratchRegs[2] //scratch reg inside callee

extern LowOp* ops[BUFFER_SIZE];
extern bool isScratchPhysical;
extern u2* rPC;
extern int offsetPC;
extern int offsetNCG;
extern int mapFromBCtoNCG[BYTECODE_SIZE_PER_METHOD];
extern char* streamStart;

extern char* streamCode;

extern char* streamMethodStart; //start of the method
extern char* stream; //current stream pointer

extern Method* currentMethod;
extern int currentExceptionBlockIdx;

extern int globalMapNum;
extern int globalWorklistNum;
extern int globalDataWorklistNum;
extern int globalPCWorklistNum;
extern int chainingWorklistNum;
extern int VMAPIWorklistNum;

extern LabelMap* globalDataWorklist;
extern LabelMap* globalPCWorklist;
extern LabelMap* chainingWorklist;
extern LabelMap* VMAPIWorklist;

extern int ncgClassNum;
extern int ncgMethodNum;

// Global pointer to the current CompilationUnit
class CompilationUnit_O1;
extern CompilationUnit_O1 *gCompilationUnit;

bool existATryBlock(Method* method, int startPC, int endPC);
// interface between register allocator & lowering
extern int num_removed_nullCheck;

//Allocate a register
int registerAlloc(int type, int reg, bool isPhysical, bool updateRef, bool isDest = false);
//Allocate a register trying to alias a virtual register with a temporary
int registerAllocMove(int reg, int type, bool isPhysical, int srcReg, bool isDest = false);

int checkVirtualReg(int reg, LowOpndRegType type, int updateRef); //returns the physical register
int updateRefCount(int reg, LowOpndRegType type);
int updateRefCount2(int reg, int type, bool isPhysical);
int spillVirtualReg(int vrNum, LowOpndRegType type, bool updateTable);
int checkTempReg(int reg, int type, bool isPhysical, int vA);
bool checkTempReg2(int reg, int type, bool isPhysical, int physicalRegForVR, int vB);
int freeReg(bool writeBackAllVRs);
int nextVersionOfHardReg(PhysicalReg pReg, int refCount);
int updateVirtualReg(int reg, LowOpndRegType type);
int setVRNullCheck(int regNum, OpndSize size);
bool isVRNullCheck(int regNum, OpndSize size);
void setVRBoundCheck(int vr_array, int vr_index);
bool isVRBoundCheck(int vr_array, int vr_index);
int requestVRFreeDelay(int regNum, u4 reason);
int cancelVRFreeDelayRequest(int regNum, u4 reason);

// Update delay flag for all VRs, stored in physical registers
bool requestVRFreeDelayAll(u4 reason);
bool cancelVRFreeDelayRequestAll(u4 reason);

bool getVRFreeDelayRequested(int regNum);

//Update the virtual register use information
void updateVRAtUse(int reg, LowOpndRegType pType, int regAll);
int touchEcx();
int touchEax();
int touchEdx();
int beforeCall(const char* target);
int afterCall(const char* target);
void startBranch();
void endBranch();
void rememberState(int);
void goToState(int);
void transferToState(int);

//Handle virtual register writebacks
int handleRegistersEndOfBB(bool syncChildren);

//Call to reset certain flags before generating native code
void startNativeCode(int num, int type);
//Call to reset certain flags after generating native code
void endNativeCode(void);

#define XMM_1 PhysicalReg_XMM0
#define XMM_2 PhysicalReg_XMM1
#define XMM_3 PhysicalReg_XMM2
#define XMM_4 PhysicalReg_XMM3

/////////////////////////////////////////////////////////////////////////////////
//LR[reg] = disp + PR[base_reg] or disp + LR[base_reg]
void load_effective_addr(int disp, int base_reg, bool isBasePhysical,
                          int reg, bool isPhysical);
void load_effective_addr_scale(int base_reg, bool isBasePhysical,
                                int index_reg, bool isIndexPhysical, int scale,
                                int reg, bool isPhysical);
//! lea reg, [base_reg + index_reg*scale + disp]
void load_effective_addr_scale_disp(int base_reg, bool isBasePhysical, int disp,
                int index_reg, bool isIndexPhysical, int scale,
                int reg, bool isPhysical);
//! Loads a 16-bit value into the x87 FPU control word. Typically used to
//! establish or change the FPU's operational mode. Can cause exceptions to
//! be thrown if not cleared beforehand.
void load_fpu_cw(int disp, int base_reg, bool isBasePhysical);
void store_fpu_cw(bool checkException, int disp, int base_reg, bool isBasePhysical);
void convert_integer(OpndSize srcSize, OpndSize dstSize);
void convert_int_to_fp(int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, bool isDouble);
void load_fp_stack(LowOp* op, OpndSize size, int disp, int base_reg, bool isBasePhysical);
void load_int_fp_stack(OpndSize size, int disp, int base_reg, bool isBasePhysical);
void load_int_fp_stack_imm(OpndSize size, int imm);
void store_fp_stack(LowOp* op, bool pop, OpndSize size, int disp, int base_reg, bool isBasePhysical);
void store_int_fp_stack(LowOp* op, bool pop, OpndSize size, int disp, int base_reg, bool isBasePhysical);

void load_fp_stack_VR(OpndSize size, int vA);
void load_int_fp_stack_VR(OpndSize size, int vA);
void store_fp_stack_VR(bool pop, OpndSize size, int vA);
void store_int_fp_stack_VR(bool pop, OpndSize size, int vA);
void compare_VR_ss_reg(int vA, int reg, bool isPhysical);
void compare_VR_sd_reg(int vA, int reg, bool isPhysical);
void fpu_VR(ALU_Opcode opc, OpndSize size, int vA);
void compare_reg_mem(LowOp* op, OpndSize size, int reg, bool isPhysical,
                           int disp, int base_reg, bool isBasePhysical);
void compare_mem_reg(OpndSize size,
                           int disp, int base_reg, bool isBasePhysical,
                           int reg, bool isPhysical);
void compare_VR_reg(OpndSize size,
                           int vA,
                           int reg, bool isPhysical);
void compare_imm_reg(OpndSize size, int imm,
                           int reg, bool isPhysical);
void compare_imm_mem(OpndSize size, int imm,
                           int disp, int base_reg, bool isBasePhysical);
void compare_imm_VR(OpndSize size, int imm,
                           int vA);
void compare_reg_reg(int reg1, bool isPhysical1,
                           int reg2, bool isPhysical2);
void compare_reg_reg_16(int reg1, bool isPhysical1,
                         int reg2, bool isPhysical2);
void compare_ss_mem_reg(LowOp* op, int disp, int base_reg, bool isBasePhysical,
                              int reg, bool isPhysical);
void compare_ss_reg_with_reg(LowOp* op, int reg1, bool isPhysical1,
                              int reg2, bool isPhysical2);
void compare_sd_mem_with_reg(LowOp* op, int disp, int base_reg, bool isBasePhysical,
                              int reg, bool isPhysical);
void compare_sd_reg_with_reg(LowOp* op, int reg1, bool isPhysical1,
                              int reg2, bool isPhysical2);
void compare_fp_stack(bool pop, int reg, bool isDouble);
void test_imm_reg(OpndSize size, int imm, int reg, bool isPhysical);
void test_imm_mem(OpndSize size, int imm, int disp, int reg, bool isPhysical);

void conditional_move_reg_to_reg(OpndSize size, ConditionCode cc, int reg1, bool isPhysical1, int reg, bool isPhysical);
void move_ss_mem_to_reg(LowOp* op, int disp, int base_reg, bool isBasePhysical,
                        int reg, bool isPhysical);
void move_ss_reg_to_mem(LowOp* op, int reg, bool isPhysical,
                         int disp, int base_reg, bool isBasePhysical);
LowOpMemReg* move_ss_mem_to_reg_noalloc(int disp, int base_reg, bool isBasePhysical,
                         MemoryAccessType mType, int mIndex,
                         int reg, bool isPhysical);
LowOpRegMem* move_ss_reg_to_mem_noalloc(int reg, bool isPhysical,
                         int disp, int base_reg, bool isBasePhysical,
                         MemoryAccessType mType, int mIndex);
void move_sd_mem_to_reg(int disp, int base_reg, bool isBasePhysical,
                         int reg, bool isPhysical);
void move_sd_reg_to_mem(LowOp* op, int reg, bool isPhysical,
                         int disp, int base_reg, bool isBasePhysical);

void conditional_jump(ConditionCode cc, const char* target, bool isShortTerm);
void unconditional_jump(const char* target, bool isShortTerm);
void unconditional_jump_reg(int reg, bool isPhysical);
void unconditional_jump_rel32(void * target);
void call(const char* target);
void call_reg(int reg, bool isPhysical);
void call_reg_noalloc(int reg, bool isPhysical);
void call_mem(int disp, int reg, bool isPhysical);
void x86_return();

void alu_unary_reg(OpndSize size, ALU_Opcode opc, int reg, bool isPhysical);
void alu_unary_mem(LowOp* op, OpndSize size, ALU_Opcode opc, int disp, int base_reg, bool isBasePhysical);

void alu_binary_imm_mem(OpndSize size, ALU_Opcode opc,
                         int imm, int disp, int base_reg, bool isBasePhysical);
void alu_binary_imm_reg(OpndSize size, ALU_Opcode opc, int imm, int reg, bool isPhysical);
//Operate on a VR with another VR and an immediate
bool alu_imm_to_VR(OpndSize size, ALU_Opcode opc,
                         int srcVR, int destVR, int imm, int tempReg, bool isTempPhysical, const MIR * mir);
void alu_binary_mem_reg(OpndSize size, ALU_Opcode opc,
                         int disp, int base_reg, bool isBasePhysical,
                         int reg, bool isPhysical);
void alu_binary_VR_reg(OpndSize size, ALU_Opcode opc, int vA, int reg, bool isPhysical);
void alu_sd_binary_VR_reg(ALU_Opcode opc, int vA, int reg, bool isPhysical, bool isSD);
void alu_binary_reg_reg(OpndSize size, ALU_Opcode opc,
                         int reg1, bool isPhysical1,
                         int reg2, bool isPhysical2);
void alu_binary_reg_mem(OpndSize size, ALU_Opcode opc,
                         int reg, bool isPhysical,
                         int disp, int base_reg, bool isBasePhysical);

void fpu_mem(LowOp* op, ALU_Opcode opc, OpndSize size, int disp, int base_reg, bool isBasePhysical);
void alu_ss_binary_reg_reg(ALU_Opcode opc, int reg, bool isPhysical,
                            int reg2, bool isPhysical2);
void alu_sd_binary_reg_reg(ALU_Opcode opc, int reg, bool isPhysical,
                            int reg2, bool isPhysical2);

void push_mem_to_stack(OpndSize size, int disp, int base_reg, bool isBasePhysical);
void push_reg_to_stack(OpndSize size, int reg, bool isPhysical);

// create a new record for a 64 bit constant
void addNewToConstList(struct ConstInfo** listPtr, int constL, int constH, int reg, bool align);
// save address of memory location to be patched
bool saveAddrToConstList(struct ConstInfo** listPtr, int constL, int constH, int reg, char* patchAddr, int offset);
// access address of global constants
int getGlobalDataAddr(const char* dataName);

//returns the pointer to end of the native code
void move_reg_to_mem(OpndSize size,
                      int reg, bool isPhysical,
                      int disp, int base_reg, bool isBasePhysical);
void xchg_reg_to_mem(OpndSize size,
                      int reg, bool isPhysical,
                      int disp, int base_reg, bool isBasePhysical);
LowOpMemReg* move_mem_to_reg(OpndSize size,
                      int disp, int base_reg, bool isBasePhysical,
                      int reg, bool isPhysical);
void movez_mem_to_reg(OpndSize size,
                      int disp, int base_reg, bool isBasePhysical,
                      int reg, bool isPhysical);
void movez_reg_to_reg(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2);
void moves_mem_to_reg(LowOp* op, OpndSize size,
                      int disp, int base_reg, bool isBasePhysical,
                      int reg, bool isPhysical);
void movez_mem_disp_scale_to_reg(OpndSize size,
                      int base_reg, bool isBasePhysical,
                      int disp, int index_reg, bool isIndexPhysical, int scale,
                      int reg, bool isPhysical);
void moves_mem_disp_scale_to_reg(OpndSize size,
                      int base_reg, bool isBasePhysical,
                      int disp, int index_reg, bool isIndexPhysical, int scale,
                      int reg, bool isPhysical);

//! \brief Performs MOVSX reg, reg2
//!
//! \details Sign extends reg and moves to reg2
//! Size of destination register is fixed at 32-bits
//! \param size of the source operand
//! \param reg source operand
//! \param isPhysical if reg is a physical register
//! \param reg2 destination register
//! \param isPhysical2 if reg2 is a physical register
void moves_reg_to_reg(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2);

//! \brief Performs a move from a GPR to XMM
//!
//! \param sourceReg source general purpose register
//! \param isSourcePhysical if sourceReg is a physical register
//! \param destReg destination XMM register
//! \param isDestPhysical if destReg is a physical register
void move_gp_to_xmm (int sourceReg, bool isSourcePhysical, int destReg, bool isDestPhysical);

void move_reg_to_reg(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2);
void move_reg_to_reg_noalloc(OpndSize size,
                      int reg, bool isPhysical,
                      int reg2, bool isPhysical2);
LowOpMemReg* move_mem_scale_to_reg(OpndSize size,
                            int base_reg, bool isBasePhysical, int index_reg, bool isIndexPhysical, int scale,
                            int reg, bool isPhysical);
void move_mem_disp_scale_to_reg(OpndSize size,
                int base_reg, bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical, int scale,
                int reg, bool isPhysical);
void move_reg_to_mem_scale(OpndSize size,
                            int reg, bool isPhysical,
                            int base_reg, bool isBasePhysical, int index_reg, bool isIndexPhysical, int scale);
void xchg_reg_to_mem_scale(OpndSize size,
                            int reg, bool isPhysical,
                            int base_reg, bool isBasePhysical, int index_reg, bool isIndexPhysical, int scale);
void move_reg_to_mem_disp_scale(OpndSize size,
                            int reg, bool isPhysical,
                            int base_reg, bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical, int scale);
void move_imm_to_mem(OpndSize size, int imm,
                      int disp, int base_reg, bool isBasePhysical);
void set_VR_to_imm(int vA, OpndSize size, int imm);
void set_VR_to_imm_noalloc(int vA, OpndSize size, int imm);
void set_VR_to_imm_noupdateref(LowOp* op, int vA, OpndSize size, int imm);
void move_imm_to_reg(OpndSize size, int imm, int reg, bool isPhysical);
void move_imm_to_reg_noalloc(OpndSize size, int imm, int reg, bool isPhysical);
void compareAndExchange(OpndSize size,
             int reg, bool isPhysical,
             int disp, int base_reg, bool isBasePhysical);

//LR[reg] = VR[vB]
//or
//PR[reg] = VR[vB]
void get_virtual_reg(int vB, OpndSize size, int reg, bool isPhysical);
void get_virtual_reg_noalloc(int vB, OpndSize size, int reg, bool isPhysical);
//VR[v] = LR[reg]
//or
//VR[v] = PR[reg]
void set_virtual_reg(int vA, OpndSize size, int reg, bool isPhysical);
void set_virtual_reg_noalloc(int vA, OpndSize size, int reg, bool isPhysical);
void get_VR_ss(int vB, int reg, bool isPhysical);
void set_VR_ss(int vA, int reg, bool isPhysical);
void get_VR_sd(int vB, int reg, bool isPhysical);
void set_VR_sd(int vA, int reg, bool isPhysical);

int spill_reg(int reg, bool isPhysical);
int unspill_reg(int reg, bool isPhysical);

void move_reg_to_mem_noalloc(OpndSize size,
                      int reg, bool isPhysical,
                      int disp, int base_reg, bool isBasePhysical,
                      MemoryAccessType mType, int mIndex);
LowOpMemReg* move_mem_to_reg_noalloc(OpndSize size,
                      int disp, int base_reg, bool isBasePhysical,
                      MemoryAccessType mType, int mIndex,
                      int reg, bool isPhysical);

//////////////////////////////////////////////////////////////
int insertLabel(const char* label, bool checkDup);
void export_pc (void);

int simpleNullCheck(int reg, bool isPhysical, int vr);
int nullCheck(int reg, bool isPhysical, int exceptionNum, int vr);
int handlePotentialException(
                             ConditionCode code_excep, ConditionCode code_okay,
                             int exceptionNum, const char* errName);
int get_currentpc(int reg, bool isPhysical);
int get_self_pointer(int reg, bool isPhysical);
int get_res_strings(int reg, bool isPhysical);
int get_res_classes(int reg, bool isPhysical);
int get_res_fields(int reg, bool isPhysical);
int get_res_methods(int reg, bool isPhysical);
int get_glue_method_class(int reg, bool isPhysical);
int get_glue_method(int reg, bool isPhysical);
int get_suspendCount(int reg, bool isPhysical);
int get_return_value(OpndSize size, int reg, bool isPhysical);
int set_return_value(OpndSize size, int reg, bool isPhysical);
void set_return_value(OpndSize size, int sourceReg, bool isSourcePhysical,
        int scratchRegForSelfThread, int isScratchPhysical);
int clear_exception();
int get_exception(int reg, bool isPhysical);
int set_exception(int reg, bool isPhysical);

int call_moddi3();
int call_divdi3();
int call_fmod();
int call_fmodf();
int call_dvmFindCatchBlock();
int call_dvmThrowVerificationError();
int call_dvmAllocObject();
int call_dvmAllocArrayByClass();
int call_dvmResolveMethod();
int call_dvmResolveClass();
int call_dvmInstanceofNonTrivial();
int call_dvmThrow();
int call_dvmThrowWithMessage();
int call_dvmCheckSuspendPending();
int call_dvmLockObject();
int call_dvmUnlockObject();
int call_dvmInitClass();
int call_dvmAllocPrimitiveArray();
int call_dvmInterpHandleFillArrayData();
int call_dvmNcgHandlePackedSwitch();
int call_dvmNcgHandleSparseSwitch();
int call_dvmJitHandlePackedSwitch();
int call_dvmJitHandleSparseSwitch();
void call_dvmJitLookUpBigSparseSwitch();
int call_dvmJitToInterpTraceSelectNoChain();
int call_dvmJitToPatchPredictedChain();
void call_dvmJitToInterpNormal();
/** @brief helper function to call dvmJitToInterpBackwardBranch */
void call_dvmJitToInterpBackwardBranch();
void call_dvmJitToInterpTraceSelect();
int call_dvmQuasiAtomicSwap64();
int call_dvmQuasiAtomicRead64();
int call_dvmCanPutArrayElement();
int call_dvmFindInterfaceMethodInCache();
int call_dvmHandleStackOverflow();
int call_dvmResolveString();
int call_dvmResolveInstField();
int call_dvmResolveStaticField();
#ifdef WITH_SELF_VERIFICATION
int call_selfVerificationLoad(void);
int call_selfVerificationStore(void);
int call_selfVerificationLoadDoubleword(void);
int call_selfVerificationStoreDoubleword(void);
#endif

//labels and branches
//shared branch to resolve class: 2 specialized versions
//OPTION 1: call & ret
//OPTION 2: store jump back label in a fixed register or memory
//jump to .class_resolve, then jump back
//OPTION 3: share translator code
/* global variables: ncg_rPC */
int resolve_class(
                  int startLR/*logical register index*/, bool isPhysical, int tmp/*const pool index*/,
                  int thirdArg);
/* EXPORT_PC; movl exceptionPtr, -8(%esp); movl descriptor, -4(%esp); lea; call; lea; jmp */
int throw_exception_message(int exceptionPtr, int obj_reg, bool isPhysical,
                            int startLR/*logical register index*/, bool startPhysical);
/* EXPORT_PC; movl exceptionPtr, -8(%esp); movl imm, -4(%esp); lea; call; lea; jmp */
int throw_exception(int exceptionPtr, int imm,
                    int startLR/*logical register index*/, bool startPhysical);

void freeShortMap();
int insertDataWorklist(s4 relativePC, char* codePtr1);
#ifdef ENABLE_TRACING
int insertMapWorklist(s4 BCOffset, s4 NCGOffset, int isStartOfPC);
#endif
int performNCGWorklist();
int performDataWorklist();
void performLabelWorklist();
void performMethodLabelWorklist();
void freeLabelMap();
void performSharedWorklist();
void performChainingWorklist();
void freeNCGWorklist();
void freeDataWorklist();
void freeLabelWorklist();
/** @brief search chainingWorklist to return instruction offset address in move instruction */
char* searchChainingWorklist(unsigned int blockId);
/** @brief search globalNCGWorklist to find the jmp/jcc offset address */
char* searchNCGWorklist(int blockId);
/** @brief search globalWorklist to find the jmp/jcc offset address */
char* searchLabelWorklist(char* label);
void freeChainingWorklist();

int common_backwardBranch();
int common_exceptionThrown();
int common_errNullObject();
int common_errArrayIndex();
int common_errArrayStore();
int common_errNegArraySize();
int common_errNoSuchMethod();
int common_errDivideByZero();
int common_periodicChecks_entry();
int common_periodicChecks4();
int common_gotoBail(void);
int common_gotoBail_0(void);
int common_errStringIndexOutOfBounds();

#if defined VTUNE_DALVIK
void sendLabelInfoToVTune(int startStreamPtr, int endStreamPtr, const char* labelName);
#endif

// Delay VRs freeing if bytecode can throw exception, then call lowerByteCode
int lowerByteCodeCanThrowCheck(const Method* method, const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1 *cUnit);
//lower a bytecode
int lowerByteCode(const Method* method, const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1 *cUnit);

int op_nop(const MIR * mir);
int op_move(const MIR * mir);
int op_move_from16(const MIR * mir);
int op_move_16(const MIR * mir);
int op_move_wide(const MIR * mir);
int op_move_wide_from16(const MIR * mir);
int op_move_wide_16(const MIR * mir);
int op_move_result(const MIR * mir);
int op_move_result_wide(const MIR * mir);
int op_move_exception(const MIR * mir);

int op_return_void(const MIR * mir);
int op_return(const MIR * mir);
int op_return_wide(const MIR * mir);
int op_const_4(const MIR * mir);
int op_const_16(const MIR * mir);
int op_const(const MIR * mir);
int op_const_high16(const MIR * mir);
int op_const_wide_16(const MIR * mir);
int op_const_wide_32(const MIR * mir);
int op_const_wide(const MIR * mir);
int op_const_wide_high16(const MIR * mir);
int op_const_string(const MIR * mir);
int op_const_string_jumbo(const MIR * mir);
int op_const_class(const MIR * mir);
int op_monitor_enter(const MIR * mir);
int op_monitor_exit(const MIR * mir);
int op_check_cast(const MIR * mir);
int op_instance_of(const MIR * mir);

int op_array_length(const MIR * mir);
int op_new_instance(const MIR * mir);
int op_new_array(const MIR * mir);
int op_filled_new_array(const MIR * mir);
int op_filled_new_array_range(const MIR * mir);
int op_fill_array_data(const MIR * mir, const u2 * dalvikPC);
int op_throw(const MIR * mir);
int op_throw_verification_error(const MIR * mir);
int op_goto (const MIR * mir, BasicBlock *currentBB);
int op_packed_switch(const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit);
int op_sparse_switch(const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit);
int op_if_ge(const MIR * mir);
int op_aget(const MIR * mir);
int op_aget_wide(const MIR * mir);
int op_aget_object(const MIR * mir);
int op_aget_boolean(const MIR * mir);
int op_aget_byte(const MIR * mir);
int op_aget_char(const MIR * mir);
int op_aget_short(const MIR * mir);
int op_aput(const MIR * mir);
int op_aput_wide(const MIR * mir);
int op_aput_object(const MIR * mir);
int op_aput_boolean(const MIR * mir);
int op_aput_byte(const MIR * mir);
int op_aput_char(const MIR * mir);
int op_aput_short(const MIR * mir);
int op_iget(const MIR * mir, bool isVolatile = false);
int op_iget_wide(const MIR * mir, bool isVolatile);
int op_iget_object(const MIR * mir, bool isVolatile = false);
int op_iget_boolean(const MIR * mir);
int op_iget_byte(const MIR * mir);
int op_iget_char(const MIR * mir);
int op_iget_short(const MIR * mir);
int op_iput(const MIR * mir, bool isVolatile = false);
int op_iput_wide(const MIR * mir, bool isVolatile);
int op_iput_object(const MIR * mir, bool isVolatile);
int op_iput_boolean(const MIR * mir);
int op_iput_byte(const MIR * mir);
int op_iput_char(const MIR * mir);
int op_iput_short(const MIR * mir);
int op_sget(const MIR * mir, bool isVolatile = false);
int op_sget_wide(const MIR * mir, bool isVolatile);
int op_sget_object(const MIR * mir, bool isVolatile = false);
int op_sget_boolean(const MIR * mir);
int op_sget_byte(const MIR * mir);
int op_sget_char(const MIR * mir);
int op_sget_short(const MIR * mir);
int op_sput(const MIR * mir, bool isObj, bool isVolatile = false);
int op_sput_wide(const MIR * mir, bool isVolatile);
int op_sput_object(const MIR * mir, bool isVolatile);
int op_sput_boolean(const MIR * mir);
int op_sput_byte(const MIR * mir);
int op_sput_char(const MIR * mir);
int op_sput_short(const MIR * mir);
int op_invoke_virtual(const MIR * mir);
int op_invoke_super(const MIR * mir);
int op_invoke_direct(const MIR * mir);
int op_invoke_static(const MIR * mir);
int op_invoke_interface(const MIR * mir);
int op_invoke_virtual_range(const MIR * mir);
int op_invoke_super_range(const MIR * mir);
int op_invoke_direct_range(const MIR * mir);
int op_invoke_static_range(const MIR * mir);
int op_invoke_interface_range(const MIR * mir);
int op_int_to_long(const MIR * mir);
int op_add_long_2addr(const MIR * mir);
int op_add_int_lit8(const MIR * mir);
int op_cmpl_float(const MIR * mir);
int op_cmpg_float(const MIR * mir);
int op_cmpl_double(const MIR * mir);
int op_cmpg_double(const MIR * mir);
int op_cmp_long(const MIR * mir);
int op_if_eq(const MIR * mir);
int op_if_ne(const MIR * mir);
int op_if_lt(const MIR * mir);
int op_if_gt(const MIR * mir);
int op_if_le(const MIR * mir);
int op_if_eqz(const MIR * mir);
int op_if_nez(const MIR * mir);
int op_if_ltz(const MIR * mir);
int op_if_gez(const MIR * mir);
int op_if_gtz(const MIR * mir);
int op_if_lez(const MIR * mir);
int op_neg_int(const MIR * mir);
int op_not_int(const MIR * mir);
int op_neg_long(const MIR * mir);
int op_not_long(const MIR * mir);
int op_neg_float(const MIR * mir);
int op_neg_double(const MIR * mir);
int op_int_to_float(const MIR * mir);
int op_int_to_double(const MIR * mir);
int op_long_to_int(const MIR * mir);
int op_long_to_float(const MIR * mir);
int op_long_to_double(const MIR * mir);
int op_float_to_int(const MIR * mir);
int op_float_to_long(const MIR * mir);
int op_float_to_double(const MIR * mir);
int op_double_to_int(const MIR * mir);
int op_double_to_long(const MIR * mir);
int op_double_to_float(const MIR * mir);
int op_int_to_byte(const MIR * mir);
int op_int_to_char(const MIR * mir);
int op_int_to_short(const MIR * mir);
int op_add_int(const MIR * mir);
int op_sub_int(const MIR * mir);
int op_mul_int(const MIR * mir);
int op_div_int(const MIR * mir);
int op_rem_int(const MIR * mir);
int op_and_int(const MIR * mir);
int op_or_int(const MIR * mir);
int op_xor_int(const MIR * mir);
int op_shl_int(const MIR * mir);
int op_shr_int(const MIR * mir);
int op_ushr_int(const MIR * mir);
int op_add_long(const MIR * mir);
int op_sub_long(const MIR * mir);
int op_mul_long(const MIR * mir);
int op_div_long(const MIR * mir);
int op_rem_long(const MIR * mir);
int op_and_long(const MIR * mir);
int op_or_long(const MIR * mir);
int op_xor_long(const MIR * mir);
int op_shl_long(const MIR * mir);
int op_shr_long(const MIR * mir);
int op_ushr_long(const MIR * mir);
int op_add_float(const MIR * mir);
int op_sub_float(const MIR * mir);
int op_mul_float(const MIR * mir);
int op_div_float(const MIR * mir);
int op_rem_float(const MIR * mir);
int op_add_double(const MIR * mir);
int op_sub_double(const MIR * mir);
int op_mul_double(const MIR * mir);
int op_div_double(const MIR * mir);
int op_rem_double(const MIR * mir);
int op_add_int_2addr(const MIR * mir);
int op_sub_int_2addr(const MIR * mir);
int op_mul_int_2addr(const MIR * mir);
int op_div_int_2addr(const MIR * mir);
int op_rem_int_2addr(const MIR * mir);
int op_and_int_2addr(const MIR * mir);
int op_or_int_2addr(const MIR * mir);
int op_xor_int_2addr(const MIR * mir);
int op_shl_int_2addr(const MIR * mir);
int op_shr_int_2addr(const MIR * mir);
int op_ushr_int_2addr(const MIR * mir);
int op_sub_long_2addr(const MIR * mir);
int op_mul_long_2addr(const MIR * mir);
int op_div_long_2addr(const MIR * mir);
int op_rem_long_2addr(const MIR * mir);
int op_and_long_2addr(const MIR * mir);
int op_or_long_2addr(const MIR * mir);
int op_xor_long_2addr(const MIR * mir);
int op_shl_long_2addr(const MIR * mir);
int op_shr_long_2addr(const MIR * mir);
int op_ushr_long_2addr(const MIR * mir);
int op_add_float_2addr(const MIR * mir);
int op_sub_float_2addr(const MIR * mir);
int op_mul_float_2addr(const MIR * mir);
int op_div_float_2addr(const MIR * mir);
int op_rem_float_2addr(const MIR * mir);
int op_add_double_2addr(const MIR * mir);
int op_sub_double_2addr(const MIR * mir);
int op_mul_double_2addr(const MIR * mir);
int op_div_double_2addr(const MIR * mir);
int op_rem_double_2addr(const MIR * mir);
int op_add_int_lit16(const MIR * mir);
int op_rsub_int(const MIR * mir);
int op_mul_int_lit16(const MIR * mir);
int op_div_int_lit16(const MIR * mir);
int op_rem_int_lit16(const MIR * mir);
int op_and_int_lit16(const MIR * mir);
int op_or_int_lit16(const MIR * mir);
int op_xor_int_lit16(const MIR * mir);
int op_rsub_int_lit8(const MIR * mir);
int op_mul_int_lit8(const MIR * mir);
int op_div_int_lit8(const MIR * mir);
int op_rem_int_lit8(const MIR * mir);
int op_and_int_lit8(const MIR * mir);
int op_or_int_lit8(const MIR * mir);
int op_xor_int_lit8(const MIR * mir);
int op_shl_int_lit8(const MIR * mir);
int op_shr_int_lit8(const MIR * mir);
int op_ushr_int_lit8(const MIR * mir);
int op_execute_inline(const MIR * mir, bool isRange);
int op_invoke_direct_empty(const MIR * mir);
int op_iget_quick(const MIR * mir);
int op_iget_wide_quick(const MIR * mir);
int op_iget_object_quick(const MIR * mir);
int op_iput_quick(const MIR * mir);
int op_iput_wide_quick(const MIR * mir);
int op_iput_object_quick(const MIR * mir);
int op_invoke_virtual_quick(const MIR * mir);
int op_invoke_virtual_quick_range(const MIR * mir);
int op_invoke_super_quick(const MIR * mir);
int op_invoke_super_quick_range(const MIR * mir);

///////////////////////////////////////////////
void set_reg_opnd(LowOpndReg* op_reg, int reg, bool isPhysical, LowOpndRegType type);
void set_mem_opnd(LowOpndMem* mem, int disp, int base, bool isPhysical);
void set_mem_opnd_scale(LowOpndMem* mem, int base, bool isPhysical, int disp, int index, bool indexPhysical, int scale);
LowOpImm* dump_imm(Mnemonic m, OpndSize size, int imm);
void dump_imm_update(int imm, char* codePtr, bool updateSecondOperand);
LowOpBlock* dump_blockid_imm(Mnemonic m, int targetBlockId,
        bool immediateNeedsAligned);
LowOpMem* dump_mem(Mnemonic m, AtomOpCode m2, OpndSize size,
               int disp, int base_reg, bool isBasePhysical);
LowOpReg* dump_reg(Mnemonic m, AtomOpCode m2, OpndSize size,
               int reg, bool isPhysical, LowOpndRegType type);
LowOpReg* dump_reg_noalloc(Mnemonic m, OpndSize size,
               int reg, bool isPhysical, LowOpndRegType type);
LowOpImmMem* dump_imm_mem_noalloc(Mnemonic m, OpndSize size,
                           int imm,
                           int disp, int base_reg, bool isBasePhysical,
                           MemoryAccessType mType, int mIndex, SwitchInfoScheduler *switchInfoScheduler);
LowOpRegReg* dump_reg_reg(Mnemonic m, AtomOpCode m2, OpndSize size,
                   int reg, bool isPhysical,
                   int reg2, bool isPhysical2, LowOpndRegType type);
LowOpRegReg* dump_reg_reg_diff_types(Mnemonic m, AtomOpCode m2, OpndSize srcSize,
                   int srcReg, int isSrcPhysical, LowOpndRegType srcType,
                   OpndSize destSize, int destReg, int isDestPhysical,
                   LowOpndRegType destType);
LowOpRegReg* dump_movez_reg_reg(Mnemonic m, OpndSize size,
                        int reg, bool isPhysical,
                        int reg2, bool isPhysical2);
LowOpMemReg* dump_mem_reg(Mnemonic m, AtomOpCode m2, OpndSize size,
                   int disp, int base_reg, bool isBasePhysical,
                   MemoryAccessType mType, int mIndex,
                   int reg, bool isPhysical, LowOpndRegType type, ConstInfo** listPtr = 0);
LowOpMemReg* dump_mem_reg_noalloc(Mnemonic m, OpndSize size,
                           int disp, int base_reg, bool isBasePhysical,
                           MemoryAccessType mType, int mIndex,
                           int reg, bool isPhysical, LowOpndRegType type);
LowOpMemReg* dump_mem_scale_reg(Mnemonic m, OpndSize size,
                         int base_reg, bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical, int scale,
                         int reg, bool isPhysical, LowOpndRegType type);
LowOpRegMem* dump_reg_mem_scale(Mnemonic m, OpndSize size,
                         int reg, bool isPhysical,
                         int base_reg, bool isBasePhysical, int disp, int index_reg, bool isIndexPhysical, int scale,
                         LowOpndRegType type);
LowOpRegMem* dump_reg_mem(Mnemonic m, AtomOpCode m2, OpndSize size,
                   int reg, bool isPhysical,
                   int disp, int base_reg, bool isBasePhysical,
                   MemoryAccessType mType, int mIndex, LowOpndRegType type);
LowOpRegMem* dump_reg_mem_noalloc(Mnemonic m, OpndSize size,
                           int reg, bool isPhysical,
                           int disp, int base_reg, bool isBasePhysical,
                           MemoryAccessType mType, int mIndex, LowOpndRegType type);

LowOpImmReg* dump_imm_reg (Mnemonic m, AtomOpCode m2, OpndSize size, int imm, int reg, bool isPhysical,
        LowOpndRegType type, bool chaining = false, SwitchInfoScheduler * switchInfoScheduler = 0);

void dump_imm_reg_reg (Mnemonic op, AtomOpCode m2, int imm, OpndSize immediateSize, int sourceReg,
        bool isSourcePhysical, LowOpndRegType sourcePhysicalType, OpndSize sourceRegSize, int destReg,
        bool isDestPhysical, LowOpndRegType destPhysicalType, OpndSize destRegSize);

/**
 * @brief generate a x86 instruction that takes one immediate and one physical reg operand
 * @param m opcode mnemonic
 * @param size width of the operand
 * @param imm immediate value
 * @param reg register number
 * @param isPhysical TRUE if reg is a physical register, false otherwise
 * @param type register type
 * @return return a LowOp for immediate to register if scheduling is on, otherwise, return NULL
 */
LowOpImmReg* dump_imm_reg_noalloc(Mnemonic m, OpndSize size, int imm, int reg,
                   bool isPhysical, LowOpndRegType type);

/**
 * @brief generate a x86 instruction that takes one immediate and one physical reg operand
 * @details Differs from @see dump_imm_reg_noalloc in that this uses the ATOM_NORMAL_ALU flag
 * @param m opcode mnemonic
 * @param size width of the operand
 * @param imm immediate value
 * @param reg register number
 * @param isPhysical TRUE if reg is a physical register, false otherwise
 * @param type register type
 * @return return a LowOp for immediate to register if scheduling is on, otherwise, return NULL
 */
LowOpImmReg* dump_imm_reg_noalloc_alu(Mnemonic m, OpndSize size, int imm, int reg,
                   bool isPhysical, LowOpndRegType type);

LowOpImmMem* dump_imm_mem(Mnemonic m, AtomOpCode m2, OpndSize size,
                   int imm,
                   int disp, int base_reg, bool isBasePhysical,
                   MemoryAccessType mType, int mIndex, bool chaining);
LowOpRegMem* dump_fp_mem(Mnemonic m, AtomOpCode m2, OpndSize size, int reg,
                  int disp, int base_reg, bool isBasePhysical,
                  MemoryAccessType mType, int mIndex);
LowOpMemReg* dump_mem_fp(Mnemonic m, AtomOpCode m2, OpndSize size,
                  int disp, int base_reg, bool isBasePhysical,
                  MemoryAccessType mType, int mIndex,
                  int reg);
LowOpLabel* dump_label(Mnemonic m, OpndSize size, int imm,
               const char* label, bool isLocal);

unsigned getJmpCallInstSize(OpndSize size, JmpCall_type type);
bool lowerByteCodeJit(const Method* method, const u2* codePtr, MIR* mir, CompilationUnit_O1* cUnit);
#if defined(WITH_JIT)
bool lowerByteCodeJit(const Method* method, const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit);
void startOfBasicBlock(struct BasicBlock* bb);
extern struct BasicBlock* traceCurrentBB;
extern JitMode traceMode;

//Forward declarations
class CompilationUnit_O1;
class BasicBlock_O1;

//Start of a trace call to reset certain elements
void startOfTrace(const Method* method, int, CompilationUnit_O1*);

//End of a trace call to reset certain elements
void endOfTrace (CompilationUnit *cUnit);

//Initiates all worklists to do their work
void performWorklistWork (void);

/**
 * @brief Generates a conditional jump to taken child of current BB being generated.
 * @details Implements semantics of "if" bytecode.
 * @param takenCondition The condition for the taken branch
 * @return Returns value >= 0 when successful and negative otherwise.
 */
int generateConditionalJumpToTakenBlock (ConditionCode takenCondition);

/**
 * @brief Align a pointer to n-bytes aligned
 * @param addr The address to align
 * @param n The bytes to align it to (should be power of two)
 * @return Returns the address n-bytes aligned
 */
char* align (char* addr, int n);

/**
 * @brief Aligns the immediate operand of jmp/jcc and movl within 16B
 * @details Updates the global stream pointer to ensure that immediate falls in 16B
 * @param offset The size of mnemonic and arguments excluding the immediate
 */
void alignOffset (int offset);

/**
 * @brief Determines if operand of jump needs alignment
 * @param bb The basic block being jumped to
 * @return Whether operand alignment is needed
 */
bool doesJumpToBBNeedAlignment (BasicBlock *bb);

/**
 * @brief Generates jump to basic block. Operand of jump will be initialized when BB is lowered.
 * @param targetBlockId The id of the basic block we must jump to
 * @param immediateNeedsAligned Whether the operand of the jump must be aligned within 16 bytes
 */
void jumpToBasicBlock (int targetBlockId, bool immediateNeedsAligned = false);

/**
 * @brief Generates conditional jump to basic block. Operand of jump will be initialized when BB is lowered.
 * @param cc The condition code for the jump
 * @param targetBlockId The id of the basic block we must jump to
 * @param immediateNeedsAligned Whether the operand of the jump must be aligned within 16 bytes
 */
void condJumpToBasicBlock (ConditionCode cc, int targetBlockId, bool immediateNeedsAligned = false);

bool jumpToException(const char* target);
int codeGenBasicBlockJit(const Method* method, BasicBlock* bb, CompilationUnit_O1* cUnit);
void endOfBasicBlock(struct BasicBlock* bb);

//Used to generate native code the extended MIRs
bool handleExtendedMIR (CompilationUnit *cUnit, BasicBlock_O1 *bb, MIR *mir);

int insertChainingWorklist(int bbId, char * codeStart);
void startOfTraceO1(const Method* method, int exceptionBlockId, CompilationUnit *cUnit);

/**
 * @brief Used to obtain the offset relative to frame pointer for a given VR
 * @param vR The virtual register number for which to calculate offset
 * @return Returns the offset relative to FP.
 */
int getVirtualRegOffsetRelativeToFP (int vR);

/** @brief search globalMap to find the entry for the given label */
char* findCodeForLabel(const char* label);
/* Find a label offset given a BasicBlock index */
int getLabelOffset (unsigned int bbIdx);
#endif
int isPowerOfTwo(int imm);

/** @brief calculate the magic number and shift for a given divisor
    @details For a division by a signed integer constant, we can always
     find a magic number M and a shift S. Thus we can transform the div
     operation to a serial of multiplies, adds, shifts. This function
     is used to calcuate the magic number and shift for a given divisor.
     For the detailed desrciption and proof of
     this optimization, please refer to "Hacker's Delight", Henry S.
     Warren, Jr., chapter 10.
    @param divisor the given divisor we need to calculate
    @param magic pointer to hold the magic number
    @param shift pointer to hold the shift
*/
void calculateMagicAndShift(int divisor, int* magic, int* shift);

void move_chain_to_mem(OpndSize size, int imm,
                        int disp, int base_reg, bool isBasePhysical);
void move_chain_to_reg(OpndSize size, int imm, int reg, bool isPhysical);

bool isInMemory(int regNum, OpndSize size);
int touchEbx();
int boundCheck(int vr_array, int reg_array, bool isPhysical_array,
               int vr_index, int reg_index, bool isPhysical_index,
               int exceptionNum);
int getRelativeOffset(const char* target, bool isShortTerm, JmpCall_type type, bool* unknown,
                      OpndSize* immSize);
int getRelativeNCG(s4 tmp, JmpCall_type type, bool* unknown, OpndSize* size);
void freeAtomMem();
OpndSize estOpndSizeFromImm(int target);

//Preprocess a BasicBlock before being lowered
int preprocessingBB (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief LCG BasicBlock creator
 * @details Initializes x86 specific BasicBlock fields
 * @return newly created BasicBlock
 */
BasicBlock *dvmCompilerLCGNewBB (void);

/**
 * @brief LCG BasicBlock printing
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @param file the File in which to dump the BasicBlock
 * @param beforeMIRs is this call performed before generating the dumps for the MIRs
 */
void dvmCompilerLCGDumpBB (CompilationUnit *cUnit, BasicBlock *bb, FILE *file, bool beforeMIRs);

/**
 * @brief Used to obtain the maximum number of scratch registers that LCG backend can support
 * @return Returns the maximum number of scratch
 */
unsigned int dvmCompilerLcgGetMaxScratch (void);

/**
 * @brief Handle the invoke label
 * @param value the form of the arguments
 * @return the section label's name
 */
const char *dvmCompilerHandleInvokeArgsHeader (int value);

/**
 * @brief LCG Compilation Error Handler allocation
 * @return the new CompilationErrorHandler
 */
CompilationErrorHandler *dvmCompilerLCGNewCompilationErrorHandler (void);

void pushCallerSavedRegs(void);
void popCallerSavedRegs(void);

/**
 * @brief Applies the shuffle operation (PSHUF)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the packed elements in bytes
 * @param mask Up to 16-bits of mask bits
 * @return Returns true if generating instruction was successful
 */
bool vec_shuffle_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize,
        unsigned short mask);

/**
 * @brief Applies one of the PADDx operations depending on size
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the packed elements in bytes
 * @return Returns true if generating instruction was successful
 */
bool vec_add_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize);

/**
 * @brief Applies one of the PMULLx operations depending on size
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the packed elements in bytes
 * @return Returns true if generating instruction was successful
 */
bool vec_mul_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize);

/**
 * @brief Applies one of the PSUBx instructions depending on size.
 * @details Operation applied is minuend - subtrahend = dest stored in minuend
 * @param subtrahend The 128-bit register holding the packed values to subtract
 * @param isSubtrahendPhysical Whether the register is physical
 * @param minuend The 128-bit register holding the packed values to subtract from.
 * Result of subtraction is stored in this register.
 * @param isMinuendPhysical Whether the register is physical
 * @param vectorUnitSize The size of the packed elements in bytes
 * @return Returns true if generating instruction was successful
 */
bool vec_sub_reg_reg (int subtrahend, bool isSubtrahendPhysical, int minuend, bool isMinuendPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Applies one of the PHADDx operations depending on size
 * @param srcReg The 128-bit register containing the packed source values
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the packed elements in bytes
 * @return Returns true if generating instruction was successful
 */
bool vec_horizontal_add_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Applies one of the PHSUBx operations depending on size
 * @param srcReg The 128-bit register containing the packed source values
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the packed elements in bytes
 * @return Returns true if generating instruction was successful
 */
bool vec_horizontal_sub_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Extracts the indexed portion of the XMM into a GPR (PEXTR)
 * @param index the offset to use to extract from the XMM
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_extract_imm_reg_reg (int index, int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Used to do a bitwise and of two XMM registers (PAND)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @return Returns true if generating instruction was successful
 */
bool vec_and_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical);

/**
 * @brief Used to do a bitwise or of two XMM registers (POR)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @return Returns true if generating instruction was successful
 */
bool vec_or_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical);

/**
 * @brief Used to do a bitwise xor of two XMM registers (PXOR)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @return Returns true if generating instruction was successful
 */
bool vec_xor_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical);

/**
 * @brief Used to generate a left shift of XMM using srcReg as the register holding shift amount in bits (PSLL)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_shift_left_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical, OpndSize vectorUnitSize);

/**
 * @brief Used the generate a left shift of XMM using numBits as the immediate for shift amount (PSLL)
 * @param numBits The number of bits for the shift
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_shift_left_imm_reg (int numBits, int destReg, bool isDestPhysical, OpndSize vectorUnitSize);

/**
 * @brief Used to generate a signed right shift of XMM using srcReg as the register holding shift amount in bits (PSRA)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_signed_shift_right_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Used the generate a signed right shift of XMM using numBits as the immediate for shift amount (PSRA)
 * @param numBits The number of bits for the shift
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_signed_shift_right_imm_reg (int numBits, int destReg, bool isDestPhysical, OpndSize vectorUnitSize);

/**
 * @brief Used to generate an unsigned right shift of XMM using srcReg as the register holding shift amount in bits (PSRL)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_unsigned_shift_right_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Used the generate an unsigned right shift of XMM using numBits as the immediate for shift amount (PSRL)
 * @param numBits The number of bits for the shift
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_unsigned_shift_right_imm_reg (int numBits, int destReg, bool isDestPhysical, OpndSize vectorUnitSize);

/**
 * @brief Does a horizontal subtract reduce on XMM registers (PHSUB)
 * @param srcReg The 128-bit register containing the src value
 * @param isSrcPhysical Whether srcReg is physical
 * @param destReg The 128-bit register where the result is to be stored
 * @param isDestPhysical whether destReg is physical
 * @param vectorUnitSize The size of the chunk to extract (1-byte / 2-byte / 4-byte)
 * @return Returns true if generating instruction was successful
 */
bool vec_horizontal_sub_reg_reg (int srcReg, bool isSrcPhysical, int destReg, bool isDestPhysical,
        OpndSize vectorUnitSize);

/**
 * @brief Entry point of the LCG backend
 * @param cUnit the CompilationUnit
 * @param info the JitTranslationInfo
 */
void dvmCompilerLCGMIR2LIR (CompilationUnit *cUnit, JitTranslationInfo *info);

#endif
