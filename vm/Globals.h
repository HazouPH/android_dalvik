/*
 * Copyright (C) 2008 The Android Open Source Project
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

/*
 * Variables with library scope.
 *
 * Prefer this over scattered static and global variables -- it's easier to
 * view the state in a debugger, it makes clean shutdown simpler, we can
 * trivially dump the state into a crash log, and it dodges most naming
 * collisions that will arise when we are embedded in a larger program.
 *
 * If we want multiple VMs per process, this can get stuffed into TLS (or
 * accessed through a Thread field).  May need to pass it around for some
 * of the early initialization functions.
 */
#ifndef DALVIK_GLOBALS_H_
#define DALVIK_GLOBALS_H_

#include <map>
#include <string>
#include <vector>

#include <stdarg.h>
#include <pthread.h>
#include <signal.h>

/* private structures */
struct GcHeap;
struct BreakpointSet;
struct InlineSub;

/*
 * One of these for each -ea/-da/-esa/-dsa on the command line.
 */
struct AssertionControl {
    char*   pkgOrClass;         /* package/class string, or NULL for esa/dsa */
    int     pkgOrClassLen;      /* string length, for quick compare */
    bool    enable;             /* enable or disable */
    bool    isPackage;          /* string ended with "..."? */
};

/*
 * Register map generation mode.  Only applicable when generateRegisterMaps
 * is enabled.  (The "disabled" state is not folded into this because
 * there are callers like dexopt that want to enable/disable without
 * specifying the configuration details.)
 *
 * "TypePrecise" is slower and requires additional storage for the register
 * maps, but allows type-precise GC.  "LivePrecise" is even slower and
 * requires additional heap during processing, but allows live-precise GC.
 */
enum RegisterMapMode {
    kRegisterMapModeUnknown = 0,
    kRegisterMapModeTypePrecise,
    kRegisterMapModeLivePrecise
};

/*
 * Profiler clock source.
 */
enum ProfilerClockSource {
    kProfilerClockSourceThreadCpu,
    kProfilerClockSourceWall,
    kProfilerClockSourceDual,
};


//Forward declarations
class CompilationErrorHandler;
struct CompilationUnit;
struct JitTranslationInfo;
struct JitTranslationDescription;
class Pass;


/*
 * All fields are initialized to zero.
 *
 * Storage allocated here must be freed by a subsystem shutdown function.
 */
struct DvmGlobals {
    /*
     * Some options from the command line or environment.
     */
    char*       bootClassPathStr;
    char*       classPathStr;

    size_t      heapStartingSize;
    size_t      heapMaximumSize;
    size_t      heapGrowthLimit;
    bool        lowMemoryMode;
    double      heapTargetUtilization __attribute__ ((aligned (8)));
    size_t      heapMinFree;
    size_t      heapMaxFree;
    size_t      stackSize;
    size_t      mainThreadStackSize;

    bool        verboseGc;
    bool        verboseJni;
    bool        verboseClass;
    bool        verboseShutdown;

    bool        jdwpAllowed;        // debugging allowed for this process?
    bool        jdwpConfigured;     // has debugging info been provided?
    JdwpTransportType jdwpTransport;
    bool        jdwpServer;
    char*       jdwpHost;
    int         jdwpPort;
    bool        jdwpSuspend;

    ProfilerClockSource profilerClockSource;

    /*
     * Lock profiling threshold value in milliseconds.  Acquires that
     * exceed threshold are logged.  Acquires within the threshold are
     * logged with a probability of $\frac{time}{threshold}$ .  If the
     * threshold is unset no additional logging occurs.
     */
    u4          lockProfThreshold;

    int         (*vfprintfHook)(FILE*, const char*, va_list);
    void        (*exitHook)(int);
    void        (*abortHook)(void);
    bool        (*isSensitiveThreadHook)(void);

    char*       jniTrace;
    bool        reduceSignals;
    bool        noQuitHandler;
    bool        verifyDexChecksum;
    char*       stackTraceFile;     // for SIGQUIT-inspired output

    bool        logStdio;

    DexOptimizerMode    dexOptMode;
    DexClassVerifyMode  classVerifyMode;

    bool        generateRegisterMaps;
    RegisterMapMode     registerMapMode;

    bool        monitorVerification;

    bool        dexOptForSmp;

    /*
     * GC option flags.
     */
    bool        preciseGc;
    bool        preVerify;
    bool        postVerify;
    bool        concurrentMarkSweep;
    bool        verifyCardTable;
    bool        disableExplicitGc;

    int         assertionCtrlCount;
    AssertionControl*   assertionCtrl;

    ExecutionMode   executionMode;

    bool        commonInit; /* whether common stubs are generated */
    bool        constInit; /* whether global constants are initialized */

    /*
     * VM init management.
     */
    bool        initializing;
    bool        optimizing;

    /*
     * java.lang.System properties set from the command line with -D.
     * This is effectively a set, where later entries override earlier
     * ones.
     */
    std::vector<std::string>* properties;

    /*
     * Where the VM goes to find system classes.
     */
    ClassPathEntry* bootClassPath;
    /* used by the DEX optimizer to load classes from an unfinished DEX */
    DvmDex*     bootClassPathOptExtra;
    bool        optimizingBootstrapClass;

    /*
     * Loaded classes, hashed by class name.  Each entry is a ClassObject*,
     * allocated in GC space.
     */
    HashTable*  loadedClasses;

    /*
     * Value for the next class serial number to be assigned.  This is
     * incremented as we load classes.  Failed loads and races may result
     * in some numbers being skipped, and the serial number is not
     * guaranteed to start at 1, so the current value should not be used
     * as a count of loaded classes.
     */
    volatile int classSerialNumber;

    /*
     * Classes with a low classSerialNumber are probably in the zygote, and
     * their InitiatingLoaderList is not used, to promote sharing. The list is
     * kept here instead.
     */
    InitiatingLoaderList* initiatingLoaderList;

    /*
     * Interned strings.
     */

    /* A mutex that guards access to the interned string tables. */
    pthread_mutex_t internLock;

    /* Hash table of strings interned by the user. */
    HashTable*  internedStrings;

    /* Hash table of strings interned by the class loader. */
    HashTable*  literalStrings;

    /*
     * Classes constructed directly by the vm.
     */

    /* the class Class */
    ClassObject* classJavaLangClass;

    /* synthetic classes representing primitive types */
    ClassObject* typeVoid;
    ClassObject* typeBoolean;
    ClassObject* typeByte;
    ClassObject* typeShort;
    ClassObject* typeChar;
    ClassObject* typeInt;
    ClassObject* typeLong;
    ClassObject* typeFloat;
    ClassObject* typeDouble;

    /* synthetic classes for arrays of primitives */
    ClassObject* classArrayBoolean;
    ClassObject* classArrayByte;
    ClassObject* classArrayShort;
    ClassObject* classArrayChar;
    ClassObject* classArrayInt;
    ClassObject* classArrayLong;
    ClassObject* classArrayFloat;
    ClassObject* classArrayDouble;

    /*
     * Quick lookups for popular classes used internally.
     */
    ClassObject* classJavaLangClassArray;
    ClassObject* classJavaLangClassLoader;
    ClassObject* classJavaLangObject;
    ClassObject* classJavaLangObjectArray;
    ClassObject* classJavaLangString;
    ClassObject* classJavaLangThread;
    ClassObject* classJavaLangVMThread;
    ClassObject* classJavaLangThreadGroup;
    ClassObject* classJavaLangStackTraceElement;
    ClassObject* classJavaLangStackTraceElementArray;
    ClassObject* classJavaLangAnnotationAnnotationArray;
    ClassObject* classJavaLangAnnotationAnnotationArrayArray;
    ClassObject* classJavaLangReflectAccessibleObject;
    ClassObject* classJavaLangReflectConstructor;
    ClassObject* classJavaLangReflectConstructorArray;
    ClassObject* classJavaLangReflectField;
    ClassObject* classJavaLangReflectFieldArray;
    ClassObject* classJavaLangReflectMethod;
    ClassObject* classJavaLangReflectMethodArray;
    ClassObject* classJavaLangReflectProxy;
    ClassObject* classJavaLangSystem;
    ClassObject* classJavaNioDirectByteBuffer;
    ClassObject* classLibcoreReflectAnnotationFactory;
    ClassObject* classLibcoreReflectAnnotationMember;
    ClassObject* classLibcoreReflectAnnotationMemberArray;
    ClassObject* classOrgApacheHarmonyDalvikDdmcChunk;
    ClassObject* classOrgApacheHarmonyDalvikDdmcDdmServer;
    ClassObject* classJavaLangRefFinalizerReference;

    /*
     * classes representing exception types. The names here don't include
     * packages, just to keep the use sites a bit less verbose. All are
     * in java.lang, except where noted.
     */
    ClassObject* exAbstractMethodError;
    ClassObject* exArithmeticException;
    ClassObject* exArrayIndexOutOfBoundsException;
    ClassObject* exArrayStoreException;
    ClassObject* exClassCastException;
    ClassObject* exClassCircularityError;
    ClassObject* exClassFormatError;
    ClassObject* exClassNotFoundException;
    ClassObject* exError;
    ClassObject* exExceptionInInitializerError;
    ClassObject* exFileNotFoundException; /* in java.io */
    ClassObject* exIOException;           /* in java.io */
    ClassObject* exIllegalAccessError;
    ClassObject* exIllegalAccessException;
    ClassObject* exIllegalArgumentException;
    ClassObject* exIllegalMonitorStateException;
    ClassObject* exIllegalStateException;
    ClassObject* exIllegalThreadStateException;
    ClassObject* exIncompatibleClassChangeError;
    ClassObject* exInstantiationError;
    ClassObject* exInstantiationException;
    ClassObject* exInternalError;
    ClassObject* exInterruptedException;
    ClassObject* exLinkageError;
    ClassObject* exNegativeArraySizeException;
    ClassObject* exNoClassDefFoundError;
    ClassObject* exNoSuchFieldError;
    ClassObject* exNoSuchFieldException;
    ClassObject* exNoSuchMethodError;
    ClassObject* exNullPointerException;
    ClassObject* exOutOfMemoryError;
    ClassObject* exRuntimeException;
    ClassObject* exStackOverflowError;
    ClassObject* exStaleDexCacheError;    /* in dalvik.system */
    ClassObject* exStringIndexOutOfBoundsException;
    ClassObject* exThrowable;
    ClassObject* exTypeNotPresentException;
    ClassObject* exUnsatisfiedLinkError;
    ClassObject* exUnsupportedOperationException;
    ClassObject* exVerifyError;
    ClassObject* exVirtualMachineError;

    /* method offsets - Object */
    int         voffJavaLangObject_equals;
    int         voffJavaLangObject_hashCode;
    int         voffJavaLangObject_toString;

    /* field offsets - String */
    int         offJavaLangString_value;
    int         offJavaLangString_count;
    int         offJavaLangString_offset;
    int         offJavaLangString_hashCode;

    /* field offsets - Thread */
    int         offJavaLangThread_vmThread;
    int         offJavaLangThread_group;
    int         offJavaLangThread_daemon;
    int         offJavaLangThread_name;
    int         offJavaLangThread_priority;
    int         offJavaLangThread_uncaughtHandler;
    int         offJavaLangThread_contextClassLoader;

    /* method offsets - Thread */
    int         voffJavaLangThread_run;

    /* field offsets - ThreadGroup */
    int         offJavaLangThreadGroup_name;
    int         offJavaLangThreadGroup_parent;

    /* field offsets - VMThread */
    int         offJavaLangVMThread_thread;
    int         offJavaLangVMThread_vmData;

    /* method offsets - ThreadGroup */
    int         voffJavaLangThreadGroup_removeThread;

    /* field offsets - Throwable */
    int         offJavaLangThrowable_stackState;
    int         offJavaLangThrowable_cause;

    /* method offsets - ClassLoader */
    int         voffJavaLangClassLoader_loadClass;

    /* direct method pointers - ClassLoader */
    Method*     methJavaLangClassLoader_getSystemClassLoader;

    /* field offsets - java.lang.reflect.* */
    int         offJavaLangReflectConstructor_slot;
    int         offJavaLangReflectConstructor_declClass;
    int         offJavaLangReflectField_slot;
    int         offJavaLangReflectField_declClass;
    int         offJavaLangReflectMethod_slot;
    int         offJavaLangReflectMethod_declClass;

    /* field offsets - java.lang.ref.Reference */
    int         offJavaLangRefReference_referent;
    int         offJavaLangRefReference_queue;
    int         offJavaLangRefReference_queueNext;
    int         offJavaLangRefReference_pendingNext;

    /* field offsets - java.lang.ref.FinalizerReference */
    int offJavaLangRefFinalizerReference_zombie;

    /* method pointers - java.lang.ref.ReferenceQueue */
    Method* methJavaLangRefReferenceQueueAdd;

    /* method pointers - java.lang.ref.FinalizerReference */
    Method* methJavaLangRefFinalizerReferenceAdd;

    /* constructor method pointers; no vtable involved, so use Method* */
    Method*     methJavaLangStackTraceElement_init;
    Method*     methJavaLangReflectConstructor_init;
    Method*     methJavaLangReflectField_init;
    Method*     methJavaLangReflectMethod_init;
    Method*     methOrgApacheHarmonyLangAnnotationAnnotationMember_init;

    /* static method pointers - android.lang.annotation.* */
    Method*
        methOrgApacheHarmonyLangAnnotationAnnotationFactory_createAnnotation;

    /* direct method pointers - java.lang.reflect.Proxy */
    Method*     methJavaLangReflectProxy_constructorPrototype;

    /* field offsets - java.lang.reflect.Proxy */
    int         offJavaLangReflectProxy_h;

    /* direct method pointer - java.lang.System.runFinalization */
    Method*     methJavaLangSystem_runFinalization;

    /* field offsets - java.io.FileDescriptor */
    int         offJavaIoFileDescriptor_descriptor;

    /* direct method pointers - dalvik.system.NativeStart */
    Method*     methDalvikSystemNativeStart_main;
    Method*     methDalvikSystemNativeStart_run;

    /* assorted direct buffer helpers */
    Method*     methJavaNioDirectByteBuffer_init;
    int         offJavaNioBuffer_capacity;
    int         offJavaNioBuffer_effectiveDirectAddress;

    /* direct method pointers - org.apache.harmony.dalvik.ddmc.DdmServer */
    Method*     methDalvikDdmcServer_dispatch;
    Method*     methDalvikDdmcServer_broadcast;

    /* field offsets - org.apache.harmony.dalvik.ddmc.Chunk */
    int         offDalvikDdmcChunk_type;
    int         offDalvikDdmcChunk_data;
    int         offDalvikDdmcChunk_offset;
    int         offDalvikDdmcChunk_length;

    /*
     * Thread list.  This always has at least one element in it (main),
     * and main is always the first entry.
     *
     * The threadListLock is used for several things, including the thread
     * start condition variable.  Generally speaking, you must hold the
     * threadListLock when:
     *  - adding/removing items from the list
     *  - waiting on or signaling threadStartCond
     *  - examining the Thread struct for another thread (this is to avoid
     *    one thread freeing the Thread struct while another thread is
     *    perusing it)
     */
    Thread*     threadList;
    pthread_mutex_t threadListLock;

    pthread_cond_t threadStartCond;

    /*
     * The thread code grabs this before suspending all threads.  There
     * are a few things that can cause a "suspend all":
     *  (1) the GC is starting;
     *  (2) the debugger has sent a "suspend all" request;
     *  (3) a thread has hit a breakpoint or exception that the debugger
     *      has marked as a "suspend all" event;
     *  (4) the SignalCatcher caught a signal that requires suspension.
     *  (5) (if implemented) the JIT needs to perform a heavyweight
     *      rearrangement of the translation cache or JitTable.
     *
     * Because we use "safe point" self-suspension, it is never safe to
     * do a blocking "lock" call on this mutex -- if it has been acquired,
     * somebody is probably trying to put you to sleep.  The leading '_' is
     * intended as a reminder that this lock is special.
     */
    pthread_mutex_t _threadSuspendLock;

    /*
     * Guards Thread->suspendCount for all threads, and
     * provides the lock for the condition variable that all suspended threads
     * sleep on (threadSuspendCountCond).
     *
     * This has to be separate from threadListLock because of the way
     * threads put themselves to sleep.
     */
    pthread_mutex_t threadSuspendCountLock;

    /*
     * Suspended threads sleep on this.  They should sleep on the condition
     * variable until their "suspend count" is zero.
     *
     * Paired with "threadSuspendCountLock".
     */
    pthread_cond_t  threadSuspendCountCond;

    /*
     * Sum of all threads' suspendCount fields. Guarded by
     * threadSuspendCountLock.
     */
    int  sumThreadSuspendCount;

    /*
     * MUTEX ORDERING: when locking multiple mutexes, always grab them in
     * this order to avoid deadlock:
     *
     *  (1) _threadSuspendLock      (use lockThreadSuspend())
     *  (2) threadListLock          (use dvmLockThreadList())
     *  (3) threadSuspendCountLock  (use lockThreadSuspendCount())
     */


    /*
     * Thread ID bitmap.  We want threads to have small integer IDs so
     * we can use them in "thin locks".
     */
    BitVector*  threadIdMap;

    /*
     * Manage exit conditions.  The VM exits when all non-daemon threads
     * have exited.  If the main thread returns early, we need to sleep
     * on a condition variable.
     */
    int         nonDaemonThreadCount;   /* must hold threadListLock to access */
    pthread_cond_t  vmExitCond;

    /*
     * The set of DEX files loaded by custom class loaders.
     */
    HashTable*  userDexFiles;

    /*
     * JNI global reference table.
     */
    IndirectRefTable jniGlobalRefTable;
    IndirectRefTable jniWeakGlobalRefTable;
    pthread_mutex_t jniGlobalRefLock;
    pthread_mutex_t jniWeakGlobalRefLock;

    /*
     * JNI pinned object table (used for primitive arrays).
     */
    ReferenceTable  jniPinRefTable;
    pthread_mutex_t jniPinRefLock;

    /*
     * Native shared library table.
     */
    HashTable*  nativeLibs;

    /*
     * GC heap lock.  Functions like gcMalloc() acquire this before making
     * any changes to the heap.  It is held throughout garbage collection.
     */
    pthread_mutex_t gcHeapLock;

    /*
     * Condition variable to queue threads waiting to retry an
     * allocation.  Signaled after a concurrent GC is completed.
     */
    pthread_cond_t gcHeapCond;

    /* Opaque pointer representing the heap. */
    GcHeap*     gcHeap;

    /* The card table base, modified as needed for marking cards. */
    u1*         biasedCardTableBase;

    /*
     * Pre-allocated throwables.
     */
    Object*     outOfMemoryObj;
    Object*     internalErrorObj;
    Object*     noClassDefFoundErrorObj;

    /* Monitor list, so we can free them */
    /*volatile*/ Monitor* monitorList;

    /* Monitor for Thread.sleep() implementation */
    Monitor*    threadSleepMon;

    /* set when we create a second heap inside the zygote */
    bool        newZygoteHeapAllocated;

    /*
     * TLS keys.
     */
    pthread_key_t pthreadKeySelf;       /* Thread*, for dvmThreadSelf */

    /*
     * Cache results of "A instanceof B".
     */
    AtomicCache* instanceofCache;

    /* inline substitution table, used during optimization */
    InlineSub*          inlineSubs;

    /*
     * Bootstrap class loader linear allocator.
     */
    LinearAllocHdr* pBootLoaderAlloc;

    /*
     * Compute some stats on loaded classes.
     */
    int         numLoadedClasses;
    int         numDeclaredMethods;
    int         numDeclaredInstFields;
    int         numDeclaredStaticFields;

    /* when using a native debugger, set this to suppress watchdog timers */
    bool        nativeDebuggerActive;

    /*
     * JDWP debugger support.
     *
     * Note: Each thread will normally determine whether the debugger is active
     * for it by referring to its subMode flags.  "debuggerActive" here should be
     * seen as "debugger is making requests of 1 or more threads".
     */
    bool        debuggerConnected;      /* debugger or DDMS is connected */
    bool        debuggerActive;         /* debugger is making requests */
    JdwpState*  jdwpState;

    /*
     * Registry of objects known to the debugger.
     */
    HashTable*  dbgRegistry;

    /*
     * Debugger breakpoint table.
     */
    BreakpointSet*  breakpointSet;

    /*
     * Single-step control struct.  We currently only allow one thread to
     * be single-stepping at a time, which is all that really makes sense,
     * but it's possible we may need to expand this to be per-thread.
     */
    StepControl stepControl;

    /*
     * DDM features embedded in the VM.
     */
    bool        ddmThreadNotification;

    /*
     * Zygote (partially-started process) support
     */
    bool        zygote;

    /*
     * Used for tracking allocations that we report to DDMS.  When the feature
     * is enabled (through a DDMS request) the "allocRecords" pointer becomes
     * non-NULL.
     */
    pthread_mutex_t allocTrackerLock;
    AllocRecord*    allocRecords;
    int             allocRecordHead;        /* most-recently-added entry */
    int             allocRecordCount;       /* #of valid entries */
    int             allocRecordMax;         /* Number of allocated entries. */

    /*
     * When a profiler is enabled, this is incremented.  Distinct profilers
     * include "dmtrace" method tracing, emulator method tracing, and
     * possibly instruction counting.
     *
     * The purpose of this is to have a single value that shows whether any
     * profiling is going on.  Individual thread will normally check their
     * thread-private subMode flags to take any profiling action.
     */
    volatile int activeProfilers;

    /*
     * State for method-trace profiling.
     */
    MethodTraceState methodTrace;
    Method*     methodTraceGcMethod;
    Method*     methodTraceClassPrepMethod;

    /*
     * State for emulator tracing.
     */
    void*       emulatorTracePage;
    int         emulatorTraceEnableCount;

    /*
     * Global state for memory allocation profiling.
     */
    AllocProfState allocProf;

    /*
     * Pointers to the original methods for things that have been inlined.
     * This makes it easy for us to output method entry/exit records for
     * the method calls we're not actually making.  (Used by method
     * profiling.)
     */
    Method**    inlinedMethods;

    /*
     * Dalvik instruction counts (kNumPackedOpcodes entries).
     */
    int*        executedInstrCounts;
    int         instructionCountEnableCount;

    /*
     * Signal catcher thread (for SIGQUIT).
     */
    pthread_t   signalCatcherHandle;
    bool        haltSignalCatcher;

    /*
     * Stdout/stderr conversion thread.
     */
    bool            haltStdioConverter;
    bool            stdioConverterReady;
    pthread_t       stdioConverterHandle;
    pthread_mutex_t stdioConverterLock;
    pthread_cond_t  stdioConverterCond;
    int             stdoutPipe[2];
    int             stderrPipe[2];

    /*
     * pid of the system_server process. We track it so that when system server
     * crashes the Zygote process will be killed and restarted.
     */
    pid_t systemServerPid;

    int kernelGroupScheduling;

//#define COUNT_PRECISE_METHODS
#ifdef COUNT_PRECISE_METHODS
    PointerSet* preciseMethods;
#endif

    /* some RegisterMap statistics, useful during development */
    void*       registerMapStats;

#ifdef VERIFIER_STATS
    VerifierStats verifierStats;
#endif

    /* String pointed here will be deposited on the stack frame of dvmAbort */
    const char *lastMessage;

    /* String containing the extra option file or 0 */
    char* extraOptionsFile;

    /* String containing the nice name to appear in ps */
    char* niceName;

    /*
     * Card marking max target. Allows to filter card dirtying
     * for target objects above the Immune limit.
     */
#ifdef WITH_CONDMARK
    u1* cardImmuneLimit;
#endif

#ifdef WITH_REGION_GC
    /* Region GC support. */
    bool enableRegionGC;
#endif

#ifdef WITH_CONDMARK
    /* Conditional marking disabled if true. */
    bool disableCondmark;
#endif

#ifdef WITH_TLA
    /* Enable thread local allocation if true. */
    bool withTLA;
#endif

    /* disable fatal errors elimination on VM shutdown */
    bool disableVMExitErrorsElimination;

    /* configurable slot number limit */
    unsigned int tlaSlotNumber;

    /* Used to store a default VM signal handler */
    struct sigaction gOldSigAction;
};

extern struct DvmGlobals gDvm;

#if defined(WITH_JIT)

#define DEFAULT_CODE_CACHE_SIZE 0xffffffff
#define UNINITIALIZED_DATA_CACHE_SIZE 0xffffffff

/* Trace profiling modes.  Ordering matters - off states before on states */
enum TraceProfilingModes {
    kTraceProfilingDisabled = 0,      // Not profiling
    kTraceProfilingPeriodicOff = 1,   // Periodic profiling, off phase
    kTraceProfilingContinuous = 2,    // Always profiling
    kTraceProfilingPeriodicOn = 3     // Periodic profiling, on phase
};

/*
 * Exiting the compiled code w/o chaining will incur overhead to look up the
 * target in the code cache which is extra work only when JIT is enabled. So
 * we want to monitor it closely to make sure we don't have performance bugs.
 */
enum NoChainExits {
    kInlineCacheMiss = 0,
    kCallsiteInterpreted,
    kSwitchOverflow,
    kHeavyweightMonitor,
    kNoChainExitLast,
};

#if defined(VTUNE_DALVIK)
enum VTuneInfo {
    kVTuneInfoDisabled = 0,   // don't collect information about JIT
    kVTuneInfoNativeCode = 1, // common information about JIT code
    kVTuneInfoByteCode = 2,   // mapping from JIT code to byte code
    kVTuneInfoJavaCode = 3    // mapping from JIT code to Java code
};

#define _STR_VALUE(arg)      #arg
#define STR_VALUE(arg) _STR_VALUE(arg)

// Update the default VTune build number to the latest public build
#define VTUNE_VERSION_DEFAULT 313935
#define VTUNE_VERSION_EXPERIMENTAL 7777777
#endif

struct ChainCellCounts;

/**
 * @class sPassFramework
 * @brief The Pass framework's entry point
 */
typedef struct sJitFramework
{
    /** @brief the First Pass of the framework */
    Pass *firstPass;

    /** @brief The general pass gate if there is one, called before each Pass */
    bool (*generalGate) (const CompilationUnit *, Pass*);

    /** @brief Back-end function pointer */
    void (*backEndFunction) (CompilationUnit *, JitTranslationInfo *);

    /** @brief Back-end BasicBlock allocater pointer */
    BasicBlock * (*backEndBasicBlockAllocation) (void);

    /** @brief Back-end CompilationErrorHandler allocater pointer */
    CompilationErrorHandler * (*backEndCompilationErrorHandlerAllocation) (void);

    /** @brief Back-end Dumping a BasicBlock */
    void (*backEndDumpSpecificBB) (CompilationUnit *, BasicBlock *, FILE *, bool);

    /** @brief Back-end gate, do we want to process the trace? */
    bool (*backEndGate) (CompilationUnit *);

    /** @brief Invoke handler */
    const char* (*backEndInvokeArgsDone) (int);

    /** @brief Used to check whether backend supports extended opcode */
    bool (*backendSupportExtendedOp) (int);

    /** @brief Back-end callback to add a symbol at a specific location in the JIT code cache */
    void (*backEndSymbolCreationCallback) (const char *, void *);

    /** @brief Middle-end function pointer */
    bool (*middleEndFunction) (JitTraceDescription *, int, JitTranslationInfo *, jmp_buf *, int );

    /** @brief Middle-end gate, do we want to process the trace? */
    bool (*middleEndGate) (JitTraceDescription *, int, JitTranslationInfo *, jmp_buf *, int );

    /** @brief What is maximum number of scratch registers allowed? */
    unsigned int (*scratchRegAvail) (void);

    /** @brief Function to dump verbose trace information to logcat */
    void (*printTrace) (CompilationUnit *cUnit, ChainCellCounts &chainCellCounts, int wide_const_count, u2* pCCOffsetSection);
}SJitFramework;

/*
 * Values for gDvmJit.codeGenerator.
 */
enum CodeGenerators {
    LCG = 0,
    PCG,
};

/*
 * JIT-specific global state
 */
struct DvmJitGlobals {
    /*
     * Guards writes to Dalvik PC (dPC), translated code address (codeAddr) and
     * chain fields within the JIT hash table.  Note carefully the access
     * mechanism.
     * Only writes are guarded, and the guarded fields must be updated in a
     * specific order using atomic operations.  Further, once a field is
     * written it cannot be changed without halting all threads.
     *
     * The write order is:
     *    1) codeAddr
     *    2) dPC
     *    3) chain [if necessary]
     *
     * This mutex also guards both read and write of curJitTableEntries.
     */
    pthread_mutex_t tableLock;

    /* The JIT hash table.  Note that for access speed, copies of this pointer
     * are stored in each thread. */
    struct JitEntry *pJitEntryTable;

    /* Array of compilation trigger threshold counters */
    unsigned char *pProfTable;

    /* Trace profiling counters */
    struct JitTraceProfCounters *pJitTraceProfCounters;

    /* Copy of pProfTable used for temporarily disabling the Jit */
    unsigned char *pProfTableCopy;

    /* Size of JIT hash table in entries.  Must be a power of 2 */
    unsigned int jitTableSize;

    /* Mask used in hash function for JitTable.  Should be jitTableSize-1 */
    unsigned int jitTableMask;

    /* How many entries in the JitEntryTable are in use */
    unsigned int jitTableEntriesUsed;

    /* Max bytes allocated for the code cache.  Rough rule of thumb: 1K per 1M of system RAM */
    unsigned int codeCacheSize;

    /* Max bytes allocated for the data cache */
    unsigned int dataCacheSize;

    /* Trigger for trace selection */
    unsigned short threshold;

    /* JIT Compiler Control */
    bool               haltCompilerThread;
    bool               blockingMode;
    bool               methodTraceSupport;
    bool               genSuspendPoll;
    Thread*            compilerThread;
    pthread_t          compilerHandle;
    pthread_mutex_t    compilerLock;
    pthread_mutex_t    compilerICPatchLock;
    pthread_cond_t     compilerQueueActivity;
    pthread_cond_t     compilerQueueEmpty;
    volatile int       compilerQueueLength;
    int                compilerHighWater;
    int                compilerWorkEnqueueIndex;
    int                compilerWorkDequeueIndex;
    int                compilerICPatchIndex;

    /* JIT internal stats */
    int                compilerMaxQueued;
    int                translationChains;

    /* Compiled code cache */
    void* codeCache;

    /* Compiled data cache */
    void* dataCache;


    /*
     * This is used to store the base address of an in-flight compilation whose
     * class object pointers have been calculated to populate literal pool.
     * Once the compiler thread has changed its status to VM_WAIT, we cannot
     * guarantee whether GC has happened before the code address has been
     * installed to the JIT table. Because of that, this field can only
     * been cleared/overwritten by the compiler thread if it is in the
     * THREAD_RUNNING state or in a safe point.
     */
    void *inflightBaseAddr;

    /* Translation cache version (protected by compilerLock */
    int cacheVersion;

    /*
     * Loop cache information: map telling us if the loop is not a header
     * The loop cache is used to know if an offset is a loop head or not. It helps reduce compilation time.
     * The loop cache contains all the BasicBlocks that are known to NOT be loop heads
     */
    std::map<const u2 *, bool> knownNonLoopHeaderCache;

    /* Bytes used by the code templates */
    unsigned int templateSize;

    /* Bytes already used in the code cache */
    volatile unsigned int codeCacheByteUsed;

    /* Bytes already used in the data cache */
    volatile unsigned int dataCacheByteUsed;

    /* Number of installed compilations in the cache */
    unsigned int numCompilations;

    /* Flag to indicate that the code cache is full */
    bool codeCacheFull;

    /* Flag to indicate that the data cache is full */
    bool dataCacheFull;

    /* Page size  - 1 */
    unsigned int pageSizeMask;

    /* Lock to change the protection type of the code cache */
    pthread_mutex_t    codeCacheProtectionLock;

    /* Lock to change the protection type of the data cache */
    pthread_mutex_t    dataCacheProtectionLock;

    /* Number of times that the code cache has been reset */
    int numCodeCacheReset;

    /* Number of times that the code cache reset request has been delayed */
    int numCodeCacheResetDelayed;

    /* true/false: compile/reject opcodes specified in the -Xjitop list */
    bool includeSelectedOp;

    /* true/false: compile/reject methods specified in the -Xjitmethod list */
    bool includeSelectedMethod;

    /* true/false: compile/reject traces with offset specified in the -Xjitoffset list */
    bool includeSelectedOffset;

    /* Disable JIT for selected opcodes - one bit for each opcode */
    char opList[(kNumPackedOpcodes+7)/8];

    /* Disable JIT for selected methods */
    HashTable *methodTable;

    /* Disable JIT for selected classes */
    HashTable *classTable;

    /* Disable JIT for selected offsets */
    unsigned int pcTable[COMPILER_PC_OFFSET_SIZE];
    int num_entries_pcTable;

    /* Flag to dump all compiled code */
    bool printMe;

#if defined(VTUNE_DALVIK)
    /* Flag to enable VTune support for Dalvik VM */
    VTuneInfo vtuneInfo;
    /* Generate jit files compatible with specific VTune build. Default is VTUNE_VERSION_DEFAULT. */
    int vtuneVersion;
#endif

    /* Flag to dump compiled binary code in bytes */
    bool printBinary;

    /* Flag to control instruction scheduling */
    bool scheduling;

    /* Flag to control nested loops in JIT mode or not */
    bool nestedLoops;

    /* Flag to control loops with branches in JIT mode or not */
    bool branchLoops;

    /* Flag to control whether the loops should be tested */
    bool testLoops;

    /* Flag to control whether failure to load a plugin is fatal */
    bool userpluginfatal;

    /* Flag to control whether a failure to load a plugin has occured */
    bool userpluginfailed;

    /* Flag to control backend registerization */
    bool backEndRegisterization;

    /* Flag to control the number of vector registers to use */
    unsigned char vectorRegisters;

    /* Flag to control the number of the minimum vectorized iterations */
    unsigned char minVectorizedIterations;

    /* Integer to control the number of backend retries */
    int backEndRetries;

    /* Structure to handle the Jit framework */
    SJitFramework jitFramework;

    /* Selector for code generator to use
     * This is used only during initializaton to choose how to set up
     * function pointers in the 'jitFramework' structure
     */
    CodeGenerators codeGenerator;

    /*
     * Flag to control maximum number of registerization requests when
     * backend registerization is enabled.
     */
    unsigned int maximumRegisterization;

    /*
     * Used to determine the maximum number of bytecode when considering method to inline
     */
    unsigned int maximumInliningNumBytecodes;

    /*
     * Used to determine the maximum number of scratch registers that can be used by optimization passes
     */
    unsigned int maximumScratchRegisters;

    /* Map to handle options for the backend */
    std::map<std::string, std::string> backendOptions;

    /* Unprocessed string for the backend */
    char * backendString;

    /* Flag to control which loop detection system is being used */
    bool oldLoopDetection;

    /* Flag to control the loop passes executed */
    char *ignorePasses;

    /* Used to control verbosity of pass actions. Contains names of passes to be verbose for */
    char *debugPasses;

    /* Used to control verbosity for all passes */
    bool debugAllPasses;

    /* Used to dump Control Flow Graph after each loop optimization pass */
    bool debugDumpCFGAfterLoopOpt;

    /* Per-process debug flag toggled when receiving a SIGUSR2 */
    bool receivedSIGUSR2;

    /* Trace profiling mode */
    TraceProfilingModes profileMode;

    /* Periodic trace profiling countdown timer */
    int profileCountdown;

    /* Vector to disable selected optimizations */
    int disableOpt;

    /* Table to track the overall and trace statistics of hot methods */
    HashTable*  methodStatsTable;

    /* Filter method compilation blacklist with call-graph information */
    bool checkCallGraph;

    /* New translation chain has been set up */
    volatile bool hasNewChain;

#if defined(WITH_SELF_VERIFICATION)
    /* Spin when error is detected, volatile so GDB can reset it */
    volatile bool selfVerificationSpin;
#endif

    /* Framework or stand-alone? */
    bool runningInAndroidFramework;

    /* Framework callback happened? */
    bool alreadyEnabledViaFramework;

    /* Framework requests to disable the JIT for good */
    bool disableJit;

#if defined(ARCH_IA32)
    /** @brief Identifier for cpu family
     *  @details http://software.intel.com/en-us/articles/intel-architecture-and-
     *  processor-identification-with-cpuid-model-and-family-numbers */
    int cpuFamily;

    /** @brief Identifier for cpu model
     *  @details http://software.intel.com/en-us/articles/intel-architecture-and-
     *  processor-identification-with-cpuid-model-and-family-numbers */
    int cpuModel;

    /**
     * @brief CPU Feature information about the CPU
     */
    int featureInformation[2];
#endif

#if defined(SIGNATURE_BREAKPOINT)
    /* Signature breakpoint */
    u4 signatureBreakpointSize;         // # of words
    u4 *signatureBreakpoint;            // Signature content
#endif

#if defined(WITH_JIT_TUNING)
    /* Performance tuning counters */
    int                addrLookupsFound;
    int                addrLookupsNotFound;
    int                noChainExit[kNoChainExitLast];
    int                normalExit;
    int                puntExit;
    int                invokeMonomorphic;
    int                invokePolymorphic;
    int                invokeNative;
    int                invokeMonoGetterInlined;
    int                invokeMonoSetterInlined;
    int                invokePolyGetterInlined;
    int                invokePolySetterInlined;
    int                returnOp;
    int                icPatchInit;
    int                icPatchLockFree;
    int                icPatchQueued;
    int                icPatchRejected;
    int                icPatchDropped;
    int                codeCachePatches;
    int                numCompilerThreadBlockGC;
    u8                 jitTime;
    u8                 compilerThreadBlockGCStart;
    u8                 compilerThreadBlockGCTime;
    u8                 maxCompilerThreadBlockGCTime;

    /* The list of methods that is being profiled, if not NULL, VM is run in method profile mode */
    HashTable*         methodProfTable;
    /* The dir prefix for cfg dump */
    char*              cfgDirPrefix;
#endif

#if defined(ARCH_IA32)
    JitOptLevel        optLevel;
#endif

    /* Place arrays at the end to ease the display in gdb sessions */

    /* Work order queue for compilations */
    CompilerWorkOrder compilerWorkQueue[COMPILER_WORK_QUEUE_SIZE];

    /* Work order queue for predicted chain patching */
    ICPatchWorkOrder compilerICPatchQueue[COMPILER_IC_PATCH_QUEUE_SIZE];

    /* If flag is true, we abort the VM if any error happens during JIT
     * compilation, even if we can possibly ignore it and move on
     */
    bool abortOnCompilerError;
};

extern struct DvmJitGlobals gDvmJit;

#if defined(WITH_JIT_TUNING)
extern int gDvmICHitCount;
#endif

#endif

struct DvmJniGlobals {
    bool useCheckJni;
    bool warnOnly;
    bool forceCopy;

    // Provide backwards compatibility for pre-ICS apps on ICS.
    bool workAroundAppJniBugs;

    // Debugging help for third-party developers. Similar to -Xjnitrace.
    bool logThirdPartyJni;

    // We only support a single JavaVM per process.
    JavaVM*     jniVm;
};

extern struct DvmJniGlobals gDvmJni;

#endif  // DALVIK_GLOBALS_H_
