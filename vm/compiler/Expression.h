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

#ifndef EXPRESSION_H_
#define EXPRESSION_H_

#include <string>
#include <vector>
#include <map>
#include "Dataflow.h"

// Forward declarations
struct MIR;
class VirtualRegister;
class VirtualRegisterMappingComparator;
struct CompilationUnit;

/**
 * @brief Defines kind of expression depending on number of operands
 * and semantics of operation.
 * @details There is a 1:N mapping between this an ExpressionKind.
 */
enum GeneralExpressionKind
{
    GenExpKind_NoExp,     //!< No expression
    GenExpKind_UnaryExp,  //!< Unary expression
    GenExpKind_BinaryExp, //!< Binary expression
};

/**
 * @brief Defines kind of expression depending on operation.
 */
enum ExpressionKind
{
    ExpKind_Invalid = 0,  //!< Invalid kind (noexp)
    ExpKind_ConstSet,     //!< Constant set bytecode (unop)
    ExpKind_Add,          //!< Addition (binop)
    ExpKind_Sub,          //!< Subtraction (binop)
    ExpKind_Mul,          //!< Multiplication (binop)
    ExpKind_Phi,          //!< Phi node (binop)
    ExpKind_Cast,         //!< Cast (unop)
    ExpKind_Div,          //!< Division (binop)
    ExpKind_Rem,          //!< Remainder (binop)
    ExpKind_And,          //!< And (binop)
    ExpKind_Or,           //!< Or (binop)
    ExpKind_Xor,          //!< Xor (binop)
    ExpKind_Shl,          //!< Shift left (binop)
    ExpKind_Shr,          //!< Signed shift right (binop)
    ExpKind_Ushr,         //!< Unsigned shift right (binop)
};

/**
 * @brief Defines type of expression depending on primitive type
 * of result after operation is applied to operands.
 */
enum ExpressionType
{
    ExpType_Invalid = 0,  //!< invalid type
    ExpType_Int,          //!< expression represents operation on int (32-bit)
    ExpType_Long,         //!< expression represents operation on long (64-bit)
    ExpType_Float,        //!< expression represents operation on float (32-bit)
    ExpType_Double,       //!< expression represents operation on double (64-bit)
};

/**
 * @brief Defines result of linear accumulation check.
 */
enum LinearAccumulationCheckResult
{
    LinAccResVRNotSeen,
    LinAccResVRSeen,
    LinAccResError
};


/**
 * @brief Base virtual class for representing an expression.
 */
class Expression
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Expression (void)
    {
    }

    /**
     * @brief Converts expression to string representation.
     * @param cUnit the compilation unit.
     * @return string representation.
     */
    virtual std::string toString (const CompilationUnit * cUnit) = 0;

    /**
     * @brief Used to get a list of children.
     * @return Returns vector of child expressions.
     */
    virtual std::vector<Expression *> getChildren (void) const
    {
        return std::vector<Expression *> ();
    }

    /**
     * @brief Returns true if expression represents a dalvik bytecode.
     */
    virtual bool isBytecodeExpression (void) const
    {
        return false;
    }

    /**
     * @brief Returns true if expression is a literal.
     */
    virtual bool isConstant (void) const
    {
        return false;
    }

    /**
     * @brief Returns true if expression evaluates to a constant value.
     */
    virtual bool evaluatesToConstant (void) const
    {
        return false;
    }

    /**
     * @brief Returns true if expression represent a virtual register.
     */
    virtual bool isVirtualRegister (void) const
    {
        return false;
    }

    /**
     * @brief Converts a MIR to an expression.
     * @details Uses vrToExpression in order to find expressions for the
     * operands of the MIR in order to create an expression tree.
     * @param mir Dalvik bytecode.
     * @param vrToExpression Map of virtual registers and corresponding
     * expression that was used to assign a value to it. Only expressions
     * that are unary or binary expressions can be in this list since others
     * do not assign to a VR.
     * @return Returns expression representing the MIR or null if
     * conversion is not successful.
     */
    static Expression * mirToExpression (MIR * mir,
            std::map<VirtualRegister *, Expression *> * vrToExpression = 0);

    /**
     * @brief Converts a list of MIRs to expressions.
     * @details Takes the MIRs in order and converts them to expression.
     * If during conversion of a MIR we find that we have already generated
     * an expression for another MIR that sets the current operand, we use
     * the other expression to create an expression tree.
     * @param listOfMirs Vector of MIRs.
     * @return Returns a map of each MIR to its corresponding expression.
     * If conversion was not successful, the map will contain a null
     * expression for that MIR.
     */
    static std::map<MIR *, Expression *> mirsToExpressions (
            const std::vector<MIR *> & listOfMirs);

    /**
     * @brief Test the expression to be a linear accumulation. In other words
     * expression looks like v += f() where f() does not depend on v.
     * v is virtual register this expression is assigned to.
     * @param cUnit CompilationUnit
     * @return true if expression is linear accumulation.
     */
    bool isLinearAccumulation (const CompilationUnit * cUnit);

    /**
     * @brief Test the expression to be a linear accumulation with respect to VR.
     * @param cUnit CompilationUnit
     * @param VR test accumalation for this VR
     * @return Return result of the check
     */
    virtual LinearAccumulationCheckResult isLinearAccumulation (const CompilationUnit * cUnit, int VR) = 0;

protected:
    /**
     * @brief Constructor.
     */
    Expression (void)
    {
    }

};

/**
 * @brief Expression used for representing wide and non-wide virtual registers.
 */
class VirtualRegister: public Expression
{
public:
    /**
     * @brief Non-wide virtual register constructor
     * @param ssaReg the ssa register representing the VR.
     */
    VirtualRegister (int ssaReg) :
            Expression (), lowSsaReg (ssaReg), highSsaReg (0), wide (false)
    {
    }

    /**
     * @brief Wide virtual register constructor
     * @param lowSsaReg the ssa register representing the low VR
     * @param highSsaReg the ssa register representing the high VR
     */
    VirtualRegister (int lowSsaReg, int highSsaReg) :
            Expression (), lowSsaReg (lowSsaReg), highSsaReg (highSsaReg), wide (
                    true)
    {
    }

    /**
     * @brief Destructor
     */
    virtual ~VirtualRegister (void)
    {
    }

    /**
     * @brief Always returns true since class represents virtual register.
     */
    bool isVirtualRegister (void) const
    {
        return true;
    }

    /**
     * @brief Returns whether the virtual register represented is wide or not.
     */
    bool isWide (void) const
    {
        return wide;
    }

    /**
     * @brief Get the lowSsaReg of the VR corresponding to the class
     * @return The lowSSA value
     */
    int getLowSSAReg (void) const
    {
        return lowSsaReg;
    }

    /**
     * @brief Get the highSSA value, if this is a wide VR
     * @return highSSA value if wide, else -1
     */
    int getHighSSAReg (void) const
    {
        return (wide == true) ? highSsaReg : -1;
    }

    /**
     * @brief Converts one of the ssa registers (or two for wide case) to
     * representation of virtual register.
     * @details Once a virtual register is created, we then look through the
     * vrToExpression map and if we find a match of an expression for that VR,
     * we return that instead.
     * @param vrToExpression Map of virtual registers to expressions used to
     * assign to them. Allowed to be null.
     * @param lowSsaReg the ssa register representing the low VR
     * @param highSsaReg the ssa register representing the high VR
     * @param wide whether we are to construct a wide VR
     * @return Returns expression tree representing the operand. It is either
     * a virtual register or an expression tree that was used to assign to it.
     */
    static Expression * convertToVR (
            std::map<VirtualRegister *, Expression *> * vrToExpression,
            int lowSsaReg, int highSsaReg = 0, bool wide = false);

    /**
     * @brief Converts virtual register to string representation.
     * @param cUnit the compilation unit.
     * @return string representation.
     */
    std::string toString (const CompilationUnit * cUnit);

    /**
     * @brief Report whether this covers VR
     */
    virtual LinearAccumulationCheckResult isLinearAccumulation (const CompilationUnit * cUnit, int VR);

    /**
     * @brief The comparator must be able to access the fields of the
     * virtual register.
     */
    friend class VirtualRegisterMappingComparator;

protected:

    /**
     * @brief Low ssa register.
     */
    int lowSsaReg;

    /**
     * @brief High ssa register (only applicable if wide).
     */
    int highSsaReg;

    /**
     * @brief Wideness of the virtual register.
     */
    bool wide;

private:
    /**
     * @brief Default Constructor
     * @details Disabled by making it private.
     */
    VirtualRegister (void);

    /**
     * @brief Helper method to look for an expression for self.
     * @param vrToExpression Map of virtual registers to expression trees.
     * @return Returns found expression. If nothing was found, returns 0.
     */
    Expression * findExpressionForVR (
            std::map<VirtualRegister *, Expression *> * vrToExpression);
};

/**
 * @brief Helper class for being able to compare VirtualRegister instances
 * for equality.
 */
class VirtualRegisterMappingComparator: std::unary_function<VirtualRegister *,
        bool>
{
public:
    /**
     * @brief Constructor.
     * @param vR Virtual register we want to compare against.
     */
    VirtualRegisterMappingComparator (VirtualRegister * vR)
    {
        holder = vR;
    }

    /**
     * @brief Functor used for checking for equality among two VRs.
     * @param vrToExpressionMapping Pair of virtual register to expression. Only
     * key is used for comparison.
     * @return Returns true if key from pair matches the virtual register
     * we are looking for.
     */
    bool operator() (
            const std::pair<VirtualRegister *, Expression *> vrToExpressionMapping);

private:
    /**
     * @brief Used to keep track of the virtual register we are comparing other
     * instances against in order to find a match.
     */
    VirtualRegister * holder;
};

/**
 * @brief Virtual class used to represent dalvik bytecodes.
 * @details There is a 1:1 mapping between a Dalvik bytecode and a
 * BytecodeExpression
 */
class BytecodeExpression: public Expression
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~BytecodeExpression (void)
    {
    }

    /**
     * @brief Always returns true since this is a bytecode expression.
     */
    bool isBytecodeExpression (void) const
    {
        return true;
    }

    /**
     * @brief Returns virtual register that the bytecode expression
     * assigns to.
     */
    virtual VirtualRegister * getAssignmentTo (void) const
    {
        return this->assignmentTo;
    }

    /**
     * @brief Get the associated MIR
     * @return The MIR associated with the Expression.
     */
    virtual MIR * getMir (void) const
    {
        return mir;
    }

    /**
     * @brief Returns expression kind.
     */
    virtual ExpressionKind getExpressionKind (void) const
    {
        return expKind;
    }

    /**
     * @brief Returns expression type depending on type of result
     * and not on the operands.
     */
    virtual ExpressionType getExpressionType (void) const
    {
        return expType;
    }

    /**
     * @brief Used to create a MIR when given parameters that can build
     * expression. This can only be used for float and int versions.
     * @param expKind Expression kind
     * @param expType Expression type
     * @param assignToVR Dalvik register we are assigning to (not ssa)
     * @param lhsVR Dalvik register for lhs operand
     * @param rhsVR Dalvik register for rhs operand
     * @return MIR that was created. If no MIR was created, returns 0.
     */
    static MIR * createMir (ExpressionKind expKind, ExpressionType expType,
            int assignToVR, int lhsVR, int rhsVR = 0);

protected:
    /**
     * @brief Constructor of bytecode expression
     * @param assignTo Virtual register that expression assigns to.
     * Cannot be null.
     * @param kind Expression kind
     * @param type Expression type
     * @param insn MIR associated with expression. Cannot be null.
     */
    BytecodeExpression (VirtualRegister * assignTo, ExpressionKind kind, ExpressionType type, MIR * insn) :
            Expression (), assignmentTo (assignTo), mir (insn), expKind (kind), expType (type)
    {
        assert(assignTo != 0);
        assert(insn != 0);
    }

    /**
     * @brief Keeps track of virtual register we are assigning to.
     * @details Dalvik bytecodes always have a virtual register that the result
     * is assigned to. In order to simplify dealing with expression tree, we
     * keep the assignment to as part of the b
     */
    VirtualRegister * assignmentTo;

    /**
     * @brief Associated MIR.
     */
    MIR * mir;

    /**
     * @brief Keeps track of expression kind of this expression.
     */
    ExpressionKind expKind;

    /**
     * @brief Defines the type of the result of the operation. Namely, this defines
     * type of assignmentTo field in how it is intended to be interpreted.
     */
    ExpressionType expType;

    /**
     * @brief Returns general expression kind (noexp, unexp, or binexp) for
     * the dalvik opcode.
     * @param dalvikOpcode dalvik opcode
     */
    static GeneralExpressionKind getGenExpressionKind (Opcode dalvikOpcode);

    /**
     * @brief Returns expression kind for the dalvik opcode.
     * @param dalvikOpcode dalvik opcode
     */
    static ExpressionKind getExpressionKind (Opcode dalvikOpcode);

    /**
     * @brief Returns expression type for the dalvik opcode.
     * @param dalvikOpcode dalvik opcode
     */
    static ExpressionType getExpressionType (Opcode dalvikOpcode);

private:
    /**
     * @brief Default Constructor
     * @details Disabled by making it private.
     */
    BytecodeExpression (void);
};

/**
 * @brief Used to represent a bytecode expression which has two operands.
 * @details Used with bytecodes of form "binop vAA, vBB, vCC",
 * "binop/2addr vA, vB", "binop/lit16 vA, vB, #+CCCC", and
 * "binop/lit8 vAA, vBB, #+CC"
 */
class BinaryExpression: public BytecodeExpression
{
public:
    /**
     * @brief Constructor for binary bytecode expression.
     * @param assignTo Virtual register that expression assigns to.  Cannot be null.
     * @param lhs Expression tree for the lhs operand. Cannot be null.
     * @param rhs Expression tree for the rhs operand. Cannot be null.
     * @param kind Expression kind
     * @param type Expression type
     * @param mir MIR associated with expression. Cannot be null.
     */
    BinaryExpression (VirtualRegister * assignTo, Expression * lhs, Expression * rhs, ExpressionKind kind, ExpressionType type, MIR * mir) :
            BytecodeExpression (assignTo, kind, type, mir), lhs (lhs), rhs (rhs)
    {
        assert(lhs != 0);
        assert(rhs != 0);
    }

    /**
     * @brief Destructor
     */
    virtual ~BinaryExpression (void)
    {
    }

    /**
     * @brief Returns the lhs expression.
     */
    Expression * getLhs (void) const
    {
        return this->lhs;
    }

    /**
     * @brief Returns the rhs expression.
     */
    Expression * getRhs (void) const
    {
        return this->rhs;
    }

    /**
     * @brief Returns whether both of the operands evaluate to a constant.
     */
    bool evaluatesToConstant (void) const
    {
        return lhs->evaluatesToConstant () && rhs->evaluatesToConstant ();
    }

    /**
     * @brief Returns a vector which contains both operands.
     */
    std::vector<Expression *> getChildren (void) const;

    /**
     * @brief Converts binary expression to string representation.
     * @param cUnit the compilation unit.
     * @return string representation.
     */
    std::string toString (const CompilationUnit * cUnit);

    /**
     * @brief Converts a MIR to expression representation.
     * @param mir The dalvik MIR to convert.
     * @param vrToExpression Map of virtual registers and corresponding
     * expression that was used to assign a value to it. Note that this
     * is updated by function when new expression is successfully created.
     * @param expKind Expression kind
     * @return Newly created expression. If failed to create, returns 0.
     */
    static BinaryExpression * mirToExpression (MIR * mir,
            std::map<VirtualRegister *, Expression *> * vrToExpression,
            ExpressionKind expKind);

    /**
     * @brief Creates an instance of BinaryExpression in arena space.
     * @param mir MIR associated with expression. Cannot be null.
     * @param assignTo Virtual register that expression assigns to.
     * @param lhs Expression tree for the lhs operand.
     * @param rhs Expression tree for the rhs operand.
     * @param expKind Expression kind
     * @param expType Expression type
     * @return Returns newly created expression or 0 when one cannot
     * be created from the given arguments.
     */
    static BinaryExpression * newExpression (MIR * mir,
            VirtualRegister * assignTo, Expression * lhs, Expression * rhs,
            ExpressionKind expKind, ExpressionType expType);

protected:
    /**
     * @details Result depends on virtual register this expression is assigned to (A) and
     * whether the operation the expression represents is addition (O).
     * The rule is the following:
     * A  & O  => VR should be seen exactly in one child
     * !A & O  => VR may be seen in children not more than once
     * A  & !O => Fail
     * !A & !O => VR should not be seen in children
     * The function returns error or whether the VR has been seen
     */
    virtual LinearAccumulationCheckResult isLinearAccumulation (const CompilationUnit * cUnit, int VR);

    /**
     * @brief Keeps track of the lhs operand.
     */
    Expression * lhs;

    /**
     * @brief Keeps track of the rhs operand.
     */
    Expression * rhs;

private:
    /**
     * @brief Default Constructor
     * @details Disabled by making it private.
     */
    BinaryExpression (void);
};

/**
 * @brief
 * @details Used with bytecodes of form "unop vA, vB", const, and move
 */
class UnaryExpression: public BytecodeExpression
{
public:
    /**
     * @brief Constructor for unary bytecode expression.
     * @param assignTo Virtual register that expression assigns to.
     * Cannot be null.
     * @param operand Expression tree for the operand. Cannot be null.
     * @param expKind Expression kind
     * @param expType Expression type
     * @param mir MIR associated with expression. Cannot be null.
     */
    UnaryExpression (VirtualRegister * assignTo, Expression * operand,
            ExpressionKind expKind, ExpressionType expType, MIR * mir) :
            BytecodeExpression (assignTo, expKind, expType, mir), operand (
                    operand)
    {
        assert(operand != 0);
    }

    /**
     * @brief Destructor.
     */
    virtual ~UnaryExpression (void)
    {
    }

    /**
     * @brief Returns whether the operand evaluates to a constant.
     */
    bool evaluatesToConstant (void) const
    {
        return operand->evaluatesToConstant ();
    }

    /**
     * @brief Returns a vector containing the single operand.
     */
    std::vector<Expression *> getChildren (void) const;

    /**
     * @brief Converts unary expression to string representation.
     * @param cUnit the compilation unit.
     * @return string representation.
     */
    std::string toString (const CompilationUnit * cUnit);

    /**
     * @brief Creates an instance of UnaryExpression in arena space.
     * @param mir MIR associated with expression. Cannot be null.
     * @param assignTo Virtual register that expression assigns to.
     * @param operand Expression tree for the operand.
     * @param expKind Expression kind
     * @param expType Expression type
     * @return Returns newly created expression or 0 when one cannot
     * be created from the given arguments.
     */
    static UnaryExpression * newExpression (MIR * mir,
            VirtualRegister * assignTo, Expression * operand,
            ExpressionKind expKind, ExpressionType expType);

    /**
     * @brief Converts a MIR to expression representation.
     * @param mir The dalvik MIR to convert.
     * @param vrToExpression Map of virtual registers and corresponding
     * expression that was used to assign a value to it. Note that this
     * is updated by function when new expression is successfully created.
     * @param expKind Expression kind
     * @return Newly created expression. If failed to create, returns 0.
     */
    static UnaryExpression * mirToExpression (MIR * mir,
            std::map<VirtualRegister *, Expression *> * vrToExpression,
            ExpressionKind expKind);

protected:
    /**
     * @brief returns error if VR is seen in unary expression.
     */
    virtual LinearAccumulationCheckResult isLinearAccumulation (const CompilationUnit * cUnit, int VR);

    /**
     * @brief Used to keep track of the expression tree of operand.
     */
    Expression * operand;

private:
    /**
     * @brief Default Constructor
     * @details Disabled by making it private.
     */
    UnaryExpression (void);
};

/**
 * @brief Expression used to represent a constant.
 */
class ConstantExpression: public Expression
{
public:
    /**
     * @brief Constructor of non-wide constant.
     * @param constant constant represented as 32-bit integer.
     */
    ConstantExpression (int32_t constant) :
            wide (false)
    {
        value = static_cast<int64_t> (constant);
    }

    /**
     * @brief Constructor of wide constant.
     * @param lowConstant constant of low bits represented as 32-bit integer.
     * @param highConstant constant of high bits represented as 32-bit integer.
     */
    ConstantExpression (int32_t lowConstant, int32_t highConstant) :
            wide (true)
    {
        value = (static_cast<int64_t> (highConstant) << 32)
                | (static_cast<int64_t> (lowConstant));
    }

    /**
     * @brief Constructor of non-wide constant.
     * @param constant constant represented as 32-bit floating point value.
     */
    ConstantExpression (float constant) :
            wide (false)
    {
        //We need to break strict aliasing rules so we quiet down compiler
        //by using char* first.
        char *ptr = reinterpret_cast<char *> (&constant);

        value = static_cast<int64_t> (*reinterpret_cast<int32_t*> (ptr));
    }

    /**
     * @brief Constructor of wide constant.
     * @param constant constant represented as 64-bit floating point value.
     */
    ConstantExpression (double constant) :
            wide (true)
    {
        //We need to break strict aliasing rules so we quiet down compiler
        //by using char* first.
        char *ptr = reinterpret_cast<char *> (&constant);

        value = *reinterpret_cast<int64_t*> (ptr);
    }

    /**
     * @brief Constructor of wide constant.
     * @param constant constant represented as 64-bit integer.
     */
    ConstantExpression (int64_t constant) :
            wide (true)
    {
        value = constant;
    }

    /**
     * @brief Destructor.
     */
    virtual ~ConstantExpression (void)
    {
    }

    /**
     * @brief Always returns true because this represents a constant.
     */
    bool isConstant (void) const
    {
        return true;
    }

    /**
     * @brief Always returns true because this evaluates to a constant
     */
    bool evaluatesToConstant (void) const
    {
        return true;
    }


    /**
     * @brief Returns whether the constant is wide (64-bit) or not.
     */
    bool isWide (void) const
    {
        return wide;
    }

    /**
     * @brief Returns constant value into desired primitive type.
     * @tparam Desired type of constant value
     */
    template<typename desired_type>
    desired_type getValue (void)
    {
        //We need to break strict aliasing rules so we quiet down compiler
        //by using char* first.
        char *ptr = reinterpret_cast<char *> (&value);

        //Since the backing store is 64-bit integer, we need to
        //reinterpret to desired type.
        return *reinterpret_cast<desired_type *> (ptr);
    }

    /**
     * @brief Converts constant to string representation.
     * @details Since it is not known how the value will be interpreted,
     * everything gets printed as a 64-bit integer.
     * @param cUnit the compilation unit.
     * @return string representation.
     */
    std::string toString (const CompilationUnit * cUnit);

    /**
     * @brief Used to create a ConstantExpression using arena.
     * @param lowInitialValue The constant value of low bits.
     * @param highInitialValue The constant value of high bits (wide
     * must be true)
     * @param wide Whether we are creating a wide (64-bit) constant.
     * @return Returns the constant expression instance created.
     */
    static ConstantExpression * newExpression (int32_t lowInitialValue,
            int32_t highInitialValue = 0, bool wide = false);

protected:
    /**
     * @brief Always returns LinAccResVRNotSeen.
     */
    virtual LinearAccumulationCheckResult isLinearAccumulation (const CompilationUnit * cUnit, int VR)
    {
        return LinAccResVRNotSeen;
    }


private:
    /**
     * @brief Default Constructor
     * @details Disabled by making it private.
     */
    ConstantExpression (void);

    /**
     * @brief Backing store for constant value since we don't know how it
     * will be used when we create it.
     */
    int64_t value;

    /**
     * @brief Used to keep track of wideness of constant.
     */
    bool wide;
};

#endif /* EXPRESSION_H_ */
