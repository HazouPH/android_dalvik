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

#include "Dalvik.h"
#include "Dataflow.h"
#include "InlineNative.h"
#include "Loop.h"
#include "LoopInformation.h"
#include "libdex/DexOpcodes.h"
#include "SSAWalkData.h"
#include "Utility.h"

#include <queue>
#include <vector>
#include <set>

/*
 * Main table containing data flow attributes for each bytecode. The
 * first kNumPackedOpcodes entries are for Dalvik bytecode
 * instructions, where extended opcode at the MIR level are appended
 * afterwards.
 *
 * TODO - many optimization flags are incomplete - they will only limit the
 * scope of optimizations but will not cause mis-optimizations.
 */
long long dvmCompilerDataFlowAttributes[kMirOpLast] = {
    // 00 OP_NOP
    DF_NOP,

    // 01 OP_MOVE vA, vB
    DF_DA | DF_UB | DF_IS_MOVE,

    // 02 OP_MOVE_FROM16 vAA, vBBBB
    DF_DA | DF_UB | DF_IS_MOVE,

    // 03 OP_MOVE_16 vAAAA, vBBBB
    DF_DA | DF_UB | DF_IS_MOVE,

    // 04 OP_MOVE_WIDE vA, vB
    DF_DA_WIDE | DF_UB_WIDE | DF_IS_MOVE,

    // 05 OP_MOVE_WIDE_FROM16 vAA, vBBBB
    DF_DA_WIDE | DF_UB_WIDE | DF_IS_MOVE,

    // 06 OP_MOVE_WIDE_16 vAAAA, vBBBB
    DF_DA_WIDE | DF_UB_WIDE | DF_IS_MOVE,

    // 07 OP_MOVE_OBJECT vA, vB
    DF_DA | DF_UB | DF_IS_MOVE,

    // 08 OP_MOVE_OBJECT_FROM16 vAA, vBBBB
    DF_DA | DF_UB | DF_IS_MOVE,

    // 09 OP_MOVE_OBJECT_16 vAAAA, vBBBB
    DF_DA | DF_UB | DF_IS_MOVE,

    // 0A OP_MOVE_RESULT vAA
    DF_DA,

    // 0B OP_MOVE_RESULT_WIDE vAA
    DF_DA_WIDE,

    // 0C OP_MOVE_RESULT_OBJECT vAA
    DF_DA,

    // 0D OP_MOVE_EXCEPTION vAA
    DF_DA,

    // 0E OP_RETURN_VOID
    DF_NOP,

    // 0F OP_RETURN vAA
    DF_UA,

    // 10 OP_RETURN_WIDE vAA
    DF_UA_WIDE,

    // 11 OP_RETURN_OBJECT vAA
    DF_UA,

    // 12 OP_CONST_4 vA, #+B
    DF_DA | DF_SETS_CONST,

    // 13 OP_CONST_16 vAA, #+BBBB
    DF_DA | DF_SETS_CONST,

    // 14 OP_CONST vAA, #+BBBBBBBB
    DF_DA | DF_SETS_CONST,

    // 15 OP_CONST_HIGH16 VAA, #+BBBB0000
    DF_DA | DF_SETS_CONST,

    // 16 OP_CONST_WIDE_16 vAA, #+BBBB
    DF_DA_WIDE | DF_SETS_CONST,

    // 17 OP_CONST_WIDE_32 vAA, #+BBBBBBBB
    DF_DA_WIDE | DF_SETS_CONST,

    // 18 OP_CONST_WIDE vAA, #+BBBBBBBBBBBBBBBB
    DF_DA_WIDE | DF_SETS_CONST,

    // 19 OP_CONST_WIDE_HIGH16 vAA, #+BBBB000000000000
    DF_DA_WIDE | DF_SETS_CONST,

    // 1A OP_CONST_STRING vAA, string@BBBB
    DF_DA,

    // 1B OP_CONST_STRING_JUMBO vAA, string@BBBBBBBB
    DF_DA,

    // 1C OP_CONST_CLASS vAA, type@BBBB
    DF_DA,

    // 1D OP_MONITOR_ENTER vAA
    DF_UA | DF_NULL_OBJECT_CHECK_0 | DF_CLOBBERS_MEMORY,

    // 1E OP_MONITOR_EXIT vAA
    DF_UA | DF_NULL_OBJECT_CHECK_0 | DF_CLOBBERS_MEMORY,

    // 1F OP_CHECK_CAST vAA, type@BBBB
    DF_UA,

    // 20 OP_INSTANCE_OF vA, vB, type@CCCC
    DF_DA | DF_UB,

    // 21 OP_ARRAY_LENGTH vA, vB
    DF_DA | DF_UB | DF_NULL_OBJECT_CHECK_0,

    // 22 OP_NEW_INSTANCE vAA, type@BBBB
    DF_DA | DF_CLOBBERS_MEMORY,

    // 23 OP_NEW_ARRAY vA, vB, type@CCCC
    DF_DA | DF_UB | DF_CLOBBERS_MEMORY,

    // 24 OP_FILLED_NEW_ARRAY {vD, vE, vF, vG, vA}
    DF_FORMAT_35C | DF_CLOBBERS_MEMORY,

    // 25 OP_FILLED_NEW_ARRAY_RANGE {vCCCC .. vNNNN}, type@BBBB
    DF_FORMAT_3RC | DF_CLOBBERS_MEMORY,

    // 26 OP_FILL_ARRAY_DATA vAA, +BBBBBBBB
    DF_UA,

    // 27 OP_THROW vAA
    DF_UA,

    // 28 OP_GOTO
    DF_NOP,

    // 29 OP_GOTO_16
    DF_NOP,

    // 2A OP_GOTO_32
    DF_NOP,

    // 2B OP_PACKED_SWITCH vAA, +BBBBBBBB
    DF_UA,

    // 2C OP_SPARSE_SWITCH vAA, +BBBBBBBB
    DF_UA,

    // 2D OP_CMPL_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_B | DF_FP_C,

    // 2E OP_CMPG_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_B | DF_FP_C,

    // 2F OP_CMPL_DOUBLE vAA, vBB, vCC
    DF_DA | DF_UB_WIDE | DF_UC_WIDE | DF_FP_B | DF_FP_C,

    // 30 OP_CMPG_DOUBLE vAA, vBB, vCC
    DF_DA | DF_UB_WIDE | DF_UC_WIDE | DF_FP_B | DF_FP_C,

    // 31 OP_CMP_LONG vAA, vBB, vCC
    DF_DA | DF_UB_WIDE | DF_UC_WIDE,

    // 32 OP_IF_EQ vA, vB, +CCCC
    DF_UA | DF_UB,

    // 33 OP_IF_NE vA, vB, +CCCC
    DF_UA | DF_UB,

    // 34 OP_IF_LT vA, vB, +CCCC
    DF_UA | DF_UB,

    // 35 OP_IF_GE vA, vB, +CCCC
    DF_UA | DF_UB,

    // 36 OP_IF_GT vA, vB, +CCCC
    DF_UA | DF_UB,

    // 37 OP_IF_LE vA, vB, +CCCC
    DF_UA | DF_UB,


    // 38 OP_IF_EQZ vAA, +BBBB
    DF_UA,

    // 39 OP_IF_NEZ vAA, +BBBB
    DF_UA,

    // 3A OP_IF_LTZ vAA, +BBBB
    DF_UA,

    // 3B OP_IF_GEZ vAA, +BBBB
    DF_UA,

    // 3C OP_IF_GTZ vAA, +BBBB
    DF_UA,

    // 3D OP_IF_LEZ vAA, +BBBB
    DF_UA,

    // 3E OP_UNUSED_3E
    DF_NOP,

    // 3F OP_UNUSED_3F
    DF_NOP,

    // 40 OP_UNUSED_40
    DF_NOP,

    // 41 OP_UNUSED_41
    DF_NOP,

    // 42 OP_UNUSED_42
    DF_NOP,

    // 43 OP_UNUSED_43
    DF_NOP,

    // 44 OP_AGET vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 45 OP_AGET_WIDE vAA, vBB, vCC
    DF_DA_WIDE | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 46 OP_AGET_OBJECT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 47 OP_AGET_BOOLEAN vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 48 OP_AGET_BYTE vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 49 OP_AGET_CHAR vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 4A OP_AGET_SHORT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_0 | DF_IS_GETTER,

    // 4B OP_APUT vAA, vBB, vCC
    DF_UA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_1 | DF_IS_SETTER,

    // 4C OP_APUT_WIDE vAA, vBB, vCC
    DF_UA_WIDE | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_2 | DF_IS_SETTER,

    // 4D OP_APUT_OBJECT vAA, vBB, vCC
    DF_UA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_1 | DF_IS_SETTER,

    // 4E OP_APUT_BOOLEAN vAA, vBB, vCC
    DF_UA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_1 | DF_IS_SETTER,

    // 4F OP_APUT_BYTE vAA, vBB, vCC
    DF_UA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_1 | DF_IS_SETTER,

    // 50 OP_APUT_CHAR vAA, vBB, vCC
    DF_UA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_1 | DF_IS_SETTER,

    // 51 OP_APUT_SHORT vAA, vBB, vCC
    DF_UA | DF_UB | DF_UC | DF_NULL_N_RANGE_CHECK_1 | DF_IS_SETTER,

    // 52 OP_IGET vA, vB, field@CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 53 OP_IGET_WIDE vA, vB, field@CCCC
    DF_DA_WIDE | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 54 OP_IGET_OBJECT vA, vB, field@CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 55 OP_IGET_BOOLEAN vA, vB, field@CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 56 OP_IGET_BYTE vA , vB, field@CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 57 OP_IGET_CHAR vA , vB, field@CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 58 OP_IGET_SHORT vA , vB, field@CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // 59 OP_IPUT vA , vB, field@CCCC
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // 5A OP_IPUT_WIDE vA, vB, field@CCCC
    DF_UA_WIDE | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_2,

    // 5B OP_IPUT_OBJECT vA, vB, field@CCCC
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // 5C OP_IPUT_BOOLEAN vA, vB, field@CCCC
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // 5D OP_IPUT_BYTE vA, vB, field@CCCC
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // 5E OP_IPUT_CHAR vA, vB, field@CCCC
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // 5F OP_IPUT_SHORT vA, vB, field@CCCC
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // 60 OP_SGET vAA, field@BBBB
    DF_DA | DF_IS_GETTER,

    // 61 OP_SGET_WIDE vAA, field@BBBB
    DF_DA_WIDE | DF_IS_GETTER,

    // 62 OP_SGET_OBJECT vAA, field@BBBB
    DF_DA | DF_IS_GETTER,

    // 63 OP_SGET_BOOLEAN vAA, field@BBBB
    DF_DA | DF_IS_GETTER,

    // 64 OP_SGET_BYTE vAA, field@BBBB
    DF_DA | DF_IS_GETTER,

    // 65 OP_SGET_CHAR vAA, field@BBBB
    DF_DA | DF_IS_GETTER,

    // 66 OP_SGET_SHORT vAA, field@BBBB
    DF_DA | DF_IS_GETTER,

    // 67 OP_SPUT vAA, field@BBBB
    DF_UA | DF_IS_SETTER,

    // 68 OP_SPUT_WIDE vAA, field@BBBB
    DF_UA_WIDE | DF_IS_SETTER,

    // 69 OP_SPUT_OBJECT vAA, field@BBBB
    DF_UA | DF_IS_SETTER,

    // 6A OP_SPUT_BOOLEAN vAA, field@BBBB
    DF_UA | DF_IS_SETTER,

    // 6B OP_SPUT_BYTE vAA, field@BBBB
    DF_UA | DF_IS_SETTER,

    // 6C OP_SPUT_CHAR vAA, field@BBBB
    DF_UA | DF_IS_SETTER,

    // 6D OP_SPUT_SHORT vAA, field@BBBB
    DF_UA | DF_IS_SETTER,

    // 6E OP_INVOKE_VIRTUAL {vD, vE, vF, vG, vA}
    DF_FORMAT_35C | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // 6F OP_INVOKE_SUPER {vD, vE, vF, vG, vA}
    DF_FORMAT_35C | DF_IS_CALL,

    // 70 OP_INVOKE_DIRECT {vD, vE, vF, vG, vA}
    DF_FORMAT_35C | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // 71 OP_INVOKE_STATIC {vD, vE, vF, vG, vA}
    DF_FORMAT_35C | DF_IS_CALL,

    // 72 OP_INVOKE_INTERFACE {vD, vE, vF, vG, vA}
    DF_FORMAT_35C | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // 73 OP_UNUSED_73
    DF_NOP,

    // 74 OP_INVOKE_VIRTUAL_RANGE {vCCCC .. vNNNN}
    DF_FORMAT_3RC | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // 75 OP_INVOKE_SUPER_RANGE {vCCCC .. vNNNN}
    DF_FORMAT_3RC | DF_IS_CALL,

    // 76 OP_INVOKE_DIRECT_RANGE {vCCCC .. vNNNN}
    DF_FORMAT_3RC | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // 77 OP_INVOKE_STATIC_RANGE {vCCCC .. vNNNN}
    DF_FORMAT_3RC | DF_IS_CALL,

    // 78 OP_INVOKE_INTERFACE_RANGE {vCCCC .. vNNNN}
    DF_FORMAT_3RC | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // 79 OP_UNUSED_79
    DF_NOP,

    // 7A OP_UNUSED_7A
    DF_NOP,

    // 7B OP_NEG_INT vA, vB
    DF_DA | DF_UB,

    // 7C OP_NOT_INT vA, vB
    DF_DA | DF_UB,

    // 7D OP_NEG_LONG vA, vB
    DF_DA_WIDE | DF_UB_WIDE,

    // 7E OP_NOT_LONG vA, vB
    DF_DA_WIDE | DF_UB_WIDE,

    // 7F OP_NEG_FLOAT vA, vB
    DF_DA | DF_UB | DF_FP_A | DF_FP_B,

    // 80 OP_NEG_DOUBLE vA, vB
    DF_DA_WIDE | DF_UB_WIDE | DF_FP_A | DF_FP_B,

    // 81 OP_INT_TO_LONG vA, vB
    DF_DA_WIDE | DF_UB | DF_CAST,

    // 82 OP_INT_TO_FLOAT vA, vB
    DF_DA | DF_UB | DF_FP_A | DF_CAST,

    // 83 OP_INT_TO_DOUBLE vA, vB
    DF_DA_WIDE | DF_UB | DF_FP_A | DF_CAST,

    // 84 OP_LONG_TO_INT vA, vB
    DF_DA | DF_UB_WIDE | DF_CAST,

    // 85 OP_LONG_TO_FLOAT vA, vB
    DF_DA | DF_UB_WIDE | DF_FP_A | DF_CAST,

    // 86 OP_LONG_TO_DOUBLE vA, vB
    DF_DA_WIDE | DF_UB_WIDE | DF_FP_A | DF_CAST,

    // 87 OP_FLOAT_TO_INT vA, vB
    DF_DA | DF_UB | DF_FP_B | DF_CAST,

    // 88 OP_FLOAT_TO_LONG vA, vB
    DF_DA_WIDE | DF_UB | DF_FP_B | DF_CAST,

    // 89 OP_FLOAT_TO_DOUBLE vA, vB
    DF_DA_WIDE | DF_UB | DF_FP_A | DF_FP_B | DF_CAST,

    // 8A OP_DOUBLE_TO_INT vA, vB
    DF_DA | DF_UB_WIDE | DF_FP_B | DF_CAST,

    // 8B OP_DOUBLE_TO_LONG vA, vB
    DF_DA_WIDE | DF_UB_WIDE | DF_FP_B | DF_CAST,

    // 8C OP_DOUBLE_TO_FLOAT vA, vB
    DF_DA | DF_UB_WIDE | DF_FP_A | DF_FP_B | DF_CAST,

    // 8D OP_INT_TO_BYTE vA, vB
    DF_DA | DF_UB | DF_CAST,

    // 8E OP_INT_TO_CHAR vA, vB
    DF_DA | DF_UB | DF_CAST,

    // 8F OP_INT_TO_SHORT vA, vB
    DF_DA | DF_UB | DF_CAST,

    // 90 OP_ADD_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_IS_LINEAR | DF_ADD_EXPRESSION,

    // 91 OP_SUB_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_IS_LINEAR | DF_SUBTRACT_EXPRESSION,

    // 92 OP_MUL_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_MULTIPLY_EXPRESSION,

    // 93 OP_DIV_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_DIVIDE_EXPRESSION,

    // 94 OP_REM_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_REMAINDER_EXPRESSION,

    // 95 OP_AND_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_AND_EXPRESSION,

    // 96 OP_OR_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_OR_EXPRESSION,

    // 97 OP_XOR_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_XOR_EXPRESSION,

    // 98 OP_SHL_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_SHL_EXPRESSION,

    // 99 OP_SHR_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_SHR_EXPRESSION,

    // 9A OP_USHR_INT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_USHR_EXPRESSION,

    // 9B OP_ADD_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_ADD_EXPRESSION,

    // 9C OP_SUB_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_SUBTRACT_EXPRESSION,

    // 9D OP_MUL_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_MULTIPLY_EXPRESSION,

    // 9E OP_DIV_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_DIVIDE_EXPRESSION,

    // 9F OP_REM_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_REMAINDER_EXPRESSION,

    // A0 OP_AND_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_AND_EXPRESSION,

    // A1 OP_OR_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_OR_EXPRESSION,

    // A2 OP_XOR_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_XOR_EXPRESSION,

    // A3 OP_SHL_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC | DF_SHL_EXPRESSION,

    // A4 OP_SHR_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC | DF_SHR_EXPRESSION,

    // A5 OP_USHR_LONG vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC | DF_USHR_EXPRESSION,

    // A6 OP_ADD_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_A | DF_FP_B | DF_FP_C | DF_ADD_EXPRESSION,

    // A7 OP_SUB_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_A | DF_FP_B | DF_FP_C | DF_SUBTRACT_EXPRESSION,

    // A8 OP_MUL_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_A | DF_FP_B | DF_FP_C | DF_MULTIPLY_EXPRESSION,

    // A9 OP_DIV_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_A | DF_FP_B | DF_FP_C | DF_DIVIDE_EXPRESSION,

    // AA OP_REM_FLOAT vAA, vBB, vCC
    DF_DA | DF_UB | DF_UC | DF_FP_A | DF_FP_B | DF_FP_C | DF_REMAINDER_EXPRESSION,

    // AB OP_ADD_DOUBLE vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_FP_A | DF_FP_B | DF_FP_C | DF_ADD_EXPRESSION,

    // AC OP_SUB_DOUBLE vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_FP_A | DF_FP_B | DF_FP_C | DF_SUBTRACT_EXPRESSION,

    // AD OP_MUL_DOUBLE vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_FP_A | DF_FP_B | DF_FP_C | DF_MULTIPLY_EXPRESSION,

    // AE OP_DIV_DOUBLE vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_FP_A | DF_FP_B | DF_FP_C | DF_DIVIDE_EXPRESSION,

    // AF OP_REM_DOUBLE vAA, vBB, vCC
    DF_DA_WIDE | DF_UB_WIDE | DF_UC_WIDE | DF_FP_A | DF_FP_B | DF_FP_C | DF_REMAINDER_EXPRESSION,

    // B0 OP_ADD_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_ADD_EXPRESSION,

    // B1 OP_SUB_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_SUBTRACT_EXPRESSION,

    // B2 OP_MUL_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_MULTIPLY_EXPRESSION,

    // B3 OP_DIV_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_DIVIDE_EXPRESSION,

    // B4 OP_REM_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_REMAINDER_EXPRESSION,

    // B5 OP_AND_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_AND_EXPRESSION,

    // B6 OP_OR_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_OR_EXPRESSION,

    // B7 OP_XOR_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_XOR_EXPRESSION,

    // B8 OP_SHL_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_SHL_EXPRESSION,

    // B9 OP_SHR_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_SHR_EXPRESSION,

    // BA OP_USHR_INT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_USHR_EXPRESSION,

    // BB OP_ADD_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_ADD_EXPRESSION,

    // BC OP_SUB_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_SUBTRACT_EXPRESSION,

    // BD OP_MUL_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_MULTIPLY_EXPRESSION,

    // BE OP_DIV_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_DIVIDE_EXPRESSION,

    // BF OP_REM_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_REMAINDER_EXPRESSION,

    // C0 OP_AND_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_AND_EXPRESSION,

    // C1 OP_OR_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_OR_EXPRESSION,

    // C2 OP_XOR_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_XOR_EXPRESSION,

    // C3 OP_SHL_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB | DF_SHL_EXPRESSION,

    // C4 OP_SHR_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB | DF_SHR_EXPRESSION,

    // C5 OP_USHR_LONG_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB | DF_USHR_EXPRESSION,

    // C6 OP_ADD_FLOAT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_FP_A | DF_FP_B | DF_ADD_EXPRESSION,

    // C7 OP_SUB_FLOAT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_FP_A | DF_FP_B | DF_SUBTRACT_EXPRESSION,

    // C8 OP_MUL_FLOAT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_FP_A | DF_FP_B | DF_MULTIPLY_EXPRESSION,

    // C9 OP_DIV_FLOAT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_FP_A | DF_FP_B | DF_DIVIDE_EXPRESSION,

    // CA OP_REM_FLOAT_2ADDR vA, vB
    DF_DA | DF_UA | DF_UB | DF_FP_A | DF_FP_B | DF_REMAINDER_EXPRESSION,

    // CB OP_ADD_DOUBLE_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_FP_A | DF_FP_B | DF_ADD_EXPRESSION,

    // CC OP_SUB_DOUBLE_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_FP_A | DF_FP_B | DF_SUBTRACT_EXPRESSION,

    // CD OP_MUL_DOUBLE_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_FP_A | DF_FP_B | DF_MULTIPLY_EXPRESSION,

    // CE OP_DIV_DOUBLE_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_FP_A | DF_FP_B | DF_DIVIDE_EXPRESSION,

    // CF OP_REM_DOUBLE_2ADDR vA, vB
    DF_DA_WIDE | DF_UA_WIDE | DF_UB_WIDE | DF_FP_A | DF_FP_B | DF_REMAINDER_EXPRESSION,

    // D0 OP_ADD_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_LINEAR | DF_ADD_EXPRESSION,

    // D1 OP_RSUB_INT vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_SUBTRACT_EXPRESSION,

    // D2 OP_MUL_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_MULTIPLY_EXPRESSION,

    // D3 OP_DIV_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_DIVIDE_EXPRESSION,

    // D4 OP_REM_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_REMAINDER_EXPRESSION,

    // D5 OP_AND_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_AND_EXPRESSION,

    // D6 OP_OR_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_OR_EXPRESSION,

    // D7 OP_XOR_INT_LIT16 vA, vB, #+CCCC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_XOR_EXPRESSION,

    // D8 OP_ADD_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_LINEAR | DF_ADD_EXPRESSION,

    // D9 OP_RSUB_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_SUBTRACT_EXPRESSION,

    // DA OP_MUL_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_MULTIPLY_EXPRESSION,

    // DB OP_DIV_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_DIVIDE_EXPRESSION,

    // DC OP_REM_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_REMAINDER_EXPRESSION,

    // DD OP_AND_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_AND_EXPRESSION,

    // DE OP_OR_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_OR_EXPRESSION,

    // DF OP_XOR_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_XOR_EXPRESSION,

    // E0 OP_SHL_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_SHL_EXPRESSION,

    // E1 OP_SHR_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_SHR_EXPRESSION,

    // E2 OP_USHR_INT_LIT8 vAA, vBB, #+CC
    DF_DA | DF_UB | DF_C_IS_CONST | DF_USHR_EXPRESSION,

    // E3 OP_IGET_VOLATILE
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // E4 OP_IPUT_VOLATILE
    DF_UA | DF_UB | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // E5 OP_SGET_VOLATILE
    DF_DA | DF_IS_GETTER,

    // E6 OP_SPUT_VOLATILE
    DF_UA | DF_IS_SETTER,

    // E7 OP_IGET_OBJECT_VOLATILE
    DF_DA | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // E8 OP_IGET_WIDE_VOLATILE
    DF_DA_WIDE | DF_UB | DF_C_IS_CONST | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // E9 OP_IPUT_WIDE_VOLATILE
    DF_UA_WIDE | DF_UB  | DF_C_IS_CONST | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_2,

    // EA OP_SGET_WIDE_VOLATILE
    DF_DA_WIDE | DF_IS_GETTER,

    // EB OP_SPUT_WIDE_VOLATILE
    DF_UA_WIDE | DF_IS_SETTER,

    // EC OP_BREAKPOINT
    DF_NOP,

    // ED OP_THROW_VERIFICATION_ERROR
    DF_NOP,

    // EE OP_EXECUTE_INLINE
    DF_FORMAT_35C | DF_IS_CALL,

    // EF OP_EXECUTE_INLINE_RANGE
    DF_FORMAT_3RC | DF_IS_CALL,

    // F0 OP_INVOKE_OBJECT_INIT_RANGE
    DF_NOP,

    // F1 OP_RETURN_VOID_BARRIER
    DF_NOP,

    // F2 OP_IGET_QUICK
    DF_DA | DF_UB | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // F3 OP_IGET_WIDE_QUICK
    DF_DA_WIDE | DF_UB | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // F4 OP_IGET_OBJECT_QUICK
    DF_DA | DF_UB | DF_IS_GETTER | DF_NULL_OBJECT_CHECK_0,

    // F5 OP_IPUT_QUICK
    DF_UA | DF_UB | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // F6 OP_IPUT_WIDE_QUICK
    DF_UA_WIDE | DF_UB | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_2,

    // F7 OP_IPUT_OBJECT_QUICK
    DF_UA | DF_UB | DF_IS_SETTER | DF_NULL_OBJECT_CHECK_1,

    // F8 OP_INVOKE_VIRTUAL_QUICK
    DF_FORMAT_35C | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // F9 OP_INVOKE_VIRTUAL_QUICK_RANGE
    DF_FORMAT_3RC | DF_NULL_OBJECT_CHECK_0 | DF_IS_CALL,

    // FA OP_INVOKE_SUPER_QUICK
    DF_FORMAT_35C | DF_IS_CALL,

    // FB OP_INVOKE_SUPER_QUICK_RANGE
    DF_FORMAT_3RC | DF_IS_CALL,

    // FC OP_IPUT_OBJECT_VOLATILE
    DF_UA | DF_UB,

    // FD OP_SGET_OBJECT_VOLATILE
    DF_DA,

    // FE OP_SPUT_OBJECT_VOLATILE
    DF_UA,

    // FF OP_UNUSED_FF
    DF_NOP,

    /*
     * This is the beginning of the extended MIR opcodes. We make sure that the
     * more complex ones receives the tag for extended format so that we can treat
     * the uses and defs specially.
     */

    //kMirOpPhi
    DF_DA,

    //kMirOpNullNRangeUpCheck
    DF_FORMAT_EXT_OP,

    //kMirOpNullNRangeDownCheck
    DF_FORMAT_EXT_OP,

    //kMirOpLowerBound
    DF_FORMAT_EXT_OP,

    //kMirOpPunt
    DF_NOP,

    //kMirOpCheckInlinePrediction
    DF_UC | DF_NULL_OBJECT_CHECK_0,

    //kMirOpNullCheck
    DF_FORMAT_EXT_OP | DF_NULL_OBJECT_CHECK_0,

    //kMirOpBoundCheck
    DF_FORMAT_EXT_OP,

    //kMirOpRegisterize
    DF_FORMAT_EXT_OP,

    //kMirOpConst128b
    DF_NOP,

    //kMirOpMove128b,
    DF_NOP,

    //kMirOpPackedMultiply,
    DF_NOP,

    //kMirOpPackedAddition,
    DF_NOP,

    //kMirOpPackedSubtract,
    DF_NOP,

    //kMirOpPackedShiftLeft,
    DF_NOP,

    //kMirOpPackedSignedShiftRight,
    DF_NOP,

    //kMirOpPackedUnsignedShiftRight,
    DF_NOP,

    //kMirOpPackedAnd,
    DF_NOP,

    //kMirOpPackedOr,
    DF_NOP,

    //kMirOpPackedXor,
    DF_NOP,

    //kMirOpPackedAddReduce,
    DF_DA | DF_UA,

    //kMirOpPackedReduce,
    DF_DA,

    //kMirOpPackedSet,
    DF_UB,

    //kMirOpCheckStackOverflow
    DF_NOP,
};

/* Return the Dalvik register/subscript pair of a given SSA register */
int dvmConvertSSARegToDalvik(const CompilationUnit *cUnit, int ssaReg)
{
      return GET_ELEM_N(cUnit->ssaToDalvikMap, int, ssaReg);
}

/**
 * @brief Extract the SSA subscript from a SSA register
 * @param cUnit the CompilationUnit
 * @param ssaReg the SSA register we want to extract from
 * @return the subscript for ssaReg
 */
unsigned int dvmExtractSSASubscript (const CompilationUnit *cUnit, int ssaReg)
{
    //First get the dalvik register
    int reg = dvmConvertSSARegToDalvik (cUnit, ssaReg);

    //Then get the subscript from it
    return DECODE_SUB (reg);
}

/**
 * @brief Extract the SSA register value from a SSA register
 * @param cUnit the CompilationUnit
 * @param ssaReg the SSA register we want to extract from
 * @return the register for ssaReg
 */
unsigned int dvmExtractSSARegister (const CompilationUnit *cUnit, int ssaReg)
{
    //First get the dalvik register
    int reg = dvmConvertSSARegToDalvik (cUnit, ssaReg);

    //Then get the subscript from it
    return DECODE_REG (reg);
}

/*
 * Utility function to convert encoded SSA register value into Dalvik register
 * and subscript pair. Each SSA register can be used to index the
 * ssaToDalvikMap list to get the subscript[31..16]/dalvik_reg[15..0] mapping.
 */
char *dvmCompilerGetDalvikDisassembly(const DecodedInstruction *insn,
                                      const char *note)
{
    char buffer[256];
    Opcode opcode = insn->opcode;
    long long dfAttributes = dvmCompilerDataFlowAttributes[opcode];
    int flags;
    char *ret;

    buffer[0] = 0;
    if ((int)opcode >= (int)kMirOpFirst) {
        dvmCompilerExtendedDisassembler (NULL, NULL, insn, buffer, sizeof (buffer));
        flags = 0;
    } else {
        strcpy(buffer, dexGetOpcodeName(opcode));
        flags = dexGetFlagsFromOpcode(insn->opcode);
    }

    /* For branches, decode the instructions to print out the branch targets */
    int len = strlen (buffer);
    int left = sizeof (buffer) - len;

    if (note) {
        strncat(buffer, note, left);
        len = strlen (buffer);
        left = sizeof (buffer) - len;
    }

    if (flags & kInstrCanBranch) {
        InstructionFormat dalvikFormat = dexGetFormatFromOpcode(insn->opcode);
        int offset = 0;
        switch (dalvikFormat) {
            case kFmt21t:
                snprintf(buffer + len, left, " v%d,", insn->vA);
                offset = (int) insn->vB;
                break;
            case kFmt22t:
                snprintf(buffer + len, left, " v%d, v%d,",
                         insn->vA, insn->vB);
                offset = (int) insn->vC;
                break;
            case kFmt10t:
            case kFmt20t:
            case kFmt30t:
                offset = (int) insn->vA;
                break;
            default:
                ALOGE("Unexpected branch format %d / opcode %#x", dalvikFormat,
                     opcode);
                dvmAbort();
                break;
        }

        len = strlen (buffer);
        left = sizeof (buffer) - len;
        snprintf(buffer + len, left, " (%c%x)",
                 offset > 0 ? '+' : '-',
                 offset > 0 ? offset : -offset);
    } else if (dfAttributes & DF_FORMAT_35C) {
        unsigned int i;
        for (i = 0; i < insn->vA; i++) {
            const char* sep = (i == 0) ? "" : ",";

            len = strlen (buffer);
            left = sizeof (buffer) - len;
            snprintf(buffer + len, left, "%s v%d", sep, insn->arg[i]);
        }
    }
    else if (dfAttributes & DF_FORMAT_3RC) {
        snprintf(buffer + len, left,
                 " v%d..v%d", insn->vC, insn->vC + insn->vA - 1);
    }
    else {
        if (dfAttributes & DF_A_IS_REG) {
            snprintf(buffer + len, left, " v%d", insn->vA);
            len = strlen (buffer);
            left = sizeof (buffer) - len;
        }

        if (dfAttributes & DF_B_IS_REG) {
            snprintf(buffer + len, left, ", v%d", insn->vB);
        }
        else if ((int)opcode < (int)kMirOpFirst) {
            snprintf(buffer + len, left, ", (#%d)", insn->vB);
        }

        //Recalculate
        len = strlen (buffer);
        left = sizeof (buffer) - len;

        if (dfAttributes & DF_C_IS_REG) {
            snprintf(buffer + len, left, ", v%d", insn->vC);
        }
        else if ((int)opcode < (int)kMirOpFirst || (dfAttributes & DF_C_IS_CONST) != 0) {
            snprintf(buffer + len, left, ", (#%d)", insn->vC);
        }
    }
    int length = strlen(buffer) + 1;
    ret = (char *)dvmCompilerNew(length, false);
    memcpy(ret, buffer, length);
    return ret;
}

char *getSSAName(const CompilationUnit *cUnit, int ssaReg, char *name)
{
    const char *printingTemplate = "v%d_%d";
    int virtualReg = dvmExtractSSARegister (cUnit, ssaReg);
    int subscript = dvmExtractSSASubscript (cUnit, ssaReg);

    if (dvmCompilerIsPureLocalScratch (cUnit, virtualReg) == true)
    {
        printingTemplate = "t%d_%d";
    }

    sprintf(name, printingTemplate, virtualReg, subscript);
    return name;
}


/**
 * @brief Add a dissassembler for the extended instructions
 * @param cUnit the CompilationUnit (can be NULL)
 * @param mir the MIR (can be NULL)
 * @param insn the DecodedInstruction
 * @param buffer the buffer in which to dissassemble
 * @param len length of the buffer
 */
void dvmCompilerExtendedDisassembler (const CompilationUnit *cUnit,
                                       const MIR *mir,
                                       const DecodedInstruction *insn,
                                       char *buffer,
                                       int len)
{
    char operand0[256],
         operand1[256],
         operand2[256];
    //Buffer and insn cannot be NULL, mir and cUnit can
    assert (buffer != NULL && insn != NULL);

    //Set to 0
    buffer[0] = 0;

    ExtendedMIROpcode value = static_cast<ExtendedMIROpcode> (insn->opcode);

    //If not an extended, we can just send it to whoever is best suited
    if (value < kMirOpFirst)
    {
        char *decodedInstruction;

        if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
        {
            decodedInstruction = dvmCompilerFullDisassembler(cUnit, mir);
        }
        else
        {
            decodedInstruction = dvmCompilerGetDalvikDisassembly(insn, NULL);
        }

        assert (decodedInstruction != 0);

        //We want to put a note about the inlining status
        const char * inliningNote = "";
        if ((mir->OptimizationFlags & MIR_INLINED) != 0)
        {
            inliningNote = " (no-op)";
        }
        else if ((mir->OptimizationFlags & MIR_INLINED_PRED) != 0)
        {
            inliningNote = " (prediction inline)";
        }
        else if ((mir->OptimizationFlags & MIR_CALLEE) != 0)
        {
            inliningNote = " (inlined)";
        }

        //We would like to put a note as well in case of null/range check elimination
        const char * checkEliminationNote = "";
        if ( (mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) != 0
                && (mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK) != 0 )
        {
            checkEliminationNote = " (N | B)";
        }
        else
        {
            if ((mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK) != 0)
            {
                checkEliminationNote = " (B)";
            }
            else
            {
                if ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK)  != 0)
                {
                    checkEliminationNote = " (N)";
                }
            }
        }

        const unsigned int renameNoteSize = 32;
        char renamingOffsetNote[renameNoteSize];

        if (mir->virtualRegRenameOffset != 0)
        {
            snprintf (renamingOffsetNote, renameNoteSize, " (renamed: %d)", mir->virtualRegRenameOffset);
        }
        else
        {
            renamingOffsetNote[0] = 0;
        }

        //Now actually put everything in the desired buffer
        snprintf (buffer, len, "%s%s%s%s", decodedInstruction, renamingOffsetNote, checkEliminationNote, inliningNote);

        return;
    }

    switch (value)
    {
        case kMirOpPhi:
            {
                //Start by putting PHI
                snprintf (buffer, len, "kMirOpPhi");

                if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
                {
                    int i, left, offset;

                    offset = strlen (buffer);
                    left = len - strlen (buffer);

                    if (left < 0)
                        break;

                    snprintf(buffer + offset, left, " %s = (%s",
                            getSSAName(cUnit, mir->ssaRep->defs[0], operand0),
                            getSSAName(cUnit, mir->ssaRep->uses[0], operand1));
                    for (i = 1; i < mir->ssaRep->numUses; i++) {
                        offset = strlen (buffer);
                        left = len - offset;

                        if (left <= 0)
                            break;

                        snprintf(buffer + strlen (buffer), left, ", %s",
                                getSSAName(cUnit, mir->ssaRep->uses[i], operand0));
                    }

                    offset = strlen (buffer);
                    left = len - offset;

                    if (left > 1)
                        snprintf(buffer + offset, left, ")");
                }
            }
            break;
        case kMirOpNullNRangeUpCheck:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpNullNRangeUpCheck: %s[%s], cond reg %s, m: %d, M: %d, b: %d",
                        getSSAName (cUnit, insn->vA, operand0),
                        getSSAName (cUnit, insn->vB, operand1),
                        getSSAName (cUnit, insn->vC, operand2),
                        insn->arg[0],
                        insn->arg[1],
                        insn->arg[2]);
            }
            else
            {
                snprintf (buffer, len, "kMirOpNullNRangeUpCheck: v%d[v%d], cond reg v%d, m: %d, M: %d, b: %d",
                        insn->vA,
                        insn->vB,
                        insn->vC,
                        insn->arg[0],
                        insn->arg[1],
                        insn->arg[2]);
            }
            break;
        case kMirOpNullNRangeDownCheck:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpNullNRangeDownCheck: %s[%s], cond reg %s, m: %d, M: %d, b: %d",
                        getSSAName (cUnit, insn->vA, operand0),
                        getSSAName (cUnit, insn->vB, operand1),
                        getSSAName (cUnit, insn->vC, operand2),
                        insn->arg[0],
                        insn->arg[1],
                        insn->arg[2]);
            }
            else
            {
                snprintf (buffer, len, "kMirOpNullNRangeDownCheck: v%d[v%d], cond reg v%d, m: %d, M: %d, b: %d",
                        insn->vA,
                        insn->vB,
                        insn->vC,
                        insn->arg[0],
                        insn->arg[1],
                        insn->arg[2]);
            }
            break;
        case kMirOpLowerBound:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpLowerBound: %s, min: %d",
                        getSSAName (cUnit, mir->dalvikInsn.vB, operand0),
                        mir->dalvikInsn.vB);
            }
            else
            {
                snprintf (buffer, len, "kMirOpLowerBound: v%d, min: %d",
                        insn->vA,
                        insn->vB);
            }
            break;
        case kMirOpPunt:
            snprintf (buffer, len, "kMirOpPunt");
            break;
        case kMirOpCheckInlinePrediction:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpCheckInlinePrediction: %s, Class %s",
                        getSSAName (cUnit, mir->dalvikInsn.vC, operand0),
                        mir->meta.callsiteInfo != 0 ? mir->meta.callsiteInfo->classDescriptor : "Unknown");
            }
            else
            {
                snprintf (buffer, len, "kMirOpCheckInlinePrediction: v%d, Class %s", insn->vC,
                        mir != 0 && mir->meta.callsiteInfo != 0 ? mir->meta.callsiteInfo->classDescriptor : "Unknown");
            }
            break;
        case kMirOpRegisterize: {
            /* The kMirOpRegisterize uses vB as the type of register:
             *    - kCoreReg -> is a general purpose register
             *    - kFPReg -> general floating point register
             *    - kSFPReg -> single floating point (uses movss for x86 for example)
             *    - kDFReg -> double floating point (uses movq for x86 for example)
             *    - kX87Reg -> x87 register
             *    - kAnyReg -> is put here for completeness of regClass but registerization should not use that
             */
            const char * regClass;
            switch (insn->vB) {
                case kCoreReg:
                    regClass = "core";
                    break;
                case kFPReg:
                    regClass = "FP";
                    break;
                case kSFPReg:
                    regClass = "Single FP";
                    break;
                case kDFPReg:
                    regClass = "Double FP";
                    break;
                case kAnyReg:
                    regClass = "any";
                    break;
                case kX87Reg:
                    regClass = "X87";
                    break;
                default:
                    regClass = "invalid";
                    break;
            }
            snprintf (buffer, len, "kMirOpRegisterize: v%d %s", insn->vA, regClass);
            break;
        }
        case kMirOpMove128b:
            snprintf (buffer, len, "kMirOpMove128b xmm%d = xmm%d", insn->vA, insn->vB);
            break;
        case kMirOpPackedSet:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpPackedSet xmm%d = %s, size %d",
                          insn->vA,
                          getSSAName (cUnit, mir->ssaRep->uses[0], operand0),
                          insn->vC);
            }
            else
            {
                snprintf (buffer, len, "kMirOpPackedSet xmm%d = v%d, size %d",
                          insn->vA,
                          insn->vB,
                          insn->vC);
            }
            break;
        case kMirOpConst128b:
            snprintf (buffer, len, "kMirOpConst128DW xmm%d = %x, %x, %x, %x", insn->vA, insn->arg[0], insn->arg[1],
                    insn->arg[2], insn->arg[3]);
            break;
        case kMirOpPackedAddition:
            snprintf (buffer, len, "kMirOpPackedAddition xmm%d = xmm%d + xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedMultiply:
            snprintf (buffer, len, "kMirOpPackedMultiply xmm%d = xmm%d * xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedSubtract:
            snprintf (buffer, len, "kMirOpPackedSubtract xmm%d = xmm%d - xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedAnd:
            snprintf (buffer, len, "kMirOpPackedAnd xmm%d = xmm%d & xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedOr:
            snprintf (buffer, len, "kMirOpPackedOr xmm%d = xmm%d | xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedXor:
            snprintf (buffer, len, "kMirOpPackedXor xmm%d = xmm%d ^ xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedShiftLeft:
            snprintf (buffer, len, "kMirOpPackedShiftLeft xmm%d = xmm%d << xmm%d, size %d", insn->vA, insn->vA,
                    insn->vB, insn->vC);
            break;
        case kMirOpPackedUnsignedShiftRight:
            snprintf (buffer, len, "kMirOpPackedUnsignedShiftRight xmm%d = xmm%d >>> xmm%d, size %d", insn->vA,
                    insn->vA, insn->vB, insn->vC);
            break;
        case kMirOpPackedSignedShiftRight:
            snprintf (buffer, len, "kMirOpPackedSignedShiftRight xmm%d = xmm%d >> xmm%d, size %d", insn->vA, insn->vA, insn->vB,
                    insn->vC);
            break;
        case kMirOpPackedAddReduce:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpPackedAddReduce %s = xmm%d + %s, size %d",
                          getSSAName (cUnit, mir->ssaRep->defs[0], operand0),
                          insn->vB,
                          getSSAName (cUnit, mir->ssaRep->uses[0], operand1),
                          insn->vC);
            }
            else
            {
                snprintf (buffer, len, "kMirOpPackedAddReduce v%d = xmm%d + v%d, size %d", insn->vA, insn->vB, insn->vA,
                        insn->vC);
            }
            break;
        case kMirOpPackedReduce:
            if (cUnit != NULL && mir != NULL && mir->ssaRep != NULL)
            {
                snprintf (buffer, len, "kMirOpPackedReduce %s = xmm%d, size %d",
                        getSSAName (cUnit, mir->ssaRep->defs[0], operand0),
                        insn->vB, insn->vC);
            }
            else
            {
                snprintf (buffer, len, "kMirOpPackedReduce v%d = xmm%d, size %d", insn->vA, insn->vB, insn->vC);
            }
            break;
        case kMirOpNullCheck:
            if (mir != 0)
            {
                snprintf (buffer, len, "kMirOpNullCheck v%d offset:0x%x", insn->vA, mir->offset);
            }
            else
            {
                snprintf (buffer, len, "kMirOpNullCheck v%d", insn->vA);
            }
            break;
        case kMirOpCheckStackOverflow:
            snprintf (buffer, len, "kMirOpCheckStackOverflow #%d", insn->vB);
            break;
        default:
            snprintf (buffer, len, "Unknown Extended Opcode");
            break;
    }
}

/*
 * Dalvik instruction disassembler with optional SSA printing.
 */
char *dvmCompilerFullDisassembler(const CompilationUnit *cUnit,
                                  const MIR *mir)
{
    char buffer[256];
    char operand0[256], operand1[256];
    const DecodedInstruction *insn = &mir->dalvikInsn;
    int opcode = insn->opcode;
    long long dfAttributes = dvmCompilerDataFlowAttributes[opcode];
    char *ret;
    int length;
    int offset, left;
    OpcodeFlags flags;

    buffer[0] = 0;
    if (opcode >= kMirOpFirst) {
        dvmCompilerExtendedDisassembler (cUnit, mir, insn, buffer, sizeof (buffer));
        goto done;
    } else {
        strcpy(buffer, dexGetOpcodeName((Opcode)opcode));
    }

    flags = dexGetFlagsFromOpcode((Opcode)opcode);
    offset = strlen (buffer);
    left = sizeof (buffer) - offset;
    /* For branches, decode the instructions to print out the branch targets */
    if (flags & kInstrCanBranch) {
        InstructionFormat dalvikFormat = dexGetFormatFromOpcode(insn->opcode);
        int delta = 0;
        switch (dalvikFormat) {
            case kFmt21t:
                snprintf(buffer + offset, left, " %s, ",
                         getSSAName(cUnit, mir->ssaRep->uses[0], operand0));
                delta = (int) insn->vB;
                break;
            case kFmt22t:
                snprintf(buffer + offset, left, " %s, %s, ",
                         getSSAName(cUnit, mir->ssaRep->uses[0], operand0),
                         getSSAName(cUnit, mir->ssaRep->uses[1], operand1));
                delta = (int) insn->vC;
                break;
            case kFmt10t:
            case kFmt20t:
            case kFmt30t:
                delta = (int) insn->vA;
                break;
            default:
                ALOGE("Unexpected branch format: %d", dalvikFormat);
                dvmAbort();
                break;
        }
        offset = strlen (buffer);
        left = sizeof (buffer) - offset;
        snprintf(buffer + offset, left, " %04x",
                 mir->offset + delta);
    } else if (dfAttributes & (DF_FORMAT_35C | DF_FORMAT_3RC)) {
        unsigned int i;
        for (i = 0; i < insn->vA; i++) {
            const char* sep = (i == 0) ? " " : ", ";

            offset = strlen (buffer);
            left = sizeof (buffer) - offset;
            snprintf(buffer + offset, left, "%s%s", sep,
                     getSSAName(cUnit, mir->ssaRep->uses[i], operand0));
        }

        //Now print some more information about method being invoked
        InstructionFormat dalvikFormat = dexGetFormatFromOpcode ((Opcode) opcode);
        offset = strlen (buffer);
        left = sizeof(buffer) - offset;
        switch (dalvikFormat)
        {
            case kFmt35ms:
            case kFmt3rms:
                //For quick invokes, this offset represents index into vtable
                snprintf (buffer + offset, left, " vtable[#%#x]", insn->vB);
                break;
            case kFmt35mi:
            case kFmt3rmi:
            {
                //For execute-inline, the offset represents an inline operation
                const InlineOperation &operation = gDvmInlineOpsTable[insn->vB];
                snprintf (buffer + offset, left, " %s.%s%s", operation.classDescriptor, operation.methodName,
                        operation.methodSignature);
                break;
            }
            default:
                break;
        }
    } else {
        int udIdx;
        if (mir->ssaRep->numDefs) {

            for (udIdx = 0; udIdx < mir->ssaRep->numDefs; udIdx++) {
                offset = strlen (buffer);
                left = sizeof (buffer) - offset;
                snprintf(buffer + offset, left, " %s",
                         getSSAName(cUnit, mir->ssaRep->defs[udIdx], operand0));
            }
            offset = strlen (buffer);
            left = sizeof (buffer) - offset;
            strncat(buffer, ",", left);
        }
        if (mir->ssaRep->numUses) {
            /* No leading ',' for the first use */
            offset = strlen (buffer);
            left = sizeof (buffer) - offset;
            snprintf(buffer + offset, left, " %s",
                     getSSAName(cUnit, mir->ssaRep->uses[0], operand0));
            for (udIdx = 1; udIdx < mir->ssaRep->numUses; udIdx++) {
                offset = strlen (buffer);
                left = sizeof (buffer) - offset;
                snprintf(buffer + offset, left, ", %s",
                         getSSAName(cUnit, mir->ssaRep->uses[udIdx], operand0));
            }
        }
        if (opcode < kMirOpFirst) {
            InstructionFormat dalvikFormat =
                dexGetFormatFromOpcode((Opcode)opcode);
            offset = strlen (buffer);
            left = sizeof (buffer) - offset;
            switch (dalvikFormat) {
                case kFmt11n:        // op vA, #+B
                case kFmt21s:        // op vAA, #+BBBB
                case kFmt21h:        // op vAA, #+BBBB00000[00000000]
                case kFmt31i:        // op vAA, #+BBBBBBBB
                    snprintf(buffer + offset, left, " #%#x", insn->vB);
                    break;
                case kFmt51l:        // op vAA, #+BBBBBBBBBBBBBBBB
                    snprintf(buffer + offset, left, " #%#llx", insn->vB_wide);
                    break;
                case kFmt21c:        // op vAA, thing@BBBB
                case kFmt31c:        // op vAA, thing@BBBBBBBB
                    snprintf(buffer + offset, left, " @%#x", insn->vB);
                    break;
                case kFmt22b:        // op vAA, vBB, #+CC
                case kFmt22s:        // op vA, vB, #+CCCC
                    snprintf(buffer + offset, left, " #%#x", insn->vC);
                    break;
                case kFmt22c:        // op vA, vB, thing@CCCC
                case kFmt22cs:       // [opt] op vA, vB, field offset CCCC
                    snprintf(buffer + offset, left, " @%#x", insn->vC);
                    break;
                    /* No need for special printing */
                default:
                    break;
            }
        }
    }

done:
    length = strlen(buffer) + 1;
    ret = (char *) dvmCompilerNew(length, false);
    memcpy(ret, buffer, length);
    return ret;
}

/*
 * Utility function to convert encoded SSA register value into Dalvik register
 * and subscript pair. Each SSA register can be used to index the
 * ssaToDalvikMap list to get the subscript[31..16]/dalvik_reg[15..0] mapping.
 */
char *dvmCompilerGetSSAString(CompilationUnit *cUnit, SSARepresentation *ssaRep)
{
    char buffer[256];
    char *ret;
    int i;

    buffer[0] = 0;
    for (i = 0; i < ssaRep->numDefs; i++) {
        int ssa2DalvikValue = dvmConvertSSARegToDalvik(cUnit, ssaRep->defs[i]);

        sprintf(buffer + strlen(buffer), "s%d(v%d_%d) ",
                ssaRep->defs[i], DECODE_REG(ssa2DalvikValue),
                DECODE_SUB(ssa2DalvikValue));
    }

    if (ssaRep->numDefs) {
        strncat(buffer, "<- ", sizeof(buffer) - strlen(buffer));
    }

    for (i = 0; i < ssaRep->numUses; i++) {
        int ssa2DalvikValue = dvmConvertSSARegToDalvik(cUnit, ssaRep->uses[i]);
        int len = strlen(buffer);

        if (snprintf(buffer + len, 250 - len, "s%d(v%d_%d) ",
                     ssaRep->uses[i], DECODE_REG(ssa2DalvikValue),
                     DECODE_SUB(ssa2DalvikValue)) >= (250 - len)) {
            strncat(buffer, "...", sizeof(buffer) - strlen(buffer));
            break;
        }
    }

    int length = strlen(buffer) + 1;
    ret = (char *)dvmCompilerNew(length, false);
    memcpy(ret, buffer, length);
    return ret;
}

/* Set any register that is used before being defined */
static inline void handleUse(BitVector *useV, BitVector *defV, int dalvikRegId)
{
    //If it has been defined before, don't set it, it is dead
    if (dvmIsBitSet(defV, dalvikRegId) == 0) {
        dvmCompilerSetBit(useV, dalvikRegId);
    }
}

/* Mark a reg as being defined */
static inline void handleDef(BitVector *defV, int dalvikRegId)
{
    dvmCompilerSetBit(defV, dalvikRegId);
}

/* Handle use for extended op */
void handleExtOpUses (BitVector *useV, BitVector *defV, MIR *mir)
{
    DecodedInstruction* dInsn = &(mir->dalvikInsn);

    switch (static_cast<int> (dInsn->opcode))
    {
        case kMirOpNullNRangeUpCheck:
        case kMirOpNullNRangeDownCheck:
            //vA holds the array pointer register
            handleUse (useV, defV, dInsn->vA);
            //vB holds the index register
            handleUse (useV, defV, dInsn->vB);
            break;
        case kMirOpLowerBound:
            //vA holds the index register
            handleUse (useV, defV, dInsn->vA);
            break;
        case kMirOpNullCheck:
            //We only reference the register if we need to do a null check
            if ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
            {
                //vA holds object reference register
                handleUse (useV, defV, dInsn->vA);
            }
            break;
        case kMirOpBoundCheck:
            //vA holds object reference register
            handleUse (useV, defV, dInsn->vA);

            //We also have a use if we have an index register
            if (dInsn->arg[0] == MIR_BOUND_CHECK_REG)
            {
                handleUse (useV, defV, dInsn->arg[1]);
            }
            break;
        case kMirOpRegisterize:
            //vA holds the register number we want to registerize
            handleUse(useV, defV, dInsn->vA);

            //The type of usage depends on whether we have a wide VR. So we check if we can make that it
            //a non-wide use. If we cannot tell that it is surely non-wide, then make conservative assumption
            //that it is a wide use.
            if (dInsn->vB != static_cast<u4> (kCoreReg) && dInsn->vB != static_cast<u4> (kSFPReg))
            {
                //We add a use for VR represented by high bits
                handleUse (useV, defV, dInsn->vA + 1);
            }
            break;
        default:
            ALOGW("JIT_INFO: Unexpected Extended opcode %#x", dInsn->opcode);
            break;
    }
}

/* Handle def extended op */
void handleExtOpDefs (BitVector *defV, DecodedInstruction* dInsn)
{
    switch (static_cast<int> (dInsn->opcode))
    {
        case kMirOpNullNRangeUpCheck:
        case kMirOpNullNRangeDownCheck:
        case kMirOpLowerBound:
        case kMirOpNullCheck:
        case kMirOpBoundCheck:
        case kMirOpRegisterize:
            //No defs
            break;
        default:
            ALOGW("JIT_INFO: Unexpected Extended opcode %#x", dInsn->opcode);
            break;
    }
}

/*
 * Find out live-in variables for natural loops. Variables that are live-in in
 * the main loop body are considered to be defined in the entry block.
 */
bool dvmCompilerFindLocalLiveIn(CompilationUnit *cUnit, BasicBlock *bb)
{
    if (bb->dataFlowInfo == NULL) return false;

    //If not allocated yet
    if (bb->dataFlowInfo->useV == 0)
    {
        bb->dataFlowInfo->useV = dvmCompilerAllocBitVector (cUnit->numDalvikRegisters, true);
    }
    else
    {
        dvmEnsureSizeAndClear (bb->dataFlowInfo->useV, cUnit->numDalvikRegisters);
    }

    //If not allocated yet
    if (bb->dataFlowInfo->defV == 0)
    {
        bb->dataFlowInfo->defV = dvmCompilerAllocBitVector (cUnit->numDalvikRegisters, true);
    }
    else
    {
        dvmEnsureSizeAndClear (bb->dataFlowInfo->defV, cUnit->numDalvikRegisters);
    }

    //If not allocated yet
    if (bb->dataFlowInfo->liveInV == 0)
    {
        bb->dataFlowInfo->liveInV = dvmCompilerAllocBitVector (cUnit->numDalvikRegisters, true);
    }
    else
    {
        dvmEnsureSizeAndClear (bb->dataFlowInfo->liveInV, cUnit->numDalvikRegisters);
    }

    //If not allocated yet
    if (bb->dataFlowInfo->liveOutV == 0)
    {
        bb->dataFlowInfo->liveOutV = dvmCompilerAllocBitVector (cUnit->numDalvikRegisters, true);
    }
    else
    {
        dvmEnsureSizeAndClear (bb->dataFlowInfo->liveOutV, cUnit->numDalvikRegisters);
    }

    //Get local versions
    BitVector *defV = bb->dataFlowInfo->defV;
    BitVector *useV = bb->dataFlowInfo->useV;

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        long long dfAttributes = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];
        DecodedInstruction *dInsn = &mir->dalvikInsn;

        //If backend can bail out, ensure that all reaching definitions are uses
        if (backendCanBailOut (cUnit, mir) == true)
        {
            //At this point we could actually compute reaching definitions but let's just be
            //conservative that all registers in cUnit are uses. However, we can skip some
            //registers if they are from inlined method and we are in caller.

            int start = 0;
            if (mir->nesting.parent == 0)
            {
                //We are not in nested method and therefore we only need to consider caller registers.
                //The reason for this is that when we have an inlined body we have the whole scope so
                //we have no registers that are live from outside trace. Thus when we are going through
                //mir that is in caller, the callee frame doesn't even exist.
                start = cUnit->registerWindowShift;
            }

            //Go through the dalvik registers and add them as explicit uses
            for (int i = start; i < cUnit->numDalvikRegisters; i++)
            {
                //If the register we are looking at now is scratch, then we don't add a use for it
                //because as purely-scratch, it is not live out of trace.
                if (dvmArchIsPureLocalScratchRegister (cUnit->method, i, cUnit->registerWindowShift) == false)
                {
                    handleUse (useV, defV, i);
                }
            }
        }

        if (dfAttributes & DF_HAS_USES) {
            if (dfAttributes & DF_UA) {
                handleUse(useV, defV, dInsn->vA);
            } else if (dfAttributes & DF_UA_WIDE) {
                handleUse(useV, defV, dInsn->vA);
                handleUse(useV, defV, dInsn->vA + 1);
            }
            if (dfAttributes & DF_UB) {
                handleUse(useV, defV, dInsn->vB);
            } else if (dfAttributes & DF_UB_WIDE) {
                handleUse(useV, defV, dInsn->vB);
                handleUse(useV, defV, dInsn->vB + 1);
            }
            if (dfAttributes & DF_UC) {
                handleUse(useV, defV, dInsn->vC);
            } else if (dfAttributes & DF_UC_WIDE) {
                handleUse(useV, defV, dInsn->vC);
                handleUse(useV, defV, dInsn->vC + 1);
            }
        }

        if (dfAttributes & DF_HAS_DEFS) {
            handleDef(defV, dInsn->vA);
            if (dfAttributes & DF_DA_WIDE) {
                handleDef(defV, dInsn->vA + 1);
            }
        }

        //Now handle uses and defs for extended MIRs
        if ((dfAttributes & DF_FORMAT_EXT_OP) != 0)
        {
            handleExtOpUses (useV, defV, mir);
            handleExtOpDefs (defV, dInsn);
        }
    }
    return true;
}

/**
 * @brief Updates the uses vector to include the defines from current BB and its predecessors.
 * @param cUnit The compilation unit
 * @param bb The basic block whose defines we want to include in the uses of the exit
 * @param uses Updated by function to include virtual register definitions that are reaching
 * @param visited Keeps track of basic blocks we visited so we don't get stuck in cycles
 * @return If any changes were made to the uses vector.
 */
static bool initializeExitUsesHelper (CompilationUnit *cUnit, BasicBlock *bb, BitVector *uses,
        std::set<BasicBlock *> &visited)
{
    //We have nothing to do if we already visited this
    if (visited.find (bb) != visited.end ())
    {
        return false;
    }

    //Mark current BB as visited
    visited.insert (bb);

    //Eagerly assume we don't update anything
    bool changes = false;

    //If we have information about our defines then we must ensure those are added to the uses vector
    if (bb->dataFlowInfo != 0 && bb->dataFlowInfo->defV != 0)
    {
        //Update the uses to include the defines
        dvmUnifyBitVectors (uses, uses, bb->dataFlowInfo->defV);

        //Assume that the unify leads to changes
        changes = true;
    }

    //Now we want to capture the defines from all predecessors so get them
    BitVector *predecessors = bb->predecessors;

    //If we have no predecessors we have nothing to do
    if (predecessors == 0)
    {
        return changes;
    }

    //Create iterator
    BitVectorIterator bvIterator;
    dvmBitVectorIteratorInit (predecessors, &bvIterator);

    //Walk through the predecessors
    while (true)
    {
        //Get the block index
        int blockIdx = dvmBitVectorIteratorNext (&bvIterator);

        //When index is -1, we are done looking through the blocks
        if (blockIdx == -1)
        {
            break;
        }

        //Get the predecessor block
        BasicBlock *predecessor = reinterpret_cast<BasicBlock *> (dvmGrowableListGetElement (&cUnit->blockList,
                blockIdx));

        //Paranoid
        assert (predecessor != 0);

        //Initialize uses using information from predecessor
        bool initPred = initializeExitUsesHelper (cUnit, predecessor, uses, visited);

        //Make sure we keep track if we made any changes
        changes = changes || initPred;
    }

    return changes;
}

/**
 * @brief If the basic block is an exit block then we set up all reachable defines as uses.
 * @details Because we work within a trace context, we take the conservative approach that all
 * defined registers are live-out; so by adding them to the uses, the dataflow will make them
 * as live-in for the exit blocks.
 * @param cUnit The compilation unit.
 * @param bb The basic block we want to look at.
 * @return Returns if any changes were done to the basic blocks.
 */
bool dvmCompilerInitializeExitUses (CompilationUnit *cUnit, BasicBlock *bb)
{
    //This must be an exit block so it can be exit type, chaining cell, or exception block
    bool isChainingCell = (bb->blockType >= kChainingCellNormal && bb->blockType <= kChainingCellLast);
    bool isExitBlock = (bb->blockType == kExitBlock);
    bool isExceptionBlock = (bb->blockType == kExceptionHandling);

    if (isChainingCell == true || isExitBlock == true || isExceptionBlock == true)
    {
        //Make sure that dataflow information has been initialized
        if (bb->dataFlowInfo != 0)
        {
            if (bb->dataFlowInfo->useV == 0)
            {
                //Allocate space for the uses information
                bb->dataFlowInfo->useV = dvmCompilerAllocBitVector (cUnit->numDalvikRegisters, false);
            }

            std::set <BasicBlock *> visited;

            //Initialize the uses for this BB to be the reaching defines
            return initializeExitUsesHelper (cUnit, bb, bb->dataFlowInfo->useV, visited);
        }
    }

    //If we make it here we did not change anything for this basic block
    return false;
}

/* Find out the latest SSA register for a given Dalvik register */
static void handleSSAUse(CompilationUnit *cUnit, int *uses, int dalvikReg,
                         int regIndex)
{
    int encodedValue = cUnit->dalvikToSSAMap[dalvikReg];
    int ssaReg = DECODE_REG(encodedValue);
    uses[regIndex] = ssaReg;
}

/**
 * @brief Get the next subscript available for a given register
 * @param cUnit the CompilationUnit
 * @param dalvikReg the register we want a new subscript for
 * @return the new subscript for the given register
 */
static int getNextSubScript (CompilationUnit *cUnit, int dalvikReg)
{
    //Paranoid
    assert (cUnit != 0 && cUnit->ssaSubScripts != 0 && dalvikReg < cUnit->numDalvikRegisters);

    //Increment counter, then return it
    cUnit->ssaSubScripts[dalvikReg]++;
    return cUnit->ssaSubScripts[dalvikReg];
}

/* Setup a new SSA register for a given Dalvik register */
static void handleSSADef(CompilationUnit *cUnit, int *defs, int dalvikReg,
                         int regIndex)
{
    int ssaReg = cUnit->numSSARegs++;

    /* Bump up the subscript */
    int dalvikSub = getNextSubScript (cUnit, dalvikReg);
    int newD2SMapping = ENCODE_REG_SUB(ssaReg, dalvikSub);

    cUnit->dalvikToSSAMap[dalvikReg] = newD2SMapping;

    int newS2DMapping = ENCODE_REG_SUB(dalvikReg, dalvikSub);
    dvmInsertGrowableList(cUnit->ssaToDalvikMap, newS2DMapping);

    defs[regIndex] = ssaReg;
}

/* Look up new SSA names for format_35c instructions */
static void dataFlowSSAFormat35C(CompilationUnit *cUnit, MIR *mir)
{
    DecodedInstruction *dInsn = &mir->dalvikInsn;
    int numUses = dInsn->vA;
    int i;

    //Do we need more space ?
    if (numUses > mir->ssaRep->numUses)
    {
        mir->ssaRep->uses = static_cast <int *> (dvmCompilerNew (sizeof (* (mir->ssaRep->uses)) * numUses, false));
        mir->ssaRep->fpUse = static_cast<bool *> (dvmCompilerNew (sizeof (* (mir->ssaRep->fpUse)) * numUses, false));
        mir->ssaRep->defWhere = static_cast<MIR **> (dvmCompilerNew (sizeof (* (mir->ssaRep->defWhere)) * numUses, true));
    }

    //All structures should have enough room to hold information about uses. So update the number of uses now.
    mir->ssaRep->numUses = numUses;

    for (i = 0; i < numUses; i++) {
        handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->arg[i], i);
    }

    //We have no defines so update that now
    mir->ssaRep->numDefs = 0;
    mir->ssaRep->defs = 0;
    mir->ssaRep->usedNext = 0;
    mir->ssaRep->fpDef = 0;
}

/* Look up new SSA names for format_3rc instructions */
static void dataFlowSSAFormat3RC(CompilationUnit *cUnit, MIR *mir)
{
    DecodedInstruction *dInsn = &mir->dalvikInsn;
    int numUses = dInsn->vA;
    int i;

    //Do we need more space ?
    if (numUses > mir->ssaRep->numUses)
    {
        mir->ssaRep->uses = static_cast <int *> (dvmCompilerNew (sizeof (* (mir->ssaRep->uses)) * numUses, false));
        mir->ssaRep->fpUse = static_cast<bool *> (dvmCompilerNew (sizeof (* (mir->ssaRep->fpUse)) * numUses, false));
        mir->ssaRep->defWhere = static_cast<MIR **> (dvmCompilerNew (sizeof (* (mir->ssaRep->defWhere)) * numUses, true));
    }

    //All structures should have enough room to hold information about uses. So update the number of uses now.
    mir->ssaRep->numUses = numUses;

    for (i = 0; i < numUses; i++) {
        handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vC+i, i);
    }

    //We have no defines so update that now
    mir->ssaRep->numDefs = 0;
    mir->ssaRep->defs = 0;
    mir->ssaRep->usedNext = 0;
    mir->ssaRep->fpDef = 0;
}

/**
 * @brief Handles ssa representation for extended MIRs.
 * @param cUnit The compilation Unit
 * @param mir Extended bytecode
 */
static void dataFlowSSAFormatExtendedOp(CompilationUnit *cUnit, MIR *mir)
{
    //We need to keep track of uses and defs and just so we can put those arrays
    //on the stack, we set up some maximums.
    const int maxUses = 2;
    const int maxDefs = 1;

    //Now declare the temporary arrays we will use
    int uses[maxUses] = {}, defs[maxDefs] = {};
    bool fpUse[maxUses] = {}, fpDef[maxDefs] = {};

    //Eagerly assume there are no uses or defs
    int numUses = 0, numDefs = 0;

    switch (static_cast<int> (mir->dalvikInsn.opcode))
    {
        case kMirOpNullNRangeUpCheck:
        case kMirOpNullNRangeDownCheck:
            handleSSAUse(cUnit, uses, mir->dalvikInsn.vA, numUses);
            numUses++;
            handleSSAUse(cUnit, uses, mir->dalvikInsn.vB, numUses);
            numUses++;
            break;
        case kMirOpLowerBound:
            handleSSAUse(cUnit, uses, mir->dalvikInsn.vA, numUses);
            numUses++;
            break;
        case kMirOpNullCheck:
            //We only have a use if we need to do a null check
            if ((mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
            {
                handleSSAUse(cUnit, uses, mir->dalvikInsn.vA, numUses);
                numUses++;
            }
            break;
        case kMirOpBoundCheck:
            handleSSAUse(cUnit, uses, mir->dalvikInsn.vA, numUses);
            numUses++;

            //We also have a use if we have an index register
            if (mir->dalvikInsn.arg[0] == MIR_BOUND_CHECK_REG)
            {
                handleSSAUse(cUnit, uses, mir->dalvikInsn.arg[1], numUses);
                numUses++;
            }
            break;
        case kMirOpRegisterize:
            handleSSAUse(cUnit, uses, mir->dalvikInsn.vA, numUses);
            numUses++;

            //The type of usage depends on whether we have a wide VR. So we check if we can make that it
            //a non-wide use. If we cannot tell that it is surely non-wide, then make conservative assumption
            //that it is a wide use.
            if (mir->dalvikInsn.vB != static_cast<u4> (kCoreReg) && mir->dalvikInsn.vB != static_cast<u4> (kSFPReg))
            {
                handleSSAUse(cUnit, uses, mir->dalvikInsn.vA + 1, numUses);
                numUses++;
            }
            break;
        default:
            ALOGW("Unexpected extended opcode when figuring out SSA %#x", mir->dalvikInsn.opcode);
            break;
    }

    //Paranoid because we shouldn't exceed the space allocated
    assert (numUses <= maxUses);
    assert (numDefs <= maxDefs);

    //Do we need more space for the uses?
    if (numUses > mir->ssaRep->numUses)
    {
        mir->ssaRep->uses = static_cast<int *> (dvmCompilerNew (sizeof(*(mir->ssaRep->uses)) * numUses, false));
        mir->ssaRep->fpUse = static_cast<bool *> (dvmCompilerNew (sizeof(*(mir->ssaRep->fpUse)) * numUses, false));
        mir->ssaRep->defWhere = static_cast<MIR **> (dvmCompilerNew (sizeof(*(mir->ssaRep->defWhere)) * numUses, true));
    }

    //Do we need more space for the defs?
    if (numDefs > mir->ssaRep->numDefs)
    {
        mir->ssaRep->defs = static_cast<int *> (dvmCompilerNew (sizeof(*(mir->ssaRep->defs)) * numDefs, false));
        mir->ssaRep->fpDef = static_cast<bool *> (dvmCompilerNew (sizeof(*(mir->ssaRep->fpDef)) * numDefs, false));
        mir->ssaRep->usedNext = static_cast<SUsedChain **> (dvmCompilerNew (sizeof(*(mir->ssaRep->usedNext)) * numDefs, true));
    }

    //Initialize the number of uses and defs for the ssa
    mir->ssaRep->numUses = numUses;
    mir->ssaRep->numDefs = numDefs;

    //Now do the actual copying
    if (numUses != 0)
    {
        //Copy uses to ssa representation
        memcpy (mir->ssaRep->uses, uses, numUses * sizeof(int));
        memcpy (mir->ssaRep->fpUse, fpUse, numUses * sizeof(bool));
    }

    if (numDefs != 0)
    {
        //Copy defs to ssa representation
        memcpy (mir->ssaRep->defs, defs, numDefs * sizeof(int));
        memcpy (mir->ssaRep->fpDef, fpDef, numDefs * sizeof(bool));
    }
}

SUsedChain *dvmCompilerGetUseChainForUse (MIR *mir, int useIndex)
{
    //Get the SSA representation
    SSARepresentation *useSsaRep = mir->ssaRep;

    if (useSsaRep == 0)
    {
        assert (false);
        return 0;
    }

    //The useIndex should be within bounds
    if (useIndex >= useSsaRep->numUses || useIndex < 0)
    {
        assert (false);
        return 0;
    }

    //Get the defining MIR
    MIR *defMIR = useSsaRep->defWhere[useIndex];

    //If defMIR is null, there is no use chain
    if (defMIR == 0)
    {
        return 0;
    }

    //Get the index of the define
    int defIndex = 0;

    while (defMIR->ssaRep->defs[defIndex] != useSsaRep->uses[useIndex])
    {
        //Check the next define
        defIndex++;

        //Paranoid
        if (defIndex >= defMIR->ssaRep->numDefs)
        {
            assert (false);
            return 0;
        }
    }

    //Paranoid
    if (defMIR->ssaRep->usedNext == 0)
    {
        return 0;
    }

    SUsedChain *useChain = defMIR->ssaRep->usedNext[defIndex];

    //Now advance the use chain to the requested MIR
    while (useChain != 0 && useChain->mir != mir)
    {
        useChain = useChain->nextUse;
    }

    return useChain;
}

/* Entry function to convert a block into SSA representation */
bool dvmCompilerDoSSAConversion(CompilationUnit *cUnit, BasicBlock *bb)
{
    MIR *mir;

    if (bb->dataFlowInfo == NULL) return false;

    //Check if visited
    if (bb->visited == true)
    {
        return false;
    }
    bb->visited = true;

    const unsigned int numDalvikRegisters = cUnit->numDalvikRegisters;

    //We want to remember state at entrance into BB but we need space to store it.
    //In case the BB's dalvikToSSAMap isn't allocated yet or we need larger size, we allocate it now.
    if (bb->dataFlowInfo->dalvikToSSAMapEntrance == 0 || numDalvikRegisters != bb->dataFlowInfo->numEntriesDalvikToSSAMap)
    {
        bb->dataFlowInfo->dalvikToSSAMapEntrance = static_cast<int *> (dvmCompilerNew (
                sizeof (* (bb->dataFlowInfo->dalvikToSSAMapEntrance)) * numDalvikRegisters, true));
    }

    //Do the same for exit as we did for entrance
    if (bb->dataFlowInfo->dalvikToSSAMapExit == 0 || numDalvikRegisters != bb->dataFlowInfo->numEntriesDalvikToSSAMap)
    {
        bb->dataFlowInfo->dalvikToSSAMapExit = static_cast<int *> (dvmCompilerNew (
                sizeof (* (bb->dataFlowInfo->dalvikToSSAMapExit)) * numDalvikRegisters, true));
    }

    //Number of entries in the dalvikToSSAMaps now matches number of registers
    bb->dataFlowInfo->numEntriesDalvikToSSAMap = numDalvikRegisters;

    //For ensuring sanity of memcpy, we check that type matches because both structures should be same size
    assert (sizeof (*(cUnit->dalvikToSSAMap)) == sizeof (*(bb->dataFlowInfo->dalvikToSSAMapEntrance)));

    //Remember the state we were at when starting the BasicBlock
    memcpy(bb->dataFlowInfo->dalvikToSSAMapEntrance, cUnit->dalvikToSSAMap,
           sizeof (* (bb->dataFlowInfo->dalvikToSSAMapEntrance)) * numDalvikRegisters);

    for (mir = bb->firstMIRInsn; mir; mir = mir->next) {
        //If not yet generated
        if (mir->ssaRep == NULL)
        {
            mir->ssaRep = static_cast<struct SSARepresentation *> (dvmCompilerNew (sizeof (* (mir->ssaRep)), true));
        }

        long long dfAttributes =
            dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if (dfAttributes & DF_FORMAT_35C) {
            dataFlowSSAFormat35C(cUnit, mir);
            continue;
        }

        if (dfAttributes & DF_FORMAT_3RC) {
            dataFlowSSAFormat3RC(cUnit, mir);
            continue;
        }

        if (dfAttributes & DF_FORMAT_EXT_OP) {
            dataFlowSSAFormatExtendedOp(cUnit, mir);
            continue;
        }

        int numUses = 0;

        if (dfAttributes & DF_HAS_USES) {
            if (dfAttributes & DF_UA) {
                numUses++;
            } else if (dfAttributes & DF_UA_WIDE) {
                numUses += 2;
            }
            if (dfAttributes & DF_UB) {
                numUses++;
            } else if (dfAttributes & DF_UB_WIDE) {
                numUses += 2;
            }
            if (dfAttributes & DF_UC) {
                numUses++;
            } else if (dfAttributes & DF_UC_WIDE) {
                numUses += 2;
            }
        }

        if (numUses > mir->ssaRep->numUses) {
            mir->ssaRep->uses = static_cast<int *> (dvmCompilerNew (sizeof (* (mir->ssaRep->uses)) * numUses, false));
            mir->ssaRep->fpUse = static_cast<bool *> (dvmCompilerNew (sizeof (* (mir->ssaRep->fpUse)) * numUses, false));
            mir->ssaRep->defWhere = static_cast<MIR **> (dvmCompilerNew (sizeof (* (mir->ssaRep->defWhere)) * numUses, true));
        }

        //All structures should have enough room to hold information about uses. So update the number of uses now.
        mir->ssaRep->numUses = numUses;

        int numDefs = 0;

        if (dfAttributes & DF_HAS_DEFS) {
            numDefs++;
            if (dfAttributes & DF_DA_WIDE) {
                numDefs++;
            }
        }

        if (numDefs > mir->ssaRep->numDefs) {
            mir->ssaRep->defs = static_cast<int *> (dvmCompilerNew (sizeof (* (mir->ssaRep->defs)) * numDefs, false));
            mir->ssaRep->fpDef = static_cast<bool *> (dvmCompilerNew (sizeof (* (mir->ssaRep->fpDef)) * numDefs, false));
            mir->ssaRep->usedNext = static_cast<SUsedChain **> (dvmCompilerNew (sizeof (* (mir->ssaRep->usedNext)) * numDefs, true));
        }

        //All structures should have enough room to hold information about defs. So update the number of defs now.
        mir->ssaRep->numDefs = numDefs;

        DecodedInstruction *dInsn = &mir->dalvikInsn;

        if (dfAttributes & DF_HAS_USES) {
            numUses = 0;
            if (dfAttributes & DF_UA) {
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_A;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vA, numUses++);
            } else if (dfAttributes & DF_UA_WIDE) {
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_A;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vA, numUses++);
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_A;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vA + 1, numUses++);
            }
            if (dfAttributes & DF_UB) {
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_B;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vB, numUses++);
            } else if (dfAttributes & DF_UB_WIDE) {
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_B;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vB, numUses++);
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_B;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vB + 1, numUses++);
            }
            if (dfAttributes & DF_UC) {
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_C;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vC, numUses++);
            } else if (dfAttributes & DF_UC_WIDE) {
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_C;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vC, numUses++);
                mir->ssaRep->fpUse[numUses] = dfAttributes & DF_FP_C;
                handleSSAUse(cUnit, mir->ssaRep->uses, dInsn->vC + 1, numUses++);
            }
        }
        if (dfAttributes & DF_HAS_DEFS) {
            mir->ssaRep->fpDef[0] = dfAttributes & DF_FP_A;
            handleSSADef(cUnit, mir->ssaRep->defs, dInsn->vA, 0);
            if (dfAttributes & DF_DA_WIDE) {
                mir->ssaRep->fpDef[1] = dfAttributes & DF_FP_A;
                handleSSADef(cUnit, mir->ssaRep->defs, dInsn->vA + 1, 1);
            }
        }
    }

    //Create iterator for visiting children
    ChildBlockIterator childIter (bb);

    //Now iterate through the children to do the SSA conversion
    for (BasicBlock **childPtr = childIter.getNextChildPtr (); childPtr != 0; childPtr = childIter.getNextChildPtr ())
    {
        BasicBlock *child = *childPtr;

        assert (child != 0);

        dvmCompilerDoSSAConversion (cUnit, child);
    }

    //If we have a successor list, process that
    if (bb->successorBlockList.blockListType != kNotUsed)
    {
        GrowableListIterator iterator;
        dvmGrowableListIteratorInit(&bb->successorBlockList.blocks,
                                    &iterator);

        for (SuccessorBlockInfo *successorBlockInfo = (SuccessorBlockInfo *) (dvmGrowableListIteratorNext(&iterator));
            successorBlockInfo != NULL;
            successorBlockInfo = (SuccessorBlockInfo *) (dvmGrowableListIteratorNext(&iterator)))
        {
            BasicBlock *succBB = successorBlockInfo->block;
            if (succBB != 0)
            {
                dvmCompilerDoSSAConversion (cUnit, succBB);
            }
        }
    }

    //For ensuring sanity of memcpy, we check that type matches because both structures should be same size
    assert (sizeof (*(cUnit->dalvikToSSAMap)) == sizeof (*(bb->dataFlowInfo->dalvikToSSAMapExit)));

    //Copy the state also to exit, this is used by any PHI operand calculation
    memcpy(bb->dataFlowInfo->dalvikToSSAMapExit, cUnit->dalvikToSSAMap,
           sizeof (* (bb->dataFlowInfo->dalvikToSSAMapExit)) * numDalvikRegisters);

    //Copy the entrance back to cUnit, this is used to know the SSA registers associated to VRs at the entrance of a BB
    memcpy (cUnit->dalvikToSSAMap, bb->dataFlowInfo->dalvikToSSAMapEntrance,
           sizeof (* (bb->dataFlowInfo->dalvikToSSAMapEntrance)) * numDalvikRegisters);

    return true;
}

/* Setup a constant value for opcodes thare have the DF_SETS_CONST attribute */
static void setConstant(CompilationUnit *cUnit, int ssaReg, int value)
{
    dvmSetBit(cUnit->isConstantV, ssaReg);
    (*cUnit->constantValues)[ssaReg] = value;
}

bool dvmCompilerDoConstantPropagation(CompilationUnit *cUnit, BasicBlock *bb)
{
    MIR *mir;
    BitVector *isConstantV = cUnit->isConstantV;

    for (mir = bb->firstMIRInsn; mir; mir = mir->next) {
        long long dfAttributes =
            dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if (!(dfAttributes & DF_HAS_DEFS)) continue;

        /* Handle instructions that set up constants directly */
        if (dfAttributes & DF_SETS_CONST) {
            int lowConst = 0, highConst = 0;
            bool isWide = false;

            bool setsConst = dexGetConstant (mir->dalvikInsn, lowConst,
                    highConst, isWide);

            //Since we always know we have a constant set expression, this
            //should always be true.
            if (setsConst == true)
            {
                setConstant (cUnit, mir->ssaRep->defs[0], lowConst);

                if (isWide == true)
                {
                    setConstant (cUnit, mir->ssaRep->defs[1], highConst);
                }
            }

        /* Handle instructions that set constant by moving it from another register */
        } else if (dfAttributes & DF_IS_MOVE) {
            int i;

            for (i = 0; i < mir->ssaRep->numUses; i++) {
                if (!dvmIsBitSet(isConstantV, mir->ssaRep->uses[i])) break;
            }
            /* Move a register holding a constant to another register */
            if (i == mir->ssaRep->numUses) {
                setConstant(cUnit, mir->ssaRep->defs[0],
                            (*cUnit->constantValues)[mir->ssaRep->uses[0]]);
                if (dfAttributes & DF_DA_WIDE) {
                    setConstant(cUnit, mir->ssaRep->defs[1],
                                (*cUnit->constantValues)[mir->ssaRep->uses[1]]);
                }
            }
        }
    }
    /* TODO: implement code to handle arithmetic operations */
    return true;
}

#ifndef ARCH_IA32
bool dvmCompilerFindInductionVariables(struct CompilationUnit *cUnit,
                                       struct BasicBlock *bb)
{
    BitVector *isIndVarV = cUnit->loopAnalysis->isIndVarV;
    BitVector *isConstantV = cUnit->isConstantV;
    GrowableList *ivList = cUnit->loopAnalysis->ivList;
    MIR *mir;

    if (bb->blockType != kDalvikByteCode && bb->blockType != kEntryBlock) {
        return false;
    }

    /* If the bb doesn't have a phi it cannot contain an induction variable */
    if (bb->firstMIRInsn == NULL ||
        (int)bb->firstMIRInsn->dalvikInsn.opcode != (int)kMirOpPhi) {
        return false;
    }

    /* Find basic induction variable first */
    for (mir = bb->firstMIRInsn; mir; mir = mir->next) {
        int dfAttributes =
            dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if (!(dfAttributes & DF_IS_LINEAR)) continue;

        /*
         * For a basic induction variable:
         *   1) use[0] should belong to the output of a phi node
         *   2) def[0] should belong to the input of the same phi node
         *   3) the value added/subtracted is a constant
         */
        MIR *phi;
        for (phi = bb->firstMIRInsn; phi; phi = phi->next) {
            if ((int)phi->dalvikInsn.opcode != (int)kMirOpPhi) break;

            if (phi->ssaRep->defs[0] == mir->ssaRep->uses[0] &&
                phi->ssaRep->uses[1] == mir->ssaRep->defs[0]) {
                bool deltaIsConstant = false;
                int deltaValue;

                switch (mir->dalvikInsn.opcode) {
                    case OP_ADD_INT:
                        if (dvmIsBitSet(isConstantV,
                                        mir->ssaRep->uses[1])) {
                            deltaValue =
                                (*cUnit->constantValues)[mir->ssaRep->uses[1]];
                            deltaIsConstant = true;
                        }
                        break;
                    case OP_SUB_INT:
                        if (dvmIsBitSet(isConstantV,
                                        mir->ssaRep->uses[1])) {
                            deltaValue =
                                -(*cUnit->constantValues)[mir->ssaRep->uses[1]];
                            deltaIsConstant = true;
                        }
                        break;
                    case OP_ADD_INT_LIT8:
                        deltaValue = mir->dalvikInsn.vC;
                        deltaIsConstant = true;
                        break;
                    default:
                        break;
                }
                if (deltaIsConstant) {
                    dvmSetBit(isIndVarV, mir->ssaRep->uses[0]);
                    InductionVariableInfo *ivInfo = (InductionVariableInfo *)
                        dvmCompilerNew(sizeof(InductionVariableInfo),
                                       false);

                    ivInfo->ssaReg = mir->ssaRep->uses[0];
                    ivInfo->basicSSAReg = mir->ssaRep->uses[0];
                    //A basic IV has form i = 1*i + d
                    ivInfo->multiplier = 1;
                    ivInfo->constant = deltaValue;
                    ivInfo->loopIncrement = deltaValue;
                    dvmInsertGrowableList(ivList, (intptr_t) ivInfo);
                    cUnit->loopAnalysis->numBasicIV++;
                    break;
                }
            }
        }
    }

    /* Find dependent induction variable now */
    for (mir = bb->firstMIRInsn; mir; mir = mir->next) {
        int dfAttributes =
            dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if (!(dfAttributes & DF_IS_LINEAR)) continue;

        /* Skip already identified induction variables */
        if (dvmIsBitSet(isIndVarV, mir->ssaRep->defs[0])) continue;

        /*
         * For a dependent induction variable:
         *  1) use[0] should be an induction variable (basic/dependent)
         *  2) operand2 should be a constant
         */
        if (dvmIsBitSet(isIndVarV, mir->ssaRep->uses[0])) {
            int srcDalvikReg = dvmConvertSSARegToDalvik(cUnit,
                                                        mir->ssaRep->uses[0]);
            int dstDalvikReg = dvmConvertSSARegToDalvik(cUnit,
                                                        mir->ssaRep->defs[0]);

            bool cIsConstant = false;
            int constant = 0;

            switch (mir->dalvikInsn.opcode) {
                case OP_ADD_INT:
                    if (dvmIsBitSet(isConstantV,
                                    mir->ssaRep->uses[1])) {
                        constant = (*cUnit->constantValues)[mir->ssaRep->uses[1]];
                        cIsConstant = true;
                    }
                    break;
                case OP_SUB_INT:
                    if (dvmIsBitSet(isConstantV,
                                    mir->ssaRep->uses[1])) {
                        constant = -(*cUnit->constantValues)[mir->ssaRep->uses[1]];
                        cIsConstant = true;
                    }
                    break;
                case OP_ADD_INT_LIT8:
                    constant = mir->dalvikInsn.vC;
                    cIsConstant = true;
                    break;
                default:
                    break;
            }

            /* Ignore the update to the basic induction variable itself */
            if (DECODE_REG(srcDalvikReg) == DECODE_REG(dstDalvikReg))  {
                cUnit->loopAnalysis->ssaBIV = mir->ssaRep->defs[0];
                cIsConstant = false;
            }

            if (cIsConstant) {
                unsigned int i;
                dvmSetBit(isIndVarV, mir->ssaRep->defs[0]);
                InductionVariableInfo *ivInfo = (InductionVariableInfo *)
                    dvmCompilerNew(sizeof(InductionVariableInfo),
                                   false);
                InductionVariableInfo *ivInfoOld = NULL ;

                for (i = 0; i < ivList->numUsed; i++) {
                    ivInfoOld = (InductionVariableInfo *) ivList->elemList[i];
                    if (ivInfoOld->ssaReg == mir->ssaRep->uses[0]) break;
                }

                /* Guaranteed to find an element */
                assert(i < ivList->numUsed);

                ivInfo->ssaReg = mir->ssaRep->defs[0];
                ivInfo->basicSSAReg = ivInfoOld->basicSSAReg;
                ivInfo->multiplier = ivInfoOld->multiplier;
                ivInfo->constant = constant + ivInfoOld->constant;
                ivInfo->loopIncrement = ivInfoOld->loopIncrement;
                dvmInsertGrowableList(ivList, (intptr_t) ivInfo);
            }
        }
    }
    return true;
}
#else

/**
 * @brief check if mir is a supported type conversion bytecode during IV detection
 * @param mir mir instruction need to be checked
 * @return whether bytecode type of mir is support for IV detection
 */
static bool isSupportedCastBytecodeForIV(MIR* mir)
{
    switch (mir->dalvikInsn.opcode)
    {
        case OP_INT_TO_BYTE:
        case OP_INT_TO_SHORT:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Find the upper bound
 * @details Find the upper bound of a count up loop by looking
 * at the CONSTs defining the if-VR
 * Currently works only with a single BasicBlock loop
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param[out] upperBound reference to where to store the upperBound
 * @return whether or not the function found the upper bound
 */
static bool findLoopUpperBound (CompilationUnit *cUnit, LoopInformation *const info, int &upperBound)
{
    //Loop should exist and should be a count up loop
    if (info == 0)
    {
        return false;
    }

    //Get the bitvector of BWCC
    BitVector *bv = info->getBackwardBranches ();

    if (bv == 0)
    {
        return false;
    }

    //Check if we have only one BWCC
    unsigned int count = dvmCountSetBits (bv);

    if (count != 1)
    {
        return false;
    }

    //Get BWCC
    unsigned int idx = dvmHighestBitSet (bv);
    BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, idx);

    //Paranoid
    if (bb == 0)
    {
        return false;
    }

    //Now get its predecessor
    BitVector *predecessors = bb->predecessors;
    count = dvmCountSetBits (predecessors);

    //If we have more one predecessors, bail
    if (count != 1)
    {
        return false;
    }

    //Get index
    idx = dvmHighestBitSet (predecessors);

    //Get the bb
    bb = (BasicBlock *) dvmGrowableListGetElement(&cUnit->blockList, idx);

    //Make sure we have an "if" at the end
    const MIR *const lastMIR = bb->lastMIRInsn;
    if (lastMIR == 0)
    {
        return false;
    }

    const Opcode opcode = lastMIR->dalvikInsn.opcode;
    if (opcode < OP_IF_EQ || opcode > OP_IF_LEZ)
    {
        return false;
    }

    //Get the SSA etc
    SSARepresentation *ssaRep = lastMIR->ssaRep;

    //Make sure we have only two uses
    if (ssaRep == 0 || ssaRep->numUses != 2)
    {
        return false;
    }

    //Check the MIR defining the first use
    MIR *mirUseFirst = ssaRep->defWhere[0];
    MIR *mirUseSecond = ssaRep->defWhere[1];


    //Paranoid
    if (mirUseFirst == 0 || mirUseSecond == 0)
    {
        return false;
    }

    //This will hold the const value if found
    int constValue = 0;

    //We don't care about wide constants
    int constValueIgnored = 0;

    bool isWideConst = false;

    bool setsConst = dexGetConstant (mirUseFirst->dalvikInsn, constValue,
            constValueIgnored, isWideConst);

    if (setsConst == true && isWideConst == false)
    {
        upperBound = constValue;
        return true;
    }

    //Otherwise check the MIR defining the second use
    setsConst = dexGetConstant (mirUseSecond->dalvikInsn, constValue,
            constValueIgnored, isWideConst);

    if (setsConst == true && isWideConst == false)
    {
        upperBound = constValue;
        return true;
    }

    return false;
}


/**
 * @brief Check if whether this is a valid cast operation on an IV
 * @details Assuming the MIR defines an IV, is the operation a cast,
 * and if so, is it a valid cast? A valid cast on an IV will keep the
 * type of IV compatible with the loop bound.
 * @param cUnit the CompilationUnit
 * @param info loop information
 * @param defMir mir that is a bytecode need to be checked
 * @return whether we need and can go see the next def for IV detection
 */
static bool isAValidCastForIV (CompilationUnit *cUnit, LoopInformation *info, MIR* defMir)
{
    // check if the defMIR is the type conversion bytecode we supported for IV detection
    if (isSupportedCastBytecodeForIV(defMir) == true)
    {
        int upperBound = 0;

        //Find the upper bound and put it in upperBound
        if (findLoopUpperBound(cUnit, info, upperBound) == false)
        {
            return false;
        }

        // Check the constant to see if it's within the range of the cast type
        switch (defMir->dalvikInsn.opcode)
        {
            case OP_INT_TO_BYTE:
                if (upperBound > 127 || upperBound < -128)
                {
                    return false;
                }
                break;
            case OP_INT_TO_SHORT:
                if (upperBound > 32767 || upperBound < -32768)
                {
                    return false;
                }
                break;
            default:
                return false;
        }
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Can the parser go through the instruction when looking for a definition of an induction variable?
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param mir the MIR instruction
 * @param wentThrough did the function go across an instruction?
 * @return the MIR to consider
 */
static MIR *tryToGoThroughMIR (CompilationUnit *cUnit, LoopInformation *info, MIR *mir, bool &wentThrough)
{
    //Reset went through
    wentThrough = false;

    while (true)
    {
        // null check
        if (mir == 0)
        {
            break;
        }

        /* If this is a cast operation, and is valid even if the
         * candidate is an IV, ignore it
         */
        if (isAValidCastForIV (cUnit, info, mir) == true)
        {
            //Go to the previous define
            mir = mir->ssaRep->defWhere[0];

            //Update wentThrough
            wentThrough = true;
            continue;
        }

        //We are done then
        break;
    }

    return mir;
}

/**
 * @brief Detects whether the phi node is a basic IV. If it is, then it
 * augments the loop with that information.
 * @param cUnit The compilation unit
 * @param info The loop information for current loop.
 * @param phi MIR representing the phi node
 * @return Returns whether basic IV was found.
 */
bool detectAndInitializeBasicIV(CompilationUnit *cUnit,
                                LoopInformation *info,
                                MIR *phi)
{
    //Get the SSA representation
    SSARepresentation *ssaRep = phi->ssaRep;

    //Paranoid
    if (ssaRep == 0 || ssaRep->uses == 0 || ssaRep->defs == 0)
    {
        return false;
    }

    //For now accept only PHI nodes that have two uses and one define
    if (ssaRep->numDefs != 1 || ssaRep->numUses != 2)
    {
        return false;
    }

    //Get local copy of where the uses are defined
    MIR **defWhere = ssaRep->defWhere;

    //If we have more than one use, we should have links to definition
    if (defWhere == 0)
    {
        return false;
    }

    //Keep track of MIR that is candidate for identifying simple IV
    MIR *candidate = 0;
    bool wentThrough = false;

    //Go through each of the PHI's uses
    for (int use = 0; use < ssaRep->numUses; use++)
    {
        //Get MIR that defines the use
        MIR *defMIR = defWhere[use];

        //A cast operation doesn't automatically disqualify a PHI node
        //as an IV. If it acceptable, move to the next define
        defMIR = tryToGoThroughMIR (cUnit, info, defMIR, wentThrough);

        //If we cannot find the definition nor any uses, then just continue
        if (defMIR == 0 || defMIR->ssaRep == 0 || defMIR->ssaRep->uses == 0)
        {
            continue;
        }

        //A phi node having a single define was part of criteria of getting
        //here. But just in case that changes, assert it now.
        assert(ssaRep->numDefs == 1);

        //Go through each use of the MIR to compare with define of phi node
        for (int mirUse = 0; mirUse < defMIR->ssaRep->numUses; mirUse++)
        {
            //If the use matches the definition from phi node, we found
            // a candidate
            if (ssaRep->defs[0] == defMIR->ssaRep->uses[mirUse])
            {
                candidate = defMIR;
                break;
            }
        }

        //If we have a potential candidate, we need to make sure that this
        //dominates every backward chaining cell.
        if (candidate != 0)
        {
            //If candidate is executed per iteration of current loop, then we
            //can keep it and run it through the IV detection criteria
            if (info->executedPerIteration (cUnit, candidate) == true)
            {
                break;
            }

            //If we get here it means that the candidate doesn't have to be
            //executed per iteration and thus it cannot be an IV. Therefore,
            //we reset the candidate now.
            candidate = 0;
        }
    }

    //Assume that we will not find a basic IV
    bool foundBasicIV = false;

    //If we found a candidate, check that it matches criteria for basic IV
    if (candidate != 0)
    {
        bool deltaIsConstant = false;
        int deltaValue;

        //TODO This code should be using expression tree instead of going
        //through each bytecode like this
        switch (candidate->dalvikInsn.opcode)
        {
            case OP_ADD_INT:
            case OP_ADD_INT_2ADDR:
                if (dvmCompilerIsRegConstant (cUnit, candidate->ssaRep->uses[1]))
                {
                    deltaValue = (*cUnit->constantValues)[candidate->ssaRep->uses[1]];
                    deltaIsConstant = true;
                }
                break;
            case OP_SUB_INT:
            case OP_SUB_INT_2ADDR:
                if (dvmCompilerIsRegConstant (cUnit, candidate->ssaRep->uses[1]))
                {
                    deltaValue = - (*cUnit->constantValues)[candidate->ssaRep->uses[1]];
                    deltaIsConstant = true;
                }
                break;
            case OP_ADD_INT_LIT8:
            case OP_ADD_INT_LIT16:
                deltaValue = candidate->dalvikInsn.vC;
                deltaIsConstant = true;
                break;
            default:
                break;
        }

        if (deltaIsConstant == true)
        {
            //Only accept this IV if the deltaValue is positive OR it's negative and we did not go through a cast
            if (deltaValue >= 0 || wentThrough == false)
            {
                GrowableList & ivList = info->getInductionVariableList();

                void *space = dvmCompilerNew (sizeof (InductionVariableInfo), true);
                InductionVariableInfo *ivInfo = new (space) InductionVariableInfo;

                ivInfo->ssaReg = candidate->ssaRep->defs[0];
                ivInfo->basicSSAReg = candidate->ssaRep->uses[0];
                ivInfo->multiplier = 1;         // always 1 to basic iv
                ivInfo->constant = 0;         // N/A to basic iv
                ivInfo->loopIncrement = deltaValue;
                ivInfo->isBasic = true;
                ivInfo->linearMir = candidate;
                ivInfo->phiMir = phi;
                ivInfo->multiplierMir = 0; //always null for basic iv
                dvmInsertGrowableList (&ivList, (intptr_t) ivInfo);

                //FIXME The loop system relies on the IV detection to set the loop's BIV.
                //And there are users of this variable whenever a loop is found with a single BIV.
                //But this is not the correct way to detect this scenario.
                info->setSSABIV(candidate->ssaRep->defs[0]);
                foundBasicIV = true;
            }
        }
    }

    return foundBasicIV;
}

/**
 * @brief Used to detect and initialize dependent IVs
 * @param cUnit The compilation unit
 * @param loopInfo The loop information for which we want to find dependent IVs
 */
static void detectAndInitializeDependentIVs (CompilationUnit *cUnit, LoopInformation *loopInfo)
{
    //Used to keep the dependent IVs we need to insert
    std::set<InductionVariableInfo *> toInsert;

    //Get the induction variable list and create an iterator for it
    GrowableList &ivList = loopInfo->getInductionVariableList ();
    GrowableListIterator iter;
    dvmGrowableListIteratorInit (&ivList, &iter);

    while (true)
    {
        //Get the IV information
        InductionVariableInfo *ivInfo = reinterpret_cast<InductionVariableInfo *> (dvmGrowableListIteratorNext (&iter));

        //Break out if there is no more information
        if (ivInfo == 0)
        {
            break;
        }

        //Keep looking if this one is not a basic IV
        if (ivInfo->isBasicIV () == false)
        {
            continue;
        }

        //Keep track of candidates for dependent IV
        std::set<MIR *> candidates;

        //Create set of mirs that are associate with this IV
        //TODO Might make sense to also include any casting MIRs
        std::set<MIR *> mirsForIV;
        mirsForIV.insert (ivInfo->linearMir);
        mirsForIV.insert (ivInfo->phiMir);

        //Walk through all of the MIRs associated with this BIV
        for (std::set<MIR *>::const_iterator mirIter = mirsForIV.begin (); mirIter != mirsForIV.end (); mirIter++)
        {
            const MIR *mir = *mirIter;

            //Paranoid
            if (mir == 0 || mir->ssaRep == 0)
            {
                continue;
            }

            //Walk through the defines of this MIR to find users
            for (int def = 0; def < mir->ssaRep->numDefs; def++)
            {
                //Walk through the uses and save them as candidates
                for (SUsedChain *userChain = mir->ssaRep->usedNext[def]; userChain != 0; userChain = userChain->nextUse)
                {
                    assert (userChain->mir != 0);
                    candidates.insert (userChain->mir);
                }
            }
        }

        //TODO This logic does not detect cases of j = m * i + c. It detects solely: j = m * i and j = i + c
        for (std::set<MIR *>::iterator candIter = candidates.begin (); candIter != candidates.end (); candIter++)
        {
            MIR *candidate = *candIter;

            //If this user is not inside the loop, then do not consider it as a dependent IV for the loop
            if (loopInfo->contains (candidate->bb) == false)
            {
                continue;
            }

            bool constantIsMultiplier = false;
            bool constantSignMustFlip = false;
            bool noMatch = false;

            switch (candidate->dalvikInsn.opcode)
            {
                case OP_ADD_INT:
                case OP_ADD_INT_LIT8:
                case OP_ADD_INT_LIT16:
                    break;
                case OP_SUB_INT:
                    constantSignMustFlip = true;
                    break;
                case OP_MUL_INT:
                case OP_MUL_INT_LIT8:
                case OP_MUL_INT_LIT16:
                    constantIsMultiplier = true;
                    break;
                default:
                    noMatch = true;
                    break;
            }

            if (noMatch == true)
            {
                continue;
            }

            int constant = 0;
            bool foundConst = dvmCompilerGetFirstConstantUsed (cUnit, candidate, constant);

            if (foundConst == false)
            {
                //We expect to find a constant if this is a dependent IV
                continue;
            }

            if (constantSignMustFlip == true)
            {
                constant = -constant;
            }

            void *space = dvmCompilerNew (sizeof(InductionVariableInfo), true);
            InductionVariableInfo *depIVInfo = new (space) InductionVariableInfo;

            depIVInfo->ssaReg = candidate->ssaRep->defs[0];
            depIVInfo->basicSSAReg = ivInfo->basicSSAReg;
            depIVInfo->loopIncrement = ivInfo->loopIncrement;
            depIVInfo->isBasic = false;

            //There cannot be a phi node because the DevIV is not used in its own calculation
            depIVInfo->phiMir = 0;

            if (constantIsMultiplier == true)
            {
                depIVInfo->multiplier = constant;
                depIVInfo->constant = 0;
                depIVInfo->linearMir = 0;
                depIVInfo->multiplierMir = candidate;
            }
            else
            {
                depIVInfo->multiplier = 1;
                depIVInfo->constant = constant;
                depIVInfo->linearMir = candidate;
                depIVInfo->multiplierMir = 0;
            }

            toInsert.insert (depIVInfo);
        }
    }

    //Iterate through all of the dependent IVs that we need to insert
    for (std::set<InductionVariableInfo *>::iterator iter = toInsert.begin (); iter != toInsert.end (); iter++)
    {
        InductionVariableInfo *depIVInfo = *iter;

        //Insert into the official IV list
        dvmInsertGrowableList (&ivList, (intptr_t) depIVInfo);
    }
}

/**
 * @brief Looks through loop structure to find induction variables.
 * @param cUnit The compilation unit
 * @param info The loop information for current loop.
 * @param data required by interface (not used)
 * @return true to continue iteration over loops
 */
bool dvmCompilerFindInductionVariablesHelper(CompilationUnit *cUnit,
                                             LoopInformation *info, void *data = 0)
{
    //Get the loop entry BB
    BasicBlock *entry = info->getEntryBlock();

    //Paranoid
    if (entry == 0)
    {
        return true;
    }

    //Clear the induction variable information and inter-iteration variables
    GrowableList & ivList = info->getInductionVariableList();
    dvmClearGrowableList (&ivList);

    //Clear inter iteration variables
    info->clearInterIterationVariables ();

    //Go through the PHI nodes to find the simple IVs
    for (MIR *phi = entry->firstMIRInsn; phi != 0; phi = phi->next)
    {
        //If we run into a non-PHI node, we are done
        if (static_cast<int>(phi->dalvikInsn.opcode) != static_cast<int>(kMirOpPhi))
        {
            break;
        }

        detectAndInitializeBasicIV(cUnit, info, phi);

        //Also add it as a inter-iteration variable
        SSARepresentation *ssa = phi->ssaRep;

        if (ssa != 0)
        {
            //Add each definition
            int max = ssa->numDefs;
            for (int i = 0; i < max; i++)
            {
                int value = ssa->defs[i];

                //Get dalvik register
                value = dvmExtractSSARegister (cUnit, value);

                //Add it to the loop information
                info->addInterIterationVariable (value);
            }
        }
    }

    //Now look for the dependent IVs
    detectAndInitializeDependentIVs (cUnit, info);

    return true;
}

/**
 * @brief Looks through all of the loops to find the induction variables.
 * @param cUnit The compilation unit
 * @param info The loop information for outer most loop.
 */
void dvmCompilerFindInductionVariables(CompilationUnit *cUnit,
                                       LoopInformation *info)
{
    //If the loop information has not been set up we have no IVs to detect
    if (info == 0)
    {
        return;
    }

    info->iterate (cUnit, dvmCompilerFindInductionVariablesHelper);
}
#endif

/* Setup the basic data structures for SSA conversion */
void dvmInitializeSSAConversion(CompilationUnit *cUnit)
{
    int numDalvikReg = cUnit->numDalvikRegisters;

    if (cUnit->ssaToDalvikMap == 0)
    {
        cUnit->ssaToDalvikMap = static_cast<GrowableList *> (dvmCompilerNew (sizeof (* (cUnit->ssaToDalvikMap)), false));
        dvmInitGrowableList (cUnit->ssaToDalvikMap, numDalvikReg);
    }
    else
    {
        //Otherwise it's big enough to hold a bit, just reset its usage
        dvmClearGrowableList (cUnit->ssaToDalvikMap);
    }

    /*
     * Initialize the SSA2Dalvik map list. For the first numDalvikReg elements,
     * the subscript is 0 so we use the ENCODE_REG_SUB macro to encode the value
     * into "(0 << 16) | i"
     */
    for (int i = 0; i < numDalvikReg; i++) {
        dvmInsertGrowableList(cUnit->ssaToDalvikMap, ENCODE_REG_SUB(i, 0));
    }

    /*
     * Initialize the DalvikToSSAMap map. The low 16 bit is the SSA register id,
     * while the high 16 bit is the current subscript. The original Dalvik
     * register N is mapped to SSA register N with subscript 0.
     */
    if (cUnit->dalvikToSSAMap == 0)
    {
        cUnit->dalvikToSSAMap = static_cast<int *> (dvmCompilerNew (sizeof (* (cUnit->dalvikToSSAMap)) * numDalvikReg, false));
    }

    for (int i = 0; i < numDalvikReg; i++) {
        cUnit->dalvikToSSAMap[i] = i;
    }

    /**
     * Initialize the SSA subscript array. This provides a means to get a unique subscript
     * for each register and start them all at 0. A unique counter is also possible but it
     * makes debugging difficult to read on large traces
     */
    if (cUnit->ssaSubScripts == 0)
    {
        cUnit->ssaSubScripts = static_cast<int *> (dvmCompilerNew (sizeof (* (cUnit->ssaSubScripts)) * numDalvikReg, true));
    }
    else
    {
        //Otherwise set it back to 0
        for (int i = 0; i < numDalvikReg; i++) {
            cUnit->ssaSubScripts[i] = 0;
        }
    }

    // Constant propagation: allocate the vector if required
    if (cUnit->isConstantV == 0)
    {
        cUnit->isConstantV = dvmCompilerAllocBitVector (numDalvikReg, true);
    }
    else
    {
        dvmClearAllBits (cUnit->isConstantV);
    }

    /*
     * Initial number of SSA registers is equal to the number of Dalvik
     * registers.
     */
    cUnit->numSSARegs = numDalvikReg;

    /*
     * Allocate the BasicBlockDataFlow structure for the entry and code blocks
     */
    GrowableListIterator iterator;

    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);

    while (true) {
        BasicBlock *bb = (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
        if (bb == NULL) break;
        if (bb->hidden == true) continue;

        //If already allocated, skip it
        if (bb->dataFlowInfo != 0)
        {
            continue;
        }

        bb->dataFlowInfo = (BasicBlockDataFlow *) dvmCompilerNew(sizeof(BasicBlockDataFlow), true);
    }
}

/* Clear the visited flag for each BB */
bool dvmCompilerClearVisitedFlag(struct CompilationUnit *cUnit,
                                 struct BasicBlock *bb)
{
    bb->visited = false;
    return true;
}

 /**
  * @brief Performs the Predecessors-First Traversal of CFG
  * @param cUnit the CompilationUnit
  * @param func the BasicBlock's visitor function
  * @return whether the traversal change CFG or not
  */
static bool predecessorsFirstTraversal(CompilationUnit *cUnit,  bool (*func)(CompilationUnit *, BasicBlock *))
{
    bool change = false;
    std::queue<BasicBlock *> q;
    std::map<int, int> visitedCntValues;

    //Clear the nodes
    dvmCompilerDataFlowAnalysisDispatcher (cUnit,
                     dvmCompilerClearVisitedFlag, kAllNodes, false);

    //set up exit status
    cUnit->predecessorFirstTraversalOK = true;

    // set up visitedCntValues map for all BB. The default value for this counters in the map is zero.
    // also fill initial queue
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    for (BasicBlock *bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator)); bb != 0;
        bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator)))
    {
        if (bb->hidden == true)
        {
            continue;
        }
        BitVector *predecessors = bb->predecessors;
        if (predecessors != 0)
        {
            visitedCntValues[bb->id] = dvmCountSetBits(bb->predecessors);
            // to process loops we should not wait dominators
            BitVectorIterator predIter;
            dvmBitVectorIteratorInit (predecessors, &predIter);
            for (BasicBlock *predecessor = dvmCompilerGetNextBasicBlockViaBitVector (predIter, cUnit->blockList);
                predecessor != 0;
                predecessor = dvmCompilerGetNextBasicBlockViaBitVector (predIter, cUnit->blockList))
            {
                if (predecessor->dominators == 0 || predecessor->hidden == true)
                {
                    continue;
                }
                //Skip the backward branch
                if (dvmIsBitSet (predecessor->dominators, bb->id) != 0)
                {
                    visitedCntValues[bb->id]--;
                }
            }
        }
        if (visitedCntValues[bb->id] == 0) // add entry block to queue
        {
            q.push (bb);
        }
    }

    while (q.size () > 0)
    {
        //Get top
        BasicBlock *bb = q.front ();
        q.pop ();

        assert (bb != 0);
        assert (bb->visited == false);
        assert (bb->hidden == false);

        if (bb->visited == false) // We've visited all the predecessors. So, we can visit bb
        {
            change |= (*func)(cUnit, bb);
            bb->visited = true;

            // reduce visitedCnt for all the successors and add into the queue ones with visitedCnt equals to zero
            ChildBlockIterator succIter (bb);
            BasicBlock **succPtr = succIter.getNextChildPtr ();
            while (succPtr != 0)
            {
                BasicBlock *successor = *succPtr;

                //Paranoid
                assert (successor != 0);

                // one more predecessor was visited
                visitedCntValues[successor->id]--;

                if (visitedCntValues[successor->id] <= 0 && successor->visited == false && successor->hidden == false)
                {
                    q.push (successor);
                }

                //Take next successor
                succPtr = succIter.getNextChildPtr ();
            }
        }
    }

    // Now check whether there are some items not visited
    dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
    for (BasicBlock *bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator)); bb != 0;
        bb = (BasicBlock *) (dvmGrowableListIteratorNext(&iterator)))
    {
        if (visitedCntValues[bb->id] > 0 && bb->hidden == false) // not all bb were visited
        {
            cUnit->predecessorFirstTraversalOK = false;
            break;
        }
    }

    //Return change flag
    return change;
}

void dvmCompilerDataFlowAnalysisDispatcher(CompilationUnit *cUnit,
                bool (*func)(CompilationUnit *, BasicBlock *),
                DataFlowAnalysisMode dfaMode,
                bool isIterative, void *walkData)
{
    //Use the walkData if supplied
    if (walkData != 0)
    {
        //We should not have a walkData already
        if (cUnit->walkData != 0)
        {
            assert(false && "Overwriting walkData in dvmCompilerDataFlowAnalysis");
        }

        //Set the walk data
        cUnit->walkData = walkData;
    }

    bool change = true;

    while (change) {
        change = false;

        /* Scan all blocks and perform the operations specified in func */
        if (dfaMode == kAllNodes) {
            GrowableListIterator iterator;
            dvmGrowableListIteratorInit(&cUnit->blockList, &iterator);
            while (true) {
                BasicBlock *bb =
                    (BasicBlock *) dvmGrowableListIteratorNext(&iterator);
                if (bb == NULL) break;
                if (bb->hidden == true) continue;
                change |= (*func)(cUnit, bb);
            }
        }
        /*
         * Scan all reachable blocks and perform the operations specified in
         * func.
         */
        else if (dfaMode == kReachableNodes) {
            int numReachableBlocks = cUnit->numReachableBlocks;
            int idx;
            const GrowableList *blockList = &cUnit->blockList;

            for (idx = 0; idx < numReachableBlocks; idx++) {
                int blockIdx = cUnit->dfsOrder.elemList[idx];
                BasicBlock *bb =
                    (BasicBlock *) dvmGrowableListGetElement(blockList,
                                                             blockIdx);
                change |= (*func)(cUnit, bb);
            }
        }
        /*
         * Scan all reachable blocks by the pre-order in the depth-first-search
         * CFG and perform the operations specified in func.
         */
        else if (dfaMode == kPreOrderDFSTraversal) {
            int numReachableBlocks = cUnit->numReachableBlocks;
            int idx;
            const GrowableList *blockList = &cUnit->blockList;

            for (idx = 0; idx < numReachableBlocks; idx++) {
                int dfsIdx = cUnit->dfsOrder.elemList[idx];
                BasicBlock *bb =
                    (BasicBlock *) dvmGrowableListGetElement(blockList, dfsIdx);
                change |= (*func)(cUnit, bb);
            }
        }
        /*
         * Scan all reachable blocks by the post-order in the depth-first-search
         * CFG and perform the operations specified in func.
         */
        else if (dfaMode == kPostOrderDFSTraversal) {
            int numReachableBlocks = cUnit->numReachableBlocks;
            int idx;
            const GrowableList *blockList = &cUnit->blockList;

            for (idx = numReachableBlocks - 1; idx >= 0; idx--) {
                int dfsIdx = cUnit->dfsOrder.elemList[idx];
                BasicBlock *bb =
                    (BasicBlock *) dvmGrowableListGetElement(blockList, dfsIdx);
                change |= (*func)(cUnit, bb);
            }
        }
        /*
         * Scan all reachable blocks by the post-order in the dominator tree
         * and perform the operations specified in func.
         */
        else if (dfaMode == kPostOrderDOMTraversal) {
            int numReachableBlocks = cUnit->numReachableBlocks;
            int idx;
            const GrowableList *blockList = &cUnit->blockList;

            for (idx = 0; idx < numReachableBlocks; idx++) {
                int domIdx = cUnit->domPostOrderTraversal.elemList[idx];
                BasicBlock *bb =
                    (BasicBlock *) dvmGrowableListGetElement(blockList, domIdx);
                change |= (*func)(cUnit, bb);
            }
        }
        /*
         * Scan all blocks in breadth first manner
         */
        else if (dfaMode == kBreadthFirstTraversal) {

            std::queue<BasicBlock *> q;

            //Clear the nodes
            dvmCompilerDataFlowAnalysisDispatcher (cUnit,
                    dvmCompilerClearVisitedFlag, kAllNodes, false);

            //Push in the entry basic block
            if (cUnit->entryBlock != 0)
            {
                q.push (cUnit->entryBlock);
            }

            while (q.size () > 0)
            {
                //Get top
                BasicBlock *bb = q.front ();
                q.pop ();

                //Paranoid
                if (bb->visited == true)
                    continue;

                //Visit it
                bb->visited = true;

                //Call func function
                change |= (*func)(cUnit, bb);

                //Now handle queue: only push taken/fallThrough if not yet visited
                if (bb->taken != NULL && bb->taken->visited == false)
                {
                    q.push (bb->taken);
                }

                if (bb->fallThrough != NULL && bb->fallThrough->visited == false)
                {
                    q.push (bb->fallThrough);
                }
            }

            //Clear the nodes
            dvmCompilerDataFlowAnalysisDispatcher (cUnit,
                    dvmCompilerClearVisitedFlag, kAllNodes, false);
        }
        /*
         * Scan all blocks including new added during traversal
         */
        else if (dfaMode == kAllNodesAndNew) {
            const GrowableList *blockList = &cUnit->blockList;

            for (size_t idx = 0; idx < dvmGrowableListSize(blockList); idx++)
            {
                BasicBlock *bb = (BasicBlock *) dvmGrowableListGetElement(blockList, idx);
                change |= (*func)(cUnit, bb);
            }
        }
        /*
         *
         */
        else if (dfaMode == kPredecessorsFirstTraversal) {
            //Call the helper function
            change = predecessorsFirstTraversal (cUnit, func);
        }

        /* If isIterative is false, exit the loop after the first iteration */
        change &= isIterative;
    }

    //Reset the walk data if we started with it
    if (walkData != 0)
    {
        cUnit->walkData = 0;
    }
}

/* Main entry point to do SSA conversion for non-loop traces */
void dvmCompilerNonLoopAnalysis(CompilationUnit *cUnit)
{
    dvmCompilerDataFlowAnalysisDispatcher(cUnit, dvmCompilerDoSSAConversion,
                                          kAllNodes,
                                          false /* isIterative */);
}

int dvmCompilerGetStartUseIndex (Opcode opcode)
{
    //Default result
    int res = 0;

    //We are basically setting the iputs to their igets counterparts
    switch (opcode)
    {
        case OP_IPUT:
        case OP_IPUT_OBJECT:
        case OP_IPUT_BOOLEAN:
        case OP_IPUT_BYTE:
        case OP_IPUT_CHAR:
        case OP_IPUT_SHORT:
        case OP_IPUT_QUICK:
        case OP_IPUT_OBJECT_QUICK:
        case OP_APUT:
        case OP_APUT_OBJECT:
        case OP_APUT_BOOLEAN:
        case OP_APUT_BYTE:
        case OP_APUT_CHAR:
        case OP_APUT_SHORT:
        case OP_SPUT:
        case OP_SPUT_OBJECT:
        case OP_SPUT_BOOLEAN:
        case OP_SPUT_BYTE:
        case OP_SPUT_CHAR:
        case OP_SPUT_SHORT:
            //Skip the VR containing what to store
            res = 1;
            break;
        case OP_IPUT_WIDE:
        case OP_IPUT_WIDE_QUICK:
        case OP_APUT_WIDE:
        case OP_SPUT_WIDE:
            //Skip the two VRs containing what to store
            res = 2;
            break;
        default:
            //Do nothing in the general case
            break;
    }

    return res;
}

bool dvmCompilerIsOpcodeVolatile (Opcode opcode)
{
    switch (opcode)
    {
        case OP_IGET_VOLATILE:
        case OP_IPUT_VOLATILE:
        case OP_SGET_VOLATILE:
        case OP_SPUT_VOLATILE:
        case OP_IGET_OBJECT_VOLATILE:
        case OP_IGET_WIDE_VOLATILE:
        case OP_IPUT_WIDE_VOLATILE:
        case OP_SGET_WIDE_VOLATILE:
        case OP_SPUT_WIDE_VOLATILE:
        case OP_IPUT_OBJECT_VOLATILE:
        case OP_SGET_OBJECT_VOLATILE:
        case OP_SPUT_OBJECT_VOLATILE:
            return true;
        default:
            break;
    }
    return false;
}
