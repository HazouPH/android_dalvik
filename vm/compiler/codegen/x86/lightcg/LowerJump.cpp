
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


/**
 * @file vm/compiler/codegen/x86/LowerJump.cpp
 * @brief This file lowers the following bytecodes: IF_XXX, GOTO
 */
#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include <math.h>
#include "libdex/DexOpcodes.h"
#include "libdex/DexFile.h"
#include "Lower.h"
#include "NcgAot.h"
#include "enc_wrapper.h"
#include "interp/InterpDefs.h"
#include "NcgHelper.h"
#include "RegisterizationBE.h"
#include "Scheduler.h"
#include "Singleton.h"
#include "Utility.h"

#if defined VTUNE_DALVIK
#include "compiler/codegen/x86/VTuneSupportX86.h"
#endif

LabelMap* globalMap;
LabelMap* globalShortMap;//make sure for each bytecode, there is no duplicated label
LabelMap* globalWorklist = NULL;
LabelMap* globalShortWorklist;

int globalMapNum;
int globalWorklistNum;
int globalDataWorklistNum;
int VMAPIWorklistNum;
int globalPCWorklistNum;
int chainingWorklistNum;

LabelMap* globalDataWorklist = NULL;
LabelMap* globalPCWorklist = NULL;
LabelMap* chainingWorklist = NULL;
LabelMap* VMAPIWorklist = NULL;

char* ncgClassData;
char* ncgClassDataPtr;
char* ncgMethodData;
char* ncgMethodDataPtr;
int   ncgClassNum;
int   ncgMethodNum;

NCGWorklist* globalNCGWorklist;
DataWorklist* methodDataWorklist;
#ifdef ENABLE_TRACING
MapWorklist* methodMapWorklist;
#endif
/*!
\brief search globalShortMap to find the entry for the given label

*/
LabelMap* findItemForShortLabel(const char* label) {
    LabelMap* ptr = globalShortMap;
    while(ptr != NULL) {
        if(!strcmp(label, ptr->label)) {
            return ptr;
        }
        ptr = ptr->nextItem;
    }
    return NULL;
}
//assume size of "jump reg" is 2
#define JUMP_REG_SIZE 2
#define ADD_REG_REG_SIZE 3
/*!
\brief update value of the immediate in the given jump instruction

check whether the immediate is out of range for the pre-set size
*/
int updateJumpInst(char* jumpInst, OpndSize immSize, int relativeNCG) {
#ifdef DEBUG_NCG_JUMP
    ALOGI("update jump inst @ %p with %d", jumpInst, relativeNCG);
#endif
    if(immSize == OpndSize_8) { //-128 to 127
        if(relativeNCG >= 128 || relativeNCG < -128) {
            ALOGI("JIT_INFO: Pre-allocated space for a forward jump is not big enough\n");
            SET_JIT_ERROR(kJitErrorShortJumpOffset);
            return -1;
        }
    }
    if(immSize == OpndSize_16) { //-2^16 to 2^16-1
        if(relativeNCG >= 32768 || relativeNCG < -32768) {
            ALOGI("JIT_INFO: Pre-allocated space (16-bit) for a forward jump is not big enough\n");
            SET_JIT_ERROR(kJitErrorShortJumpOffset);
            return -1;
        }
    }
    dump_imm_update(relativeNCG, jumpInst, false);
    return 0;
}

/*!
\brief insert a label

It takes argument checkDup, if checkDup is true, an entry is created in globalShortMap, entries in globalShortWorklist are checked, if there exists a match, the immediate in the jump instruction is updated and the entry is removed from globalShortWorklist;
otherwise, an entry is created in globalMap.
*/
int insertLabel(const char* label, bool checkDup) {
    LabelMap* item = NULL;

    // We are inserting a label. Someone might want to jump to it
    // so flush scheduler's queue
    if (gDvmJit.scheduling)
        singletonPtr<Scheduler>()->signalEndOfNativeBasicBlock();

    if(!checkDup) {
        item = (LabelMap*)malloc(sizeof(LabelMap));
        if(item == NULL) {
            ALOGI("JIT_INFO: Memory allocation failed at insertLabel with checkDup false");
            SET_JIT_ERROR(kJitErrorMallocFailed);
            return -1;
        }
        snprintf(item->label, LABEL_SIZE, "%s", label);
        item->codePtr = stream;
        item->nextItem = globalMap;
        globalMap = item;
#ifdef DEBUG_NCG_CODE_SIZE
        ALOGI("insert global label %s %p", label, stream);
#endif
        globalMapNum++;
        return 0;
    }

    item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertLabel with checkDup true");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    snprintf(item->label, LABEL_SIZE, "%s", label);
    item->codePtr = stream;
    item->nextItem = globalShortMap;
    globalShortMap = item;
#ifdef DEBUG_NCG
    ALOGI("Insert short-term label %s %p", label, stream);
#endif
    LabelMap* ptr = globalShortWorklist;
    LabelMap* ptr_prevItem = NULL;
    while(ptr != NULL) {
        if(!strcmp(ptr->label, label)) {
            //perform work
            int relativeNCG = stream - ptr->codePtr;
            unsigned instSize = encoder_get_inst_size(ptr->codePtr);
            relativeNCG -= instSize; //size of the instruction
#ifdef DEBUG_NCG
            ALOGI("Perform work short-term %p for label %s relative %d\n", ptr->codePtr, label, relativeNCG);
#endif
            int retval = updateJumpInst(ptr->codePtr, ptr->size, relativeNCG);
            //If this fails, the jump offset was not big enough. Raise the corresponding error flag
            //We may decide to re-compiler the trace with a large jump offset later
            if (retval == -1){
                ALOGI("JIT_INFO: Label \"%s\" too far away from jump location", label);
                SET_JIT_ERROR(kJitErrorShortJumpOffset);
                return retval;
            }

            //remove work
            if(ptr_prevItem == NULL) {
                globalShortWorklist = ptr->nextItem;
                free(ptr);
                ptr = globalShortWorklist; //ptr_prevItem is still NULL
            }
            else {
                ptr_prevItem->nextItem = ptr->nextItem;
                free(ptr);
                ptr = ptr_prevItem->nextItem;
            }
        }
        else {
            ptr_prevItem = ptr;
            ptr = ptr->nextItem;
        }
    } //while
    return 0;
}
/*!
\brief search globalMap to find the entry for the given label

*/
char* findCodeForLabel(const char* label) {
    LabelMap* ptr = globalMap;
    while(ptr != NULL) {
        if(!strcmp(label, ptr->label)) {
            return ptr->codePtr;
        }
        ptr = ptr->nextItem;
    }
    return NULL;
}
/*!
\brief search globalShortMap to find the entry for the given label

*/
char* findCodeForShortLabel(const char* label) {
    LabelMap* ptr = globalShortMap;
    while(ptr != NULL) {
        if(!strcmp(label, ptr->label)) {
            return ptr->codePtr;
        }
        ptr = ptr->nextItem;
    }
    return NULL;
}
int insertLabelWorklist(const char* label, OpndSize immSize) {
    LabelMap* item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertLabelWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    snprintf(item->label, LABEL_SIZE, "%s", label);
    item->codePtr = stream;
    item->size = immSize;
    item->nextItem = globalWorklist;
    globalWorklist = item;
#ifdef DEBUG_NCG
    ALOGI("Insert globalWorklist: %s %p", label, stream);
#endif
    return 0;
}

int insertShortWorklist(const char* label, OpndSize immSize) {
    LabelMap* item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertShortWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    snprintf(item->label, LABEL_SIZE, "%s", label);
    item->codePtr = stream;
    item->size = immSize;
    item->nextItem = globalShortWorklist;
    globalShortWorklist = item;
#ifdef DEBUG_NCG
    ALOGI("Insert globalShortWorklist: %s %p", label, stream);
#endif
    return 0;
}
/*!
\brief free memory allocated for globalMap

*/
void freeLabelMap() {
    LabelMap* ptr = globalMap;
    while(ptr != NULL) {
        globalMap = ptr->nextItem;
        free(ptr);
        ptr = globalMap;
    }
}
/*!
\brief free memory allocated for globalShortMap

*/
void freeShortMap() {
    LabelMap* ptr = globalShortMap;
    while(ptr != NULL) {
        globalShortMap = ptr->nextItem;
        free(ptr);
        ptr = globalShortMap;
    }
    globalShortMap = NULL;
}

int insertGlobalPCWorklist(char * offset, char * codeStart)
{
    LabelMap* item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertGlobalPCWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    snprintf(item->label, LABEL_SIZE, "%s", "export_pc");
    item->size = OpndSize_32;
    item->codePtr = offset; //points to the immediate operand
    item->addend = codeStart - streamMethodStart; //relative code pointer
    item->nextItem = globalPCWorklist;
    globalPCWorklist = item;
    globalPCWorklistNum ++;

#ifdef DEBUG_NCG
    ALOGI("Insert globalPCWorklist: %p %p %p %x %p", globalDvmNcg->streamCode,  codeStart, streamCode, item->addend, item->codePtr);
#endif
    return 0;
}

/*
 * search chainingWorklist to return instruction offset address in move instruction
 */
char* searchChainingWorklist(unsigned int blockId) {
    LabelMap* ptr = chainingWorklist;
    unsigned instSize;

    while (ptr != NULL) {
       if (blockId == ptr->addend) {
           instSize = encoder_get_inst_size(ptr->codePtr);
           assert((uint)(ptr->codePtr + instSize - 4) % 16 <= 12);
           return (ptr->codePtr + instSize - 4); // 32bit relative offset
       }
       ptr = ptr->nextItem;
    }
#ifdef DEBUG_NCG
    ALOGI("can't find item for blockId %d in searchChainingWorklist\n", blockId);
#endif
    return NULL;
}

int insertChainingWorklist(int bbId, char * codeStart)
{
    LabelMap* item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertChainingWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    item->size = OpndSize_32;
    item->codePtr = codeStart; //points to the move instruction
    item->addend = bbId; //relative code pointer
    item->nextItem = chainingWorklist;
    chainingWorklist = item;

#ifdef DEBUG_NCG
    ALOGI("InsertChainingWorklist: %p basic block %d", codeStart, bbId);
#endif
    return 0;
}

int insertGlobalDataWorklist(char * offset, const char* label)
{
    LabelMap* item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertGlobalDataWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    snprintf(item->label, LABEL_SIZE, "%s", label);
    item->codePtr = offset;
    item->size = OpndSize_32;
    item->nextItem = globalDataWorklist;
    globalDataWorklist = item;
    globalDataWorklistNum ++;

#ifdef DEBUG_NCG
    ALOGI("Insert globalDataWorklist: %s %p", label, offset);
#endif

    return 0;
}

int insertVMAPIWorklist(char * offset, const char* label)
{
    LabelMap* item = (LabelMap*)malloc(sizeof(LabelMap));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertVMAPIWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    snprintf(item->label, LABEL_SIZE, "%s", label);
    item->codePtr = offset;
    item->size = OpndSize_32;

    item->nextItem = VMAPIWorklist;
    VMAPIWorklist = item;

    VMAPIWorklistNum ++;

#ifdef DEBUG_NCG
    ALOGI("Insert VMAPIWorklist: %s %p", label, offset);
#endif
    return 0;
}
////////////////////////////////////////////////


int updateImmRMInst(char* moveInst, const char* label, int relativeNCG); //forward declaration
//////////////////// performLabelWorklist is defined differently for code cache
void performChainingWorklist() {
    LabelMap* ptr = chainingWorklist;
    while(ptr != NULL) {
        int tmpNCG = getLabelOffset (ptr->addend);
        char* NCGaddr = streamMethodStart + tmpNCG;
        updateImmRMInst(ptr->codePtr, "", (int)NCGaddr);
        chainingWorklist = ptr->nextItem;
        free(ptr);
        ptr = chainingWorklist;
    }
}
void freeChainingWorklist() {
    LabelMap* ptr = chainingWorklist;
    while(ptr != NULL) {
        chainingWorklist = ptr->nextItem;
        free(ptr);
        ptr = chainingWorklist;
    }
}

/*
 *search globalWorklist to find the jmp/jcc offset address
 */
char* searchLabelWorklist(char* label) {
    LabelMap* ptr = globalWorklist;
    unsigned instSize;

    while(ptr != NULL) {
        if(!strcmp(label, ptr->label)) {
            instSize = encoder_get_inst_size(ptr->codePtr);
            assert((uint)(ptr->codePtr + instSize - 4) % 16 <= 12);
            return (ptr->codePtr + instSize - 4); // 32bit relative offset
        }
        ptr = ptr->nextItem;
   }
#ifdef DEBUG_NCG
    ALOGI("can't find item for label %s in searchLabelWorklist\n", label);
#endif
    return NULL;
}

// delete the node with label "vr_store_at_loop_back" from globalMap
static void deleteVRStoreLabelGlobalMap()
{
    LabelMap * ptr = globalMap;
    LabelMap * prePtr = NULL;

    while(ptr != NULL) {
        if (strstr(ptr->label, ".vr_store_at_loop_back") != 0) {
            if (prePtr == NULL)
                globalMap = ptr->nextItem;
            else
                prePtr->nextItem = ptr->nextItem;
            free(ptr);
            return;
        }
        prePtr = ptr;
        ptr = ptr->nextItem;
    }
}

//Work only for initNCG
void performLabelWorklist() {
    LabelMap* ptr = globalWorklist;
    while(ptr != NULL) {
#ifdef DEBUG_NCG
        ALOGI("Perform work global %p for label %s", ptr->codePtr, ptr->label);
#endif
        char* targetCode = findCodeForLabel(ptr->label);
        assert(targetCode != NULL);
        int relativeNCG = targetCode - ptr->codePtr;
        unsigned instSize = encoder_get_inst_size(ptr->codePtr);
        relativeNCG -= instSize; //size of the instruction
        updateJumpInst(ptr->codePtr, ptr->size, relativeNCG);
        globalWorklist = ptr->nextItem;
        free(ptr);
        ptr = globalWorklist;
    }
    deleteVRStoreLabelGlobalMap();
}

void freeLabelWorklist() {
    LabelMap* ptr = globalWorklist;
    while(ptr != NULL) {
        globalWorklist = ptr->nextItem;
        free(ptr);
        ptr = globalWorklist;
    }
}

///////////////////////////////////////////////////
/*!
\brief update value of the immediate in the given move instruction

*/
int updateImmRMInst(char* moveInst, const char* label, int relativeNCG) {
#ifdef DEBUG_NCG
    ALOGI("Perform work ImmRM inst @ %p for label %s with %d", moveInst, label, relativeNCG);
#endif
    dump_imm_update(relativeNCG, moveInst, true);
    return 0;
}
//! maximum instruction size for jump,jcc,call: 6 for jcc rel32
#define MAX_JCC_SIZE 6
//! minimum instruction size for jump,jcc,call: 2
#define MIN_JCC_SIZE 2
/*!
\brief estimate size of the immediate

Somehow, 16 bit jump does not work. This function will return either 8 bit or 32 bit
EXAMPLE:
  native code at A: ...
  native code at B: jump relOffset (target is A)
  native code at B':
  --> relOffset = A - B' = A - B - size of the jump instruction
  Argument "target" is equal to A - B. To determine size of the immediate, we check tha value of "target - size of the jump instructoin"
*/
OpndSize estOpndSizeFromImm(int target) {
    if(target-MIN_JCC_SIZE < 128 && target-MAX_JCC_SIZE >= -128) return OpndSize_8;
#ifdef SUPPORT_IMM_16
    if(target-MIN_JCC_SIZE < 32768 && target-MAX_JCC_SIZE >= -32768) return OpndSize_16;
#endif
    return OpndSize_32;
}

/*!
\brief return size of a jump or call instruction
*/
unsigned getJmpCallInstSize(OpndSize size, JmpCall_type type) {
    if(type == JmpCall_uncond) {
        if(size == OpndSize_8) return 2;
        if(size == OpndSize_16) return 4;
        return 5;
    }
    if(type == JmpCall_cond) {
        if(size == OpndSize_8) return 2;
        if(size == OpndSize_16) return 5;
        return 6;
    }
    if(type == JmpCall_reg) {
        assert(size == OpndSize_32);
        return JUMP_REG_SIZE;
    }
    if(type == JmpCall_call) {
        assert(size != OpndSize_8);
        if(size == OpndSize_16) return 4;
        return 5;
    }
    return 0;
}

//! \brief Get the offset given a jump target
//!
//! \details check whether a branch target is already handled if yes, return the
//! size of the immediate; otherwise, call insertShortWorklist or insertLabelWorklist.
//!
//! If the branch target is not handled, call insertShortWorklist or insertLabelWorklist
//! depending on isShortTerm, unknown is set to true, immSize is set to 32 if isShortTerm
//! is false, set to 32 if isShortTerm is true and target is check_cast_null, set to 8 otherwise.
//!
//! If the branch target is handled, call estOpndSizeFromImm to set immSize for jump
//! instruction, returns the value of the immediate
//!
//! \param target the target of the jump
//! \param isShortTerm whether this is a short term jump
//! \param type Call or Jmp
//! \param unknown target known or not
//! \param immSize size of the jump offset
//!
//! \return jump offset (can also return error value, but caller cannot distinguish)
int getRelativeOffset(const char* target, bool isShortTerm, JmpCall_type type, bool* unknown, OpndSize* immSize) {
    char* targetPtrInStream = NULL;
    if(isShortTerm) targetPtrInStream = findCodeForShortLabel(target);
    else targetPtrInStream = findCodeForLabel(target);

    int relOffset;
    int retCode = 0;
    *unknown = false;
    if(targetPtrInStream == NULL) {
        //branch target is not handled yet
        relOffset = 0;
        *unknown = true;
        if(isShortTerm) {
            /* for backward jump, at this point, we don't know how far the target is from this jump
               since the lable is only used within a single bytecode, we assume OpndSize_8 is big enough
               but there are special cases where we should use 32 bit offset
            */
            //Check if we have failed with 8-bit offset previously. Use 32-bit offsets if so.
            if (gDvmJit.disableOpt & (1 << kShortJumpOffset)){
                *immSize = OpndSize_32;
            }
            //Check if it is a special case:
            //These labels are known to be far off from the jump location
            //Safe to set them to large offset by default
            else if(!strcmp(target, ".stackOverflow") ||
                    !strcmp(target, ".invokeChain") ||
                    !strcmp(target, "after_exception_1") ||
                    !strncmp(target, "exception_restore_state_", 24)) {
#ifdef SUPPORT_IMM_16
                *immSize = OpndSize_16;
#else
                *immSize = OpndSize_32;
#endif
            } else {
                *immSize = OpndSize_8;
            }
#ifdef WITH_SELF_VERIFICATION
            if(!strcmp(target, ".aput_object_skip_check") ||
               !strcmp(target, ".aput_object_after_check") ) {
                *immSize = OpndSize_32;
            }
#endif
#ifdef DEBUG_NCG_JUMP
            ALOGI("Insert to short worklist %s %d", target, *immSize);
#endif
            retCode = insertShortWorklist(target, *immSize);
            //NOTE: Returning negative value here cannot indicate an error
            //The caller accepts any value as correct. Only the premature
            //return matters here.
            if (retCode < 0)
                return retCode;
        }
        else {
#ifdef SUPPORT_IMM_16
            *immSize = OpndSize_16;
#else
            *immSize = OpndSize_32;
#endif
            retCode = insertLabelWorklist(target, *immSize);
            //NOTE: Returning negative value here cannot indicate an error
            //The caller accepts any value as correct. Only the premature
            //return matters here.
            if (retCode < 0) {
                return retCode;
            }
        }
        if(type == JmpCall_call) { //call sz16 does not work in gdb
            *immSize = OpndSize_32;
        }
        return 0;
    }
    else if (!isShortTerm) {
#ifdef SUPPORT_IMM_16
        *immSize = OpndSize_16;
#else
        *immSize = OpndSize_32;
#endif
        retCode = insertLabelWorklist(target, *immSize);
        if (retCode < 0) {
            return retCode;
        }
    }

#ifdef DEBUG_NCG
    ALOGI("Backward branch @ %p for label %s", stream, target);
#endif
    relOffset = targetPtrInStream - stream;
    if (type == JmpCall_call) {
        *immSize = OpndSize_32;
    }
    else {
        *immSize = estOpndSizeFromImm(relOffset);
    }
    relOffset -= getJmpCallInstSize(*immSize, type);
    return relOffset;
}

/*!
\brief generate a single native instruction "jcc imm" to jump to a label

*/
void conditional_jump(ConditionCode cc, const char* target, bool isShortTerm) {
    if(jumpToException(target) && currentExceptionBlockIdx >= 0) { //jump to the exceptionThrow block
        condJumpToBasicBlock (cc, currentExceptionBlockIdx);
        return;
    }
    Mnemonic m = (Mnemonic)(Mnemonic_Jcc + cc);
    bool unknown;
    OpndSize size = OpndSize_Null;
    int imm = 0;
    if(!gDvmJit.scheduling)
        imm = getRelativeOffset(target, isShortTerm, JmpCall_cond, &unknown, &size);
    dump_label(m, size, imm, target, isShortTerm);
}

/*!
\brief generate a single native instruction "jmp imm" to jump to a label

If the target is ".invokeArgsDone" and mode is NCG O1, extra work is performed to dump content of virtual registers to memory.
*/
void unconditional_jump(const char* target, bool isShortTerm) {
    if(jumpToException(target) && currentExceptionBlockIdx >= 0) { //jump to the exceptionThrow block
        jumpToBasicBlock (currentExceptionBlockIdx);
        return;
    }
    Mnemonic m = Mnemonic_JMP;
    bool unknown;
    OpndSize size = OpndSize_Null;
    int imm = 0;
    if(!gDvmJit.scheduling)
        imm = getRelativeOffset(target, isShortTerm, JmpCall_uncond, &unknown, &size);
    dump_label(m, size, imm, target, isShortTerm);
}

/**
 * @brief Generates a single native instruction "jcc imm"
 * @param cc The condition to take the jump
 * @param target The immediate representing the relative offset from instruction pointer to jump to.
 * @param size The size of immediate
 */
static void conditional_jump_int(ConditionCode cc, int target, OpndSize size) {
    Mnemonic m = (Mnemonic)(Mnemonic_Jcc + cc);
    dump_imm(m, size, target);
}

/**
 * @brief Generates a single native instruction "jmp imm"
 * @param target The immediate representing the relative offset from instruction pointer to jump to.
 * @param size The size of immediate
 */
static void unconditional_jump_int(int target, OpndSize size) {
    Mnemonic m = Mnemonic_JMP;
    dump_imm(m, size, target);
}

//! Used to generate a single native instruction for conditionally
//! jumping to a block when the immediate is not yet known.
//! This should only be used when instruction scheduling is enabled.
//! \param cc type of conditional jump
//! \param targetBlockId id of MIR basic block
//! \param immediateNeedsAligned Whether the immediate needs to be aligned
//! within 16-bytes
static void conditional_jump_block(ConditionCode cc, int targetBlockId,
        bool immediateNeedsAligned) {
    Mnemonic m = (Mnemonic)(Mnemonic_Jcc + cc);
    dump_blockid_imm(m, targetBlockId, immediateNeedsAligned);
}

//! Used to generate a single native instruction for unconditionally
//! jumping to a block when the immediate is not yet known.
//! This should only be used when instruction scheduling is enabled.
//! \param targetBlockId id of MIR basic block
//! \param immediateNeedsAligned Whether the immediate needs to be aligned
//! within 16-bytes
static void unconditional_jump_block(int targetBlockId, bool immediateNeedsAligned) {
    Mnemonic m = Mnemonic_JMP;
    dump_blockid_imm(m, targetBlockId, immediateNeedsAligned);
}

/**
 * @brief Generates a single native instruction "jmp reg"
 * @param reg The register to use for jump
 * @param isPhysical Whether the register is physical
 */
void unconditional_jump_reg(int reg, bool isPhysical) {
    dump_reg(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, reg, isPhysical, LowOpndRegType_gp);
}

/**
 * @brief Generates a jump with 32-bit relative immediate that jumps
 * to the target.
 * @details Updates the instruction stream with the jump.
 * @param target absolute address of target.
 */
void unconditional_jump_rel32 (void * target)
{
    // We will need to figure out the immediate to use for the relative
    // jump, so we need to flush scheduler so that stream is updated.
    // In most cases this won't affect the schedule since the jump would've
    // ended the native BB anyway and would've been scheduled last.
    if (gDvmJit.scheduling == true)
    {
        singletonPtr<Scheduler> ()->signalEndOfNativeBasicBlock ();
    }

    // Calculate the address offset between the destination of jump and the
    // function we are jumping to.
    int relOffset = reinterpret_cast<int> (target) - reinterpret_cast<int> (stream);

    // Since instruction pointer will already be updated when executing this,
    // subtract size of jump instruction
    relOffset -= getJmpCallInstSize (OpndSize_32, JmpCall_uncond);

    // Generate the unconditional jump now
    unconditional_jump_int (relOffset, OpndSize_32);
}

void jumpToBasicBlock (int targetBlockId, bool immediateNeedsAligned)
{
    //When scheduling is enabled the jump that needs patched may be moved and thus
    //we cannot assume that current place in code stream is where the jump will be lowered.
    //For that reason we have two different paths.
    if (gDvmJit.scheduling == true)
    {
        unconditional_jump_block (targetBlockId, immediateNeedsAligned);
    }
    else
    {
        //If jump needs aligned, then we simply align by 1 since size of encoded jump is 1
        if (immediateNeedsAligned == true)
        {
            alignOffset (1);
        }

        //Get location of target
        bool unknown;
        OpndSize size;
        int relativeNCG = getRelativeNCG (targetBlockId, JmpCall_uncond, &unknown, &size);

        //Generate unconditional jump
        unconditional_jump_int (relativeNCG, size);
    }
}

void condJumpToBasicBlock (ConditionCode cc, int targetBlockId, bool immediateNeedsAligned)
{
    //When scheduling is enabled the jump that needs patched may be moved and thus
    //we cannot assume that current place in code stream is where the jump will be lowered.
    //For that reason we have two different paths.
    if (gDvmJit.scheduling == true)
    {
        conditional_jump_block (cc, targetBlockId, immediateNeedsAligned);
    }
    else
    {
        //If jump needs aligned, then we simply align by 2 since size of encoded conditional jump is 2
        if (immediateNeedsAligned == true)
        {
            alignOffset (2);
        }

        //Get location of target
        bool unknown;
        OpndSize size;
        int relativeNCG = getRelativeNCG (targetBlockId, JmpCall_cond, &unknown, &size);

        //Generate unconditional jump
        conditional_jump_int (cc, relativeNCG, size);
    }
}

/*!
\brief generate a single native instruction to call a function

If mode is NCG O1, extra work is performed to dump content of virtual registers to memory.
*/
void call(const char* target) {
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        beforeCall(target);
    }
    Mnemonic m = Mnemonic_CALL;
    bool dummy;
    OpndSize size = OpndSize_Null;
    int relOffset = 0;
    if(!gDvmJit.scheduling)
        relOffset = getRelativeOffset(target, false, JmpCall_call, &dummy, &size);
    dump_label(m, size, relOffset, target, false);
    if(gDvm.executionMode == kExecutionModeNcgO1) {
        afterCall(target);
    }
}
/*!
\brief generate a single native instruction to call a function

*/
void call_reg(int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_CALL;
    dump_reg(m, ATOM_NORMAL, OpndSize_32, reg, isPhysical, LowOpndRegType_gp);
}
void call_reg_noalloc(int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_CALL;
    dump_reg_noalloc(m, OpndSize_32, reg, isPhysical, LowOpndRegType_gp);
}

/*!
\brief generate a single native instruction to call a function

*/
void call_mem(int disp, int reg, bool isPhysical) {
    Mnemonic m = Mnemonic_CALL;
    dump_mem(m, ATOM_NORMAL, OpndSize_32, disp, reg, isPhysical);
}

/*!
\brief insert an entry to globalNCGWorklist

*/
int insertNCGWorklist(s4 relativePC, OpndSize immSize) {
    int offsetNCG2 = stream - streamMethodStart;
#ifdef DEBUG_NCG
    ALOGI("Insert NCGWorklist (goto forward) @ %p offsetPC %x relativePC %x offsetNCG %x", stream, offsetPC, relativePC, offsetNCG2);
#endif
    NCGWorklist* item = (NCGWorklist*)malloc(sizeof(NCGWorklist));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertNCGWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    item->relativePC = relativePC;
    item->offsetPC = offsetPC;
    item->offsetNCG = offsetNCG2;
    item->codePtr = stream;
    item->size = immSize;
    item->nextItem = globalNCGWorklist;
    globalNCGWorklist = item;
    return 0;
}


/*
 *search globalNCGWorklist to find the jmp/jcc offset address
 */
char* searchNCGWorklist(int blockId) {
    NCGWorklist* ptr = globalNCGWorklist;
    unsigned instSize;

    while (ptr != NULL) {
      if (blockId == ptr->relativePC) {
           instSize = encoder_get_inst_size(ptr->codePtr);
           assert((uint)(ptr->codePtr + instSize - 4) % 16 <= 12);
           return (ptr->codePtr + instSize - 4); // 32bit relative offset
       }
       ptr = ptr->nextItem;
    }
#ifdef DEBUG_NCG
    ALOGI("can't find item for blockId %d in searchNCGWorklist\n", blockId);
#endif
    return NULL;
}

#ifdef ENABLE_TRACING
int insertMapWorklist(s4 BCOffset, s4 NCGOffset, int isStartOfPC) {
    return 0;
}
#endif
/*!
\brief insert an entry to methodDataWorklist

This function is used by bytecode FILL_ARRAY_DATA, PACKED_SWITCH, SPARSE_SWITCH
*/
int insertDataWorklist(s4 relativePC, char* codePtr1) {
    //insert according to offsetPC+relativePC, smallest at the head
    DataWorklist* item = (DataWorklist*)malloc(sizeof(DataWorklist));
    if(item == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at insertDataWorklist");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    item->relativePC = relativePC;
    item->offsetPC = offsetPC;
    item->codePtr = codePtr1;
    item->codePtr2 = stream; //jump_reg for switch
    DataWorklist* ptr = methodDataWorklist;
    DataWorklist* prev_ptr = NULL;
    while(ptr != NULL) {
        int tmpPC = ptr->offsetPC + ptr->relativePC;
        int tmpPC2 = relativePC + offsetPC;
        if(tmpPC2 < tmpPC) {
            break;
        }
        prev_ptr = ptr;
        ptr = ptr->nextItem;
    }
    //insert item before ptr
    if(prev_ptr != NULL) {
        prev_ptr->nextItem = item;
    }
    else methodDataWorklist = item;
    item->nextItem = ptr;
    return 0;
}

/*!
\brief work on globalNCGWorklist

*/
int performNCGWorklist() {
    NCGWorklist* ptr = globalNCGWorklist;
    while(ptr != NULL) {
        int tmpNCG = getLabelOffset (ptr->relativePC);
        ALOGV("Perform NCG worklist: @ %p target block %d target NCG %x",
             ptr->codePtr, ptr->relativePC, tmpNCG);
        assert(tmpNCG >= 0);
        int relativeNCG = tmpNCG - ptr->offsetNCG;
        unsigned instSize = encoder_get_inst_size(ptr->codePtr);
        relativeNCG -= instSize;
        updateJumpInst(ptr->codePtr, ptr->size, relativeNCG);
        globalNCGWorklist = ptr->nextItem;
        free(ptr);
        ptr = globalNCGWorklist;
    }
    return 0;
}
void freeNCGWorklist() {
    NCGWorklist* ptr = globalNCGWorklist;
    while(ptr != NULL) {
        globalNCGWorklist = ptr->nextItem;
        free(ptr);
        ptr = globalNCGWorklist;
    }
}

/*!
\brief used by bytecode SWITCH
\param targetPC points to start of the data section
\param codeInst the code instruction pointer
\return the offset in native code between add_reg_reg and the data section

Code sequence for SWITCH
  call ncgGetEIP
  codeInst: add_reg_reg %eax, %edx
  jump_reg %edx
This function returns the offset in native code between add_reg_reg and the data section
*/
int getRelativeNCGForSwitch(int targetPC, char* codeInst) {
    int tmpNCG = mapFromBCtoNCG[targetPC];
    int offsetNCG2 = codeInst - streamMethodStart;
    int relativeOff = tmpNCG - offsetNCG2;
    return relativeOff;
}

/*!
\brief work on methodDataWorklist
*/
int performDataWorklist(void) {
    DataWorklist* ptr = methodDataWorklist;
    if(ptr == NULL) return 0;

    char* codeCacheEnd = ((char *) gDvmJit.codeCache) + gDvmJit.codeCacheSize - CODE_CACHE_PADDING;
    u2 insnsSize = dvmGetMethodInsnsSize(currentMethod); //bytecode
    //align stream to multiple of 4
    int alignBytes = (int)stream & 3;
    if(alignBytes != 0) alignBytes = 4-alignBytes;
    stream += alignBytes;

    while(ptr != NULL) {
        int tmpPC = ptr->offsetPC + ptr->relativePC;
        int endPC = insnsSize;
        if(ptr->nextItem != NULL) endPC = ptr->nextItem->offsetPC + ptr->nextItem->relativePC;
        mapFromBCtoNCG[tmpPC] = stream - streamMethodStart; //offsetNCG in byte

        //handle fill_array_data, packed switch & sparse switch
        u2 tmpInst = *(currentMethod->insns + ptr->offsetPC);
        u2* sizePtr;
        s4* entryPtr_bytecode;
        u2 tSize, iVer;
        u4 sz;

        if (gDvmJit.codeCacheFull == true) {
            // We are out of code cache space. Skip writing data/code to
            //   code cache. Simply free the item.
            methodDataWorklist = ptr->nextItem;
            free(ptr);
            ptr = methodDataWorklist;
        }

        switch (INST_INST(tmpInst)) {
        case OP_FILL_ARRAY_DATA:
            sz = (endPC-tmpPC)*sizeof(u2);
            if ((stream + sz) < codeCacheEnd) {
                memcpy(stream, (u2*)currentMethod->insns+tmpPC, sz);
#ifdef DEBUG_NCG_CODE_SIZE
                ALOGI("Copy data section to stream %p: start at %d, %d bytes", stream, tmpPC, sz);
#endif
#ifdef DEBUG_NCG
                ALOGI("Update data section at %p with %d", ptr->codePtr, stream-ptr->codePtr);
#endif
                updateImmRMInst(ptr->codePtr, "", stream - ptr->codePtr);
                stream += sz;
            } else {
                dvmCompilerSetCodeAndDataCacheFull();
            }
            break;
        case OP_PACKED_SWITCH:
            updateImmRMInst(ptr->codePtr, "", stream-ptr->codePtr);
            sizePtr = (u2*)currentMethod->insns+tmpPC + 1 /*signature*/;
            entryPtr_bytecode = (s4*)(sizePtr + 1 /*size*/ + 2 /*firstKey*/);
            tSize = *(sizePtr);
            sz = tSize * 4;     /* expected size needed in stream */
            if ((stream + sz) < codeCacheEnd) {
                for(iVer = 0; iVer < tSize; iVer++) {
                    //update entries
                    s4 relativePC = *entryPtr_bytecode; //relative to ptr->offsetPC
                    //need stream, offsetPC,
                    int relativeNCG = getRelativeNCGForSwitch(relativePC+ptr->offsetPC, ptr->codePtr2);
#ifdef DEBUG_NCG_CODE_SIZE
                    ALOGI("Convert target from %d to %d", relativePC+ptr->offsetPC, relativeNCG);
#endif
                    *((s4*)stream) = relativeNCG;
                    stream += 4;
                    entryPtr_bytecode++;
                }
            } else {
                dvmCompilerSetCodeAndDataCacheFull();
            }
            break;
        case OP_SPARSE_SWITCH:
            updateImmRMInst(ptr->codePtr, "", stream-ptr->codePtr);
            sizePtr = (u2*)currentMethod->insns+tmpPC + 1 /*signature*/;
            s4* keyPtr_bytecode = (s4*)(sizePtr + 1 /*size*/);
            tSize = *(sizePtr);
            entryPtr_bytecode = (s4*)(keyPtr_bytecode + tSize);
            sz = tSize * (sizeof(s4) + 4); /* expected size needed in stream */
            if ((stream + sz) < codeCacheEnd) {
                memcpy(stream, keyPtr_bytecode, tSize*sizeof(s4));
                stream += tSize*sizeof(s4);
                for(iVer = 0; iVer < tSize; iVer++) {
                    //update entries
                    s4 relativePC = *entryPtr_bytecode; //relative to ptr->offsetPC
                    //need stream, offsetPC,
                    int relativeNCG = getRelativeNCGForSwitch(relativePC+ptr->offsetPC, ptr->codePtr2);
                    *((s4*)stream) = relativeNCG;
                    stream += 4;
                    entryPtr_bytecode++;
                }
            } else {
                dvmCompilerSetCodeAndDataCacheFull();
            }
            break;
        }

        //remove the item
        methodDataWorklist = ptr->nextItem;
        free(ptr);
        ptr = methodDataWorklist;
    }
    return 0;
}
void freeDataWorklist() {
    DataWorklist* ptr = methodDataWorklist;
    while(ptr != NULL) {
        methodDataWorklist = ptr->nextItem;
        free(ptr);
        ptr = methodDataWorklist;
    }
}

//////////////////////////
/*!
\brief check whether a branch target (specified by relative offset in bytecode) is already handled, if yes, return the size of the immediate; otherwise, call insertNCGWorklist.

If the branch target is not handled, call insertNCGWorklist, unknown is set to true, immSize is set to 32.

If the branch target is handled, call estOpndSizeFromImm to set immSize for jump instruction, returns the value of the immediate
*/
int getRelativeNCG(s4 tmp, JmpCall_type type, bool* unknown, OpndSize* size) {//tmp: relativePC
    int tmpNCG = getLabelOffset (tmp);

    *unknown = false;
    if(tmpNCG <0) {
        *unknown = true;
#ifdef SUPPORT_IMM_16
        *size = OpndSize_16;
#else
        *size = OpndSize_32;
#endif
        insertNCGWorklist(tmp, *size);
        return 0;
    }
    int offsetNCG2 = stream - streamMethodStart;
#ifdef DEBUG_NCG
    ALOGI("Goto backward @ %p offsetPC %d relativePC %d offsetNCG %d relativeNCG %d", stream, offsetPC, tmp, offsetNCG2, tmpNCG-offsetNCG2);
#endif
    int relativeOff = tmpNCG - offsetNCG2;
    *size = estOpndSizeFromImm(relativeOff);
    return relativeOff - getJmpCallInstSize(*size, type);
}
/*!
\brief a helper function to handle backward branch

input: jump target in %eax; at end of the function, jump to %eax
*/
int common_backwardBranch() {
    if (insertLabel("common_backwardBranch", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
     int startStreamPtr = (int)stream;
#endif

    spill_reg(PhysicalReg_EAX, true);
    call("common_periodicChecks_entry");
    unspill_reg(PhysicalReg_EAX, true);
    unconditional_jump_reg(PhysicalReg_EAX, true);

#if defined(VTUNE_DALVIK)
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_backwardBranch");
    }
#endif
    return 0;
}
#if !defined(WITH_JIT)
/*!
\brief common code to handle GOTO

If it is a backward branch, call common_periodicChecks4 to handle GC request.
Since this is the end of a basic block, globalVREndOfBB are called right before the jump instruction.
*/
int common_goto(s4 tmp) { //tmp: relativePC
    int retCode = 0;
    if(tmp < 0) {
#ifdef ENABLE_TRACING
#if !defined(TRACING_OPTION2)
        insertMapWorklist(offsetPC + tmp, mapFromBCtoNCG[offsetPC+tmp], 1);
#endif
        //(target offsetPC * 2)
        move_imm_to_reg(OpndSize_32, 2*(offsetPC+tmp), PhysicalReg_EDX, true);
#endif
        //call( ... ) will dump VRs to memory first
        //potential garbage collection will work as designed
        call_helper_API("common_periodicChecks4");
    }
    retCode = handleRegistersEndOfBB(true);
    if (retCode < 0)
        return retCode;
    bool unknown;
    OpndSize size;
    int relativeNCG = tmp;
    if(!gDvmJit.scheduling)
        relativeNCG = getRelativeNCG(tmp, JmpCall_uncond, &unknown, &size);
    unconditional_jump_int(relativeNCG, size);
    return 0;
}
//the common code to lower a if bytecode
int common_if(s4 tmp, ConditionCode cc_next, ConditionCode cc_taken) {
    if(tmp < 0) { //backward
        conditional_jump(cc_next, ".if_next", true);
        common_goto(tmp);
        if (insertLabel(".if_next", true) == -1)
            return -1;
    }
    else {
        //if(tmp < 0) ALOGI("skip periodicCheck for if");
        bool unknown;
        OpndSize size;
        int relativeNCG = tmp;
        if(!gDvmJit.scheduling)
            relativeNCG = getRelativeNCG(tmp, JmpCall_cond, &unknown, &size); //must be known
        conditional_jump_int(cc_taken, relativeNCG, size); //CHECK
    }
    return 0;
}
#else

int generateConditionalJumpToTakenBlock (ConditionCode takenCondition)
{
    // A basic block whose last bytecode is "if" must have two children
    assert (traceCurrentBB->taken != NULL);
    assert (traceCurrentBB->fallThrough != NULL);

    BasicBlock_O1 * takenBB =
            reinterpret_cast<BasicBlock_O1 *>(traceCurrentBB->taken);
    BasicBlock_O1 * fallThroughBB =
            reinterpret_cast<BasicBlock_O1 *>(traceCurrentBB->fallThrough);

    // When assert version is disabled, fallthroughBB is not used
    (void) fallThroughBB;

    // We should always have a pre backward block before backward chaining cell
    // so we can assert that here.
    if (takenBB->blockType == kChainingCellBackwardBranch)
    {
        ALOGI("JIT_INFO: No pre-backward on taken branch");
        SET_JIT_ERROR(kJitErrorTraceFormation);
        return -1;
    }

    if (fallThroughBB->blockType == kChainingCellBackwardBranch)
    {
        ALOGI("JIT_INFO: No pre-backward on fallThrough branch");
        SET_JIT_ERROR(kJitErrorTraceFormation);
        return -1;
    }

    // The prebackward block should always be the taken branch
    if (fallThroughBB->blockType == kPreBackwardBlock)
    {
        ALOGI("JIT_INFO: Pre-backward branch is fallThrough");
        SET_JIT_ERROR(kJitErrorTraceFormation);
        return -1;
    }

    // Since we have reached the end of basic block, let's handle registers at
    // end of BB without actually syncing the state. We sync the state below
    // when we handle each child
    handleRegistersEndOfBB (false);

    // So if we have a Prebackward block, we need to satisfy associations
    // of loop entry
    if (takenBB->blockType == kPreBackwardBlock)
    {
        // The child of the prebackward block should always be backward
        // chaining cell so it should never be null.
        assert (takenBB->fallThrough != 0);

        BasicBlock_O1 * backward =
                reinterpret_cast<BasicBlock_O1 *> (takenBB->fallThrough);

        //This must be a backward branch chaining cell
        assert (backward->blockType == kChainingCellBackwardBranch);

        //Backward CC must always have as child the loop entry
        assert (backward->fallThrough != 0);

        //Get the child
        BasicBlock_O1 * loopEntry =
                reinterpret_cast<BasicBlock_O1 *> (backward->fallThrough);

        //Paranoid. We want to make sure that the loop entry has been
        //already handled.
        if (loopEntry->associationTable.hasBeenFinalized() == false)
        {
            ALOGI("JIT_INFO: Loop entry still not finalized at common_if");
            SET_JIT_ERROR(kJitErrorTraceFormation);
            return -1;
        }

        //Just in case the current BB has any spill requests, let's handle them
        //before we satisfy BB associations
        if (AssociationTable::handleSpillRequestsFromME (currentBB) == false)
        {
            return -1;
        }

        //Now we want to satisfy the associations of the loop entry.
        //We also inform satisfyBBAssociations that this is a backward branch.
        if (AssociationTable::satisfyBBAssociations (backward, loopEntry,
                true) == false)
        {
            return -1;
        }
    }

    // First sync with the taken child
    if (AssociationTable::createOrSyncTable (currentBB, false) == false)
    {
        return -1;
    }

    //Now generate conditional jump to taken branch
    condJumpToBasicBlock (takenCondition, takenBB->id, doesJumpToBBNeedAlignment (takenBB));

    // Now sync with the fallthrough child
    if (AssociationTable::createOrSyncTable (currentBB, true) == false)
    {
        return -1;
    }

    // Return success
    return 1;
}
#endif

/*!
\brief helper function to handle null object error

*/
int common_errNullObject() {
    if (insertLabel("common_errNullObject", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, (int) gDvm.exNullPointerException, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errNullObject");
    }
#endif

    return 0;
}
/*!
\brief helper function to handle string index error

*/
int common_errStringIndexOutOfBounds() {
    if (insertLabel("common_errStringIndexOutOfBounds", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, (int)gDvm.exStringIndexOutOfBoundsException, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errStringIndexOutOfBounds");
    }
#endif
    return 0;
}

/*!
\brief helper function to handle array index error

*/
int common_errArrayIndex() {
    if (insertLabel("common_errArrayIndex", false) == -1)
        return -1;

    //Get call back
    void (*backEndSymbolCreationCallback) (const char *, void *) =
        gDvmJit.jitFramework.backEndSymbolCreationCallback;

    if (backEndSymbolCreationCallback != 0)
    {
        backEndSymbolCreationCallback ("common_errArrayIndex", (void*) stream);
    }

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, LstrArrayIndexException, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errArrayIndex");
    }
#endif
    return 0;
}
/*!
\brief helper function to handle array store error

*/
int common_errArrayStore() {
    if (insertLabel("common_errArrayStore", false) == -1)
        return -1;
#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, LstrArrayStoreException, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errArrayStore");
    }
#endif
    return 0;
}
/*!
\brief helper function to handle negative array size error

*/
int common_errNegArraySize() {
    if (insertLabel("common_errNegArraySize", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif
    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, LstrNegativeArraySizeException, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errNegArraySize");
    }
#endif
    return 0;
}
/*!
\brief helper function to handle divide-by-zero error

*/
int common_errDivideByZero() {
    if (insertLabel("common_errDivideByZero", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif
    move_imm_to_reg(OpndSize_32, LstrDivideByZero, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, LstrArithmeticException, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errDivideByZero");
    }
#endif
    return 0;
}
/*!
\brief helper function to handle no such method error

*/
int common_errNoSuchMethod() {
    if (insertLabel("common_errNoSuchMethod", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif
    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true);
    move_imm_to_reg(OpndSize_32, LstrNoSuchMethodError, PhysicalReg_ECX, true);
    unconditional_jump("common_throw", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_errNoSuchMethod");
    }
#endif
    return 0;
}
int call_dvmFindCatchBlock();

#define P_GPR_1 PhysicalReg_ESI //self callee-saved
#define P_GPR_2 PhysicalReg_EBX //exception callee-saved
#define P_GPR_3 PhysicalReg_EAX //method that caused exception
/*!
\brief helper function common_exceptionThrown

*/
int common_exceptionThrown() {
    if (insertLabel("common_exceptionThrown", false) == -1)
        return -1;
#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif
    typedef void (*vmHelper)(int);
    vmHelper funcPtr = dvmJitToExceptionThrown;
    move_imm_to_reg(OpndSize_32, (int)funcPtr, C_SCRATCH_1, isScratchPhysical);
    unconditional_jump_reg(C_SCRATCH_1, isScratchPhysical);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_exceptionThrown");
    }
#endif
    return 0;
}
#undef P_GPR_1
#undef P_GPR_2
#undef P_GPR_3

/*!
\brief helper function to throw an exception with message

INPUT: obj_reg(%eax), exceptionPtrReg(%ecx)
SCRATCH: C_SCRATCH_1(%esi) & C_SCRATCH_2(%edx)
OUTPUT: no
*/
int throw_exception_message(int exceptionPtrReg, int obj_reg, bool isPhysical,
                            int startLR/*logical register index*/, bool startPhysical) {
    if (insertLabel("common_throw_message", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
 #endif
    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EDX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;

    move_mem_to_reg(OpndSize_32, OFFSETOF_MEMBER(Object, clazz), obj_reg, isPhysical, C_SCRATCH_1, isScratchPhysical);
    move_mem_to_reg(OpndSize_32, OFFSETOF_MEMBER(ClassObject, descriptor), C_SCRATCH_1, isScratchPhysical, C_SCRATCH_2, isScratchPhysical);
    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, C_SCRATCH_2, isScratchPhysical, 4, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, exceptionPtrReg, true, 0, PhysicalReg_ESP, true);
    call_dvmThrowWithMessage();
    load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    unconditional_jump("common_exceptionThrown", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_throw_message");
    }
#endif
    return 0;
}
/*!
\brief helper function to throw an exception

scratch: C_SCRATCH_1(%edx)
*/
int throw_exception(int exceptionPtrReg, int immReg,
                    int startLR/*logical register index*/, bool startPhysical) {
    if (insertLabel("common_throw", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    scratchRegs[0] = PhysicalReg_EDX; scratchRegs[1] = PhysicalReg_Null;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;

    load_effective_addr(-8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, immReg, true, 4, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, exceptionPtrReg, true, 0, PhysicalReg_ESP, true);
    call_dvmThrow();
    load_effective_addr(8, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    unconditional_jump("common_exceptionThrown", false);

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_throw");
    }
#endif
    return 0;
}

/**
 * @brief Generates jump for handling goto bytecode. It also ensures that it handles registers since it is end of BB.
 * @param mir The mir that represents the goto
 * @param currentBB The current basic block
 * @return Returns a positive value if successful.
 */
int op_goto (const MIR * mir, BasicBlock *currentBB)
{
    assert (mir->dalvikInsn.opcode == OP_GOTO
            || mir->dalvikInsn.opcode == OP_GOTO_16
            || mir->dalvikInsn.opcode == OP_GOTO_32);

    BasicBlock *targetBlock = currentBB->taken;

    //Paranoid
    if (targetBlock == 0)
    {
        return -1;
    }

    //We call it with true because we want to actually want to update
    //association tables of children and handle ME spill requests
    int retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    //Determine if jump needs alignment
    bool needAlignment = doesJumpToBBNeedAlignment(targetBlock);

    //Generate an unconditional jump to the basic block
    jumpToBasicBlock (targetBlock->id, needAlignment);

    //We are successful so return positive value
    return 1;
}

#define P_GPR_1 PhysicalReg_EBX

/*
 * @brief create a switchInfo for a switch bytecode and initialize switchInfo
   @param tSize the size of switch bytecode
 */
static void createSwitchInfo(u2 tSize, CompilationUnit_O1* cUnit)
{
    struct SwitchInfo *switchInfo = static_cast<SwitchInfo*>(dvmCompilerNew(sizeof(SwitchInfo), true));
    assert(switchInfo != 0);
    switchInfo->tSize = tSize;
    cUnit->setSwitchInfo(switchInfo);
}

/*
 * @brief create a switchInfoScheduler for a move instruction in switch bytecode lowering
   @param isFirst TRUE for the first move instruction which need to be patched
   @param offset offset of the immediate value from the start of instruction
 */
static SwitchInfoScheduler * createSwitchInfoScheduler(bool isFirst, int offset, CompilationUnit_O1* cUnit)
{
    struct SwitchInfoScheduler *switchInfoScheduler = static_cast<SwitchInfoScheduler*>(dvmCompilerNew(sizeof(SwitchInfoScheduler), false));
    assert(switchInfoScheduler!= 0);
    switchInfoScheduler->isFirst = isFirst;
    switchInfoScheduler->offset = offset;
    switchInfoScheduler->switchInfo = cUnit->getSwitchInfo();
    return switchInfoScheduler;
}

/**
 * @brief fill immediate value in switchInfo
 * @param immAddr immediate value address which need to be patched
 * @param isFirst TRUE for the first move instruction which need to be patched
 * @param cUnit O1 CompilationUnit
 */
static void fillSwitchInfo(char *immAddr, bool isFirst, CompilationUnit_O1 * cUnit)
{
    assert(cUnit->getSwitchInfo() != NULL);

    if (isFirst == true) {
        cUnit->getSwitchInfo()->immAddr = immAddr;
    }
    else {
        cUnit->getSwitchInfo()->immAddr2 = immAddr;
    }
}

/**
 * @brief Generate native code for bytecode packed-switch when number of
 *        switch cases less or equal than MAX_CHAINED_SWITCH_CASES
 * @param vA switch argument virtual register
 * @param tSize size of packed switch bytecode switch cases
 * @param firstKey first case value for packed switch bytecode
 * @param cUnit O1 CompilationUnit
 * @return value >=0 if successful code generated
 */
static int packedNormal(int vA, u2 tSize, s4 firstKey, CompilationUnit_O1* cUnit)
{
    int retCode = 0;

    SwitchInfoScheduler * switchInfoScheduler1 = createSwitchInfoScheduler(true, 1, cUnit);

    // get the switch argument
    get_virtual_reg(vA, OpndSize_32, 1, false);
    alu_binary_imm_reg(OpndSize_32, sub_opc, firstKey, 1, false); //idx
    fillSwitchInfo(stream+1, true, cUnit); // 1 is the offset to the immediate location

    // switch table address will be patched later here
    dump_imm_reg(Mnemonic_MOV, ATOM_NORMAL, OpndSize_32, 0, 2, false, LowOpndRegType_gp, false, switchInfoScheduler1);
    compare_imm_reg(OpndSize_32, tSize, 1, false);
    conditional_jump(Condition_GE, ".switch_default", true);
    rememberState(1);
    compare_imm_reg(OpndSize_32, 0, 1, false);
    transferToState(1);
    conditional_jump(Condition_L, ".switch_default", true);
    rememberState(2);

    load_effective_addr_scale(2, false, 1, false, 4, 2, false);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }
    dump_mem(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, 0, 2, false);

    transferToState(1);
    if (insertLabel(".switch_default", true) == -1) {
        return -1;
    }

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    // (2, false) hold the switch table address
    dump_mem(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, 4*tSize, 2, false);
    return 0;
}

/**
 * @brief Generate native code for bytecode packed-switch when number of
 *        switch cases greater than MAX_CHAINED_SWITCH_CASES
 * @param vA switch argument virtual register
 * @param tSize size of packed switch bytecode switch cases
 * @param firstKey first case value for packed switch bytecode
 * @param entries address of case handling offset area
 * @param dalvikPC program counter for Dalvik bytecode
 * @param cUnit O1 CompilationUnit
 * @return value >=0 if successful code generated
 */
static int packedBig(int vA, u2 tSize, s4 firstKey, const s4* entries, const u2* dalvikPC, CompilationUnit_O1 *cUnit)
{
    int retCode = 0;
    int maxChains = MIN(tSize, MAX_CHAINED_SWITCH_CASES);

    SwitchInfoScheduler * switchInfoScheduler1 = createSwitchInfoScheduler(true, 1, cUnit);
    SwitchInfoScheduler * switchInfoScheduler2 = createSwitchInfoScheduler(false, 1, cUnit);

    // get the switch argument
    get_virtual_reg(vA, OpndSize_32, 1, false);
    alu_binary_imm_reg(OpndSize_32, sub_opc, firstKey, 1, false); //idx
    compare_imm_reg(OpndSize_32, tSize, 1, false);
    conditional_jump(Condition_GE, ".switch_default", true);
    rememberState(1);
    compare_imm_reg(OpndSize_32, 0, 1, false);
    transferToState(1);
    conditional_jump(Condition_L, ".switch_default", true);
    compare_imm_reg(OpndSize_32, MAX_CHAINED_SWITCH_CASES, 1, false);
    conditional_jump(Condition_GE, ".switch_nochain", true);
    rememberState(2);

    fillSwitchInfo(stream+1, true, cUnit); // 1 is the offset to the immediate location

    // switch table address will be patched later here
    dump_imm_reg(Mnemonic_MOV, ATOM_NORMAL, OpndSize_32, 0, 2, false, LowOpndRegType_gp, false, switchInfoScheduler1);
    load_effective_addr_scale(2, false, 1, false, 4, 2, false);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }
    dump_mem(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, 0, 2, false);

    transferToState(1);
    if (insertLabel(".switch_default", true) == -1) {
        return -1;
    }

    fillSwitchInfo(stream+1, false, cUnit); // 1 is the offset to the immediate location
    dump_imm_reg(Mnemonic_MOV, ATOM_NORMAL, OpndSize_32, 0, 2, false, LowOpndRegType_gp, false, switchInfoScheduler2);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    dump_mem(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, 4*maxChains, 2, false);
    goToState(2);
    if (insertLabel(".switch_nochain", true) == -1) {
        return -1;
    }

    // Compute rPC based on matching index
    alu_binary_imm_reg(OpndSize_32, shl_opc, 2, 1, false);
    alu_binary_imm_reg(OpndSize_32, add_opc, (int)entries, 1, false);
    move_mem_to_reg(OpndSize_32, 0, 1, false, PhysicalReg_EAX, true);
    alu_binary_imm_reg(OpndSize_32, shl_opc, 1, PhysicalReg_EAX, true);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    // save rPC in EAX
    alu_binary_imm_reg(OpndSize_32, add_opc, (int)dalvikPC, PhysicalReg_EAX, true);
    scratchRegs[0] = PhysicalReg_SCRATCH_2;

#if defined(WITH_JIT_TUNING)
    /* Fall back to interpreter after resolving address of switch target.
     * Indicate a kSwitchOverflow. Note: This is not an "overflow". But it helps
     * count the times we return from a Switch
     */
    move_imm_to_mem(OpndSize_32, kSwitchOverflow, 0, PhysicalReg_ESP, true);
#endif

    jumpToInterpNoChain();
    return 0;
}

/**
 * @brief Generate native code for bytecode packed-switch
 * @param mir bytecode representation
 * @param dalvikPC program counter for Dalvik bytecode
 * @param cUnit O1 CompilationUnit
 * @return value >= 0 when handled
 */
int op_packed_switch(const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1 *cUnit) {
    int retCode = 0;
    assert(mir->dalvikInsn.opcode == OP_PACKED_SWITCH);
    int vA = mir->dalvikInsn.vA;
    u4 tmp = mir->dalvikInsn.vB;

#ifdef DEBUG_EACH_BYTECODE
    u2 tSize = 0;
    s4 firstKey = 0;
    s4* entries = NULL;
#else
    u2* switchData = const_cast<u2 *>(dalvikPC) + (s4)tmp;
    if (*switchData++ != kPackedSwitchSignature) {
        /* should have been caught by verifier */
        dvmThrowInternalError(
                          "bad packed switch magic");
        return 0; //no_op
    }
    u2 tSize = *switchData++;
    assert(tSize > 0);
    s4 firstKey = *switchData++;
    firstKey |= (*switchData++) << 16;
    s4* entries = (s4*) switchData;
    assert(((u4)entries & 0x3) == 0);
#endif
    createSwitchInfo(tSize, cUnit);

    // normal switch case
    if (tSize <= MAX_CHAINED_SWITCH_CASES) {
        retCode = packedNormal(vA, tSize, firstKey, cUnit);
        if (retCode < 0) {
            return retCode;
        }
    }

    // big switch case
    else {
        retCode = packedBig(vA, tSize, firstKey, entries, dalvikPC, cUnit);
        if (retCode < 0) {
            return retCode;
        }
    }
    return 0;
}
#undef P_GPR_1

/**
 * @brief Generate native code for bytecode sparse-switch when number of
 *        switch cases greater than MAX_CHAINED_SWITCH_CASES
 * @param vA switch argument virtual register
 * @param tSize size of packed switch bytecode switch cases
 * @param keys case constants area in sparse-switch bytecode
 * @param entries case offset area in sparse-switch bytecode
 * @param dalvikPC program counter for Dalvik bytecode
 * @param cUnit O1 CompilationUnit
 * @return value >=0 if successful code generated
 */
static int sparseBig(int vA, u2 tSize, const s4* keys, const s4* entries, const u2* dalvikPC, CompilationUnit_O1* cUnit)
{
    int retCode = 0;
    int maxChains = MIN(tSize, MAX_CHAINED_SWITCH_CASES);

    SwitchInfoScheduler * switchInfoScheduler1 = createSwitchInfoScheduler(true, 1, cUnit);
    SwitchInfoScheduler * switchInfoScheduler2 = createSwitchInfoScheduler(false, 1, cUnit);

    // get the switch argument
    get_virtual_reg(vA, OpndSize_32, 1, false);
    load_effective_addr(-12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_imm_to_mem(OpndSize_32, tSize, 4, PhysicalReg_ESP, true);

    /* "keys" is constant for JIT
       it is the 1st argument to dvmJitHandleSparseSwitch */
    move_imm_to_mem(OpndSize_32, (int)keys, 0, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, 1, false, 8, PhysicalReg_ESP, true);

    scratchRegs[0] = PhysicalReg_SCRATCH_1;

    //return index in EAX where keys[index] == switch argument
    call_dvmJitLookUpBigSparseSwitch();
    load_effective_addr(12, PhysicalReg_ESP, true, PhysicalReg_ESP, true);

    compare_imm_reg(OpndSize_32, tSize, PhysicalReg_EAX, true);
    conditional_jump(Condition_GE, ".switch_default", true);
    rememberState(1);
    compare_imm_reg(OpndSize_32, MAX_CHAINED_SWITCH_CASES, PhysicalReg_EAX, true);
    conditional_jump(Condition_GE, ".switch_nochain", true);
    rememberState(2);

    fillSwitchInfo(stream+1, true, cUnit); // 1 is the offset to the immediate location
    dump_imm_reg(Mnemonic_MOV, ATOM_NORMAL, OpndSize_32, 0, 2, false, LowOpndRegType_gp, false, switchInfoScheduler1);
    load_effective_addr_scale(2, false, PhysicalReg_EAX, true, 4, 2, false);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
       //Just pass along error information
       return retCode;
    }
    dump_mem(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, 0, 2, false);

    goToState(1);
    if (insertLabel(".switch_default", true) == -1)
        return -1;

    fillSwitchInfo(stream+1, false, cUnit); // 1 is the offset to the immediate location
    dump_imm_reg(Mnemonic_MOV, ATOM_NORMAL, OpndSize_32, 0, 2, false, LowOpndRegType_gp, false, switchInfoScheduler2);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    dump_mem(Mnemonic_JMP, ATOM_NORMAL, OpndSize_32, 4*maxChains, 2, false);
    goToState(2);
    if (insertLabel(".switch_nochain", true) == -1)
        return -1;

    // Compute rPC based on matching index
    alu_binary_imm_reg(OpndSize_32, shl_opc, 2, PhysicalReg_EAX, true);
    alu_binary_imm_reg(OpndSize_32, add_opc, (int)entries, PhysicalReg_EAX, true);
    move_mem_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true, PhysicalReg_EAX, true);
    alu_binary_imm_reg(OpndSize_32, shl_opc, 1, PhysicalReg_EAX, true);

    //We are done using the VRs and it is end of BB, so we handle it right now
    retCode = handleRegistersEndOfBB (true);
    if (retCode < 0)
    {
        //Just pass along error information
        return retCode;
    }

    // save rPC in EAX
    alu_binary_imm_reg(OpndSize_32, add_opc, (int)dalvikPC, PhysicalReg_EAX, true);
    scratchRegs[0] = PhysicalReg_SCRATCH_2;
#if defined(WITH_JIT_TUNING)
    /* Fall back to interpreter after resolving address of switch target.
     * Indicate a kSwitchOverflow. Note: This is not an "overflow". But it helps
     * count the times we return from a Switch
     */
    move_imm_to_mem(OpndSize_32, kSwitchOverflow, 0, PhysicalReg_ESP, true);
#endif

    jumpToInterpNoChain();
    return 0;
}

/**
 * @brief Generate native code for bytecode sparse-switch
 * @param mir bytecode representation
 * @param dalvikPC program counter for Dalvik bytecode
 * @param cUnit O1 CompilationUnit
 * @return value >= 0 when handled
 */
int op_sparse_switch(const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit) {
    int retCode = 0;
    assert(mir->dalvikInsn.opcode == OP_SPARSE_SWITCH);
    int vA = mir->dalvikInsn.vA;
    u4 tmp = mir->dalvikInsn.vB;

#ifdef DEBUG_EACH_BYTECODE
    u2 tSize = 0;
    const s4* keys = NULL;
    s4* entries = NULL;
#else
    u2* switchData = const_cast<u2 *>(dalvikPC) + (s4)tmp;

    if (*switchData++ != kSparseSwitchSignature) {
        /* should have been caught by verifier */
        dvmThrowInternalError(
                          "bad sparse switch magic");
        return 0; //no_op
    }
    u2 tSize = *switchData++;
    assert(tSize > 0);
    const s4* keys = (const s4*) switchData;
    const s4* entries = keys + tSize;
    assert(((u4)keys & 0x3) == 0);
    assert((((u4) ((s4*) switchData + tSize)) & 0x3) == 0);
#endif
    createSwitchInfo(tSize, cUnit);

    // normal switch case
    if (tSize <= MAX_CHAINED_SWITCH_CASES) {
        SwitchInfoScheduler * switchInfoScheduler = createSwitchInfoScheduler(true, 3, cUnit);

        // switch argument
        get_virtual_reg(vA, OpndSize_32, 1, false);
        load_effective_addr(-16, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
        move_imm_to_mem(OpndSize_32, tSize, 8, PhysicalReg_ESP, true);

        /* "keys" is constant for JIT
           it is the 1st argument to dvmJitHandleSparseSwitch */
        move_imm_to_mem(OpndSize_32, (int)keys, 4, PhysicalReg_ESP, true);
        move_reg_to_mem(OpndSize_32, 1, false, 12, PhysicalReg_ESP, true);
        fillSwitchInfo(stream+3, true, cUnit); // 3 is the offset to the immediate location
        dump_imm_mem_noalloc(Mnemonic_MOV, OpndSize_32, 0, 0, PhysicalReg_ESP, true, MemoryAccess_Unknown, -1, switchInfoScheduler);

        scratchRegs[0] = PhysicalReg_SCRATCH_1;

        // call dvmJitHandleSparseSwitch to return the value that the execution will jump to,
        // either normal chaining cell or target trace
        call_dvmJitHandleSparseSwitch();
        load_effective_addr(16, PhysicalReg_ESP, true, PhysicalReg_ESP, true);

        //We are done using the VRs and it is end of BB, so we handle it right now
        retCode = handleRegistersEndOfBB (true);
        if (retCode < 0)
        {
            //Just pass along error information
            return retCode;
        }
        unconditional_jump_reg(PhysicalReg_EAX, true);
    }

    // big switch case
    else {
        int retCode = sparseBig(vA, tSize, keys, entries, dalvikPC, cUnit);
        if (retCode < 0) {
            return retCode;
        }
    }
    return 0;
}

#define P_GPR_1 PhysicalReg_EBX

/**
 * @brief Generate native code for bytecode if-eq
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_eq(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_EQ);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;

    get_virtual_reg(vA, OpndSize_32, 1, false);
    compare_VR_reg(OpndSize_32, vB, 1, false);

    return generateConditionalJumpToTakenBlock (Condition_E);
}

/**
 * @brief Generate native code for bytecode if-ne
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_ne(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_NE);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;

    get_virtual_reg(vA, OpndSize_32, 1, false);
    compare_VR_reg(OpndSize_32, vB, 1, false);

    return generateConditionalJumpToTakenBlock (Condition_NE);
}

/**
 * @brief Generate native code for bytecode if-lt
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_lt(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_LT);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;

    get_virtual_reg(vA, OpndSize_32, 1, false);
    compare_VR_reg(OpndSize_32, vB, 1, false);

    return generateConditionalJumpToTakenBlock (Condition_L);
}

/**
 * @brief Generate native code for bytecode if-ge
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_ge(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_GE);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;

    get_virtual_reg(vA, OpndSize_32, 1, false);
    compare_VR_reg(OpndSize_32, vB, 1, false);

    return generateConditionalJumpToTakenBlock (Condition_GE);
}

/**
 * @brief Generate native code for bytecode if-gt
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_gt(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_GT);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;

    get_virtual_reg(vA, OpndSize_32, 1, false);
    compare_VR_reg(OpndSize_32, vB, 1, false);

    return generateConditionalJumpToTakenBlock (Condition_G);
}

/**
 * @brief Generate native code for bytecode if-le
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_le(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_LE);
    int vA = mir->dalvikInsn.vA;
    int vB = mir->dalvikInsn.vB;

    get_virtual_reg(vA, OpndSize_32, 1, false);
    compare_VR_reg(OpndSize_32, vB, 1, false);

    return generateConditionalJumpToTakenBlock (Condition_LE);
}
#undef P_GPR_1

/**
 * @brief Generate native code for bytecode if-eqz
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_eqz(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_EQZ);
    int vA = mir->dalvikInsn.vA;

    compare_imm_VR(OpndSize_32, 0, vA);

    return generateConditionalJumpToTakenBlock (Condition_E);
}

/**
 * @brief Generate native code for bytecode if-nez
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_nez(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_NEZ);
    int vA = mir->dalvikInsn.vA;

    compare_imm_VR(OpndSize_32, 0, vA);

    return generateConditionalJumpToTakenBlock (Condition_NE);
}

/**
 * @brief Generate native code for bytecode if-ltz
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_ltz(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_LTZ);
    int vA = mir->dalvikInsn.vA;

    compare_imm_VR(OpndSize_32, 0, vA);

    return generateConditionalJumpToTakenBlock (Condition_L);
}

/**
 * @brief Generate native code for bytecode if-gez
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_gez(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_GEZ);
    int vA = mir->dalvikInsn.vA;

    compare_imm_VR(OpndSize_32, 0, vA);

    return generateConditionalJumpToTakenBlock (Condition_GE);
}

/**
 * @brief Generate native code for bytecode if-gtz
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_gtz(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_GTZ);
    int vA = mir->dalvikInsn.vA;

    compare_imm_VR(OpndSize_32, 0, vA);

    return generateConditionalJumpToTakenBlock (Condition_G);
}

/**
 * @brief Generate native code for bytecode if-lez
 * @param mir bytecode representation
 * @return value >= 0 when handled
 */
int op_if_lez(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_IF_LEZ);
    int vA = mir->dalvikInsn.vA;

    compare_imm_VR(OpndSize_32, 0, vA);

    return generateConditionalJumpToTakenBlock (Condition_LE);
}

#define P_GPR_1 PhysicalReg_ECX
#define P_GPR_2 PhysicalReg_EBX
/*!
\brief helper function common_periodicChecks4 to check GC request
BCOffset in %edx
*/
int common_periodicChecks4() {
    if (insertLabel("common_periodicChecks4", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

#if (!defined(ENABLE_TRACING))
    get_self_pointer(PhysicalReg_ECX, true);
    move_mem_to_reg(OpndSize_32, offsetof(Thread, suspendCount), PhysicalReg_ECX, true, PhysicalReg_EAX, true);
    compare_imm_reg(OpndSize_32, 0, PhysicalReg_EAX, true); //suspendCount
    conditional_jump(Condition_NE, "common_handleSuspend4", true); //called once
    x86_return();

    if (insertLabel("common_handleSuspend4", true) == -1)
        return -1;
    push_reg_to_stack(OpndSize_32, PhysicalReg_ECX, true);
    call_dvmCheckSuspendPending();
    load_effective_addr(4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    x86_return();

#else
    ///////////////////
    //get debuggerActive: 3 memory accesses, and $7
    move_mem_to_reg(OpndSize_32, offGlue_pSelfSuspendCount, PhysicalReg_Glue, true, P_GPR_1, true);
    move_mem_to_reg(OpndSize_32, offGlue_pIntoDebugger, PhysicalReg_Glue, true, P_GPR_2, true);

    compare_imm_mem(OpndSize_32, 0, 0, P_GPR_1, true); //suspendCount
    conditional_jump(Condition_NE, "common_handleSuspend4_1", true); //called once

    compare_imm_mem(OpndSize_32, 0, 0, P_GPR_2, true); //debugger active

    conditional_jump(Condition_NE, "common_debuggerActive4", true);

    //recover registers and return
    x86_return();

    if (insertLabel("common_handleSuspend4_1", true) == -1)
        return -1;
    push_mem_to_stack(OpndSize_32, offGlue_self, PhysicalReg_Glue, true);
    call_dvmCheckSuspendPending();
    load_effective_addr(4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    x86_return();

    if (insertLabel("common_debuggerActive4", true) == -1)
        return -1;
    //%edx: offsetBC (at run time, get method->insns_bytecode, then calculate BCPointer)
    move_mem_to_reg(OpndSize_32, offGlue_method, PhysicalReg_Glue, true, P_GPR_1, true);
    move_mem_to_reg(OpndSize_32, offMethod_insns_bytecode, P_GPR_1, true, P_GPR_2, true);
    alu_binary_reg_reg(OpndSize_32, add_opc, P_GPR_2, true, PhysicalReg_EDX, true);
    move_imm_to_mem(OpndSize_32, 0, offGlue_entryPoint, PhysicalReg_Glue, true);
    unconditional_jump("common_gotoBail", false); //update glue->rPC with edx
#endif

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_periodicChecks4");
    }
#endif
    return 0;
}
//input: %edx PC adjustment
//CHECK: should %edx be saved before calling dvmCheckSuspendPending?
/*!
\brief helper function common_periodicChecks_entry to check GC request

*/
int common_periodicChecks_entry() {
    if (insertLabel("common_periodicChecks_entry", false) == -1)
        return -1;
#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif
    scratchRegs[0] = PhysicalReg_ESI; scratchRegs[1] = PhysicalReg_EAX;
    scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;
    get_suspendCount(P_GPR_1, true);

    //get debuggerActive: 3 memory accesses, and $7
#if 0 //defined(WITH_DEBUGGER)
    get_debuggerActive(P_GPR_2, true);
#endif

    compare_imm_reg(OpndSize_32, 0, P_GPR_1, true); //suspendCount
    conditional_jump(Condition_NE, "common_handleSuspend", true); //called once

#if 0 //defined(WITH_DEBUGGER)
#ifdef NCG_DEBUG
    compare_imm_reg(OpndSize_32, 0, P_GPR_2, true); //debugger active
    conditional_jump(Condition_NE, "common_debuggerActive", true);
#endif
#endif

    //recover registers and return
    x86_return();
    if (insertLabel("common_handleSuspend", true) == -1)
        return -1;
    get_self_pointer(P_GPR_1, true);
    load_effective_addr(-4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    move_reg_to_mem(OpndSize_32, P_GPR_1, true, 0, PhysicalReg_ESP, true);
    call_dvmCheckSuspendPending();
    load_effective_addr(4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    x86_return();
#ifdef NCG_DEBUG
    if (insertLabel("common_debuggerActive", true) == -1)
        return -1;
    //adjust PC!!! use 0(%esp) TODO
    set_glue_entryPoint_imm(0); //kInterpEntryInstr);
    unconditional_jump("common_gotoBail", false);
#endif

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_periodicChecks_entry");
    }
#endif

    return 0;
}
#undef P_GPR_1
#undef P_GPR_2
/*!
\brief helper function common_gotoBail
  input: %edx: BCPointer %esi: Glue
  set %eax to 1 (switch interpreter = true), recover the callee-saved registers and return
*/
int common_gotoBail(void) {
    if (insertLabel("common_gotoBail", false) == -1)
        return -1;

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    //scratchRegs[0] = PhysicalReg_EDX; scratchRegs[1] = PhysicalReg_ESI;
    //scratchRegs[2] = PhysicalReg_Null; scratchRegs[3] = PhysicalReg_Null;
    get_self_pointer(PhysicalReg_EAX, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_FP, true, offsetof(Thread, interpSave.curFrame), PhysicalReg_EAX, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EDX, true, offsetof(Thread, interpSave.pc), PhysicalReg_EAX, true);

    move_mem_to_reg(OpndSize_32, offsetof(Thread, interpSave.bailPtr), PhysicalReg_EAX, true, PhysicalReg_ESP, true);
    move_reg_to_reg(OpndSize_32, PhysicalReg_ESP, true, PhysicalReg_EBP, true);
    load_effective_addr(FRAME_SIZE-4, PhysicalReg_EBP, true, PhysicalReg_EBP, true);
    move_imm_to_reg(OpndSize_32, 1, PhysicalReg_EAX, true); //return value
    move_mem_to_reg(OpndSize_32, -4, PhysicalReg_EBP, true, PhysicalReg_EDI, true);
    move_mem_to_reg(OpndSize_32, -8, PhysicalReg_EBP, true, PhysicalReg_ESI, true);
    move_mem_to_reg(OpndSize_32, -12, PhysicalReg_EBP, true, PhysicalReg_EBX, true);
    move_reg_to_reg(OpndSize_32, PhysicalReg_EBP, true, PhysicalReg_ESP, true);
    move_mem_to_reg(OpndSize_32, 0, PhysicalReg_ESP, true, PhysicalReg_EBP, true);
    load_effective_addr(4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    x86_return();

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_gotoBail");
    }
#endif
    return 0;
}
/*!
\brief helper function common_gotoBail_0

  set %eax to 0, recover the callee-saved registers and return
*/
int common_gotoBail_0(void) {
    if (insertLabel("common_gotoBail_0", false) == -1)
        return -1;

    //Get call back
    void (*backEndSymbolCreationCallback) (const char *, void *) =
        gDvmJit.jitFramework.backEndSymbolCreationCallback;

    if (backEndSymbolCreationCallback != 0)
    {
        backEndSymbolCreationCallback ("common_gotoBail_0", (void*) stream);
    }

#if defined VTUNE_DALVIK
    int startStreamPtr = (int)stream;
#endif

    get_self_pointer(PhysicalReg_EAX, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_FP, true, offsetof(Thread, interpSave.curFrame), PhysicalReg_EAX, true);
    move_reg_to_mem(OpndSize_32, PhysicalReg_EDX, true, offsetof(Thread, interpSave.pc), PhysicalReg_EAX, true);

    /*
    movl    offThread_bailPtr(%ecx),%esp # Restore "setjmp" esp
    movl    %esp,%ebp
    addl    $(FRAME_SIZE-4), %ebp       # Restore %ebp at point of setjmp
    movl    EDI_SPILL(%ebp),%edi
    movl    ESI_SPILL(%ebp),%esi
    movl    EBX_SPILL(%ebp),%ebx
    movl    %ebp, %esp                   # strip frame
    pop     %ebp                         # restore caller's ebp
    ret                                  # return to dvmMterpStdRun's caller
    */
    move_mem_to_reg(OpndSize_32, offsetof(Thread, interpSave.bailPtr), PhysicalReg_EAX, true, PhysicalReg_ESP, true);
    move_reg_to_reg(OpndSize_32, PhysicalReg_ESP, true, PhysicalReg_EBP, true);
    load_effective_addr(FRAME_SIZE-4, PhysicalReg_EBP, true, PhysicalReg_EBP, true);
    move_imm_to_reg(OpndSize_32, 0, PhysicalReg_EAX, true); //return value
    move_mem_to_reg(OpndSize_32, -4, PhysicalReg_EBP, true, PhysicalReg_EDI, true);
    move_mem_to_reg(OpndSize_32, -8, PhysicalReg_EBP, true, PhysicalReg_ESI, true);
    move_mem_to_reg(OpndSize_32, -12, PhysicalReg_EBP, true, PhysicalReg_EBX, true);
    move_reg_to_reg(OpndSize_32, PhysicalReg_EBP, true, PhysicalReg_ESP, true);
    move_mem_to_reg(OpndSize_32, 0, PhysicalReg_ESP, true, PhysicalReg_EBP, true);
    load_effective_addr(4, PhysicalReg_ESP, true, PhysicalReg_ESP, true);
    x86_return();

#if defined VTUNE_DALVIK
    if(gDvmJit.vtuneInfo != kVTuneInfoDisabled) {
        int endStreamPtr = (int)stream;
        sendLabelInfoToVTune(startStreamPtr, endStreamPtr, "common_gotoBail_0");
    }
#endif
    return 0;
}
