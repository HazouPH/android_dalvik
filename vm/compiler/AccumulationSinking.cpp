/*
 * Copyright (C) 2013 Intel Corporation
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

#include "AccumulationSinking.h"
#include "CompilerIR.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "Expression.h"
#include "LoopInformation.h"
#include "PassDriver.h"

#include <algorithm>

/**
 * @brief Choose an IV for the pass: it must be able to count the iterations
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param increment Holds the chosen IV increment value (updated by the function)
 * @return the chosen IV for the pass, or -1 if not found
 */
static int chooseIV (CompilationUnit *cUnit, LoopInformation *info, int &increment)
{
    //Get the IV list
    GrowableList& ivList = info->getInductionVariableList ();

    //Go through the induction variable list
    GrowableListIterator iterator;
    dvmGrowableListIteratorInit(&ivList, &iterator);

    while (true)
    {
        InductionVariableInfo *infoIV = (InductionVariableInfo *) (dvmGrowableListIteratorNext(&iterator));

        //Bail at the end
        if (infoIV == 0)
        {
            break;
        }

        //Is it a simple induction variable?
        if (infoIV->getMultiplier () == 1 && infoIV->isBasicIV () == true)
        {
            //Is the increment 1?
            //TODO Add support for negative and positive constants.
            if (infoIV->loopIncrement == 1)
            {
                //Get the ssa register for this induction
                int ssaReg = infoIV->ssaReg;

                //Get the increment
                increment = infoIV->loopIncrement;

                //Return the register side
                return dvmExtractSSARegister (cUnit, ssaReg);
            }
        }
    }

    //Did not find a simple induction variable
    return -1;
}

/**
 * @brief Find the definition of the phi node that is in the loop
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param phi the Phi node
 * @return find the last definition in the loop for that virtual register
 */
static MIR *findLastDefinition (CompilationUnit *cUnit, LoopInformation *info, MIR *phi)
{
    //Get the SSA structure
    SSARepresentation *ssa = phi->ssaRep;

    //We should have only two uses for the phi node
    if (ssa->numUses != 2)
    {
        return 0;
    }

    //In theory, we could assume the second use is the one that we want but let's be paranoid
    unsigned int firstUse = ssa->uses[0];
    unsigned int secondUse = ssa->uses[1];

    //Get the subscripts
    unsigned int subScript1 = dvmExtractSSASubscript (cUnit, firstUse);
    unsigned int subScript2 = dvmExtractSSASubscript (cUnit, secondUse);

    //Get the defWhere list
    MIR **defWhere = ssa->defWhere;

    //Paranoid
    assert (defWhere != 0);

    //Use the highest of the two
    MIR *mir = (subScript1 < subScript2) ? defWhere[1] : defWhere[0];

    //Return it
    return mir;
}

/**
 * @brief Helper function for filtering, it follows the definitions and accumulates them in the vector and map provided as arguments
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param current the current instruction
 * @param accumulatorCalculation the calculation of the vr accumulator
 * @param accumulatorMap the map containing every MIR used for the calculation of the vr accumulator
 */
static void fillAccumulatorMapHelper (CompilationUnit *cUnit, LoopInformation *info, MIR *current,
                                      std::vector<MIR *> &accumulatorCalculation, std::map<MIR *, bool> &accumulatorMap)
{
    //First job is to check if current is in the accumulator map
    if (accumulatorMap.find (current) != accumulatorMap.end ())
    {
        //Then bail, we've handled it before
        return;
    }

    //Otherwise add it there
    accumulatorMap[current] = true;

    //Next, add it to the calculation as well
    accumulatorCalculation.push_back (current);
    PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: ----add MIR into accumulator list: %s ", dvmCompilerGetDalvikDisassembly (& (current->dalvikInsn), NULL));

    //Get its SSA representation
    SSARepresentation *ssa = current->ssaRep;

    //Now go through it's defWhere and recursively call with them
    int max = ssa->numUses;
    for (int i = 0; i < max; i++)
    {
        MIR *defined = ssa->defWhere[i];

        if (defined != 0)
        {
            //Get its BasicBlock
            BasicBlock *bb = defined->bb;

            //If in the loop, call recursively
            if (info->contains (bb) == true)
            {
                fillAccumulatorMapHelper (cUnit, info, defined, accumulatorCalculation, accumulatorMap);
            }
        }
    }
}

/**
 * @brief Filter the VRs to only consider inter-iteration interIteration, non IV registers, and not used for another calculation
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param vr the considered virtual register
 * @param accumulatorCalculation the calculation of the vr accumulator
 * @param accumulatorMap the map containing every MIR used for the calculation of the vr accumulator
 * @return whether or not the VR is a potential accumulator
 */
static bool fillAccumulatorMap (CompilationUnit *cUnit, LoopInformation *info, unsigned int vr,
                                std::vector<MIR *> &accumulatorCalculation, std::map<MIR *, bool> &accumulatorMap)
{
    //First job is to find the related PHI MIR for the loop
    MIR *phi = info->getPhiInstruction (cUnit, vr);

    //Paranoid
    if (phi == 0)
    {
        return false;
    }

    //Find the real last definition we care about
    MIR *lastDef = findLastDefinition (cUnit, info, phi);

    //Paranoid: if we didn't find it, bail
    if (lastDef == 0)
    {
        return false;
    }

    //Add the Phi node to the map to not add it to the calculation
    accumulatorMap[phi] = true;

    //Now recurse to add all calculations related to the accumulator calculation
    fillAccumulatorMapHelper (cUnit, info, lastDef, accumulatorCalculation, accumulatorMap);

    //Report success
    return true;
}

/**
 * This enum represetents a position in wide VR of the VR defined by Phi node
 */
enum PositionInWideVR {
    vrNotWidePosition = 0,      /**< VR is 32-bit */
    vrLowPosition,              /**< VR is low part of 64-bit VR */
    vrHighPosition,             /**< VR is high part of 64-bit VR */
    vrAmbigousPosition,         /**< defined VR has uses of different types
                                     This is a specific case for Phi node where one use is 32-bit
                                     another one is part of 64-bit, so it means the rigister is dead here,
                                     otherwise it is a problem in a program or very complex case. */
    vrUnknownPosition,          /**< Cannot determine position, mostly is used internally. */
};

/**
 * @brief Get the position of VR defined by Phi node in possible Wide VR.
 * @param cUnit the CompilationUnit
 * @param phi node to exersise
 * @param seen already seen mirs
 * @return detected position
 */
static PositionInWideVR getPhiPositionInWideVR (CompilationUnit *cUnit, MIR* phi, BitVector *seen = 0)
{
    assert (phi->dalvikInsn.opcode == static_cast<Opcode> (kMirOpPhi));

    assert (phi->ssaRep != 0);
    assert (phi->ssaRep->numDefs == 1);

    //Get the SSA representation
    SSARepresentation *ssa = phi->ssaRep;

    // Are we in recursion ?
    if (seen != 0)
    {
        // We are iterating over Phi nodes corresponding to the same VR
        // So SSA subscript uniquely defines a Phi node
        unsigned int subScript = dvmExtractSSASubscript (cUnit, ssa->defs[0]);

        // Check whether we have already visited this Phi node
        if (dvmIsBitSet (seen, subScript) == true)
        {
            return vrUnknownPosition;
        }
    }

    // At this moment we do not know result
    PositionInWideVR result = vrUnknownPosition;

    for (int i = 0; i < ssa->numUses; i++)
    {
        MIR *mir = ssa->defWhere [i];

        // If mir is 0 then def comes to use from beginning of the trace
        // and we do not know what type this register is, let's base on other definitions.
        if (mir != 0)
        {
            PositionInWideVR tmp;

            if (static_cast<ExtendedMIROpcode>(mir->dalvikInsn.opcode) == kMirOpPhi)
            {
                // Rare case, don't worry about allocation
                if (seen == 0)
                {
                    seen = dvmCompilerAllocBitVector (1, true);
                    dvmClearAllBits (seen);
                }
                // Mark that there is no sense to ask us
                unsigned int subScript = dvmExtractSSASubscript (cUnit, ssa->defs[0]);
                dvmSetBit (seen, subScript);

                // Ask phi node about its position
                tmp = getPhiPositionInWideVR (cUnit, mir, seen);
            }
            else
            {
                // 0 defs is not possible because we came here as use of this def
                assert (mir->ssaRep->numDefs > 0);

                if (mir->ssaRep->numDefs == 1)
                {
                    tmp = vrNotWidePosition;
                }
                else
                {
                    tmp = (mir->ssaRep->defs[0] == ssa->uses[i]) ? vrLowPosition : vrHighPosition;
                }
            }

            // Now merge results
            if (result == vrUnknownPosition)
            {
                result = tmp;
            }
            else
            {
                if (tmp != vrUnknownPosition)
                {
                    // Results are different ?
                    if (result != tmp)
                    {
                        return vrAmbigousPosition;
                    }
                    // otherwise tmp == result and there is no sense to update
                }
            }
        }
    }

    return result;
}

/**
 * @brief Find an alone use in the loop of specified def
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param def MIR to find an alone use
 * @param defPos the position inf ssa->defs array
 * @return MIR if single use is found, 0 otherwise
 */
static MIR *findAloneDefInALoop(CompilationUnit *cUnit, LoopInformation *info, MIR *def, int defPos)
{
    SSARepresentation *ssa = def->ssaRep;

    assert (ssa != 0);
    assert (ssa->usedNext != 0);
    assert (ssa->numDefs > defPos);

    MIR *result = 0;

    for (SUsedChain *chain = ssa->usedNext[defPos]; chain != 0; chain = chain->nextUse)
    {
        MIR* mir = chain->mir;

        if (info->contains (mir->bb) == true)
        {
            if (result != 0)
            {
                // It is not alone use
                return 0;
            }

            result = mir;
        }
    }
    return result;
}

/**
 * @brief check whether given VR has Phi node and is used as input for def of other VRs
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param vrLow Virtual Register to check, low part for wide VR
 * @param vrHigh high part of wide VR or 0 for non-wide VR
 * @return whether VR is not used in other calculations except itself
 */
static bool checkNoOtherUses(CompilationUnit *cUnit, LoopInformation *info, unsigned int vrLow, unsigned int vrHigh)
{
    bool isWide = vrHigh != 0;

    MIR *phiLow = info->getPhiInstruction (cUnit, vrLow);

    if (phiLow == 0)
    {
        return false;
    }

    MIR *phiHigh = (isWide == false) ? 0 : info->getPhiInstruction (cUnit, vrHigh);

    if ((isWide == true) && (phiHigh == 0))
    {
        return false;
    }

    MIR *defLow = findAloneDefInALoop (cUnit, info, phiLow, 0);
    MIR *defHigh = (isWide == false) ? 0 : findAloneDefInALoop (cUnit, info, phiHigh, 0);

    // iterate over re-assignments until we return to our phi nodes
    do
    {
        // alone def should be and it should be the same for wide VR
        if (defLow == 0 || ((isWide == true) && (defLow != defHigh)))
        {
            return false;
        }

        SSARepresentation *ssa = defLow->ssaRep;
        int expectedNumberOfDefs = (isWide == true) ? 2 : 1;
        if (ssa->numDefs != expectedNumberOfDefs)
        {
            return false;
        }

        if (vrLow != dvmExtractSSARegister (cUnit, ssa->defs[0]))
        {
            return false;
        }

        if ((isWide == true) && (vrHigh != dvmExtractSSARegister (cUnit, ssa->defs[1])))
        {
            return false;
        }

        defLow = findAloneDefInALoop (cUnit, info, defLow, 0);
        defHigh = (isWide == false) ? 0 : findAloneDefInALoop (cUnit, info, defHigh, 1);

    } while ((defLow != phiLow) || ((isWide == true) && (defHigh != phiHigh)));

    return true;
}

/**
 * @brief Filter the VRs to only consider inter-iteration interIteration, non IV registers, and not used for another calculation
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param accumulatorList the calculation of the accumulators to be considered by the pass
 */
static void filterVRs (CompilationUnit *cUnit, LoopInformation *info, std::vector<std::vector<MIR*> > &accumulatorList)
{
    //Get the loop interIteration
    BitVector *interIterationVariables = info->getInterIterationVariables ();

    //If we don't have any, we are done
    if (interIterationVariables == 0)
    {
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Did not find any iteration->iteration variable");
        return;
    }

    //Have a map for the filtered
    std::vector<unsigned int> vrElements;

    //First step: find the VRs that are not IV but are inter-iteration interIteration

    //Go through each variable
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit (interIterationVariables, &bvIterator);
    while (true) {
        int vr = dvmBitVectorIteratorNext(&bvIterator);

        //If done, bail
        if (vr == -1)
        {
            break;
        }

        //Is that register an induction variable?
        if (info->isBasicInductionVariable (cUnit, vr) == false)
        {
            MIR *phi = info->getPhiInstruction (cUnit, vr);
            // If vr is an induction variable, phi cannot be 0
            // but if something went wrong, simply don't consider this vr for optimization.
            // In assert world just stop.
            assert (phi != 0);
            if (phi == 0)
            {
                continue;
            }

            PositionInWideVR position = getPhiPositionInWideVR (cUnit, phi);

            // if it is ambigous position we skip it
            // if it is a high position, it has already been checked in low position
            if (position == vrNotWidePosition || position == vrLowPosition)
            {
                unsigned int vrHigh = (position == vrLowPosition) ? vr + 1 : 0;
                if (checkNoOtherUses(cUnit, info, vr, vrHigh) == true)
                {
                    //Add it now
                    vrElements.push_back (vr);
                    PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Push v%d into filtered VR list", vr);
                }
            }
        }
    }

    //Step two: check if these are accumulators, there is no use for any other variable
    //   - We are going to fill a vector of MIR vectors
    //      - In the end it will contain, for each accumulator, the MIRs that create it
    //      - We also fill a map to say which MIR goes to which accumulator for filtering
    //      - Finally, we check if any other MIR in the loop uses the accumulator or any subscript of it
    for (std::vector<unsigned int>::const_iterator it = vrElements.begin (); it != vrElements.end (); it++)
    {
        //Get the VR
        unsigned int vr = *it;

        //Get a local vector of MIR instructions for the accumulator
        std::vector<MIR *> accumulatorCalculation;
        //Get a local map for the MIRs being used
        std::map<MIR *, bool> accumulatorMap;
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Build accumulator list for VR v%d:", vr);

        //Fill the map if it is an accumulation
        bool res = fillAccumulatorMap (cUnit, info, vr, accumulatorCalculation, accumulatorMap);

        //If we succeeded, check every vector doesn't contain linear calculations and then
        //reverse the list before adding it
        if (res == true)
        {
            std::reverse (accumulatorCalculation.begin (), accumulatorCalculation.end ());

            accumulatorList.push_back (accumulatorCalculation);
        }
    }
}

/**
 * @brief Check if the instruction has a future use for its defines
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param mir the instruction we are checking if there is a use
 * @return whether the instruction has a future use
 */
static bool checkUsage (const CompilationUnit *cUnit, const LoopInformation *info, const MIR *mir)
{
    //If no mir, everything is fine
    if (mir == 0)
    {
        return true;
    }

    //Get the SSA representation
    SSARepresentation *ssa = mir->ssaRep;

    //Paranoid
    assert (ssa != 0 && ssa->usedNext != 0);

    //Go through the usedNext of the defines
    int max = ssa->numDefs;
    SUsedChain **chains = ssa->usedNext;

    for (int i = 0; i < max; i++)
    {
        SUsedChain *chain = chains[i];

        //Basically, we fail if the chain has a next
        if (chain != 0 && chain->nextUse != 0)
        {
            return false;
        }

        //We know there is no uses of our def
        //But it can leave the trace, so loop exits will be enough to check
        if (info->isSSARegLeavesLoop (cUnit, ssa->defs[i]) == true)
        {
            return false;
        }
    }


    //Report success
    return true;
}

/**
 * @brief Builds expressions for lists of MIRs.
 * @param cUnit
 * @param info
 * @param vrList
 * @param ivExpressions Expressions are written to this list. There is a 1:1
 * mapping between entries and vrList and expressions in this list.
 */
static void buildExpressions(CompilationUnit *cUnit, LoopInformation *info,
                                const std::vector<std::vector<MIR *> > &vrList,
                                std::vector<Expression *> &ivExpressions)
{
    //Try to construct an expression tree for a list of candidate MIRs. Thus,
    //we iterate through the list which contains lists of MIRs
    std::vector<std::vector<MIR *> >::const_iterator listIter;

    //Go through the VR list
    for (listIter = vrList.begin(); listIter != vrList.end(); listIter++)
    {
        //Get reference to the list of MIRs
        const std::vector<MIR *> & listOfMIRs = *listIter;

        //Build expression trees for the list of MIRs
        std::map<MIR *, Expression *> mirToExpression = Expression::mirsToExpressions(listOfMIRs);

        //We only care about expression tree of last MIR in our list
        MIR * lastMIR = listOfMIRs.back();

        //Do we have an expression tree for our last MIR?
        std::map<MIR *, Expression *>::const_iterator mirIter;
        mirIter = mirToExpression.find(lastMIR);

        //If we did not find it, then let's just insert an empty expression
        if (mirIter == mirToExpression.end())
        {
            ivExpressions.push_back(0);
        }
        //Else we save the expression we found
        else
        {
            Expression * found = mirIter->second;
            ivExpressions.push_back(found);
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Build expression tree for accumulator list starting with %s\n%s",
                dvmCompilerGetDalvikDisassembly (& (lastMIR->dalvikInsn), NULL), found->toString(cUnit).c_str());
        }
    }
}

/**
 * @brief Recursive walker over sub-expression for find a dangling constants
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param expression the expression where to find a dangling constant
 * @param expressionRef the leaf expressions with dangling constants
 * @param inductionVariableIncrement the increment value on the chosen IV
 * @param isRootExpression whether the expression is root one
 * @return true if dangling constant is found
 */
static bool findDanglingConstantsHelper(CompilationUnit *cUnit, const LoopInformation *info, Expression * expression,
        std::vector< BytecodeExpression *> & expressionRef,
        const int & inductionVariableIncrement, const bool isRootExpression)
{
    if (expression == 0)
    {
        return false;
    }

    //If we do not have a bytecode expression, we cannot find a linear
    //transformation expression.
    if (expression->isBytecodeExpression() == false)
    {
        return false;
    }

    //If we get to this point, we know that we have a bytecode expression.
    BytecodeExpression * bytecodeExpr =
            static_cast<BytecodeExpression *>(expression);

    const MIR * expressionMir = bytecodeExpr->getMir();

    //If check usage returns false, let's be conservative and not accept it.
    //Check only non-root expressions, root expression was checked when we chose it.
    if (isRootExpression == false && checkUsage(cUnit, info, expressionMir) == false)
    {
        return false;
    }

    //If we don't have a linear transformation, we eagerly return false
    //TODO Sub can also be supported, but we need to count the number of
    //levels and also figure out if it is the rhs or lhs operand.
    if (bytecodeExpr->getExpressionKind() != ExpKind_Add)
    {
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Haven't found dangling constant in below expression, we only consider Addition \n%s",
            bytecodeExpr->toString(cUnit).c_str());
        return false;
    }

    //Linear transformation expressions are always binary
    BinaryExpression * binaryExpr =
            static_cast<BinaryExpression *>(bytecodeExpr);

    //Look at both sides to find the expression containing the
    //constant. We do however, only care if the constant itself
    //is the RHS of its expression.
    Expression * rhsChild = binaryExpr->getRhs();
    Expression * lhsChild = binaryExpr->getLhs();

    //Paranoid, but should never happen because expressions guarantee
    //non-null children.
    if (rhsChild == 0 || lhsChild == 0)
    {
        return false;
    }

    bool foundConstantRight = false;
    bool foundConstantLeft = false;

    //Now we can check if we found linear transformation using a constant.
    //TODO A better approach here would be to check if expression evaluates to
    //a constant value. Also since our IV is an int, we want to make sure
    //that what we find is also an integer linear transformation.
    if (rhsChild->isConstant() == true
            && bytecodeExpr->getExpressionType() == ExpType_Int)
    {
        //Cast is safe because we know it is a constant
        ConstantExpression * constant = static_cast<ConstantExpression *>(rhsChild);

        //Interpret its value as an integer since we are using it in
        //an expression whose result will be an integer.
        int value = constant->getValue<int>();

        //Now compare its value with what we are looking for
        foundConstantRight = (value == inductionVariableIncrement);

        if (foundConstantRight == true)
        {
            expressionRef.push_back(bytecodeExpr);
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Found dangling constant in below expression, same value with IV increment\n%s",
                bytecodeExpr->toString(cUnit).c_str());
        }
    }
    else
    {
        //Look for a constant on the right
        foundConstantRight = findDanglingConstantsHelper(cUnit, info, rhsChild, expressionRef,
                inductionVariableIncrement, false);
    }

    //Look for a constant on the left
    foundConstantLeft = findDanglingConstantsHelper(cUnit, info, lhsChild, expressionRef,
            inductionVariableIncrement, false);

    return (foundConstantLeft || foundConstantRight);
}

/**
 * @brief Find dangling constants we can sink
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param vrExpressions the expressions for the virtual registers in the vrList (updated by the function)
 * @param chosenIV the IV used to count the loop iterations
 * @param increment the increment value on the chosen IV
 * @param toRemove the vector of MIR instructions to remove (updated by the function)
 * @param toSink the vector of MIR instructions to sink (updated by the function)
 * @param toHoist the vector of MRI instructions to hoist (updated by the function)
 */
static void findDanglingConstants(CompilationUnit *cUnit, LoopInformation *info,
        std::vector<Expression *> &vrExpressions, unsigned int chosenIV,
        const int & increment, std::vector<MIR *> &toRemove,
        std::vector<MIR *> &toSink, std::vector<MIR *> &toHoist)
{
    std::vector<Expression *>::const_iterator exprIter;

    bool foundDanglingConstant = false;

    //Iterate through the expression candidates
    for (exprIter = vrExpressions.begin(); exprIter != vrExpressions.end();
            exprIter++)
    {
        Expression * expression = *exprIter;

        //The expression for this MIR could be null. Continue
        if (expression == 0)
        {
            continue;
        }

        //We consider only linear accumulations
        if (expression->isLinearAccumulation (cUnit) == false)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Skip expression tree %s due to it is not linear accumulation",
                expression->toString(cUnit).c_str());
            continue;
        }

        BytecodeExpression * bytecodeExpr =
                static_cast<BytecodeExpression *>(expression);

        MIR * bytecodeExprMir = bytecodeExpr->getMir();

        //If it is a bytecode expression, it must have a MIR associated
        assert (bytecodeExprMir != 0);

        std::vector<BytecodeExpression *> expressionsToRemoveList;

        foundDanglingConstant = findDanglingConstantsHelper(cUnit, info, expression,
                expressionsToRemoveList, increment, true);

        if (foundDanglingConstant == true)
        {
            std::vector<BytecodeExpression *>::const_iterator toRemoveIter;

            //Iterate through all the expressions which are candidates for sinking
            for (toRemoveIter = expressionsToRemoveList.begin(); toRemoveIter != expressionsToRemoveList.end(); toRemoveIter++)
            {
                BytecodeExpression * expressionToRemove = *toRemoveIter;

                //Get the MIR to remove
                MIR * mirToRemove = expressionToRemove->getMir();

                //All bytecode expressions must have a MIR
                assert (mirToRemove != 0);

                //Depending on the IV increment, we must decide which kind of
                //expression we need to generate for the sink and which kind
                //for the hoist.
                ExpressionKind sinkExpKind =
                        increment >= 0 ? ExpKind_Add : ExpKind_Sub;
                ExpressionKind hoistExpKind =
                        sinkExpKind == ExpKind_Sub ? ExpKind_Add : ExpKind_Sub;

                //For the new expression we create, we want it to be the same type
                //as the one as we are removing.
                ExpressionType newMirExpType = expressionToRemove->getExpressionType();

                //We are sinking and hoisting operations on the VR that receives result
                //that is at top of expression tree.
                int resultVR = bytecodeExprMir->dalvikInsn.vA;

                //Create MIR to use for sinking
                MIR * mirToSink = BytecodeExpression::createMir(sinkExpKind,
                        newMirExpType, resultVR, resultVR, chosenIV);

                //Create MIR to use for hoisting
                MIR * mirToHoist = BytecodeExpression::createMir(hoistExpKind,
                        newMirExpType, resultVR, resultVR, chosenIV);

                //Now save all MIRs in the output lists
                toSink.push_back(mirToSink);
                toHoist.push_back(mirToHoist);
                toRemove.push_back(mirToRemove);
            }
        }
    }
}

/**
 * @brief Removes the MIRs in list from their corresponding Basic Block.
 * @details Note it does not ensure to fix uses of the VRs defined.
 * If we tag v6 = v5 + 1 for removal, it does not ensure that users v6 use
 * v5 instead.
 * @param cUnit the CompilationUnit
 * @param toRemove list of MIRs to remove
 */
static void removeAccumulations(CompilationUnit *cUnit, const std::vector<MIR *> & toRemove)
{
    std::vector<MIR *>::const_iterator mirIter;

    for (mirIter = toRemove.begin (); mirIter != toRemove.end (); mirIter++)
    {
        MIR * mir = (*mirIter);

        //Check if the destVR is different from source
        //This assumes that LHS of expression is a VR
        unsigned int vA = mir->dalvikInsn.vA;
        unsigned int vB = mir->dalvikInsn.vB;

        //If different, we need to do some work
        if (vA != vB)
        {
            dvmCompilerRewriteMirDef (mir, vA, vB);
        }

        dvmCompilerRemoveMIR (mir->bb, mir);
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Successfully sunk %s", dvmCompilerGetDalvikDisassembly (& (mir->dalvikInsn), NULL));
    }
}

void dvmCompilerGetLoopExpressions (CompilationUnit *cUnit, LoopInformation *info,
                                std::vector<Expression *> &ivExpressions)
{
    //Filter out the virtual registers because we only want to keep PHI nodes that aren't IVs
    //and are not used except for their own calculation.
    std::vector<std::vector<MIR *> > accumulatorList;
    filterVRs (cUnit, info, accumulatorList);

    //Now build the expressions for all these MIRs
    buildExpressions (cUnit, info, accumulatorList, ivExpressions);
}

/**
 * @brief Handle a loop for the sinking of an accumulation
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param data required by interface (not used)
 * @return true to continue iteration over loops
 */
static bool sinkAccumulation (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Try to optimize %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
        cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
    // Only apply pass to innermost loop
    if (info->getNested () != 0)
    {
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: This is not the innermost loop");
        return true;
    }

    //Step 1: Choose an IV: we want an IV that can count the iterations
    int increment;
    int chosenIV = chooseIV (cUnit, info, increment);

    //If we didn't find one, bail
    if (chosenIV < 0)
    {
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Did not find a simple induction variable");
        return true;
    }

    //Step 2: Get the PHI Nodes and build the expressions for them
    std::vector<Expression *> ivExpressions;
    dvmCompilerGetLoopExpressions (cUnit, info, ivExpressions);

    //Step 3: Find the dangling constants (any constant accumulation we can sink)
    std::vector<MIR *> toRemove;
    std::vector<MIR *> toSink;
    std::vector<MIR *> toHoist;
    findDanglingConstants (cUnit, info, ivExpressions, chosenIV, increment, toRemove,
            toSink, toHoist);

    //Step 4: Sink the accumulation
    info->addInstructionsToExits (cUnit, toSink);

    //Step 5: Hoist the initial value decrementation
    dvmCompilerAddInstructionsToBasicBlock (info->getPreHeader (), toHoist);

    //Step 6: Remove MIRs no longer needed
    removeAccumulations (cUnit, toRemove);

    PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking: Finished to sink accumulation in %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
        cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
    return true;
}

/**
 * @brief Check whether the sinking of an accumulation is applicable
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation to check
 * @param data required by interface (not used)
 * @return false to reject optimization
 */
static bool compilerSinkAccumulationsGateHelper(const CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    //We are only interested in the innermost loops
    if (info->getNested () == 0)
    {
        //We don't want loops with multiple exit blocks
        if (dvmCountSetBits (info->getExitLoops ()) > 1)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, we don't want loops with multiple exit blocks: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }

        //We also don't want multiple backward blocks
        if (dvmCountSetBits (info->getBackwardBranches ()) > 1)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, we don't want loops with multiple backward blocks: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }

        //Reject if we can throw an exception in our code
        if (info->canThrow (cUnit) == true)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, the code in loop can throw: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }

        //We also don't want invokes in the loop
        if (info->hasInvoke (cUnit) == true)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, we don't want invokes in loop: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }

        //We only accept one BB
        if (dvmCountSetBits (info->getBasicBlocks ()) > 1)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, we only accept one BB: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }

        //If more than one IV, we bail
        if (info->getNumBasicIV (cUnit) != 1)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, loop has more than one basic IV: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }

        //TODO wrap this into loop information
        if (info->isUniqueIVIncrementingBy1 () == false)
        {
            PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, loop has more than one basic IV or increment is not 1: %s%s, loop start offset @0x%02x, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, info->getEntryBlock()->startOffset, cUnit->entryBlock->startOffset);
            return false;
        }
    }

    //All good!
    return true;
}

bool dvmCompilerSinkAccumulationsGate(const CompilationUnit *cUnit, Pass *curPass)
{
    //First, make sure we are in the new loop detection system
    if (dvmCompilerTraceIsLoopNewSystem(cUnit, curPass) == false)
    {
        PASS_LOG (ALOGD, cUnit, "Accumulation_Sinking not applicable, old loop detection system used here: %s%s, cUnit start offset @0x%02x",
                cUnit->method->clazz->descriptor, cUnit->method->name, cUnit->entryBlock->startOffset);
        return false;
    }

    LoopInformation *info = cUnit->loopInformation;

    //Find the innermost loop and test it
    if (info != 0)
    {
        if (info->iterateWithConst (cUnit, compilerSinkAccumulationsGateHelper) == true)
        {
            // Optimization can be applicable
            return true;
        }
    }
    return false;
}

void dvmCompilerAccumulationSinking (CompilationUnit *cUnit, Pass *currentPass)
{
    //Now let's go through the loop information
    LoopInformation *info = cUnit->loopInformation;

    //Now try to sink accumulations
    if (info != 0)
    {
        info->iterate (cUnit, sinkAccumulation);
    }

    //Unused argument
    (void) currentPass;
}

