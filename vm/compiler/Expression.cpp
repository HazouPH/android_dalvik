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

#include "Expression.h"
#include "Dataflow.h"
#include <sstream>
#include <utility>
#include <algorithm>

std::vector<Expression *> UnaryExpression::getChildren (void) const
{
    std::vector<Expression *> child;

    //Unary expressions only have one child
    child.push_back (operand);

    return child;
}

std::vector<Expression *> BinaryExpression::getChildren (void) const
{
    std::vector<Expression *> children;

    //Binary expressions have two children
    children.push_back (lhs);
    children.push_back (rhs);

    return children;
}

std::string BinaryExpression::toString (const CompilationUnit * cUnit)
{
    std::string expressionString;

    expressionString.append ("(");
    expressionString.append (assignmentTo->toString (cUnit));
    expressionString.append (" = ");

    //For phi expressions we need to prepend before printing
    //operands.
    if (expKind == ExpKind_Phi)
    {
        expressionString.append ("PHI");
    }

    expressionString.append ("(");

    //Print lhs operand first
    expressionString.append (lhs->toString (cUnit));

    //Now we print the operation
    switch (expKind)
    {
        case ExpKind_ConstSet:
            break;
        case ExpKind_Add:
            expressionString.append (" + ");
            break;
        case ExpKind_Sub:
            expressionString.append (" - ");
            break;
        case ExpKind_Mul:
            expressionString.append (" * ");
            break;
        case ExpKind_Phi:
            expressionString.append (", ");
            break;
        case ExpKind_Div:
            expressionString.append (" / ");
            break;
        case ExpKind_Rem:
            expressionString.append (" % ");
            break;
        case ExpKind_And:
            expressionString.append (" & ");
            break;
        case ExpKind_Or:
            expressionString.append (" | ");
            break;
        case ExpKind_Xor:
            expressionString.append (" ^ ");
            break;
        case ExpKind_Shl:
            expressionString.append (" << ");
            break;
        case ExpKind_Shr:
            expressionString.append (" >> ");
            break;
        case ExpKind_Ushr:
            expressionString.append (" >>> ");
            break;
        case ExpKind_Invalid:
        default:
            expressionString.append (" ?? ");
            break;
    }

    //Now we can print rhs operand
    expressionString.append (rhs->toString (cUnit));
    expressionString.append ("))");

    return expressionString;
}

std::string UnaryExpression::toString (const CompilationUnit * cUnit)
{
    std::string expressionString;

    expressionString.append ("(");
    expressionString.append (assignmentTo->toString (cUnit));
    expressionString.append (" = ");

    if (expKind == ExpKind_Cast)
    {
        expressionString.append (" (cast)");
    }
    else if (expKind == ExpKind_Invalid)
    {
        expressionString.append (" ?? ");
    }

    expressionString.append (operand->toString (cUnit));
    expressionString.append (")");

    return expressionString;
}

std::string VirtualRegister::toString (const CompilationUnit * cUnit)
{
    std::stringstream ss;

    //For readability, we convert to dalvik register
    int lowDalvikReg = dvmExtractSSARegister (cUnit, this->lowSsaReg);

    ss << "v" << lowDalvikReg;

    //If our virtual register is wide, we print the VR for the high bits
    if (isWide ())
    {
        //Technically we could print lowDalvikReg+1, but converting
        //is not as brittle.
        int highDalvikReg = dvmExtractSSARegister (cUnit, this->highSsaReg);

        ss << ",v" << highDalvikReg;
    }

    return ss.str ();
}

std::string ConstantExpression::toString (const CompilationUnit * cUnit)
{
    std::stringstream ss;

    //Since we don't know how the constant will be interpreted, we always
    //print its 64-bit int representation.
    ss << getValue<int64_t> ();

    return ss.str ();
}

Expression * Expression::mirToExpression (MIR * mir,
        std::map<VirtualRegister *, Expression *> * vrToExpression)
{
    //If mir is null, we have no expression to build
    if (mir == 0)
    {
        return 0;
    }

    //Get local copy of opcode
    int dalvikOpcode = mir->dalvikInsn.opcode;

    Expression * result = 0;

    //In order to figure out how to create the expression, we look at flags
    //from the dataflow information. However, a better approach here would be
    //to update opcode-gen to automatically generate tables with this
    //information for every bytecode. That can only work when expression
    //implementation is complete.
    if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_ADD_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Add);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_SUBTRACT_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Sub);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_MULTIPLY_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Mul);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_DIVIDE_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Div);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_REMAINDER_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Rem);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_AND_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_And);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_OR_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Or);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_XOR_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Xor);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_SHR_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Shr);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_SHL_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Shl);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_USHR_EXPRESSION)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Ushr);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_CAST)
    {
        result = UnaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Cast);
    }
    else if (dvmCompilerDataFlowAttributes[dalvikOpcode] & DF_SETS_CONST)
    {
        result = UnaryExpression::mirToExpression (mir, vrToExpression, ExpKind_ConstSet);
    }
    else if (dalvikOpcode == kMirOpPhi)
    {
        result = BinaryExpression::mirToExpression (mir, vrToExpression, ExpKind_Phi);
    }

    return result;
}

bool Expression::isLinearAccumulation (const CompilationUnit * cUnit)
{
    //First find a VR we are interested in
    if (isBytecodeExpression () != true)
    {
        //It is not accumulation
        return false;
    }

    if (getChildren().size() != 2)
    {
        //it is not accumulation
        return false;
    }

    //Get VR
    VirtualRegister *vr = static_cast<BytecodeExpression *>(this)->getAssignmentTo ();
    int VR = dvmExtractSSARegister (cUnit, vr->getLowSSAReg());

    //Check whether this expression is a linear accumulation wrt VR
    LinearAccumulationCheckResult result = isLinearAccumulation (cUnit, VR);
    return result == LinAccResVRSeen;
}

LinearAccumulationCheckResult BinaryExpression::isLinearAccumulation (const CompilationUnit * cUnit, int VR)
{
    //If this expression is assigned to VR
    LinearAccumulationCheckResult aResult = getAssignmentTo ()->isLinearAccumulation (cUnit, VR);

    //If left child is linear accumulation wrt VR
    LinearAccumulationCheckResult lResult = lhs->isLinearAccumulation (cUnit, VR);
    if (lResult == LinAccResError)
    {
        return LinAccResError;
    }

    //If right child is linear accumulation wrt VR
    LinearAccumulationCheckResult rResult = rhs->isLinearAccumulation (cUnit, VR);
    if (rResult == LinAccResError)
    {
        return LinAccResError;
    }

    bool res;
    if (getExpressionKind() == ExpKind_Add)
    {
        if (aResult == LinAccResVRSeen)
        {
            //We are assigning to VR and it is addition, so we should see VR exactly
            //in one child otherwise it is something like v = v + v or v = a + b
            res = (lResult == LinAccResVRSeen && rResult == LinAccResVRNotSeen)
                    || (lResult == LinAccResVRNotSeen && rResult == LinAccResVRSeen);
        }
        else
        {
            //We are assigning to not VR and it is addition, so we are allowed to see VR
            //no more than once otherwise it is something like a = v + v
            res = (lResult == LinAccResVRSeen && rResult == LinAccResVRSeen) == false;
        }
    }
    else
    {
        if (aResult == LinAccResVRSeen)
        {
            //We are assigning to VR and it is not addition, so we failed
            //because it is something like v = c
            res = false;
        }
        else
        {
            //We are assigning to not VR and it is not addition, so we should not see VR
            //otherwise it is something like a = v * b
            res = (lResult == LinAccResVRSeen || rResult == LinAccResVRSeen) == false;
        }
    }

    if (res == true)
    {
        //If it is linear accumulation we should report whether we seen VR or not
        return lResult == LinAccResVRSeen || rResult == LinAccResVRSeen ? LinAccResVRSeen : LinAccResVRNotSeen;
    }

    return LinAccResError;
}

LinearAccumulationCheckResult UnaryExpression::isLinearAccumulation (const CompilationUnit * cUnit, int VR)
{
    LinearAccumulationCheckResult aResult = getAssignmentTo ()->isLinearAccumulation (cUnit, VR);
    LinearAccumulationCheckResult oResult = operand->isLinearAccumulation (cUnit, VR);

    //For unary expression we should not see VR
    if (aResult == LinAccResVRNotSeen && oResult == LinAccResVRNotSeen)
    {
        return LinAccResVRNotSeen;
    }
    return LinAccResError;
}

LinearAccumulationCheckResult VirtualRegister::isLinearAccumulation (const CompilationUnit * cUnit, int VR)
{
    //Check whether this Virtual Register covers VR
    int vrLow = dvmExtractSSARegister (cUnit, getLowSSAReg());
    if (vrLow == VR)
    {
        return LinAccResVRSeen;
    }

    if (isWide ())
    {
        int vrHigh = dvmExtractSSARegister (cUnit, getHighSSAReg());
        if (vrHigh == VR)
        {
            return LinAccResVRSeen;
        }
    }

    return LinAccResVRNotSeen;
}


std::map<MIR *, Expression *> Expression::mirsToExpressions (
        const std::vector<MIR *> & listOfMirs)
{
    std::map<VirtualRegister *, Expression *> vrToExpression;
    std::map<MIR *, Expression *> mirToExpressionMap;

    for (std::vector<MIR *>::const_iterator iter = listOfMirs.begin ();
            iter != listOfMirs.end (); iter++)
    {
        //Get the mir
        MIR * mir = (*iter);

        //Convert mir to expression
        Expression * result = mirToExpression (mir, &vrToExpression);

        //We insert the mapping into the map. Note that null expression
        //is allowed to be inserted.
        mirToExpressionMap.insert (
                std::pair<MIR *, Expression *> (mir, result));
    }

    return mirToExpressionMap;
}

BinaryExpression * BinaryExpression::mirToExpression (MIR * mir,
        std::map<VirtualRegister *, Expression *> * vrToExpression,
        ExpressionKind expKind)
{
    //If mir is null, we have no expression to build
    if (mir == 0)
    {
        return 0;
    }

    //Get local copy of ssa representation
    SSARepresentation * ssaRep = mir->ssaRep;

    //We cannot build the expression for assignment to without ssa rep
    if (ssaRep == 0)
    {
        return 0;
    }

    //If we don't have at least one definition we cannot create an assignment
    //expression.
    if (ssaRep->numDefs <= 0 || ssaRep->defs == 0)
    {
        return 0;
    }

    //The results of all binary expressions must be assigned to a VR so we create
    //an expression for that first. We must create the VR and thus we don't need
    //a map to look for an expression tree.
    VirtualRegister * assignTo = 0;
    Expression * assignToExpr = 0;

    {
        bool isWide = (ssaRep->numDefs == 1) ? false : true;
        int lowSsaReg = ssaRep->defs[0];
        int highSsaReg = isWide ? ssaRep->defs[1] : 0;

        assignToExpr = VirtualRegister::convertToVR (0, lowSsaReg, highSsaReg,
                isWide);
    }

    if (assignToExpr != 0)
    {
        //Because of how we built it, it should be a virtual register but we
        //are just being paranoid
        if (assignToExpr->isVirtualRegister () == true)
        {
            assignTo = static_cast<VirtualRegister *> (assignToExpr);
        }
    }

    //If we fail at creating the VR we are assigning to, then we cannot
    //complete successfully
    if (assignTo == 0)
    {
        return 0;
    }

    Expression *lhs = 0, *rhs = 0;

    //Since we are generating for a binary expression, there must be two operands
    //Thus we look at the number of uses to figure out which of the following
    //scenarios we are dealing with:
    // 1. One use means one operand is non-wide VR and other is literal
    // 2. Two uses means both operands are non-wide VRs
    // 3. Three uses means first operand is wide VRs while the second one is non-wide VR
    // 4. Four uses means both operands are wide VRs

    if (ssaRep->numUses == 1)
    {
        assert (ssaRep->uses != 0);

        lhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[0]);

        int literalValue = mir->dalvikInsn.vC;
        rhs = ConstantExpression::newExpression (literalValue);
    }
    else if (ssaRep->numUses == 2)
    {
        assert (ssaRep->uses != 0);

        lhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[0]);
        rhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[1]);
    }
    else if (ssaRep->numUses == 3)
    {
        assert (ssaRep->uses != 0);

        lhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[0],
                ssaRep->uses[1], true);
        rhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[2]);
    }
    else if (ssaRep->numUses == 4)
    {
        assert (ssaRep->uses != 0);

        lhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[0],
                ssaRep->uses[1], true);
        rhs = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[2],
                ssaRep->uses[3], true);
    }
    else
    {
        //An assumption we made must be wrong if we get here. In assert world
        //we want to fail if we get here.
        assert (false);

        return 0;
    }

    //If we did not generate operands successfully, then we cannot fully
    //generate the expression.
    if (lhs == 0 || rhs == 0)
    {
        return 0;
    }

    BinaryExpression * result = 0;

    //Now we put together the operands to create a binary expression
    if (expKind != ExpKind_Invalid)
    {
        //In order to create expression, we must first find out the
        //primitive type of the result.
        ExpressionType expType = BytecodeExpression::getExpressionType (
                mir->dalvikInsn.opcode);

        //If we know the type, we can create the binary expression
        if (expType != ExpType_Invalid)
        {
            result = BinaryExpression::newExpression (mir, assignTo, lhs, rhs,
                    expKind, expType);
        }
    }

    //If we have created an expression, we should add its tree to the mapping
    //of VRs to Expressions
    if (result != 0 && vrToExpression != 0)
    {
        vrToExpression->insert (
                std::pair<VirtualRegister *, Expression *> (assignTo, result));
    }

    //Return the expression
    return result;

}

UnaryExpression * UnaryExpression::mirToExpression (MIR * mir,
        std::map<VirtualRegister *, Expression *> * vrToExpression,
        ExpressionKind expKind)
{
    //If mir is null, we have no expression to build
    if (mir == 0)
    {
        return 0;
    }

    //Get local copy of the ssa representation
    SSARepresentation * ssaRep = mir->ssaRep;

    //We cannot build the expression for assignment to without ssa rep
    if (ssaRep == 0)
    {
        return 0;
    }

    //If we don't have at least one definition we cannot create an assignment
    //expression.
    if (ssaRep->numDefs <= 0 || ssaRep->defs == 0)
    {
        return 0;
    }

    //The results of all unary expressions must be assigned to a VR so we create
    //an expression for that first. We must create the VR and thus we don't need
    //a map to look for an expression tree.
    VirtualRegister * assignTo = 0;
    Expression * assignToExpr = 0;

    {
        bool isWide = (ssaRep->numDefs == 1) ? false : true;
        int lowSsaReg = ssaRep->defs[0];
        int highSsaReg = isWide ? ssaRep->defs[1] : 0;

        assignToExpr = VirtualRegister::convertToVR (0, lowSsaReg, highSsaReg,
                isWide);
    }

    if (assignToExpr != 0)
    {
        //Because of how we built it, it should be a virtual register but we
        //are just being paranoid
        if (assignToExpr->isVirtualRegister () == true)
        {
            assignTo = static_cast<VirtualRegister *> (assignToExpr);
        }
    }

    //If we fail at creating the VR we are assigning to, then we cannot
    //complete successfully
    if (assignTo == 0)
    {
        return 0;
    }

    Expression * operand = 0;

    //Since we are generating for a unary expression, there must be one operand.
    //Thus we look at the number of uses to figure out which of the following
    //scenarios we are dealing with:
    // 1. Zero uses mean that we are dealing with either wide or non-wide constant
    // 2. One use means that operand is a non-wide VR
    // 3. Two uses means that operand is a wide VR

    if (ssaRep->numUses == 0)
    {
        bool isWide = false;
        int lowConstant = 0, highConstant = 0;

        bool setsConst = dexGetConstant (mir->dalvikInsn, lowConstant,
                highConstant, isWide);

        //If we have a constant set expression, then we can build
        //a constant expression for the operand
        if (setsConst == true)
        {
            operand = ConstantExpression::newExpression (lowConstant,
                    highConstant, isWide);
        }
    }
    else if (ssaRep->numUses == 1)
    {
        assert(ssaRep->uses != 0);

        operand = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[0]);
    }
    else if (ssaRep->numUses == 2)
    {
        assert(ssaRep->uses != 0);

        operand = VirtualRegister::convertToVR (vrToExpression, ssaRep->uses[0],
                ssaRep->uses[1], true);
    }
    else
    {
        //An assumption we made must be wrong if we get here. In assert world
        //we want to fail if we get here.
        assert (false);

        return 0;
    }

    //If we did not generate operands successfully, then we cannot fully
    //generate the expression
    if (operand == 0)
    {
        return 0;
    }

    UnaryExpression * result = 0;

    //Now we need to create expressions for the operands of the binary expression
    //we are generating.
    if (expKind != ExpKind_Invalid)
    {
        ExpressionType expType = BytecodeExpression::getExpressionType (
                mir->dalvikInsn.opcode);

        //Some of the unary expressions have unknown type until a use.
        //For example, this applies to const bytecodes. Thus we do not
        //check whether expType is invalid.

        result = UnaryExpression::newExpression (mir, assignTo, operand,
                expKind, expType);
    }

    //If we have created an expression, we should add its tree to the mapping
    //of VRs to Expressions
    if (result != 0 && vrToExpression != 0)
    {
        vrToExpression->insert (
                std::pair<VirtualRegister *, Expression *> (assignTo, result));
    }

    //Return the expression
    return result;

}

BinaryExpression * BinaryExpression::newExpression (MIR * mir,
        VirtualRegister * assignTo, Expression * lhs, Expression * rhs,
        ExpressionKind expKind, ExpressionType expType)
{
    //If we don't have all parts of expression, we cannot create it
    if (mir == 0 || assignTo == 0 || lhs == 0 || rhs == 0)
    {
        return 0;
    }

    //Create space for the expression
    BinaryExpression *result = 0;
    void * space = dvmCompilerNew (sizeof (*result), false);

    //Call constructor
    result = new (space) BinaryExpression (assignTo, lhs, rhs, expKind, expType, mir);

    return result;
}

UnaryExpression * UnaryExpression::newExpression (MIR * mir,
        VirtualRegister * assignTo, Expression * operand,
        ExpressionKind expKind, ExpressionType expType)
{
    //If we don't have all parts of expression, we cannot create it
    if (mir == 0 || assignTo == 0 || operand == 0)
    {
        return 0;
    }

    //Create space for the expression
    UnaryExpression *result = 0;
    void * space = dvmCompilerNew (sizeof (*result), false);

    //Call constructor
    result = new (space) UnaryExpression (assignTo, operand, expKind, expType, mir);

    return result;
}

ConstantExpression * ConstantExpression::newExpression (int32_t lowInitialValue,
        int32_t highInitialValue, bool wide)
{
    ConstantExpression *result = 0;

    //Create space for a new constant
    void * space = dvmCompilerNew (sizeof (*result), false);

    //Call constructor using given initial value
    if (wide == false)
    {
        result = new (space) ConstantExpression (lowInitialValue);
    }
    else
    {
        result = new (space) ConstantExpression (lowInitialValue,
                highInitialValue);
    }

    return result;
}

Expression * VirtualRegister::convertToVR (
        std::map<VirtualRegister *, Expression *> * vrToExpression,
        int lowSsaReg, int highSsaReg, bool wide)
{
    VirtualRegister *result = 0;

    //Create space for a VR
    void * space = dvmCompilerNew (sizeof (*result), false);

    //Call constructor
    if (wide == false)
    {
        result = new (space) VirtualRegister (lowSsaReg);
    }
    else
    {
        result = new (space) VirtualRegister (lowSsaReg, highSsaReg);
    }

    //Look to see if we have an existing expression for this VR
    Expression * existingExpression = result->findExpressionForVR (
            vrToExpression);

    //If we have an existing expression, return that instead
    if (existingExpression != 0)
    {
        return existingExpression;
    }
    else
    {
        return result;
    }
}

Expression * VirtualRegister::findExpressionForVR (
        std::map<VirtualRegister *, Expression *> * vrToExpression)
{
    //If the mapping doesn't exist, then there is nothing to find
    if (vrToExpression == 0)
    {
        return 0;
    }

    std::map<VirtualRegister *, Expression *>::const_iterator iter;

    //Look for an expression for the VR
    iter = std::find_if (vrToExpression->begin (), vrToExpression->end (),
            VirtualRegisterMappingComparator (this));

    //If we didn't find an expression in the map for this VR, return 0.
    //Otherwise, return the expression we found.
    if (iter == vrToExpression->end ())
    {
        return 0;
    }
    else
    {
        return iter->second;
    }
}

bool VirtualRegisterMappingComparator::operator() (
        const std::pair<VirtualRegister *, Expression *> vrToExpressionMapping)
{
    VirtualRegister * toCompareWith = vrToExpressionMapping.first;

    if (toCompareWith == 0)
    {
        return false;
    }

    //If wideness does not match, the VRs are not equal
    if (holder->isWide () != toCompareWith->isWide ())
    {
        return false;
    }

    //If low ssa reg does not match, then the VRs are not equal
    if (holder->lowSsaReg != toCompareWith->lowSsaReg)
    {
        return false;
    }

    //If high ssa reg does not match, then the VRs are not equal
    if (holder->highSsaReg != toCompareWith->highSsaReg)
    {
        return false;
    }

    //If we made it this far, must be that the VRs are equal
    return true;
}

MIR * BytecodeExpression::createMir (ExpressionKind expKind,
        ExpressionType expType, int assignToVR, int lhsVR, int rhsVR)
{
    MIR * mir = 0;

    //This method supports only non-wide VRs and thus only supports
    //creating float and int MIRs
    if (expType != ExpType_Int && expType != ExpType_Float)
    {
        return 0;
    }

    //Assume we will find an opcode we can use
    bool foundOpcode = true;
    Opcode opcode;

    //As an enhancement to this logic, we could also allow 2addr forms
    //to be used.
    switch (expKind)
    {
        case ExpKind_Add:
            opcode = (expType == ExpType_Int) ? OP_ADD_INT : OP_ADD_FLOAT;
            break;
        case ExpKind_Sub:
            opcode = (expType == ExpType_Int) ? OP_SUB_INT : OP_SUB_FLOAT;
            break;
        case ExpKind_Mul:
            opcode = (expType == ExpType_Int) ? OP_MUL_INT : OP_MUL_FLOAT;
            break;
        default:
            foundOpcode = false;
            break;
    }

    //If we didn't find an opcode we cannot generate a MIR
    if (foundOpcode == false)
    {
        return 0;
    }

    //Create the MIR and assign the fields
    mir = dvmCompilerNewMIR ();
    mir->dalvikInsn.opcode = opcode;
    mir->dalvikInsn.vA = assignToVR;
    mir->dalvikInsn.vB = lhsVR;
    mir->dalvikInsn.vC = rhsVR;

    return mir;

}

ExpressionType BytecodeExpression::getExpressionType (Opcode dalvikOpcode)
{
    ExpressionType expType;

    switch (dalvikOpcode)
    {
        case OP_NEG_INT:
        case OP_NOT_INT:
        case OP_LONG_TO_INT:
        case OP_FLOAT_TO_INT:
        case OP_DOUBLE_TO_INT:
        case OP_INT_TO_BYTE:
        case OP_INT_TO_CHAR:
        case OP_INT_TO_SHORT:
        case OP_ADD_INT:
        case OP_SUB_INT:
        case OP_MUL_INT:
        case OP_DIV_INT:
        case OP_REM_INT:
        case OP_AND_INT:
        case OP_OR_INT:
        case OP_XOR_INT:
        case OP_SHL_INT:
        case OP_SHR_INT:
        case OP_ADD_INT_2ADDR:
        case OP_SUB_INT_2ADDR:
        case OP_MUL_INT_2ADDR:
        case OP_DIV_INT_2ADDR:
        case OP_REM_INT_2ADDR:
        case OP_AND_INT_2ADDR:
        case OP_OR_INT_2ADDR:
        case OP_XOR_INT_2ADDR:
        case OP_SHL_INT_2ADDR:
        case OP_SHR_INT_2ADDR:
        case OP_USHR_INT_2ADDR:
        case OP_ADD_INT_LIT16:
        case OP_RSUB_INT:
        case OP_MUL_INT_LIT16:
        case OP_DIV_INT_LIT16:
        case OP_REM_INT_LIT16:
        case OP_AND_INT_LIT16:
        case OP_OR_INT_LIT16:
        case OP_XOR_INT_LIT16:
        case OP_ADD_INT_LIT8:
        case OP_RSUB_INT_LIT8:
        case OP_MUL_INT_LIT8:
        case OP_DIV_INT_LIT8:
        case OP_REM_INT_LIT8:
        case OP_AND_INT_LIT8:
        case OP_OR_INT_LIT8:
        case OP_XOR_INT_LIT8:
        case OP_SHL_INT_LIT8:
        case OP_SHR_INT_LIT8:
        case OP_USHR_INT_LIT8:
            expType = ExpType_Int;
            break;
        case OP_NEG_LONG:
        case OP_NOT_LONG:
        case OP_INT_TO_LONG:
        case OP_FLOAT_TO_LONG:
        case OP_DOUBLE_TO_LONG:
        case OP_USHR_INT:
        case OP_ADD_LONG:
        case OP_SUB_LONG:
        case OP_MUL_LONG:
        case OP_DIV_LONG:
        case OP_REM_LONG:
        case OP_AND_LONG:
        case OP_OR_LONG:
        case OP_XOR_LONG:
        case OP_SHL_LONG:
        case OP_SHR_LONG:
        case OP_USHR_LONG:
        case OP_ADD_LONG_2ADDR:
        case OP_SUB_LONG_2ADDR:
        case OP_MUL_LONG_2ADDR:
        case OP_DIV_LONG_2ADDR:
        case OP_REM_LONG_2ADDR:
        case OP_AND_LONG_2ADDR:
        case OP_OR_LONG_2ADDR:
        case OP_XOR_LONG_2ADDR:
        case OP_SHL_LONG_2ADDR:
        case OP_SHR_LONG_2ADDR:
        case OP_USHR_LONG_2ADDR:
            expType = ExpType_Long;
            break;
        case OP_NEG_FLOAT:
        case OP_INT_TO_FLOAT:
        case OP_LONG_TO_FLOAT:
        case OP_DOUBLE_TO_FLOAT:
        case OP_ADD_FLOAT:
        case OP_SUB_FLOAT:
        case OP_MUL_FLOAT:
        case OP_DIV_FLOAT:
        case OP_REM_FLOAT:
        case OP_ADD_FLOAT_2ADDR:
        case OP_SUB_FLOAT_2ADDR:
        case OP_MUL_FLOAT_2ADDR:
        case OP_DIV_FLOAT_2ADDR:
        case OP_REM_FLOAT_2ADDR:
            expType = ExpType_Float;
            break;
        case OP_NEG_DOUBLE:
        case OP_INT_TO_DOUBLE:
        case OP_LONG_TO_DOUBLE:
        case OP_FLOAT_TO_DOUBLE:
        case OP_ADD_DOUBLE:
        case OP_SUB_DOUBLE:
        case OP_MUL_DOUBLE:
        case OP_DIV_DOUBLE:
        case OP_REM_DOUBLE:
        case OP_ADD_DOUBLE_2ADDR:
        case OP_SUB_DOUBLE_2ADDR:
        case OP_MUL_DOUBLE_2ADDR:
        case OP_DIV_DOUBLE_2ADDR:
        case OP_REM_DOUBLE_2ADDR:
            expType = ExpType_Double;
            break;
        default:
            expType = ExpType_Invalid;
            break;
    }

    return expType;
}
