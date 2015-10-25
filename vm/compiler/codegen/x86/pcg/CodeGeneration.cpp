/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mman.h>
#include "Analysis.h"
#include "BasicBlockPCG.h"
#include "ChainingCellException.h"
#include "CodeGeneration.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "CompilerIR.h"
#include "Dataflow.h"
#include "interp/InterpDefs.h"
#include "Labels.h"
#include "libpcg.h"
#include "Lower.h"
#include "LowerALU.h"
#include "LowerArray.h"
#include "LowerCall.h"
#include "LowerExtended.h"
#include "LowerGetPut.h"
#include "LowerJump.h"
#include "LowerMemory.h"
#include "LowerOther.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "Utility.h"
#include "UtilityPCG.h"
#include <list>

/**
 * @brief Translate an MIR instruction
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 */
static bool dvmCompilerPcgTranslateInsn (CompilationUnitPCG *cUnit, MIR *mir)
{
    bool success = true;
    Opcode dalvikOpCode = mir->dalvikInsn.opcode;

    //Update rPC to contain the dalvik PC for this bytecode
    rPC = dvmCompilerGetDalvikPC (cUnit, mir);

    //Get the SSARepresentation
    SSARepresentation *ssaRep = mir->ssaRep;

    assert (ssaRep != 0);

    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        const u4 cBufLen = 2048;
        char mybuf[cBufLen];
        dvmCompilerExtendedDisassembler (cUnit, mir, &mir->dalvikInsn, mybuf, cBufLen);
        ALOGI ("LOWER %s %s\n", mybuf,
            (mir->OptimizationFlags & MIR_CALLEE) ? " (inlined)" : ""
        );
    }

    if (dalvikOpCode >= kNumPackedOpcodes)
    {
        switch ( (ExtendedMIROpcode) dalvikOpCode)
        {
            case kMirOpPhi:
            case kMirOpPunt:
            case kMirOpRegisterize:
                break;

            case kMirOpCheckInlinePrediction:
                dvmCompilerPcgTranslatePredictionInlineCheck (cUnit, mir);
                break;

            case kMirOpLowerBound:
                dvmCompilerPcgTranslateLowerBoundCheck (cUnit, mir);
                break;

            case kMirOpBoundCheck:
                dvmCompilerPcgTranslateBoundCheck (cUnit, mir);
                break;

            case kMirOpNullCheck:
                dvmCompilerPcgTranslateNullCheck (cUnit, mir);
                break;

            case kMirOpNullNRangeUpCheck:
                dvmCompilerPcgTranslateLoopChecks (cUnit, mir, true);
                break;

            case kMirOpNullNRangeDownCheck:
                dvmCompilerPcgTranslateLoopChecks (cUnit, mir, false);
                break;

            case kMirOpCheckStackOverflow:
                dvmCompilerPcgTranslateCheckStackOverflow (cUnit, mir);
                break;

            case kMirOpPackedSet:
                dvmCompilerPcgTranslatePackedSet (cUnit, mir);
                break;

            case kMirOpConst128b:
                dvmCompilerPcgTranslatePackedConst (cUnit, mir);
                break;

            case kMirOpMove128b:
                dvmCompilerPcgTranslatePackedMove (cUnit, mir);
                break;

            case kMirOpPackedAddition:
            case kMirOpPackedMultiply:
            case kMirOpPackedSubtract:
            case kMirOpPackedAnd:
            case kMirOpPackedOr:
            case kMirOpPackedXor:
                success = dvmCompilerTranslatePackedAlu (cUnit, mir);
                break;

            case kMirOpPackedAddReduce:
                dvmCompilerPcgTranslatePackedAddReduce (cUnit, mir);
                break;

            default:
                ALOGE ("Jit (PCG): unsupported extended MIR opcode");
                assert (0);
                break;
        }

        return success;
    }

    switch (dalvikOpCode)
    {
        case OP_NOP:
            break;

        case OP_MOVE:
        case OP_MOVE_OBJECT:
        case OP_MOVE_FROM16:
        case OP_MOVE_OBJECT_FROM16:
        case OP_MOVE_16:
        case OP_MOVE_OBJECT_16:
            dvmCompilerPcgTranslateMove (cUnit, mir);
            break;

        case OP_MOVE_WIDE:
        case OP_MOVE_WIDE_FROM16:
        case OP_MOVE_WIDE_16:
            // It is a bit odd that OP_MOVE_WIDE_FROM16 is implemented in
            // exactly the same way as OP_MOVE_WIDE, but this matches the
            // existing Dalvik implementation.
            // TODO - Check on this.  It might be a bug.
            dvmCompilerPcgTranslateMoveWide (cUnit, mir);
            break;

        case OP_MOVE_EXCEPTION:
            dvmCompilerPcgGenerateRaiseException (cUnit);
            break;

        case OP_THROW:
            dvmCompilerPcgGenerateRaiseException (cUnit);
            break;

        case OP_CONST:
            dvmCompilerPcgTranslateConst (cUnit, mir);
            break;

        case OP_CONST_4:
            dvmCompilerPcgTranslateConst4 (cUnit, mir);
            break;

        case OP_CONST_16:
            dvmCompilerPcgTranslateConst16 (cUnit, mir);
            break;

        case OP_CONST_HIGH16:
            dvmCompilerPcgTranslateConstHigh16 (cUnit, mir);
            break;

        case OP_CONST_CLASS:
            dvmCompilerPcgTranslateConstHelper (cUnit, mir, (u4) (cUnit->method->clazz->pDvmDex->pResClasses[mir->dalvikInsn.vB]));
            break;

        case OP_CONST_WIDE:
            dvmCompilerPcgTranslateConstWide (cUnit, mir, mir->dalvikInsn.vB_wide);
            break;

        case OP_CONST_WIDE_16:
            dvmCompilerPcgTranslateConstWide (cUnit, mir, (s2)mir->dalvikInsn.vB);
            break;

        case OP_CONST_WIDE_HIGH16:
            dvmCompilerPcgTranslateConstWide (cUnit, mir, ( (u8)mir->dalvikInsn.vB) << 48);
            break;

        case OP_CONST_WIDE_32:
            dvmCompilerPcgTranslateConstWide (cUnit, mir, (s4)mir->dalvikInsn.vB);
            break;

        case OP_CONST_STRING:
        case OP_CONST_STRING_JUMBO:
            dvmCompilerPcgTranslateConstString (cUnit, mir);
            break;

        case OP_IF_EQ:
            dvmCompilerPcgTranslateIf (cUnit, mir, "eq");
            break;

        case OP_IF_NE:
            dvmCompilerPcgTranslateIf (cUnit, mir, "ne");
            break;

        case OP_IF_LT:
            dvmCompilerPcgTranslateIf (cUnit, mir, "slt");
            break;

        case OP_IF_GE:
            dvmCompilerPcgTranslateIf (cUnit, mir, "sge");
            break;

        case OP_IF_GT:
            dvmCompilerPcgTranslateIf (cUnit, mir, "sgt");
            break;

        case OP_IF_LE:
            dvmCompilerPcgTranslateIf (cUnit, mir, "sle");
            break;

        case OP_IF_GEZ:
            dvmCompilerPcgTranslateIfZero (cUnit, mir, "sge");
            break;

        case OP_IF_NEZ:
            dvmCompilerPcgTranslateIfZero (cUnit, mir, "ne");
            break;

        case OP_IF_EQZ:
            dvmCompilerPcgTranslateIfZero (cUnit, mir, "eq");
            break;

        case OP_IF_LTZ:
            dvmCompilerPcgTranslateIfZero (cUnit, mir, "slt");
            break;

        case OP_IF_GTZ:
            dvmCompilerPcgTranslateIfZero (cUnit, mir, "sgt");
            break;

        case OP_IF_LEZ:
            dvmCompilerPcgTranslateIfZero (cUnit, mir, "sle");
            break;

        case OP_GOTO:
        case OP_GOTO_16:
        case OP_GOTO_32:
            {
                BasicBlockPCG *bb = static_cast<BasicBlockPCG *> (mir->bb);

                //Paranoid
                assert (bb != 0);

                dvmCompilerPcgTranslateGoto (bb);
                break;
            }

        case OP_NEG_INT:
            dvmCompilerPcgTranslateIntOp (cUnit, mir, "neg");
            break;

        case OP_NOT_INT:
            dvmCompilerPcgTranslateIntOp (cUnit, mir, "not");
            break;

        case OP_SUB_INT:
        case OP_SUB_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "sub");
            break;

        case OP_RSUB_INT:
        case OP_RSUB_INT_LIT8:
            dvmCompilerPcgTranslateRsub (cUnit, mir);
            break;

        case OP_ADD_INT:
        case OP_ADD_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "add");
            break;

        case OP_OR_INT:
        case OP_OR_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "or");
            break;

        case OP_AND_INT:
        case OP_AND_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "and");
            break;

        case OP_XOR_INT:
        case OP_XOR_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "xor");
            break;

        case OP_SHL_INT:
        case OP_SHL_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "shl");
            break;

        case OP_SHR_INT:
        case OP_SHR_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "sar");
            break;

        case OP_USHR_INT:
        case OP_USHR_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "shr");
            break;

        case OP_ADD_INT_LIT8:
        case OP_ADD_INT_LIT16:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "add");
            break;

        case OP_OR_INT_LIT16:
        case OP_OR_INT_LIT8:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "or");
            break;

        case OP_XOR_INT_LIT8:
        case OP_XOR_INT_LIT16:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "xor");
            break;

        case OP_AND_INT_LIT16:
        case OP_AND_INT_LIT8:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "and");
            break;

        case OP_SHL_INT_LIT8:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "shl");
            break;

        case OP_SHR_INT_LIT8:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "sar");
            break;

        case OP_USHR_INT_LIT8:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "shr");
            break;

        case OP_MUL_INT:
        case OP_MUL_INT_2ADDR:
            dvmCompilerPcgTranslateIntOpOp (cUnit, mir, "imul");
            break;

        case OP_MUL_INT_LIT8:
        case OP_MUL_INT_LIT16:
            dvmCompilerPcgTranslateIntOpLit (cUnit, mir, "imul");
            break;

        case OP_INT_TO_LONG:
            dvmCompilerPcgTranslateIntToLong (cUnit, mir);
            break;

        case OP_INT_TO_SHORT:
            dvmCompilerPcgTranslateIntExtend (cUnit, mir, "sext", 16);
            break;

        case OP_INT_TO_BYTE:
            dvmCompilerPcgTranslateIntExtend (cUnit, mir, "sext", 24);
            break;

        case OP_LONG_TO_INT:
            dvmCompilerPcgTranslateLongToInt (cUnit, mir);
            break;

        case OP_INT_TO_CHAR:
            dvmCompilerPcgTranslateIntExtend (cUnit, mir, "zext", 16);
            break;

        case OP_CMP_LONG:
            dvmCompilerPcgTranslateCmpLong (cUnit, mir);
            break;

        case OP_MUL_LONG:
        case OP_MUL_LONG_2ADDR:
            dvmCompilerPcgTranslateLLreg (cUnit, mir, "limul");
            break;

        case OP_ADD_LONG:
        case OP_ADD_LONG_2ADDR:
            dvmCompilerPcgTranslateLLreg (cUnit, mir, "ladd");
            break;

        case OP_SUB_LONG:
        case OP_SUB_LONG_2ADDR:
            dvmCompilerPcgTranslateLLreg (cUnit, mir, "lsub");
            break;

        case OP_AND_LONG:
        case OP_AND_LONG_2ADDR:
            dvmCompilerPcgTranslateLLreg (cUnit, mir, "land");
            break;

        case OP_OR_LONG:
        case OP_OR_LONG_2ADDR:
            dvmCompilerPcgTranslateLLreg (cUnit, mir, "lor");
            break;

        case OP_XOR_LONG:
        case OP_XOR_LONG_2ADDR:
            dvmCompilerPcgTranslateLLreg (cUnit, mir, "lxor");
            break;

        case OP_NEG_LONG:
            dvmCompilerPcgTranslateLLregOp (cUnit, "lneg", ssaRep->defs[0], ssaRep->uses[0]);
            break;

        case OP_NOT_LONG:
            dvmCompilerPcgTranslateLLregOp (cUnit, "lnot", ssaRep->defs[0], ssaRep->uses[0]);
            break;

        case OP_SHL_LONG:
        case OP_SHL_LONG_2ADDR:
            dvmCompilerPcgTranslateLLregShift (cUnit, mir, "lshl");
            break;

        case OP_SHR_LONG:
        case OP_SHR_LONG_2ADDR:
            dvmCompilerPcgTranslateLLregShift (cUnit, mir, "lsar");
            break;

        case OP_USHR_LONG:
        case OP_USHR_LONG_2ADDR:
            dvmCompilerPcgTranslateLLregShift (cUnit, mir, "lshr");
            break;

        case OP_DIV_INT_2ADDR:
        case OP_REM_INT_2ADDR:
        case OP_DIV_INT:
        case OP_REM_INT:
            dvmCompilerPcgTranslateDivRemInt (cUnit, mir);
            break;

        case OP_DIV_LONG:
        case OP_REM_LONG:
        case OP_DIV_LONG_2ADDR:
        case OP_REM_LONG_2ADDR:
            dvmCompilerPcgTranslateDivRemLong (cUnit, mir);
            break;

        case OP_DIV_INT_LIT8:
        case OP_REM_INT_LIT8:
        case OP_DIV_INT_LIT16:
        case OP_REM_INT_LIT16:
            dvmCompilerPcgTranslateDivRemIntLit (cUnit, mir);
            break;

        case OP_ADD_FLOAT_2ADDR:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "addss1");
            break;

        case OP_SUB_FLOAT_2ADDR:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "subss1");
            break;

        case OP_MUL_FLOAT_2ADDR:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "mulss1");
            break;

        case OP_ADD_FLOAT:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "addss1");
            break;

        case OP_SUB_FLOAT:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "subss1");
            break;

        case OP_MUL_FLOAT:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "mulss1");
            break;

        case OP_DIV_FLOAT:
        case OP_DIV_FLOAT_2ADDR:
            dvmCompilerPcgTranslateFloat (cUnit, mir, "divss1");
            break;

        case OP_REM_FLOAT:
        case OP_REM_FLOAT_2ADDR:
            dvmCompilerPcgTranslateRemFloat (cUnit, mir);
            break;

        case OP_REM_DOUBLE:
        case OP_REM_DOUBLE_2ADDR:
            dvmCompilerPcgTranslateRemDouble (cUnit, mir);
            break;

        case OP_ADD_DOUBLE:
        case OP_ADD_DOUBLE_2ADDR:
            dvmCompilerPcgTranslateDouble (cUnit, mir, "addsd1");
            break;

        case OP_SUB_DOUBLE:
        case OP_SUB_DOUBLE_2ADDR:
            dvmCompilerPcgTranslateDouble (cUnit, mir, "subsd1");
            break;

        case OP_MUL_DOUBLE:
        case OP_MUL_DOUBLE_2ADDR:
            dvmCompilerPcgTranslateDouble (cUnit, mir, "mulsd1");
            break;

        case OP_DIV_DOUBLE:
        case OP_DIV_DOUBLE_2ADDR:
            dvmCompilerPcgTranslateDouble (cUnit, mir, "divsd1");
            break;

        case OP_INT_TO_DOUBLE:
            dvmCompilerPcgTranslateIntToFP (cUnit, mir, 8);
            break;

        case OP_INT_TO_FLOAT:
            dvmCompilerPcgTranslateIntToFP (cUnit, mir, 4);
            break;

        case OP_LONG_TO_DOUBLE:
            dvmCompilerPcgTranslateLongToFP (cUnit, mir, 8);
            break;

        case OP_LONG_TO_FLOAT:
            dvmCompilerPcgTranslateLongToFP (cUnit, mir, 4);
            break;

        case OP_DOUBLE_TO_INT:
            dvmCompilerPcgTranslateFPToInt (cUnit, mir, 8);
            break;

        case OP_FLOAT_TO_INT:
            dvmCompilerPcgTranslateFPToInt (cUnit, mir, 4);
            break;

        case OP_DOUBLE_TO_LONG:
            dvmCompilerPcgTranslateFPToLong (cUnit, mir, 8);
            break;

        case OP_FLOAT_TO_LONG:
            dvmCompilerPcgTranslateFPToLong (cUnit, mir, 4);
            break;

        case OP_FLOAT_TO_DOUBLE:
            dvmCompilerPcgTranslateFloatToDouble (cUnit, mir);
            break;

        case OP_DOUBLE_TO_FLOAT:
            dvmCompilerPcgTranslateDoubleToFloat (cUnit, mir);
            break;

        case OP_NEG_FLOAT:
            dvmCompilerPcgTranslateNegFloat (cUnit, mir);
            break;

        case OP_NEG_DOUBLE:
            dvmCompilerPcgTranslateNegDouble (cUnit, mir);
            break;

        case OP_CMPG_FLOAT:
            dvmCompilerPcgTranslateIfFp (cUnit, mir, 4, 1);
            break;

        case OP_CMPL_FLOAT:
            dvmCompilerPcgTranslateIfFp (cUnit, mir, 4, -1);
            break;

        case OP_CMPG_DOUBLE:
            dvmCompilerPcgTranslateIfFp (cUnit, mir, 8, 1);
            break;

        case OP_CMPL_DOUBLE:
            dvmCompilerPcgTranslateIfFp (cUnit, mir, 8, -1);
            break;

        case OP_IGET_WIDE_QUICK:
            dvmCompilerPcgTranslateIgetWideQuick (cUnit, mir);
            break;

        case OP_IGET_OBJECT_QUICK:
        case OP_IGET_QUICK:
            dvmCompilerPcgTranslateIgetObjectQuick (cUnit, mir);
            break;

        case OP_IGET:
        case OP_IGET_BOOLEAN:
        case OP_IGET_BYTE:
        case OP_IGET_CHAR:
        case OP_IGET_SHORT:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, true, false, false, false);
            break;

        case OP_IGET_VOLATILE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, true, false, false, true);
            break;

        case OP_IGET_WIDE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, true, false, true, false);
            break;

        case OP_IGET_WIDE_VOLATILE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, true, false, true, true);
            break;

        case OP_IGET_OBJECT:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, true, true, false, false);
            break;

        case OP_IGET_OBJECT_VOLATILE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, true, true, false, true);
            break;

        case OP_IPUT:
        case OP_IPUT_BOOLEAN:
        case OP_IPUT_BYTE:
        case OP_IPUT_CHAR:
        case OP_IPUT_SHORT:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, false, false, false, false);
            break;

        case OP_IPUT_VOLATILE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, false, false, false, true);
            break;

        case OP_IPUT_OBJECT:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, false, true, false, false);
            break;

        case OP_IPUT_OBJECT_VOLATILE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, false, true, false, true);
            break;

        case OP_IPUT_WIDE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, false, false, true, false);
            break;

        case OP_IPUT_WIDE_VOLATILE:
            dvmCompilerPcgTranslateIgetIput (cUnit, mir, false, false, true, true);
            break;

        case OP_EXECUTE_INLINE:
        case OP_EXECUTE_INLINE_RANGE:
            dvmCompilerPcgTranslateExecuteInline (cUnit, mir);
            break;

        case OP_MONITOR_ENTER:
            dvmCompilerPcgTranslateMonitorEnter (cUnit, mir);
            break;

        case OP_MONITOR_EXIT:
            dvmCompilerPcgTranslateMonitorExit (cUnit, mir);
            break;

        case OP_IPUT_QUICK:
        case OP_IPUT_OBJECT_QUICK:
        case OP_IPUT_WIDE_QUICK:
            dvmCompilerPcgTranslateIput (cUnit, mir);
            break;

        case OP_AGET:
        case OP_AGET_OBJECT:
        case OP_AGET_WIDE:
        case OP_AGET_BYTE:
        case OP_AGET_BOOLEAN:
        case OP_AGET_CHAR:
        case OP_AGET_SHORT:
            dvmCompilerPcgTranslateAget (cUnit, mir);
            break;

        case OP_APUT:
        case OP_APUT_CHAR:
        case OP_APUT_BYTE:
        case OP_APUT_BOOLEAN:
        case OP_APUT_SHORT:
        case OP_APUT_WIDE:
            dvmCompilerPcgTranslateAput (cUnit, mir);
            break;

        case OP_APUT_OBJECT:
            dvmCompilerPcgTranslateAputObject (cUnit, mir);
            break;

        case OP_SGET:
        case OP_SGET_BOOLEAN:
        case OP_SGET_CHAR:
        case OP_SGET_BYTE:
        case OP_SGET_SHORT:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, true, false, false, false);
            break;

        case OP_SGET_VOLATILE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, true, false, false, true);
            break;

        case OP_SGET_OBJECT:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, true, true, false, false);
            break;

        case OP_SGET_OBJECT_VOLATILE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, true, true, false, true);
            break;

        case OP_SGET_WIDE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, true, false, true, false);
            break;

        case OP_SGET_WIDE_VOLATILE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, true, false, true, true);
            break;

        case OP_SPUT:
        case OP_SPUT_BYTE:
        case OP_SPUT_CHAR:
        case OP_SPUT_SHORT:
        case OP_SPUT_BOOLEAN:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, false, false, false, false);
            break;

        case OP_SPUT_VOLATILE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, false, false, false, true);
            break;

        case OP_SPUT_OBJECT:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, false, true, false, false);
            break;

        case OP_SPUT_OBJECT_VOLATILE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, false, true, false, true);
            break;

        case OP_SPUT_WIDE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, false, false, true, false);
            break;

        case OP_SPUT_WIDE_VOLATILE:
            success = dvmCompilerPcgTranslateSgetSput (cUnit, mir, false, false, true, true);
            break;

        case OP_PACKED_SWITCH:
            dvmCompilerPcgTranslatePackedSwitch (cUnit, mir);
            break;

        case OP_SPARSE_SWITCH:
            dvmCompilerPcgTranslateSparseSwitch (cUnit, mir);
            break;

        case OP_RETURN:
        case OP_RETURN_OBJECT:
        case OP_RETURN_WIDE:
            dvmCompilerPcgTranslateReturn (cUnit, mir, false);
            break;

        case OP_RETURN_VOID:
        case OP_RETURN_VOID_BARRIER:
            dvmCompilerPcgTranslateReturn (cUnit, mir, true);
            break;

        case OP_INVOKE_VIRTUAL:
        case OP_INVOKE_VIRTUAL_RANGE:
        case OP_INVOKE_VIRTUAL_QUICK:
        case OP_INVOKE_VIRTUAL_QUICK_RANGE:
            dvmCompilerPcgTranslateInvokeVirtual (cUnit, mir);
            break;

        case OP_INVOKE_SUPER:
        case OP_INVOKE_SUPER_RANGE:
        case OP_INVOKE_STATIC:
        case OP_INVOKE_STATIC_RANGE:
        case OP_INVOKE_SUPER_QUICK:
        case OP_INVOKE_SUPER_QUICK_RANGE:
            dvmCompilerPcgTranslateInvokeStaticSuper (cUnit, mir);
            break;

        case OP_INVOKE_INTERFACE:
        case OP_INVOKE_INTERFACE_RANGE:
            dvmCompilerPcgTranslateInvokeInterface (cUnit, mir);
            break;

        case OP_INVOKE_DIRECT:
        case OP_INVOKE_DIRECT_RANGE:
            dvmCompilerPcgTranslateInvokeDirect (cUnit, mir);
            break;

        case OP_MOVE_RESULT:
        case OP_MOVE_RESULT_OBJECT:
        case OP_MOVE_RESULT_WIDE:
            dvmCompilerPcgTranslateMoveResult (cUnit, mir);
            break;

        case OP_NEW_INSTANCE:
            dvmCompilerPcgTranslateNewInstance (cUnit, mir);
            break;

        case OP_NEW_ARRAY:
            dvmCompilerPcgTranslateNewArray (cUnit, mir);
            break;

        case OP_FILLED_NEW_ARRAY:
        case OP_FILLED_NEW_ARRAY_RANGE:
            dvmCompilerPcgTranslateFilledNewArray (cUnit, mir);
            break;

        case OP_FILL_ARRAY_DATA:
            dvmCompilerPcgTranslateFillArrayData (cUnit, mir);
            break;

        case OP_INSTANCE_OF:
            dvmCompilerPcgTranslateInstanceOf (cUnit, mir);
            break;

        case OP_CHECK_CAST:
            dvmCompilerPcgTranslateCheckCast (cUnit, mir);
            break;

        case OP_ARRAY_LENGTH:
            dvmCompilerPcgTranslateArrayLength (cUnit, mir);
            break;

        default:
            ALOGI ("XXXXXXXXXXXXXX Insn: %s (%d)\n",
                    dvmCompilerGetOpcodeName (dalvikOpCode), dalvikOpCode);
            ALOGE ("Jit (PCG): unsupported MIR opcode");
            assert (0);
            cUnit->errorHandler->setError (kJitErrorUnsupportedBytecode);
    }

    if (cUnit->errorHandler->isAnyErrorSet () == true)
    {
        return false;
    }

    return success;
}

/**
 * @brief Handle the from interpreter node
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return true if success
 */
static bool handleFromInterpreter (CompilationUnitPCG *cUnit, BasicBlockPCG *bb)
{
    //Bind the block label
    dvmCompilerPcgBindBlockLabel (bb);

    //Create entry stub
    dvmCompilerPcgCreateEntryStub (cUnit);

    //Let us get the preheader we care about
    BasicBlockPCG *preheader = static_cast<BasicBlockPCG *> (bb->fallThrough);

    //Paranoid
    if (preheader == 0)
    {
        //TODO: should add an error handler here
        return true;
    }

    //Since we have an entry with live-ins, handle those initial loads
    dvmCompilerPcgLoadLiveInVRs (cUnit, preheader);

    //Now jump to preheader
    dvmCompilerPcgTranslateDirectJumpToBlock (preheader);

    //Report success
    return true;
}

/**
 * @brief Check if the BasicBlockPCG is possibly referenced, if not report failure
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlockPCG
 * @return whether or not the BasicBlockPCG is possibly referenced
 */
static bool checkPossiblyReferenced (const CompilationUnitPCG *cUnit, const BasicBlockPCG *bb)
{
    if (bb->possiblyReferenced == false)
    {
        if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
        {
            ALOGI ("XXXXXX block [%d] is not referenced. Skipping any IL.\n", bb->id);
        }

        //Report it is not
        return false;
    }

    //Report it is
    return true;
}

/**
 * @brief Handle a pre backward branch block
 * @param cUnit the CompilationUnitPCG
 * @param bb the pre backward branch block
 */
static void handlePreBackwardBlock (CompilationUnitPCG *cUnit, BasicBlockPCG *bb)
{
    // First create a symbol and associate it with the label of the pre backward block.
    CGLabel cgLabel = CGCreateLabel();
    CGSymbol cgSymbol;
    std::string blockName;
    dvmCompilerPcgGetBlockName (bb, blockName);
    // We want the block to have an aligned symbol location, to enable the jump to it
    // to be safely patchable
    cgSymbol = dvmCompilerPcgCreateSymbol (cUnit, blockName, 0, true);
    cUnit->addLabelSymbolPair(cgLabel, cgSymbol);

    // Now find the backward branch chaining cell block and store the
    // symbol so we can capture its address when we generate the CC.
    BasicBlockPCG *bwccBlock = (BasicBlockPCG*)bb->fallThrough;
    if (bwccBlock == NULL || bwccBlock->blockType != kChainingCellBackwardBranch)
    {
        ALOGI("Unexpected CFG for pre backward block");
        exit(-1);
    }
    bwccBlock->writebackTargetSymbol = cgSymbol;

    // Find the loop header block. This block is the target
    // of the back edge of the loop in loop traces.
    CGLabel loopHeadLabel = CGLabelInvalid;
    BasicBlockPCG *loopHeader = NULL;
    LoopInformation *loopInfo = cUnit->loopInformation;

    if (loopInfo != 0)
    {
        loopInfo = loopInfo->getLoopInformationByEntry (bwccBlock->fallThrough);

        if (loopInfo != 0)
        {
            loopHeader = (BasicBlockPCG *) loopInfo->getEntryBlock ();
        }
    }

    if (loopHeader != 0)
    {
        loopHeadLabel = loopHeader->cgLabel;
    }

    // Generate the patchable jump.  We generate an unconditional jump
    // here and rely on PCG to optimize it if the predecessor block ends
    // in a conditional jump to this block.  The possible branch targets
    // are the pre backward block label (before the jump is patched) and
    // the loop head (after the jump is patched).
    CGInst jmp = CGCreateNewInst ("jmp", "n", cgSymbol);
    CGAddIndirectBranchTarget(jmp, cgLabel);
    assert(loopHeadLabel != 0);
    CGAddIndirectBranchTarget(jmp, loopHeadLabel);
    CGBindLabel(cgLabel);
}

/**
 * @brief Generate the code for a generic BasicBlock
 * @param cUnit the CompilationUnitPCG
 * @param bb the pre backward branch block
 * @return success or not
 */
static bool handleBBCodeGeneration (CompilationUnitPCG *cUnit, BasicBlockPCG *bb)
{
    // Track the modified SSANums so that we can do the proper writebacks at side exits.
    BitVector *currModBV = cUnit->getCurrMod ();

    dvmCopyBitVector (currModBV, bb->dirtyIns);

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        if (dvmCompilerPcgTranslateInsn (cUnit, mir) == false)
        {
            return false;
        }

        // Get opcode
        Opcode opcode = mir->dalvikInsn.opcode;

        // Ignore PHI's.  They do not define a new value, so they do not
        // "dirty" a VR.
        if ( static_cast<ExtendedMIROpcode> (opcode) == kMirOpPhi)
        {
            continue;
        }

        // Update the mod SSANum set.
        for (int i = 0; i < mir->ssaRep->numDefs; i++)
        {
            int ssaNum = mir->ssaRep->defs[i];
            u2 vrNum = dvmExtractSSARegister (cUnit, ssaNum);

            // Remove any defs of this VR from the mod set
            BitVector *bv = cUnit->getSSANumSet (vrNum);
            dvmSubtractBitVectors (currModBV, currModBV, bv);

            //Get the information
            SSANumInfo &info = cUnit->getRootSSANumInformation (ssaNum);

            // Add this define
            if (info.registerize == true)
            {
                dvmSetBit (currModBV, info.parentSSANum);
            }
        }
    }

    //Handle fallthrough now
    if (bb->fallThrough && bb->fallThrough->blockType != kExitBlock && bb->fallThrough->blockType != kPCReconstruction)
    {
        if (dvmCompilerPcgBlockEndsInInvoke (bb) == false)
        {
            dvmCompilerPcgDoWritebacksOnEdge (cUnit, bb, (BasicBlockPCG *) bb->fallThrough);

            BasicBlockPCG *bbPCG = (BasicBlockPCG *) (bb->fallThrough);
            dvmCompilerPcgTranslateDirectJumpToBlock (bbPCG);
        }
    }

    //Handle taken now
    if (bb->taken != 0 && dvmCompilerPcgBlockEndsInInvoke (bb) == false)
    {
        CGBindLabel (bb->takenLabel);
        dvmCompilerPcgDoWritebacksOnEdge (cUnit, bb, (BasicBlockPCG *) bb->taken);

        BasicBlockPCG *bbPCG = (BasicBlockPCG *) (bb->taken);
        dvmCompilerPcgTranslateDirectJumpToBlock (bbPCG);
    }

    //Report success
    return true;
}

/**
 * @brief Handle the code generation of a generic BasicBlock
 * @param cUnit the CompilationUnitPCG
 * @param bb the pre backward branch block
 * @return success or not
 */
bool handleGeneralBasicBlock (CompilationUnitPCG *cUnit, BasicBlockPCG *bb)
{
    //First check if it is possibly referenced
    if (checkPossiblyReferenced (cUnit, bb) == false)
    {
        //If it isn't referenced, we are done so we report success
        return true;
    }

    //Next bind the block label
    dvmCompilerPcgBindBlockLabel (bb);

    //Prebackward branch has a specific pre code generation sequence
    if (bb->blockType == kPreBackwardBlock)
    {
        // The backward branch chaining cell needs to know the address of the
        // corresponding pre-backward block.  Create that association when
        // we see the pre-backward block.
        handlePreBackwardBlock (cUnit, bb);
    }

    //Now handle code generation for the block
    if (handleBBCodeGeneration (cUnit, bb) == false)
    {
        //Report failure
        return false;
    }

    //Report success
    return true;
}
/**
 * @brief Translate the BasicBlock
 * @param cUnit the CompilationUnitPCG
 * @param bb the BasicBlock
 */
static bool dvmCompilerPcgTranslateBB (CompilationUnitPCG *cUnit, BasicBlockPCG *bb)
{
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true) {
        BasicBlockPCG *ft = (BasicBlockPCG *) (bb->fallThrough);
        BasicBlockPCG *taken = (BasicBlockPCG *) (bb->taken);

        char bbName[BLOCK_NAME_LEN] = "";
        dvmGetBlockName (bb, bbName);

        char ftName[BLOCK_NAME_LEN] = "";

        if (ft != 0)
        {
            dvmGetBlockName (ft, ftName);
        }

        char takenName[BLOCK_NAME_LEN] = "";

        if (taken != 0)
        {
            dvmGetBlockName (taken, takenName);
        }

        ALOGI ("\nStarting %s Translation (BB:%d, FallThrough:%d%s, Taken:%d%s)\n",
                bbName, bb->id,
                bb->fallThrough ? bb->fallThrough->id : -1,
                ftName, bb->taken ? bb->taken->id : -1,
                takenName
              );
        ALOGI ("------------------------------\n");
    }

    switch (bb->blockType)
    {
        case kEntryBlock:
            // The entry block is a nop.  It is like firstBlock in PCG.
            return true;

        case kExceptionHandling:
            // ZZZ TODO : this needs to be lowered properly. Also, check to see
            // why we hit this. Comment for jumpToInterpPunt (which this calls
            // in Dalvik) says "jump from JIT'ed code to interpreter becaues of
            // exception"... need to understand why we might hit this.
            //
            // * grumble, grumble, grumble *
            if (cUnit->getExceptionBlockReferenced () == true && bb->id == cUnit->exceptionBlockId)
            {
                dvmCompilerPcgBindBlockLabel (bb);

                CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
                CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
                CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

                //Get symbol to the dvmJitToExceptionThrown callback
                CGSymbol callback = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "dvmJitToInterpPunt");

                //Paranoid
                assert (callback != 0);

                dvmCompilerPcgCreateJsr (cUnit, callback, parms);
            }
            else
            {
                if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
                {
                    ALOGI ("\nNot binding exception block #%d.\n",bb->id);
                }
            }
            return true;

        case kPCReconstruction:
            return true;

        case kFromInterpreter:
            handleFromInterpreter (cUnit, bb);
            return true;

        case kExitBlock:
        case kDalvikByteCode:
        case kPreBackwardBlock:
            //Handle the general basic block
            if (handleGeneralBasicBlock (cUnit, bb) == false)
            {
                //Return failure
                return false;
            }

            //Send out a success return
            return true;

        default:
            //Set the error
            cUnit->errorHandler->setError (kJitErrorPcgUnknownBlockType);
            break;
    }

    return true;
}

/**
 * @brief Translate a CompilationUnitPCG
 * @param cUnit the CompilationUnitPCG
 * @param info the JitTranslationInfo associated
 * @return whether or not it succeeded
 */
static bool dvmCompilerPcgTranslateCUnit (CompilationUnitPCG *cUnit, JitTranslationInfo* info)
{
    unsigned int i;
    GrowableList *blockList = &cUnit->blockList;
    BasicBlockPCG *bb;

    //TODO: ask why we renumber it
    int dvmCompilerPcgExceptionBlockId = -1;

    // do a cfg reaching walk to figure out if blocks are referenced, or not.
    //TODO use iterator
    for (i = 0; i < blockList->numUsed; i++)
    {
        bb = (BasicBlockPCG *)blockList->elemList[i];
        if (bb->blockType == kEntryBlock)
        {
            cUnit->entryBlock = bb;
            dvmCompilerPcgMarkPossiblyReferenced (bb);
            break;
        }
    }

    for (i = 0; i < blockList->numUsed; i++)
    {
        bb = (BasicBlockPCG *) blockList->elemList[i];

        // Chaining cells and exception handling blocks need to be processed
        // regardless of whether possiblyReferenced is true.  For exception
        // handling blocks, we need to capture dvmCompilerPcgExceptionBlockId, because
        // there can be implicit references to the exception block, e.g. via
        // bytecodes whose expansion includes a null check.  Chaining cells
        // need to be processed, because we must lay down the chaining cell
        // regardless of whether it is referenced in the code, and we need
        // its chainingCellSymbol in order to do that.
        if (bb->possiblyReferenced == false && bb->blockType != kFromInterpreter &&
                                               bb->blockType != kExceptionHandling && bb->blockType >= kChainingCellLast)
        {
            continue;
        }

        // Define a label and symbol for each basic block.  We might or might
        // not need them depending on the contents of the MIR.  Also capture
        // any other necessary information about the block.  For symbols, use
        // 0 as the address for now.  It cannot be resolved until later.
        bb->cgLabel = CGCreateLabel ();
        bb->takenLabel = CGCreateLabel ();
        bb->blockBound = false;

        bb->writebackTargetSymbol = CGSymbolInvalid;

        if (bb->blockType < kChainingCellLast)
        {
            std::string blockName;
            dvmCompilerPcgGetBlockName (bb, blockName);

            // We want some block symbols to be marked as having references to them being aligned
            // so that jumps to the symbol are safely patchable
            bool needPatchableSymbol = true;

            if (bb->blockType == kChainingCellInvokePredicted)
            {
                // Predicted chaining cells don't need patchable symbols
                needPatchableSymbol = false;
            }

            // Now create the symbol for the block
            bb->chainingCellSymbol = dvmCompilerPcgCreateSymbol (cUnit, blockName, 0, needPatchableSymbol);
        }

        // Attempt to merge all the exception handling blocks
        // into the first one that we see.
        if (bb->taken && bb->taken->blockType == kExceptionHandling)
        {
            if (dvmCompilerPcgExceptionBlockId == -1)
            {
                cUnit->exceptionBlockId = bb->taken->id;
                dvmCompilerPcgExceptionBlockId = bb->taken->id;
            }
            else
            {
                bb->taken = (BasicBlock *)blockList->elemList[cUnit->exceptionBlockId];
            }
            cUnit->setExceptionBlockReferenced (true);
        }

        if (bb->fallThrough && bb->fallThrough->blockType == kExceptionHandling)
        {
            if (dvmCompilerPcgExceptionBlockId == -1)
            {
                cUnit->exceptionBlockId = bb->fallThrough->id;
                dvmCompilerPcgExceptionBlockId = bb->fallThrough->id;
            }
            else
            {
                bb->fallThrough = (BasicBlock *)blockList->elemList[cUnit->exceptionBlockId];
            }
            cUnit->setExceptionBlockReferenced (true);
        }

        if (bb->blockType == kExceptionHandling)
        {
            if (dvmCompilerPcgExceptionBlockId == -1)
            {
                cUnit->exceptionBlockId = i;
                dvmCompilerPcgExceptionBlockId = i;
            }
        }
    }

    // Walk the list of blocks and translate the non-chaining blocks.
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true)
    {
        ALOGI ("Starting translation for trace %d\n", cUnit->getTraceID ());
        ALOGI ("=====================================\n");
    }

    GrowableList *chainingListByType = cUnit->getChainingList ();

    for (i = 0; i < blockList->numUsed; i++)
    {
        bb = (BasicBlockPCG *)blockList->elemList[i];

        if (bb->blockType < kChainingCellLast)
        {
            dvmInsertGrowableList (&chainingListByType[bb->blockType], i);
        }
        else
        {
            if (dvmCompilerPcgTranslateBB (cUnit, bb) == false)
            {
                return false;
            }
        }
    }

    // Insert a trace-exit trampoline for every exit from the trace.
    // Basically, the idea is to bind the label for the chaining call and
    // have it serve as the branch target for all the other blocks that branch
    // to the chaining cell.  We'll insert all the necessary exit code in
    // the trampoline, and then the trampoline will jump to the "real"
    // chaining cell.
    //
    // Note that we do not consider kChainingCellInvokePredicted chaining cells
    // here.  Those are not real code blocks.  Rather, they hold data that is
    // loaded in the predicted chaining code.  For some reason, they model this
    // as a taken branch to the kChainingCellInvokePredicted chaining cell from
    // the invoke block in the MIR.
    for (i = 0; i < kChainingCellGap; i++)
    {
        if (i == kChainingCellNormal || i == kChainingCellBackwardBranch || i == kChainingCellHot)
        {
            unsigned int j;
            for (j = 0; j < chainingListByType[i].numUsed; j++)
            {
                int *blockIdList = (int *)chainingListByType[i].elemList;
                int blockId = blockIdList[j];

                BasicBlockPCG *bb = cUnit->getBasicBlockPCG (blockId);

                //Paranoid test
                if (bb == 0)
                {
                    //For the moment just make it fail with the generic error
                    cUnit->errorHandler->setError (kJitErrorPcgCodegen);

                    //Just return because this is already a bad enough situation
                    return false;
                }

                if (bb->possiblyReferenced == false)
                {
                    continue;
                }

                dvmCompilerPcgBindBlockLabel (bb);

                dvmCompilerPcgGenerateWritebacks (cUnit, bb->dirtyIns);

                // Define the parms for the jsr here.
                CGInst parmEdi = dvmCompilerPcgGenerateVMPtrMov (cUnit);
                CGInst parmEbp = dvmCompilerPcgGenerateFramePtrMov (cUnit);
                CGInst parms[3] = {parmEdi, parmEbp, CGInstInvalid};

                //Create the JSR
                dvmCompilerPcgCreateJsr (cUnit, bb->chainingCellSymbol, parms);
            }
        }
    }

   (void) info;

    return true;
}

/**
 * @brief Generate PCGil for the entry idiom
 * @details The only thing unique about this entry sequence is that we also model the virtual machine state pointer as an incoming argument in EDI
 * @param cUnit the CompilationUnitPCG
 */
static void dvmCompilerPcgGenerateEntryIl (CompilationUnitPCG *cUnit)
{
    CGInst inst, entryInst;

    entryInst = CGCreateEntryInst ();
    // Use an esp frame so that we can spill to the stack.
    CGSetRreg (entryInst, "esp");
    inst = entryInst;
    inst = CGCreateNewInst ("spsubi", "ri", inst, 0);
    CGSetRreg (inst, "esp");
    inst = CGCreateNewInst ("idef", "a", entryInst);
    CGSetRreg (inst, "edi");
    inst = CGCreateNewInst ("mov", "r", inst);

    //TODO: most likely getVMPtrReg can disappear and we can generate the temp here but it's used in aan unused function in PCGInterface, so check there, if we can remove that one, then we can remove vmptrreg from cUnit and generate a temp here
    CGAddTempDef (cUnit->getVMPtrReg (), inst);
    cUnit->setVMPtr (CGGetTempUseInst (cUnit->getVMPtrReg ()));

    CGInst framePtr = CGCreateNewInst ("idef", "a", entryInst);
    CGSetRreg (framePtr, "ebp");
    framePtr = CGCreateNewInst ("mov", "r", framePtr);
    CGAddTempDef(cUnit->getFramePtrReg (), framePtr);
    cUnit->setFramePtr (framePtr);

    //Get an entry insertion point
    CGInsertionPoint entryInsertionPoint = CGGetCurrentInsertionPoint ();
    cUnit->setEntryInsertionPoint (entryInsertionPoint);
}

bool dvmCompilerPcgGenerateIlForTrace (CompilationUnitPCG *cUnit, JitTranslationInfo* info)
{
    dvmCompilerPcgGenerateEntryIl (cUnit);
    dvmCompilerPcgGenerateSpeculativeNullChecks (cUnit);

    if (cUnit->errorHandler->isAnyErrorSet () == true)
    {
        return false;
    }

    if (dvmCompilerPcgTranslateCUnit (cUnit, info) == false)
    {
        return false;
    }

    dvmCompilerPcgAddVRInterfaceCode (cUnit);

    return true;
}

uint8_t* dvmCompilerPcgEmitMemConsts(CompilationUnitPCG *cUnit,
        uint8_t *currCachePtr,
        size_t *freeSpace)
{
    if (cUnit->checkDebugMask (DebugMaskBytecode) == true) {
        if (cUnit->memConstBegin() == cUnit->memConstEnd()) {
            return currCachePtr;
        }
        ALOGI ("LOWER memory constants at @%p\n", currCachePtr);
    }

    // Walk the mem consts
    for (MemConstIterator it = cUnit->memConstBegin();
         it != cUnit->memConstEnd(); ++it) {

        // Make sure we have enough room in the code cache for the mem const.
        uint8_t *alignedCachePtr = (uint8_t*)align ((char*)currCachePtr, it->first.align);
        size_t requiredSpace = (alignedCachePtr + it->first.length) - currCachePtr;
        if (*freeSpace < requiredSpace) {
            cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
            return 0;
        }

        // Align the current cache pointer.
        currCachePtr = alignedCachePtr;

        if (cUnit->checkDebugMask (DebugMaskBytecode) == true) {
            ALOGI ("    [%p] : [%x, %x, %x, %x], length = %d, alignment = %d.\n",
                   currCachePtr,
                   ((uint32_t *)(it->first.value))[0], ((uint32_t *)(it->first.value))[1],
                   ((uint32_t *)(it->first.value))[2], ((uint32_t *)(it->first.value))[3],
                   it->first.length, it->first.align);
        }

        dvmCompilerPcgBindSymbolAddress (cUnit, it->second, currCachePtr);
        memcpy(currCachePtr, it->first.value, it->first.length);
        currCachePtr += it->first.length;
        *freeSpace -= requiredSpace;
    }

    return currCachePtr;
}

/**
 * @brief Write data that includes the switch table and the constant data section to the data cache if possible or write to the code cache as fallback
 * @param cUnit the compilation unit
 * @param currCodeCachePtr the current pointer to the code cache
 * @param freeCodeSpace the number of free bytes in the code cache
 * @return true if success
 */
static bool dvmCompilerPcgWriteDataToDataOrCodeCache(CompilationUnitPCG *cUnit,
        uint8_t *&currCodeCachePtr,
        size_t freeCodeSpace)
{
    // Process the switch table and the constant data section
    // Estimate the switch table size
    size_t switchTableAlignment = 0;
    size_t switchTableSize = cUnit->getNumberOfSwitchTableEntries () * 4;
    // Align the switch table to 4 bytes
    if (switchTableSize > 0) {
        switchTableAlignment = 4;
    }

    // Estimate the constant data section size
    size_t constDataAlignment = 0;
    size_t constDataSize = 0;
    // Walk the mem consts
    for (MemConstIterator it = cUnit->memConstBegin();
         it != cUnit->memConstEnd(); ++it) {
        // We conservatively assume that each data needs 16 bytes due to alignment requirement
        constDataSize += 16;
    }
    // Align the const data section to 16 bytes
    if (constDataSize > 0) {
        constDataAlignment = 16;
    }

    // Calculate the total estimated data size
    size_t totalEstimatedDataSize = switchTableAlignment + switchTableSize + constDataAlignment + constDataSize;

    // Check if we need to store any data
    if (totalEstimatedDataSize == 0) {
        // Nothing to store
        return true;
    }

    // Point to the stream start to write data
    uint8_t *streamDataStart = NULL;

    // Indicate if we can write data to the data cache
    bool useDataCache = false;

    // Record the number of free bytes in the data or code cache
    size_t freeSpace = 0;

    // Check if we can store data to the data cache
    if (dvmCompilerWillDataCacheOverflow(totalEstimatedDataSize) == false) {
        // We can write data to the data cache
        useDataCache = true;

        // Update freeSpace to free bytes in the data cache
        freeSpace = gDvmJit.dataCacheSize - gDvmJit.dataCacheByteUsed;

        // Set the start pointer for the data cache
        streamDataStart = (uint8_t *)gDvmJit.dataCache + gDvmJit.dataCacheByteUsed;

        // Unprotect data cache
        UNPROTECT_DATA_CACHE(streamDataStart, totalEstimatedDataSize);
    }
    else {
        // Set data cache full
        dvmCompilerSetDataCacheFull();

        // Check if we can store data to the code cache
        if (freeCodeSpace < totalEstimatedDataSize) {
            // We don't have enough room in the code cache
            cUnit->errorHandler->setError (kJitErrorCodeCacheFull);

            // Fail
            return false;
        }

        // Update freeSpace to free bytes in the code cache
        freeSpace = freeCodeSpace;

        // Set the start pointer to the pointer for the code cache
        streamDataStart = currCodeCachePtr;
    }

    // Point to the current location of the stream data
    uint8_t *streamData = streamDataStart;

    // Write the switch table
    if (switchTableSize > 0) {
        streamData = dvmCompilerPcgEmitSwitchTables(cUnit, streamData, freeSpace);
        if (cUnit->errorHandler->isAnyErrorSet () == true) {
            if (useDataCache == true) {
                // Protect data cache
                PROTECT_DATA_CACHE(streamDataStart, totalEstimatedDataSize);
            }

            // Fail due to errros
            return false;
        }
    }

    // Update freeSpace
    freeSpace -= streamData - streamDataStart;

    // Write the constant data section
    if (constDataSize > 0) {
        streamData = dvmCompilerPcgEmitMemConsts(cUnit, streamData, &freeSpace);

        if (cUnit->errorHandler->isAnyErrorSet () == true) {
            if (useDataCache == true) {
                // Protect data cache
                PROTECT_DATA_CACHE(streamDataStart, totalEstimatedDataSize);
            }

            // Fail due to errros
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

        // We need to update currentCodeCachePtr, because it will be used later.
        currCodeCachePtr = streamData;

        ALOGV("JIT code cache has the switch table and const data %uB", streamData - streamDataStart);
    }

    // Success, signal it
    return true;
}


void dvmCompilerPcgEmitCode (CompilationUnitPCG *cUnit, JitTranslationInfo* info)
{
    uint8_t *startAddr, *endAddr;
    uint32_t requiredAlign;
    uint8_t *cacheStartPtr = (uint8_t*)gDvmJit.codeCache + gDvmJit.codeCacheByteUsed;
    uint8_t *currCachePtr = cacheStartPtr;
    size_t freeSpace;
    const uint32_t cExtraBytesForChaining = 4;

    freeSpace = gDvmJit.codeCacheSize - gDvmJit.codeCacheByteUsed;

    // Allocate space for the chaining information.
    if (freeSpace < cExtraBytesForChaining)
    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return;
    }

    currCachePtr += cExtraBytesForChaining;
    freeSpace -= cExtraBytesForChaining;

    // The code buffer is fixed in memory, so we know the exact alignment.
    // Use 0x80000000 to indicate this.  Given that we know the alignment,
    // the only reason CGGetBinaryCode should fail is due to insufficient
    // space in the code cache.
    if (CGGetBinaryCode (currCachePtr, freeSpace, 0x80000000, &startAddr, &endAddr, &requiredAlign) == false)

    {
        cUnit->errorHandler->setError (kJitErrorCodeCacheFull);
        return;
    }

    if (cUnit->printMe == true)
    {
        // record all assmebly code before chaining cells as a block
        std::pair<BBType, char*> code_blk_elem(kDalvikByteCode, (char*)startAddr);
        cUnit->code_block_table->push_back(code_blk_elem);
    }

    freeSpace -= (endAddr + 1) - currCachePtr;

    cUnit->bindBlockSymbolAddresses (startAddr);

    // There are two sets of cross references that we need to save
    // The first is from the chaining cells to the switch table entries
    // The second is from the switch table to the beginning of the chaining
    // cells; this is easy, we have the symbols to those blocks already saved.
    // The first is harder, because it's a forward reference. Hence, we
    // create a relocation object for each normal chaining cell so that
    // the chaining cell can be updated when we know where the corresponding
    // switch table entry is laid down
    ChainCellCounts pcgChainCellCounts;
    currCachePtr = dvmCompilerPcgEmitChainingCells (cUnit, &pcgChainCellCounts, startAddr, endAddr + 1, freeSpace);

    if (cUnit->errorHandler->isAnyErrorSet () == true)
    {
        return;
    }

    freeSpace -= currCachePtr - (endAddr + 1);

    // Update cUnit->totalSize and gDvmJit.codeCacheByteUsed before writing
    // data.
    // cUnit->totalSize gives the total size, including initial padding.  We
    // also increment gDvmJit.codeCacheByteUsed by this amount.
    cUnit->totalSize = currCachePtr - cacheStartPtr;
    gDvmJit.codeCacheByteUsed += cUnit->totalSize;

    // Try to write switch tables and memory constants to data or code cache
    if (dvmCompilerPcgWriteDataToDataOrCodeCache(cUnit, currCachePtr, freeSpace) == false)
    {
        // Fail due to errros
        return;
    }

    cUnit->resolveAllRelocations (startAddr);

    // Update the necessary state following the successful compilation.
    // cUint->baseAddr is the function entry point.  So is info->codeAddress.
    // Increment gDvmJit.numCompilations to indicate successful compilation.
    cUnit->baseAddr = startAddr;
    info->codeAddress = startAddr;
    gDvmJit.numCompilations++;

    if (cUnit->checkDebugMask (DebugMaskDisasm) == true)
    {
        ALOGI ("Disassembly for trace %d\n", cUnit->getTraceID ());
        ALOGI ("=========================\n");
        dvmCompilerPcgPrintTrace (cUnit, pcgChainCellCounts, (u2*)(startAddr - cExtraBytesForChaining));
    }

    // TODO (DLK): stream needs to be updated here for the time being, because
    //     the call that patches instruction immediates (presumably jump
    //     offsets) asserts that the address being patched is less than stream.
    stream = (char*)currCachePtr;

    if (cUnit->checkDebugMask (DebugMaskDisasm) == true)
    {
        ALOGI ("Code cache range for trace %d [0x%p, 0x%p)\n", cUnit->getTraceID (),
                cacheStartPtr, stream);
    }

    if (cUnit->checkDebugMask (DebugMaskDisasm) == true)
    {
        ALOGD("-------- PCG: Emit trace for [%s%s@%#x] binary code starts at %p (cache start %p)",
                cUnit->method->clazz->descriptor, cUnit->method->name,
                cUnit->traceDesc->trace[0].info.frag.startOffset,
                cUnit->baseAddr, gDvmJit.codeCache);
    }
}

void *dvmCompilerPcgCreateHookFunction (void)
{
    //We are going to create the code to break and this will be our debugHook function
    uint8_t *debugHook = (uint8_t *) (stream);
    *debugHook = 0xcc;
    * (debugHook + 1) = 0xc3;
    stream += 2;

    //Now return start of debug hook
    return static_cast<void *> (debugHook);
}

#ifdef DEBUG_HOOK
static void dvmCompilerPcgDebugHook (void)
{
    CGSymbol target = singletonPtr<PersistentInfo> ()->getCallBack (cUnit, "debugHook");
    CGCreateNewInst ("call", "n", target);
}
#endif // DEBUG_HOOK

