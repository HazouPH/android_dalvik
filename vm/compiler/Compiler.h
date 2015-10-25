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

#ifndef DALVIK_VM_COMPILER_H_
#define DALVIK_VM_COMPILER_H_

#include <setjmp.h>
#include "Thread.h"

/*
 * Uncomment the following to enable JIT signature breakpoint
 * #define SIGNATURE_BREAKPOINT
 */

#define COMPILER_WORK_QUEUE_SIZE        100
#define COMPILER_IC_PATCH_QUEUE_SIZE    64
#define COMPILER_PC_OFFSET_SIZE         100

/* Architectural-independent parameters for predicted chains */
#define PREDICTED_CHAIN_CLAZZ_INIT       0
#define PREDICTED_CHAIN_METHOD_INIT      0
#define PREDICTED_CHAIN_COUNTER_INIT     0
/* A fake value which will avoid initialization and won't match any class */
#define PREDICTED_CHAIN_FAKE_CLAZZ       0xdeadc001
/* Has to be positive */
#define PREDICTED_CHAIN_COUNTER_AVOID    0x7fffffff
/* Rechain after this many misses - shared globally and has to be positive */
#define PREDICTED_CHAIN_COUNTER_RECHAIN  8192

#define COMPILER_TRACED(X)
#define COMPILER_TRACEE(X)
#define COMPILER_TRACE_CHAINING(X)

/* Macro to change the permissions applied to a chunk of the code cache */
#define PROTECT_CODE_CACHE_ATTRS       (PROT_READ | PROT_EXEC)
#define UNPROTECT_CODE_CACHE_ATTRS     (PROT_READ | PROT_EXEC | PROT_WRITE)

/* Macro to change the permissions applied to a chunk of the data cache */
#define PROTECT_DATA_CACHE_ATTRS       (PROT_READ)
#define UNPROTECT_DATA_CACHE_ATTRS     (PROT_READ | PROT_WRITE)

/* Acquire the lock before removing PROT_WRITE from the specified mem region */
#define UNPROTECT_CODE_CACHE(addr, size)                                       \
    {                                                                          \
        dvmLockMutex(&gDvmJit.codeCacheProtectionLock);                        \
        mprotect((void *) (((intptr_t) (addr)) & ~gDvmJit.pageSizeMask),       \
                 (size) + (((intptr_t) (addr)) & gDvmJit.pageSizeMask),        \
                 (UNPROTECT_CODE_CACHE_ATTRS));                                \
    }

/* Add the PROT_WRITE to the specified memory region then release the lock */
#define PROTECT_CODE_CACHE(addr, size)                                         \
    {                                                                          \
        mprotect((void *) (((intptr_t) (addr)) & ~gDvmJit.pageSizeMask),       \
                 (size) + (((intptr_t) (addr)) & gDvmJit.pageSizeMask),        \
                 (PROTECT_CODE_CACHE_ATTRS));                                  \
        dvmUnlockMutex(&gDvmJit.codeCacheProtectionLock);                      \
    }

/* Acquire the lock before removing PROT_WRITE from the specified mem region */
#define UNPROTECT_DATA_CACHE(addr, size)                                       \
    {                                                                          \
        dvmLockMutex(&gDvmJit.dataCacheProtectionLock);                        \
        mprotect((void *) (((intptr_t) (addr)) & ~gDvmJit.pageSizeMask),       \
                 (size) + (((intptr_t) (addr)) & gDvmJit.pageSizeMask),        \
                 (UNPROTECT_DATA_CACHE_ATTRS));                                \
    }

/* Add the PROT_WRITE to the specified memory region then release the lock */
#define PROTECT_DATA_CACHE(addr, size)                                         \
    {                                                                          \
        mprotect((void *) (((intptr_t) (addr)) & ~gDvmJit.pageSizeMask),       \
                 (size) + (((intptr_t) (addr)) & gDvmJit.pageSizeMask),        \
                 (PROTECT_DATA_CACHE_ATTRS));                                  \
        dvmUnlockMutex(&gDvmJit.dataCacheProtectionLock);                      \
    }

#define SINGLE_STEP_OP(opcode)                                                 \
    (gDvmJit.includeSelectedOp !=                                              \
     ((gDvmJit.opList[opcode >> 3] & (1 << (opcode & 0x7))) != 0))

typedef enum JitInstructionSetType {
    DALVIK_JIT_NONE = 0,
    DALVIK_JIT_ARM,
    DALVIK_JIT_THUMB,
    DALVIK_JIT_THUMB2,
    DALVIK_JIT_IA32,
    DALVIK_JIT_MIPS
} JitInstructionSetType;

/* Description of a compiled trace. */
typedef struct JitTranslationInfo {
    void *codeAddress;
    JitInstructionSetType instructionSet;
    int profileCodeSize;
    bool discardResult;         // Used for debugging divergence and IC patching
    bool methodCompilationAborted;  // Cannot compile the whole method
    Thread *requestingThread;   // For debugging purpose
    int cacheVersion;           // Used to identify stale trace requests
} JitTranslationInfo;

typedef enum WorkOrderKind {
    kWorkOrderInvalid = 0,      // Should never see by the backend
    kWorkOrderMethod = 1,       // Work is to compile a whole method
    kWorkOrderTrace = 2,        // Work is to compile code fragment(s)
    kWorkOrderTraceDebug = 3,   // Work is to compile/debug code fragment(s)
    kWorkOrderProfileMode = 4,  // Change profiling mode
} WorkOrderKind;

typedef struct CompilerWorkOrder {
    const u2* pc;
    WorkOrderKind kind;
    void* info;
    JitTranslationInfo result;
    jmp_buf *bailPtr;
} CompilerWorkOrder;

/* Chain cell for predicted method invocation */
typedef struct PredictedChainingCell {
    u4 branch;                  /* Branch to chained destination */
#ifdef __mips__
    u4 delay_slot;              /* nop goes here */
#elif defined(ARCH_IA32)
    u4 branch2;                 /* IA32 branch instr may be > 32 bits */
#endif
    const ClassObject *clazz;   /* key for prediction */
    const Method *method;       /* to lookup native PC from dalvik PC */
    const ClassObject *stagedClazz;   /* possible next key for prediction */
} PredictedChainingCell;

/* Work order for inline cache patching */
typedef struct ICPatchWorkOrder {
    PredictedChainingCell *cellAddr;    /* Address to be patched */
    PredictedChainingCell cellContent;  /* content of the new cell */
    const char *classDescriptor;        /* Descriptor of the class object */
    Object *classLoader;                /* Class loader */
    u4 serialNumber;                    /* Serial # (for verification only) */
} ICPatchWorkOrder;

/*
 * Trace description as will appear in the translation cache.  Note
 * flexible array at end, as these will be of variable size.  To
 * conserve space in the translation cache, total length of JitTraceRun
 * array must be recomputed via seqential scan if needed.
 */
typedef struct JitTraceDescription {
    const Method* method;
    JitTraceRun trace[0];       // Variable-length trace descriptors
} JitTraceDescription;

typedef enum JitMethodAttributes {
    kIsCallee = 0,      /* Code is part of a callee (invoked by a hot trace) */
    kIsHot,             /* Code is part of a hot trace */
    kIsLeaf,            /* Method is leaf */
    kIsEmpty,           /* Method is empty */
    kIsThrowFree,       /* Method doesn't throw */
    kIsGetter,          /* Method fits the getter pattern */
    kIsSetter,          /* Method fits the setter pattern */
    kCannotCompile,     /* Method cannot be compiled */
    kCannotInline,      /* Method cannot be inlined */
} JitMethodAttributes;

#define METHOD_IS_CALLEE        (1 << kIsCallee)
#define METHOD_IS_HOT           (1 << kIsHot)
#define METHOD_IS_LEAF          (1 << kIsLeaf)
#define METHOD_IS_EMPTY         (1 << kIsEmpty)
#define METHOD_IS_THROW_FREE    (1 << kIsThrowFree)
#define METHOD_IS_GETTER        (1 << kIsGetter)
#define METHOD_IS_SETTER        (1 << kIsSetter)
#define METHOD_CANNOT_COMPILE   (1 << kCannotCompile)
#define METHOD_CANNOT_INLINE    (1 << kCannotInline)

/* Vectors to provide optimization hints */
typedef enum JitOptimizationHints {
    kJitOptNoLoop = 0,          // Disable loop formation/optimization
} JitOptimizationHints;

#define JIT_OPT_NO_LOOP         (1 << kJitOptNoLoop)

/* Customized node traversal orders for different needs */
typedef enum DataFlowAnalysisMode {
    kAllNodes = 0,              // All nodes
    kReachableNodes,            // All reachable nodes
    kPreOrderDFSTraversal,      // Depth-First-Search / Pre-Order
    kPostOrderDFSTraversal,     // Depth-First-Search / Post-Order
    kPostOrderDOMTraversal,     // Dominator tree / Post-Order
    kBreadthFirstTraversal,     // Breadth-First Traversal
    kAllNodesAndNew,            // All nodes and new added during traversal
    kPredecessorsFirstTraversal,// Predecessors-First Traversal
} DataFlowAnalysisMode;

typedef struct CompilerMethodStats {
    const Method *method;          // Used as hash entry signature
    int dalvikSize;                // # of bytes for dalvik bytecodes
    int compiledDalvikSize;        // # of compiled dalvik bytecodes
    int nativeSize;                // # of bytes for produced native code
    int attributes;                // attribute vector
    unsigned int numBytecodes;     // # of dalvik bytecodes
} CompilerMethodStats;

struct CompilationUnit;
struct BasicBlock;
struct SSARepresentation;
struct GrowableList;
struct JitEntry;
struct MIR;
#ifdef ARCH_IA32
class LoopInformation;
#endif

#ifdef ARCH_IA32
bool dvmCompilerDataStructureSizeCheck(int);
#endif
bool dvmCompilerSetupCodeAndDataCache(void);
bool dvmCompilerArchInit(void);
void dvmCompilerArchDump(void);
bool dvmCompilerStartup(void);
void dvmCompilerShutdown(void);
void dvmCompilerForceWorkEnqueue(const u2* pc, WorkOrderKind kind, void* info);
bool dvmCompilerWorkEnqueue(const u2* pc, WorkOrderKind kind, void* info);
void *dvmCheckCodeCache(void *method);
CompilerMethodStats *dvmCompilerAnalyzeMethodBody(const Method *method,
                                                  bool isCallee);

/**
 * @brief Used to split a basic block into two, thus creating a new BB in the cUnit.
 * @param blockList The list of basic blocks to update
 * @param mirToSplitAt The mir at which the split should happen. This MIR becomes part of newly
 * created bottom block. This should not be null but if it is, the block returned is null.
 * @param origBlock The original block which to split.
 * @param immedPredBlockP Updated to contain the newly added block (may be null).
 * @return Returns the newly added block that was split from original (may return null).
 */
BasicBlock *dvmCompilerSplitBlock (GrowableList *blockList, MIR *mirToSplitAt, BasicBlock *origBlock,
        BasicBlock **immedPredBlockP = 0);

/**
 * @brief Decodes methods and creates control flow graph for it with single entry and single exit.
 * @param method The method to decode
 * @param blockList Updated by function to contain the newly added blocks
 * @param entry Updated by function to point to entry block
 * @param exit Updated by function to point to exit block
 * @param tryBlockAddr Bit vector of method offsets in try block (may be null)
 * @param bytecodeGate A function that looks at decoded instruction and make a decision about CFG building
 * @return Returns whether we successfully built CFG
 */
bool dvmCompilerBuildCFG (const Method *method, GrowableList *blockList, BasicBlock **entry = 0, BasicBlock **exit = 0,
        BitVector *tryBlockAddr = 0, bool (*bytecodeGate) (const Method *, const DecodedInstruction *, const char **) = 0);

bool dvmCompileMethod(const Method *method, JitTranslationInfo *info);
bool dvmCompileTrace(JitTraceDescription *trace, int numMaxInsts,
                     JitTranslationInfo *info, jmp_buf *bailPtr, int optHints);
void dvmCompilerDumpStats(void);
void dvmCompilerDrainQueue(void);
void dvmJitUnchainAll(void);
void dvmJitScanAllClassPointers(void (*callback)(void *ptr));
void dvmCompilerSortAndPrintTraceProfiles(void);
void dvmCompilerPerformSafePointChecks(void);

/**
 * @brief Walks through the basic blocks looking for BB's with instructions in order to try to possibly inline an invoke
 * @param cUnit The compilation unit
 * @param info The description of the trace
 */
void dvmCompilerInlineMIR (CompilationUnit *cUnit, JitTranslationInfo *info);

void dvmInitializeSSAConversion(struct CompilationUnit *cUnit);
int dvmConvertSSARegToDalvik(const struct CompilationUnit *cUnit, int ssaReg);
//Extract the subscript from a SSA register
unsigned int dvmExtractSSASubscript (const CompilationUnit *cUnit, int ssaReg);
//Extract the register from a SSA register
unsigned int dvmExtractSSARegister (const CompilationUnit *cUnit, int ssaReg);
bool dvmCompilerLoopOpt(struct CompilationUnit *cUnit);

/**
 * @brief Insert the backward chaining BasicBlock
 * @param cUnit the CompilationUnit
 * @return whether the process was a success or not
 */
bool dvmCompilerInsertBackwardChaining(struct CompilationUnit *cUnit);
void dvmCompilerNonLoopAnalysis(struct CompilationUnit *cUnit);
bool dvmCompilerFindLocalLiveIn(struct CompilationUnit *cUnit,
                                struct BasicBlock *bb);
bool dvmCompilerInitializeExitUses (CompilationUnit *cUnit, BasicBlock *bb);
bool dvmCompilerDoSSAConversion(struct CompilationUnit *cUnit,
                                struct BasicBlock *bb);
bool dvmCompilerDoConstantPropagation(struct CompilationUnit *cUnit,
                                      struct BasicBlock *bb);
#ifndef ARCH_IA32
bool dvmCompilerFindInductionVariables(struct CompilationUnit *cUnit,
                                       struct BasicBlock *bb);
#else
void dvmCompilerFindInductionVariables(struct CompilationUnit *cUnit,
                                            LoopInformation *info);
#endif
/* Clear the visited flag for each BB */
bool dvmCompilerClearVisitedFlag(struct CompilationUnit *cUnit,
                                 struct BasicBlock *bb);
char *dvmCompilerGetDalvikDisassembly(const DecodedInstruction *insn,
                                      const char *note = 0);
char *dvmCompilerFullDisassembler(const struct CompilationUnit *cUnit,
                                  const struct MIR *mir);

void dvmCompilerExtendedDisassembler (const CompilationUnit *cUnit,
                                      const MIR *mir,
                                      const DecodedInstruction *insn,
                                      char *buffer,
                                      int len);

char *dvmCompilerGetSSAString(struct CompilationUnit *cUnit,
                              struct SSARepresentation *ssaRep);

/**
 * @brief Apply a function to BasicBlocks of the CompilationUnit
 * @details Traverse the CFG of the cUnit according to the dfaMode,
 * and perform the function on each block.
 * @param cUnit CompilationUnit in which we dispatch the function
 * @param func Function to apply to each BasicBlock
 * @param dfaMode The DataFlowAnalysisMode to use to iterate the BBs
 * @param isIterative Whether we iterate on changes
 * @param walkData Supplemental data if needed during the traversal
 */
void dvmCompilerDataFlowAnalysisDispatcher(struct CompilationUnit *cUnit,
                bool (*func)(struct CompilationUnit *, struct BasicBlock *),
                DataFlowAnalysisMode dfaMode,
                bool isIterative, void *walkData = 0);

void dvmCompilerMethodSSATransformation(struct CompilationUnit *cUnit);

/**
 * @brief Calculate the BasicBlock information
 * @param cUnit the CompilationUnit
 * @param filter do we wish to filter the loop? (default: true)
 * @param buildLoopInfo do we wish to build loop information (default: false)
 * @return whether the calculation succeeded or not
 */
bool dvmCompilerCalculateBasicBlockInformation (CompilationUnit *cUnit, bool filter = true, bool buildLoopInfo = false);

/**
 * @brief Fill in a CompilationUnit with method level data
 * @details Create the complete CFG of the method as BasicBlocks of the cUnit
 * Also add the exception blocks to the CFG if asked for, and verify the predecessor
 * information at the end.
 * @param[out] cUnit The CompilationUnit to fill in
 * @param method The method for which the cUnit will be prepared for
 * @param needTryCatchBlocks whether we also want data about try catch blocks
 * @return true if we were able to successfully fill up the cUnit
 */
bool dvmCompilerFillCUnitWithMethodData(CompilationUnit &cUnit, const Method *method, bool needTryCatchBlocks);

void dvmCompilerUpdateGlobalState(void);
JitTraceDescription *dvmCopyTraceDescriptor(const u2 *pc,
                                            const struct JitEntry *desc);
extern "C" void *dvmCompilerGetInterpretTemplate();
JitInstructionSetType dvmCompilerGetInterpretTemplateSet();
u8 dvmGetRegResourceMask(int reg);
void dvmDumpCFG(struct CompilationUnit *cUnit, const char *dirPrefix, const char *suffix="");
void dvmCompilerDumpBasicBlocks (CompilationUnit *cUnit, const char *dirPrefix, const char *suffix = "", bool dumpLoopInformation = false);

bool dvmIsOpcodeSupportedByJit(const DecodedInstruction & insn);

/* Calculate the predecessor information for the CompilationUnit */
void dvmCompilerCalculatePredecessors (CompilationUnit *cUnit);

/* Build domination information */
void dvmCompilerBuildDomination (CompilationUnit *cUnit);

/* Find the highest MIR in the color */
MIR *dvmCompilerFindHighestMIRInColor (MIR *elem);

/* Find the lowest MIR in the color */
MIR *dvmCompilerFindLowestMIRInColor (MIR *elem);

/* Build the def use chains */
bool dvmCompilerBuildDefUseChain (CompilationUnit *cUnit, BasicBlock *bb);

/**
 * @brief Checks if the opcode is a conditional branch.
 * @param opcode The opcode to check which may be an extended one
 * @return Returns whether we are looking at a conditional branch
 */
bool dvmCompilerIsOpcodeConditionalBranch (int opcode);

#if defined(WITH_JIT_TUNING)
/**
 * @brief a dvmHashForeach callback, dump the Method CFG for each method, helper function to call dvmCompilerDumpMethodCFG
 * @param data the Method being processed
 * @param arg this is the parameter dvmHashForeach required, not used in this function
 * @return 0 for success
 */
int dvmCompilerDumpMethodCFGHandle(void* vmethodProf, void* arg);

/**
 * @brief Dump the Method CFG of every BasicBlock into a DOT graph, and lable the execution count of each edge
 * @param method the method we are trying to dump
 * @param profileTable this is a array that contains the execution count of each bytecode
 */
void dvmCompilerDumpMethodCFG(const Method* method, int* profileTable);
#endif

#endif  // DALVIK_VM_COMPILER_H_
