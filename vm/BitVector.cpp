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
 * Implementation of an expandable bit vector.
 */
#include "Dalvik.h"

#ifdef WITH_JIT
#include "compiler/CompilerUtility.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <sstream>

#define kBitVectorGrowth    4   /* increase by 4 u4s when limit hit */

/**
 * Reallocation of the BitVector storage depending on whether for the compiler or not
 */
static void reallocateStorage (BitVector *bv, size_t newSize)
{
    if (bv->isFromCompiler == true)
    {
        //The compiler version just allocates a new size, the arena will handle it
        //False because we are doing like the realloc version
#ifdef WITH_JIT
        void *newPtr = dvmCompilerNew (newSize, false);
        //Copy now
        memcpy (newPtr, bv->storage, bv->storageSize * sizeof (* (bv->storage)));
        //Assign it now in the BitVector
        bv->storage = static_cast<u4*> (newPtr);
#else
        assert ("Trying to resize a compiler BitVector without the Jit" == 0);
        //If not in assert mode, at least allocate it really
        bv->isFromCompiler = false;
        reallocateStorage (bv, newSize);
#endif
    }
    else
    {
        bv->storage = static_cast<u4 *> (realloc (bv->storage, newSize));
        assert (bv->storage != 0);
    }
}

/*
 * Allocate a bit vector with enough space to hold at least the specified
 * number of bits.
 */
BitVector* dvmAllocBitVector(unsigned int startBits, bool expandable, bool fromCompiler)
{
    BitVector* bv;
    unsigned int count = (startBits + 31) >> 5;

    //Always ensure that we allocate at least one block of space.
    if (count == 0)
    {
        count = 1;
    }

    assert(sizeof(bv->storage[0]) == 4);        /* assuming 32-bit units */

    //Decide on the allocation scheme
    if (fromCompiler == true)
    {
#ifdef WITH_JIT
        bv = static_cast<BitVector*> (dvmCompilerNew (sizeof (*bv), false));
        bv->isFromCompiler = true;
        //True for this call to be like calloc below
        bv->storage = static_cast<u4*> (dvmCompilerNew (count * sizeof( * (bv->storage)), true));
#else
        assert ("Trying to allocate a compiler BitVector without the Jit" == 0);
        //If not in assert mode, at least allocate it really
        return dvmAllocBitVector (startBits, expandable, false);
#endif
    }
    else
    {
        bv = (BitVector*) malloc(sizeof(BitVector));

        //Paranoid
        if (bv == 0)
        {
            return 0;
        }

        bv->isFromCompiler = false;
        bv->storage = (u4*) calloc(count, sizeof(u4));
    }

    bv->storageSize = count;
    bv->expandable = expandable;
    return bv;
}

/*
 * Free a BitVector.
 */
void dvmFreeBitVector(BitVector* pBits)
{
    if (pBits == NULL)
        return;

    //If not from the compiler, we do have things to free up
    if (pBits->isFromCompiler == false)
    {
        free(pBits->storage);
        free(pBits);
    }
}

/*
 * "Allocate" the first-available bit in the bitmap.
 *
 * This is not synchronized.  The caller is expected to hold some sort of
 * lock that prevents multiple threads from executing simultaneously in
 * dvmAllocBit/dvmFreeBit.
 */
int dvmAllocBit(BitVector* pBits)
{
    unsigned int word, bit;

retry:
    for (word = 0; word < pBits->storageSize; word++) {
        if (pBits->storage[word] != 0xffffffff) {
            /*
             * There are unallocated bits in this word.  Return the first.
             */
            bit = ffs(~(pBits->storage[word])) -1;
            assert(bit < 32);
            pBits->storage[word] |= 1 << bit;
            return (word << 5) | bit;
        }
    }

    /*
     * Ran out of space, allocate more if we're allowed to.
     */
    if (!pBits->expandable)
        return -1;

    reallocateStorage (pBits, (pBits->storageSize + kBitVectorGrowth) * sizeof(u4));
    memset(&pBits->storage[pBits->storageSize], 0x00,
        kBitVectorGrowth * sizeof(u4));
    pBits->storageSize += kBitVectorGrowth;
    goto retry;
}

/*
 * Mark the specified bit as "set".
 */
bool dvmSetBit(BitVector* pBits, unsigned int num, bool abortOnFail)
{
    //First we do some validation that the bit we are trying to set is valid.
    //This assert makes sense because most parts of compiler don't work with bit vectors
    //this large and we want to prevent any early problems. However, at some point it may
    //be okay to remove this if use case is legitimate.
    assert (static_cast<int> (num) >= 0);

    if (num >= pBits->storageSize * sizeof(u4) * 8) {
        if (pBits->expandable == false) {
            ALOGE("Attempt to set bit outside valid range (%d, limit is %d)",
                num, pBits->storageSize * sizeof(u4) * 8);

            //Do we want to abort (keeps common code transparent to the change)
            if (abortOnFail == true)
            {
                dvmAbort ();
            }
            return false;
        }

        /* Round up to word boundaries for "num+1" bits */
        unsigned int newSize = (num + 1 + 31) >> 5;
        assert(newSize > pBits->storageSize);
        reallocateStorage (pBits, newSize * sizeof(u4));
        if (pBits->storage == NULL) {
            ALOGE("BitVector expansion to %d failed", newSize * sizeof(u4));
            dvmAbort();
        }
        memset(&pBits->storage[pBits->storageSize], 0x00,
            (newSize - pBits->storageSize) * sizeof(u4));
        pBits->storageSize = newSize;
    }

    pBits->storage[num >> 5] |= 1 << (num & 0x1f);

    //Success, we set the bit
    return true;
}

/*
 * Mark the specified bit as "clear".
 */
void dvmClearBit(BitVector* pBits, unsigned int num)
{
    //If the index is over the size
    if (num >= pBits->storageSize * sizeof(u4) * 8)
    {
        //If a set has not been done yet, we don't have to do anything, it is cleared
        return;
    }

    //Otherwise we have a bit more work to do
    pBits->storage[num >> 5] &= ~(1 << (num & 0x1f));
}

/*
 * Mark all bits bit as "clear".
 */
void dvmClearAllBits(BitVector* pBits)
{
    unsigned int count = pBits->storageSize;
    memset(pBits->storage, 0, count * sizeof(u4));
}

/**
 * @see BitVector.h
 */
bool dvmEnsureSizeAndClear (BitVector* pBits, int numBitsToExpandTo)
{
    //We can only handle positive (and 0) sizes
    if (numBitsToExpandTo < 0)
    {
        return false;
    }

    //Start off assuming we will successfully expand
    bool expanded = true;

    if (numBitsToExpandTo > 0)
    {
        //Now try to expand by setting the last bit
        expanded = dvmSetBit (pBits, numBitsToExpandTo - 1, false);
    }

    //We must clear all bits as per our specification
    dvmClearAllBits (pBits);

    return expanded;
}

/*
 * Mark specified number of bits as "set" and clear the rest.  Don't set all
 * bits like ClearAll since there might be unused bits - setting those to one
 * would confuse the iterator.
 */
bool dvmSetInitialBits(BitVector* pBits, unsigned int numBits)
{
    //If numBits == 0, clear all the bits and report success
    if (numBits == 0)
    {
        dvmClearAllBits (pBits);
        return true;
    }

    //Otherwise, first step is to see if we can set those bits
    if (dvmSetBit (pBits, numBits - 1, false) == false)
    {
        //Then don't do it
        return false;
    }

    unsigned int idx;
    for (idx = 0; idx < (numBits >> 5); idx++) {
        pBits->storage[idx] = -1;
    }
    unsigned int remNumBits = numBits & 0x1f;
    if (remNumBits) {
        pBits->storage[idx] = (1 << remNumBits) - 1;
        idx++;
    }
    for (; idx < pBits->storageSize; idx++) {
        pBits->storage[idx] = 0;
    }

    //Report success
    return true;
}

/*
 * Determine whether or not the specified bit is set.
 */
bool dvmIsBitSet(const BitVector* pBits, unsigned int num)
{
    //If the index is over the size
    if (num >= pBits->storageSize * sizeof(u4) * 8)
    {
        //Whether it is expandable or not, this bit does not exist: thus it is not set
        return false;
    }

    unsigned int val = pBits->storage[num >> 5] & (1 << (num & 0x1f));
    return (val != 0);
}

/*
 * Count the number of bits that are set.
 */
int dvmCountSetBits(const BitVector* pBits)
{
    unsigned int word;
    unsigned int count = 0;

    for (word = 0; word < pBits->storageSize; word++) {
        u4 val = pBits->storage[word];

        if (val != 0) {
            if (val == 0xffffffff) {
                count += 32;
            } else {
                /* count the number of '1' bits */
                while (val != 0) {
                    val &= val - 1;
                    count++;
                }
            }
        }
    }

    return count;
}

/*
 * If the vector sizes don't match, log an error and abort.
 */
static void checkSizes(const BitVector* bv1, const BitVector* bv2)
{
    if (bv1->storageSize != bv2->storageSize) {
        ALOGE("Mismatched vector sizes (%d, %d)",
            bv1->storageSize, bv2->storageSize);
        dvmAbort();
    }
}

/*
 * Copy a whole vector to the other. Only do that when the both vectors have
 * the same size.
 */
bool dvmCopyBitVector(BitVector *dest, const BitVector *src)
{
    //Paranoid
    if (dest == 0 || src == 0)
    {
        return false;
    }

    //Paranoid
    if (dest == src)
    {
        return true;
    }

    int destSize = dest->storageSize;
    int srcSize = src->storageSize;

    if (destSize < srcSize)
    {
        //We have more work to do
        //Calculate the highest possible bit position
        unsigned int nbrBitsPerBlock = sizeof ( * (src->storage)) * 8;
        int highestBit = srcSize * nbrBitsPerBlock - 1;

        //Set it and capture the answer: can dest hold the highest bit
        bool res = dvmSetBit (dest, highestBit, false);

        //If dvmSetBit returned false, then we cannot do anything here
        // - It means that dest is non expandable and src are too big
        if (res == false)
        {
            return false;
        }
    }

    //Otherwise we know dest is at least as big as src because
    //  - Either it was big enough to begin with
    //  - Or it wasn't and we got here by correctly getting it big enough via the dvmSetBit
    int i;
    for (i = 0; i < srcSize; i++)
    {
        dest->storage[i] = src->storage[i];
    }

    //For anything remaining, set to 0
    while (i < destSize)
    {
        dest->storage[i] = 0;
        i++;
    }

    //Report success
    return true;
}

bool dvmCheckCopyBitVector(BitVector *dst, const BitVector *src)
{
    bool changed = false;

    checkSizes(dst, src);

    unsigned int idx;
    for (idx = 0; idx < dst->storageSize; idx++) {
        if (dst->storage[idx] != src->storage[idx]) {
            dst->storage[idx] = src->storage[idx];
            changed = true;
        }
    }

    return changed;
}

/*
 * Intersect two bit vectors and store the result to the dest vector.
 */
bool dvmIntersectBitVectors(BitVector *dest, const BitVector *src1,
                            const BitVector *src2)
{
    int destSize = dest->storageSize;
    int src1Size = src1->storageSize;
    int src2Size = src2->storageSize;

    //Are the sizes different?
    if (destSize != src1Size || destSize != src2Size)
    {
        //Get the minimum between both, the excess automatically is 0 for an intersection
        int minSize = (src1Size < src2Size) ? src1Size : src2Size;

        //Calculate the highest possible bit position
        unsigned int nbrBitsPerBlock = sizeof ( * (dest->storage)) * 8;
        int highestBit = minSize * nbrBitsPerBlock - 1;

        //Find out if the bit is set at the highest bit: if it is, we know dest is big enough
        bool isSet = dvmIsBitSet (dest, highestBit);

        //Is the bit set? If not, we have to check dest for size
        if (isSet == false)
        {
            //Set it and capture the answer: can dest hold the highest bit
            bool res = dvmSetBit (dest, highestBit, false);

            //Clear is safe even if highestBit is too big
            dvmClearBit (dest, highestBit);

            //If dvmSetBit returned false, then we cannot do anything here
            // - It means that dest is non expandable and src1/src2 are too big
            if (res == false)
            {
                return false;
            }
        }

        //Paranoid: dest storage size should be at least highest now
        assert (dest->storageSize >= static_cast<unsigned int> (destSize));

        //Intersect until num
        //Note: we know here that num is smaller or equal than either src1 and src2's sizes
        int idx;
        for (idx = 0; idx < minSize; idx++)
        {
            dest->storage[idx] = src1->storage[idx] & src2->storage[idx];
        }

        //Now highest is going to be dest->storageSize, and we are going to reset the rest
        int max = dest->storageSize;
        for (; idx < max; idx++) {
            dest->storage[idx] = 0;
        }
    }
    else
    {
        unsigned int idx;
        for (idx = 0; idx < dest->storageSize; idx++) {
            dest->storage[idx] = src1->storage[idx] & src2->storage[idx];
        }
    }
    return true;
}

/**
 * Calculate the highest bit set index
 */
int dvmHighestBitSet (const BitVector *bv)
{
    unsigned int max = bv->storageSize;
    for (int idx = max - 1; idx >= 0; idx--)
    {
        //If not 0, we have more work
        u4 value = bv->storage[idx];

        if (value != 0)
        {
            //Shift right for the counting
            value /= 2;

            int cnt = 0;

            //Count the bits
            while (value > 0)
            {
                value /= 2;
                cnt++;
            }

            //Return cnt + how many storage units still remain * the number of bits per unit
            int res = cnt + (idx * (sizeof (* (bv->storage)) * 8));
            return res;
        }
    }

    //All zero
    return -1;
}

/*
 * Unify two bit vectors and store the result to the dest vector.
 */
bool dvmUnifyBitVectors(BitVector *dest, const BitVector *src1,
                        const BitVector *src2)
{
    int destSize = dest->storageSize;
    int src1Size = src1->storageSize;
    int src2Size = src2->storageSize;

    //Are the sizes different?
    if (destSize != src1Size || destSize != src2Size)
    {
        //Ok, first question is what size do we really need for dest ?
        int src1Size = dvmHighestBitSet (src1);
        int src2Size = dvmHighestBitSet (src2);

        //Get the maximum value for a union
        int highestBit = (src1Size > src2Size) ? src1Size : src2Size;

        //If nothing, we can clear and return
        if (highestBit == -1)
        {
            dvmClearAllBits (dest);
            return true;
        }

        //Find out if the bit is set at the highest bit: if it is, we know dest is big enough
        bool isSet = dvmIsBitSet (dest, highestBit);

        //Is the bit set? If not, we have to check dest for size
        if (isSet == false)
        {
            //Set it and capture the answer (this is to see if the size is ok, it will reallocate if possible):
            bool res = dvmSetBit (dest, highestBit, false);

            //Clear it back because dest might be src1 or src2
            //Clear is safe even if highestBit is too big
            dvmClearBit (dest, highestBit);

            //If dvmSetBit returned false, then we cannot do anything here
            // - It means that dest is non expandable and src1/src2 are too big
            if (res == false)
            {
                return false;
            }
        }

        //Paranoid: dest storage size should be at least highest now
        assert (dest->storageSize >=
                ( (highestBit + (sizeof (* (dest->storage)) * 8 - 1)) / (sizeof (* (dest->storage)) * 8))
               );

        //Now do the union
        unsigned int idx = 0;

        //First step, copy the minimum accepted size
        unsigned int max = src1->storageSize;

        if (src1->storageSize > src2->storageSize)
        {
            max = src2->storageSize;
        }

        //Get the max of destination
        unsigned int maxDest = dest->storageSize;

        //Copy until max, we know both accept it
        while (idx < max && idx < maxDest)
        {
            dest->storage[idx] = src1->storage[idx] | src2->storage[idx];
            idx++;
        }

        //Now it depends on the sizes, we can just do both sibling loops
        max = src1->storageSize;
        while (idx < max && idx < maxDest)
        {
            dest->storage[idx] = src1->storage[idx];
            idx++;
        }
        max = src2->storageSize;
        while (idx < max && idx < maxDest)
        {
            dest->storage[idx] = src2->storage[idx];
            idx++;
        }

        //Finally reset anything remaining in dest
        while (idx < maxDest)
        {
            dest->storage[idx] = 0;
            idx++;
        }
    }
    else
    {
        unsigned int idx;
        for (idx = 0; idx < dest->storageSize; idx++) {
            dest->storage[idx] = src1->storage[idx] | src2->storage[idx];
        }
    }

    return true;
}

/*
 * Compare two bit vectors and return true if difference is seen.
 */
bool dvmCompareBitVectors(const BitVector *src1, const BitVector *src2)
{
    if (src1->storageSize != src2->storageSize ||
        src1->expandable != src2->expandable)
        return true;

    unsigned int idx;
    for (idx = 0; idx < src1->storageSize; idx++) {
        if (src1->storage[idx] != src2->storage[idx]) return true;
    }
    return false;
}

/* Initialize the iterator structure */
void dvmBitVectorIteratorInit(BitVector* pBits, BitVectorIterator* iterator)
{
    iterator->pBits = pBits;
    iterator->bitSize = pBits->storageSize * sizeof(u4) * 8;
    iterator->idx = 0;
}

/* Return the next position set to 1. -1 means end-of-element reached */
int dvmBitVectorIteratorNext(BitVectorIterator* iterator)
{
    const BitVector* pBits = iterator->pBits;
    u4 bitIndex = iterator->idx;

    assert(iterator->bitSize == pBits->storageSize * sizeof(u4) * 8);
    if (bitIndex >= iterator->bitSize) return -1;

    for (; bitIndex < iterator->bitSize; bitIndex++) {
        unsigned int wordIndex = bitIndex >> 5;
        unsigned int mask = 1 << (bitIndex & 0x1f);
        if (pBits->storage[wordIndex] & mask) {
            iterator->idx = bitIndex+1;
            return bitIndex;
        }
    }
    /* No more set bits */
    return -1;
}

/*
 * Subtract two bit vectors and store the result to the dest vector
 */
bool dvmSubtractBitVectors (BitVector *dest, BitVector *src1, BitVector *src2)
{
    int destSize = dest->storageSize;
    int src1Size = src1->storageSize;

    //Are the sizes different?
    if (destSize < src1Size)
    {
        //Ok, first question is what size do we really need for dest ?
        int src1Highest = dvmHighestBitSet (src1);

        //If nothing in src1, we can clear and return
        if (src1Highest == -1)
        {
            dvmClearAllBits (dest);
            return true;
        }

        //Find out if the bit is set at the highest bit: if it is, we know dest is big enough
        bool isSet = dvmIsBitSet (dest, src1Highest);

        //Is the bit set? If not, we have to check dest for size
        if (isSet == false)
        {
            //Set it and capture the answer (this is to see if the size is ok, it will reallocate if possible):
            bool res = dvmSetBit (dest, src1Highest, false);

            //Clear it back because dest might be src1 or src2
            //Clear is safe even if highestBit is too big
            dvmClearBit (dest, src1Highest);

            //If dvmSetBit returned false, then we cannot do anything here
            // - It means that dest is non expandable and src1/src2 are too big
            if (res == false)
            {
                return false;
            }
        }
    }

    //Now do the difference
    unsigned int idx = 0;

    //First step, copy the minimum accepted size
    unsigned int max = src1->storageSize;

    if (src1->storageSize > src2->storageSize)
    {
        max = src2->storageSize;
    }

    //Get the max of destination
    unsigned int maxDest = dest->storageSize;

    //Diff until max, we know both accept it
    while (idx < max && idx < maxDest)
    {
        dest->storage[idx] = (src1->storage[idx] & (~ (src2->storage[idx])));
        idx++;
    }

    //Now we don't really care about src2 anymore, we do want to copy all of src1 though
    //However, perhaps higher bytes aren't set so we might not have maxDest == max
    max = src1->storageSize;
    while (idx < max && idx < maxDest)
    {
        dest->storage[idx] = src1->storage[idx];
        idx++;
    }

    //Ok, now src2 is irrelevant because without src1 it would be 0

    //Reset dest above
    while (idx < maxDest)
    {
        dest->storage[idx] = 0;
        idx++;
    }

    //Report success
    return true;
}

/*
 * Merge the contents of "src" into "dst", checking to see if this causes
 * any changes to occur.  This is a logical OR.
 *
 * Returns "true" if the contents of the destination vector were modified.
 */
bool dvmCheckMergeBitVectors(BitVector* dst, const BitVector* src)
{
    bool changed = false;

    checkSizes(dst, src);

    unsigned int idx;
    for (idx = 0; idx < dst->storageSize; idx++) {
        u4 merged = src->storage[idx] | dst->storage[idx];
        if (dst->storage[idx] != merged) {
            dst->storage[idx] = merged;
            changed = true;
        }
    }

    return changed;
}

/**
 * @brief Helper to dump a BitVector, it dumps to a string buffer
 * @param buffer the string buffer in which we dump the BitVector
 * @param prefix a prefix before dumping
 * @param bitVector the BitVector we wish to dump
 * @param printIndices do we print indices or not?
 */
static void dvmDumpBitVectorHelper (std::string &buffer, const char *prefix, const BitVector *bitVector, bool printIndices)
{
    //Initialize it
    if (prefix != 0)
    {
        buffer = prefix;
    }
    else
    {
        buffer = "";
    }

    //Null checking
    if (bitVector == 0)
    {
        buffer += "BitVector null";
        return;
    }

    int max = dvmHighestBitSet (bitVector);

    //Are we printing indices or not
    if (printIndices == true)
    {
        //If it's indices, we go upwards
        for (int i = 0; i <= max; i++)
        {
            bool res = dvmIsBitSet (bitVector, i);

            if (res == true)
            {
                std::ostringstream oss;
                oss << i << " ";
                buffer += oss.str ();
            }
        }
    }
    else
    {
        //Otherwise, we go downwards
        for (int i = max; i >= 0; i--)
        {
            bool res = dvmIsBitSet (bitVector, i);

            char c = res + '0';
            buffer += c;
        }
    }
}

/**
 * @brief Dump a bitvector to ALOGD
 * @param prefix do we want a prefix
 * @param bitVector the BitVector to dump
 * @param printIndices do we wish to print indices instead of bit representation (default: false)
 */
void dvmDumpBitVector (const char *prefix, const BitVector *bitVector, bool printIndices)
{
    std::string buffer;

    dvmDumpBitVectorHelper (buffer, prefix, bitVector, printIndices);

    //Now debug log it
    ALOGD ("%s", buffer.c_str ());
}

/**
 * @brief Dump a bitvector in DOT format
 * @param file the File in which to dump
 * @param prefix do we want a prefix
 * @param bitVector the BitVector to dump
 * @param printIndices do we wish to print indices instead of bit representation (default: false)
 * @param lastEntry is it the last entry of the structure (default: false)
 */
void dvmDumpBitVectorDotFormat (FILE *file, const char *prefix, const BitVector *bitVector, bool printIndices, bool lastEntry)
{
    std::string buffer;

    dvmDumpBitVectorHelper (buffer, prefix, bitVector, printIndices);

    //Now print it to the file
    fprintf (file, "    {%s}", buffer.c_str ());

    //If it isn't the last entry, add a |
    if (lastEntry == false)
    {
        fprintf (file, "|");
    }

    //Add the \n
    fprintf (file, "\\\n");
}
