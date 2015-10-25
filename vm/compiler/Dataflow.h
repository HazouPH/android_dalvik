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

#ifndef DALVIK_VM_DATAFLOW_H_
#define DALVIK_VM_DATAFLOW_H_

#include "Dalvik.h"
#include "CompilerInternals.h"

typedef enum DataFlowAttributePos {
    kUA = 0,
    kUB,
    kUC,
    kUAWide,
    kUBWide,
    kUCWide,
    kDA,
    kDAWide,
    kIsMove,
    kIsLinear,
    kSetsConst,
    kFormat35c,
    kFormat3rc,
    kFormatExtendedOp,   //!< Flag used to tag extended MIRs since each different dataflow treatment
    kPhi,
    kNullNRangeCheck0,
    kNullNRangeCheck1,
    kNullNRangeCheck2,
    kNullObjectCheck0,
    kNullObjectCheck1,
    kNullObjectCheck2,
    kFPA,
    kFPB,
    kFPC,
    kConstC,             //!< Used to determine whether vC is a constant
    kGetter,
    kSetter,
    kCall,
    kClobbersMemory,
    kAddExpression,
    kSubtractExpression,
    kMultiplyExpression,
    kDivideExpression,
    kRemainerExpression,
    kShiftLeftExpression,
    kSignedShiftRightExpression,
    kUnsignedShiftRightExpression,
    kAndExpression,
    kOrExpression,
    kXorExpression,
    kCastExpression,
} DataFlowAttributes;

#define DF_NOP                  0
#define DF_UA                   (1LL << kUA)
#define DF_UB                   (1LL << kUB)
#define DF_UC                   (1LL << kUC)
#define DF_UA_WIDE              (1LL << kUAWide)
#define DF_UB_WIDE              (1LL << kUBWide)
#define DF_UC_WIDE              (1LL << kUCWide)
#define DF_DA                   (1LL << kDA)
#define DF_DA_WIDE              (1LL << kDAWide)
#define DF_IS_MOVE              (1LL << kIsMove)
#define DF_IS_LINEAR            (1LL << kIsLinear)
#define DF_SETS_CONST           (1LL << kSetsConst)
#define DF_FORMAT_35C           (1LL << kFormat35c)
#define DF_FORMAT_3RC           (1LL << kFormat3rc)
#define DF_FORMAT_EXT_OP        (1LL << kFormatExtendedOp)
#define DF_PHI                  (1LL << kPhi)
#define DF_NULL_N_RANGE_CHECK_0 (1LL << kNullNRangeCheck0)
#define DF_NULL_N_RANGE_CHECK_1 (1LL << kNullNRangeCheck1)
#define DF_NULL_N_RANGE_CHECK_2 (1LL << kNullNRangeCheck2)
#define DF_NULL_OBJECT_CHECK_0  (1LL << kNullObjectCheck0)
#define DF_NULL_OBJECT_CHECK_1  (1LL << kNullObjectCheck1)
#define DF_NULL_OBJECT_CHECK_2  (1LL << kNullObjectCheck2)
#define DF_FP_A                 (1LL << kFPA)
#define DF_FP_B                 (1LL << kFPB)
#define DF_FP_C                 (1LL << kFPC)
#define DF_C_IS_CONST           (1LL << kConstC)
#define DF_IS_GETTER            (1LL << kGetter)
#define DF_IS_SETTER            (1LL << kSetter)
#define DF_IS_CALL              (1LL << kCall)
#define DF_CLOBBERS_MEMORY      (1LL << kClobbersMemory)
#define DF_ADD_EXPRESSION       (1LL << kAddExpression)
#define DF_SUBTRACT_EXPRESSION  (1LL << kSubtractExpression)
#define DF_MULTIPLY_EXPRESSION  (1LL << kMultiplyExpression)
#define DF_DIVIDE_EXPRESSION    (1LL << kDivideExpression)
#define DF_REMAINDER_EXPRESSION (1LL << kRemainerExpression)
#define DF_SHL_EXPRESSION       (1LL << kShiftLeftExpression)
#define DF_SHR_EXPRESSION       (1LL << kSignedShiftRightExpression)
#define DF_USHR_EXPRESSION      (1LL << kUnsignedShiftRightExpression)
#define DF_AND_EXPRESSION       (1LL << kAndExpression)
#define DF_OR_EXPRESSION        (1LL << kOrExpression)
#define DF_XOR_EXPRESSION       (1LL << kXorExpression)
#define DF_CAST                 (1LL << kCastExpression)

#define DF_HAS_USES             (DF_UA | DF_UB | DF_UC | DF_UA_WIDE | \
                                 DF_UB_WIDE | DF_UC_WIDE)

#define DF_HAS_DEFS             (DF_DA | DF_DA_WIDE)

#define DF_HAS_NR_CHECKS        (DF_NULL_N_RANGE_CHECK_0 | \
                                 DF_NULL_N_RANGE_CHECK_1 | \
                                 DF_NULL_N_RANGE_CHECK_2)

#define DF_HAS_OBJECT_CHECKS    (DF_NULL_OBJECT_CHECK_0 | \
                                 DF_NULL_OBJECT_CHECK_1 | \
                                 DF_NULL_OBJECT_CHECK_2)

#define DF_HAS_CONSTANTS        (DF_C_IS_CONST)

#define DF_A_IS_REG             (DF_UA | DF_UA_WIDE | DF_DA | DF_DA_WIDE)
#define DF_A_IS_USED_REG        (DF_UA | DF_UA_WIDE)
#define DF_A_IS_DEFINED_REG     (DF_DA | DF_DA_WIDE)
#define DF_B_IS_REG             (DF_UB | DF_UB_WIDE)
#define DF_C_IS_REG             (DF_UC | DF_UC_WIDE)
#define DF_IS_GETTER_OR_SETTER  (DF_IS_GETTER | DF_IS_SETTER)

extern long long dvmCompilerDataFlowAttributes[kMirOpLast];

typedef struct BasicBlockDataFlow {
    BitVector *useV;
    BitVector *defV;
    BitVector *liveInV;
    BitVector *liveOutV;
    int *dalvikToSSAMapExit;
    int *dalvikToSSAMapEntrance;
    unsigned int numEntriesDalvikToSSAMap; //!< Represents number of entries in each of the dalvikToSSAMap
} BasicBlockDataFlow;

/**
 * @class SUsedChain
 * @brief Used chain for each virtual register to link the MIRs together
 */
typedef struct sUsedChain
{
    struct sUsedChain *prevUse;        /**< @brief Chain containing the previous use */
    MIR *mir;                          /**< @brief MIR containing the current use */
    struct sUsedChain *nextUse;        /**< @brief Chain containing the next use */

    struct sUsedChain *nextChain;      /**< @brief Used internally by the chain builder */
}SUsedChain;

/**
 * @class SSARepresentation
 * @brief The SSA Representation for a MIR
 */
typedef struct SSARepresentation {
    int numUses;
    int *uses;
    bool *fpUse;
    int numDefs;
    int *defs;
    bool *fpDef;

    /** @brief For each definition in defs, we have an entry in the usedNext array
     *     If there is a WIDE, it gets two defs in the defs array and gets two entries in the def-use chain
     *     Depending on uses, it might be important/necessary to follow both chains
     */
    SUsedChain **usedNext;

    /** @brief Where the uses are defined:
     *      For each usage is uses, there is an entry in defWhere to provide the MIR containing the definition
     */
    MIR **defWhere;

} SSARepresentation;

/*
 * An induction variable is represented by "m*i + c", where i is a basic
 * induction variable.
 */
typedef struct InductionVariableInfo {
    int ssaReg;         //!< The ssa register defined by expression for IV
    int basicSSAReg;    //!< The basic ssa register involved
    int multiplier;     //!< Multiplier. For basic IV it is always 1.
    int constant;       //!< Constant. For basic IV is it always 0.
    int loopIncrement;  //!< Loop increment. Only relevant for basic IV to keep the loop increment/decrement.
    bool isBasic;       //!< Whether the induction variable is basic
    MIR *linearMir;     //!< MIR associated with the linear operation.
    MIR *multiplierMir; //!< MIR associated with the multiplication operation. Always null for Basic IV.
    MIR *phiMir;        //!< MIR associated with the phi node. May be null but never for Basic IV.

    int getMultiplier () const { return multiplier; }

    int getLoopIncrement () const { return loopIncrement; }

    int getConstant () const { return constant; }

    bool isBasicIV () const { return isBasic == true; }

    bool isDependentIV () const { return isBasic == false; }

} InductionVariableInfo;

typedef struct ArrayAccessInfo {
    int arrayReg;
    int ivReg;
    int maxC;                   // For DIV - will affect upper bound checking
    int minC;                   // For DIV - will affect lower bound checking
    int inc;                    // For DIV - will affect bound checking
} ArrayAccessInfo;

#define ENCODE_REG_SUB(r,s)             ((s<<16) | r)
#define DECODE_REG(v)                   (v & 0xffff)
#define DECODE_SUB(v)                   (((unsigned int) v) >> 16)

/**
 * @brief Returns 2 for wide put bytecodes, 1 for non-wide put bytecodes, 0 otherwise
 * @param opcode the considered opcode
 * @return the uses vector in the SSARepresentation's start index to be considered when comparing two instructions
 * @return 0 for any bytecode, 1 for non-wide put bytecodes, 2 for wide put bytecodes
 */
int dvmCompilerGetStartUseIndex (Opcode opcode);

/**
 * @brief Given a use of an MIR, return the SUsedChain to which it belongs
 * @param mir The MIR which contains the use
 * @param useIndex The index of the use for which we need the chain
 * @return The useChain if available, 0 otherwise
 */
SUsedChain *dvmCompilerGetUseChainForUse (MIR *mir, int useIndex);

/**
 * @brief Check whether opcode is volatile
 * @param opcode the considered opcode
 * @return true if opcode represents a volatile instruction
 */
bool dvmCompilerIsOpcodeVolatile (Opcode opcode);

#endif  // DALVIK_VM_DATAFLOW_H_
