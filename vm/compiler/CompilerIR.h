/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef DALVIK_VM_COMPILER_IR_H_
#define DALVIK_VM_COMPILER_IR_H_

#include "codegen/Optimizer.h"
#include "CompilerIR.h"

#ifdef ARCH_IA32
#include "CompilerUtility.h"
#include "LoopInformation.h"
#endif

typedef enum RegisterClass {
    kCoreReg,
    kFPReg,
    kX87Reg,    /**< @brief X87 style register */
    kSFPReg,    /**< @brief Single precision floating-point */
    kDFPReg,    /**< @brief Double precision floating-point */
    kAnyReg,
} RegisterClass;

typedef enum RegLocationType {
    kLocDalvikFrame = 0,
    kLocPhysReg,
    kLocRetval,          // Return region in interpState
    kLocSpill,
} RegLocationType;

typedef struct RegLocation {
    RegLocationType location:2;
    unsigned wide:1;
    unsigned fp:1;      // Hint for float/double
    u1 lowReg:6;        // First physical register
    u1 highReg:6;       // 2nd physical register (if wide)
    s2 sRegLow;         // SSA name for low Dalvik word
} RegLocation;

#define INVALID_SREG (-1)
#define INVALID_REG (0x3F)

typedef enum BBType {
    /* For coding convenience reasons chaining cell types should appear first */
    kChainingCellNormal = 0,
    kChainingCellHot,
    kChainingCellInvokeSingleton,
    kChainingCellInvokePredicted,
    kChainingCellBackwardBranch,
    kChainingCellGap,
    /* Don't insert new fields between Gap and Last */
    kChainingCellLast = kChainingCellGap + 1,
    kEntryBlock,
    kDalvikByteCode,
    kExitBlock,
    kPCReconstruction,
    kExceptionHandling,
    kCatchEntry,
    kPreBackwardBlock,
    kFromInterpreter,        /**< @brief BasicBlock representing an entry from interpreter other than entry */
} BBType;

typedef enum JitMode {
    kJitTrace = 0, // Acyclic - all instructions come from the trace descriptor
    kJitLoop,      // Cycle - trace descriptor is used as a hint
    kJitMethod,    // Whole method
} JitMode;

typedef struct ChainCellCounts {
    union {
        u1 count[kChainingCellLast]; /* include one more space for the gap # */
        u4 dummyForAlignment;
    } u;
} ChainCellCounts;

typedef struct LIR {
    int offset;
    struct LIR *next;
    struct LIR *prev;
    struct LIR *target;
} LIR;

/* Used in case of a kMirOpBoundCheck to retrieve information */
#define MIR_BOUND_CHECK_REG 0
#define MIR_BOUND_CHECK_CST 1

enum ExtendedMIROpcode {

    kMirOpFirst = kNumPackedOpcodes,

    /** @brief PHI node
      No arguments for the back-end */
    kMirOpPhi = kMirOpFirst,

    /** @brief Null and Range Up for Up Loop
      va: arrayReg, vB: idx Reg, vC: end condition,
      arg[0]: maxC, arg[1]: minC, arg[2]:branch opcode */
    kMirOpNullNRangeUpCheck,

    /** @brief Null and Range for Down Loop
      va: arrayReg, vB: idx Reg, vC: end condition,
      arg[0]: maxC, arg[1]: minC, arg[2]:branch opcode */
    kMirOpNullNRangeDownCheck,

    /** @brief Check lower bound if an index register
      va: idxReg, vb:globalMinC */
    kMirOpLowerBound,

    /** @brief Punt
      No arguments for the back end */
    kMirOpPunt,

    /** @brief Checks for validity of predicted inlining
      vB: Class object pointer
      vC: The register that holds "this" reference */
    kMirOpCheckInlinePrediction,

    /** @brief Null Check, va: objectReg */
    kMirOpNullCheck,

    /** @brief Bound Check using a constant value or invariant register:
      vA: objectReg, arg[0]=MIR_BOUND_CHECK_REG/CST
      arg[1]: indexREG or constant
     */
    kMirOpBoundCheck,

    /** @brief MIR for hint to registerize a VR:
      vA: the VR number, vB: the type using the RegisterClass enum
     */
    kMirOpRegisterize,

    /** @brief MIR to move data to a 128-bit vectorized register
       vA: destination
       args[0]~args[3]: the 128-bit data to be stored in vA
     */
    kMirOpConst128b,

    /** @brief MIR to move a 128-bit vectorized register to another
       vA: destination
       vB: source
     */
    kMirOpMove128b,

    /** @brief Packed multiply of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .* vB using vC to know the packed unit size
       vA: destination and source
       vB: source
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedMultiply,

    /** @brief Packed addition of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .+ vB using vC to know the packed unit size
       vA: destination and source
       vB: source
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedAddition,

    /** @brief Packed subtraction of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .- vB using vC to know the packed unit size
       vA: destination and source
       vB: source
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedSubtract,

    /** @brief Packed shift left of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .<< vB using vC to know the packed unit size
       vA: destination and source
       vB: immediate
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedShiftLeft,

    /** @brief Packed signed shift right of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .>> vB using vC to know the packed unit size
       vA: destination and source
       vB: immediate
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedSignedShiftRight,

    /** @brief Packed unsigned shift right of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .>>> vB using vC to know the packed unit size
       vA: destination and source
       vB: immediate
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedUnsignedShiftRight,

    /** @brief Packed bitwise and of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .& vB
       vA: destination and source
       vB: source
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedAnd,

    /** @brief Packed bitwise or of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .| vB
       vA: destination and source
       vB: source
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedOr,

    /** @brief Packed bitwise xor of units (using "." to represent unit) in two 128-bit vectorized registers: vA = vA .^ vB
       vA: destination and source
       vB: source
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedXor,

    /**
       @brief Reduce a 128-bit packed element into a single VR by taking lower bits
       @details Instruction does a horizontal addition of the different packed elements and then adds it to VR:
          vA = xmmvB + vA, size vC
       vA: destination and source VR
       vB: 128-bit source register
       vC: operands' size (2 bytes, 4 bytes)
       arg[0]: The index to use for extraction from vector register
     */
    kMirOpPackedAddReduce,

    /**
       @brief Reduce a 128-bit packed element into a single VR by taking lower bits
       vA: destination VR
       vB: 128-bit source register
       vC: operands' size (2 bytes, 4 bytes)
       arg[0]: The index to use for extraction from vector register
     */
    kMirOpPackedReduce,

    /** @brief Create a 128 bit value, with all 16 bytes / vC values equal to vB
       vA: destination 128-bit vector register
       vB: source VR
       vC: operands' size (2 bytes, 4 bytes)
     */
    kMirOpPackedSet,

    /**
     * @brief Check if creating frame for target method will cause a stack overflow.
     * @details vB holds size of managed frame for target method.
     */
    kMirOpCheckStackOverflow,

    /** @brief Last enumeration: not used except for array bounds */
    kMirOpLast,
};
#define isExtendedMir(x) ((x) >= static_cast<Opcode> (kMirOpFirst))

struct SSARepresentation;

typedef enum {
    kMIRIgnoreNullCheck = 0,
    kMIRNullCheckOnly,
    kMIRIgnoreRangeCheck,
    kMIRRangeCheckOnly,
    kMIRInlined,                        // Invoke is inlined (ie dead)
    kMIRInlinedPred,                    // Invoke is inlined via prediction
    kMIRCallee,                         // Instruction is inlined from callee
    kMIRInvokeMethodJIT,                // Callee is JIT'ed as a whole method
    kMIROptimizedAway,                  // Optimized away mir
    kMIRIgnoreBailOut,                  // Instruction is safe (no bail out from JIT code)
} MIROptimizationFlagPositons;

#define MIR_IGNORE_BAIL_OUT_CHECK       (1 << kMIRIgnoreBailOut)
#define MIR_IGNORE_NULL_CHECK           (1 << kMIRIgnoreNullCheck)
#define MIR_NULL_CHECK_ONLY             (1 << kMIRNullCheckOnly)
#define MIR_IGNORE_RANGE_CHECK          (1 << kMIRIgnoreRangeCheck)
#define MIR_RANGE_CHECK_ONLY            (1 << kMIRRangeCheckOnly)
#define MIR_INLINED                     (1 << kMIRInlined)
#define MIR_INLINED_PRED                (1 << kMIRInlinedPred)
#define MIR_CALLEE                      (1 << kMIRCallee)
#define MIR_INVOKE_METHOD_JIT           (1 << kMIRInvokeMethodJIT)
#define MIR_OPTIMIZED_AWAY              (1 << kMIROptimizedAway)

typedef struct CallsiteInfo {
    const char *classDescriptor;
    Object *classLoader;
    const Method *method;
    LIR *misPredBranchOver;
} CallsiteInfo;

/**
 * @class sInstructionColor
 * @brief The instruction color to disambiguate memory aliasing
 */
typedef struct sInstructionColor {
    unsigned int aliasingColor;         /**< @brief Aliasing color */
    MIR *prev;                          /**< @brief Previous instruction in the color */
    MIR *next;                          /**< @brief Next instruction in the color */
}SInstructionColor;

/**
 * @brief Used to keep track of nesting level of a bytecode
 */
typedef struct NestedMethod {
    /**
     * @brief Constructor for nested method information. Sets up parent as null.
     * @param source The source method
     */
    NestedMethod (const Method *source) :
            parent (0), sourceMethod (source)
    {
    }

    NestedMethod *parent;       //!< The nesting information of parent. If 0, the sourceMethod matches cUnit's method
    const Method *sourceMethod; //!< The source method of the bytecode
} NestedMethod;

typedef struct MIR {
    DecodedInstruction dalvikInsn;
    unsigned int width;
    unsigned int offset;
    unsigned int localValueNumber;      //Local value number
    unsigned int topologicalOrder;      //Topological order of the MIR in the entire CFG
    BasicBlock *bb;                     //BasicBlock containing the MIR
    SInstructionColor color;            //Instruction color
    bool invariant;                     //Is the MIR an invariant for the loop?
    MIR *copiedFrom;                    //Base MIR this MIR was copied from
    struct MIR *prev;
    struct MIR *next;
    struct SSARepresentation *ssaRep;
    int OptimizationFlags;
    int seqNum;

    /**
     * @brief Used to keep track of the nesting level of the MIR
     */
    NestedMethod nesting;

    /**
     * @brief Used to keep track of renaming offset.
     * @details For example if v1 was renamed to v3, this holds value of 2 (3 - 1).
     * This field is similar to CompilationUnit::registerWindowShift in that it captures renaming
     * of virtual register. However, this field is specific to the instruction itself because
     * depending on source method, different MIRs get renamed differently.
     */
    int virtualRegRenameOffset;

    union {
        // Used by the inlined insn from the callee to find the mother method
        const Method *calleeMethod;
        // Used by the inlined invoke to find the class and method pointers
        CallsiteInfo *callsiteInfo;
    } meta;
} MIR;

struct BasicBlockDataFlow;

/* For successorBlockList */
typedef enum BlockListType {
    kNotUsed = 0,
    kCatch,
    kPackedSwitch,
    kSparseSwitch,
} BlockListType;

/**
 * @brief Used to provide directives which refer to a BasicBlock's children.
 */
typedef enum ChildBlockType {
    kChildTypeFallthrough,       //!< @brief Used for the fallthrough child
    kChildTypeTaken,             //!< @brief Used for the taken child
    kChildTypeSwitchOrException, //!< @brief Used whenever there are multiple children due to either exception or switch
} ChildBlockType;

typedef struct BasicBlock {
    int id;
    bool visited;
    bool peeled;                        // Is the BasicBlock for a peeled iteration?
    struct {                            // Define behavior in a loop
        bool walkForward;               // Do we go to children to traverse the loop?
        bool walkBackward;              // Do we go to predecessor to traverse the loop?
        BasicBlock *relativeTo;
    } loopTraversalType;
    BitVector *requestWriteBack;        //Request a write back from the BE
    bool hidden;
    unsigned int startOffset;
    const Method *containingMethod;     // For blocks from the callee
    BBType blockType;
    bool needFallThroughBranch;         // For blocks ended due to length limit
    bool isFallThroughFromInvoke;       // True means the block needs alignment
    MIR *firstMIRInsn;
    MIR *lastMIRInsn;
    struct BasicBlock *fallThrough;
    struct BasicBlock *taken;
    struct BasicBlock *iDom;            // Immediate dominator
    struct BasicBlockDataFlow *dataFlowInfo;
    BitVector *predecessors;
    BitVector *dominators;
    BitVector *iDominated;              // Set nodes being immediately dominated
    BitVector *domFrontier;             // Dominance frontier
    struct {                            // For one-to-many successors like
        BlockListType blockListType;    // switch and exception handling
        GrowableList blocks;
    } successorBlockList;
    unsigned int topologicalOrder;      //Topological order of the BB's first instruction in the whole CFG
} BasicBlock;

/*
 * The "blocks" field in "successorBlockList" points to an array of
 * elements with the type "SuccessorBlockInfo".
 * For catch blocks, key is type index for the exception.
 * For swtich blocks, key is the case value.
 */
typedef struct SuccessorBlockInfo {
    BasicBlock *block;
    int key;
} SuccessorBlockInfo;

/**
 * @brief Used to iterate through the children of a basic block.
 * @details If a child of basic block is updated, this iterator does not ensure to visit it
 * if it already visited its location once.
 */
class ChildBlockIterator
{
public:
    /**
     * @brief Constructs a child iterator
     * @param bb The basic whose children we need to iterate through.
     */
    ChildBlockIterator (BasicBlock *bb);

    /**
     * @brief Used to obtain a pointer to unvisited child
     * @return Returns pointer to an unvisited child. When all children are visited it returns null.
     */
    BasicBlock **getNextChildPtr (void);

private:
    /**
     * @brief Used to keep track of the basic block whose children we are visiting
     */
    BasicBlock *basicBlock;

    /**
     * @brief Whether we visited fallthrough child
     */
    bool visitedFallthrough;

    /**
     * @brief Whether we visited taken child
     */
    bool visitedTaken;

    /**
     * @brief Whether we have blocks to visit in the successor list
     */
    bool haveSuccessors;

    /**
     * @brief Used to iterate through the block's successor list
     */
    GrowableListIterator successorIter;
};

//Forward Declarations
class CompilationErrorHandler;
struct LoopAnalysis;
class LoopInformation;
struct RegisterPool;

typedef enum AssemblerStatus {
    kSuccess,
    kRetryAll,
    kRetryHalve
} AssemblerStatus;

typedef struct sPhiVectors {
    BitVector *phiBlocks;
    BitVector *tmpBlocks;
    BitVector *inputBlocks;
    int size;
}SPhiVectors;


typedef struct CompilationUnit {
    int numInsts;
    int numBlocks;
    GrowableList blockList;
    const Method *method;
#ifdef ARCH_IA32
    int exceptionBlockId;               /**< @brief The block corresponding to exception handling */
    struct ConstInfo *constListHead;    /**< @brief pointer to head of the list of 64 bit constants */
#endif
    unsigned int maximumRegisterization;    /**< @brief Maximum registerization to be accepted */
    void *passData;                         /**< @brief Pass data is used to transfer data throughout a Pass */
    bool printPass;                         /**< @brief Whether pass verbosity should be enabled. Useful for understanding pass decisions */
    void *walkData;                         /**< @brief Walk data when using the dispatcher */
    struct sUsedChain* globalDefUseChain;   /**< @brief The global def-use chain, this contains all def-use chains for reuse when recalculating */
    const JitTraceDescription *traceDesc;
    LIR *firstLIRInsn;
    LIR *lastLIRInsn;
    LIR *literalList;                   // Constants
    LIR *classPointerList;              // Relocatable
    int numClassPointers;
    LIR *chainCellOffsetLIR;
    GrowableList pcReconstructionList;
    int headerSize;                     // bytes before the first code ptr
    int dataOffset;                     // starting offset of literal pool
    int totalSize;                      // header + code size
    AssemblerStatus assemblerStatus;    // Success or fix and retry
    int assemblerRetries;               // How many times tried to fix assembly
    unsigned char *codeBuffer;
    void *baseAddr;
    bool printMe;
    bool allSingleStep;
    bool hasClassLiterals;              // Contains class ptrs used as literals
    bool hasLoop;                       // Contains a loop
    bool hasInvoke;                     // Contains an invoke instruction
    bool heapMemOp;                     // Mark mem ops for self verification
    bool usesLinkRegister;              // For self-verification only
    int profileCodeSize;                // Size of the profile prefix in bytes
    int numChainingCells[kChainingCellGap];
    LIR *firstChainingLIR[kChainingCellGap];
    LIR *chainingCellBottom;
    struct RegisterPool *regPool;
    int optRound;                       // round number to tell an LIR's age
    jmp_buf *bailPtr;
    JitInstructionSetType instructionSet;
    /* Number of total regs used in the whole cUnit after SSA transformation */
    int numSSARegs;
    /* Map SSA reg i to the Dalvik[15..0]/Sub[31..16] pair. */
    GrowableList *ssaToDalvikMap;

    /* The following are new data structures to support SSA representations */
    /* Map original Dalvik reg i to the SSA[15..0]/Sub[31..16] pair */
    int *dalvikToSSAMap;                // length == method->registersSize
    int *ssaSubScripts;                 // Subscript definition counters for each Dalvik Register
    BitVector *isConstantV;             // length == numSSAReg

    /* Data structure for loop analysis and optimizations */
#ifndef ARCH_IA32
    struct LoopAnalysis *loopAnalysis;
#else
    LoopInformation *loopInformation;
#endif

    /* Error framework */
    CompilationErrorHandler *errorHandler;

    /* Map SSA names to location */
    RegLocation *regLocation;
    int sequenceNumber;

    /*
     * Set to the Dalvik PC of the switch instruction if it has more than
     * MAX_CHAINED_SWITCH_CASES cases.
     */
    const u2 *switchOverflowPad;

    JitMode jitMode;
    int numReachableBlocks;

    /**
     * @brief Keeps track of number of registers in the cUnit.
     * @details Takes into account method->registersSize, inlined registers, and scratch registers.
     */
    int numDalvikRegisters;

    /**
     * @brief Used to keep track of the number of pending scratch registers
     * which are not yet counted in field numDalvikRegisters.
     */
    unsigned int pendingScratchRegisters;

    /**
     * @brief Used to keep track of the number of scratch registers currently being used.
     */
    unsigned int numUsedScratchRegisters;

    BasicBlock *entryBlock;
    BasicBlock *exitBlock;
    BasicBlock *puntBlock;              // punting to interp for exceptions
    BasicBlock *backChainBlock;         // for loop-trace
    BasicBlock *curBlock;
    BasicBlock *nextCodegenBlock;       // for extended trace codegen
    GrowableList dfsOrder;
    GrowableList domPostOrderTraversal;
    BitVector *tryBlockAddr;
    int defBlockMatrixSize;             // Size of defBlockMatrix
    BitVector **defBlockMatrix;         // numDalvikRegister x numBlocks
    BitVector *tempBlockV;

    BitVector *tempDalvikRegisterV;     //!< Temporary vector used during dataflow to store dalvik registers
    BitVector *tempSSARegisterV;        //!< Temporary vector used during dataflow to store SSA registers

    SPhiVectors phi;

    bool printSSANames;
    void *blockLabelList;
    bool quitLoopMode;                  // cold path/complex bytecode

    std::map<int, int> *constantValues; // Constant values map using the ssa register as a key

    /**
     * @brief All virtual registers in compilation unit are relative to frame pointer modified with this offset.
     * @details The frame pointer is designed to point to place on stack where a method's virtual registers
     * exist. But when inlining methods, we bring in even more virtual registers. Since we create a frame for
     * callee as well, those VRs are negative direction (in direction of stack growth). But in order to allow
     * all algorithms to work, we rename all VRs to have positive values relative to frame pointer. But we need
     * to specify the modified on the frame pointer itself. For example if this holds a value of 2, if you see v3
     * in the compilation unit after rename, then if the MIR's source method is not inlined, then v3 was actually
     * originally v1. The MIR::virtualRegRenameOffset would capture 2 as well in this case. But for MIRs from inlined
     * methods, the MIR::virtualRegRenameOffset may be different. The distinction is that registerWindowShift captures
     * the global renaming property for the frame pointer which applies for all virtual registers in cUnit.
     */
    int registerWindowShift;

    /**
     * @brief Keeps track of the SSA numbers associated with degenerate PHIs
     * @details A degenerate PHI node is a PHI with a single operand, i.e. a PHI
     * that has only one reaching definition.  Degenerate PHIs are removed from
     * the IR, and references to them are eventually replaced by references to
     * the single reaching definition.  The degeneratePhiMap maintains an
     * association between the SSA number of the PHI (the key) and the SSA
     * number of the single reaching def (the value) from the time when the
     * degenerate PHIs are removed to the time when the references to them
     * are updated.
     */
    std::map<int, int> *degeneratePhiMap;

    /**
     * @brief Keeps track of code blocks, to facilitate jit verbose printing
     */
    std::vector<std::pair<BBType, char*> >  *code_block_table;

    /**
     * @brief The status of CFG walking. It is 'true' if all blocks were visited.
     */
    bool predecessorFirstTraversalOK;
}CompilationUnit;

#if defined(WITH_SELF_VERIFICATION)
#define HEAP_ACCESS_SHADOW(_state) cUnit->heapMemOp = _state
#else
#define HEAP_ACCESS_SHADOW(_state)
#endif

//Helper macro to only call logger in case cUnit->printPass field is true
#define PASS_LOG(LOG_TYPE, cUnit, ...) \
    do { \
        if (cUnit->printPass == true) { \
            LOG_TYPE (__VA_ARGS__); \
        } \
    } while (false)

/**
 * @brief Creates a new block and adds its to the block list.
 * @details If a cUnit block list is provided, this function does not ensure to update cUnit->numBlocks.
 * @param blockList The block list to which we should add to.
 * @param blockType The block type to create.
 * @return Returns the newly created block.
 */
BasicBlock *dvmCompilerNewBBinList (GrowableList &blockList, BBType blockType);

/**
 * @brief Allocates a new basic block and adds it to the compilation unit.
 * @details Automatically generates an id for the BB. It also ensures to update cUnit->numBlocks.
 * @param cUnit Compilation Unit
 * @param blockType Type of block we want to create
 * @return Returns the newly created BB
 */
BasicBlock *dvmCompilerNewBBinCunit (CompilationUnit *cUnit, BBType blockType);

/**
 * @brief Used to hide a basic block from block list. It ensures that all CFG links to this block are severed.
 * @details Currently we don't have mechanism to remove from a growable list, so we fake that we remove it.
 * @param blockList The block list from which to hide block.
 * @param bb The basic block to hide.
 */
void dvmCompilerHideBasicBlock (GrowableList &blockList, BasicBlock *bb);

/**
 * @brief Used for allocating a new MIR.
 * @return Returns the newly created MIR.
 */
MIR *dvmCompilerNewMIR(void);

MIR *dvmCompilerNewMoveMir (int sourceVR, int destVR, bool wide);

void dvmCompilerAppendMIR(BasicBlock *bb, MIR *mir);

void dvmCompilerPrependMIR(BasicBlock *bb, MIR *mir);

/**
 * @brief Insert an MIR instruction before the specified MIR. If no MIR to insert before is provided, the
 * insertion happens at beginning of block.
 * @param bb the Basic Block where the insertion is to happen
 * @param mirToInsertBefore the specified mir to insert before (if null, insertion happens at beginning of basic block)
 * @param newMIR the mir to insert
 */
void dvmCompilerInsertMIRBefore (BasicBlock *bb, MIR *mirToInsertBefore, MIR *newMIR);

/**
 * @brief Insert an MIR instruction after the specified MIR. If no MIR to insert after is provided, the
 * insertion happens at end of block.
 * @param bb the Basic Block where the insertion is to happen
 * @param mirToInsertAfter the specified mir to insert after (if null, insertion happens at end of basic block)
 * @param newMIR the mir to insert
 */
void dvmCompilerInsertMIRAfter (BasicBlock *bb, MIR *mirToInsertAfter, MIR *newMIR);

/**
 * @brief Moves a chain of MIRs after mirToInsertAfter. If no MIR to insert after is provided, the
 * insertion happens at end of block. All MIRs linked forward to mirChainStart are moved.
 * @param bb the Basic Block where the insertion is to happen
 * @param mirToInsertAfter the specified mir to insert after (if null, insertion happens at end of basic block)
 * @param mirChainStart The beginning of the MIR chain to be moved
 */
void dvmCompilerMoveLinkedMIRsAfter (BasicBlock *bb, MIR *mirToInsertAfter, MIR *mirChainStart);

/**
 * @brief Moves a chain of MIRs before mirToInsertBefore. If no MIR to insert before is provided, the
 * insertion happens at beginning of block. All MIRs linked forward to mirChainStart are moved.
 * @param bb the Basic Block where the insertion is to happen
 * @param mirToInsertBefore the specified mir to insert before (if null, insertion happens at beginning of basic block)
 * @param mirChainStart The beginning of the MIR chain to be moved
 */
void dvmCompilerMoveLinkedMIRsBefore (BasicBlock *bb, MIR *mirToInsertBefore, MIR *mirChainStart);

void dvmCompilerAppendLIR(CompilationUnit *cUnit, LIR *lir);

void dvmCompilerInsertLIRBefore(LIR *currentLIR, LIR *newLIR);

void dvmCompilerInsertLIRAfter(LIR *currentLIR, LIR *newLIR);

void dvmCompilerAbort(CompilationUnit *cUnit);

/* Remove a MIR from a BasicBlock: returns true on success */
bool dvmCompilerRemoveMIR (BasicBlock *bb, MIR *mir);
/* Remove a MIR using its internal BasicBlock pointer */
bool dvmCompilerRemoveMIR (MIR *mir);

void dvmCompilerAddInstructionsToBasicBlock (BasicBlock *bb, const std::vector<MIR *> &toAdd);
void dvmCompilerPrependInstructionsToBasicBlock (BasicBlock *bb, const std::vector<MIR *> &toAdd);

void dvmCompilerAddInstructionsToBasicBlocks (CompilationUnit *cUnit, BitVector *basicBlocks,
        const std::vector<MIR *> &toAdd);
void dvmCompilerPrependInstructionsToBasicBlocks (CompilationUnit *cUnit, BitVector *basicBlocks,
        const std::vector<MIR *> &toAdd);

/**
 * @brief Used to replace one of the children of a block with a new child.
 * @param newChild The new child for this basic block.
 * @param parent The basic block whose child should be replaced.
 * @param oldChild The child which should be replaced
 * @return Returns whether replacing child succeeds.
 */
bool dvmCompilerReplaceChildBasicBlock (BasicBlock *newChild, BasicBlock *parent, BasicBlock *oldChild);

/**
 * @brief Used to replace one of the children of a block with a new child.
 * @details Guaranteed to succeed if the child type is either fallthrough or taken.
 * @param newChild The new child for this basic block.
 * @param parent The basic block whose child should be replaced.
 * @param childType The type of the child which should be replaced. Fallthrough replaced by default.
 * @return Returns whether replacing child succeeds.
 */
bool dvmCompilerReplaceChildBasicBlock (BasicBlock *newChild, BasicBlock *parent, ChildBlockType childType =
        kChildTypeFallthrough);

/**
 * @brief Used to insert a basic block between a parent and its child.
 * @param newBlock The block to insert in between. The child becomes its fallthrough.
 * @param parent The parent after which to insert the new block.
 * @param child The child before which the new block should be inserted.
 * @param updateChildPredecessors Whether or not to update the child predecessor to include the newBlock.
 * The user should set this to false when iterating through child's predecessor and would like to prevent
 * the iterator to go through the new block. When this option is used, the client must ensure itself
 * that newBlock becomes child's predecessor. (Option set to true by default)
 * @return Returns whether insertion was successful.
 */
bool dvmCompilerInsertBasicBlockBetween (BasicBlock *newBlock, BasicBlock *parent, BasicBlock *child,
        bool updateChildPredecessors = true);

/* Copy a BasicBlock */
BasicBlock *dvmCompilerCopyBasicBlock (CompilationUnit *cUnit, const BasicBlock *old);

/* Update the predecessor information of old and new BB */
void dvmCompilerUpdatePredecessors(BasicBlock *parent, BasicBlock *oldChild, BasicBlock *newChild);

/* Copy a MIR */
MIR *dvmCompilerCopyMIR (MIR *orig);

/**
 * @brief Get the dalvik PC for a MIR
 * @param cUnit The CompilationUnit
 * @param mir The bytecode whose PC to determine
 * @return Returns the dalvik PC.
 */
u2 * dvmCompilerGetDalvikPC (CompilationUnit *cUnit, const MIR *mir);

/**
 * @brief Determines if ssa reg define is live out of current basic block.
 * @param cUnit The compilation unit.
 * @param bb The basic block we want to look at.
 * @param ssaReg The ssa representation of the dalvik register whose define
 * we want to look for.
 * @return Returns whether or not the ssa register is the last define of that
 * virtual register in the current basic block. If no virtual register define
 * is found, then simply returns true.
 */
bool dvmCompilerIsSsaLiveOutOfBB (CompilationUnit *cUnit, BasicBlock *bb, int ssaReg);

/**
 * @brief Is the instruction's results invariant: depends on opcode and operands
 * @param mir the MIR instruction
 * @param variants the currently known variants of the loop
 * @param skipUses The number of uses we want to skip when making the invariant decision
 * @return whether or not the operation is invariant or not
 */
bool dvmCompilerUsesAreInvariant (const MIR *mir, const BitVector *variants, int skipUses = 0);

/**
 * @brief Check if an instruction is invariant or not
 * @param elem the instruction
 * @param variants the variant BitVector
 * @param skipUses The number of uses we want to skip when making the variant decision
 * @return whether the instruction is variant
 */
bool dvmCompilerCheckVariant (MIR *elem, BitVector *variants, int skipUses = 0);

/* Debug Utilities */
void dvmCompilerDumpCompilationUnit(CompilationUnit *cUnit);

/**
 * @brief Used to rewrite a MIR's def register with a new register.
 * @details This is guaranteed to not rewrite unless it will succeed.
 * @param mir the MIR to rewrite
 * @param oldVR the old VR that we want to rewrite
 * @param newVR the new VR we want to use
 * @param shouldRewriteUses Since we are changing the def register, when this is set to true
 * it will walk to the uses and change those to match the new register.
 * @param remainInSameBB should the rewrite only remain in the same BasicBlock (default: false)
 * @return Returns true if successfully rewrote with the given rewrite rules.
 */
bool dvmCompilerRewriteMirDef (MIR *mir, int oldVR, int newVR, bool shouldRewriteUses = true, bool remainInSameBB = false);

/**
 * @brief Rewrite the uses of def specified by a MIR.
 * @details This is guaranteed to not rewrite unless it will succeed.
 * @param mir The MIR to rewrite uses of its def
 * @param oldVR the old VR that we want to rewrite
 * @param newVR the new VR we want to use
 * @param remainInSameBB should the rewrite only remain in the same BasicBlock (default: false)
 * @return whether the rewrite was successful
 */
bool dvmCompilerRewriteMirUses (MIR *mir, int oldVR, int newVR, bool remainInSameBB = false);

/**
 * @brief Used to rewrite the virtual register numbers of a MIR
 * @details This is guaranteed to not rewrite unless it will succeed.
 * @param dalvikInsn The dalvik instruction to update
 * @param oldToNew The map of old to new virtual registers
 * @param onlyUses Flag on whether only uses should be updated
 * @return Returns whether the rewrite was successful
 */
bool dvmCompilerRewriteMirVRs (DecodedInstruction &dalvikInsn, const std::map<int, int> &oldToNew, bool onlyUses = true);

/**
 * @brief Given a dalvik Opcode or an extended opcode, it returns the flags.
 * @param opcode The opcode for which to get flags
 * @return Returns the flags for the specified opcode.
 */
OpcodeFlags dvmCompilerGetOpcodeFlags (int opcode);

/**
 * @brief Given a dalvik Opcode or an extended opcode, it returns the name its name.
 * @param opcode The opcode for which to get name
 * @return Returns the name for the specified opcode.
 */
const char* dvmCompilerGetOpcodeName (int opcode);

#endif  // DALVIK_VM_COMPILER_IR_H_
