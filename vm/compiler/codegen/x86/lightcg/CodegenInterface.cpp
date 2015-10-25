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
#include <sys/mman.h>
#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "Dalvik.h"
#include "libdex/DexOpcodes.h"
#include "compiler/Compiler.h"
#include "compiler/CompilerIR.h"
#include "interp/Jit.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "NcgAot.h"
#include "compiler/codegen/CompilerCodegen.h"
#include "InstructionGeneration.h"
#include "Singleton.h"
#include "ExceptionHandling.h"
#include "Scheduler.h"
#include "Profile.h"
#include "Utility.h"
#include "X86Common.h"
#include "JitVerbose.h"

#ifdef HAVE_ANDROID_OS
#include <cutils/properties.h>
#endif

#if !defined(VTUNE_DALVIK)
/* Handy function for VTune updates of PredictedChainingCells */
static void updateCodeCache(PredictedChainingCell& dst, const PredictedChainingCell& src)
{
    dst = src;
}

/* Handy function for VTune updates of ints */
static void updateCodeCache(int &dst, int src)
{
    dst = src;
}

/* Send updated code cache content to VTune */
static void SendUpdateToVTune(void * address, unsigned size, unsigned method_id = 0)
{
    (void) address; (void) size; (void) method_id;
}

#else

#include "compiler/codegen/x86/VTuneSupportX86.h"

/* Send updated code cache content to VTune */
static void SendUpdateToVTune(void * address, unsigned size, unsigned method_id = 0)
{
    if (gDvmJit.vtuneInfo == kVTuneInfoDisabled || gDvmJit.vtuneVersion < 279867) {
        return;
    }

    iJIT_Method_Load jitMethod;
    memset (&jitMethod, 0, sizeof (jitMethod));

    jitMethod.method_id = method_id;
    jitMethod.method_load_address = address;
    jitMethod.method_size = size;
    if (gDvmJit.vtuneVersion >= VTUNE_VERSION_EXPERIMENTAL) {
        jitMethod.class_id = 1; // update bytes, do not change format
    }

    // Send the trace update event to the VTune analyzer
    int res = notifyVTune(iJVM_EVENT_TYPE_METHOD_UPDATE, (void*)&jitMethod);
    if (gDvmJit.printMe == true) {
        ALOGD("JIT API: code update with method_id=%u address=%p size=%u %s",
                jitMethod.method_id, jitMethod.method_load_address, jitMethod.method_size,
                (res == 0 ?                    "failed to write"
                 : (jitMethod.method_id == 0 ? "failed to resolve"
                 :                             "written")));
    }
}

/* Handy function for VTune updates of changed PredictedChainingCells */
static void updateCodeCache(PredictedChainingCell& dst, const PredictedChainingCell& src)
{
    bool isDiff = dst.branch != src.branch || dst.branch2 != src.branch2;
    dst = src;
    if (isDiff == true) {
        SendUpdateToVTune(&dst, sizeof(PredictedChainingCell));
    }
}

/* Handy function for VTune updates of changed ints */
static void updateCodeCache(int &dst, int src)
{
    bool isDiff = dst != src;
    dst = src;
    if (isDiff == true) {
        SendUpdateToVTune(&dst, sizeof(int));
    }
}
#endif // !defined(VTUNE_DALVIK)

/* JIT opcode filtering */
bool jitOpcodeTable[kNumPackedOpcodes];
Opcode jitNotSupportedOpcode[] = {
#if defined (WITH_SELF_VERIFICATION)
    OP_MONITOR_ENTER,
    OP_MONITOR_EXIT,
    OP_NEW_INSTANCE,
    OP_NEW_ARRAY,
    OP_CHECK_CAST,
    OP_MOVE_EXCEPTION,
    OP_FILL_ARRAY_DATA,
    OP_EXECUTE_INLINE,
    OP_EXECUTE_INLINE_RANGE,

    //TODO: fix for the test case
    /* const does not generate assembly instructions
     * so a divergence will falsely occur when interp executes and sets
     * the virtual registers (in memory ).
     *
     * const*
     * return
     *
     * const*
     * invoke_*
     */
    OP_CONST_4,
    OP_CONST_16,
    OP_CONST,
    OP_CONST_HIGH16,
    OP_CONST_WIDE_16,
    OP_CONST_WIDE_32,
    OP_CONST_WIDE,
    OP_CONST_WIDE_HIGH16,
    OP_CONST_STRING,
    OP_CONST_STRING_JUMBO,

    OP_RETURN,
    OP_RETURN_VOID, //const and return
    OP_RETURN_OBJECT,
    OP_INVOKE_VIRTUAL_QUICK_RANGE,
    OP_INVOKE_VIRTUAL_QUICK,
    OP_INVOKE_INTERFACE,
    OP_INVOKE_STATIC,

    //occurs with threaded apps
    OP_APUT_CHAR,
    OP_APUT_BOOLEAN,
    OP_APUT_BYTE,

#endif
};

/*
 * Initial value of predicted chain cell
 * EB FE   : jmp -2 // self
 * 0F 1F 00: nop3
 * 0F 1F 00: nop3
 *
 * When patched with 5-byte call/jmp rel32 instruction it will be correct.
 */
#define PREDICTED_CHAIN_BX_PAIR_INIT1     0x1f0ffeeb
#define PREDICTED_CHAIN_BX_PAIR_INIT2     0x001f0f00

#if defined(WITH_JIT)
/* Target-specific save/restore */
extern "C" void dvmJitCalleeSave(double *saveArea);
extern "C" void dvmJitCalleeRestore(double *saveArea);
#endif

/*
 * Determine the initial instruction set to be used for this trace.
 * Later components may decide to change this.
 */
//JitInstructionSetType dvmCompilerInstructionSet(CompilationUnit *cUnit)
JitInstructionSetType dvmCompilerInstructionSet(void)
{
    return DALVIK_JIT_IA32;
}

JitInstructionSetType dvmCompilerGetInterpretTemplateSet()
{
    return DALVIK_JIT_IA32;
}

/* we don't use template for IA32 */
void *dvmCompilerGetInterpretTemplate()
{
      return NULL;//(void*) ((int)gDvmJit.codeCache);
}

/* Initialize the jitOpcodeTable which records what opcodes are supported
 *  by the JIT compiler.
 */
void dvmInitJitOpcodeTable() {
    unsigned int i;
    memset(jitOpcodeTable, 1, sizeof(jitOpcodeTable));
    for (i = 0; i < sizeof(jitNotSupportedOpcode)/sizeof(Opcode); i++) {
        jitOpcodeTable[((unsigned int)jitNotSupportedOpcode[i])] = false;
    }
    for (i = 0; i < sizeof(jitOpcodeTable)/sizeof(bool); i++) {
        if (jitOpcodeTable[i] == false)
            ALOGV("opcode 0x%x not supported by JIT", i);
    }
}

/* Return true if the opcode is supported by the JIT compiler. */
bool dvmIsOpcodeSupportedByJit(const DecodedInstruction & insn)
{
     /* reject traces containing bytecodes requesting virtual registers exceeding allowed limit */
     if ((insn.opcode == OP_INVOKE_VIRTUAL_RANGE) || (insn.opcode == OP_INVOKE_VIRTUAL_QUICK_RANGE) ||
         (insn.opcode == OP_INVOKE_SUPER_RANGE) || (insn.opcode == OP_INVOKE_SUPER_QUICK_RANGE) ||
         (insn.opcode == OP_INVOKE_DIRECT_RANGE) || (insn.opcode == OP_INVOKE_STATIC_RANGE) ||
         (insn.opcode == OP_INVOKE_INTERFACE_RANGE)){
        int opcodeArgs = (int) (insn.vA);
        if (opcodeArgs > MAX_REG_PER_BYTECODE)
           return false;
     }
    return jitOpcodeTable[((int) insn.opcode)];
}

/* Track the number of times that the code cache is patched */
#if defined(WITH_JIT_TUNING)
#define UPDATE_CODE_CACHE_PATCHES()    (gDvmJit.codeCachePatches++)
#else
#define UPDATE_CODE_CACHE_PATCHES()
#endif

//! default JIT table size used by x86 JIT
#define DEFAULT_X86_ATOM_DALVIK_JIT_TABLE_SIZE 1<<12
//! default JIT threshold used by x86 JIT
#define DEFAULT_X86_ATOM_DALVIK_JIT_THRESHOLD 50
//! default JIT code cache size used by x86 JIT
#define DEFAULT_X86_ATOM_DALVIK_JIT_CODE_CACHE_SIZE 512*1024
//! JIT data cache size vs code cache size ratio
#define JIT_DATA_CACHE_SIZE_RATIO 0

//! Initializes target-specific configuration

//! Configures the jit table size, jit threshold, and jit code cache size
//! Initializes status of all threads and the table of supported bytecodes
//! @return true when initialization is successful (NOTE: current
//! implementation always returns true)
bool dvmCompilerArchInit() {
#ifdef HAVE_ANDROID_OS
    // Used to get global properties
    char propertyBuffer[PROPERTY_VALUE_MAX];
#endif
    unsigned long propertyValue;

    // Used to identify cpu
    int infoRequestType = 0x1;
    int familyAndModelInformation;
    const int familyIdMask = 0xF00;
    const int familyIdShift = 8;
    const int modelMask = 0XF0;
    const int modelShift = 4;
    const int modelWidth = 4;
    const int extendedModelIdMask = 0xF0000;
    const int extendedModelShift = 16;

    // Initialize JIT table size
    if(gDvmJit.jitTableSize == 0 || (gDvmJit.jitTableSize & (gDvmJit.jitTableSize - 1))) {
        // JIT table size has not been initialized yet or is not a power of two
#ifdef HAVE_ANDROID_OS
        memset(propertyBuffer, 0, PROPERTY_VALUE_MAX); // zero out buffer so we don't use junk
        property_get("dalvik.jit.table_size", propertyBuffer, NULL);
        propertyValue = strtoul(propertyBuffer, NULL, 10 /*base*/);
#else
        propertyValue = 0ul;
#endif
        if (errno == ERANGE || propertyValue == 0ul || (propertyValue & (propertyValue - 1ul)))
            /* out of range, conversion failed, trying to use invalid value of 0, or using non-power of two */
            gDvmJit.jitTableSize = DEFAULT_X86_ATOM_DALVIK_JIT_TABLE_SIZE;
        else // property is valid, but we still need to cast from unsigned long to unsigned int
            gDvmJit.jitTableSize = static_cast<unsigned int>(propertyValue);
    }

    // Initialize JIT table mask
    gDvmJit.jitTableMask = gDvmJit.jitTableSize - 1;
    gDvmJit.optLevel = kJitOptLevelO1;

    // Initialize JIT threshold
    if(gDvmJit.threshold == 0) { // JIT threshold has not been initialized yet
#ifdef HAVE_ANDROID_OS
        memset(propertyBuffer, 0, PROPERTY_VALUE_MAX); // zero out buffer so we don't use junk
        property_get("dalvik.jit.threshold", propertyBuffer, NULL);
        propertyValue = strtoul(propertyBuffer, NULL, 10 /*base*/);
#else
        propertyValue = 0ul;
#endif
        if (errno == ERANGE || propertyValue == 0ul)
            /* out of range, conversion failed, or trying to use invalid value of 0 */
            gDvmJit.threshold = DEFAULT_X86_ATOM_DALVIK_JIT_THRESHOLD;
        else // property is valid, but we still need to cast from unsigned long to unsigned short
            gDvmJit.threshold = static_cast<unsigned short>(propertyValue);
    }

    // Initialize JIT code cache size
    if(gDvmJit.codeCacheSize == 0) { // JIT code cache size has not been initialized yet
#ifdef HAVE_ANDROID_OS
        memset(propertyBuffer, 0, PROPERTY_VALUE_MAX); // zero out buffer so we don't use junk
        property_get("dalvik.jit.code_cache_size", propertyBuffer, NULL);
        propertyValue = strtoul(propertyBuffer, NULL, 10 /*base*/);
#else
        propertyValue = 0ul;
#endif
        if (errno == ERANGE || propertyValue == 0ul)
            /* out of range, conversion failed, or trying to use invalid value of 0 */
            gDvmJit.codeCacheSize = DEFAULT_X86_ATOM_DALVIK_JIT_CODE_CACHE_SIZE;
        else // property is valid, but we still need to cast from unsigned long to unsigned int
            gDvmJit.codeCacheSize = static_cast<unsigned int>(propertyValue);
    }

    // Initialize JIT data cache size
    if (gDvmJit.dataCacheSize == UNINITIALIZED_DATA_CACHE_SIZE) { // JIT data cache size has not been initialized yet
#ifdef HAVE_ANDROID_OS
        memset(propertyBuffer, 0, PROPERTY_VALUE_MAX); // zero out buffer so we don't use junk
        property_get("dalvik.jit.data_cache_size", propertyBuffer, NULL);
        char *endPtr = NULL;
        long int value = strtol(propertyBuffer, &endPtr, 10 /*base*/);
        if (*endPtr == '\0' && value >= 0 && *propertyBuffer != '\0') {
            // The value is a decimal value of KBytes requested
            gDvmJit.dataCacheSize = static_cast<unsigned int>(value) * 1024;
        }
        else {
            /* out of range, conversion failed */
            gDvmJit.dataCacheSize = gDvmJit.codeCacheSize * JIT_DATA_CACHE_SIZE_RATIO;
        }
#else
        gDvmJit.dataCacheSize = gDvmJit.codeCacheSize * JIT_DATA_CACHE_SIZE_RATIO;
#endif
    }

    // Print out values used
    ALOGV("JIT threshold set to %hu",gDvmJit.threshold);
    ALOGV("JIT table size set to %u",gDvmJit.jitTableSize);
    ALOGV("JIT code cache size set to %u",gDvmJit.codeCacheSize);
    ALOGV("JIT data cache size set to %u",gDvmJit.dataCacheSize);

    //Disable Method-JIT
    gDvmJit.disableOpt |= (1 << kMethodJit);

#ifdef HAVE_ANDROID_OS
    // If JIT verbose has not been enabled, check the global property dalvik.jit.verbose
    if (!gDvmJit.printMe) {
        memset(propertyBuffer, 0, PROPERTY_VALUE_MAX); // zero out buffer so we don't use junk
        property_get("dalvik.jit.verbose", propertyBuffer, NULL);
        // Look for text ". We could enable finer control by checking application
        // name, but the VM would need to know which application it is running
        if (strncmp("true", propertyBuffer, PROPERTY_VALUE_MAX) == 0) {
            gDvmJit.printMe = true;
        }
    }
#endif

    // Now determine machine model
    asm volatile (
            "pushl %%ebx\n"
            "cpuid\n"
            "popl %%ebx\n"
            : "=a" (familyAndModelInformation),
              "=c" (gDvmJit.featureInformation[0]),
              "=d" (gDvmJit.featureInformation[1])
            : "a" (infoRequestType)
          );
    gDvmJit.cpuFamily = (familyAndModelInformation & familyIdMask) >> familyIdShift;
    gDvmJit.cpuModel = (((familyAndModelInformation & extendedModelIdMask)
            >> extendedModelShift) << modelWidth)
            + ((familyAndModelInformation & modelMask) >> modelShift);

    ALOGV ("Processor family:%d model:%d %s SSE4.1", gDvmJit.cpuFamily, gDvmJit.cpuModel,
            (dvmCompilerArchitectureSupportsSSE41 () == true) ? "supports" : "does not support");

#if defined(WITH_SELF_VERIFICATION)
    /* Force into blocking mode */
    gDvmJit.blockingMode = true;
    gDvm.nativeDebuggerActive = true;
#endif

    // Make sure all threads have current values
    dvmJitUpdateThreadStateAll();

    /* Initialize jitOpcodeTable for JIT supported opcode */
    dvmInitJitOpcodeTable();

    return true;
}

/**
 * @brief Check whether architecture supports vectorized packed size in bytes
 * @details For x86, we check SSE support level because for some sizes we don't have instruction support
 * @param size The vectorized packed size
 * @return Returns whether the architecture supports it
 */
bool dvmCompilerArchSupportsVectorizedPackedSize (unsigned int size)
{
    //Always support size of 2
    if (size == 2)
    {
        return true;
    }

    //Other sizes require SSE4.1
    bool supportsSSE41 = dvmCompilerArchitectureSupportsSSE41 ();

    if (supportsSSE41 == false)
    {
        return false;
    }

    //If it's 4, we can do it
    if (size == 4)
    {
        return true;
    }

    return false;
}

/**
 * @brief Used to check whether the architecture specific portion supports extended opcode
 * @param extendedOpcode The opcode to check
 * @return Returns whether the extended opcode is supported
 */
bool dvmCompilerArchSupportsExtendedOp (int extendedOpcode)
{
    switch (extendedOpcode)
    {
        case kMirOpPhi:
        case kMirOpNullCheck:
        case kMirOpBoundCheck:
        case kMirOpNullNRangeUpCheck:
        case kMirOpNullNRangeDownCheck:
        case kMirOpLowerBound:
        case kMirOpCheckInlinePrediction:
        case kMirOpRegisterize:
        case kMirOpPackedSet:
        case kMirOpConst128b:
        case kMirOpMove128b:
        case kMirOpPackedAddition:
        case kMirOpPackedMultiply:
        case kMirOpPackedSubtract:
        case kMirOpPackedShiftLeft:
        case kMirOpPackedSignedShiftRight:
        case kMirOpPackedUnsignedShiftRight:
        case kMirOpPackedAnd:
        case kMirOpPackedOr:
        case kMirOpPackedXor:
        case kMirOpPackedAddReduce:
        case kMirOpPackedReduce:
        case kMirOpCheckStackOverflow:
            return true;
        default:
            break;
    }

    //If we get here it is not supported
    return false;
}

void dvmCompilerPatchInlineCache(void)
{
    int i;
    PredictedChainingCell *minAddr, *maxAddr;

    /* Nothing to be done */
    if (gDvmJit.compilerICPatchIndex == 0) return;

    /*
     * Since all threads are already stopped we don't really need to acquire
     * the lock. But race condition can be easily introduced in the future w/o
     * paying attention so we still acquire the lock here.
     */
    dvmLockMutex(&gDvmJit.compilerICPatchLock);

    UNPROTECT_CODE_CACHE(gDvmJit.codeCache, gDvmJit.codeCacheByteUsed);

    //ALOGD("Number of IC patch work orders: %d", gDvmJit.compilerICPatchIndex);

    /* Initialize the min/max address range */
    minAddr = (PredictedChainingCell *)
        ((char *) gDvmJit.codeCache + gDvmJit.codeCacheSize);
    maxAddr = (PredictedChainingCell *) gDvmJit.codeCache;

    for (i = 0; i < gDvmJit.compilerICPatchIndex; i++) {
        ICPatchWorkOrder *workOrder = &gDvmJit.compilerICPatchQueue[i];
        PredictedChainingCell *cellAddr = workOrder->cellAddr;
        PredictedChainingCell *cellContent = &workOrder->cellContent;
        ClassObject *clazz = dvmFindClassNoInit(workOrder->classDescriptor,
                                                workOrder->classLoader);

        assert(clazz->serialNumber == workOrder->serialNumber);

        /* Use the newly resolved clazz pointer */
        cellContent->clazz = clazz;

        if (cellAddr->clazz == NULL) {
            COMPILER_TRACE_CHAINING(
                ALOGI("Jit Runtime: predicted chain %p to %s (%s) initialized",
                      cellAddr,
                      cellContent->clazz->descriptor,
                      cellContent->method->name));
        } else {
            COMPILER_TRACE_CHAINING(
                ALOGI("Jit Runtime: predicted chain %p from %s to %s (%s) "
                      "patched",
                      cellAddr,
                      cellAddr->clazz->descriptor,
                      cellContent->clazz->descriptor,
                      cellContent->method->name));
        }

        /* Patch the chaining cell */
        updateCodeCache(*cellAddr, *cellContent);

        minAddr = (cellAddr < minAddr) ? cellAddr : minAddr;
        maxAddr = (cellAddr > maxAddr) ? cellAddr : maxAddr;
    }

    PROTECT_CODE_CACHE(gDvmJit.codeCache, gDvmJit.codeCacheByteUsed);

    gDvmJit.compilerICPatchIndex = 0;
    dvmUnlockMutex(&gDvmJit.compilerICPatchLock);
}

/* Target-specific cache clearing */
void dvmCompilerCacheClear(char *start, size_t size)
{
    /* "0xFF 0xFF" is an invalid opcode for x86. */
    memset(start, 0xFF, size);
}

/* for JIT debugging, to be implemented */
void dvmJitCalleeSave(double *saveArea) {
}

void dvmJitCalleeRestore(double *saveArea) {
}

void dvmJitToInterpSingleStep() {
}

JitTraceDescription *dvmCopyTraceDescriptor(const u2 *pc,
                                            const JitEntry *knownEntry) {
    return NULL;
}

void dvmCompilerCodegenDump(CompilationUnit *cUnit) //in ArchUtility.c
{
}

void dvmCompilerArchDump(void)
{
}

void dvmCompilerAssembleLIR(CompilationUnit *cUnit, JitTranslationInfo* info)
{
}

void dvmJitInstallClassObjectPointers(CompilationUnit *cUnit, char *codeAddress)
{
}

void dvmCompilerMethodMIR2LIR(CompilationUnit *cUnit)
{
    // Method-based JIT not supported for x86.
}

void dvmJitScanAllClassPointers(void (*callback)(void *))
{
}

/*
 * Attempt to enqueue a work order to patch an inline cache for a predicted
 * chaining cell for virtual/interface calls.
 */
static bool inlineCachePatchEnqueue(PredictedChainingCell *cellAddr,
                                    PredictedChainingCell *newContent)
{
    bool result = true;

    /*
     * Make sure only one thread gets here since updating the cell (ie fast
     * path and queueing the request (ie the queued path) have to be done
     * in an atomic fashion.
     */
    dvmLockMutex(&gDvmJit.compilerICPatchLock);

    /* Fast path for uninitialized chaining cell */
    if (cellAddr->clazz == NULL &&
        cellAddr->branch == PREDICTED_CHAIN_BX_PAIR_INIT1) {
        UNPROTECT_CODE_CACHE(cellAddr, sizeof(*cellAddr));

        cellAddr->method = newContent->method;

        /* remember if the branch address has changed, other fields do not matter.
         * If changed then send new values to VTune a bit later */
        bool isBranchUpdated = cellAddr->branch != newContent->branch || cellAddr->branch2 != newContent->branch2;

        cellAddr->branch = newContent->branch;
        cellAddr->branch2 = newContent->branch2;

        /*
         * The update order matters - make sure clazz is updated last since it
         * will bring the uninitialized chaining cell to life.
         */
        android_atomic_release_store((int32_t)newContent->clazz,
            (volatile int32_t *)(void*) &cellAddr->clazz);
        //cacheflush((intptr_t) cellAddr, (intptr_t) (cellAddr+1), 0);
        UPDATE_CODE_CACHE_PATCHES();
        if (isBranchUpdated == true) {
            SendUpdateToVTune(cellAddr, sizeof(*cellAddr));
        }

        PROTECT_CODE_CACHE(cellAddr, sizeof(*cellAddr));

#if 0
        MEM_BARRIER();
        cellAddr->clazz = newContent->clazz;
        //cacheflush((intptr_t) cellAddr, (intptr_t) (cellAddr+1), 0);
#endif
#if defined(WITH_JIT_TUNING)
        gDvmJit.icPatchInit++;
#endif
        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: FAST predicted chain %p to method %s%s %p",
                  cellAddr, newContent->clazz->descriptor, newContent->method->name, newContent->method));
    /* Check if this is a frequently missed clazz */
    } else if (cellAddr->stagedClazz != newContent->clazz) {
        /* Not proven to be frequent yet - build up the filter cache */
        UNPROTECT_CODE_CACHE(cellAddr, sizeof(*cellAddr));

        cellAddr->stagedClazz = newContent->clazz;

        UPDATE_CODE_CACHE_PATCHES();
        PROTECT_CODE_CACHE(cellAddr, sizeof(*cellAddr));

#if defined(WITH_JIT_TUNING)
        gDvmJit.icPatchRejected++;
#endif
    /*
     * Different classes but same method implementation - it is safe to just
     * patch the class value without the need to stop the world.
     */
    } else if (cellAddr->method == newContent->method) {
        UNPROTECT_CODE_CACHE(cellAddr, sizeof(*cellAddr));

        cellAddr->clazz = newContent->clazz;
        /* No need to flush the cache here since the branch is not patched */
        UPDATE_CODE_CACHE_PATCHES();

        PROTECT_CODE_CACHE(cellAddr, sizeof(*cellAddr));

#if defined(WITH_JIT_TUNING)
        gDvmJit.icPatchLockFree++;
#endif
    /*
     * Cannot patch the chaining cell inline - queue it until the next safe
     * point.
     */
    } else if (gDvmJit.compilerICPatchIndex < COMPILER_IC_PATCH_QUEUE_SIZE)  {
        int index = gDvmJit.compilerICPatchIndex++;
        const ClassObject *clazz = newContent->clazz;

        gDvmJit.compilerICPatchQueue[index].cellAddr = cellAddr;
        gDvmJit.compilerICPatchQueue[index].cellContent = *newContent;
        gDvmJit.compilerICPatchQueue[index].classDescriptor = clazz->descriptor;
        gDvmJit.compilerICPatchQueue[index].classLoader = clazz->classLoader;
        /* For verification purpose only */
        gDvmJit.compilerICPatchQueue[index].serialNumber = clazz->serialNumber;

#if defined(WITH_JIT_TUNING)
        gDvmJit.icPatchQueued++;
#endif
        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: QUEUE predicted chain %p to method %s%s",
                  cellAddr, newContent->clazz->descriptor, newContent->method->name));
    } else {
    /* Queue is full - just drop this patch request */
#if defined(WITH_JIT_TUNING)
        gDvmJit.icPatchDropped++;
#endif

        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: DROP predicted chain %p to method %s%s",
                  cellAddr, newContent->clazz->descriptor, newContent->method->name));
    }

    dvmUnlockMutex(&gDvmJit.compilerICPatchLock);
    return result;
}

/*
 * This method is called from the invoke templates for virtual and interface
 * methods to speculatively setup a chain to the callee. The templates are
 * written in assembly and have setup method, cell, and clazz at r0, r2, and
 * r3 respectively, so there is a unused argument in the list. Upon return one
 * of the following three results may happen:
 *   1) Chain is not setup because the callee is native. Reset the rechain
 *      count to a big number so that it will take a long time before the next
 *      rechain attempt to happen.
 *   2) Chain is not setup because the callee has not been created yet. Reset
 *      the rechain count to a small number and retry in the near future.
 *   3) Ask all other threads to stop before patching this chaining cell.
 *      This is required because another thread may have passed the class check
 *      but hasn't reached the chaining cell yet to follow the chain. If we
 *      patch the content before halting the other thread, there could be a
 *      small window for race conditions to happen that it may follow the new
 *      but wrong chain to invoke a different method.
 */
extern "C" const Method *dvmJitToPatchPredictedChain(const Method *method,
                                          Thread *self,
                                          PredictedChainingCell *cell,
                                          const ClassObject *clazz)
{
    int newRechainCount = PREDICTED_CHAIN_COUNTER_RECHAIN;
    /* Don't come back here for a long time if the method is native */
    if (dvmIsNativeMethod(method)) {
        UNPROTECT_CODE_CACHE(cell, sizeof(*cell));

        /*
         * Put a non-zero/bogus value in the clazz field so that it won't
         * trigger immediate patching and will continue to fail to match with
         * a real clazz pointer.
         */
        cell->clazz = (ClassObject *) PREDICTED_CHAIN_FAKE_CLAZZ;

        UPDATE_CODE_CACHE_PATCHES();
        PROTECT_CODE_CACHE(cell, sizeof(*cell));
        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: predicted chain %p to native method %s ignored",
                  cell, method->name));
        goto done;
    }
    {
    int tgtAddr = (int) dvmJitGetTraceAddr(method->insns);

    /*
     * Compilation not made yet for the callee. Reset the counter to a small
     * value and come back to check soon.
     */
    if ((tgtAddr == 0) ||
        ((void*)tgtAddr == dvmCompilerGetInterpretTemplate())) {
        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: predicted chain %p to method %s%s delayed",
                  cell, method->clazz->descriptor, method->name));
        goto done;
    }

    PredictedChainingCell newCell;

    if (cell->clazz == NULL) {
        newRechainCount = self->icRechainCount;
    }

    int relOffset = (int) tgtAddr - (int)cell;
    OpndSize immSize = estOpndSizeFromImm(relOffset);
    int jumpSize = getJmpCallInstSize(immSize, JmpCall_uncond);
    relOffset -= jumpSize;
    COMPILER_TRACE_CHAINING(
            ALOGI("inlineCachePatchEnqueue chain %p to method %s%s inst size %d",
                  cell, method->clazz->descriptor, method->name, jumpSize));

    // This does not need to go through lowering interface and can encode directly
    // at address because it does not actually update code stream until safe point.
    // Can't use stream here since it is used by the compilation thread.
    newCell.branch = PREDICTED_CHAIN_BX_PAIR_INIT1;
    newCell.branch2 = PREDICTED_CHAIN_BX_PAIR_INIT2;
    encoder_imm(Mnemonic_JMP, immSize, relOffset, (char*) (&newCell)); //update newCell.branch

    newCell.clazz = clazz;
    newCell.method = method;

    /*
     * Enter the work order to the queue and the chaining cell will be patched
     * the next time a safe point is entered.
     *
     * If the enqueuing fails reset the rechain count to a normal value so that
     * it won't get indefinitely delayed.
     */
    inlineCachePatchEnqueue(cell, &newCell);
    }
done:
    self->icRechainCount = newRechainCount;
    return method;
}

/**
 * @class BackwardBranchChainingCellContents
 * @brief Defines the data structure of a Backward Branch Chaining Cell.
 */
struct __attribute__ ((packed)) BackwardBranchChainingCellContents
{
    /**
     * @brief Used to hold the "call rel32" to dvmJitToInterpBackwardBranch
     */
    char instructionHolder[5];

    unsigned int nextPC;    //!< Next bytecode PC

    /**
     * @brief Holds address of operand of jump instruction that is to be patched.
     * After chaining, the jump is  filled with relative offset to loop header.
     * After unchaining it is filled with relative offset to the VR write-back.
     */
    char * codePtr;

    char * loopHeaderAddr;    //!< Address of loop header block.
    char * vrWriteBackAddr;   //!< Address of VR write-back block.
    char * loopPreHeaderAddr; //!< Address of loop pre-header block.

    /**
     * Doxygen does not like documentation of functions here, so let's just document it but not expose it to doxygen
     * brief Used for unchaining backward branch chaining cells.
     * param location This is location where unchaining method can assume that a Backward Branch CC exists.
     * return Returns size of unchained cell.
     */
    static size_t unchain (u1 * location)
    {
        //First we reinterpret the location to be a chaining cell
        BackwardBranchChainingCellContents * contents =
                reinterpret_cast<BackwardBranchChainingCellContents *> (location);

        //We want to jump to the VR write back address and we know that the code
        //pointer points to operand of jump. Thus we also subtract our assumed
        //operand size of 32-bits.
        int relativeOffset = (contents->vrWriteBackAddr - contents->codePtr)
                - OpndSize_32;

        //We want to patch with an int value so we reinterpret the address here
        int * addressOfJumpOperand = reinterpret_cast<int *> (contents->codePtr);

        //This does the actual patching with the offset we calculated
        updateCodeCache(*addressOfJumpOperand, relativeOffset);

        //We return size of our chaining cell
        return sizeof(*contents);
    }
};

#define BYTES_OF_NORMAL_CHAINING 17
#define BYTES_OF_HOT_CHAINING 17
#define BYTES_OF_SINGLETON_CHAINING 17
#define BYTES_OF_PREDICTED_CHAINING 20
#define OFFSET_OF_PATCHADDR 9 // offset in chaining cell to the field for the location to be patched
#define OFFSET_OF_ISMOVEFLAG 13  // offset in hot chaining cell to the isMove field
#define OFFSET_OF_ISSWITCH 13  // offset in normal chaining cell to the isSwitch field
#define BYTES_OF_32BITS 4
/*
 * Unchain a trace given the starting address of the translation
 * in the code cache.  Refer to the diagram in dvmCompilerAssembleLIR.
 * For ARM, it returns the address following the last cell unchained.
 * For IA, it returns NULL since cacheflush is not required for IA.
 */
u4* dvmJitUnchain(void* codeAddr)
{
    /* codeAddr is 4-byte aligned, so is chain cell count offset */
    u2* pChainCellCountOffset = (u2*)((char*)codeAddr - 4);
    u2 chainCellCountOffset = *pChainCellCountOffset;
    /* chain cell counts information is 4-byte aligned */
    ChainCellCounts *pChainCellCounts =
          (ChainCellCounts*)((char*)codeAddr + chainCellCountOffset);
    u2* pChainCellOffset = (u2*)((char*)codeAddr - 2);
    u2 chainCellOffset = *pChainCellOffset;
    u1* pChainCells;
    int i,j;
    PredictedChainingCell *predChainCell;
    int padding;
    u1* patchAddr;
    int relativeNCG;
    int ismove_flag = 0;

    /* Locate the beginning of the chain cell region */
    pChainCells = (u1 *)((char*)codeAddr + chainCellOffset);

    /* The cells are sorted in order - walk through them and reset */
    for (i = 0; i < kChainingCellGap; i++) {
        /* for normal chaining:
               call imm32
               rPC
               codePtr (offset address of jmp/jcc)
               isSwitch
           after chaining:
               if (isSwitch)
                 codePtr is filled with absolute address to the target
               else
                 codePtr is filled with a relative offset to the target
           after unchaining:
               if (isSwitch)
                 codePtr is filled with absolute adress of the chaining cell
               else
                 codePtr is filled with original relative offset to the chaining cell

           for backward chaining:
               call imm32
               rPC
               codePtr (offset address of jmp/jcc)
               loop header address
               vrStoreCodePtr (code address of deferred VR store)
           after chaining:
               codePtr is filled with a relative offset to the loop header
           after unchaining:
               if (vrStoreCodePtr)
                   codePtr is filled with relative offset to the deferred vr store
               else
                   codePtr is filled with relative offset to the chaining cell

          for singleton chaining:
               call imm32
               rPC
               codePtr (offset address of movl)
           after chaining:
               codePtr is filled with absolute address to the target
           after unchaining:
               codePtr is filled with absolute adress of the chaining cell

           for hot chaining:
               call imm32
               rPC
               codePtr (offset address of jmp or movl)
               ismove_flag
           after chaining:
               if (ismove_flag)
                 codePtr is filled with a relative offset to the target
               else
                 codePtr is filled with absolute address to the target
           after unchaining:
               if (ismove_flag)
                 codePtr is filled with original relative offset to the chaining cell
               else
                 codePtr is filled with absolute adress of the chaining cell

           Space occupied by the chaining cell in bytes:
                normal, singleton: 5+4+4
                backward: 5+4+4+4+4
                hot: 5+4+4+4
                codePtr should be within 16B line.

           Space for predicted chaining: 5 words = 20 bytes + padding to make it 4-byte aligned
        */
        int elemSize = 0;

        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: unchaining type %d count %d", i, pChainCellCounts->u.count[i]));

        for (j = 0; j < pChainCellCounts->u.count[i]; j++) {
            switch(i) {
                case kChainingCellNormal:
                    int isSwitch;

                    COMPILER_TRACE_CHAINING(
                        ALOGI("Jit Runtime: unchaining of normal"));
                    elemSize = BYTES_OF_NORMAL_CHAINING;
                    patchAddr = (u1 *)(*(int *)((char*)pChainCells + OFFSET_OF_PATCHADDR));
                    isSwitch = *(int *)((char*)pChainCells + OFFSET_OF_ISSWITCH);
                    if (patchAddr != 0) {
                        if (isSwitch != 0) {
                            updateCodeCache(*(int*)patchAddr, (int)pChainCells);
                        }
                        else {
                            relativeNCG = (pChainCells - patchAddr) - BYTES_OF_32BITS;
                            updateCodeCache(*(int*)patchAddr, relativeNCG);
                        }
                    }
                    break;
                case kChainingCellHot:
                    COMPILER_TRACE_CHAINING(
                        ALOGI("Jit Runtime: unchaining of hot"));
                    elemSize = BYTES_OF_HOT_CHAINING;
                    patchAddr = (u1 *)(*(int *)((char*)pChainCells + OFFSET_OF_PATCHADDR));
                    ismove_flag = *(int *)((char*)pChainCells + OFFSET_OF_ISMOVEFLAG);
                    if (patchAddr) {
                        if (ismove_flag) {
                            relativeNCG = (pChainCells - patchAddr) - BYTES_OF_32BITS;
                            updateCodeCache(*(int*)patchAddr, relativeNCG);
                        } else
                            updateCodeCache(*(int*)patchAddr, (int)pChainCells);
                    }
                    break;
                case kChainingCellInvokeSingleton:
                    COMPILER_TRACE_CHAINING(
                        ALOGI("Jit Runtime: unchaining of singleton"));
                    elemSize = BYTES_OF_SINGLETON_CHAINING;
                    patchAddr = (u1 *)(*(int *)((char*)pChainCells + OFFSET_OF_PATCHADDR));
                    if (patchAddr)
                        updateCodeCache(*(int*)patchAddr, (int)pChainCells);
                    break;
                case kChainingCellBackwardBranch:
                    COMPILER_TRACE_CHAINING(
                        ALOGI("Jit Runtime: unchaining of backward"));
                    elemSize = BackwardBranchChainingCellContents::unchain (pChainCells);
                    break;
                case kChainingCellInvokePredicted:
                    COMPILER_TRACE_CHAINING(
                        ALOGI("Jit Runtime: unchaining of predicted"));
                    //The cell is always 4-byte aligned so we need to take that
                    //into account first
                    padding = (4 - ((u4) pChainCells & 3)) & 3;
                    pChainCells += padding;

                    predChainCell = reinterpret_cast<PredictedChainingCell *> (
                            pChainCells);
                    /*
                     * There could be a race on another mutator thread to use
                     * this particular predicted cell and the check has passed
                     * the clazz comparison. So we cannot safely wipe the
                     * method and branch but it is safe to clear the clazz,
                     * which serves as the key.
                     */
                    predChainCell->clazz = PREDICTED_CHAIN_CLAZZ_INIT;

                    elemSize = sizeof(*predChainCell);
                    break;
                default:
                    ALOGI("JIT_INFO: Unexpected chaining type: %d", i);
                    //Error is beyond the scope of the x86 JIT back-end
                    ALOGE("\t FATAL ERROR. ABORTING!");
                    dvmAbort();  // dvmAbort OK here - can't safely recover
            }
            COMPILER_TRACE_CHAINING(
                ALOGI("Jit Runtime: unchaining 0x%x", (int)pChainCells));
            pChainCells += elemSize;  /* Advance by a fixed number of bytes */
        }
    }
    return NULL;
}

/* Unchain all translation in the cache. */
void dvmJitUnchainAll()
{
    ALOGV("Jit Runtime: unchaining all");
    if (gDvmJit.pJitEntryTable != NULL) {
        COMPILER_TRACE_CHAINING(ALOGI("Jit Runtime: unchaining all"));
        dvmLockMutex(&gDvmJit.tableLock);

        UNPROTECT_CODE_CACHE(gDvmJit.codeCache, gDvmJit.codeCacheByteUsed);
        // Unprotect the data cache if the data cache exists
        if (gDvmJit.dataCache != NULL) {
            UNPROTECT_DATA_CACHE(gDvmJit.dataCache, gDvmJit.dataCacheByteUsed);
        }

        for (size_t i = 0; i < gDvmJit.jitTableSize; i++) {
            if (gDvmJit.pJitEntryTable[i].dPC &&
                !gDvmJit.pJitEntryTable[i].u.info.isMethodEntry &&
                gDvmJit.pJitEntryTable[i].codeAddress) {
                      dvmJitUnchain(gDvmJit.pJitEntryTable[i].codeAddress);
            }
        }

        PROTECT_CODE_CACHE(gDvmJit.codeCache, gDvmJit.codeCacheByteUsed);
        // Protect the data cache if the data cache exists
        if (gDvmJit.dataCache != NULL) {
            PROTECT_DATA_CACHE(gDvmJit.dataCache, gDvmJit.dataCacheByteUsed);
        }

        dvmUnlockMutex(&gDvmJit.tableLock);
        gDvmJit.translationChains = 0;
    }
    gDvmJit.hasNewChain = false;
}

/**
 * @brief check if the predecessor of a normal chaining cell block contains a switch bytecode in the end of block
 * @param cUnit the compilation unit
 * @param normalChainingCellBB the normal chaining cell basic block
 * return true if normal chaining cell block have switch bytecode block as predecessor
 */
static bool isSwitchPred(CompilationUnit *cUnit,  BasicBlock_O1 * normalChainingCellBB)
{
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (normalChainingCellBB->predecessors, &bvIterator);

    int blockIdx = dvmBitVectorIteratorNext (&bvIterator);

    // if no predecessor found
    if (blockIdx == -1) {
        return false;
    }

    BasicBlock_O1 *predBB = reinterpret_cast<BasicBlock_O1 *> (dvmGrowableListGetElement (
                    &cUnit->blockList, blockIdx));
    if (predBB != 0 &&
        predBB->blockType == kDalvikByteCode &&
        predBB->lastMIRInsn != 0 && (
        predBB->lastMIRInsn->dalvikInsn.opcode == OP_PACKED_SWITCH ||
        predBB->lastMIRInsn->dalvikInsn.opcode == OP_SPARSE_SWITCH))
        return true;
    return false;
}

/**
 * @brief fill fields in a switchNormalCCInfo item and insert this item into swithNormalCCList
 * @param cUnit the compilation unit
 * @param startOfNormal the start address of the normal chaining cell
 * @param patchAddr the address for the codePtr field in normal chaining cell
 */
static void createSwitchNormalInfo(CompilationUnit_O1 *cUnit, char* startOfNormal, char* patchAddr)
{
    SwitchNormalCCInfo switchNormalCCInfo;

    // fill the two address fields
    switchNormalCCInfo.patchAddr = patchAddr;
    switchNormalCCInfo.normalCCAddr = startOfNormal;

    assert(cUnit->getSwitchInfo() != 0);

    // insert the new item into the switchNormalCCList
    cUnit->getSwitchInfo()->switchNormalCCList.push_back(switchNormalCCInfo);
}

/* Chaining cell for code that may need warmup. */
/* ARM assembly: ldr r0, [r6, #76] (why a single instruction to access member of glue structure?)
                 blx r0
                 data 0xb23a //bytecode address: 0x5115b23a
                 data 0x5115
   IA32 assembly:
                  call imm32 //relative offset to dvmJitToInterpNormal
                  rPC
                  codePtr
                  isSwitch
*/
/**
 * @brief Generates code for normal chaining cell.
 * @param cUnit the compilation unit
 * @param offset the target bytecode offset
 * @param normalChainingCellBB the normal chaining cell handled here
 * @return return 0
 */
static int handleNormalChainingCell(CompilationUnit_O1 *cUnit, unsigned int offset, BasicBlock_O1 *normalChainingCellBB)
{
    ALOGV("In handleNormalChainingCell for method %s block %d BC offset %x NCG offset %x",
          cUnit->method->name, blockId, offset, stream - streamMethodStart);
    if(dump_x86_inst)
        ALOGI("LOWER NormalChainingCell at offsetPC %x offsetNCG %x @%p",
              offset, stream - streamMethodStart, stream);
    int isSwitch = 0;

    if (isSwitchPred(cUnit, normalChainingCellBB) == true) {
        isSwitch = 1;
    }

    char *startOfNormal = stream;
#ifndef WITH_SELF_VERIFICATION
    call_dvmJitToInterpNormal();
#else
    call_dvmJitToInterpBackwardBranch();
#endif
    unsigned int *ptr = (unsigned int*)stream;
    *ptr++ = (unsigned int)(cUnit->method->insns + offset);

    char *codePtr = NULL;
    if (isSwitch == false) {
        codePtr = searchNCGWorklist(normalChainingCellBB->id);
    }
    else {
        createSwitchNormalInfo(cUnit, startOfNormal, (char*)ptr);
    }
    *ptr++ = (unsigned int)codePtr;
    *ptr++ = (unsigned int)isSwitch;
    stream = (char*)ptr;
    return 0;
}

/*
 * Chaining cell for instructions that immediately following already translated
 * code.
   IA32 assembly:
                  call imm32 // relative offset to dvmJitToInterpNormal or dvmJitToInterpTraceSelect
                  rPC
                  codePtr
                  ismove_flag
 */
static int handleHotChainingCell(CompilationUnit *cUnit, unsigned int offset, int blockId)
{
    ALOGV("In handleHotChainingCell for method %s block %d BC offset %x NCG offset %x",
          cUnit->method->name, blockId, offset, stream - streamMethodStart);
    if(dump_x86_inst)
        ALOGI("LOWER HotChainingCell at offsetPC %x offsetNCG %x @%p",
              offset, stream - streamMethodStart, stream);

    int isMove = 0;
    char* codePtr = searchChainingWorklist(blockId);
    if (codePtr == NULL) {
        codePtr = searchNCGWorklist(blockId);
        if (codePtr != 0) {
            isMove = 1;
        }
    }
    call_dvmJitToInterpTraceSelect();
    unsigned int *ptr = (unsigned int*)stream;
    *ptr++ = (unsigned int)(cUnit->method->insns + offset);
    *ptr++ = (unsigned int)codePtr;
    *ptr++ = (unsigned int)isMove;
    stream = (char*)ptr;
    return 0;
}

/**
 * @brief Generates code for backward branch chaining cell.
 * @param cUnit the compilation unit
 * @param chainingCell the chaining cell we are generating code for
 * @return true if chaining cell was successfully generated
 */
static bool handleBackwardBranchChainingCell (CompilationUnit *cUnit,
        BasicBlock_O1 *chainingCell)
{
    assert(chainingCell != 0);
    assert(chainingCell->blockType == kChainingCellBackwardBranch);

    //Get the loop entry
    BasicBlock *loopEntry = chainingCell->fallThrough;

    //Paranoid
    assert(cUnit->loopInformation != 0);

    //We want the loop header and preloop header
    char *loopHeaderAddr = 0;
    char *preLoopHeaderAddr = 0;

    BasicBlock_O1 *bbO1 = reinterpret_cast<BasicBlock_O1 *> (loopEntry);
    assert(bbO1 != 0);

    //Set the loop header address
    loopHeaderAddr = bbO1->streamStart;

    //Get the associated loop information
    LoopInformation *info = cUnit->loopInformation;

    //But if info is 0, we might not have that and should just use the fallThrough's information
    //This can happen if the user has used the old loop system, and should only happen then
    if (info == 0)
    {
        //Then request the interpreter jump back to where the loop is
        preLoopHeaderAddr = loopHeaderAddr;
    }
    else
    {
        //Get the right loop
        info = info->getLoopInformationByEntry (loopEntry);

        //Paranoid
        if (info != 0)
        {
            //We have a preLoop
            BasicBlock *preLoop = info->getPreHeader ();

            //Paranoid
            if (preLoop != 0)
            {
                bbO1 = reinterpret_cast<BasicBlock_O1 *> (preLoop);

                //Paranoid
                if (bbO1 != 0)
                {
                    preLoopHeaderAddr = bbO1->streamStart;
                }
            }
        }
    }

    //If we cannot find these, then we have a problem
    if (loopHeaderAddr == 0 || preLoopHeaderAddr == 0)
    {
        return false;
    }

    //Every backward branch chaining cell must have a prebackward
    //predecessor. So we look for it.
    if (chainingCell->predecessors == 0)
    {
        return false;
    }

    //Initialize iterator
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (chainingCell->predecessors, &bvIterator);

    //Get the block index of predecessor
    int blockIdx = dvmBitVectorIteratorNext (&bvIterator);

    //Return false if we did not find predecessor
    if (blockIdx == -1)
    {
        return false;
    }

    //Get the predecessor block
    BasicBlock_O1 *preBackward =
            reinterpret_cast<BasicBlock_O1 *> (dvmGrowableListGetElement (
                    &cUnit->blockList, blockIdx));

    //If it is not the right type then we return false
    if (preBackward == 0 || preBackward->blockType != kPreBackwardBlock)
    {
        return false;
    }

    char *vrStoreCodePtr = preBackward->streamStart;

    //We should have already generated code for the prebackward block
    if (vrStoreCodePtr == 0)
    {
        return false;
    }

    //If scheduling is enabled, lets assert that queue is empty. Otherwise,
    //it is not safe to use the stream pointer.
    if (gDvmJit.scheduling)
    {
        //Using stream pointer is not safe unless scheduler queue is empty.
        //We should never get here with anything in queue.
        if (singletonPtr<Scheduler>()->isQueueEmpty() == false)
        {
            return false;
        }
    }

    //At this point we have tried gathering all information we could so we
    //ready to generate the chaining cell
    if (cUnit->printMe)
    {
        ALOGI("LOWER BackwardBranchChainingCell with offsetPC %x @%p",
                chainingCell->startOffset, stream);
    }

    BackwardBranchChainingCellContents *backwardContents =
            reinterpret_cast<BackwardBranchChainingCellContents *> (stream);

    //Generate the call to interpreter
    call_dvmJitToInterpBackwardBranch ();

    //Paranoid, we want to make sure that chaining cell has enough room
    //for the call instruction
    assert((reinterpret_cast<int>(stream) - reinterpret_cast<int>(backwardContents))
            == sizeof(backwardContents->instructionHolder));

    //Find the jump that goes to the prebackward block
    char *codePtr = searchNCGWorklist (preBackward->id);

    //If we cannot find this jump, something went wrong
    if (codePtr == 0)
    {
        return false;
    }

    //Now write the data into the chaining cell
    backwardContents->nextPC =
            reinterpret_cast<unsigned int> (cUnit->method->insns
                    + chainingCell->startOffset);
    backwardContents->codePtr = codePtr;
    backwardContents->loopHeaderAddr = loopHeaderAddr;
    backwardContents->vrWriteBackAddr = vrStoreCodePtr;
    backwardContents->loopPreHeaderAddr = preLoopHeaderAddr;

    //Update stream pointer
    stream = reinterpret_cast<char *> (backwardContents)
            + sizeof(*backwardContents);

    //We have successfully generated the chaining cell
    return true;
}

/* Chaining cell for monomorphic method invocations.
   IA32 assembly:
                  call imm32 // relative offset to dvmJitToInterpTraceSelect
                  rPC
                  codePtr
                  flag // dummy flag
*/
static int handleInvokeSingletonChainingCell(CompilationUnit *cUnit,
                                              const Method *callee, int blockId)
{
    ALOGV("In handleInvokeSingletonChainingCell for method %s block %d callee %s NCG offset %x",
          cUnit->method->name, blockId, callee->name, stream - streamMethodStart);
    if(dump_x86_inst)
        ALOGI("LOWER InvokeSingletonChainingCell at block %d offsetNCG %x @%p",
              blockId, stream - streamMethodStart, stream);

    call_dvmJitToInterpTraceSelect();
    unsigned int *ptr = (unsigned int*)stream;
    *ptr++ = (unsigned int)(callee->insns);
    char* codePtr = searchChainingWorklist(blockId);
    *ptr++ = (unsigned int)codePtr;
    *ptr++ = 0;
    stream = (char*)ptr;
    return 0;
}

/**
 * @brief Generates code for predicted chaining cell.
 * @details This chaining cell is used for polymorphic invocations.
 * @param cUnit the compilation unit
 * @param chainingCell the chaining cell we are generating code for
 * @return true if chaining cell was successfully generated
 */
static bool handleInvokePredictedChainingCell (CompilationUnit *cUnit,
        BasicBlock_O1 *chainingCell)
{
    if(cUnit->printMe)
    {
        ALOGI("LOWER InvokePredictedChainingCell (block %d) @%p",
                chainingCell->id, stream);
    }

#ifdef PREDICTED_CHAINING

    //Because we will be patching this at runtime, we want to make sure that
    //the chaining cell is 4 byte aligned. Since every field of the chaining
    //cell is 4 byte wide, this will ensure atomic updates since the cell
    //won't be split across cache line.
    int padding = (4 - ((u4) stream & 3)) & 3;
    stream += padding;

    //Since we are aligning, we should also update the offset so anyone using
    //it accesses the correct data.
    chainingCell->label->lop.generic.offset += padding;

    PredictedChainingCell *predictedContents =
            reinterpret_cast<PredictedChainingCell *> (stream);

    //Now initialize the data using the predefined macros for initialization
    predictedContents->branch = PREDICTED_CHAIN_BX_PAIR_INIT1;
    predictedContents->branch2 = PREDICTED_CHAIN_BX_PAIR_INIT2;
    predictedContents->clazz = PREDICTED_CHAIN_CLAZZ_INIT;
    predictedContents->method = PREDICTED_CHAIN_METHOD_INIT;
    predictedContents->stagedClazz = PREDICTED_CHAIN_COUNTER_INIT;

    //Update stream pointer
    stream = reinterpret_cast<char *> (predictedContents)
            + sizeof(*predictedContents);

#else
    //assume rPC for callee->insns in %ebx
    scratchRegs[0] = PhysicalReg_EAX;
#if defined(WITH_JIT_TUNING)
    /* Predicted chaining is not enabled. Fall back to interpreter and
     * indicate that predicted chaining was not done.
     */
    move_imm_to_reg(OpndSize_32, kInlineCacheMiss, PhysicalReg_EDX, true);
#endif
    call_dvmJitToInterpTraceSelectNoChain();
#endif

    //We have successfully generated the chaining cell
    return true;
}

/**
 * @brief Used to handle semantics of extended MIRs, including possibly generating native code.
 * @param cUnit The compilation unit
 * @param bb The basic block containing the MIR
 * @param mir The extended instruction
 * @return Returns whether or not it successfully handled the extended MIR
 */
bool handleExtendedMIR (CompilationUnit *cUnit, BasicBlock_O1 *bb, MIR *mir)
{
    if (cUnit->printMe == true)
    {
        char * decodedString = dvmCompilerGetDalvikDisassembly(&mir->dalvikInsn,
                NULL);
        ALOGI("LOWER %s @%p\n", decodedString, stream);
    }

    //Assume that we will be able to handle it
    bool result = true;

    switch ((ExtendedMIROpcode) mir->dalvikInsn.opcode)
    {
        case kMirOpPhi:
            //Nothing to do
            break;
        case kMirOpNullCheck:
            genHoistedNullCheck (cUnit, mir);
            break;
        case kMirOpBoundCheck:
        {
            ExecutionMode origMode = gDvm.executionMode;
            gDvm.executionMode = kExecutionModeNcgO0;

            genHoistedBoundCheck (cUnit, mir);

            gDvm.executionMode = origMode;
            break;
        }
        case kMirOpNullNRangeUpCheck:
        {
            ExecutionMode origMode = gDvm.executionMode;
            gDvm.executionMode = kExecutionModeNcgO0;

            genHoistedChecksForCountUpLoop (cUnit, mir);

            gDvm.executionMode = origMode;
            break;
        }
        case kMirOpNullNRangeDownCheck:
        {
            ExecutionMode origMode = gDvm.executionMode;
            gDvm.executionMode = kExecutionModeNcgO0;

            genHoistedChecksForCountDownLoop (cUnit, mir);

            gDvm.executionMode = origMode;
            break;
        }
        case kMirOpLowerBound:
        {
            ExecutionMode origMode = gDvm.executionMode;
            gDvm.executionMode = kExecutionModeNcgO0;

            genHoistedLowerBoundCheck (cUnit, mir);

            gDvm.executionMode = origMode;
            break;
        }
        case kMirOpRegisterize:
            result = genRegisterize (cUnit, bb, mir);
            break;
        case kMirOpCheckInlinePrediction:
            result = genValidationForPredictedInline (cUnit, mir);
            break;
        case kMirOpMove128b:
            result = genMove128b (cUnit, mir);
            break;
        case kMirOpPackedAddition:
            result = genPackedAlu (cUnit, mir, add_opc);
            break;
        case kMirOpPackedMultiply:
            result = genPackedAlu (cUnit, mir, mul_opc);
            break;
        case kMirOpPackedSubtract:
            result = genPackedAlu (cUnit, mir, sub_opc);
            break;
        case kMirOpPackedXor:
            result = genPackedAlu (cUnit, mir, xor_opc);
            break;
        case kMirOpPackedOr:
            result = genPackedAlu (cUnit, mir, or_opc);
            break;
        case kMirOpPackedAnd:
            result = genPackedAlu (cUnit, mir, and_opc);
            break;
        case kMirOpPackedShiftLeft:
            result = genPackedAlu (cUnit, mir, shl_opc);
            break;
        case kMirOpPackedSignedShiftRight:
            result = genPackedAlu (cUnit, mir, sar_opc);
            break;
        case kMirOpPackedUnsignedShiftRight:
            result = genPackedAlu (cUnit, mir, shr_opc);
            break;
        case kMirOpPackedAddReduce:
            result = genPackedHorizontalOperationWithReduce (cUnit, mir, add_opc);
            break;
        case kMirOpPackedReduce:
            result = genPackedReduce (cUnit, mir);
            break;
        case kMirOpConst128b:
            result = genMoveData128b (cUnit, mir);
            break;
        case kMirOpPackedSet:
            result = genPackedSet (cUnit, mir);
            break;
        case kMirOpCheckStackOverflow:
            genCheckStackOverflow (cUnit, mir);
            break;
        default:
        {
            char * decodedString = dvmCompilerGetDalvikDisassembly(&mir->dalvikInsn, NULL);
            ALOGD ("JIT_INFO: No logic to handle extended MIR %s", decodedString);
            result = false;
            break;
        }
    }

    return result;
}

/**
 * @brief Print the content of a trace to LOG.
 * @param basicCompilationUnit - pointer to the CompilationUnit
 * @param chainCellCounts - reference to the ChainCellCounts table
 * @param wide_const_count - number of long/double constants in the constant section
 * @param pCCOffsetSection - pointer to the chaining cell offset header
 */
void dvmCompilerLcgPrintTrace (CompilationUnit *basicCompilationUnit, ChainCellCounts &chainCellCounts, int wide_const_count, u2* pCCOffsetSection)
{
    CompilationUnit_O1 *cUnit = static_cast<CompilationUnit_O1 *> (basicCompilationUnit);
    char *next_code_ptr = 0;

    next_code_ptr = dvmCompilerPrintTrace (cUnit);

    if (next_code_ptr == 0)
    {
        // simply return if there is no entry in code block
        return;
    }

    // print switch table section if any
    if ((cUnit->getSwitchInfo () != 0) && (cUnit->getSwitchInfo ()->tSize > 0))
    {
        // 4 byte aligned
        next_code_ptr = reinterpret_cast<char*>((reinterpret_cast<unsigned int>(next_code_ptr) + 3) & ~0x3);
        ALOGD ("** // Switch Table section (4B aligned)");
        unsigned int *stPtr = (unsigned int *)next_code_ptr;
        int switchTableSize = MIN (cUnit->getSwitchInfo ()->tSize, MAX_CHAINED_SWITCH_CASES) + 1;
        for (int i = 0; i < switchTableSize; i++)
        {
            ALOGD ("**  %p: %#x", (void*) stPtr, *stPtr);
            stPtr++;
        }
        next_code_ptr = (char*)stPtr;
    }

    next_code_ptr = dvmCompilerPrintChainingCellCounts (next_code_ptr, chainCellCounts);

    // print the long/double constant section if any
    if (wide_const_count > 0)
    {
        long long *llptr;
        double *dblptr;
        ALOGD ("** // long/double constant section (16B aligned)");
        next_code_ptr = reinterpret_cast<char*>((reinterpret_cast<unsigned int>(next_code_ptr) + 0xF) & ~0xF);
        llptr = (long long *) next_code_ptr;
        for (int i = 0; i < wide_const_count; i++)
        {
            dblptr = (double *) llptr;
            ALOGD ("**  %p: %lld (%g)", llptr, *llptr, *dblptr);
            llptr++;        // increases pointer by 8B
        }
    }

    dvmCompilerPrintChainingCellOffsetHeader (pCCOffsetSection);
}

/**
 * @brief Handle fallthrough branch: determine whether we need one or not
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @param ptrNextFallThrough pointer to the nextFallThrough if requested (can be 0)
 */
static void handleFallThroughBranch (CompilationUnit *cUnit, BasicBlock *bb, BasicBlock **ptrNextFallThrough)
{
    //Get next fall through
    BasicBlock *nextFallThrough = *ptrNextFallThrough;

    //Check if the jump needs alignment. If it needs alignment it means it will be patched at runtime
    //and thus we cannot skip generating this jump
    bool jumpNeedsAlignment = false;
    if (nextFallThrough != 0)
    {
        jumpNeedsAlignment = doesJumpToBBNeedAlignment (nextFallThrough);
    }

    //We need a fallthrough branch if we had a next and it isn't the current BasicBlock or jump is needed
    bool needFallThroughBranch = (nextFallThrough != 0 && (jumpNeedsAlignment == true || bb != nextFallThrough));

    if (needFallThroughBranch == true)
    {
        //Generate the jump now
        jumpToBasicBlock (nextFallThrough->id, jumpNeedsAlignment);
    }

    //Clear it
    *ptrNextFallThrough = 0;
}

/**
 * @brief Create a new record of 64bit constant in use
 * @details Allocates memory to store a 64bit constant and its details. All address fields
 *          are initialized to NULL.
 * @param listPtr address of the constList
 * @param constL the lower 32bits
 * @param constH the higher 32bits
 * @param reg Virtual Register number
 * @param align Align to 16 bytes
 */
void addNewToConstList(struct ConstInfo** listPtr, int constL, int constH, int reg, bool align) {
    struct ConstInfo* tmpPtr =static_cast<ConstInfo *>(dvmCompilerNew(sizeof(ConstInfo), false));
    tmpPtr->valueL = constL;
    tmpPtr->valueH = constH;
    tmpPtr->regNum = reg;
    tmpPtr->offsetAddr = 0;
    tmpPtr->streamAddr = NULL;
    tmpPtr->constAddr = NULL;
    tmpPtr->constAlign = align;
    tmpPtr->next = *listPtr;
    *listPtr = tmpPtr;
    assert(*listPtr != NULL);
}

/**
 * @brief Save address of memory access into constList
 * @details Populates stream information
 * @param listPtr address of the constList
 * @param constL the lower 32bits
 * @param constH the higher 32bits
 * @param reg Virtual Register number
 * @param patchAddr The address of memory location to be patched currently
 * @param offset the offset where to save the constant
 * @return true when it succeeds, false when it fails
 */
bool saveAddrToConstList(struct ConstInfo** listPtr, int constL, int constH, int reg, char* patchAddr, int offset) {
    struct ConstInfo* tmpPtr = *listPtr;
    while (tmpPtr != NULL) {                // check all elements of the structure
        if (tmpPtr->valueL == constL && tmpPtr->valueH == constH && tmpPtr->regNum == reg && tmpPtr->streamAddr==NULL) {
            tmpPtr->streamAddr = patchAddr; // save address of instruction in jit stream
            tmpPtr->offsetAddr = offset;    // save offset to memory location to patch for the instruction
#ifdef DEBUG_CONST
            ALOGD("**Save constants for VR# %d containing constant (%x):(%x) streamAddr is (%d)%x, offset %d",
                             tmpPtr->regNum, tmpPtr->valueL, tmpPtr->valueH,
                             tmpPtr->streamAddr, tmpPtr->streamAddr, tmpPtr->offsetAddr);
#endif
            return true;
        }
        tmpPtr = tmpPtr->next; // check next element
    }
    return false;
}

/**
 * @brief insert 64bit constants in a Constant Data Section at end of a trace
 * @details Populates stream information
 * @param constListTemp pointer to list of 64 bit constants
 * @param stream pointer to jit code or data cache
 * @return the updated value of stream
 */
char* insertConstDataSection(struct ConstInfo *constListTemp,  char * stream) {
    unsigned int *intaddr = reinterpret_cast<unsigned int *>(stream);
    while(constListTemp != NULL){

        /* Align trace to 16-bytes before Constant Data Section */
        if (constListTemp->constAlign == true) {
            stream = (char*)(((unsigned int)stream + 0xF) & ~0xF);
        }
        constListTemp->constAddr = stream;
        intaddr = reinterpret_cast<unsigned int *>(stream);
        *intaddr = constListTemp->valueL;    // store lower 32 bit of a constant
#ifdef DEBUG_CONST
        ALOGI("**Lower constants at  %p: %d(%x), VR# %d containing constant (%x):(%x) constAddr is %p",
                             intaddr, *intaddr, *intaddr, constListTemp->regNum, constListTemp->valueL, constListTemp->valueH,
                             *intaddr, constListTemp->constAddr);
#endif
        intaddr++;
        *intaddr = constListTemp->valueH;    // store higher 32 bits of a constant
        intaddr++;
        stream = reinterpret_cast<char *>(intaddr);
        constListTemp = constListTemp->next; // move to next constant in list
    }
    return stream;
}

/**
 * @brief patch stream with address of constants in Constant Data Section
 * @details lowers address of constant if placeholder data is found
 * @param constListTemp pointer to list of 64 bit constants
 * @param cUnit the compilation unit
 * @return returns -1 if error, else reports number of patches
 */
int patchConstToStream(struct ConstInfo *constListTemp, CompilationUnit *cUnit) {
    unsigned int *writeval;
    char *iaddr;
    int pResult = 0;

    while (constListTemp != NULL){
        /* iterate through the generated code to patch constants */
        iaddr = static_cast<char*>(constListTemp->streamAddr+constListTemp->offsetAddr);

        //If the patching address is null, then we can just skip it because we have nothing to update
        if (iaddr == 0)
        {
            //Advance to the next constant that needs handled
            constListTemp = constListTemp->next;

            //We had no work to do so we succesfully handled this case
            pResult++;
            continue;
        }

        writeval = reinterpret_cast<unsigned int*>(iaddr);
        unsigned int dispAddr =  getGlobalDataAddr("64bits");

        if (*writeval == dispAddr){ /* verify that place holder data inserted is present */
            *writeval = reinterpret_cast<unsigned int>(constListTemp->constAddr);
#ifdef DEBUG_CONST
            ALOGI("Patched location of VR# %d with constant (%x):(%x)",
                            constListTemp->regNum, constListTemp->valueL, constListTemp->valueH);
            ALOGI("Address is streamAddr %p,  offset %d with constAddr %p",
                       constListTemp->streamAddr, constListTemp->offsetAddr, constListTemp->constAddr);
#endif
            pResult++;              /* keep count of successful patches in stream */
        } else {
            ALOGI("JIT_INFO: Error Wrong value found at streamAddr");
#ifdef DEBUG_CONST
            ALOGI("Tried patching VR# %d with constant (%x):(%x)",
                            constListTemp->regNum, constListTemp->valueL, constListTemp->valueH);
            ALOGI("Address is streamAddr %p, offset %d with constAddr %p",
                    constListTemp->streamAddr, constListTemp->offsetAddr, constListTemp->constAddr);
#endif
            ALOGI("JIT_INFO: Constant init opt could not patch all required locations");
            SET_JIT_ERROR(kJitErrorConstInitFail);
            cUnit->constListHead = NULL;
            return -1;              /* incorrect data found at patch location, reject trace */
        }
        constListTemp = constListTemp->next;
    }
    return pResult;
}

/**
 * @brief Generate the code for the BasicBlock
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @param nextFallThrough a pointer to the next fall through BasicBlock
 * @return whether the generation went well
 */
static bool generateCode (CompilationUnit_O1 *cUnit, BasicBlock *bb, BasicBlock **nextFallThrough)
{
    if (cUnit->printMe)
    {
        char blockName[BLOCK_NAME_LEN];
        dvmGetBlockName (bb, blockName);
        ALOGD ("LOWER BB%d type:%s hidden:%s @%p", bb->id, blockName, bb->hidden ? "yes" : "no", stream);
    }

    /* We want to update the stream start to remember it for future backward chaining cells */
    BasicBlock_O1 *bbO1 = reinterpret_cast<BasicBlock_O1 *> (bb);
    assert (bbO1 != 0);
    bbO1->streamStart = stream;

#ifdef WITH_JIT_TPROFILE
    //Generate the loop counter profile code for loop
    genLoopCounterProfileCode(cUnit, bbO1);
#endif

    //Generate the code
    startOfBasicBlock(bb);
    int cg_ret = codeGenBasicBlockJit(cUnit->method, bb, cUnit);
    endOfBasicBlock(bb);

    //Error handling, we return false
    if(cg_ret < 0 || IS_ANY_JIT_ERROR_SET()) {
        ALOGI("Could not compile trace for %s%s, offset %d",
                cUnit->method->clazz->descriptor, cUnit->method->name,
                cUnit->traceDesc->trace[0].info.frag.startOffset);
        SET_JIT_ERROR(kJitErrorCodegen);
        endOfTrace (cUnit);
        return false;
    }

    //Register next fall through
    *nextFallThrough = bb->fallThrough;

    //Everything went fine
    return true;
}

/**
 * @brief create a switch table in the end of trace and finish the patching needed in switch bytecode lowered instruction and normal chaining cells
 * @param cUnit the compilation unit
 * @param stream pointer to jit code or data cache
 * @return the updated value of the stream pointer
 */
static char *createSwitchTable(CompilationUnit_O1 *cUnit, char *stream)
{
    // align switch table start address to 4 byte aligned
    int padding = (4 - ((u4) stream & 3)) & 3;
    stream += padding;

    assert(cUnit->getSwitchInfo() != NULL);

    unsigned int *immAddr = reinterpret_cast<unsigned int*>((cUnit->getSwitchInfo())->immAddr);
    assert(immAddr != NULL);

    // Patched the instruction with the switch table address
    *immAddr = reinterpret_cast<unsigned int>(stream);

    unsigned int *immAddr2 = reinterpret_cast<unsigned int*>(cUnit->getSwitchInfo()->immAddr2);
    if (immAddr2 != 0) {

        // Patched the instruction with the switch table address
        *immAddr2 = reinterpret_cast<unsigned int>(stream);
    }

    std::vector<SwitchNormalCCInfo> &switchNormalCCList = cUnit->getSwitchInfo()->switchNormalCCList;
    unsigned int *ptr = reinterpret_cast<unsigned int*>(stream);
    unsigned int *patchAddr;

    // Initialize switch table in the end of trace with start address of each normal chaining cell and backpatched patchAddr field in normal chaining cell
    for (unsigned int i = 0; i < switchNormalCCList.size(); i++) {
        *ptr = reinterpret_cast<unsigned int>(switchNormalCCList[i].normalCCAddr);
        patchAddr = reinterpret_cast<unsigned int*>(switchNormalCCList[i].patchAddr);
        *patchAddr = reinterpret_cast<unsigned int>(ptr);
        ptr ++;
    }

    // update stream pointer
    stream = reinterpret_cast<char *>(ptr);
    return stream;
}

/**
 * @brief Write data that includes the switch table and the constant data section to the data cache if possible or write to the code cache as fallback
 * @param cUnit the compilation unit
 * @param patchCount the number of patches for constant data that occur
 * @return true if success
 */
static bool writeDataToDataOrCodeCache(CompilationUnit_O1 *cUnit, int &patchCount)
{
    // Process the switch table and the constant data section
    // Estimate the switch table size
    size_t switchTableAlignment = 0;
    size_t switchTableSize = 0;
    if (cUnit->getSwitchInfo() != 0) {
        switchTableSize = (MIN(cUnit->getSwitchInfo()->tSize, MAX_CHAINED_SWITCH_CASES) + 1) * 4;
        // Align the switch table to 4 bytes
        if (switchTableSize > 0) {
            switchTableAlignment = 4;
        }
    }

    // Estimate the constant data section size
    size_t constDataAlignment = 0;
    size_t constDataSize = 0;
    if (((gDvmJit.disableOpt & (1 << kElimConstInitOpt)) == false) && cUnit->constListHead != NULL ) {
        ConstInfo *constListTemp; // Temp ptr for constant initialization
        constListTemp = cUnit->constListHead;
        while(constListTemp != NULL) { // Find projected end of trace
            // We conservatively assume that each data needs 16 bytes due to alignment requirement
            constDataSize += 16;
            constListTemp = constListTemp->next;
        }
        // Align the const data section to 16 bytes
        if (constDataSize > 0) {
            constDataAlignment = 16;
        }
    }

    // Calculate the total estimated data size
    size_t totalEstimatedDataSize = switchTableAlignment + switchTableSize + constDataAlignment + constDataSize;

    // Check if we need to store any data
    if (totalEstimatedDataSize == 0) {
        // Nothing to store
        return true;
    }

    // Point to the stream start to write data
    char *streamDataStart = NULL;

    // Indicate if we can write data to the data cache
    bool useDataCache = false;

    // Check if we can store data to the data cache
    if (dvmCompilerWillDataCacheOverflow(totalEstimatedDataSize) == false) {
        // We can write data to the data cache
        useDataCache = true;

        // Set the start pointer for the data cache
        streamDataStart = (char*)gDvmJit.dataCache + gDvmJit.dataCacheByteUsed;

        // Unprotect data cache
        UNPROTECT_DATA_CACHE(streamDataStart, totalEstimatedDataSize);
    }
    else {
        // Set data cache full
        dvmCompilerSetDataCacheFull();

        // Check if we can store data to the code cache
        if (dvmCompilerWillCodeCacheOverflow(totalEstimatedDataSize) == true) {
            ALOGI("JIT_INFO: Code cache full after the switch table and the constant data section");
            SET_JIT_ERROR_MANUAL (cUnit, kJitErrorCodeCacheFull);
            dvmCompilerSetCodeAndDataCacheFull();
            cUnit->baseAddr = NULL;

            // Fail
            return false;
        }

        // Set the start pointer to the pointer for the code cache
        streamDataStart = stream;
    }

    // Point to the current location of the stream data
    char *streamData = streamDataStart;

    // Write the switch table
    if (switchTableSize > 0) {
        /* Align trace to 4-byte before the switch table */
        streamData = reinterpret_cast<char*>((reinterpret_cast<unsigned int>(streamData) + 0x3) & ~0x3);
        streamData = createSwitchTable(cUnit, streamData);
    }

    // Write the constant data section
    if (constDataSize > 0) {
        /* Align trace to 16-byte before Constant Data Section */
        streamData = reinterpret_cast<char*>((reinterpret_cast<unsigned int>(streamData) + 0xF) & ~0xF);
        streamData = insertConstDataSection(cUnit->constListHead, streamData);

        /* Patch address of constants into stream */
        patchCount = patchConstToStream(cUnit->constListHead, cUnit);
        if (patchCount < 0) {// if patchCount is less than 0, trigger error recovery
            ALOGI("JIT_INFO: Constant init opt could not patch all required locations");
            SET_JIT_ERROR_MANUAL (cUnit, kJitErrorConstInitFail);
            cUnit->baseAddr = NULL;
            cUnit->constListHead = NULL;
            if (useDataCache == true) {
                PROTECT_DATA_CACHE(streamDataStart, totalEstimatedDataSize);
            }

            // Fail
            return false;
        }
    }

    // Update totalSize
    cUnit->totalSize += (streamData - streamDataStart);

    if (useDataCache == true) {
        // Protect data cache
        PROTECT_DATA_CACHE(streamDataStart, totalEstimatedDataSize);

        // Update dataCacheByteUsed
        gDvmJit.dataCacheByteUsed += (streamData - streamDataStart); // store data cache byte used to include the current trace

        ALOGV("JIT data cache has the switch table and const data %uB", streamData - streamDataStart);
    }
    else {
        // Update codeCacheByteUsed
        gDvmJit.codeCacheByteUsed += (streamData - streamDataStart); // store code cache byte used to include the current trace

        // We need to update stream because it is a global pointer
        stream = streamData;

        ALOGV("JIT code cache has the switch table and const data %uB", streamData - streamDataStart);
    }

    // Reset constant data list head
    cUnit->constListHead = NULL;

    // Success, signal it
    return true;
}

/**
 * @brief Real Entry point of the LCG backend
 * @param cUnitME the CompilationUnit
 * @param info the JitTranslationInfo
 */
static void compileLCGMIR2LIR (CompilationUnit *cUnitME, JitTranslationInfo *info)
{
    //Get the CompilationUnit_O1
    CompilationUnit_O1 *cUnit = static_cast<CompilationUnit_O1 *> (cUnitME);

    //Used to determine whether we need a fallthrough jump
    BasicBlock *nextFallThrough = 0;
    // Define the code_block_table for tracking various type of code blocks
    //  for printing.
    char *print_stream_ptr = 0; // current block stream pointer

    dump_x86_inst = cUnit->printMe;

    GrowableList chainingListByType[kChainingCellLast];

    unsigned int i, padding;

    traceMode = cUnit->jitMode;

    //Initialize the base address to null
    cUnit->baseAddr = NULL;

    /*
     * Initialize various types chaining lists.
     */
    for (i = 0; i < kChainingCellLast; i++) {
        dvmInitGrowableList(&chainingListByType[i], 2);
    }

    GrowableListIterator iterator;

    //BasicBlock **blockList = cUnit->blockList;
    GrowableList *blockList = &cUnit->blockList;
    BasicBlock *bb;

    info->codeAddress = NULL;
    stream = (char*)gDvmJit.codeCache + gDvmJit.codeCacheByteUsed;

    streamStart = stream; /* trace start before alignment */

#if defined(WITH_JIT_TPROFILE)
    /* Align stream's address end with 0100, this is to make sure the code start address align to 16-bytes after add the extra bytes */
    stream = ((u4)stream & 0x7) < 4 ? (char*)(((unsigned int)stream + 0x4) & ~0x3) : (char*)(((unsigned int)stream + 0x8) & ~0x3);
    stream += EXTRA_BYTES_FOR_LOOP_COUNT_ADDR; /*This is for the loop count's addr*/
    stream += EXTRA_BYTES_FOR_PROF_ADDR; /* This is for the execution count's addr */

    //zero the loop count address, so we can check if the trace is a loop
    memset(stream - EXTRA_BYTES_FOR_LOOP_COUNT_ADDR - EXTRA_BYTES_FOR_PROF_ADDR, 0, EXTRA_BYTES_FOR_LOOP_COUNT_ADDR + EXTRA_BYTES_FOR_PROF_ADDR);
#endif

    stream += EXTRA_BYTES_FOR_CHAINING; /* This is needed for chaining. */
    stream = (char*)(((unsigned int)stream + 0xF) & ~0xF); /* Align trace to 16-bytes */
    streamMethodStart = stream; /* code start */

    cUnit->exceptionBlockId = -1;
    for (i = 0; i < blockList->numUsed; i++) {
        bb = (BasicBlock *) blockList->elemList[i];
        if(bb->blockType == kExceptionHandling)
            cUnit->exceptionBlockId = i;
    }
    startOfTrace(cUnit->method, cUnit->exceptionBlockId, cUnit);

    /* Traces start with a profiling entry point.  Generate it here */
    cUnit->profileCodeSize = genTraceProfileEntry(cUnit);

    cUnit->constListHead = NULL; // Initialize constant list

    if(gDvm.executionMode == kExecutionModeNcgO1) {

        //Go over the basic blocks of the compilation unit
        dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
        for (bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator));
                bb != NULL;
                bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator))) {

            int retCode = preprocessingBB (cUnit, bb);

            if (retCode < 0) {
                SET_JIT_ERROR(kJitErrorCodegen);
                endOfTrace (cUnit);
                return;
            }
        }
    }

    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);

    /* Handle the content in each basic block */
    for (bb = (BasicBlock *) (dvmGrowableListIteratorNext (&iterator)),
         i = 0;
         //We stop when bb is 0
         bb != 0;
         //Induction variables: bb goes to next iterator, i is incremented
         bb = (BasicBlock *) (dvmGrowableListIteratorNext (&iterator)),
         i++) {

        //Get O1 version
        BasicBlock_O1 *bbO1 = reinterpret_cast<BasicBlock_O1 *> (bb);

        //Paranoid
        if (bbO1 == 0) {
            continue;
        }

        //Switch depending on the BasicBlock type
        switch (bbO1->blockType)
        {
            case kEntryBlock:
                //The entry block should always be processed first because it is entry to trace
                assert (i == 0);

                //Intentional fallthrough because we handle it same way as an exit block
            case kExitBlock:
                //Only handle the fallthrough if there is an instruction
                if (bbO1->firstMIRInsn != 0)
                {
                    //First handle fallthrough branch
                    handleFallThroughBranch (cUnit, bbO1, &nextFallThrough);
                }

                //Set label offset
                bbO1->label->lop.generic.offset = (stream - streamMethodStart);

                if (generateCode (cUnit, bbO1, &nextFallThrough) == false)
                {
                    //Generate code set an error for the jit, we can just return
                    return;
                }
                break;
            case kDalvikByteCode:
            case kPreBackwardBlock:
            case kFromInterpreter:
                //If hidden, we don't generate code
                if (bbO1->hidden == false)
                {
                    //First handle fallthrough branch
                    handleFallThroughBranch (cUnit, bbO1, &nextFallThrough);

                    //Set label offset
                    bbO1->label->lop.generic.offset = (stream - streamMethodStart);

                    if (generateCode (cUnit, bbO1, &nextFallThrough) == false)
                    {
                        //Generate code set an error for the jit, we can just return
                        return;
                    }
                }
                break;
            case kChainingCellNormal:
                /* Handle the codegen later */
                dvmInsertGrowableList(&chainingListByType[kChainingCellNormal], i);
                break;
            case kChainingCellInvokeSingleton:
                /* Handle the codegen later */
                dvmInsertGrowableList (&chainingListByType[kChainingCellInvokeSingleton], i);
                break;
            case kChainingCellInvokePredicted:
                /* Handle the codegen later */
                dvmInsertGrowableList(&chainingListByType[kChainingCellInvokePredicted], i);
                break;
            case kChainingCellHot:
                /* Handle the codegen later */
                dvmInsertGrowableList(&chainingListByType[kChainingCellHot], i);
                break;
            case kExceptionHandling:
                //First handle fallthrough branch
                handleFallThroughBranch (cUnit, bbO1, &nextFallThrough);

                //Update the offset of the block
                bbO1->label->lop.generic.offset = (stream - streamMethodStart);

                //Now generate any code for this BB
                if (generateCode (cUnit, bbO1, &nextFallThrough) == false)
                {
                    //Generate code set an error for the jit, we can just return
                    return;
                }

                //Finally generate a jump to dvmJitToInterpPunt using eax as scratch register
                scratchRegs[0] = PhysicalReg_EAX;
                jumpToInterpPunt();

                break;
            case kChainingCellBackwardBranch:
                /* Handle the codegen later */
                dvmInsertGrowableList(&chainingListByType[kChainingCellBackwardBranch], i);
                break;
            default:
                break;
            }
        }

    if (cUnit->printMe) {
        // record all assmebly code before chaining cells as a block
        std::pair<BBType, char*> code_blk_elem(kDalvikByteCode, streamMethodStart);
        cUnit->code_block_table->push_back(code_blk_elem);
        print_stream_ptr = stream;
    }

    char* streamChainingStart = 0;
    /* Handle the chaining cells in predefined order */

    for (i = 0; i < kChainingCellGap; i++) {
        size_t j;
        cUnit->numChainingCells[i] = chainingListByType[i].numUsed;

        /* No chaining cells of this type */
        if (cUnit->numChainingCells[i] == 0)
            continue;

        //First handle fallthrough branch
        handleFallThroughBranch (cUnit, 0, &nextFallThrough);

        //If we haven't initialized the start of the chaining cells we do it now
        if (streamChainingStart == 0)
        {
            //Stream has been updated because handleFallThroughBranch always generates jumps which
            //forces scheduler to update the stream pointer. Thus we can use it here.
            assert (singletonPtr<Scheduler>()->isQueueEmpty() == true);

            //Initialize the beginning of the chaining cells
            streamChainingStart = stream;
        }

        if (cUnit->printMe && print_stream_ptr < stream) {
            // If there is any code before the chaining cell block and the
            // last recorded block, make it a separate code block.
            std::pair<BBType, char*> code_blk_elem(kDalvikByteCode, print_stream_ptr);
            cUnit->code_block_table->push_back(code_blk_elem);
            print_stream_ptr = stream;
        }

        /* Record the first LIR for a new type of chaining cell */
        for (j = 0; j < chainingListByType[i].numUsed; j++) {
            int blockId = (int) dvmGrowableListGetElement (& (chainingListByType[i]), j);

            BasicBlock *chainingBlock =
                (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList,
                                                         blockId);

            //Get O1 version
            BasicBlock_O1 *bbO1 = reinterpret_cast<BasicBlock_O1 *> (chainingBlock);

            //Paranoid
            if (bbO1 == 0) {
                continue;
            }

            //Set offset
            bbO1->label->lop.generic.offset = (stream - streamMethodStart);

            //Eagerly assume we successfully generated chaining cell
            bool success = true;

            int nop_size;
            switch (chainingBlock->blockType) {
                case kChainingCellNormal:
                    nop_size = handleNormalChainingCell(cUnit,
                     chainingBlock->startOffset, bbO1);
                    bbO1->label->lop.generic.offset += nop_size; //skip over nop
                    break;
                case kChainingCellInvokeSingleton:
                    nop_size = handleInvokeSingletonChainingCell(cUnit,
                        chainingBlock->containingMethod, blockId);
                    bbO1->label->lop.generic.offset += nop_size; //skip over nop
                    break;
                case kChainingCellInvokePredicted:
                    success = handleInvokePredictedChainingCell (cUnit, bbO1);
                    break;
                case kChainingCellHot:
                    nop_size = handleHotChainingCell(cUnit,
                        chainingBlock->startOffset, blockId);
                    bbO1->label->lop.generic.offset += nop_size; //skip over nop
                    break;
                case kChainingCellBackwardBranch:
                    success = handleBackwardBranchChainingCell (cUnit, bbO1);
                    break;
                default:
                    ALOGI("JIT_INFO: Bad blocktype %d", chainingBlock->blockType);
                    SET_JIT_ERROR(kJitErrorTraceFormation);
                    endOfTrace (cUnit);
                    cUnit->code_block_table->clear();
                    return;
            }

            if (success == false)
            {
                SET_JIT_ERROR(kJitErrorChainingCell);
                endOfTrace (cUnit);
                return;
            }

            if (cUnit->printMe) {
                // record the chaining cell block
                std::pair<BBType, char*> code_blk_elem(chainingBlock->blockType, print_stream_ptr);
                cUnit->code_block_table->push_back(code_blk_elem);
                print_stream_ptr = stream;
            }

            if (dvmCompilerWillCodeCacheOverflow((stream - streamStart) + CODE_CACHE_PADDING) == true) {
                ALOGI("JIT_INFO: Code cache full after ChainingCell (trace uses %uB)", (stream - streamStart));
                SET_JIT_ERROR(kJitErrorCodeCacheFull);
                dvmCompilerSetCodeAndDataCacheFull();
                endOfTrace (cUnit);
                cUnit->code_block_table->clear();
                return;
            }
        }
    }

    // Now that we finished handling all of the MIR BBs, we can dump all exception handling
    // restore state to the code stream
    singletonPtr<ExceptionHandlingRestoreState>()->dumpAllExceptionHandlingRestoreState();

    //In case, handle fallthrough branch
    handleFallThroughBranch (cUnit, 0, &nextFallThrough);

    //Since we are at end of trace, we need to finish all work in the worklists
    performWorklistWork ();

    //We finished generating code for trace so we can signal end of trace now
    endOfTrace (cUnit);

    if (cUnit->printMe) {
        // record exception VR restores as block type kExceptionHandling
        std::pair<BBType, char*> code_blk_elem(kExceptionHandling, print_stream_ptr);
        cUnit->code_block_table->push_back(code_blk_elem);
        print_stream_ptr = stream;
    }

    if (gDvmJit.codeCacheFull) {
        // We hit code cache size limit either after dumping exception handling
        // state or after calling endOfTrace. Bail out for this trace!
        ALOGI("JIT_INFO: Code cache full after endOfTrace (trace uses %uB)", (stream - streamStart));
        SET_JIT_ERROR_MANUAL (cUnit, kJitErrorCodeCacheFull);
        cUnit->code_block_table->clear();
        return;
    }

    /* dump section for chaining cell counts, make sure it is 4-byte aligned */
    padding = (4 - ((u4)stream & 3)) & 3;
    stream += padding;
    ChainCellCounts chainCellCounts;
    /* Install the chaining cell counts */
    for (i=0; i< kChainingCellGap; i++) {
        chainCellCounts.u.count[i] = cUnit->numChainingCells[i];
    }
    char* streamCountStart = (char*)stream;
    memcpy((char*)stream, &chainCellCounts, sizeof(chainCellCounts));
    stream += sizeof(chainCellCounts);

    cUnit->totalSize = (stream - streamStart);
    if (dvmCompilerWillCodeCacheOverflow(cUnit->totalSize + CODE_CACHE_PADDING) == true) {
        ALOGI("JIT_INFO: Code cache full after ChainingCellCounts (trace uses %uB)", (stream - streamStart));
        SET_JIT_ERROR_MANUAL (cUnit, kJitErrorCodeCacheFull);
        dvmCompilerSetCodeAndDataCacheFull();
        cUnit->code_block_table->clear();
        return;
    }

    /* write chaining cell count offset & chaining cell offset */
    u2* pOffset = (u2*)(streamMethodStart - EXTRA_BYTES_FOR_CHAINING); /* space was already allocated for this purpose */
    *pOffset = streamCountStart - streamMethodStart; /* from codeAddr */
    pOffset[1] = streamChainingStart - streamMethodStart;

#if defined(WITH_JIT_TPROFILE)
    /* Install the trace description, so that we can retrieve the trace info from trace code addr later */
    int descSize = (cUnit->jitMode == kJitMethod) ?
        0 : getTraceDescriptionSize(cUnit->traceDesc);
    memcpy((char*) stream, cUnit->traceDesc, descSize);
    stream += descSize;
    cUnit->totalSize = (stream - streamStart);

    /* Check if the trace installation will cause the code cache full */
    if (dvmCompilerWillCodeCacheOverflow(cUnit->totalSize + CODE_CACHE_PADDING) == true) {
        ALOGI("JIT_INFO: Code cache full after Trace Description (trace uses %uB)", (stream - streamStart));
        SET_JIT_ERROR_MANUAL (cUnit, kJitErrorCodeCacheFull);
        dvmCompilerSetCodeAndDataCacheFull();
        cUnit->baseAddr = NULL;
        return;
    }
#endif

    // Update totalSize and codeCacheByteUsed used so far
    cUnit->totalSize = (stream - streamStart);  // store size of trace in cUnit->totalSize
    gDvmJit.codeCacheByteUsed += (stream - streamStart); // store code cache byte used to include the current trace

    int patchCount = 0;       // Store number of constants initialized in a trace
    // Try to write data to data or code cache
    if (writeDataToDataOrCodeCache(cUnit, patchCount) == false) {
        cUnit->code_block_table->clear();
        // Return because of failures
        return;
    }

    // Now print out the trace in code cache based on code_block_table
    if (cUnit->printMe) {
        // Push an kExitBlock block as an end marker of the trace.
        // The chaining cell count and the long/double constants are
        //  emit after the end marker.
        std::pair<BBType, char*> code_blk_elem(kExitBlock, print_stream_ptr);
        cUnit->code_block_table->push_back(code_blk_elem);
        dvmCompilerLcgPrintTrace(cUnit, chainCellCounts, patchCount, pOffset);
    }
    if (cUnit->getSwitchInfo() != 0) {
        cUnit->getSwitchInfo()->switchNormalCCList.clear();
    }
    cUnit->code_block_table->clear();
    ALOGV("JIT CODE after trace %p to %p size %x START %p", streamMethodStart,
          (char *) gDvmJit.codeCache + gDvmJit.codeCacheByteUsed,
          cUnit->totalSize, gDvmJit.codeCache);

    gDvmJit.numCompilations++;

    //Update the base addr
    cUnit->baseAddr = streamMethodStart;

    info->codeAddress = (char*)cUnit->baseAddr;// + cUnit->headerSize;
#if defined(WITH_JIT_TPROFILE)
    info->profileCodeSize = cUnit->profileCodeSize;
#endif
}

/**
 * @brief Check if the address is inside the range of the JIT code cache
 * @param addr the address
 * @return true if inside the code cache
 */
static bool isAddrInCodeCache(char *addr)
{
    /* Check if the address is inside the code cache */
    if (addr >= (char *)gDvmJit.codeCache
        && addr < (char *)gDvmJit.codeCache + gDvmJit.codeCacheSize) {
        return true;
    }
    /* Address is not inside the code cache */
    return false;
}

void dvmCompilerLCGMIR2LIR (CompilationUnit *cUnitME, JitTranslationInfo *info)
{
    // TODO: compile into a temporary buffer and then copy into the code cache.
    // That would let us leave the code cache unprotected for a shorter time.

    // params should be obtained under the lock i.e. should not be stored in the locals
    UNPROTECT_CODE_CACHE(((char*)gDvmJit.codeCache + gDvmJit.codeCacheByteUsed),
                           gDvmJit.codeCacheSize - gDvmJit.codeCacheByteUsed);
    compileLCGMIR2LIR (cUnitME, info);
    PROTECT_CODE_CACHE(((char*)gDvmJit.codeCache + gDvmJit.codeCacheByteUsed),
                           gDvmJit.codeCacheSize - gDvmJit.codeCacheByteUsed);
}

/*
 * Perform translation chain operation.
 */
void* dvmJitChain(void* tgtAddr, u4* branchAddr)
{
#ifdef JIT_CHAIN
    int relOffset;

    if ((gDvmJit.pProfTable != NULL) && (gDvm.sumThreadSuspendCount == 0) &&
        (gDvmJit.codeCacheFull == false)) {

        bool isInCodeCache = isAddrInCodeCache((char *)branchAddr);
        if (isInCodeCache == true) {
            UNPROTECT_CODE_CACHE(branchAddr, sizeof(int));
        }
        else {
            UNPROTECT_DATA_CACHE(branchAddr, sizeof(int));
        }
        gDvmJit.translationChains++;
        UPDATE_CODE_CACHE_PATCHES();

        relOffset = (int) tgtAddr - (int)branchAddr - 4; // 32bit offset
        updateCodeCache(*(int*)branchAddr, relOffset);

        gDvmJit.hasNewChain = true;

        if (isInCodeCache == true) {
            PROTECT_CODE_CACHE(branchAddr, sizeof(int));
        }
        else {
            PROTECT_DATA_CACHE(branchAddr, sizeof(int));
        }
        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: chaining 0x%x to %p with relOffset %x",
                  (int) branchAddr, tgtAddr, relOffset));
    }
#endif
    return tgtAddr;
}

/*
 * Perform chaining operation. Patched branchAddr using static address tgtAddr
 */
void* dvmJitChain_staticAddr(void* tgtAddr, u4* branchAddr)
{
#ifdef JIT_CHAIN
    if ((gDvmJit.pProfTable != NULL) && (gDvm.sumThreadSuspendCount == 0) &&
        (gDvmJit.codeCacheFull == false)) {

        bool isInCodeCache = isAddrInCodeCache((char *)branchAddr);
        if (isInCodeCache == true) {
            UNPROTECT_CODE_CACHE(branchAddr, sizeof(int));
        }
        else {
            UNPROTECT_DATA_CACHE(branchAddr, sizeof(int));
        }
        gDvmJit.translationChains++;
        UPDATE_CODE_CACHE_PATCHES();

        updateCodeCache(*(int*)branchAddr, (int)tgtAddr);

        gDvmJit.hasNewChain = true;

        if (isInCodeCache == true) {
            PROTECT_CODE_CACHE(branchAddr, sizeof(int));
        }
        else {
            PROTECT_DATA_CACHE(branchAddr, sizeof(int));
        }
        COMPILER_TRACE_CHAINING(
            ALOGI("Jit Runtime: chaining 0x%x to %p\n",
                 (int) branchAddr, tgtAddr));
    }
#endif
    return tgtAddr;
}

/**
 * @brief Send off the work
 * @param work the CompilerWorkOrder
 * @return if the compilation succeeded
 */
static bool sendOffWork (CompilerWorkOrder *work)
{
    //Get trace description
    JitTraceDescription *desc = static_cast<JitTraceDescription *> (work->info);
    bool success = true;

    //Will we compile it?
    bool (*middleEndGate) (JitTraceDescription *, int, JitTranslationInfo *, jmp_buf *, int ) = gDvmJit.jitFramework.middleEndGate;

    //Compilation function
    bool (*middleEndFunction) (JitTraceDescription *, int, JitTranslationInfo *, jmp_buf *, int ) = gDvmJit.jitFramework.middleEndFunction;

    //If we have a middle-end function, we have work
    if (middleEndFunction != 0)
    {
        //Suppose we will compile it
        bool willCompile = true;

        //If we have a gate
        if (middleEndGate != 0)
        {
            willCompile = middleEndGate (desc, JIT_MAX_TRACE_LEN, &work->result, work->bailPtr, 0);
        }

        if (willCompile == true)
        {
            //Get middle end function

            success = middleEndFunction (desc, JIT_MAX_TRACE_LEN, &work->result, work->bailPtr, 0 /* no hints */);
        }
    }

    return success;
}

/*
 * Accept the work and start compiling.  Returns true if compilation
 * is attempted.
 */
bool dvmCompilerDoWork(CompilerWorkOrder *work)
{
    bool isCompile = true;
    bool success = true;

    if (gDvmJit.codeCacheFull == true) {
        return false;
    }

    switch (work->kind) {
        case kWorkOrderTrace:
            sendOffWork (work);
            break;
        case kWorkOrderTraceDebug:
            {
                bool oldPrintMe = gDvmJit.printMe;
                gDvmJit.printMe = true;
                sendOffWork (work);
                gDvmJit.printMe = oldPrintMe;
                break;
            }
        case kWorkOrderProfileMode:
            dvmJitChangeProfileMode ( (TraceProfilingModes) (int) work->info);
            isCompile = false;
            break;
        default:
            isCompile = false;
            ALOGI ("JIT_INFO: Unknown work order type");
            assert (0);  // Bail if debug build, discard otherwise
            ALOGI ("\tError ignored");
            break;
    }

    if (success == false) {
        work->result.codeAddress = NULL;
    }

    return isCompile;
}

void dvmCompilerCacheFlush(long start, long end, long flags) {
  /* cacheflush is needed for ARM, but not for IA32 (coherent icache) */
}

bool dvmCompilerFindRegClass (MIR *mir, int vR, RegisterClass &regClass, bool onlyUse)
{
    //Get information about the VRs in current bytecode
    VirtualRegInfo infoByteCode[MAX_REG_PER_BYTECODE];
    int numVRs = getVirtualRegInfo (infoByteCode, mir);

    //If we get a negative return value, there was an error.
    if (numVRs < 0)
    {
        return false;
    }

    int entry;
    for (entry = 0; entry < numVRs; entry++) {
        if (infoByteCode[entry].regNum == vR) {
            // We found out vR if we are interested in use or def or if access is not def
            if ((onlyUse == false) || (infoByteCode[entry].accessType != REGACCESS_D))
            {
                break;
            }
        }
    }

    // If we cannot find this VR, we failed
    if (entry == numVRs)
    {
        return false;
    }

    switch (infoByteCode[entry].physicalType)
    {
        case LowOpndRegType_gp:
            regClass = kCoreReg;
            break;
        case LowOpndRegType_fs_s:
        case LowOpndRegType_fs:
            regClass = kX87Reg;
            break;
        case LowOpndRegType_ss:
            regClass = kSFPReg;
            break;
        case LowOpndRegType_xmm:
            regClass = kDFPReg;
            break;
        default:
            ALOGD ("JIT_INFO: dvmCompilerFindClass: Type not found %d\n",
                    infoByteCode[entry].physicalType);
            return false;
    }

    //Success, signal it
    return true;
}

BasicBlock *dvmCompilerLCGNewBB (void)
{
    // Make space on arena for this BB
    void * space = dvmCompilerNew(sizeof(BasicBlock_O1), true);

    // Ensure that constructor is called
    BasicBlock_O1 * newBB = new (space) BasicBlock_O1;

    // Paranoid because dvmCompilerNew should never return NULL
    assert(newBB != 0);

    return newBB;
}

void dvmCompilerLCGDumpBB (CompilationUnit *cUnit, BasicBlock *bb, FILE *file, bool beforeMIRs)
{
    // We have already created the x86 specific BB so cast is okay
    BasicBlock_O1 * curBB = reinterpret_cast<BasicBlock_O1 *>(bb);

    if (beforeMIRs == true)
    {
        curBB->associationTable.printToDot(file);
    }
}

unsigned int dvmCompilerLcgGetMaxScratch (void)
{
    //We can only use as many temporaries as are actually allocated on stack
    return StackTemporaries::getTotalScratchVRs ();
}

/**
 * @brief A function to check the size of the DvmJitGlobals data structure
 * @details This function checks the size of the DvmJitGlobals data structure, to ensure consistent usage across shared objects compiled apart from libdvm.so.
 * @param dvmJitGlobalsSize The size of DvmJitGlobals, from the view of a dynamically loaded .so
 * @return true if the size passed as an argument matches the size of the DvmJitGlobals object, from the perspective of libdvm.so.
 */
bool dvmCompilerDataStructureSizeCheck(int dvmJitGlobalsSize)
{
    return (dvmJitGlobalsSize == sizeof(DvmJitGlobals));
}

CompilationErrorHandler *dvmCompilerLCGNewCompilationErrorHandler (void)
{
    CompilationErrorHandlerLCG *res;

    //Make space for it
    void *space = dvmCompilerNew (sizeof (*res), true);

    //Ensure the constructor is called
    res = new (space) CompilationErrorHandlerLCG ();

    //Return it
    return res;
}
