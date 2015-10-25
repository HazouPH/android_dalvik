/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <map>
#include "Dalvik.h"
#include "Dataflow.h"
#include "libdex/DexOpcodes.h"
#include "Utility.h"

/**
 * @brief Used to define different failure modes for inlining
 */
enum InliningFailure
{
    kInliningNoError = 0,                //!< @brief No inlining error
    kInliningSuccess = kInliningNoError, //!< @brief Inlining success is the same as having no inlining error
    kInliningMirRemovalFailed,           //!< @brief Used when removal of an MIR fails
    kInliningInvokeBBNoChild,            //!< @brief Used when the invoke's BB does not have a child
    kInliningBadCalleeCFG,               //!< @brief Used when the callee CFG created is bad
    kInliningUnsupportedBytecodes,       //!< @brief Used when CFG building fails because of unsupported bytecodes for inlining
    kInliningCannotFindReturn,           //!< @brief Used when return bytecode from callee cannot be found
    kInliningCannotFindMoveResult,       //!< @brief Used when move-result bytecode from caller cannot be found
    kInliningMoveResultNoMatchReturn,    //!< @brief Used when move-result does not match return type
    kInliningCannotFindBytecode,         //!< @brief Used when bytecode for rewriting cannot be found
    kInliningNativeMethod,               //!< @brief Used when for inlining is native
    kInliningFailedBefore,               //!< @brief Used when we've tried inlining before and failed for same method
    kInliningNoVirtualSupport,           //!< @brief Used when backend does not support devirtualization
    kInliningDisabled,                   //!< @brief Used when inlining is disabled
    kInliningMethodTraceEnabled,         //!< @brief Used when method trace is enabled because inlining cannot happen
    kInliningSingleStepInvoke,           //!< @brief Used when invoke is selected for single stepping and thus inlining cannot happen
    kInliningInvokeBBProblem,            //!< @brief Used when the BB of invoke does not match what invoke believes
    kInliningNestedInlining,             //!< @brief Used when we try to inline an invoke that itself has been inlined
    kInliningUnknownMethod,              //!< @brief Used when it is not know which method to inline
    kInliningMoreThanOneBytecode,        //!< @brief Used when we are trying to inline a method with one bytecode and we find more than one
    kInliningAlreadyInlined,             //!< @brief Used when we have already inlined the invoke
    kInliningNoDefButMoveResult,         //!< @brief Used when caller has move-result but callee doesn't define anything
    kInliningDefNoMatchReturn,           //!< @brief Used when callee has a def that doesn't match the VR returned
    kInliningRewriteFailed,              //!< @brief Used when virtual register rewriting fails
    kInliningUnrecoverableRewrite,       //!< @brief Used when there is a rewriting failure that is not recoverable
    kInliningCalleeHasLoops,             //!< @brief Used when callee has loops
    kInliningVirtualRegNumberTooLarge,   //!< @brief Used when the renamed VR is larger than 16-bits
    kInliningNoBackendExtendedOpSupport, //!< @brief Used when backend does not support one of the generated extended ops
    kInliningCalleeMayThrow,             //!< @brief Used when callee may throw
    kInliningCalleeTooLarge,             //!< @brief Used when callee is too large
    kInliningCalleeNotLeaf,              //!< @brief Used when callee is not a leaf
    kInliningMethodComplicated,          //!< @brief Used when method is too complicated for inliner
    kInliningClassNotLoaded,             //!< @brief Used when we try to find class object but cannot because it is not loaded
    kInliningUnmatchedLocals,            //!< @brief Used when we try to inline simple method but there are local VR in callee without pair in caller
};

/**
 * @brief Used to get a message for failure mode
 * @param failure The type of inlining failure
 * @return Returns the message matching failure
 */
static const char *getFailureMessage (InliningFailure failure)
{
    const char *message = "unknown inlining failure";

    switch (failure) {
        case kInliningNoError:
            message = "";
            break;
        case kInliningMirRemovalFailed:
            message = "removing an MIR failed";
            break;
        case kInliningInvokeBBNoChild:
            message = "invoke's basic block does not have a child basic block";
            break;
        case kInliningBadCalleeCFG:
            message = "the callee method CFG has unexpected shape";
            break;
        case kInliningUnsupportedBytecodes:
            message = "during building of callee CFG, unsupported bytecodes were found";
            break;
        case kInliningCannotFindReturn:
            message = "cannot find return bytecode in callee CFG";
            break;
        case kInliningCannotFindMoveResult:
            message = "cannot find move-result in caller and we need one";
            break;
        case kInliningMoveResultNoMatchReturn:
            message = "the type of move-result does not match type of return";
            break;
        case kInliningCannotFindBytecode:
            message = "the single bytecode that we need to rewrite cannot be found";
            break;
        case kInliningNativeMethod:
            message = "native methods cannot be inlined";
            break;
        case kInliningFailedBefore:
            message = "we tried inlining method before and we failed";
            break;
        case kInliningNoVirtualSupport:
            message = "backend does not support devirtualization so we cannot inline virtual invokes";
            break;
        case kInliningDisabled:
            message = "inlining is disabled";
            break;
        case kInliningMethodTraceEnabled:
            message = "method tracing is enabled and thus we should not be inlining";
            break;
        case kInliningSingleStepInvoke:
            message = "invoke was selected for single stepping and we should not be inlining";
            break;
        case kInliningInvokeBBProblem:
            message = "the BB that holds invoke does not match what the invoke believe is its parent";
            break;
        case kInliningNestedInlining:
            message = "inlining of inlined invoke is not yet supported";
            break;
        case kInliningUnknownMethod:
            message = "cannot figure out what method needs to be inlined";
            break;
        case kInliningMoreThanOneBytecode:
            message = "more than one bytecode found when we weren't expecting";
            break;
        case kInliningAlreadyInlined:
            message = "already inlined invoke";
            break;
        case kInliningNoDefButMoveResult:
            message = "we have a move-result but inlined MIR doesn't define anything";
            break;
        case kInliningDefNoMatchReturn:
            message = "define of inlineable instruction does not match return";
            break;
        case kInliningRewriteFailed:
        case kInliningUnrecoverableRewrite:
            message = "virtual register rewriting failed";
            break;
        case kInliningCalleeHasLoops:
            message = "the CFG of callee method has loops and those are not yet supported";
            break;
        case kInliningVirtualRegNumberTooLarge:
            message = "register window shift causes virtual register number to exceed 16-bits";
            break;
        case kInliningNoBackendExtendedOpSupport:
            message = "backend does not support extended MIR needed for inlining";
            break;
        case kInliningCalleeMayThrow:
            message = "callee method has potential to throw exceptions";
            break;
        case kInliningCalleeNotLeaf:
            message = "callee method is not a leaf method";
            break;
        case kInliningCalleeTooLarge:
            message = "callee method exceeds number of maximum bytecodes";
            break;
        case kInliningMethodComplicated:
            message = "method is too complicated for inliner";
            break;
        case kInliningClassNotLoaded:
            message = "cannot find class object needed to create devirtualization check";
            break;
        case kInliningUnmatchedLocals:
            message = "cannot match all callee VRs to caller ones";
            break;
        default:
            break;
    }

    return message;
}

/**
 * @brief Checks if inlining failure that occurred is recoverable
 * @param failure The inlining failure
 * @return Returns true if inliner believes that error is fatal
 */
static bool isInliningFailureFatal (InliningFailure failure)
{
    bool isFatal = false;

    switch (failure) {
        //Bad CFG because invoke's BB should be consistent with the BB
        case kInliningInvokeBBProblem:
        //Bad CFG because there should always be BB after invoke's BB
        case kInliningInvokeBBNoChild:
        //MIR removal can only fail if there is a bad CFG when we cannot find a MIR in its BB
        case kInliningMirRemovalFailed:
        //Register window shift causes virtual register number to exceed 16-bits
        //Since this error can happen after inlining, we cannot safely recover.
        case kInliningVirtualRegNumberTooLarge:
        //The rewrite is unrecoverable thus fatal
        case kInliningUnrecoverableRewrite:
            isFatal = true;
            break;
        default:
            break;
    }

    return isFatal;
}

/**
 * @brief Determines if bytecode can be inlined.
 * @details Checks if all fields are resolved and then checks for unsupported bytecodes
 * @param method The method that is being inlined
 * @param insn The decoded instruction we want to examine
 * @param failureMessage In case of false return, it may be updated to point to a failure message.
 * @return Returns whether this bytecode can be inlined
 */
static bool canInlineBytecode (const Method *method, const DecodedInstruction *insn, const char **failureMessage)
{
    if (dvmCompilerCheckResolvedReferences (method, insn) == false)
    {
        if (failureMessage != 0)
        {
            *failureMessage = "could not resolve fields";
        }
        return false;
    }

    switch (insn->opcode)
    {
        case OP_INVOKE_VIRTUAL:
        case OP_INVOKE_INTERFACE:
        case OP_INVOKE_VIRTUAL_RANGE:
        case OP_INVOKE_INTERFACE_RANGE:
        case OP_INVOKE_VIRTUAL_QUICK:
        case OP_INVOKE_VIRTUAL_QUICK_RANGE:
            if (failureMessage != 0)
            {
                *failureMessage = "no support for making prediction for inlined virtual invokes";
            }
            return false;

        case OP_PACKED_SWITCH:
        case OP_SPARSE_SWITCH:
            if (failureMessage != 0)
            {
                *failureMessage = "no support for sparse/packed switch";
            }
            return false;

        case OP_NEW_INSTANCE:
        case OP_CHECK_CAST:
        case OP_FILLED_NEW_ARRAY:
        case OP_FILLED_NEW_ARRAY_RANGE:
        case OP_CONST_CLASS:
        case OP_NEW_ARRAY:
        case OP_INSTANCE_OF:
            //The reason we reject these is because the backends assume that the cUnit's method
            //is the one that needs to be use when looking for class. However, instead the MIR's
            //method should be used because we may be inlining a method that comes from a different
            //dex and thus class resolution won't be correct.
            if (failureMessage != 0)
            {
                *failureMessage = "backends need to support looking at class from MIR not class from cUnit";
            }
            return false;

        case OP_FILL_ARRAY_DATA:
            //When inlined as a single bytecode (without creating callee frame), the implementation of fill-array
            //assumes that the data it needs to load is at end of current method when in reality it needs to load
            //data from its original method.
            if (failureMessage != 0)
            {
                *failureMessage = "fill-array may try to load data from wrong location";
            }
            return false;

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
            //We should not be inlining volatile bytecodes
            if (failureMessage != 0)
            {
                *failureMessage = "volatile bytecodes should not be inlined";
            }
            return false;

        default:
            break;
    }

    //We can inline instruction if we get here
    return true;
}

/**
 * @brief Checks if we have a very simple method: empty, getter, setter, or single bytecode
 * @details For a method to be single bytecode (including getters and setters), the return
 * bytecode is not counted towards this. Namely a "single bytecode" method always has two
 * bytecodes when counting the return.
 * @param methodStats The method stats of the method we care about
 * @return Returns whether we have a very simple method
 */
static bool isVerySimpleMethod (CompilerMethodStats *methodStats)
{
    //If empty then it is very simple
    if ((methodStats->attributes & METHOD_IS_EMPTY) != 0)
    {
        return true;
    }

    //If getter/setter then it is very simple
    if ((methodStats->attributes & METHOD_IS_GETTER) != 0
            || (methodStats->attributes & METHOD_IS_SETTER) != 0)
    {
        return true;
    }

    //All methods must have return so we check if they have one additional bytecode
    if ((methodStats->attributes & METHOD_IS_LEAF) != 0 && methodStats->numBytecodes == 2)
    {
        return true;
    }

    //Not very simple
    return false;
}

#ifdef ARCH_IA32
/**
 * @brief Checks if method is throw-free, leaf, and fewer than 20 instructions.
 * @param methodStats The statics on method we are trying to inline
 * @return Returns kInliningNoError if method is a small throw free leaf. Otherwise
 * returns the inlining error message saying what the limitation is.
 */
static InliningFailure isSmallThrowFreeLeaf (CompilerMethodStats *methodStats)
{
    //Check if method is leaf
    bool methodIsLeaf = (methodStats->attributes & METHOD_IS_LEAF) != 0;

    if (methodIsLeaf == false)
    {
        //Reject non-leaf methods
        return kInliningCalleeNotLeaf;
    }

    //Check if method can throw
    bool methodThrowFree = (methodStats->attributes & METHOD_IS_THROW_FREE) != 0;

    if (methodThrowFree == false)
    {
        //Reject methods that can throw
        return kInliningCalleeMayThrow;
    }

    //Check number of bytecodes
    unsigned int numBytecodes = methodStats->numBytecodes;

    if (numBytecodes > gDvmJit.maximumInliningNumBytecodes)
    {
        //Reject if we have more than the allowed number of instructions
        return kInliningCalleeTooLarge;
    }

    return kInliningNoError;
}
#endif

/**
 * @brief Used to determine the register window shift required to uniquely name all VRs for multiple levels of nesting
 * @param calleeMethod The callee method that is being invoked
 * @param invokeNesting The nesting information from the invoke bytecode
 * @return Returns the register window shift required to support inlining callee method
 */
static int determineRegisterWindowShift (const Method *calleeMethod, const NestedMethod *invokeNesting)
{
    assert (calleeMethod != 0);

    //Basically the approach to computing the offset is
    // 1) Fist we look at method being inlined and count its registers and StackSaveArea
    // 2) Then we start walking through the invoke's nesting information to count additional levels of nesting
    int registerWindowShift = 0;

    do
    {
        //Include caller's stack save area in calculation of offset
        registerWindowShift += (sizeof(StackSaveArea) / sizeof(u4));

        //Now count callee's registers in offset calculation
        registerWindowShift += calleeMethod->registersSize;

        //We check if invoke has nesting information and that it actually is in the middle of nesting.
        if (invokeNesting != 0 && invokeNesting->parent != 0)
        {
            calleeMethod = invokeNesting->sourceMethod;

            //We want to prepare for looking at the next level
            invokeNesting = invokeNesting->parent;
        }
        else
        {
            //We don't have nesting so we have no other method to include in our calculation
            calleeMethod = 0;
        }
    } while (calleeMethod != 0);

    return registerWindowShift;
}

/**
 * @brief Used to check whether an invoke opcode is a range variant
 * @param opcode The opcode to check
 * @return Returns whether the opcode is a range invoke
 */
static inline bool isRangeInvoke (Opcode opcode)
{
    switch (opcode)
    {
        //Return true for all range invokes
        case OP_INVOKE_SUPER_RANGE:
        case OP_INVOKE_DIRECT_RANGE:
        case OP_INVOKE_STATIC_RANGE:
        case OP_INVOKE_SUPER_QUICK_RANGE:
        case OP_INVOKE_VIRTUAL_RANGE:
        case OP_INVOKE_VIRTUAL_QUICK_RANGE:
        case OP_INVOKE_INTERFACE_RANGE:
            return true;
        default:
            break;
    }

    //If we get here we do not have a range invoke
    return false;
}

/**
 * @brief Determines the mapping between caller's and callee's virtual registers.
 * @param invoke The decoded instruction for the invoke we are trying to inline.
 * @param invokedMethod Information about the method we are invoking.
 * @param calleeToCaller Updated by function to contain map of callee's virtual
 * registers to caller's virtual registers.
 */
static void determineRegisterMapping (DecodedInstruction &invoke,
                                      const Method *invokedMethod,
                                      std::map<int, int> &calleeToCaller)
{
   bool isRange = isRangeInvoke (invoke.opcode);

   //When setting up stack the ins follow the locals for the callee. So lets determine
   //the virtual register number of the first in.
   int firstIn = invokedMethod->registersSize - invokedMethod->insSize;

   //For invokes, vA holds the argument count
   unsigned int argumentCount = invoke.vA;

   //Now walk through the arguments of this invoke
   for (unsigned int rank = 0; rank < argumentCount; rank++)
   {
       //The callee ins are in order from the first in
       int calleeReg = firstIn + rank;

       //Now determine the virtual register used for current invoke parameter
       int callerReg;
       if (isRange == false)
       {
           //For non-range versions, the arguments are in order in the arg field
           callerReg = invoke.arg[rank];
       }
       else
       {
           //vC holds the first register to use
           callerReg = invoke.vC + rank;
       }

       //Add this entry to the map
       calleeToCaller[calleeReg] = callerReg;
   }
}

/**
 * @brief Used to check whether an opcode is a return.
 * @param opcode The opcode to check
 * @return Returns whether the opcode is a return
 */
static inline bool isReturn (Opcode opcode)
{
    switch (opcode)
    {
        //Return true for all return bytecodes
        case OP_RETURN:
        case OP_RETURN_OBJECT:
        case OP_RETURN_WIDE:
        case OP_RETURN_VOID:
        case OP_RETURN_VOID_BARRIER:
            return true;
        default:
            break;
    }

    //If we get here we do not have a return
    return false;
}

/**
 * @brief Used to check whether an opcode is a move-result.
 * @param opcode The opcode to check
 * @return Returns whether the opcode is a move-result
 */
static inline bool isMoveResult (Opcode opcode)
{
    switch (opcode)
    {
        //Return true for all move-result bytecodes
        case OP_MOVE_RESULT:
        case OP_MOVE_RESULT_OBJECT:
        case OP_MOVE_RESULT_WIDE:
            return true;
        default:
            break;
    }

    //If we get here we do not have a move-result
    return false;
}

/**
 * @brief Used to get the block that follows invoke. If move-result is provided, it gets block after that.
 * @details May cause block splits if invoke or move-result are in middle of block.
 * @param callerBasicBlocks The list of basic blocks of caller
 * @param invoke The invoke MIR
 * @param moveResult The move-result MIR
 * @return Returns the basic block which follows invoke.
 */
static BasicBlock *getBlockAfterInvoke (GrowableList &callerBasicBlocks, MIR *invoke, MIR *moveResult = 0)
{
    //Assume that the block after invoke is its fallthrough
    BasicBlock *afterInvokeBB = invoke->bb->fallThrough;

    //If invoke wasn't the last MIR of its block, we may need to split the block so we check now.
    if (invoke != invoke->bb->lastMIRInsn)
    {
        assert (invoke->next != 0);

        //If we don't have a move-result, then split after invoke
        if (moveResult == 0)
        {
            afterInvokeBB = dvmCompilerSplitBlock (&callerBasicBlocks, invoke->next, invoke->bb);
        }
        else
        {
            //If the move-result is the last instruction then we don't need to split, just take fallthrough
            if (moveResult == invoke->bb->lastMIRInsn)
            {
                afterInvokeBB = invoke->bb->fallThrough;
            }
            //If we do have a move-result then split after it
            else
            {
                assert (moveResult == invoke->next);
                assert (moveResult->next != 0);

                afterInvokeBB = dvmCompilerSplitBlock (&callerBasicBlocks, moveResult->next, invoke->bb);
            }
        }
    }
    else
    {
        //Invoke is the last MIR of its block so now check to see if we have a move-result
        if (moveResult != 0)
        {
            //If the move-result is not the last of its BB, then we must split
            if (moveResult != moveResult->bb->lastMIRInsn)
            {
                assert (moveResult->next != 0);

                afterInvokeBB = dvmCompilerSplitBlock (&callerBasicBlocks, moveResult->next, moveResult->bb);
            }
            else
            {
                //move-result is the last in its BB so the BB after invoke
                afterInvokeBB = moveResult->bb->fallThrough;
            }
        }
    }

    return afterInvokeBB;
}

/**
 * @brief Tries to remove the invoke and move-result.
 * @details If removal fails, it guarantees that the CFG will be in a valid state.
 * @param callerBasicBlocks The basic blocks of caller
 * @param invoke The invoke MIR to remove
 * @param moveResult The move-result MIR to remove
 * @return Returns inlining success/failure
 */
static InliningFailure removeInvokeAndMoveResult (GrowableList &callerBasicBlocks, MIR *invoke, MIR *moveResult)
{
    //Remember the BB which holds the invoke
    BasicBlock *invokeBB = invoke->bb;

    //Remember MIR previous to the invoke
    MIR *beforeInvoke = invoke->prev;

    //Try to remove the invoke
    bool removed = dvmCompilerRemoveMIR (invoke);

    //In assert world, removal should never fail because it means bad CFG
    assert (removed == true);

    //If we removed the invoke, we try to remove the move-result
    if (removed == true && moveResult != 0)
    {
        //Try to remove move-result
        removed = dvmCompilerRemoveMIR (moveResult);

        //In assert world, removal should never fail because it means bad CFG
        assert (removed == true);

        //If we failed the removal of the move-result, we must restore the invoke
        if (removed == false)
        {
            //We insert it back in its original location
            dvmCompilerInsertMIRAfter (invokeBB, beforeInvoke, invoke);
        }
    }

    //If we failed to remove the bytecodes, we reverted CFG but we still fail
    if (removed == false)
    {
        return kInliningMirRemovalFailed;
    }

#ifndef ARCH_IA32
    //Now we want to fix fallthrough branch because some backends need this information to be correct
    MIR *lastMir = invokeBB->lastMIRInsn;

    if (lastMir != 0)
    {
        int flags = dvmCompilerGetOpcodeFlags (lastMir->dalvikInsn.opcode);

        //If bytecode can continue, it may fall through in which case branch is needed
        if ((flags & kInstrCanContinue) != 0)
        {
            invokeBB->needFallThroughBranch = true;
        }
    }
#endif

    //Everything went well
    return kInliningNoError;
}

/**
 * @brief Used to create extended MIR that does prediction check.
 * @param invoke The virtual/interface invoke whose method call is being inlined
 * @return The newly created MIR for prediction check
 */
static MIR *createPredictionCheck (MIR *invoke)
{
    //Make a copy of the invoke. This includes copying the invoke nesting information.
    MIR *checkPrediction = dvmCompilerCopyMIR (invoke);

    //Get reference to decoded instruction so we can update it
    DecodedInstruction &newInstr = checkPrediction->dalvikInsn;

    //Update the Opcode of the invoke check
    newInstr.opcode = static_cast<Opcode> (kMirOpCheckInlinePrediction);

    //Put the "this" argument in vC
    newInstr.vC = invoke->dalvikInsn.vC;

    //Get the callsite info to use to find class
    CallsiteInfo *callsiteInfo = invoke->meta.callsiteInfo;

    /**
     * @warning Here we try to find named class using loader from meta information. Note that this
     * is only safe to call from compiler because it does not initiate an actual load. If all backends
     * could support pointer pools and had a section when the compiler thread would act as an interpreter,
     * then we could possibly allow class resolution. Please note that this code assumes that the
     * classLoader is a valid object that we still have a reference to.
     */
    ClassObject *clazz = dvmLookupClass (callsiteInfo->classDescriptor, callsiteInfo->classLoader, false);

    //Class has not been loaded and for now we avoid loading from compiler
    if (clazz == 0)
    {
        return 0;
    }

    //Get the class literal since there is a unique instance of the class
    assert (sizeof (clazz) == sizeof (u4));
    u4 clazzLiteral = reinterpret_cast<u4> (clazz);

    //Put the class literal in vB
    newInstr.vB = clazzLiteral;

    return checkPrediction;
}

/**
 * @brief Detaches the chaining cell associated with invoke and returns a pointer to it.
 * @param invokeBB The invoke BB to which the chaining cell should be attached to.
 * @param ccType The chaining cell type to look for in order to detach.
 * @return Returns the detached chaining cell if one is found.
 */
static BasicBlock *detachInvokeCC (BasicBlock *invokeBB, BBType ccType)
{
    //Find the chaining cell associated with invoke
    BasicBlock *chainingCell = 0;

    //Check whether we actually have one in the taken branch
    if (invokeBB->taken != 0 && invokeBB->taken->blockType == ccType)
    {
        chainingCell = invokeBB->taken;

        //Remove the CC from the invoke's BB taken so we can use it later
        dvmCompilerReplaceChildBasicBlock (0, invokeBB, kChildTypeTaken);
    }

    return chainingCell;
}

/**
 * @brief Sets up CFG being able to do prediction inlining by creating a devirtualization split.
 * @details From the invokeBB it creates two paths: one for the slow invoke (that itself falls through
 * to afterInvokeBB) and one that goes directly to afterInvokeBB. This split is done before the method
 * body is inlined.
 * @param callerBasicBlocks The basic blocks of caller method
 * @param invokeBB The basic block which holds invoke
 * @param invoke The invoke MIR
 * @param afterInvokeBB The basic block which follows invoke MIR
 * @param predictedCC The predicted chaining cell for virtual invoke
 * @param moveResult The move-result associated with invoke
 * @return Returns inlining success/failure
 */
static InliningFailure manipulateCFGForPrediction (GrowableList &callerBasicBlocks, BasicBlock *invokeBB, MIR *invoke,
        BasicBlock *afterInvokeBB, BasicBlock *predictedCC, MIR *moveResult)
{
    assert (invokeBB != 0 && afterInvokeBB != 0 && predictedCC != 0);

    //Remember MIR previous to the invoke
    MIR *beforeInvoke = invoke->prev;

    //Create prediction check first. The reason we do that is so that we can reject in case
    //creation of check is not successful.
    MIR *checkPrediction = createPredictionCheck (invoke);

    if (checkPrediction == 0)
    {
        return kInliningClassNotLoaded;
    }

    //Remove the invoke and move-result from their BBs
    InliningFailure removed = removeInvokeAndMoveResult (callerBasicBlocks, invoke, moveResult);

    if (removed != kInliningNoError)
    {
        //Removal failed so we just pass along error
        return removed;
    }

    //We insert our check in place of the invoke
    dvmCompilerInsertMIRAfter (invokeBB, beforeInvoke, checkPrediction);

    //In case of misprediction we need to actually do the invoke so create that BB right now
    BasicBlock *mispredictBB = dvmCompilerNewBBinList (callerBasicBlocks, kDalvikByteCode);

    //Add the invoke to the path for misprediction
    dvmCompilerAppendMIR (mispredictBB, invoke);
    invoke->OptimizationFlags |= MIR_INLINED_PRED;

    //Do the same for move-result as was done for invoke
    if (moveResult != 0)
    {
        dvmCompilerAppendMIR (mispredictBB, moveResult);
        moveResult->OptimizationFlags |= MIR_INLINED_PRED;
    }

    //Now make the taken of invokeBB be the path with invoke in case of misprediction
    dvmCompilerReplaceChildBasicBlock (mispredictBB, invokeBB, kChildTypeTaken);

    //In case prediction works correctly we need to go to inlined body but we let someone else do that.
    //We still want to guarantee that invokeBB's fallthrough is afterInvokeBB
    dvmCompilerReplaceChildBasicBlock (afterInvokeBB, invokeBB, kChildTypeFallthrough);

    //Now make the fallthrough of BB that holds the invoke have correct fallthrough
    dvmCompilerReplaceChildBasicBlock (afterInvokeBB, mispredictBB, kChildTypeFallthrough);

    if (predictedCC != 0)
    {
        //If we have a move-result, then in order to be able to attach predicted CC to invoke we need to split mispredictBB
        if (moveResult != 0)
        {
            //Split the mispredictBB so that moveResult ends up in second block
            dvmCompilerSplitBlock (&callerBasicBlocks, moveResult, mispredictBB);
        }

        //Move the predicted CC to correct place
        dvmCompilerReplaceChildBasicBlock (predictedCC, mispredictBB, kChildTypeTaken);
    }

    //Everything went well if we get here
    return kInliningNoError;
}

/**
 * @brief Given a type of return bytecode, finds the matching move-result.
 * @param returnOpcode The opcode of return bytecode
 * @return Returns the matching move-result. If return bytecode doesn't return a value,
 * OP_NOP is returned because no move-result is needed.
 */
static Opcode findMatchingMoveResult (Opcode returnOpcode)
{
    Opcode moveResultOpcode;

    switch (returnOpcode)
    {
        case OP_RETURN:
            moveResultOpcode = OP_MOVE_RESULT;
            break;
        case OP_RETURN_OBJECT:
            moveResultOpcode = OP_MOVE_RESULT_OBJECT;
            break;
        case OP_RETURN_WIDE:
            moveResultOpcode = OP_MOVE_RESULT_WIDE;
            break;
        default:
            moveResultOpcode = OP_NOP;
            break;
    }

    return moveResultOpcode;
}

/**
 * @brief Given a type of move-result bytecode, finds a matching move.
 * @param moveResultOpcode The opcode of move-result bytecode
 * @return Returns the matching move which reads from VR not from Thread.retval
 */
static Opcode findMatchingMove (Opcode moveResultOpcode)
{
    Opcode moveOpcode;

    switch (moveResultOpcode)
    {
        case OP_MOVE_RESULT:
            moveOpcode = OP_MOVE;
            break;
        case OP_MOVE_RESULT_OBJECT:
            moveOpcode = OP_MOVE_OBJECT;
            break;
        case OP_MOVE_RESULT_WIDE:
            moveOpcode = OP_MOVE_WIDE;
            break;
        default:
            moveOpcode = OP_NOP;
            break;
    }

    return moveOpcode;
}

/**
 * @brief Tags an MIR as being inlined
 * @param mir The MIR to tag
 * @param sourceMethod The source method for this MIR
 */
static inline void tagMirInlined (MIR *mir, const Method *sourceMethod)
{
    //Mark it as having been inlined which means we tag it with MIR_CALLEE flag.
    //The MIR_INLINED flag is not appropriate because that tag is solely for the invokes and move-results,
    //and that flag means that the bytecode should be treated as nop.
    mir->OptimizationFlags |= MIR_CALLEE;

    //Mark the parent method for this new MIR
    mir->meta.calleeMethod = sourceMethod;
}

/**
 * @brief Inserts the callee basic blocks between two others from caller.
 * @param callerBasicBlocks The basic blocks of caller
 * @param method The method that is being inlined
 * @param topBB Basic block from caller that will serve as entry into callee
 * @param bottomBB Basic block from caller that will server as exit from callee
 * @param calleeEntry The callee's entry basic block
 * @param calleeExit The callee's exit basic block
 * @param calleeBasicBlocks The list of callee basic blocks
 * @param invoke The invoke bytecode
 */
static void insertCalleeBetweenBasicBlocks (GrowableList &callerBasicBlocks, const Method *method, BasicBlock *topBB,
        BasicBlock *bottomBB, BasicBlock *calleeEntry, BasicBlock *calleeExit, GrowableList &calleeBasicBlocks,
        MIR *invoke)
{
    //In case the entry and exit are not bytecode blocks, we make them that now since we are inserting in middle of trace
    calleeEntry->blockType = kDalvikByteCode;
    calleeExit->blockType = kDalvikByteCode;

    //First make the fallthrough of callee's exit block be the invoke's fallthrough
    dvmCompilerReplaceChildBasicBlock (bottomBB, calleeExit, kChildTypeFallthrough);

    //Now replace the fallthough child of invokeBB to be the calleeEntry
    dvmCompilerReplaceChildBasicBlock (calleeEntry, topBB, kChildTypeFallthrough);

    //Now walk through the basic blocks
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit (&calleeBasicBlocks, &iterator);

    while (true)
    {
        //Get the basic block
        BasicBlock *bb = reinterpret_cast<BasicBlock *> (dvmGrowableListIteratorNext (&iterator));

        //When we reach the end we stop
        if (bb == 0)
        {
            break;
        }

        //Update its id to be unique for the cUnit
        bb->id = dvmGrowableListSize (&callerBasicBlocks);

        //Add it to the cUnit
        dvmInsertGrowableList (&callerBasicBlocks, (intptr_t) bb);

        //Update the method for this BB
        bb->containingMethod = method;

        //Walk through the MIRs so we can add information to them
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            tagMirInlined (mir, method);

            //Here we check if the inlined MIR has same source method and offset as the invoke.
            //The reason we do this check is that inlining has an optimization for methods that are one bytecode.
            //It does the inlining by rewriting the single MIR and then setting the PC of bytecode to that of invoke.
            //If we find that case, we don't update nesting information because all registers it uses are caller
            //registers and not callee registers.
            if (mir->nesting.sourceMethod != invoke->nesting.sourceMethod || mir->offset != invoke->offset)
            {
                //Set the nesting chain of invoke as parent of current MIR
                mir->nesting.parent = &(invoke->nesting);
            }
        }
    }
}

/**
 * @brief Updates the caller's CFG so that callee's CFG is integrated.
 * @param callerBasicBlocks List of caller's blocks
 * @param method The method being inlined
 * @param invoke The invoke of method being inlined
 * @param moveResult The move-result matching invoke (may be null)
 * @param calleeEntry The callee's entry block (cannot be null)
 * @param calleeExit The callee's exit block (cannot be null)
 * @param calleeBasicBlocks List of callee blocks
 * @param isPredicted Whether method being invoked is predicted
 * @return Returns inlining success/failure
 */
static InliningFailure insertMethodBodyIntoCFG (GrowableList &callerBasicBlocks, const Method *method, MIR *invoke,
        MIR *moveResult, BasicBlock *calleeEntry, BasicBlock *calleeExit, GrowableList &calleeBasicBlocks,
        bool isPredicted)
{
    //We need to have entry and exit for callee
    assert (calleeEntry != 0 && calleeExit != 0);

    //Save invoke's BB
    BasicBlock *invokeBB = invoke->bb;

    //Find the predicted chaining cell possibly associated with invoke
    BasicBlock *predictedCC = 0;

    if (isPredicted == true)
    {
        predictedCC = detachInvokeCC (invokeBB, kChainingCellInvokePredicted);
    }

    //Find the singleton CC possibly associated with invoke
    BasicBlock *singletonCC = detachInvokeCC (invokeBB, kChainingCellInvokeSingleton);

    //We need to insert between invoke's BB and BB after invoke
    //Determine the block following invoke's BB
    BasicBlock *afterInvokeBB = getBlockAfterInvoke (callerBasicBlocks, invoke, moveResult);

    //If we fail we need to reattach chaining cells. But we assume first that we won't have issues.
    InliningFailure trackProblem = kInliningNoError;

    if (afterInvokeBB == 0)
    {
        trackProblem = kInliningInvokeBBNoChild;
    }
    else
    {
        if (isPredicted == true)
        {
            //Manipulate CFG for prediction mechanism
            trackProblem = manipulateCFGForPrediction (callerBasicBlocks, invokeBB, invoke, afterInvokeBB,
                    predictedCC, moveResult);
        }
        else
        {
            //We remove the invoke and move-result if we don't have prediction
            trackProblem = removeInvokeAndMoveResult (calleeBasicBlocks, invoke, moveResult);
        }
    }

    //We need to fail if we had any errors
    if (trackProblem != kInliningNoError)
    {
        //Since we will fail inlining, we must re-attach predicted CC to invoke
        if (predictedCC != 0)
        {
            dvmCompilerReplaceChildBasicBlock (predictedCC, invokeBB, kChildTypeTaken);
        }
        //We must re-attach singleton CC since we fail inlining
        else if (singletonCC != 0)
        {
            dvmCompilerReplaceChildBasicBlock (singletonCC, invokeBB, kChildTypeTaken);
        }

        return trackProblem;
    }

    //Now we insert the callee CFG between the two blocks we have decided on
    insertCalleeBetweenBasicBlocks (callerBasicBlocks, method, invokeBB, afterInvokeBB, calleeEntry, calleeExit,
            calleeBasicBlocks, invoke);

    //If we had a singleton chaining cell with the invoke then we need to remove it right now since we got rid of the invoke
    if (singletonCC != 0)
    {
        dvmCompilerHideBasicBlock (callerBasicBlocks, singletonCC);
    }

    //If we make it here, everything went okay
    return kInliningNoError;
}

/**
 * @brief Looks through the CFG for the move-result that follows invoke.
 * @param invoke The MIR for the invoke.
 * @return Returns pointer to MIR representing the move-result. Returns 0 if none is found.
 */
static MIR *findMoveResult (const MIR *invoke)
{
    //We must have an invoke
    assert(invoke != 0 && invoke->bb != 0);

    //Try getting the next because move-result should follow invoke
    MIR *afterInvoke = invoke->next;

    //If we do not have a next MIR, then try to look for it in successor block
    if (afterInvoke == 0 && invoke->bb->fallThrough != 0)
    {
        //If move-result exists, it must be the first mir of fallthrough block
        afterInvoke = invoke->bb->fallThrough->firstMIRInsn;
    }

    //If we did not find a MIR, then return 0
    if (afterInvoke == 0)
    {
        return 0;
    }

    //Check whether we actually have a move-result
    if (isMoveResult (afterInvoke->dalvikInsn.opcode) == false)
    {
        return 0;
    }

    //Return the move-result
    return afterInvoke;
}

/**
 * @brief Used to find the return instruction of a basic block.
 * @param blockList The complete list of basic blocks
 * @param exit The single basic block that represents exit
 * @return Returns the MIR if a return bytecode is found.
 */
static MIR *findReturn (GrowableList &blockList, const BasicBlock *exit)
{
    //Paranoid
    assert (exit != 0);

    const BasicBlock *bbToSearch = exit;

    //If the exit block has no MIRs, we search its predecessor
    if (exit->lastMIRInsn == 0)
    {
        assert (exit->firstMIRInsn == 0);

        //If we have no predecessors then we cannot find any return. Also if we have more than one
        //predecessor we may have multiple returns and we don't know which one to return;
        if (dvmCountSetBits (exit->predecessors) != 1)
        {
            return 0;
        }

        //Get the basic block that is predecessor of exit block
        int blockIdx = dvmHighestBitSet (exit->predecessors);
        bbToSearch = reinterpret_cast<const BasicBlock *> (dvmGrowableListGetElement (&blockList, blockIdx));

        assert (bbToSearch != 0);
    }

    MIR *returnMir = 0;

    //If the BB has MIRs, the last one must be a return
    MIR *lastMir = bbToSearch->lastMIRInsn;
    if (lastMir != 0)
    {
        //Check to make sure the opcode is return
        if (isReturn (lastMir->dalvikInsn.opcode) == true)
        {
            returnMir = lastMir;
        }
    }

    //Return the mir if we found it
    return returnMir;
}

/**
 * @brief For one bytecode short methods, this is used to rewrite the virtual registers.
 * @param newMir The MIR that we are inlining
 * @param moveResult The MIR for the move-result.
 * @param returnMir The MIR for the return
 * @param calleeToCaller Map of virtual registers of callee to that of caller
 * @param calleeLocalsCount Number of locals in callee
 * @return Returns inlining success/failure
 */
static InliningFailure rewriteSingleInlinedMIR (MIR *newMir, const MIR *moveResult, const MIR *returnMir,
        const std::map<int, int> &calleeToCaller, const int calleeLocalsCount)
{
    //We need a local copy to ensure the original map is not changed
    std::map<int, int> calleeToCallerLocal (calleeToCaller);

    //Copy the decoded instruction
    DecodedInstruction newInsn = newMir->dalvikInsn;

    //If we have a move-result then something is being returned by function
    if (moveResult != 0)
    {
        //Get dataflow flags
        long long newMirFlags = dvmCompilerDataFlowAttributes[newInsn.opcode];

        //Make sure that return has a use in vA and move-result has define in vA
        assert ((dvmCompilerDataFlowAttributes[returnMir->dalvikInsn.opcode] & DF_A_IS_USED_REG) != 0);
        assert ((dvmCompilerDataFlowAttributes[moveResult->dalvikInsn.opcode] & DF_A_IS_DEFINED_REG) != 0);

        //Check to make sure that our new MIR has a define because we have a move-result
        if ((newMirFlags & DF_A_IS_DEFINED_REG) == 0)
        {
            //TODO Since we have no define but we have a move-result and a return, it must be the case that
            //caller passed argument to callee that it is returning. So what we need to do for this case
            //is generate a move from register caller passed to register that move-result is moving to.
            //However, for now we reject.
            return kInliningNoDefButMoveResult;
        }
        else
        {
            //We have a define but let's make sure that return VR matches our define
            if (returnMir->dalvikInsn.vA != newInsn.vA)
            {
                return kInliningDefNoMatchReturn;
            }

            //We write directly into register desired by move-result
            newInsn.vA = moveResult->dalvikInsn.vA;

            //Keep renaming for def
            calleeToCallerLocal[returnMir->dalvikInsn.vA] = moveResult->dalvikInsn.vA;
            if ((newMirFlags & DF_DA_WIDE) != 0)
            {
                calleeToCallerLocal[returnMir->dalvikInsn.vA + 1] = moveResult->dalvikInsn.vA + 1;
            }
        }
    }

    //Before rewriting we must ensure that all callee's VRs have corresponding caller VR
    //Otherwise our inlined MIR will corrupt VR from caller
    //We need to check only locals because other were filled in creation of map
    for (int i = 0; i < calleeLocalsCount; i++)
    {
        if (calleeToCallerLocal.find (i) == calleeToCallerLocal.end ())
        {
            return kInliningUnmatchedLocals;
        }
    }

    //Try to rewrite the uses now
    if (dvmCompilerRewriteMirVRs (newInsn, calleeToCaller, true) == false)
    {
        return kInliningRewriteFailed;
    }

    //When we get here everything went well so copy back the modified dalvik instruction
    newMir->dalvikInsn = newInsn;

    //We finished initializing the new MIR
    return kInliningNoError;
}

/**
 * @brief Used to locate and rewrite the single MIR in a very simple method
 * @param calleeBasicBlocks The basic blocks of callee
 * @param calleeToCaller The map of registers from callee to caller
 * @param invoke The invoke whose method we are inlining
 * @param returnMir The return bytecode of the callee method
 * @param moveResult The move-result following invoke
 * @param calleeLocalsCount Number of locals in callee
 * @return Returns inlining success/failure depending on whether rewriting went well
 */
static InliningFailure locateAndRewriteSingleMIR (GrowableList &calleeBasicBlocks,
        const std::map<int, int> &calleeToCaller, const MIR *invoke, const MIR *returnMir,
        const MIR *moveResult, const int calleeLocalsCount)
{
    MIR *mirToInline = 0;

    //Search the callee CFG for the MIR to rewrite
    GrowableListIterator calleeIter;
    dvmGrowableListIteratorInit (&calleeBasicBlocks, &calleeIter);

    while (true)
    {
        //Get the basic block
        BasicBlock *bb = reinterpret_cast<BasicBlock *> (dvmGrowableListIteratorNext (&calleeIter));

        //When we reach the end we stop
        if (bb == 0)
        {
            break;
        }

        //We found a bytecode
        if (bb->firstMIRInsn != 0)
        {
            if (mirToInline != 0)
            {
                return kInliningMoreThanOneBytecode;
            }

            mirToInline = bb->firstMIRInsn;

            if (mirToInline->next != 0)
            {
                return kInliningMoreThanOneBytecode;
            }
        }
    }

    if (mirToInline != 0)
    {
        //Now we need to rewrite the VRs for the MIR we wish to inline
        InliningFailure rewriting  = rewriteSingleInlinedMIR (mirToInline, moveResult, returnMir, calleeToCaller, calleeLocalsCount);

        if (rewriting != kInliningNoError)
        {
            //If we fail, then don't inline
            return rewriting;
        }

        //If the instruction is about to raise any exception, we want to punt to the interpreter
        //and re-execute the invoke. Thus we set the newMir's offset to match the invoke's offset.
        mirToInline->offset = invoke->offset;

        //Make sure that the new MIR has same nesting information as the invoke
        mirToInline->nesting = invoke->nesting;
    }

    //If we made it here we have no error
    return kInliningNoError;
}

/**
 * @brief Used to create MIR that does the stack overflow check
 * @param inlinedInvoke The invoke that was inlined for which we need to check stack overflow at entry
 * @param inlinedMethod The method that was inlined
 * @return Returns the MIR which has extended opcode kMirOpCheckStackOverflow
 */
static MIR *createStackOverflowCheck (const MIR *inlinedInvoke, const Method *inlinedMethod)
{
    MIR *stackOverflowCheck = dvmCompilerNewMIR();

    stackOverflowCheck->dalvikInsn.opcode = static_cast<Opcode> (kMirOpCheckStackOverflow);

    //We can determine required register window shift for this invoke and method it calls
    int registerWindowShift = determineRegisterWindowShift (inlinedMethod, &inlinedInvoke->nesting);

    //Basically to compute the space require we need to multiply the register window shift by sizeof (u4)
    //and then add a StackSaveArea space which is required for callee
    unsigned int stackSpaceRequired = registerWindowShift * sizeof (u4) + sizeof (StackSaveArea);

    //Store the stack space required in vB
    stackOverflowCheck->dalvikInsn.vB = stackSpaceRequired;

    //Now copy the offset and the nesting information
    stackOverflowCheck->offset = inlinedInvoke->offset;
    stackOverflowCheck->nesting = inlinedInvoke->nesting;

    return stackOverflowCheck;
}

/**
 * @brief Used to perform a register window shift by selectively rewriting MIRs from the CFG
 * @param callerBasicBlocks The basic blocks of caller after callee has merged
 * @param calleeMethod The callee method we are inlining
 * @param moveResult The caller's move result matched with callee invoke
 * @param updatedMoveResult Updated by the function to true if the moveResult mir is rewritten
 * @param renameCallee Whether callee should be renamed. If false, caller registers are renamed.
 * @param renameOffset The offset to use for renaming virtual registers.
 * @param oldToNew Map of register names to the new number they should be renamed to
 * @return Returns inlining success/failure depending on whether the renaming is successful
 */
static InliningFailure handleRenamingAfterShift (GrowableList &callerBasicBlocks, const Method *calleeMethod,
        const MIR *moveResult, bool &updatedMoveResult, const bool renameCallee, const int renameOffset,
        const std::map<int, int> &oldToNew)
{
    GrowableListIterator blockIter;
    dvmGrowableListIteratorInit (&callerBasicBlocks, &blockIter);

    while (true)
    {
        BasicBlock *bb = reinterpret_cast<BasicBlock *> (dvmGrowableListIteratorNext (&blockIter));

        //Bail at the end
        if (bb == 0)
        {
            break;
        }

        //Walk through the MIRs so we can rewrite them
        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //Depending on the rewriting logic which has happened, we need to modify either
            //MIRs from the callee method or all of the other MIRs.
            // 1) So if renameCallee is false, then we only need to rewrite if the MIR doesn't
            //come from callee method.
            // 2) If renameCalleee is true, then we only rewrite the MIR if it comes from callee method.
            if ((renameCallee == false && mir->nesting.sourceMethod != calleeMethod)
                    || (renameCallee == true && mir->nesting.sourceMethod == calleeMethod))
            {
                bool rewritten = dvmCompilerRewriteMirVRs (mir->dalvikInsn, oldToNew, false);

                if (rewritten == false)
                {
                    //We have already updated several MIRs and thus we cannot revert the damage
                    //we have done.
                    return kInliningUnrecoverableRewrite;
                }

                //We want to keep track of the renaming offset. However, this is only useful for MIRs
                //that come from a method and are not artificially generated.
                if (mir->nesting.sourceMethod != 0)
                {
                    mir->virtualRegRenameOffset += renameOffset;
                }

                //Invalidate the SSA representation in case someone prints out the CFG during inlining
                mir->ssaRep = 0;

                if (mir == moveResult)
                {
                    updatedMoveResult = true;
                }
            }
        }
    }

    return kInliningNoError;
}

/**
 * @brief Used to generate and insert the MIRs for doing caller to callee moves to replace argument
 * passing of the invoke.
 * @param calleeToCaller Map of virtual registers of callee to that of caller
 * @param calleeEntry The entry block into the callee body
 * @param renameCallee Whether callee should be renamed. If false, caller registers are renamed.
 * @param renameOffset The offset to use for renaming virtual registers.
 */
static void insertCallerToCalleeMoves (const std::map<int, int> &calleeToCaller, BasicBlock *calleeEntry,
        const bool renameCallee, const int renameOffset)
{
    //Go through the callee to caller mapping in order to insert moves
    for (std::map<int, int>::const_reverse_iterator regIter = calleeToCaller.rbegin ();
            regIter != calleeToCaller.rend (); regIter++)
    {
        //Get the original source and destination VRs
        int sourceVR = regIter->second;
        int destVR = regIter->first;

        //Depending on how renaming was done, we need to include the renaming offset
        if (renameCallee == true)
        {
            destVR += renameOffset;
        }
        else
        {
            sourceVR += renameOffset;
        }

        //Create the move itself.
        MIR *moveMir = dvmCompilerNewMoveMir (sourceVR, destVR, false);

        //Now add the move MIR to the callee entry block
        dvmCompilerPrependMIR (calleeEntry, moveMir);
    }
}

/**
 * @brief Used to generate and insert the MIR for doing callee to caller move to replace return and move-result.
 * @param calleeExit The callee method's exit block
 * @param moveResult The caller's move result matched with callee invoke
 * @param returnMir The return bytecode from callee
 * @param updatedMoveResult Whether the move-result has been renamed already
 * @param renameCallee Whether callee should be renamed. If false, caller registers are renamed.
 * @param renameOffset The offset to use for renaming virtual registers.
 */
static void insertCalleeToCallerMove (BasicBlock *calleeExit, const MIR *moveResult, const MIR *returnMir,
        const bool updatedMoveResult, const bool renameCallee, const int renameOffset)
{
    //When we have a move-result we surely have a return
    assert (returnMir != 0);

    //Make sure that return has a use in vA and move-result has define in vA
    assert ((dvmCompilerDataFlowAttributes[returnMir->dalvikInsn.opcode] & DF_A_IS_USED_REG) != 0);
    assert ((dvmCompilerDataFlowAttributes[moveResult->dalvikInsn.opcode] & DF_A_IS_DEFINED_REG) != 0);

    int sourceVR = returnMir->dalvikInsn.vA;
    int destVR = moveResult->dalvikInsn.vA;

    //Depending on how renaming was done, we need to include the renaming offset
    if (renameCallee == false)
    {
        if (updatedMoveResult == false)
        {
            destVR += renameOffset;
        }
    }
    else
    {
        sourceVR += renameOffset;
    }

    //Get the opcode we need for the move
    Opcode moveOpcode = findMatchingMove (moveResult->dalvikInsn.opcode);

    //Create the move itself.
    MIR *moveMir = dvmCompilerNewMoveMir (sourceVR, destVR, moveOpcode == OP_MOVE_WIDE ? true : false);

    //Now add the move MIR to the callee exit block.
    dvmCompilerAppendMIR (calleeExit, moveMir);
}

/**
 * @brief Used to shift the register window of the cUnit and create the moves from callee to caller regs and vice versa
 * @param cUnit The compilation unit
 * @param callerBasicBlocks The list of basic blocks for caller
 * @param calleeToCaller Map of virtual registers of callee to that of caller
 * @param calleeMethod The method that was inlined
 * @param calleeEntry The entry block into the callee body
 * @param calleeExit The exit block from the callee body
 * @param invoke The invoke whose method was inlined
 * @param moveResult The move-result following invoke
 * @param returnMir The return bytecode of callee
 * @return Returns inlining success/failure
 */
static InliningFailure shiftRegisterWindow (CompilationUnit *cUnit, GrowableList &callerBasicBlocks,
        const std::map<int, int> &calleeToCaller, const Method *calleeMethod, BasicBlock *calleeEntry,
        BasicBlock *calleeExit, const MIR *invoke, const MIR *moveResult, const MIR *returnMir)
{

    std::map<int, int> oldToNew;

    //If false we rename all VRs in cUnit excluding callee. Otherwise we update only callee.
    bool needRenaming = true;
    bool renameCallee = false;
    int renameOffset = 0;

    //Determine the register window shift
    int registerWindowShift = determineRegisterWindowShift (calleeMethod, &invoke->nesting);

    //From this point we need to handle 3 possible different cases where the register window
    //shift of the cUnit needs to be synchronized with what we're currently inlining.
    // 1) cUnit->registerWindowShift == registerWindowShift
    //     - This case can happen when we're inlining a method that has the same shift
    //     as a method already inlined. This means that both callee methods use the same number
    //     of virtual registers and thus we have already shifted the window of registers in the
    //     caller method.
    // 2) cUnit->registerWindowShift < registerWindowShift
    //     - This case happens when we're inlining a method with more virtual registers than one
    //     we've already inlined or if this is the first not very simple method we've inlined.
    //     This means that we need to rename registers in caller so that we can accommodate a frame
    //     pointer shift so that callee's virtual registers stay unchanged.
    // 3) cUnit->registerWindowShift > registerWindowShift
    //     - This case happens when we're inlining a method with fewer virtual registers that
    //     method we've already inlined. All of the virtual registers in the cUnit are relative
    //     the shift we've already done. This means that we need to rename callee's registers
    //     so that they are relative to the frame pointer which has already been shifted. Caller's
    //     registers and all other inlined registers remain unchanged.

    //Check if we have already done a register window shift by the same amount
    if (cUnit->registerWindowShift == registerWindowShift)
    {
        //We don't need to do any renaming but we still need to generate all support instructions
        needRenaming = false;
    }
    else if (cUnit->registerWindowShift < registerWindowShift)
    {
        //In this situation we need to reshift by a greater amount so we rename everything but callee
        renameCallee = false;

        renameOffset = registerWindowShift - cUnit->registerWindowShift;

        for (int reg = 0; reg < cUnit->numDalvikRegisters; reg++)
        {
            int newRegName = reg + renameOffset;

            //We cannot exceed a register name greater than 16-bits due to how SSA is represented
            if (newRegName >= (1 << 16))
            {
                return kInliningVirtualRegNumberTooLarge;
            }

            oldToNew[reg] = newRegName;
        }

        dvmCompilerUpdateCUnitNumDalvikRegisters (cUnit, cUnit->numDalvikRegisters + renameOffset);
        cUnit->registerWindowShift = registerWindowShift;
    }
    else if (cUnit->registerWindowShift > registerWindowShift)
    {
        //In this situation the shift is greater than we expect so we need to rename only callee
        renameCallee = true;

        //As noted in approach above, we do this because the cUnit already had a bigger shift done
        //so we need to synchronize all callee registers to match this shift already done.
        renameOffset = cUnit->registerWindowShift - registerWindowShift;

        for (int reg = 0; reg < calleeMethod->registersSize; reg++)
        {
            oldToNew[reg] = reg + renameOffset;
        }
    }

    bool updatedMoveResult = false;

    //Before we start renaming we check if actually needs to be done
    if (needRenaming == true)
    {
        InliningFailure renaming = handleRenamingAfterShift (callerBasicBlocks, calleeMethod, moveResult,
                updatedMoveResult, renameCallee, renameOffset, oldToNew);

        //If we had an error during renaming, simply pass it along
        if (renaming != kInliningNoError)
        {
            return renaming;
        }
    }

    //Now create and insert the caller to callee moves
    insertCallerToCalleeMoves (calleeToCaller, calleeEntry, renameCallee, renameOffset);

    //Now insert the stack overflow check
    MIR *stackOverflowCheck = createStackOverflowCheck (invoke, calleeMethod);

    //Prepend it to the callee entry basic block
    dvmCompilerPrependMIR (calleeEntry, stackOverflowCheck);

    //If we have a move-result it means we are returning a value so create that move right now
    if (moveResult != 0)
    {
        insertCalleeToCallerMove (calleeExit, moveResult, returnMir, updatedMoveResult, renameCallee, renameOffset);
    }

    //If we made it here we successfully did register window shifting
    return kInliningNoError;
}

/**
 * @brief Performs the inlining work
 * @param cUnit The compilation unit
 * @param calleeMethod The method being invoked.
 * @param invoke The MIR for the invoke
 * @param isPredicted Whether method being invoked is predicted
 * @param isVerySimple The inlined method only has one bytecode to inline
 * @param complexCaseSupported whether complex case is supported
 * @return Returns inlining success/failure
 */
static InliningFailure doInline (CompilationUnit *cUnit, const Method *calleeMethod, MIR *invoke,
        bool isPredicted, bool isVerySimple, InliningFailure complexCaseSupported)
{
    //Keep track of BBs of callee
    BasicBlock *calleeEntry = 0, *calleeExit = 0;

    //Get the caller basic blocks
    GrowableList &callerBasicBlocks = cUnit->blockList;

    //We expect that we have at least 3 basic blocks: one for entry, one for exit, and one or more for body of method
    const unsigned int fewestExpectedBlocks = 3;

    //We set up a growable list that can be used to insert the blocks
    GrowableList calleeBasicBlocks;
    dvmInitGrowableList (&calleeBasicBlocks, fewestExpectedBlocks);

    //We create the CFG for the method
    bool didCreateCfg = dvmCompilerBuildCFG (calleeMethod, &calleeBasicBlocks, &calleeEntry, &calleeExit, 0,
            canInlineBytecode);

    if (didCreateCfg == false)
    {
        //We failed to build CFG so we return false. We failed because our "canInlineBytecode"
        //filter found an unsupported case.
        return kInliningUnsupportedBytecodes;
    }

    //If we do not have the entry and exit for the callee method CFG, we cannot inline
    if (calleeEntry == 0 || calleeExit == 0 || calleeEntry == calleeExit || calleeEntry->fallThrough == 0)
    {
        return kInliningBadCalleeCFG;
    }

    //Currently we do not support methods with loops
    if (dvmCompilerDoesContainLoop (calleeBasicBlocks, calleeEntry) == true)
    {
        return kInliningCalleeHasLoops;
    }

    MIR *returnMir = findReturn (calleeBasicBlocks, calleeExit);

    //If we do not have a return, we cannot inline
    if (returnMir == 0)
    {
        return kInliningCannotFindReturn;
    }

    //Look to see if we need to find a move-result
    Opcode desiredMoveResult = findMatchingMoveResult (returnMir->dalvikInsn.opcode);

    MIR *moveResult = 0;

    if (desiredMoveResult != OP_NOP)
    {
        moveResult = findMoveResult (invoke);

        //If we do not find a move-result and we want one, then reject inlining
        if (moveResult == 0)
        {
            return kInliningCannotFindMoveResult;
        }

        //If we do find a move-result but it doesn't match our return, then reject inlining
        if (moveResult->dalvikInsn.opcode != desiredMoveResult)
        {
            return kInliningMoveResultNoMatchReturn;
        }
    }

    //We are getting ready to inline, so remove the return bytecode because it is no longer needed
    if (dvmCompilerRemoveMIR (returnMir) == false)
    {
        //We failed to remove return. Inlining won't go well.
        return kInliningMirRemovalFailed;
    }

    //Determine the mapping between callee's ins to caller's outs
    std::map<int, int> calleeToCaller;
    determineRegisterMapping (invoke->dalvikInsn, calleeMethod, calleeToCaller);

    //If method is very simple we won't be creating a callee frame so simply rewrite MIR so that it
    //it is using caller's virtual registers.
    if (isVerySimple == true)
    {
        InliningFailure rewritten = locateAndRewriteSingleMIR (calleeBasicBlocks, calleeToCaller, invoke, returnMir,
                moveResult, calleeMethod->registersSize - calleeMethod->insSize);

#ifdef ARCH_IA32
        //Only x86 has register window shift implementation so we don't inline non-simple methods for anyone else
        if (rewritten == kInliningUnmatchedLocals)
        {
            //Fallback to non-simple inline, we did not change anything so it is safe if it is supported
            if (complexCaseSupported == kInliningSuccess)
            {
                rewritten = kInliningNoError;
                isVerySimple = false;
            }
        }
#endif
        if (rewritten != kInliningNoError)
        {
            return rewritten;
        }
    }

    //Try to insert the method's body into the CFG
    InliningFailure inlined = insertMethodBodyIntoCFG (callerBasicBlocks, calleeMethod, invoke, moveResult, calleeEntry,
            calleeExit, calleeBasicBlocks, isPredicted);

    //If we inlined and we don't have a very simple case, then we need to do some general virtual register renaming
    //by shifting the cUnit's register window
    if (isVerySimple == false && inlined == kInliningNoError)
    {
        inlined = shiftRegisterWindow (cUnit, callerBasicBlocks, calleeToCaller, calleeMethod, calleeEntry, calleeExit, invoke,
                moveResult, returnMir);
    }

    //Return the result of the inlining
    return inlined;
}

/**
 * @brief Given a method, it tries to inline it.
 * @param cUnit The compilation unit
 * @param calleeMethod The method to inline
 * @param invoke The MIR for the invoke
 * @param isPredicted Whether the desired inlining is of a predicted method.
 * @return Returns inlining success/failure
 */
static InliningFailure tryInline (CompilationUnit *cUnit, const Method *calleeMethod, MIR *invoke,
        bool isPredicted)
{
    //Paranoid
    assert (calleeMethod != 0 && invoke != 0 && invoke->bb != 0);

    //Check that we do not have a native method
    if (dvmIsNativeMethod (calleeMethod) == true)
    {
        return kInliningNativeMethod;
    }

    //Get backend checker whether extended MIR is supported
    bool (*backendSupportsExtended) (int) = gDvmJit.jitFramework.backendSupportExtendedOp;

    //If we have predicted invoke, check whether backend supports devirtualization check
    if (backendSupportsExtended == 0 ||
            (isPredicted == true && backendSupportsExtended != 0
                    && backendSupportsExtended (kMirOpCheckInlinePrediction) == false))
    {
        return kInliningNoBackendExtendedOpSupport;
    }

    //Analyze the body of the method
    CompilerMethodStats *methodStats = dvmCompilerAnalyzeMethodBody (calleeMethod, true);

    //Assume we will not succeed inlining
    InliningFailure inlined = kInliningMethodComplicated;

    if ((methodStats->attributes & METHOD_CANNOT_INLINE) != 0)
    {
        inlined = kInliningFailedBefore;
    }
    else
    {
        //Now we should detect whether we will be able to use non very simple inline
        //to let doInline know whether it can fallback to more complex case
        InliningFailure complexCaseSupported = kInliningMethodComplicated;

#ifdef ARCH_IA32
        //Only x86 has register window shift implementation so we don't inline small methods for anyone else
        complexCaseSupported = isSmallThrowFreeLeaf (methodStats);
        if (complexCaseSupported == kInliningNoError)
        {
            //For methods that are not very simple, we need to make sure we don't overflow
            if (backendSupportsExtended == 0 || backendSupportsExtended (kMirOpCheckStackOverflow) == false)
            {
                complexCaseSupported = kInliningNoBackendExtendedOpSupport;
            }
            else
            {
                complexCaseSupported = kInliningSuccess;
            }
        }
#endif
        const bool isVerySimple = isVerySimpleMethod (methodStats);
        if (isVerySimple == true || complexCaseSupported == kInliningSuccess)
        {
            inlined = doInline (cUnit, calleeMethod, invoke, isPredicted, isVerySimple, complexCaseSupported);
        }
        else
        {
            inlined = complexCaseSupported;
        }
    }

    //We may have inserted basic blocks so update cUnit's value right now
    cUnit->numBlocks = dvmGrowableListSize (&cUnit->blockList);

    //If we have inlined then we may have added new basic blocks so calculate predecessors
    if (inlined == kInliningSuccess)
    {
        dvmCompilerCalculatePredecessors (cUnit);

#if defined(WITH_JIT_TUNING)
        //When we are trying to tune JIT, keep track of how many getters/setters we inlined
        if ((methodStats->attributes & METHOD_IS_GETTER) != 0)
        {
            if (isPredicted == true)
            {
                gDvmJit.invokePolyGetterInlined++;
            }
            else
            {
                gDvmJit.invokeMonoGetterInlined++;
            }

        }
        else if ((methodStats->attributes & METHOD_IS_SETTER) != 0)
        {
            if (isPredicted == true)
            {
                gDvmJit.invokePolySetterInlined++;
            }
            else
            {
                gDvmJit.invokeMonoSetterInlined++;
            }
        }
#endif
    }
    //If we failed to inline then remember it so we don't retry in future
    else
    {
        methodStats->attributes = methodStats->attributes & METHOD_CANNOT_INLINE;
    }

    //Return the result of the inlining
    return inlined;
}

/**
 * @brief Given a MIR, it checks if it is an inlinable invoke and then tries to inline it.
 * @param cUnit The compilation unit
 * @param info The description of the trace
 * @param bb The parent BB of the bytecode
 * @param invoke The invoke to try to inline
 * @return Returns whether inlining is successful.
 */
static bool handleInlining (CompilationUnit *cUnit, JitTranslationInfo *info, BasicBlock *bb, MIR *invoke)
{
    //Get the opcode
    Opcode opcode = invoke->dalvikInsn.opcode;

    //Paranoid
    assert (invoke != 0);
    assert ((static_cast<int> (dvmCompilerGetOpcodeFlags (opcode)) & kInstrInvoke) != 0);

    //Figure out early if we need to make a prediction for the inlining.
    //We would need to make a prediction in case of a polymorphic call.
    bool isPredicted = dvmCompilerDoesInvokeNeedPrediction (opcode);

    //Assume we will successfully inline
    InliningFailure inlined = kInliningSuccess;

    //Initially we don't know what we're invoking until we figure it out
    const Method *calleeMethod = 0;

    //Check first if method inlining is disabled
    if ((gDvmJit.disableOpt & (1 << kMethodInlining)) != 0)
    {
        inlined = kInliningDisabled;
    }
    //Then check if we need to make prediction but predicted inlining is disabled
    else if ((gDvmJit.disableOpt & (1 << kPredictedMethodInlining)) != 0 && isPredicted == true)
    {
        inlined = kInliningDisabled;
    }
    //Disable inlining when doing method tracing
    else if (gDvmJit.methodTraceSupport)
    {
        inlined = kInliningMethodTraceEnabled;
    }
    //If the invoke itself is selected for single stepping, don't bother to inline it.
    else if (SINGLE_STEP_OP (opcode))
    {
        inlined = kInliningSingleStepInvoke;
    }
    //The given BB should match what the invoke believes is its parent
    else if (invoke->bb != bb)
    {
       inlined = kInliningInvokeBBProblem;
    }
    else if (invoke->nesting.parent != 0 || (invoke->OptimizationFlags & MIR_CALLEE) != 0)
    {
        //For now we only accept one level of inlining so do not accept invokes that come from callee methods
        inlined = kInliningNestedInlining;
    }
    else if ((invoke->OptimizationFlags & MIR_INLINED) != 0 || (invoke->OptimizationFlags & MIR_INLINED_PRED) != 0)
    {
        //We have already inlined this invoke
        inlined = kInliningAlreadyInlined;
    }

    //If we haven't found a problem yet, then we continue with trying to inline
    if (inlined == kInliningNoError)
    {
        //If we have callsite information from the trace building, then we try to use that.
        //For virtual and interface invokes, the callsite information will be the method called
        //during building so it is just a guess. We skip this logic if we have an invoke-object-init
        //because we know method being invoked.
        if (invoke->meta.callsiteInfo != 0 && invoke->dalvikInsn.opcode != OP_INVOKE_OBJECT_INIT_RANGE)
        {
            calleeMethod = invoke->meta.callsiteInfo->method;
        }

        //If we do not have a predicted invoke and we still don't know method, we try to resolve it
        if (isPredicted == false && calleeMethod == 0)
        {
            //Get the method from which the invoke is originally from
            const Method *invokeSourceMethod = invoke->nesting.sourceMethod;

            //Method may not be resolved but for inlining we will try to resolve it
            calleeMethod = dvmCompilerCheckResolvedMethod (invokeSourceMethod, &(invoke->dalvikInsn));
        }

        if (calleeMethod != 0)
        {
            //If we know which method we want, then try to inline it
            inlined = tryInline (cUnit, calleeMethod, invoke, isPredicted);

            //If inlining failed and method JIT is enabled, we try to compile non-native method
            if (inlined != kInliningSuccess && (gDvmJit.disableOpt & (1 << kMethodJit)) == 0
                    && dvmIsNativeMethod (calleeMethod) == false && info != 0)
            {
                //Get statistics by analyzing method
                CompilerMethodStats *methodStats = dvmCompilerAnalyzeMethodBody (calleeMethod, true);

                //If method is leaf and not tagged as cannot compile, then we try to compile it first
                if ((methodStats->attributes & METHOD_IS_LEAF) != 0
                        && (methodStats->attributes & METHOD_CANNOT_COMPILE) == 0)
                {
                    //First check to see if we previously compiled it.
                    bool previouslyCompiled = dvmJitGetMethodAddr (calleeMethod->insns) != 0;

                    if (previouslyCompiled == true)
                    {
                        //If callee has been previously compiled, we simply tag the invoke
                        invoke->OptimizationFlags |= MIR_INVOKE_METHOD_JIT;
                    }
                    else
                    {
                        //Compile the callee first
                        dvmCompileMethod (calleeMethod, info);

                        //Now check whether we successfully compiled it
                        if (dvmJitGetMethodAddr (calleeMethod->insns) != 0)
                        {
                            invoke->OptimizationFlags |= MIR_INVOKE_METHOD_JIT;
                        }
                        else
                        {
                            methodStats->attributes |= METHOD_CANNOT_COMPILE;
                        }
                    }
                }
            }
        }
        else
        {
            inlined = kInliningUnknownMethod;
        }
    }

    //If we have have verbosity enabled then we print a message
    if (cUnit->printPass == true || cUnit->printMe == true)
    {
        //Decode the MIR
        char *decoded = dvmCompilerGetDalvikDisassembly (&(invoke->dalvikInsn), 0);

        //Print a message depending on success
        const char *success = (inlined == kInliningSuccess) ? "Successfully inlined" : "Failed to inline";

        //We only print a because message if we fail
        const char *because = (inlined == kInliningSuccess) ? "" : "because ";

        const char *failureMessage = getFailureMessage (inlined);

        //We print some information about method if we have it
        const char *methodInfo1 = calleeMethod != 0 ? "of " : "";
        const char *methodInfo2 = calleeMethod != 0 ? calleeMethod->clazz->descriptor : "";
        const char *methodInfo3 = calleeMethod != 0 ? "." : "";
        const char *methodInfo4 = calleeMethod != 0 ? calleeMethod->name : "";

        ALOGD ("%s %s %s%s%s%s %s%s", success, decoded, methodInfo1, methodInfo2, methodInfo3, methodInfo4,
                because, failureMessage);
    }

    if (isInliningFailureFatal (inlined) == true)
    {
        ALOGD ("JIT_INFO: Aborting trace because inliner reached an unrecoverable error");
        //If we have a fatal failure, that means we cannot recover
        dvmCompilerAbort (cUnit);
    }

    //Return whether we have successfully inlined
    return (inlined == kInliningSuccess);
}

/* Walks through the basic blocks looking for BB's with instructions in order to try to possibly inline an invoke */
void dvmCompilerInlineMIR (CompilationUnit *cUnit, JitTranslationInfo *info)
{
    const GrowableList *blockList = &cUnit->blockList;

    //We add basic blocks when we inline so we don't use growable list iterator
    for (size_t idx = 0; idx < dvmGrowableListSize (blockList); idx++)
    {
        BasicBlock *bb = reinterpret_cast<BasicBlock *> (dvmGrowableListGetElement (blockList, idx));

        //Stop looking if we have no more basic blocks
        if (bb == NULL)
        {
            break;
        }

        //Invoke should be last instruction
        MIR *lastMIRInsn = bb->lastMIRInsn;

        //If we have a last instruction
        if (lastMIRInsn != 0)
        {
            int flags = dvmCompilerGetOpcodeFlags (lastMIRInsn->dalvikInsn.opcode);

            //Check if it really is an invoke
            if ((flags & kInstrInvoke) != 0)
            {
                //Try to inline the instruction
                handleInlining (cUnit, info, bb, lastMIRInsn);
            }
        }
    }
}

/* Goes through the given basic block and tries to inline invokes */
bool dvmCompilerMethodInlining (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Start off with assumption we won't inline anything
    bool inlined = false;

    //Walk through the MIRs of this BB
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        int flags = dvmCompilerGetOpcodeFlags (mir->dalvikInsn.opcode);

        if ((flags & kInstrInvoke) != 0)
        {
            //We have an invoke, let's check if we can inline it
            bool res = handleInlining (cUnit, 0, bb, mir);

            inlined = inlined || res;
        }
    }

    return inlined;
}
