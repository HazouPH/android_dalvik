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

#define ATRACE_TAG ATRACE_TAG_DALVIK

/*
 * Garbage-collecting memory allocator.
 */
#include "Dalvik.h"
#include "alloc/HeapBitmap.h"
#include "alloc/Verify.h"
#include "alloc/Heap.h"
#include "alloc/HeapInternal.h"
#include "alloc/DdmHeap.h"
#include "alloc/HeapSource.h"
#include "alloc/MarkSweep.h"
#include "alloc/CardTable.h"
#ifdef WITH_TLA
#include "alloc/ThreadLocalHeap.h"
#endif

#include "os/os.h"

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <limits.h>
#include <errno.h>

#include <cutils/trace.h>

static const GcSpec kGcForMallocSpec = {
    true,  /* isPartial */
    false,  /* isConcurrent */
    true,  /* doPreserve */
    "GC_FOR_ALLOC"
};

const GcSpec *GC_FOR_MALLOC = &kGcForMallocSpec;

static const GcSpec kGcConcurrentSpec  = {
    true,  /* isPartial */
    true,  /* isConcurrent */
    true,  /* doPreserve */
    "GC_CONCURRENT"
};

const GcSpec *GC_CONCURRENT = &kGcConcurrentSpec;

static const GcSpec kGcExplicitSpec = {
    false, /* isPartial */
    true,  /* isConcurrent */
    true,  /* doPreserve */
    "GC_EXPLICIT"
};

const GcSpec *GC_EXPLICIT = &kGcExplicitSpec;

static const GcSpec kGcBeforeOomSpec = {
    false,  /* isPartial */
    false,  /* isConcurrent */
    false,  /* doPreserve */
    "GC_BEFORE_OOM"
};

const GcSpec *GC_BEFORE_OOM = &kGcBeforeOomSpec;

/*
 * Initialize the GC heap.
 *
 * Returns true if successful, false otherwise.
 */
bool dvmHeapStartup()
{
    GcHeap *gcHeap;

    if (gDvm.heapGrowthLimit == 0) {
        gDvm.heapGrowthLimit = gDvm.heapMaximumSize;
    }

    gcHeap = dvmHeapSourceStartup(gDvm.heapStartingSize,
                                  gDvm.heapMaximumSize,
                                  gDvm.heapGrowthLimit);
    if (gcHeap == NULL) {
        return false;
    }
    gcHeap->ddmHpifWhen = 0;
    gcHeap->ddmHpsgWhen = 0;
    gcHeap->ddmHpsgWhat = 0;
    gcHeap->ddmNhsgWhen = 0;
    gcHeap->ddmNhsgWhat = 0;
    gDvm.gcHeap = gcHeap;

    /* Set up the lists we'll use for cleared reference objects.
     */
    gcHeap->clearedReferences = NULL;

    if (!dvmCardTableStartup(gDvm.heapMaximumSize, gDvm.heapGrowthLimit)) {
        LOGE_HEAP("card table startup failed.");
        return false;
    }

    return true;
}

bool dvmHeapStartupAfterZygote()
{
    bool result = dvmHeapSourceStartupAfterZygote();

#ifdef WITH_TLA
    if (result == true)
    {
        return dvmTLHeapSourceStartupAfterZygote();
    }
#endif

    return result;
}

void dvmHeapShutdown()
{
//TODO: make sure we're locked
    if (gDvm.gcHeap != NULL) {
        dvmCardTableShutdown();
        /* Destroy the heap.  Any outstanding pointers will point to
         * unmapped memory (unless/until someone else maps it).  This
         * frees gDvm.gcHeap as a side-effect.
         */
#ifdef WITH_TLA
        dvmTLHeapSourceShutdown();
#endif
        dvmHeapSourceShutdown(&gDvm.gcHeap);
    }
}

/*
 * Shutdown any threads internal to the heap.
 */
void dvmHeapThreadShutdown()
{
    dvmHeapSourceThreadShutdown();
}

/*
 * Grab the lock, but put ourselves into THREAD_VMWAIT if it looks like
 * we're going to have to wait on the mutex.
 */
bool dvmSpinAndLockHeap(void)
{
    if (dvmTryLockMutex(&gDvm.gcHeapLock) != 0) {

        Thread *self;
        ThreadStatus oldStatus;

        self = dvmThreadSelf();
        oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);

#if ANDROID_SMP != 0
        /*
         * Multi-processor case: spin a little to avoid rescheduling
         * in case of short alloc/alloc or alloc/sweep lock collision.
         * 100 us is enough to capture most collisions while still
         * reasonable compared to scheduler freq (1/1000).
         * GC pause times can be way over the 100 us range, so in the
         * case of collision with GC pauses we skip the spinning
         * and give back hand to the kernel.
         *
         * TODO: May be worth to try true kernel adaptive mutex if
         * bionic ever supports it.
         */
        const int kHeapLockSpinTime = 100;
        u8 spinUntil  = dvmGetRelativeTimeUsec() + kHeapLockSpinTime;
        while (dvmTryLockMutex(&gDvm.gcHeapLock) != 0)
        {
#ifdef ARCH_IA32
            /* pause cpu to reduce spinning overhead */
            __asm__ ( "pause;" );
#endif

            /* wait on suspend if required */
            dvmCheckSuspendPending(self);

            /*
             * stop spinning if spin time has elapsed
             */
            if (dvmGetRelativeTimeUsec() > spinUntil)
            {
                dvmLockMutex(&gDvm.gcHeapLock);
                break;
            }
         }
#else
        dvmLockMutex(&gDvm.gcHeapLock);
#endif

        dvmChangeStatus(self, oldStatus);
    }
    return true;
}

bool dvmLockHeap(void)
{
    if (dvmTryLockMutex(&gDvm.gcHeapLock) != 0) {
        Thread *self;
        ThreadStatus oldStatus;

        self = dvmThreadSelf();
        oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);
        dvmLockMutex(&gDvm.gcHeapLock);
        dvmChangeStatus(self, oldStatus);
    }
    return true;
}

void dvmUnlockHeap(void)
{
    dvmUnlockMutex(&gDvm.gcHeapLock);
}

/* Refresh Alloc Profile after allocation.
 * Must be called under heap lock.
 */
static void allocProf(void* ptr,size_t size)
{
    if (ptr != NULL) {
        /* We've got the memory. */
        Thread* self = dvmThreadSelf();
        gDvm.allocProf.allocCount++;
        gDvm.allocProf.allocSize += size;
        if (self != NULL) {
            self->allocProf.allocCount++;
            self->allocProf.allocSize += size;
        }
    } else {
        /* The allocation failed. */
        Thread* self = dvmThreadSelf();
        gDvm.allocProf.failedAllocCount++;
        gDvm.allocProf.failedAllocSize += size;
        if (self != NULL) {
            self->allocProf.failedAllocCount++;
            self->allocProf.failedAllocSize += size;
        }
    }
}

/* Do a full garbage collection, which may grow the
 * heap as a side-effect if the live set is large.
 */
static void gcForMalloc(bool clearSoftReferences)
{
    if (gDvm.allocProf.enabled) {
        Thread* self = dvmThreadSelf();
        gDvm.allocProf.gcCount++;
        if (self != NULL) {
            self->allocProf.gcCount++;
        }
    }
    /* This may adjust the soft limit as a side-effect.
     */
    const GcSpec *spec = clearSoftReferences ? GC_BEFORE_OOM : GC_FOR_MALLOC;
    dvmCollectGarbageInternal(spec);
}

/* Try as hard as possible to allocate some memory.
 */
static void *tryMalloc(size_t size, bool clear = true)
{
    void *ptr;

//TODO: figure out better heuristics
//    There will be a lot of churn if someone allocates a bunch of
//    big objects in a row, and we hit the frag case each time.
//    A full GC for each.
//    Maybe we grow the heap in bigger leaps
//    Maybe we skip the GC if the size is large and we did one recently
//      (number of allocations ago) (watch for thread effects)
//    DeflateTest allocs a bunch of ~128k buffers w/in 0-5 allocs of each other
//      (or, at least, there are only 0-5 objects swept each time)

    ptr = dvmHeapSourceAlloc(size, clear);
    if (ptr != NULL) {
        return ptr;
    }

    /*
     * The allocation failed.  If the GC is running, block until it
     * completes and retry.
     */
    if (gDvm.gcHeap->gcRunning) {
        /*
         * The GC is concurrently tracing the heap.  Release the heap
         * lock, wait for the GC to complete, and retrying allocating.
         */
        dvmWaitForConcurrentGcToComplete();
    } else {
      /*
       * Try a foreground GC since a concurrent GC is not currently running.
       */
      gcForMalloc(false);
    }

    ptr = dvmHeapSourceAlloc(size,clear);
    if (ptr != NULL) {
        return ptr;
    }

    /* Even that didn't work;  this is an exceptional state.
     * Try harder, growing the heap if necessary.
     */
    ptr = dvmHeapSourceAllocAndGrow(size,clear);
    if (ptr != NULL) {
        size_t newHeapSize;

        newHeapSize = dvmHeapSourceGetIdealFootprint();
//TODO: may want to grow a little bit more so that the amount of free
//      space is equal to the old free space + the utilization slop for
//      the new allocation.
        LOGI_HEAP("Grow heap (frag case) to "
                "%zu.%03zuMB for %zu-byte allocation",
                FRACTIONAL_MB(newHeapSize), size);
        return ptr;
    }

    /* Most allocations should have succeeded by now, so the heap
     * is really full, really fragmented, or the requested size is
     * really big.  Do another GC, collecting SoftReferences this
     * time.  The VM spec requires that all SoftReferences have
     * been collected and cleared before throwing an OOME.
     */
//TODO: wait for the finalizers from the previous GC to finish
    LOGI_HEAP("Forcing collection of SoftReferences for %zu-byte allocation",
            size);
    gcForMalloc(true);
    ptr = dvmHeapSourceAllocAndGrow(size,clear);
    if (ptr != NULL) {
        return ptr;
    }
//TODO: maybe wait for finalizers and try one last time

    LOGE_HEAP("Out of memory on a %zd-byte allocation.", size);
//TODO: tell the HeapSource to dump its state
    dvmDumpThread(dvmThreadSelf(), false);

    return NULL;
}

#ifdef WITH_TLA
/* Try as hard as possible to allocate from local heap.
 */
static void *tryTLHeapMalloc(TLHeap* tlh, size_t size)
{
    void* ptr = dvmTLHeapAlloc(tlh,size);

    if (LIKELY(ptr != NULL)) {
        return ptr;
    }

    /* Slow Path: Retry alloc after adding some mem blocks to the heap */

    /* aggressively acquire lock */
    dvmSpinAndLockHeap();

    size_t blockSize = dvmTLHeapGetBlockSizeForAlloc(tlh,size);

    if (blockSize != 0 ) {
        void*  blocks[TLPREALLOC_NUM];

        /* First try will with tryMalloc which will force GC if required */
        blocks[0] = tryMalloc(blockSize,false);

        /* Try to allocate some more from heap source if we can */
        if (blocks[0] != NULL) {
            size_t blockCount = 1;
            size_t blockNum   = dvmTLHeapGetBlockNumForAlloc(tlh,size);

            for ( ; blockCount < blockNum; blockCount++) {
                blocks[blockCount] = dvmHeapSourceAlloc(blockSize,false);
                if(blocks[blockCount] == NULL) {
                    break;
                }
            }

            dvmUnlockHeap();
            /* Now we have some better chance of success, retry...*/
            return dvmTLHeapAllocFromNewBlocks(tlh,size,blockCount,blocks,blockSize);
        }
    }

    /* Last chance : may be we can still allocate from global heap */
    ptr = tryMalloc(size);
    dvmUnlockHeap();
    return ptr;
}
#endif


/* Throw an OutOfMemoryError if there's a thread to attach it to.
 * Avoid recursing.
 *
 * The caller must not be holding the heap lock, or else the allocations
 * in dvmThrowException() will deadlock.
 */
static void throwOOME()
{
    Thread *self;

    if ((self = dvmThreadSelf()) != NULL) {
        /* If the current (failing) dvmMalloc() happened as part of thread
         * creation/attachment before the thread became part of the root set,
         * we can't rely on the thread-local trackedAlloc table, so
         * we can't keep track of a real allocated OOME object.  But, since
         * the thread is in the process of being created, it won't have
         * a useful stack anyway, so we may as well make things easier
         * by throwing the (stackless) pre-built OOME.
         */
        if (dvmIsOnThreadList(self) && !self->throwingOOME) {
            /* Let ourselves know that we tried to throw an OOM
             * error in the normal way in case we run out of
             * memory trying to allocate it inside dvmThrowException().
             */
            self->throwingOOME = true;

            /* Don't include a description string;
             * one fewer allocation.
             */
            dvmThrowOutOfMemoryError(NULL);
        } else {
            /*
             * This thread has already tried to throw an OutOfMemoryError,
             * which probably means that we're running out of memory
             * while recursively trying to throw.
             *
             * To avoid any more allocation attempts, "throw" a pre-built
             * OutOfMemoryError object (which won't have a useful stack trace).
             *
             * Note that since this call can't possibly allocate anything,
             * we don't care about the state of self->throwingOOME
             * (which will usually already be set).
             */
            dvmSetException(self, gDvm.outOfMemoryObj);
        }
        /* We're done with the possible recursion.
         */
        self->throwingOOME = false;
    }
}

/*
 * Allocate storage on the GC heap.  We guarantee 8-byte alignment.
 *
 * The new storage is zeroed out.
 *
 * Note that, in rare cases, this could get called while a GC is in
 * progress.  If a non-VM thread tries to attach itself through JNI,
 * it will need to allocate some objects.  If this becomes annoying to
 * deal with, we can block it at the source, but holding the allocation
 * mutex should be enough.
 *
 * In rare circumstances (JNI AttachCurrentThread) we can be called
 * from a non-VM thread.
 *
 * Use ALLOC_DONT_TRACK when we either don't want to track an allocation
 * (because it's being done for the interpreter "new" operation and will
 * be part of the root set immediately) or we can't (because this allocation
 * is for a brand new thread).
 *
 * Returns NULL and throws an exception on failure.
 *
 * TODO: don't do a GC if the debugger thinks all threads are suspended
 */
void* dvmMalloc(size_t size, int flags)
{
    void *ptr;

#ifdef WITH_TLA
    TLHeap* tlh = dvmThreadSelf()->tlh;
    if (    ( tlh != NULL)
         && ( size <= TLALLOC_MAX_SIZE )
         && ( size >= TLALLOC_MIN_SIZE ) ){
        /* Try as hard as possible to allocate some local memory.
         */
        ptr = tryTLHeapMalloc(tlh,size);

        if (ptr != NULL) {
            dvmHeapSourceSetObjectBit(ptr);
        }


        if (gDvm.allocProf.enabled) {
            dvmLockHeap();
            allocProf(ptr,size);
            dvmUnlockHeap();
        }

    } else
#endif
    {
        dvmSpinAndLockHeap();
        /* Try as hard as possible to allocate some memory.
         */
        ptr = tryMalloc(size);

        if (ptr != NULL) {
            dvmHeapSourceSetObjectBit(ptr);
        }

        if (gDvm.allocProf.enabled) {
            allocProf(ptr,size);
        }

        dvmUnlockHeap();

    }

    if (ptr != NULL) {
        /*
         * If caller hasn't asked us not to track it, add it to the
         * internal tracking list.
         */
        if ((flags & ALLOC_DONT_TRACK) == 0) {
            dvmAddTrackedAlloc((Object*)ptr, NULL);
        }
    } else {
        /*
         * The allocation failed; throw an OutOfMemoryError.
         */
        throwOOME();
    }

    return ptr;
}

/*
 * Returns true iff <obj> points to a valid allocated object.
 */
bool dvmIsValidObject(const Object* obj)
{
    /* Don't bother if it's NULL or not 8-byte aligned.
     */
    if (obj != NULL && ((uintptr_t)obj & (8-1)) == 0) {
        /* Even if the heap isn't locked, this shouldn't return
         * any false negatives.  The only mutation that could
         * be happening is allocation, which means that another
         * thread could be in the middle of a read-modify-write
         * to add a new bit for a new object.  However, that
         * RMW will have completed by the time any other thread
         * could possibly see the new pointer, so there is no
         * danger of dvmIsValidObject() being called on a valid
         * pointer whose bit isn't set.
         *
         * Freeing will only happen during the sweep phase, which
         * only happens while the heap is locked.
         */
        return dvmHeapSourceContains(obj);
    }
    return false;
}

size_t dvmObjectSizeInHeap(const Object *obj)
{
#ifdef WITH_TLA
        if (gDvm.gcHeap->tlhSource != NULL)
        {
            size_t size = dvmTLHeapSourceChunkSize((void*)obj);
            if (size != 0)
            {
                return size;
            }
        }
#endif
    return dvmHeapSourceChunkSize(obj);
}

static void verifyRootsAndHeap()
{
    dvmVerifyRoots();
    dvmVerifyBitmap(dvmHeapSourceGetLiveBits());
}


/*
 * Initiate garbage collection.
 *
 * NOTES:
 * - If we don't hold gDvm.threadListLock, it's possible for a thread to
 *   be added to the thread list while we work.  The thread should NOT
 *   start executing, so this is only interesting when we start chasing
 *   thread stacks.  (Before we do so, grab the lock.)
 *
 * We are not allowed to GC when the debugger has suspended the VM, which
 * is awkward because debugger requests can cause allocations.  The easiest
 * way to enforce this is to refuse to GC on an allocation made by the
 * JDWP thread -- we have to expand the heap or fail.
 */
void dvmCollectGarbageInternal(const GcSpec* spec)
{
    GcHeap *gcHeap = gDvm.gcHeap;
    u4 gcEnd = 0;
    u4 rootStart = 0 , rootEnd = 0;
    u4 dirtyStart = 0, dirtyEnd = 0;
    size_t numObjectsFreed, numBytesFreed;
    size_t currAllocated, currFootprint;
    size_t percentFree;
    int oldThreadPriority = INT_MAX;
    bool isConcurrent = spec->isConcurrent;
    bool isPartial    = spec->isPartial;
    bool doPreserve   = spec->doPreserve;

    /* The heap lock must be held.
     */

    if (gcHeap->gcRunning) {
        LOGW_HEAP("Attempted recursive GC");
        return;
    }

    // Trace the beginning of the top-level GC.
    if (spec == GC_FOR_MALLOC) {
        ATRACE_BEGIN("GC (alloc)");
    } else if (spec == GC_CONCURRENT) {
        ATRACE_BEGIN("GC (concurrent)");
    } else if (spec == GC_EXPLICIT) {
        ATRACE_BEGIN("GC (explicit)");
    } else if (spec == GC_BEFORE_OOM) {
        ATRACE_BEGIN("GC (before OOM)");
    } else {
        ATRACE_BEGIN("GC (unknown)");
    }

    gcHeap->gcRunning = true;

    rootStart = dvmGetRelativeTimeMsec();
    ATRACE_BEGIN("GC: Threads Suspended"); // Suspend A
    dvmSuspendAllThreads(SUSPEND_FOR_GC);

    if (   (gcHeap->forceMajorGC == true)
        && (isPartial == true)
        && (gcHeap->mumConsecutivePartialGC > 5 ))
    {
        /* Major Collection has been requested, clear partial flag */
        isPartial = false;
    }

    if (isPartial == true) {
        /* refresh number of consecutive Partial GC */
        gcHeap->mumConsecutivePartialGC ++;
    }
    else {
        /* Reset partial counter and Major Collection Trigger */
        gcHeap->mumConsecutivePartialGC = 0;
        gcHeap->forceMajorGC = false;
    }

    /*
     * If we are not marking concurrently raise the priority of the
     * thread performing the garbage collection.
     */
    if (isConcurrent == false) {
        oldThreadPriority = os_raiseThreadPriority();
    }
    if (gDvm.preVerify) {
        LOGV_HEAP("Verifying roots and heap before GC");
        verifyRootsAndHeap();
    }

    dvmMethodTraceGCBegin();

    /* Set up the marking context.
     */
    if (!dvmHeapBeginMarkStep(isPartial)) {
        ATRACE_END(); // Suspend A
        ATRACE_END(); // Top-level GC
        LOGE_HEAP("dvmHeapBeginMarkStep failed; aborting");
        dvmAbort();
    }

    /* Mark the set of objects that are strongly reachable from the roots.
     */
    LOGD_HEAP("Marking...");
    dvmHeapMarkRootSet();

    /* dvmHeapScanMarkedObjects() will build the lists of known
     * instances of the Reference classes.
     */
    assert(gcHeap->softReferences == NULL);
    assert(gcHeap->weakReferences == NULL);
    assert(gcHeap->finalizerReferences == NULL);
    assert(gcHeap->phantomReferences == NULL);
    assert(gcHeap->clearedReferences == NULL);

#ifdef WITH_REGION_GC
    dvmSetEnableCrossHeapPointerCheck(isPartial == false);
#endif

    if (isConcurrent == true) {
#ifdef WITH_REGION_GC
        /* Need to clear active heap if Partial */
        dvmClearCardTable(isPartial);
#else
        dvmClearCardTable();
#endif

#ifdef WITH_CONDMARK
        /*
         * need to enable full card marking for concurrent scan write barrier
         */
        dvmDisableCardImmuneLimit();
#endif
        /*
         * Resume threads while tracing from the roots.  We unlock the
         * heap to allow mutator threads to allocate from free space.
         */
        dvmUnlockHeap();
        dvmResumeAllThreads(SUSPEND_FOR_GC);
        ATRACE_END(); // Suspend A
        rootEnd = dvmGetRelativeTimeMsec();
    }


    /* Recursively mark any objects that marked objects point to strongly.
     * If we're not collecting soft references, soft-reachable
     * objects will also be marked.
     */
    LOGD_HEAP("Recursing...");
#ifdef WITH_REGION_GC
    dvmHeapScanMarkedObjects(isPartial);
#else
    dvmHeapScanMarkedObjects();
#endif

    if (isConcurrent == true) {
        /*
         * Re-acquire the heap lock and perform the final thread
         * suspension.
         */
        dirtyStart = dvmGetRelativeTimeMsec();
        dvmLockHeap();
        ATRACE_BEGIN("GC: Threads Suspended"); // Suspend B
        dvmSuspendAllThreads(SUSPEND_FOR_GC);

#ifdef WITH_CONDMARK
        /*
         * Scanning is done. we can now reset card table immune limit.
         */
        dvmEnableCardImmuneLimit();
#endif

        /*
         * As no barrier intercepts root updates, we conservatively
         * assume all roots may be gray and re-mark them.
         */
        dvmHeapReMarkRootSet();
        /*
         * With the exception of reference objects and weak interned
         * strings, all gray objects should now be on dirty cards.
         */
        if (gDvm.verifyCardTable) {
            dvmVerifyCardTable();
        }
        /*
         * Recursively mark gray objects pointed to by the roots or by
         * heap objects dirtied during the concurrent mark.
         */
        dvmHeapReScanMarkedObjects();
    }

#ifdef WITH_REGION_GC
    else if (isPartial == true)
    {
        /*
         * Region GC bypasses zygote heap scanning.
         * To keep correctness, it needs to scan the objects in dirty
         * cards of zygote heap since those objects may contain pointers
         * to active heap.
         * Steps:
         * 1. Clear card table of active heap. Only keep the card table of zygote heap
         * 2. Scan the heap again from the objects in dirty card table.
         */
        dvmClearCardTable(true);
        dvmHeapReScanMarkedObjects();
    }
#endif

    /*
     * All strongly-reachable objects have now been marked.  Process
     * weakly-reachable objects discovered while tracing.
     */
    dvmHeapProcessReferences(&gcHeap->softReferences,
                             doPreserve == false,
                             &gcHeap->weakReferences,
                             &gcHeap->finalizerReferences,
                             &gcHeap->phantomReferences);

#if defined(WITH_JIT)
    /*
     * Patching a chaining cell is very cheap as it only updates 4 words. It's
     * the overhead of stopping all threads and synchronizing the I/D cache
     * that makes it expensive.
     *
     * Therefore we batch those work orders in a queue and go through them
     * when threads are suspended for GC.
     */
    dvmCompilerPerformSafePointChecks();
#endif

    LOGD_HEAP("Sweeping...");

    dvmHeapSweepSystemWeaks();

    /*
     * Live objects have a bit set in the mark bitmap, swap the mark
     * and live bitmaps.  The sweep can proceed concurrently viewing
     * the new live bitmap as the old mark bitmap, and vice versa.
     */
    dvmHeapSourceSwapBitmaps();

    if (gDvm.postVerify) {
        LOGV_HEAP("Verifying roots and heap after GC");
        verifyRootsAndHeap();
    }

    if (isConcurrent == true) {
        dvmUnlockHeap();
        dvmResumeAllThreads(SUSPEND_FOR_GC);
        ATRACE_END(); // Suspend B
        dirtyEnd = dvmGetRelativeTimeMsec();
    }
    dvmHeapSweepUnmarkedObjects(isPartial,isConcurrent,
                                &numObjectsFreed, &numBytesFreed);
    LOGD_HEAP("Cleaning up...");
    dvmHeapFinishMarkStep();
    if (isConcurrent == true) {
        dvmLockHeap();
    }

#ifdef WITH_TLA
    if (isPartial == false) {
        dvmTLHeapSourceReleaseFree(spec->isConcurrent);
    }
#endif


    LOGD_HEAP("Done.");

    /* Now's a good time to adjust the heap size, since
     * we know what our utilization is.
     *
     * This doesn't actually resize any memory;
     * it just lets the heap grow more when necessary.
     */
    dvmHeapSourceGrowForUtilization();

    currAllocated = dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, NULL, 0);
    currFootprint = dvmHeapSourceGetValue(HS_FOOTPRINT, NULL, 0);

    if (isPartial == true) {
        /* Major collection is only required if some Zygote objects have been
         * deleted and keep some ref to the  the active Heap (floating garbage).
         * it is quite difficult to know without performing a full scan of
         * Zygote which would increase root time...
         *
         * For now, we will trigger major collection if the memory pressure
         * becomes too high, aka:
         * - last GC didn't free anything
         * - we are at max footprint while very low on free memory.
         */
        if (numBytesFreed == 0) {
            gDvm.gcHeap->forceMajorGC = true;
        }
        else {
            /* check if free space is below minFree */
            if ((currFootprint - currAllocated) < gDvm.heapMinFree) {
                /* have we reached max footprint yet ?*/
                if (currFootprint ==
                        dvmHeapSourceGetValue(HS_ALLOWED_FOOTPRINT, NULL, 0)){
                    gDvm.gcHeap->forceMajorGC = true;
                }
            }
        }
    }

    dvmMethodTraceGCEnd();
    LOGV_HEAP("GC finished");

    gcHeap->gcRunning = false;

    LOGV_HEAP("Resuming threads");

    if (isConcurrent == true) {
        /*
         * Wake-up any threads that blocked after a failed allocation
         * request.
         */
        dvmBroadcastCond(&gDvm.gcHeapCond);
    }

    if (isConcurrent == false) {
        dvmResumeAllThreads(SUSPEND_FOR_GC);
        ATRACE_END(); // Suspend A
        dirtyEnd = dvmGetRelativeTimeMsec();
        /*
         * Restore the original thread scheduling priority if it was
         * changed at the start of the current garbage collection.
         */
        if (oldThreadPriority != INT_MAX) {
            os_lowerThreadPriority(oldThreadPriority);
        }
    }

    /*
     * Move queue of pending references back into Java.
     */
    dvmEnqueueClearedReferences(&gDvm.gcHeap->clearedReferences);

    gcEnd = dvmGetRelativeTimeMsec();
    percentFree = 100 - (size_t)(100.0f * (float)currAllocated / currFootprint);
    if (!spec->isConcurrent) {
        u4 markSweepTime = dirtyEnd - rootStart;
        u4 gcTime = gcEnd - rootStart;
        bool isSmall = numBytesFreed > 0 && numBytesFreed < 1024;
        ALOGD("%s freed %s%zdK, %d%% free %zdK/%zdK, paused %ums, total %ums",
             spec->reason,
             isSmall ? "<" : "",
             numBytesFreed ? MAX(numBytesFreed / 1024, 1) : 0,
             percentFree,
             currAllocated / 1024, currFootprint / 1024,
             markSweepTime, gcTime);
    } else {
        u4 rootTime = rootEnd - rootStart;
        u4 dirtyTime = dirtyEnd - dirtyStart;
        u4 gcTime = gcEnd - rootStart;
        bool isSmall = numBytesFreed > 0 && numBytesFreed < 1024;
        ALOGD("%s freed %s%zdK, %d%% free %zdK/%zdK, paused %ums+%ums, total %ums",
             spec->reason,
             isSmall ? "<" : "",
             numBytesFreed ? MAX(numBytesFreed / 1024, 1) : 0,
             percentFree,
             currAllocated / 1024, currFootprint / 1024,
             rootTime, dirtyTime, gcTime);
    }
    if (gcHeap->ddmHpifWhen != 0) {
        LOGD_HEAP("Sending VM heap info to DDM");
        dvmDdmSendHeapInfo(gcHeap->ddmHpifWhen, false);
    }
    if (gcHeap->ddmHpsgWhen != 0) {
        LOGD_HEAP("Dumping VM heap to DDM");
        dvmDdmSendHeapSegments(false, false);
    }
    if (gcHeap->ddmNhsgWhen != 0) {
        LOGD_HEAP("Dumping native heap to DDM");
        dvmDdmSendHeapSegments(false, true);
    }

    ATRACE_END(); // Top-level GC
}

/*
 * If the concurrent GC is running, wait for it to finish.  The caller
 * must hold the heap lock.
 *
 * Note: the second dvmChangeStatus() could stall if we were in RUNNING
 * on entry, and some other thread has asked us to suspend.  In that
 * case we will be suspended with the heap lock held, which can lead to
 * deadlock if the other thread tries to do something with the managed heap.
 * For example, the debugger might suspend us and then execute a method that
 * allocates memory.  We can avoid this situation by releasing the lock
 * before self-suspending.  (The developer can work around this specific
 * situation by single-stepping the VM.  Alternatively, we could disable
 * concurrent GC when the debugger is attached, but that might change
 * behavior more than is desirable.)
 *
 * This should not be a problem in production, because any GC-related
 * activity will grab the lock before issuing a suspend-all.  (We may briefly
 * suspend when the GC thread calls dvmUnlockHeap before dvmResumeAllThreads,
 * but there's no risk of deadlock.)
 */
bool dvmWaitForConcurrentGcToComplete()
{
    ATRACE_BEGIN("GC: Wait For Concurrent");
    bool waited = gDvm.gcHeap->gcRunning;
    Thread *self = dvmThreadSelf();
    assert(self != NULL);
    u4 start = dvmGetRelativeTimeMsec();
    while (gDvm.gcHeap->gcRunning) {
        ThreadStatus oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);
        dvmWaitCond(&gDvm.gcHeapCond, &gDvm.gcHeapLock);
        dvmChangeStatus(self, oldStatus);
    }
    u4 end = dvmGetRelativeTimeMsec();
    if (end - start > 0) {
        ALOGD("WAIT_FOR_CONCURRENT_GC blocked %ums", end - start);
    }
    ATRACE_END();
    return waited;
}
