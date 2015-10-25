/*
 * Copyright  (C) 2013 Intel Corporation
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
#ifndef DALVIK_ALLOC_THREADLOCALHEAP_H_
#define DALVIK_ALLOC_THREADLOCALHEAP_H_

#define TLALLOC_MIN_SIZE    12      /* MIN local allocable object size      */
#define TLALLOC_MAX_SIZE    60      /* MAX local allocable object size      */
#define TLCACHE_ALIGN       64      /* cache line size                      */
#define TLBLOCK_SIZE        0x1000  /* default block size                   */
#define TLPREALLOC_NUM      8       /* default preallocation counter        */
#define TLBLOCK_LMSIZE      0x400   /* low mem block size                   */
#define TLPREALLOC_LMNUM    4       /* low mem preallocation counter        */
#define TLPREALLOC_GLOBAL   4       /* number of global preallocated blocks */
#define TLPREALLOC_THRESHOLD (128 << 10) /* Free Space pre-alloc threshold  */


struct TLHeapSource;
struct TLHeap;

/**
 * @brief   Post Zygote Initialization.
 */
bool dvmTLHeapSourceStartupAfterZygote(void);

/**
 * @brief   TLHeapSource clean up on Shutdown.
 */
void  dvmTLHeapSourceShutdown(void);


/**
 * @brief   return used size if allocated from local heap.
 *
 * @param   ptr     local or global pointer
 * @return          chunk size if local, or 0
 *
 */
size_t  dvmTLHeapSourceChunkSize(void* ptr);

/**
 * @brief   Filter the GC sweep free pointer array and populate from thread
 *          local chunks and populate it with free private buffers.
 *
 * @param   numPtrs         size of the array
 * @param   ptrs            the pointer array.
 * @param   isConcurrent    true if GC is concurrent.
 * @return                  new updated numPtrs.
 *
 */
size_t dvmTLHeapSourceFreeList(size_t numPtrs, void **ptrs, bool isConcurrent);

/**
 * @brief   Post GC-Sweep local heaps clean up.
 *
 * @param   isConcurrent    true if GC is concurrent.
 *
 */
void dvmTLHeapSourceReleaseFree(bool isConcurrent);

/**
 * @brief Get the Block Size
 *
 * @return the block size or 0 to bypass block allocation
 */
size_t dvmTLHeapGetBlockSizeForAlloc(TLHeap* tlh, size_t size);

/**
 * @brief Get the number of blocks or 0 to bypass pre-allocation
 *
 * @return the number of blocks or 0 if
 */
size_t dvmTLHeapGetBlockNumForAlloc(TLHeap* tlh, size_t size);

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
                                   void** blockPtrs, size_t blockSize);

/**
 * @brief   Allocates and clear an object from the local Heap.
 *
 * @param   tlh         pointer to the local heap.
 * @param   size        requested allocation size
 * @return              pointer to allocated block or NULL
 *
 */
void* dvmTLHeapAlloc(TLHeap* tlh, size_t size);

/**
 * @brief   Create a new local heap and attach it to the thread.
 *
 * @param   self        pointer to the thread
 *
 */
void dvmTLHeapAtttach(Thread* self);

/**
 * @brief   Detach and free all resources allocated from the local heap.
 *
 * @param   self        pointer to the thread
 *
 */
void dvmTLHeapDetach(Thread* self);

#endif
