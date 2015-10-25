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


/*! \file Lower.cpp
    \brief This file implements the high-level wrapper for lowering

*/

#include "CompilationErrorLCG.h"
#include "CompilationUnit.h"
#include "libdex/DexOpcodes.h"
#include "libdex/DexFile.h"
#include <math.h>
#include <sys/mman.h>
#include "Translator.h"
#include "Lower.h"
#include "enc_wrapper.h"
#include "vm/mterp/Mterp.h"
#include "NcgHelper.h"
#include "libdex/DexCatch.h"
#include "compiler/CompilerIR.h"
#if defined VTUNE_DALVIK
#include "compiler/codegen/x86/VTuneSupportX86.h"
#endif
#include "Singleton.h"
#include "ExceptionHandling.h"
#include "compiler/Dataflow.h"

//statistics for optimization
int num_removed_nullCheck;

PhysicalReg scratchRegs[4];

LowOp* ops[BUFFER_SIZE];
LowOp* op;
u2* rPC; //PC pointer to bytecode
int offsetPC/*offset in bytecode*/, offsetNCG/*byte offset in native code*/;
int ncg_rPC;
//! map from PC in bytecode to PC in native code
int mapFromBCtoNCG[BYTECODE_SIZE_PER_METHOD]; //initially mapped to -1
char* streamStart = NULL; //start of the Pure CodeItem?, not include the global symbols
char* streamCode = NULL; //start of the Pure CodeItem?, not include the global symbols
char* streamMethodStart; //start of the method
char* stream; //current stream pointer
Method* currentMethod = NULL;
int currentExceptionBlockIdx = -1;
BasicBlock* traceCurrentBB = NULL;
JitMode traceMode = kJitTrace;
CompilationUnit_O1 *gCompilationUnit;

int common_invokeArgsDone(ArgsDoneType);

//data section of .ia32:
char globalData[128];

char strClassCastException[] = "Ljava/lang/ClassCastException;";
char strInstantiationError[] = "Ljava/lang/InstantiationError;";
char strInternalError[] = "Ljava/lang/InternalError;";
char strFilledNewArrayNotImpl[] = "filled-new-array only implemented for 'int'";
char strArithmeticException[] = "Ljava/lang/ArithmeticException;";
char strArrayIndexException[] = "Ljava/lang/ArrayIndexOutOfBoundsException;";
char strArrayStoreException[] = "Ljava/lang/ArrayStoreException;";
char strDivideByZero[] = "divide by zero";
char strNegativeArraySizeException[] = "Ljava/lang/NegativeArraySizeException;";
char strNoSuchMethodError[] = "Ljava/lang/NoSuchMethodError;";
char strNullPointerException[] = "Ljava/lang/NullPointerException;";
char strStringIndexOutOfBoundsException[] = "Ljava/lang/StringIndexOutOfBoundsException;";

int LstrClassCastExceptionPtr, LstrInstantiationErrorPtr, LstrInternalError, LstrFilledNewArrayNotImpl;
int LstrArithmeticException, LstrArrayIndexException, LstrArrayStoreException, LstrStringIndexOutOfBoundsException;
int LstrDivideByZero, LstrNegativeArraySizeException, LstrNoSuchMethodError, LstrNullPointerException;
int LdoubNeg, LvaluePosInfLong, LvalueNegInfLong, LvalueNanLong, LshiftMask, Lvalue64, L64bits, LintMax, LintMin;

void initConstDataSec() {
    char* tmpPtr = globalData;

    LdoubNeg = (int)tmpPtr;
    *((u4*)tmpPtr) = 0x00000000;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0x80000000;
    tmpPtr += sizeof(u4);

    // 16 byte aligned
    tmpPtr = align(tmpPtr, 16);
    LvaluePosInfLong = (int)tmpPtr;
    *((u4*)tmpPtr) = 0xFFFFFFFF;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0x7FFFFFFF;
    tmpPtr += sizeof(u4);

    LvalueNegInfLong = (int)tmpPtr;
    *((u4*)tmpPtr) = 0x00000000;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0x80000000;
    tmpPtr += sizeof(u4);

    LvalueNanLong = (int)tmpPtr;
    *((u4*)tmpPtr) = 0;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0;
    tmpPtr += sizeof(u4);

    LshiftMask = (int)tmpPtr;
    *((u4*)tmpPtr) = 0x3f;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0;
    tmpPtr += sizeof(u4);

    Lvalue64 = (int)tmpPtr;
    *((u4*)tmpPtr) = 0x40;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0;
    tmpPtr += sizeof(u4);

    L64bits = (int)tmpPtr;
    *((u4*)tmpPtr) = 0xFFFFFFFF;
    tmpPtr += sizeof(u4);
    *((u4*)tmpPtr) = 0xFFFFFFFF;
    tmpPtr += sizeof(u4);

    LintMin = (int)tmpPtr;
    *((u4*)tmpPtr) = 0x80000000;
    tmpPtr += sizeof(u4);

    LintMax = (int)tmpPtr;
    *((u4*)tmpPtr) = 0x7FFFFFFF;
    tmpPtr += sizeof(u4);

    LstrClassCastExceptionPtr = (int)strClassCastException;
    LstrInstantiationErrorPtr = (int)strInstantiationError;
    LstrInternalError = (int)strInternalError;
    LstrFilledNewArrayNotImpl = (int)strFilledNewArrayNotImpl;
    LstrArithmeticException = (int)strArithmeticException;
    LstrArrayIndexException = (int)strArrayIndexException;
    LstrArrayStoreException = (int)strArrayStoreException;
    LstrDivideByZero = (int)strDivideByZero;
    LstrNegativeArraySizeException = (int)strNegativeArraySizeException;
    LstrNoSuchMethodError = (int)strNoSuchMethodError;
    LstrNullPointerException = (int)strNullPointerException;
    LstrStringIndexOutOfBoundsException = (int)strStringIndexOutOfBoundsException;
}

//declarations of functions used in this file
int spill_reg(int reg, bool isPhysical);
int unspill_reg(int reg, bool isPhysical);

int const_string_resolve();
int sget_sput_resolve();
int new_instance_needinit();
int new_instance_abstract();
int invoke_virtual_resolve();
int invoke_direct_resolve();
int invoke_static_resolve();
int filled_new_array_notimpl();
int resolve_class2(
                   int startLR/*logical register index*/, bool isPhysical, int indexReg/*const pool index*/,
                   bool indexPhysical,
                   int thirdArg);
int resolve_method2(
                    int startLR/*logical register index*/, bool isPhysical, int indexReg/*const pool index*/,
                    bool indexPhysical,
                    int thirdArg/*VIRTUAL*/);
int resolve_inst_field2(
                        int startLR/*logical register index*/, bool isPhysical,
                        int indexReg/*const pool index*/,
                        bool indexPhysical);
int resolve_static_field2(
                          int startLR/*logical register index*/, bool isPhysical,
                          int indexReg/*const pool index*/,
                          bool indexPhysical);

int invokeMethodNoRange_1_helper();
int invokeMethodNoRange_2_helper();
int invokeMethodNoRange_3_helper();
int invokeMethodNoRange_4_helper();
int invokeMethodNoRange_5_helper();
int invokeMethodRange_helper();

int invoke_virtual_helper();
int invoke_virtual_quick_helper();
int invoke_static_helper();
int invoke_direct_helper();
int new_instance_helper();
int sget_sput_helper(int flag);
int aput_obj_helper();
int aget_helper(int flag);
int aput_helper(int flag);
int monitor_enter_helper();
int monitor_exit_helper();
int throw_helper();
int const_string_helper();
int array_length_helper();
int invoke_super_helper();
int invoke_interface_helper();
int iget_iput_helper(int flag);
int check_cast_helper(bool instance);
int new_array_helper();

/*!
\brief dump helper functions

*/
int performCGWorklist() {
    int retCode = 0;
    filled_new_array_notimpl();
    freeShortMap();
    const_string_resolve();
    freeShortMap();

    resolve_class2(PhysicalReg_EAX, true, PhysicalReg_EAX, true, 0);
    freeShortMap();
    resolve_method2(PhysicalReg_EAX, true, PhysicalReg_EAX, true, METHOD_VIRTUAL);
    freeShortMap();
    resolve_method2(PhysicalReg_EAX, true, PhysicalReg_EAX, true, METHOD_DIRECT);
    freeShortMap();
    resolve_method2(PhysicalReg_EAX, true, PhysicalReg_EAX, true, METHOD_STATIC);
    freeShortMap();
    resolve_inst_field2(PhysicalReg_EAX, true, PhysicalReg_EAX, true);
    freeShortMap();
    resolve_static_field2(PhysicalReg_EAX, true, PhysicalReg_EAX, true);
    freeShortMap();
    throw_exception_message(PhysicalReg_ECX, PhysicalReg_EAX, true, PhysicalReg_Null, true);
    freeShortMap();
    throw_exception(PhysicalReg_ECX, PhysicalReg_EAX, PhysicalReg_Null, true);
    freeShortMap();
    retCode = new_instance_needinit();
    freeShortMap();
    return retCode;
}

int aput_object_count;
int common_periodicChecks_entry();
int common_periodicChecks4();

#if 0 /* Commented out because it is dead code. If re-enabling, this needs to be updated
         to take MIR as parameter */
/*!
\brief for debugging purpose, dump the sequence of native code for each bytecode

*/
int ncgMethodFake(Method* method) {
    //to measure code size expansion, no need to patch up labels
    methodDataWorklist = NULL;
    globalShortWorklist = NULL;
    globalNCGWorklist = NULL;
    streamMethodStart = stream;

    //initialize mapFromBCtoNCG
    memset(&mapFromBCtoNCG[0], -1, BYTECODE_SIZE_PER_METHOD * sizeof(mapFromBCtoNCG[0]));
    unsigned int i;
    u2* rStart = (u2*)malloc(5*sizeof(u2));
    if(rStart == NULL) {
        ALOGI("JIT_INFO: Memory allocation failed at ncgMethodFake");
        SET_JIT_ERROR(kJitErrorMallocFailed);
        return -1;
    }
    rPC = rStart;
    method->insns = rStart;
    for(i = 0; i < 5; i++) *rPC++ = 0;
    for(i = 0; i < 256; i++) {
        rPC = rStart;
        //modify the opcode
        char* tmp = (char*)rStart;
        *tmp++ = i;
        *tmp = i;
        inst = FETCH(0);
        char* tmpStart = stream;
        lowerByteCodeCanThrowCheck(method); //use inst, rPC, method, modify rPC
        int size_in_u2 = rPC - rStart;
        if(stream - tmpStart  > 0)
            ALOGI("LOWER bytecode %x size in u2: %d ncg size in byte: %d", i, size_in_u2, stream - tmpStart);
    }
    exit(0);
}
#endif

bool existATryBlock(Method* method, int startPC, int endPC) {
    const DexCode* pCode = dvmGetMethodCode(method);
    u4 triesSize = pCode->triesSize;
    const DexTry* pTries = dexGetTries(pCode);
    unsigned int i;
    for (i = 0; i < triesSize; i++) {
        const DexTry* pTry = &pTries[i];
        u4 start = pTry->startAddr; //offsetPC
        u4 end = start + pTry->insnCount;
        //if [start, end] overlaps with [startPC, endPC] returns true
        if((int)end < startPC || (int)start > endPC) { //no overlap
        } else {
            return true;
        }
    }
    return false;
}

int mm_bytecode_size = 0;
int mm_ncg_size = 0;
int mm_relocation_size = 0;
int mm_map_size = 0;
void resetCodeSize() {
    mm_bytecode_size = 0;
    mm_ncg_size = 0;
    mm_relocation_size = 0;
    mm_map_size = 0;
}

bool bytecodeIsRemoved(const Method* method, u4 bytecodeOffset) {
    if(gDvm.executionMode == kExecutionModeNcgO0) return false;
    u4 ncgOff = mapFromBCtoNCG[bytecodeOffset];
    int k = bytecodeOffset+1;
    u2 insnsSize = dvmGetMethodInsnsSize(method);
    while(k < insnsSize) {
        if(mapFromBCtoNCG[k] < 0) {
            k++;
            continue;
        }
        if(mapFromBCtoNCG[k] == (int)ncgOff) return true;
        return false;
    }
    return false;
}

int invoke_super_nsm();
void init_common(const char* curFileName, DvmDex *pDvmDex, bool forNCG); //forward declaration
void initGlobalMethods(); //forward declaration

//called once when compiler thread starts up
void initJIT(const char* curFileName, DvmDex *pDvmDex) {
    init_common(curFileName, pDvmDex, false);
}

void init_common(const char* curFileName, DvmDex *pDvmDex, bool forNCG) {
    if(!gDvm.constInit) {
        globalMapNum = 0;
        globalMap = NULL;
        initConstDataSec();
        gDvm.constInit = true;
    }

    //for initJIT: stream is already set
    if(!gDvm.commonInit) {
        initGlobalMethods();
        gDvm.commonInit = true;
    }
}

void initGlobalMethods() {
    bool old_dump_x86_inst = dump_x86_inst;
    bool old_scheduling = gDvmJit.scheduling;
    dump_x86_inst = false; // Enable this to debug common section

    //! \warning Scheduling should be turned off when creating common section
    //! because it relies on the fact the register allocation has already been
    //! done (either via register allocator or via hardcoded registers). But,
    //! when we get to this point, the execution mode is Jit instead of either
    //! NcgO1 or NcgO0, which leads to the unintended consequence that NcgO0
    //! path is taken, but logical registers are used instead of physical
    //! registers and thus relies on encoder to do the mapping, which the
    //! scheduler cannot predict for dependency graph creation.
    //! \todo The reason "logical" registers are used is because variable
    //! isScratchPhysical is set to false even when a physical register is
    //! used. This issue should be addressed at some point.
    gDvmJit.scheduling = false;

    // generate native code for function ncgGetEIP
    if (insertLabel("ncgGetEIP", false) == -1)
        return;
    move_mem_to_reg(OpndSize_32, 0, PhysicalReg_ESP, true, PhysicalReg_EDX, true);
    x86_return();

    //generate code for common labels
    //jumps within a helper function is treated as short labels
    globalShortMap = NULL;
    common_periodicChecks_entry();
    freeShortMap();
    common_periodicChecks4();
    freeShortMap();

    if(dump_x86_inst)
        ALOGI("ArgsDone_Normal start");
    common_invokeArgsDone(ArgsDone_Normal);
    freeShortMap();
    if(dump_x86_inst)
        ALOGI("ArgsDone_Native start");
    common_invokeArgsDone(ArgsDone_Native);
    freeShortMap();
    if(dump_x86_inst)
        ALOGI("ArgsDone_Full start");
    common_invokeArgsDone(ArgsDone_Full);
    if(dump_x86_inst)
        ALOGI("ArgsDone_Full end");
    freeShortMap();

    common_backwardBranch();
    freeShortMap();
    common_exceptionThrown();
    freeShortMap();
    common_errNullObject();
    freeShortMap();
    common_errArrayIndex();
    freeShortMap();
    common_errArrayStore();
    freeShortMap();
    common_errNegArraySize();
    freeShortMap();
    common_errNoSuchMethod();
    freeShortMap();
    common_errDivideByZero();
    freeShortMap();
    common_gotoBail();
    freeShortMap();
    common_gotoBail_0();
    freeShortMap();
    invoke_super_nsm();
    freeShortMap();

    performCGWorklist(); //generate code for helper functions
    performLabelWorklist(); //it is likely that the common labels will jump to other common labels

    gDvmJit.scheduling = old_scheduling;
    dump_x86_inst = old_dump_x86_inst;
}

ExecutionMode origMode;

/**
 * @brief Lowers bytecode to native code
 * @param method parent method of trace
 * @param mir bytecode representation
 * @param dalvikPC the program counter of the instruction
 * @param cUnit O1 CompilationUnit
 * @return true when NOT handled and false when it IS handled
 */
bool lowerByteCodeJit(const Method* method, const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit) {
    int retCode = lowerByteCodeCanThrowCheck(method, mir, dalvikPC, cUnit);
    freeShortMap();
    if(retCode >= 0) return false; //handled
    return true; //not handled
}

void startOfBasicBlock(BasicBlock* bb) {
    traceCurrentBB = bb;
    if(gDvm.executionMode == kExecutionModeNcgO0) {
        isScratchPhysical = true;
    } else {
        isScratchPhysical = false;
    }
}

void startOfTrace(const Method* method, int exceptionBlockId,
                  CompilationUnit_O1 *cUnit) {

    //Set the global compilation
    gCompilationUnit = cUnit;

    origMode = gDvm.executionMode;
    gDvm.executionMode = kExecutionModeNcgO1;
    if(gDvm.executionMode == kExecutionModeNcgO0) {
        isScratchPhysical = true;
    } else {
        isScratchPhysical = false;
    }
    currentMethod = (Method*)method;
    currentExceptionBlockIdx = exceptionBlockId;
    methodDataWorklist = NULL;
    globalShortWorklist = NULL;
    globalNCGWorklist = NULL;
    singletonPtr<ExceptionHandlingRestoreState>()->reset();

    streamMethodStart = stream;
    //initialize mapFromBCtoNCG
    memset(&mapFromBCtoNCG[0], -1, BYTECODE_SIZE_PER_METHOD * sizeof(mapFromBCtoNCG[0]));
    if(gDvm.executionMode == kExecutionModeNcgO1)
        startOfTraceO1(method, exceptionBlockId, cUnit);
}

/**
 * @brief Used to free the data structures in basic blocks that were used by backend
 * @param basicBlocks The list of all basic blocks in current cUnit
 */
static void freeCFG (GrowableList &basicBlocks)
{
    //Create and initialize the basic block iterator
    GrowableListIterator iter;
    dvmGrowableListIteratorInit (&basicBlocks, &iter);

    //Get the first basic block provided by iterator
    BasicBlock_O1 *bb = reinterpret_cast<BasicBlock_O1 *> (dvmGrowableListIteratorNext (&iter));

    while (bb != 0)
    {
        //Call the BasicBlock_O1 clear function
        bb->freeIt ();

        //We want to move on to next basic block
        bb = reinterpret_cast<BasicBlock_O1 *> (dvmGrowableListIteratorNext (&iter));
    }
}

void performWorklistWork (void)
{
    performLabelWorklist ();
    performNCGWorklist (); //handle forward jump (GOTO, IF)
    performDataWorklist (); //handle SWITCH & FILL_ARRAY_DATA
    performChainingWorklist ();
}

void endOfTrace (CompilationUnit *cUnit) {
    freeLabelWorklist ();
    freeNCGWorklist ();
    freeDataWorklist ();
    freeChainingWorklist ();

    //Now we want to free anything in BasicBlock that we used during backend but was not
    //allocated using the arena
    freeCFG (cUnit->blockList);

    //Restore the execution mode as the ME expects it
    gDvm.executionMode = origMode;

    //Reset the global compilation unit
    gCompilationUnit = 0;
}

int lowerByteCodeCanThrowCheck(const Method* method, const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit) {
    bool delay_requested = false;

    int flags = dvmCompilerGetOpcodeFlags (mir->dalvikInsn.opcode);

    // Delay free VRs if we potentially can exit to interpreter
    // We do not activate delay if VRs state is not changed
    if ((flags & kInstrCanThrow) != 0)
    {
        long long dfAttributes = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if ( (dfAttributes & DF_IS_CALL) == 0) { // Not applicable to calls
            int mirOptFlags = mir->OptimizationFlags;

            // Avoid delay if we null/range check optimized
            if ( (dfAttributes & DF_HAS_NR_CHECKS) != 0 ) {
                // Both null check and range check applicable

                if( (mirOptFlags & MIR_IGNORE_NULL_CHECK) == 0 ) {
                    // Null check is not optimized, request delay
                    if(requestVRFreeDelayAll(VRDELAY_CAN_THROW) == true) {
                        delay_requested = true;
                    }
                }

                if( (mirOptFlags & MIR_IGNORE_RANGE_CHECK) == 0 ) {
                    // Range check is not optimized, put additional request delay
                    if(requestVRFreeDelayAll(VRDELAY_CAN_THROW) == true) {
                        delay_requested = true;
                    }
                }
            } else if ( (dfAttributes & DF_HAS_OBJECT_CHECKS) != 0 ) {
                // Only null check applicable to opcode

                if( (mirOptFlags & MIR_IGNORE_NULL_CHECK) == 0 ) {
                    // Null check is not optimized, request delay
                    if(requestVRFreeDelayAll(VRDELAY_CAN_THROW) == true) {
                        delay_requested = true;
                    }
                }
            } else {
                // Can exit to interpreter but have no null/range checks
                if(requestVRFreeDelayAll(VRDELAY_CAN_THROW) == true) {
                    delay_requested = true;
                }
            }
        }
    }

    int retCode = lowerByteCode(method, mir, dalvikPC, cUnit);

    if(delay_requested == true) {
        bool state_changed = cancelVRFreeDelayRequestAll(VRDELAY_CAN_THROW);
        if(state_changed==true) {
            // Not optimized case (delay was not canceled inside bytecode generation)
            // Release all remaining VRDELAY_CAN_THROW requests
            do {
                state_changed = cancelVRFreeDelayRequestAll(VRDELAY_CAN_THROW);
            } while (state_changed == true);
        }
    }
    return retCode;
}

/**
 * @brief Generates native code for the bytecode.
 * @details May update code stream.
 * @param method parent method of trace
 * @param mir bytecode representation
 * @param dalvikPC the program counter of the instruction
 * @param cUnit O1 CompilationUnit
 * @return 0 or greater when handled
 */
int lowerByteCode(const Method* method, const MIR * mir, const u2 * dalvikPC, CompilationUnit_O1* cUnit) {
    /* offsetPC is used in O1 code generator, where it is defined as the sequence number
       use a local version to avoid overwriting */
    int offsetPC = mir->offset; //! \warning When doing method inlining, offsetPC
                                //! will be the same for the invoke and the inlined
                                //! bytecode. This WILL break mapping from BC to NCG
                                //! if more than one bytecode is inlined.

    if (dump_x86_inst == true)
    {
        const int maxDecodedLen = 256;
        char decodedString[maxDecodedLen];

        //We want to decode the current instruction but we pass a null cUnit because we don't
        //care to have any ssa information printed.
        dvmCompilerExtendedDisassembler (0, mir, &(mir->dalvikInsn), decodedString, maxDecodedLen);

        ALOGI ("LOWER %s with offsetPC %x offsetNCG %x @%p\n", decodedString, offsetPC,
                stream - streamMethodStart, stream);
    }

    //update mapFromBCtoNCG
    offsetNCG = stream - streamMethodStart;
    if(offsetPC >= BYTECODE_SIZE_PER_METHOD) {
        ALOGI("JIT_INFO: offsetPC %d exceeds BYTECODE_SIZE_PER_METHOD", offsetPC);
        SET_JIT_ERROR(kJitErrorTraceFormation);
        return -1;
    }
    mapFromBCtoNCG[offsetPC] = offsetNCG;
#if defined(ENABLE_TRACING) && defined(TRACING_OPTION2)
    insertMapWorklist(offsetPC, mapFromBCtoNCG[offsetPC], 1);
#endif
    //return number of LowOps generated
    switch (mir->dalvikInsn.opcode) {
    case OP_NOP:
        return op_nop(mir);
    case OP_MOVE:
    case OP_MOVE_OBJECT:
        return op_move(mir);
    case OP_MOVE_FROM16:
    case OP_MOVE_OBJECT_FROM16:
        return op_move_from16(mir);
    case OP_MOVE_16:
    case OP_MOVE_OBJECT_16:
        return op_move_16(mir);
    case OP_MOVE_WIDE:
        return op_move_wide(mir);
    case OP_MOVE_WIDE_FROM16:
        return op_move_wide_from16(mir);
    case OP_MOVE_WIDE_16:
        return op_move_wide_16(mir);
    case OP_MOVE_RESULT:
    case OP_MOVE_RESULT_OBJECT:
        return op_move_result(mir);
    case OP_MOVE_RESULT_WIDE:
        return op_move_result_wide(mir);
    case OP_MOVE_EXCEPTION:
        return op_move_exception(mir);
    case OP_RETURN_VOID:
    case OP_RETURN_VOID_BARRIER:
        return op_return_void(mir);
    case OP_RETURN:
    case OP_RETURN_OBJECT:
        return op_return(mir);
    case OP_RETURN_WIDE:
        return op_return_wide(mir);
    case OP_CONST_4:
        return op_const_4(mir);
    case OP_CONST_16:
        return op_const_16(mir);
    case OP_CONST:
        return op_const(mir);
    case OP_CONST_HIGH16:
        return op_const_high16(mir);
    case OP_CONST_WIDE_16:
        return op_const_wide_16(mir);
    case OP_CONST_WIDE_32:
        return op_const_wide_32(mir);
    case OP_CONST_WIDE:
        return op_const_wide(mir);
    case OP_CONST_WIDE_HIGH16:
        return op_const_wide_high16(mir);
    case OP_CONST_STRING:
        return op_const_string(mir);
    case OP_CONST_STRING_JUMBO:
        return op_const_string_jumbo(mir);
    case OP_CONST_CLASS:
        return op_const_class(mir);
    case OP_MONITOR_ENTER:
        return op_monitor_enter(mir);
    case OP_MONITOR_EXIT:
        return op_monitor_exit(mir);
    case OP_CHECK_CAST:
        return op_check_cast(mir);
    case OP_INSTANCE_OF:
        return op_instance_of(mir);
    case OP_ARRAY_LENGTH:
        return op_array_length(mir);
    case OP_NEW_INSTANCE:
        return op_new_instance(mir);
    case OP_NEW_ARRAY:
        return op_new_array(mir);
    case OP_FILLED_NEW_ARRAY:
        return op_filled_new_array(mir);
    case OP_FILLED_NEW_ARRAY_RANGE:
        return op_filled_new_array_range(mir);
    case OP_FILL_ARRAY_DATA:
        return op_fill_array_data(mir, dalvikPC);
    case OP_THROW:
        return op_throw(mir);
    case OP_THROW_VERIFICATION_ERROR:
        return op_throw_verification_error(mir);
    case OP_GOTO:
    case OP_GOTO_16:
    case OP_GOTO_32:
        return op_goto (mir, traceCurrentBB);
    case OP_PACKED_SWITCH:
        return op_packed_switch(mir, dalvikPC, cUnit);
    case OP_SPARSE_SWITCH:
        return op_sparse_switch(mir, dalvikPC, cUnit);
    case OP_CMPL_FLOAT:
        return op_cmpl_float(mir);
    case OP_CMPG_FLOAT:
        return op_cmpg_float(mir);
    case OP_CMPL_DOUBLE:
        return op_cmpl_double(mir);
    case OP_CMPG_DOUBLE:
        return op_cmpg_double(mir);
    case OP_CMP_LONG:
        return op_cmp_long(mir);
    case OP_IF_EQ:
        return op_if_eq(mir);
    case OP_IF_NE:
        return op_if_ne(mir);
    case OP_IF_LT:
        return op_if_lt(mir);
    case OP_IF_GE:
        return op_if_ge(mir);
    case OP_IF_GT:
        return op_if_gt(mir);
    case OP_IF_LE:
        return op_if_le(mir);
    case OP_IF_EQZ:
        return op_if_eqz(mir);
    case OP_IF_NEZ:
        return op_if_nez(mir);
    case OP_IF_LTZ:
        return op_if_ltz(mir);
    case OP_IF_GEZ:
        return op_if_gez(mir);
    case OP_IF_GTZ:
        return op_if_gtz(mir);
    case OP_IF_LEZ:
        return op_if_lez(mir);
    case OP_AGET:
        return op_aget(mir);
    case OP_AGET_WIDE:
        return op_aget_wide(mir);
    case OP_AGET_OBJECT:
        return op_aget_object(mir);
    case OP_AGET_BOOLEAN:
        return op_aget_boolean(mir);
    case OP_AGET_BYTE:
        return op_aget_byte(mir);
    case OP_AGET_CHAR:
        return op_aget_char(mir);
    case OP_AGET_SHORT:
        return op_aget_short(mir);
    case OP_APUT:
        return op_aput(mir);
    case OP_APUT_WIDE:
        return op_aput_wide(mir);
    case OP_APUT_OBJECT:
        return op_aput_object(mir);
    case OP_APUT_BOOLEAN:
        return op_aput_boolean(mir);
    case OP_APUT_BYTE:
        return op_aput_byte(mir);
    case OP_APUT_CHAR:
        return op_aput_char(mir);
    case OP_APUT_SHORT:
        return op_aput_short(mir);
    case OP_IGET:
        return op_iget(mir, false);
    case OP_IGET_VOLATILE:
        return op_iget(mir, true);
    case OP_IGET_WIDE:
        return op_iget_wide(mir, false);
    case OP_IGET_WIDE_VOLATILE:
        return op_iget_wide(mir, true);
    case OP_IGET_OBJECT:
        return op_iget_object(mir, false);
    case OP_IGET_OBJECT_VOLATILE:
        return op_iget_object(mir, true);
    case OP_IGET_BOOLEAN:
        return op_iget_boolean(mir);
    case OP_IGET_BYTE:
        return op_iget_byte(mir);
    case OP_IGET_CHAR:
        return op_iget_char(mir);
    case OP_IGET_SHORT:
        return op_iget_short(mir);
    case OP_IPUT:
        return op_iput(mir, false);
    case OP_IPUT_VOLATILE:
        return op_iput(mir, true);
    case OP_IPUT_WIDE:
        return op_iput_wide(mir, false);
    case OP_IPUT_WIDE_VOLATILE:
        return op_iput_wide(mir, true);
    case OP_IPUT_OBJECT:
        return op_iput_object(mir, false);
    case OP_IPUT_OBJECT_VOLATILE:
        return op_iput_object(mir, true);
    case OP_IPUT_BOOLEAN:
        return op_iput_boolean(mir);
    case OP_IPUT_BYTE:
        return op_iput_byte(mir);
    case OP_IPUT_CHAR:
        return op_iput_char(mir);
    case OP_IPUT_SHORT:
        return op_iput_short(mir);
    case OP_SGET:
        return op_sget(mir, false);
    case OP_SGET_VOLATILE:
        return op_sget(mir, true);
    case OP_SGET_WIDE:
        return op_sget_wide(mir, false);
    case OP_SGET_WIDE_VOLATILE:
        return op_sget_wide(mir, true);
    case OP_SGET_OBJECT:
        return op_sget_object(mir, false);
    case OP_SGET_OBJECT_VOLATILE:
        return op_sget_object(mir, true);
    case OP_SGET_BOOLEAN:
        return op_sget_boolean(mir);
    case OP_SGET_BYTE:
        return op_sget_byte(mir);
    case OP_SGET_CHAR:
        return op_sget_char(mir);
    case OP_SGET_SHORT:
        return op_sget_short(mir);
    case OP_SPUT:
        return op_sput(mir, false, false);
    case OP_SPUT_VOLATILE:
        return op_sput(mir, false, true);
    case OP_SPUT_WIDE:
        return op_sput_wide(mir, false);
    case OP_SPUT_WIDE_VOLATILE:
        return op_sput_wide(mir, true);
    case OP_SPUT_OBJECT:
        return op_sput_object(mir, false);
    case OP_SPUT_OBJECT_VOLATILE:
        return op_sput_object(mir, true);
    case OP_SPUT_BOOLEAN:
        return op_sput_boolean(mir);
    case OP_SPUT_BYTE:
        return op_sput_byte(mir);
    case OP_SPUT_CHAR:
        return op_sput_char(mir);
    case OP_SPUT_SHORT:
        return op_sput_short(mir);
    case OP_INVOKE_VIRTUAL:
        return op_invoke_virtual(mir);
    case OP_INVOKE_SUPER:
        return op_invoke_super(mir);
    case OP_INVOKE_DIRECT:
        return op_invoke_direct(mir);
    case OP_INVOKE_STATIC:
        return op_invoke_static(mir);
    case OP_INVOKE_INTERFACE:
        return op_invoke_interface(mir);
    case OP_INVOKE_VIRTUAL_RANGE:
        return op_invoke_virtual_range(mir);
    case OP_INVOKE_SUPER_RANGE:
        return op_invoke_super_range(mir);
    case OP_INVOKE_DIRECT_RANGE:
        return op_invoke_direct_range(mir);
    case OP_INVOKE_STATIC_RANGE:
        return op_invoke_static_range(mir);
    case OP_INVOKE_INTERFACE_RANGE:
        return op_invoke_interface_range(mir);
    case OP_NEG_INT:
        return op_neg_int(mir);
    case OP_NOT_INT:
        return op_not_int(mir);
    case OP_NEG_LONG:
        return op_neg_long(mir);
    case OP_NOT_LONG:
        return op_not_long(mir);
    case OP_NEG_FLOAT:
        return op_neg_float(mir);
    case OP_NEG_DOUBLE:
        return op_neg_double(mir);
    case OP_INT_TO_LONG:
        return op_int_to_long(mir);
    case OP_INT_TO_FLOAT:
        return op_int_to_float(mir);
    case OP_INT_TO_DOUBLE:
        return op_int_to_double(mir);
    case OP_LONG_TO_INT:
        return op_long_to_int(mir);
    case OP_LONG_TO_FLOAT:
        return op_long_to_float(mir);
    case OP_LONG_TO_DOUBLE:
        return op_long_to_double(mir);
    case OP_FLOAT_TO_INT:
        return op_float_to_int(mir);
    case OP_FLOAT_TO_LONG:
        return op_float_to_long(mir);
    case OP_FLOAT_TO_DOUBLE:
        return op_float_to_double(mir);
    case OP_DOUBLE_TO_INT:
        return op_double_to_int(mir);
    case OP_DOUBLE_TO_LONG:
        return op_double_to_long(mir);
    case OP_DOUBLE_TO_FLOAT:
        return op_double_to_float(mir);
    case OP_INT_TO_BYTE:
        return op_int_to_byte(mir);
    case OP_INT_TO_CHAR:
        return op_int_to_char(mir);
    case OP_INT_TO_SHORT:
        return op_int_to_short(mir);
    case OP_ADD_INT:
        return op_add_int(mir);
    case OP_SUB_INT:
        return op_sub_int(mir);
    case OP_MUL_INT:
        return op_mul_int(mir);
    case OP_DIV_INT:
        return op_div_int(mir);
    case OP_REM_INT:
        return op_rem_int(mir);
    case OP_AND_INT:
        return op_and_int(mir);
    case OP_OR_INT:
        return op_or_int(mir);
    case OP_XOR_INT:
        return op_xor_int(mir);
    case OP_SHL_INT:
        return op_shl_int(mir);
    case OP_SHR_INT:
        return op_shr_int(mir);
    case OP_USHR_INT:
        return op_ushr_int(mir);
    case OP_ADD_LONG:
        return op_add_long(mir);
    case OP_SUB_LONG:
        return op_sub_long(mir);
    case OP_MUL_LONG:
        return op_mul_long(mir);
    case OP_DIV_LONG:
        return op_div_long(mir);
    case OP_REM_LONG:
        return op_rem_long(mir);
    case OP_AND_LONG:
        return op_and_long(mir);
    case OP_OR_LONG:
        return op_or_long(mir);
    case OP_XOR_LONG:
        return op_xor_long(mir);
    case OP_SHL_LONG:
        return op_shl_long(mir);
    case OP_SHR_LONG:
        return op_shr_long(mir);
    case OP_USHR_LONG:
        return op_ushr_long(mir);
    case OP_ADD_FLOAT:
        return op_add_float(mir);
    case OP_SUB_FLOAT:
        return op_sub_float(mir);
    case OP_MUL_FLOAT:
        return op_mul_float(mir);
    case OP_DIV_FLOAT:
        return op_div_float(mir);
    case OP_REM_FLOAT:
        return op_rem_float(mir);
    case OP_ADD_DOUBLE:
        return op_add_double(mir);
    case OP_SUB_DOUBLE:
        return op_sub_double(mir);
    case OP_MUL_DOUBLE:
        return op_mul_double(mir);
    case OP_DIV_DOUBLE:
        return op_div_double(mir);
    case OP_REM_DOUBLE:
        return op_rem_double(mir);
    case OP_ADD_INT_2ADDR:
        return op_add_int_2addr(mir);
    case OP_SUB_INT_2ADDR:
        return op_sub_int_2addr(mir);
    case OP_MUL_INT_2ADDR:
        return op_mul_int_2addr(mir);
    case OP_DIV_INT_2ADDR:
        return op_div_int_2addr(mir);
    case OP_REM_INT_2ADDR:
        return op_rem_int_2addr(mir);
    case OP_AND_INT_2ADDR:
        return op_and_int_2addr(mir);
    case OP_OR_INT_2ADDR:
        return op_or_int_2addr(mir);
    case OP_XOR_INT_2ADDR:
        return op_xor_int_2addr(mir);
    case OP_SHL_INT_2ADDR:
        return op_shl_int_2addr(mir);
    case OP_SHR_INT_2ADDR:
        return op_shr_int_2addr(mir);
    case OP_USHR_INT_2ADDR:
        return op_ushr_int_2addr(mir);
    case OP_ADD_LONG_2ADDR:
        return op_add_long_2addr(mir);
    case OP_SUB_LONG_2ADDR:
        return op_sub_long_2addr(mir);
    case OP_MUL_LONG_2ADDR:
        return op_mul_long_2addr(mir);
    case OP_DIV_LONG_2ADDR:
        return op_div_long_2addr(mir);
    case OP_REM_LONG_2ADDR:
        return op_rem_long_2addr(mir);
    case OP_AND_LONG_2ADDR:
        return op_and_long_2addr(mir);
    case OP_OR_LONG_2ADDR:
        return op_or_long_2addr(mir);
    case OP_XOR_LONG_2ADDR:
        return op_xor_long_2addr(mir);
    case OP_SHL_LONG_2ADDR:
        return op_shl_long_2addr(mir);
    case OP_SHR_LONG_2ADDR:
        return op_shr_long_2addr(mir);
    case OP_USHR_LONG_2ADDR:
        return op_ushr_long_2addr(mir);
    case OP_ADD_FLOAT_2ADDR:
        return op_add_float_2addr(mir);
    case OP_SUB_FLOAT_2ADDR:
        return op_sub_float_2addr(mir);
    case OP_MUL_FLOAT_2ADDR:
        return op_mul_float_2addr(mir);
    case OP_DIV_FLOAT_2ADDR:
        return op_div_float_2addr(mir);
    case OP_REM_FLOAT_2ADDR:
        return op_rem_float_2addr(mir);
    case OP_ADD_DOUBLE_2ADDR:
        return op_add_double_2addr(mir);
    case OP_SUB_DOUBLE_2ADDR:
        return op_sub_double_2addr(mir);
    case OP_MUL_DOUBLE_2ADDR:
        return op_mul_double_2addr(mir);
    case OP_DIV_DOUBLE_2ADDR:
        return op_div_double_2addr(mir);
    case OP_REM_DOUBLE_2ADDR:
        return op_rem_double_2addr(mir);
    case OP_ADD_INT_LIT16:
        return op_add_int_lit16(mir);
    case OP_RSUB_INT:
        return op_rsub_int(mir);
    case OP_MUL_INT_LIT16:
        return op_mul_int_lit16(mir);
    case OP_DIV_INT_LIT16:
        return op_div_int_lit16(mir);
    case OP_REM_INT_LIT16:
        return op_rem_int_lit16(mir);
    case OP_AND_INT_LIT16:
        return op_and_int_lit16(mir);
    case OP_OR_INT_LIT16:
        return op_or_int_lit16(mir);
    case OP_XOR_INT_LIT16:
        return op_xor_int_lit16(mir);
    case OP_ADD_INT_LIT8:
        return op_add_int_lit8(mir);
    case OP_RSUB_INT_LIT8:
        return op_rsub_int_lit8(mir);
    case OP_MUL_INT_LIT8:
        return op_mul_int_lit8(mir);
    case OP_DIV_INT_LIT8:
        return op_div_int_lit8(mir);
    case OP_REM_INT_LIT8:
        return op_rem_int_lit8(mir);
    case OP_AND_INT_LIT8:
        return op_and_int_lit8(mir);
    case OP_OR_INT_LIT8:
        return op_or_int_lit8(mir);
    case OP_XOR_INT_LIT8:
        return op_xor_int_lit8(mir);
    case OP_SHL_INT_LIT8:
        return op_shl_int_lit8(mir);
    case OP_SHR_INT_LIT8:
        return op_shr_int_lit8(mir);
    case OP_USHR_INT_LIT8:
        return op_ushr_int_lit8(mir);
    case OP_EXECUTE_INLINE:
        return op_execute_inline(mir, false /*isRange*/);
    case OP_EXECUTE_INLINE_RANGE:
        return op_execute_inline(mir, true /*isRange*/);
//  case OP_INVOKE_OBJECT_INIT_RANGE:
//      return op_invoke_object_init_range();
    case OP_IGET_QUICK:
        return op_iget_quick(mir);
    case OP_IGET_WIDE_QUICK:
        return op_iget_wide_quick(mir);
    case OP_IGET_OBJECT_QUICK:
        return op_iget_object_quick(mir);
    case OP_IPUT_QUICK:
        return op_iput_quick(mir);
    case OP_IPUT_WIDE_QUICK:
        return op_iput_wide_quick(mir);
    case OP_IPUT_OBJECT_QUICK:
        return op_iput_object_quick(mir);
    case OP_INVOKE_VIRTUAL_QUICK:
        return op_invoke_virtual_quick(mir);
    case OP_INVOKE_VIRTUAL_QUICK_RANGE:
        return op_invoke_virtual_quick_range(mir);
    case OP_INVOKE_SUPER_QUICK:
        return op_invoke_super_quick(mir);
    case OP_INVOKE_SUPER_QUICK_RANGE:
        return op_invoke_super_quick_range(mir);
    default:
        ALOGI("JIT_INFO: JIT does not support bytecode %s\n",
                dvmCompilerGetOpcodeName (mir->dalvikInsn.opcode));
        SET_JIT_ERROR(kJitErrorUnsupportedBytecode);
        assert(false && "All opcodes should be supported.");
        break;
    }
    return -1;
}

int op_nop(const MIR * mir) {
    assert(mir->dalvikInsn.opcode == OP_NOP);
    return 0;
}

#if defined VTUNE_DALVIK
/**
 * Send the label information (size, start_address and name) to VTune
 */
void sendLabelInfoToVTune(int startStreamPtr, int endStreamPtr, const char* labelName) {
    if (endStreamPtr == startStreamPtr) {
        return;
    }

    iJIT_Method_Load jitMethod;
    memset(&jitMethod, 0, sizeof(iJIT_Method_Load));
    jitMethod.method_id = iJIT_GetNewMethodID();
    jitMethod.method_name = const_cast<char *>(labelName);
    jitMethod.method_load_address = (void *)startStreamPtr;
    jitMethod.method_size = endStreamPtr-startStreamPtr;
    int res = notifyVTune(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void*)&jitMethod);
    if (gDvmJit.printMe == true) {
        if (res != 0) {
            ALOGD("JIT API: a trace of %s method was written successfully address: id=%u, address=%p, size=%d."
                    , labelName, jitMethod.method_id, jitMethod.method_load_address, jitMethod.method_size);
        } else {
            ALOGD("JIT API: failed to write a trace of %s method address: id=%u, address=%p, size=%d."
                    , labelName, jitMethod.method_id, jitMethod.method_load_address, jitMethod.method_size);
        }
    }
}
#endif

int getLabelOffset (unsigned int blockId) {
    //Paranoid
    if (gCompilationUnit == 0) {
        //We can't do much except reporting an error
        ALOGI("JIT_INFO: getLabelOffset has  null gCompilationUnit");
        SET_JIT_ERROR(kJitErrorTraceFormation);
        return -1;
    }

    //Get the BasicBlock
    BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(&gCompilationUnit->blockList, blockId);

    //Transform into a BasicBlock_O1
    BasicBlock_O1 *bbO1 = reinterpret_cast<BasicBlock_O1 *> (bb);

    //Paranoid
    if (bbO1 == 0 || bbO1->label == 0) {
        //We can't do much except reporting an error
        ALOGI("JIT_INFO: getLabelOffset has invalid basic block");
        SET_JIT_ERROR(kJitErrorInvalidBBId);
        return -1;
    }

    //Now return the label
    return bbO1->label->lop.generic.offset;
}


/**
 * @brief Calculate magic number and shift for a given divisor
 * @param divisor divisor number for calculation
 * @param magic hold calculated magic number
 * @param shift hold calculated shift
 * @return void
 */
void calculateMagicAndShift(int divisor, int* magic, int* shift) {
    //It does not make sense to calculate magic and shift for zero divisor
    assert (divisor != 0);

    int p = 31;
    unsigned abs_d, abs_nc, delta, quotient1, remainder1, quotient2, remainder2, tmp;
    const unsigned two31 = 1 << p;

    /* According to H.S.Warren's Hacker's Delight Chapter 10 and
       T,Grablund, P.L.Montogomery's Division by invariant integers using multiplication
       The magic number M and shift S can be calculated in the following way:
       Let nc be the most positive value of numerator(n) such that nc = kd - 1, where divisor(d) >=2
       Let nc be the most negative value of numerator(n) such that nc = kd + 1, where divisor(d) <= -2
       Thus nc can be calculated like:
       nc = 2^31 + 2^31 % d - 1, where d >= 2
       nc = -2^31 + (2^31 + 1) % d, where d >= 2.

       So the shift p is the smallest p satisfying
       2^p > nc * (d - 2^p % d), where d >= 2
       2^p > nc * (d + 2^p % d), where d <= -2.

       the magic number M is calcuated by
       M = (2^p + d - 2^p % d) / d, where d >= 2
       M = (2^p - d - 2^p % d) / d, where d <= -2.

       Notice that p is always bigger than or equal to 32, so we just return 32-p as the shift number S. */

    // Initialize
    abs_d = abs(divisor);
    tmp = two31 + ((unsigned)divisor >> 31);
    abs_nc = tmp - 1 - tmp % abs_d;
    quotient1 = two31 / abs_nc;
    remainder1 = two31 % abs_nc;
    quotient2 = two31 / abs_d;
    remainder2 = two31 % abs_d;

    // To avoid handling both positive and negative divisor, Hacker's Delight introduces a method to handle
    // these 2 cases together to avoid duplication.
    do {
        p++;
        quotient1 = 2 * quotient1;
        remainder1 = 2 * remainder1;
        if (remainder1 >= abs_nc){
            quotient1++;
            remainder1 = remainder1 - abs_nc;
        }
        quotient2 = 2 * quotient2;
        remainder2 = 2 * remainder2;
        if (remainder2 >= abs_d){
            quotient2++;
            remainder2 = remainder2 - abs_d;
        }
        delta = abs_d - remainder2;
    }while (quotient1 < delta || (quotient1 == delta && remainder1 == 0));

    *magic = (divisor > 0) ? (quotient2 + 1) : (-quotient2 - 1);
    *shift = p - 32;
}
