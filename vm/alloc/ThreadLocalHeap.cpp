/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0  (the "License");
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
#include "Dalvik.h"
#include "Thread.h"
#include "alloc/HeapInternal.h"
#include "alloc/HeapSource.h"
#include "alloc/HeapBitmap.h"
#include "alloc/HeapBitmapInlines.h"
#include "alloc/ThreadLocalHeap.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <malloc.h>

/*
 *  1) Overview.
 *
 * The purpose of thread local allocation is to localize small objects in the
 * cache in order to avoid false sharing issue in multi-threaded applications.
 * TLA also helps in decreasing the lock collision frequency as small objects
 * are allocated from thread local pools of memory without competing for the
 * global heap lock.
 *
 * The Thread Local Allocation is a fixed chunk size allocation within
 * preallocated blocks of global memory (obtained from the HeapSource).
 *
 * Each Thread is being assigned a Thread Local Heap (TLH) which contains a
 * pointers to the current allocating blocks and a Pool of blocks.
 *
 * The blocks are stored in a pool in specific linked lists:
 * - the free list contains the blocks which do not have any chunks in use
 * - the full list contains the blocks which do have all the chunks in use
 * - Other blocks goes to the partial lists
 *
 * Used blocks are assigned a size index (SID) which indicates the chunk size
 * for the block. when a block is free, it's chunk size is invalid and it can
 * be reused for any size.
 *
 * When a Thread exits, All the blocks from the thread's local heap are freed
 * if empty or moved to a global pool if in use.
 *
 * The GC will collect free blocks and released them. At the end of the GC,
 * partially free blocks are recycled in the global pool and can be reused.
 *
 *  2) Chunks
 *
 *  chunk->  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *           |       TLB>>2            +  01b  +
 *     mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ <-- aligned on 8 or 16 bytes
 *           |                                 |
 *           +-  size - sizeof(size_t) -       +
 *           :   available payload bytes       :
 *           +-                                +
 *           |                                 |
 *           +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * The chunk header contains :
 *
 * - 2 bits for INUSE marker (=0x1) which behaves opposite to DLMAlloc. this
 * allows quick check to discriminate Local vs DLMAlloc'ed global pointer.
 *
 * - high bits are the significant bit of the TLB pointer. TLB pointer must be
 * aligned  on at least 8 byte.
 *
 * Right after the chunk header comes the data if chunk is in used, or the link
 * to the next free chunk.
 *
 *
 *  3) Thread Local Blocks (TLB)
 *
 *  Thread local blocks are blocks of memory allocated from the global heap
 *  from which the local chunks will be allocated. The tlb header contains
 *  links to the local heap it belongs to, as well as the free chunk and
 *  allocation counters.
 *
 *
 *  tlb->    +-+-+-+-+-+-+-+-+-+-+-+  <-- aligned on 8 bytes
 *           |                     |
 *           |     tlb head        |
 *           |                     |
 *           +-+-+-+-+-+-+-+-+-+-+-+
 *           :                     :
 *   start-> +-+-+-+-+-+-+-+-+-+-+-+
 *           |     chunk[N]        |
 *           +-+-+-+-+-+-+-+-+-+-+-+
 *           :                     :
 *           +-+-+-+-+-+-+-+-+-+-+-+
 *           |     chunk[N-1]      |
 *           +-+-+-+-+-+-+-+-+-+-+-+
 *           :                     :
 *           :                     :
 *           +-+-+-+-+-+-+-+-+-+-+-+
 *           |     chunk[0]        |
 *           +-+-+-+-+-+-+-+-+-+-+-+
 *           :                     :
 *     end-> +-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 *
 *  4) Thread Local Heap, local and global pools
 *
 *  Each Thread can be associated a Thread Local Heap (TLH). The local heap
 *  object has a set of pointers to the current allocating blocks and a Pool
 *  objects which contains the list of used & free tlbs.
 *
 *  A global pool is used to collect orphan TLBs which do not belong to a
 *  specific
 *
 *  5) Allocation mechanism.
 *
 *  dvmTLHeapAlloc tries to allocate a chunk from the current allocating
 *  block if it exists, or take a block from a pool in this priority:
 *     - the partial local pool
 *     - the free local pool
 *     - the partial global pool
 *     - the free global pool
 *
 *  If not successful, responsibility to the caller to add new mem blocks to
 *  the local heap through dvmTLHeapAddMem and then retry dvmHeapAlloc.
 *
 *  6) Sweep mechanism.
 *
 *  The GC gathers the array of pointers to objects which needs to be sweeped.
 *  Before freeing the list, it calls dvmTLHeapSourceFreeList to remove
 *  all local objects from the list. the unused chunks go back to their
 *  respective blocks. if a block is empty and can be freed it's pointer is
 *  added to "to be freed" array. The GC can then safely free the list which
 *  contains only objects and blocks allocated from the global list.
 *
 *  Once the sweep is done, GC calls dvmTLHeapSourceReleaseFree to do a final
 *  cleanup of all the pools.
 *
 *
 *  7) Thread destruction.
 *
 *  At thread destruction, dvmTLHeapDetach will  release the threads resources.
 *  "in use" blocks (partial or full) are moved to the global pool. free blocks
 *  are deleted.
 *
 *
 *  8) Note regarding synchronization and lock hierarchy.
 *
 *  The synchronization mechanism are a little bit complex here as we want to
 *  optimize the allocation fast path. We use 3 different heap mutexes which
 *  must always be locked in following order to avoid dead locks:
 *
 *  1- Heap lock: protects concurrent the access to the global heap.
 *
 *  2- global pool lock: protects concurrent access to the global pool AND
 *     synchronize updates of the tlb->tlh field when moving a blocks to or
 *     from the global pool.
 *
 *  3- local pool lock: protects concurrent access to the local pool
 *
 *
 *  9) Low level implementation Notes
 *
 *  The free pool only need push and pop operations and is implemented with a
 *  singly linked list.
 *
 *  The partial and full pools only need push, pop and removal, so we use an
 *  doubly linked implementation. we skip the reset of head->prev and items out
 *  of the list to gain a little bit performance.
 *
 *  The spin locking strategy is optimized for the allocation path and may need
 *  some rework if used in the GC call tree.
 *
 *  TODO : It may be worth to optimize the recycling loops which move a full
 *  local pool to the global pool.
 *
 */

#if (!defined(MALLOC_ALIGNMENT)||(MALLOC_ALIGNMENT>=16))
/* align 16 */
#define TLALIGN             16
#define TLSHIFT             4
#else
/* align 8 */
#define TLALIGN             8
#define TLSHIFT             3
#endif

#define TLOVERHEAD        sizeof(uintptr_t)
#define TLSIDOFFSET       (((TLALLOC_MIN_SIZE)+TLOVERHEAD+TLALIGN-1)>>TLSHIFT)
#define TLCHUNCKSIZE(sid) (((sid)+TLSIDOFFSET)<<TLSHIFT)
#define TLSIZEID(s)       ((((s)+TLOVERHEAD+TLALIGN-1)>>TLSHIFT)-TLSIDOFFSET)
#define TLSIZEID_NUM      (TLSIZEID(TLALLOC_MAX_SIZE)+1)
#define TLMARKER          0x1
#define TLALIGNUP(p,a)    (char*)(((uintptr_t)(p)+(a)-1UL)&~((a)-1UL))
#define TLINLINE          static inline __attribute__ ( (always_inline))

/** free chunk structure */
struct TLChunk
{
    uintptr_t       head; /**< free chunk structure  */
    struct TLChunk* next; /**< next in the free list */
};

/** block of chunks */
struct TLBlock
{
    size_t          size;    /**< size of the block */
    char*           start;   /**< start of the chunk area */
    char*           end;     /**< end of the chunk area */
    struct TLBlock* next;    /**< next block when chained */
    struct TLBlock* prev;    /**< prev block when chained */
    struct TLHeap*  tlh;     /**< pointer to the thread local heap */
    size_t          sid;     /**< block size ID */
    size_t          num;     /**< number of chunks allocated from this block */
    struct TLChunk* free;    /**< singly linked list of free chunks */
};

/** pool of blocks */
struct TLPool
{
    pthread_mutex_t  lock;          /**< Synchronization mutex */
    TLBlock* partial[TLSIZEID_NUM]; /**< list of partial tlbs (doubly linked) */
    TLBlock* full[TLSIZEID_NUM];    /**< list of full tlbs  (doubly linked) */
    TLBlock* free;                  /**< list of free tlbs (singly linked) */
};

/** Thread Local Heap */
struct TLHeap
{
    TLBlock* tlb[TLSIZEID_NUM]; /**< current allocating block */
    TLPool   pool;              /**< local pool */
};

/** Thread Local Heap Source */
struct TLHeapSource
{
    bool    shutdown;           /**< global shutdown flag */
    bool    blockAllocEnabled;  /**< allow block allocation */
    size_t  blockAllocSize;     /**< block size */
    TLPool  pool;               /**< global block pool */
};

static TLBlock* initTLB(void* mem, size_t size);
static void resetTLB(TLBlock* tlb, size_t sid);


/* doubly linked list push. */
TLINLINE  void listPush(TLBlock** list, TLBlock* tlb) {
    if (LIKELY(*list != NULL)) {
        (*list)->prev = tlb;
    }
    tlb->next = *list;
    *list = tlb;
}

/* doubly linked list pop */
TLINLINE  TLBlock* listPop(TLBlock** list) {
    TLBlock* tlb = *list;
    if (LIKELY(tlb != NULL)) {
        *list = tlb->next;
    }
    return tlb;
}

/* checks  linked list has an item */
TLINLINE  bool listHas(TLBlock** list, TLBlock* tlb)
{
    for( TLBlock* iter = *list; iter != NULL; ){
        if (iter == tlb) return true;
        iter = iter->next;
    }
    return false;
}

/* doubly linked list remove */
TLINLINE  void listRemove(TLBlock** list, TLBlock* tlb)
{
    assert(listHas(list,tlb));

    if (UNLIKELY(*list == tlb)) {
        *list = tlb->next;
    } else {
        tlb->prev->next = tlb->next;
        if (tlb->next != NULL) {
            tlb->next->prev = tlb->prev;
        }
    }
}

/* mutext init */
static void initLock(pthread_mutex_t* lock)
{
#ifdef TLDBGMUTEX
    pthread_mutexattr_t attr;
    int cc;

    pthread_mutexattr_init(&attr);
    cc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
    assert(cc == 0);
    pthread_mutex_init(lock, &attr);
    pthread_mutexattr_destroy(&attr);
#else
    pthread_mutex_init(lock, NULL);       // default=PTHREAD_MUTEX_FAST_NP
#endif
}

#if ANDROID_SMP != 0
/* mutext lock slow path */
static void spinLock (pthread_mutex_t* lock)
{
    Thread *self = dvmThreadSelf();
    assert (self != NULL);

    ThreadStatus oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);

    const int kHeapLockSpinTime = 100;
    u8 spinUntil  = dvmGetRelativeTimeUsec() + kHeapLockSpinTime;

    while(pthread_mutex_trylock(lock)!=0) {

#ifdef ARCH_IA32
        /* pause cpu to reduce spinning overhead */
        __asm__ ( "pause;" );
#endif
        /* wait on suspend if required */
        dvmCheckSuspendPending(self);

        /* stop spinning if spincount elapsed */
        if (dvmGetRelativeTimeUsec() > spinUntil){
            /* spin counter has elapsed, fall-back on lock*/
            pthread_mutex_lock(lock);
            break;
        }
    }

    dvmChangeStatus(self, oldStatus);
}
#endif

/* pool try lock */
TLINLINE  int poolTryLock (TLPool* pool)
{
   assert(pool != NULL);
   return pthread_mutex_trylock(&pool->lock);
}

/* pool lock */
TLINLINE  void poolLock (TLPool* pool)
{
   assert(pool != NULL);
   if (UNLIKELY(pthread_mutex_trylock(&pool->lock)!=0)) {
       Thread *self;
       ThreadStatus oldStatus;

       self = dvmThreadSelf();
       oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);
       pthread_mutex_lock(&pool->lock);
       dvmChangeStatus(self, oldStatus);
   }
}

TLINLINE  void poolSpinAndLock (TLPool* pool)
{
#if ANDROID_SMP != 0
   assert(pool != NULL);
   if (UNLIKELY(pthread_mutex_trylock(&pool->lock)!=0)) {
       spinLock(&pool->lock);
   }
#else
   /*if not SMP, better just lock */
   poolLock (pool);
#endif
}


/* pool unlock */
TLINLINE  void poolUnlock (TLPool* pool)
{
    assert(pool != NULL);
    pthread_mutex_unlock(&pool->lock);
}

/* push a free block in the pool */
TLINLINE  void poolPushFree (TLPool* pool,TLBlock* tlb)
{
    assert(pool != NULL);
    assert(tlb != NULL);
    tlb->next = pool->free;
    pool->free = tlb;
}

/* pop a free block from the pool */
TLINLINE  TLBlock* poolPopFree (TLPool* pool)
{
    assert(pool != NULL);
    TLBlock* tlb = pool->free;
    if (LIKELY(tlb != NULL))     {
        pool->free = tlb->next;
    }
    return tlb;
}

/* push a full block in the pool */
TLINLINE  void poolPushFull(TLPool* pool, size_t sid, TLBlock* tlb)
{
    assert(pool != NULL);
    assert(sid < TLSIZEID_NUM);
    assert(tlb != NULL);
    listPush(&pool->full[sid],tlb);
}

/* pop a full block from the pool */
TLINLINE  TLBlock* poolPopFull (TLPool* pool, size_t sid)
{
    assert(pool != NULL);
    assert(sid < TLSIZEID_NUM);
    return listPop(&pool->full[sid]);
}

/* remove a specific full block from the pool */
TLINLINE  void poolRemoveFull (TLPool* pool, size_t sid, TLBlock* tlb)
{
    assert(pool != NULL);
    assert(sid < TLSIZEID_NUM);
    assert(tlb != NULL);
    listRemove(&pool->full[sid],tlb);
}

/* push a partial block in the pool */
TLINLINE  void poolPushPartial(TLPool* pool, size_t sid, TLBlock* tlb)
{
    assert(pool != NULL);
    assert(sid < TLSIZEID_NUM);
    assert(tlb != NULL);
    listPush(&pool->partial[sid],tlb);
}

/* pop a partial block from the pool */
TLINLINE  TLBlock* poolPopPartial (TLPool* pool, size_t sid)
{
    assert(pool != NULL);
    assert(sid < TLSIZEID_NUM);
    return listPop(&pool->partial[sid]);
}

/* remove a specific partial block from the pool */
TLINLINE  void poolRemovePartial (TLPool* pool, size_t sid, TLBlock* tlb)
{
    assert(pool != NULL);
    assert(sid < TLSIZEID_NUM);
    assert(tlb!=NULL);
    listRemove(&pool->partial[sid],tlb);
}

/**
 * @brief   Allocates and clear an object from the local Heap.
 *static void resetTBL(TLBlock* tlb, size_t sid);
 *
 * @param   tlh         pointer to the local heap.
 * @param   size        requested allocation size
 * @return              pointer to allocated block or NULL
 *
 */
void* dvmTLHeapAlloc(TLHeap* tlh, size_t size)
{
    assert (tlh != NULL);
    TLPool*  localPool = &tlh->pool;
    size_t sid = TLSIZEID(size);
    assert(sid < TLSIZEID_NUM);
    void* ptr = NULL;

    /* aggressively lock the local pool */
    poolSpinAndLock(localPool);

    /* retrieve allocating TLB. */
    TLBlock* tlb = tlh->tlb[sid];

    /* if we do not have one, try to get one from the local pool */
    if (UNLIKELY(tlb == NULL)) {
        /* try from partial list first */
        tlb = poolPopPartial(localPool,sid);

        assert ((tlb == NULL) || (tlb->free != NULL)); /*DEBUG*/

        if (tlb == NULL) {
            /* then try from free list*/
            tlb = poolPopFree(localPool);

            if ((tlb != NULL) && (tlb->sid != sid))  {
                resetTLB(tlb,sid);
            }

            assert ((tlb==NULL) || (tlb->free != NULL)); /*DEBUG*/
        }

        /* no tlb in the local pool, try to get from the global pool */
        if (UNLIKELY(tlb == NULL)) {
            TLPool*  globalPool = &gDvm.gcHeap->tlhSource->pool;

            if (globalPool->partial[sid] != NULL) {
                /* try from partial list first */
                if (poolTryLock(globalPool) != 0)
                {
                    /* reverse lock order to respect lock hierarchy */
                    poolUnlock(localPool);
                    poolSpinAndLock(globalPool);
                    poolSpinAndLock(localPool);
                }
                tlb = poolPopPartial(globalPool,sid);
                if (tlb != NULL) {
                    tlb->tlh = tlh;
                    assert (tlb->free != NULL); /*DEBUG*/
                }
                poolUnlock(globalPool);
            }
            else if (globalPool->free != NULL) {
                /* then try from free list*/
                if (poolTryLock(globalPool) != 0)
                {
                    /* reverse lock order to respect lock hierarchy */
                    poolUnlock(localPool);
                    poolSpinAndLock(globalPool);
                    poolSpinAndLock(localPool);
                }
                tlb = poolPopFree(globalPool);
                if (tlb != NULL) {
                   tlb->tlh = tlh;
                   if (tlb->sid != sid){
                        resetTLB(tlb,sid);
                   }

                   assert(tlb->free != NULL); /*DEBUG*/
                }
                poolUnlock(globalPool);
            }
        }
    }

    if (LIKELY(tlb != NULL)) {
        /* We have a valid TLB, we can now allocate
         * from free chunks. tlb->free should not be NULL here
         * as it would have already been put in the full list */
        assert (tlb->free != NULL);
        TLChunk* chunk = tlb->free;
        tlb->free = chunk->next;
        tlb->num++;
        ptr = (void*) ((uintptr_t*)chunk+1);
        memset(ptr,0,size);

        if (LIKELY(tlb->free != NULL)) {
            /* this tlb can do further alloc, let's keep it as allcating tlb */
            tlh->tlb[sid] = tlb;
        }
        else {
            /* Move full tlb to the full pool */
            tlh->tlb[sid] = NULL;
            poolPushFull(localPool,sid,tlb);
        }
    }

    poolUnlock(localPool);

    assert((((uintptr_t)ptr) & (TLALIGN-1)) == 0 );
    return ptr;
}

/* must be called on free blocks to reset chunk list according to sid */
static void resetTLB(TLBlock* tlb, size_t sid)
{
    assert (tlb != NULL);
    assert (sid < TLSIZEID_NUM);

    size_t chunkSize = TLCHUNCKSIZE(sid);
    size_t chunkNum  = (tlb->end - tlb->start) / chunkSize;
    assert (chunkNum > 2);

    unsigned head  = (((uintptr_t)tlb)&~0x3) | TLMARKER;
    TLChunk* chunk = (TLChunk*)(tlb->start + chunkSize*(chunkNum-1));

    tlb->free = chunk ;
    tlb->num  = 0;
    tlb->sid = sid;

    /* Arrange the list so that first chunk allocated is the top one,
     * so that hb->max get's updated less often. */
    for (size_t i = 1; i < chunkNum ; i++) {
        TLChunk* next = (TLChunk*) ((char*)chunk - chunkSize);
        chunk->head = head;
        chunk->next = next;
        chunk = next;
    }

    assert ((uintptr_t)chunk >= (uintptr_t)tlb->start);
    chunk->head = head;
    chunk->next = NULL;
}

/**
 * @brief Get the Block Size
 *
 * @return the block size or 0 to bypass block allocation
 */
size_t dvmTLHeapGetBlockSizeForAlloc(TLHeap* tlh, size_t size)
{
    /* TODO: as of now tlh and size are not used.
     * could be used in the future to tune block size based on
     * allocation stats. Although changing the block size very
     * often will increase fragmentation.
     */
    (void)tlh; (void)size;

    TLHeapSource* hs = gDvm.gcHeap->tlhSource;

    /* TODO: as of now blockAllocEnabled is never false, but could be
     * used to force bypassing BlockAlloc. */
    if (hs->blockAllocEnabled == true) {
        return hs->blockAllocSize;
    }
    return 0;
}

/**
 * @brief Get the number of blocks or 0 to bypass pre-allocation
 *
 * @return the number of blocks or 0 if
 */
size_t dvmTLHeapGetBlockNumForAlloc(TLHeap* tlh, size_t size)
{
    /* TODO: as of now tlh and size are not used.
     * could be used in the future to tune pre-allocation based on
     * allocation stats. As of now we just use TLPREALLOC_THRESHOLD
     * as a memory pressure safeguard.
     */
    (void)tlh; (void)size;

    if (dvmHeapSourceGetAvailableFree() > TLPREALLOC_THRESHOLD) {
        if (gDvm.lowMemoryMode == false) {
            return TLPREALLOC_NUM;
        }
        else {
            return TLPREALLOC_LMNUM;
        }
    }
    else
    {
        /* Memory pressure too high : bypass prealloc.*/
        return 0;
    }
}

/**
 * @brief   Allocate from new Memory blocks.
 *
 * @param   tlh         pointer to the local heap.
 * @param   size        requested allocation size
 * @param   numBlocks   the number of memory block to add
 * @param   blockPtrs   array of memory block to add
 * @param   blockSize   memory block size.
 *
 */
void* dvmTLHeapAllocFromNewBlocks( TLHeap* tlh, size_t size, size_t numBlocks,
                               void** blockPtrs, size_t blockSize)
{
    assert (tlh != NULL);
    assert (numBlocks > 0);
    size_t sid = TLSIZEID(size);
    assert(sid < TLSIZEID_NUM);

    TLPool*  localPool = &tlh->pool;
    TLPool*  globalPool = &gDvm.gcHeap->tlhSource->pool;


    size_t i = 1;

    /* store some blocks in global pool if suitable, aka if we have enough num
     * blocks preallocated and if we have fast access to the global lock. if
     * global lock is already lock, we just skip and put all the blocks in
     * local pool. too not waiste too much time.
     */
    if ( (numBlocks > 1) &&  (poolTryLock(globalPool) == 0)) {
        for ( ; (i < TLPREALLOC_GLOBAL) && (i < numBlocks) ; i++){
            TLBlock* tlb = initTLB(blockPtrs[i],blockSize);
            tlb->tlh = NULL;
            poolPushFree(globalPool,tlb);
        }
        poolUnlock(globalPool);
    }

    poolSpinAndLock(localPool);

    /* store remaining blocks in local pool */
    for ( ; i < numBlocks; i++){
        TLBlock* tlb = initTLB(blockPtrs[i],blockSize);
        tlb->tlh = tlh;
        poolPushFree(localPool,tlb);
    }

    /* and allocate from first */
    TLBlock* tlb = initTLB(blockPtrs[0],blockSize);
    tlb->tlh = tlh;
    resetTLB(tlb,sid);
    TLChunk* chunk = tlb->free;
    assert (chunk != NULL);
    tlb->free = chunk->next;
    tlb->num++;
    void* ptr = (void*) ((uintptr_t*)chunk+1);
    memset(ptr,0,size);
    assert(tlh->tlb[sid] == NULL);
    tlh->tlb[sid] = tlb;

    poolUnlock(localPool);

    assert((((uintptr_t)ptr) & (TLALIGN-1)) == 0 );
    return ptr;
}

/* Initialize a freshly allocated block */
static TLBlock* initTLB(void* mem, size_t size)
{
    assert (mem != NULL);
    assert (((uintptr_t)mem & 0x3) == 0);
    assert (size>sizeof(TLBlock));

    TLBlock* tlb = (TLBlock*) mem;

    memset(mem,0,sizeof(TLBlock));

    tlb->size = size;
    tlb->sid = TLSIZEID_NUM;
    tlb->end = (char*)mem + size;

    tlb->start= TLALIGNUP((char*)mem+sizeof(TLBlock)+sizeof(uintptr_t),TLALIGN)
            - sizeof(uintptr_t);

    return tlb;
}


/**
 * @brief   return used size if allocated from local heap.
 *
 * @param   ptr     local or global pointer
 * @return          chunk size if local, or 0
 *
 */
size_t dvmTLHeapSourceChunkSize(void* ptr)
{
    assert (ptr != NULL);
    TLChunk* chunk = (TLChunk*) ((uintptr_t*)ptr-1);
    assert (chunk != NULL);

    if ((chunk->head & 0x3) == TLMARKER) {
        TLBlock* tlb = (TLBlock*)(chunk->head & ~0x3);

        assert( tlb != NULL);
        assert((char*)chunk >= tlb->start);
        assert((char*)chunk < tlb->end);

        return TLCHUNCKSIZE(tlb->sid);
    }
    return 0;
}

/**
 * @brief   Create a new local heap and attach it to the thread.
 *
 * @param   self        pointer to the thread
 *
 */
void dvmTLHeapAtttach(Thread* self)
{
    TLHeap* tlh = (TLHeap*)self->tlh;
    TLHeapSource* hs = gDvm.gcHeap->tlhSource;

    /* if TLA enabled, try allocating the local heap */
    if ((hs != NULL) && (tlh == NULL)) {
        size_t allocSize =
                ((sizeof(TLHeap)+TLCACHE_ALIGN-1)/TLCACHE_ALIGN)*TLCACHE_ALIGN;

        /* alone and aligned on cache line */
        tlh = (TLHeap*) memalign(TLCACHE_ALIGN,allocSize);

        if (tlh != NULL) {
            /* Initialize and attach the heap...*/
            memset(tlh,0,sizeof(TLHeap));
            initLock(&tlh->pool.lock);
            self->tlh = tlh;
        }
        else {
            /* This is bad, no more system memory...*/
            LOGE_HEAP("memalign failed; aborting...");
            dvmAbort();
        }
    }
}

/**
 * @brief   Detach and free all resources allocated from the local heap.
 *
 * @param   self    pointer to the thread
 *
 * Called when a thread is detached from the VM.
 *
 * 1- move all used (full or partial) blocks to the global pool
 * 2- delete all free blocks
 */
void dvmTLHeapDetach(Thread* self)
{
    TLHeap* tlh = (TLHeap*)self->tlh;
    TLHeapSource* hs = gDvm.gcHeap->tlhSource;

    if ((hs == NULL) || (tlh == NULL)) {
        return;
    }

    /* prevent suspend for GC checks */
    ThreadStatus oldStatus = dvmChangeStatus(self, THREAD_VMWAIT);

    TLPool* localPool = &tlh->pool;
    TLPool* globalPool = &hs->pool;

    dvmLockHeap();        /* for dvmHeapSourceFree */
    poolLock(globalPool); /* for global pool AND tlb->tlh = NULL */
    poolLock(localPool);  /* for local pool */

    for (size_t sid = 0 ; sid < TLSIZEID_NUM; sid ++) {
        /* recycle current tlb */
        TLBlock* tlb = tlh->tlb[sid];
        if (tlb != NULL) {
            tlh->tlb[sid] = NULL;
            tlb->tlh = NULL;
            assert(tlb->free != NULL);
            poolPushPartial(globalPool,sid,tlb);
        }

        /* recycle partial tlbs */
        for (   tlb = poolPopPartial(localPool,sid);
                tlb != NULL ;
                tlb = poolPopPartial(localPool,sid) ) {
            tlb->tlh = NULL;
            assert(tlb->free != NULL);
            poolPushPartial(globalPool,sid,tlb);
        }

        /* recycle full tlbs */
        for (   tlb = poolPopFull(localPool,sid);
                tlb != NULL ;
                tlb = poolPopFull(localPool,sid) ) {
            tlb->tlh = NULL;
            assert(tlb->free == NULL);
            poolPushFull(globalPool,sid,tlb);
        }

    }

    /* delete free tlbs */
    for (   TLBlock* tlb = poolPopFree(localPool);
            tlb != NULL ;
            tlb = poolPopFree(localPool) ) {
        tlb->tlh = NULL;
        dvmHeapSourceFree(tlb);
    }

    poolUnlock(localPool);
    poolUnlock(globalPool);
    dvmUnlockHeap();

    self->tlh = NULL;
    free(tlh);

    /* restore status */
    dvmChangeStatus(self, oldStatus );

}


/**
 * @brief   Filter the GC sweep free pointer array from thread local chunks
 *          and populate it with free blocks.
 *
 * @param   numPtrs         size of the array
 * @param   ptrs            the pointer array.
 * @param   isConcurrent    true if GC is concurrent.
 * @return                  new updated numPtrs.
 *
 *
 * warning: If GC is not concurrent, allocating thread will be suspended and we
 *          do not need to lock the pools.
 *
 * note:    If GC is concurrent, it is safe to call this function without
 *          Heap lock being held.
 */
size_t dvmTLHeapSourceFreeList(size_t numPtrs, void **ptrs, bool isConcurrent)
{
    TLHeapSource* hs = gDvm.gcHeap->tlhSource;

    if (hs == NULL) {
        /* skip as TLA not activated */
        return numPtrs;
    }

    size_t count = 0;
    TLPool* globalPool =&hs->pool;
    TLPool* lockedPool = NULL;

    if (isConcurrent == true){
        poolLock(globalPool);
    }

    for ( size_t i = 0; i < numPtrs; i++) {

        void* ptr = ptrs[i];
        assert (ptr != NULL);

        TLChunk* chunk = (TLChunk*) ((uintptr_t*)ptr-1);
        assert (chunk != NULL);

        if ( (chunk->head & 0x3) == TLMARKER) {

            TLHeap* tlh; TLPool* pool; TLBlock* tlb; size_t sid;

            /* Retrieve TLB */
            tlb = (TLBlock*)(chunk->head &~0x3);
            assert (tlb != NULL);
            assert (tlb->end == (char*)tlb + tlb->size);
            assert((((uintptr_t)ptr) & (TLALIGN-1)) == 0 );
            assert ((char*)ptr > tlb->start);
            assert ((char*)ptr < tlb->end);
            tlh = tlb->tlh ;

            /* Retrieve and lock the pool */
            if (tlh == NULL) {
                pool = globalPool;
                if (isConcurrent  && (lockedPool != NULL)) {
                    poolUnlock(lockedPool);
                    lockedPool = NULL;
                }
            }
            else {
                pool =  &tlh->pool;
                if (isConcurrent  && (pool != lockedPool )){
                    if (lockedPool != NULL) poolUnlock(lockedPool);
                    poolLock(pool);
                    lockedPool = pool;
                }
            }

            /* Sweep the chunk */
            sid =  tlb->sid;
            assert(tlb->num > 0);
            assert(sid < TLSIZEID_NUM);
            chunk->next = tlb->free;
            tlb->free = chunk;
            tlb->num--;

            /*
             * Handle previously Full TLBs
             */
            if (chunk->next == NULL) {
                if ((tlh == NULL) || (tlb != tlh->tlb[sid])){
                    /* freeing from a full pool, move to partial */
                    poolRemoveFull(pool,sid,tlb);
                    assert(tlb->free != NULL);
                    poolPushPartial(pool,sid,tlb);
                }
            }

            /*
             * Handle empty TLBs
             */
            if (tlb->num == 0) {
                /* tlb can now be recycled, add it to the to be freed list */
                ptrs[count++] = (void*)tlb;

                if ((tlh != NULL) && (tlb == tlh->tlb[sid])) {
                    /* this was the current allocating tlb */
                    if (isConcurrent){
                        /* parallel alloc benefits if we keep a tlb */
                        tlh->tlb[sid] = poolPopPartial(pool,sid);
                    }
                    else {
                        tlh->tlb[sid] = NULL;
                    }
                }
                else {
                    poolRemovePartial(pool,sid,tlb);
                }
            }

        }
        else {
            /* global pointer stays in the list */
            ptrs[count++] = ptrs[i];
        }
    }

    /* unlock locked pools */
    if (isConcurrent){
        if (lockedPool != NULL) {
            poolUnlock(lockedPool );
        }
        poolUnlock(globalPool);
    }

    return count;
}


/**
 * @brief   Post GC-Sweep local heaps clean up.
 *
 * @param   isConcurrent    true if GC is concurrent.
 *
 * Needs to be called at the end of the sweep phase to delete
 * extra free blocks
 *
 * warning: Must be called from the GC at the end of the sweep while heap
 *          lock is being held
 *
 * warning: If GC is not concurrent, allocating thread will be suspended and
 *          we do not need to lock the pools.
 */
void dvmTLHeapSourceReleaseFree(bool isConcurrent)
{
    TLHeapSource* hs = gDvm.gcHeap->tlhSource;

    if (hs == NULL) {
        /* skip as TLA not activated */
        return;
    }

    dvmLockThreadList(NULL);

    TLPool* globalPool =&hs->pool;
    if (isConcurrent){
        poolLock(globalPool);
    }

    /* Visit threads and recycle blocks */
    for (Thread* thread=gDvm.threadList; thread != NULL; thread=thread->next) {
        if (thread->tlh != NULL) {
            TLPool* localPool = &thread->tlh->pool;
            if (isConcurrent == true) {
                poolLock(localPool);
            }

            for (size_t sid = 0 ; sid < TLSIZEID_NUM; sid ++) {
                /* recycle partial tlbs */
                for (   TLBlock* tlb = poolPopPartial(localPool,sid);
                        tlb != NULL ;
                        tlb = poolPopPartial(localPool,sid) ) {
                    tlb->tlh = NULL;
                    assert (tlb->free != NULL);
                    poolPushPartial(globalPool,sid,tlb);
                }
            }

            /* recycle free tlbs */
            for (   TLBlock* tlb = poolPopFree(localPool);
                    tlb != NULL ;
                    tlb = poolPopFree(localPool) ) {
                dvmHeapSourceFree(tlb);
            }

            if (isConcurrent == true) {
                poolUnlock(localPool);
            }
        }
    }

    dvmUnlockThreadList();

    /* Visit Orphan pool and free unused blocks */
    for (   TLBlock* tlb = poolPopFree(globalPool);
            tlb != NULL ;
            tlb = poolPopFree(globalPool) ) {
        dvmHeapSourceFree(tlb);
    }

    if (isConcurrent == true) {
        poolUnlock(globalPool);
    }
}

/**
 * @brief   Post Zygote Initialization.
 */
bool dvmTLHeapSourceStartupAfterZygote()
{
    if (gDvm.withTLA == false) {
        return true;
    }

    // allocate alone on cache line
    size_t allocSize =
            ((sizeof(TLHeapSource)+TLCACHE_ALIGN-1)
                    /TLCACHE_ALIGN)*TLCACHE_ALIGN;

    struct TLHeapSource* hs =
            (TLHeapSource*)memalign(TLCACHE_ALIGN,allocSize);

    if (hs != NULL) {

        memset (hs,0,sizeof(TLHeapSource));
        initLock(&hs->pool.lock);

        if (gDvm.lowMemoryMode == true){
            hs->blockAllocSize = TLBLOCK_LMSIZE - HEAP_SOURCE_CHUNK_OVERHEAD;
        }
        else {
            hs->blockAllocSize = TLBLOCK_SIZE - HEAP_SOURCE_CHUNK_OVERHEAD;
        }

        hs->blockAllocEnabled = true;
    }

    gDvm.gcHeap->tlhSource = hs;
    return (hs != NULL);

}

/**
 * @brief   TLHeapSource clean up on Shutdown.
 *
 * TODO: check if need to free remaining ...
 */
void  dvmTLHeapSourceShutdown()
{
    TLHeapSource* hs = gDvm.gcHeap->tlhSource;

    if (hs != NULL)
    {
        hs->shutdown = true;
    }
}
