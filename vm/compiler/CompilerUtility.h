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

#ifndef DALVIK_VM_COMPILER_UTILITY_H_
#define DALVIK_VM_COMPILER_UTILITY_H_

#include "Dalvik.h"

/* Each arena page has some overhead, so take a few bytes off 8k */
#define ARENA_DEFAULT_SIZE 8100

/* Allocate the initial memory block for arena-based allocation */
bool dvmCompilerHeapInit(void);

typedef struct ArenaMemBlock {
    size_t blockSize;
    size_t bytesAllocated;
    struct ArenaMemBlock *next;
    char ptr[0];
} ArenaMemBlock;

void *dvmCompilerNew(size_t size, bool zero);

void dvmCompilerArenaReset(void);

typedef struct GrowableList {
    size_t numAllocated;
    size_t numUsed;
    intptr_t *elemList;
} GrowableList;

typedef struct GrowableListIterator {
    GrowableList *list;
    size_t idx;
    size_t size;
} GrowableListIterator;

#define GET_ELEM_N(LIST, TYPE, N) (((TYPE*) LIST->elemList)[N])

#define BLOCK_NAME_LEN 80

/* Forward declarations */
struct LIR;
struct BasicBlock;

void dvmInitGrowableList(GrowableList *gList, size_t initLength);
/**
 * @brief Empty Growable List
 * @param gList the GrowableList to empty
 */
void dvmClearGrowableList (GrowableList *gList);
void dvmInsertGrowableList(GrowableList *gList, intptr_t elem);
void dvmGrowableListIteratorInit(GrowableList *gList,
                                 GrowableListIterator *iterator);
intptr_t dvmGrowableListIteratorNext(GrowableListIterator *iterator);
bool dvmGrowableListSetLastIterator(GrowableListIterator *iterator, intptr_t elem);
intptr_t dvmGrowableListGetElement(const GrowableList *gList, size_t idx);
size_t dvmGrowableListSize(const GrowableList *gList);

BitVector* dvmCompilerAllocBitVector(unsigned int startBits, bool expandable);

/**
 * @brief Allocate an expandable BitVector, set to 0
 * @return an expandable BitVector, 0 if there is a problem
 */
BitVector *dvmCompilerAllocBitVector (void);

BitVector* dvmCompilerAllocBitVector(unsigned int startBits, bool expandable);
bool dvmCompilerSetBit(BitVector* pBits, unsigned int num);
bool dvmCompilerClearBit(BitVector* pBits, unsigned int num);
void dvmCompilerMarkAllBits(BitVector *pBits, bool set);
void dvmDebugBitVector(char *msg, const BitVector *bv, int length);
void dvmDumpLIRInsn(struct LIR *lir, unsigned char *baseAddr);
void dvmDumpResourceMask(struct LIR *lir, u8 mask, const char *prefix);
void dvmDumpBlockBitVector(const GrowableList *blocks, char *msg,
                           const BitVector *bv, int length);
void dvmGetBlockName(struct BasicBlock *bb, char *name);
void dvmCompilerCacheFlush(long start, long end, long flags);
void dvmCompilerCacheClear(char *start, size_t size);

#ifdef ARCH_IA32

/**
 * @brief Set the trim user value
 * @param value the value we want
 */
void setArenaTrimUserValue (unsigned int value);

/** @brief Triming style */
enum ArenaTrimStyle {
    ARENA_NONE,        /**< @brief No trimming */
    ARENA_ALL_BUT_ONE, /**< @brief All but one block */
    ARENA_AVERAGE,     /**< @brief Until the current average */
    ARENA_USER_DEFINED /**< @brief User defined */
    };

/**
 * @brief Set the arena trim style
 * @param value the value we want
 */
void setArenaTrimStyle (ArenaTrimStyle value);
#endif

#endif  // DALVIK_COMPILER_UTILITY_H_
