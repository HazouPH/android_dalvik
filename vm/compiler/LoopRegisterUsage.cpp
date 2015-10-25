/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CompilerIR.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "LoopInformation.h"
#include "LoopRegisterUsage.h"
#include "Pass.h"
#include <set>

/**
 * @brief Mark the instruction as a variant and set all the defines in the variant BitVector
 * @param current the MIR instruction
 * @param variants the variant BitVector
 */
static void markMIRAsVariant (MIR *current, BitVector *variants)
{
    current->invariant = false;

    //Go through the defs
    SSARepresentation *ssaRep = current->ssaRep;

    //Paranoid
    assert (ssaRep);

    //Go through the defs and set the bits
    int numDefs = ssaRep->numDefs;
    for (int i = 0; i < numDefs; i++)
    {
        dvmSetBit (variants, ssaRep->defs[i]);
    }
}

/**
 * @brief Handle a MIR to update the variant bitvector for setters and getters
 * @param highest the highest MIR instruction in the color link list
 * @param variants the variant BitVector
 * @param forceGetterSetterAsVariant output whether all setters and getters should be marked as variant
 * @return whether highest MIR was marked as variant
 */
static bool handleChainOfTheSameColor (MIR *highest, BitVector *variants, bool& forceGetterSetterAsVariant)
{
    /*
     The algorithm in this function is conservative: if there is a getter AND a setter in the color:
      - Every MIR in the color is set as variant
     A refinement for it would actually be:
      - If there is a setter, check if what it is setting is the same as what you got
        That would handle the case:
          int val = tab[i];
          ... use of val but never redefining it
          tab[i] = val;
     */
    bool haveGetter = false;
    bool haveSetter = false;

    //Walk the color link list to find a getter/setter, at the same time, find out if the uses would make it an invariant or not
    MIR *current = highest;

    while (current != 0)
    {
        //Get the opcode
        int opcode = current->dalvikInsn.opcode;

        //Get flags for it
        long long dfAttributes = dvmCompilerDataFlowAttributes[opcode];

        //Check if getter/setter
        if ( (dfAttributes & DF_IS_SETTER) != 0)
        {
            haveSetter = true;
        }

        if ( (dfAttributes & DF_IS_GETTER) != 0)
        {
            haveGetter = true;
        }

        // Volatile get/put cannot be invariant. The value can be changed any time
        // by other thread and we should see this change immidiately
        if (dvmCompilerIsOpcodeVolatile (current->dalvikInsn.opcode) == true)
        {
            forceGetterSetterAsVariant = true;

            //We can break here, we are done in this case
            break;
        }

        current = current->color.next;
    }

    //If the color had one variant or if it had a setter and a getter, all become variants
    bool isVariant = (forceGetterSetterAsVariant == true) || (haveGetter == true && haveSetter == true);

    if (isVariant == true)
    {
        //Walk the color again
        for (MIR *current = highest; current != 0; current = current->color.next)
        {
            markMIRAsVariant (current, variants);
        }
    }

    return isVariant;
}

/**
 * @brief Compare constants between two MIRs
 * @param first the first DecodedInstruction
 * @param second the second DecodedInstruction
 * @return whether the two instructions are identical regarding constants
 */
static bool compareConstants (const DecodedInstruction &first, const DecodedInstruction &second)
{
    //Get the associated flags
    long long fflags = dvmCompilerDataFlowAttributes[first.opcode];
    long long sflags = dvmCompilerDataFlowAttributes[second.opcode];

    //We have a special case for getter and setters and all the others
    //First test is that the fact that first and second are either a setter/getter is the same
    int firstMask = ( (fflags & (DF_IS_GETTER | DF_IS_SETTER)) != 0);
    int secondMask = ( (sflags & (DF_IS_GETTER | DF_IS_SETTER)) != 0);

    //If not, we are done
    if (firstMask != secondMask)
    {
        return false;
    }

    //If it's constants, let's get them
    int firstLowConst = 0, secondLowConst = 0, firstHighConst = 0, secondHighConst = 0;
    bool firstIsWide = 0, secondIsWide = 0;

    bool firstIsConst = dexGetConstant (first, firstLowConst, firstHighConst, firstIsWide);
    bool secondIsConst = dexGetConstant (second, secondLowConst, secondHighConst, secondIsWide);

    //If the booleans aren't the same value bail
    if (firstIsConst != secondIsConst || firstIsWide != secondIsWide)
    {
        return false;
    }

    //If they are constants though, we can just look at the values
    if (firstIsConst == true)
    {
        //Compare low and, if it is wide, look at high
        //We know that secondIsWide == firstIsWide at this point
        bool res = (firstLowConst == secondLowConst) && (secondIsWide == false || firstHighConst == secondHighConst);
        return res;
    }

    //Let us separate the case of different opcode and are getter or setters
    if ( (firstMask != 0) && (first.opcode != second.opcode))
    {
        //We now know that one is a getter and the other is a setter
        //To simplify this code greatly, let's force the GETTER to be first
        if ( (fflags & DF_IS_GETTER) == 0)
        {
            return compareConstants (second, first);
        }
        else
        {
            //Ok we now know that first is a getter, and second is a setter
            //The way opcodes are paired prove:
            //  vA/vB/vC will be used by both or neither, so just check first
            //If vC is used, we are done (no constants)
            //If not:
            //  If vB is not used, vB is our constant
            //  If not, vC is our constant

            //The above code basically handles the difference between Agets/Aputs, Igets/Iputs, and Sgets/Sputs

            //Check vC
            int mask = (fflags & (DF_UC | DF_UC_WIDE));

            if (mask == 0)
            {
                //vC is not used, what about vB?
                mask = (fflags & (DF_UB | DF_UB_WIDE));

                if (mask == 0)
                {
                    //vB is not used, so it is our constant
                    return (first.vB == second.vB);
                }
                else
                {
                    //vC is not used, so it is our constant
                    return (first.vC == second.vC);
                }
            }
        }
    }
    else
    {
        //In the general case, we force the instruction opcodes to be similar
        if (first.opcode != second.opcode)
        {
            return false;
        }

        if (firstMask != 0)
        {
            //It is setter or getter
            //Now we look at vA/vB/vC if not used
            int mask = (fflags & (DF_DA | DF_UA | DF_DA_WIDE | DF_UA_WIDE));

            if (mask == 0)
            {
                if (first.vA != second.vA)
                {
                    return false;
                }
            }

            //Now vB
            mask = (fflags & (DF_UB | DF_UB_WIDE));

            if (mask == 0)
            {
                if (first.vB != second.vB)
                {
                    return false;
                }
            }

            //Now vC
            mask = (fflags & (DF_UC | DF_UC_WIDE));

            if (mask == 0)
            {
                if (first.vC != second.vC)
                {
                    return false;
                }
            }

        }
        else
        {
            //it is not setter or getter, so we look at vC if it is const
            int mask = (fflags & DF_C_IS_CONST);

            if (mask != 0)
            {
                if (first.vC != second.vC && first.vB == second.vB)
                {
                    return false;
                }
            }
        }
    }

    //Got here, all is good
    return true;
}

/**
 * @brief Hash the opcode
 * @param opcode the opcode to hash
 * @return the new opcode if we are combining them together
 */
static int hashOpcode (int opcode)
{
    //We are basically setting the puts to their gets counterparts
    switch (opcode)
    {
        case OP_IPUT:
            return OP_IGET;
        case OP_IPUT_WIDE:
            return OP_IGET_WIDE;
        case OP_IPUT_OBJECT:
            return OP_IGET_OBJECT;
        case OP_IPUT_BOOLEAN:
            return OP_IGET_BOOLEAN;
        case OP_IPUT_BYTE:
            return OP_IGET_BYTE;
        case OP_IPUT_CHAR:
            return OP_IGET_CHAR;
        case OP_IPUT_SHORT:
            return OP_IGET_SHORT;
        case OP_IPUT_QUICK:
            return OP_IGET_QUICK;
        case OP_IPUT_WIDE_QUICK:
            return OP_IGET_WIDE_QUICK;
        case OP_IPUT_OBJECT_QUICK:
            return OP_IGET_OBJECT_QUICK;
        case OP_APUT:
            return OP_AGET;
        case OP_APUT_WIDE:
            return OP_AGET_WIDE;
        case OP_APUT_OBJECT:
            return OP_AGET_OBJECT;
        case OP_APUT_BOOLEAN:
            return OP_AGET_BOOLEAN;
        case OP_APUT_BYTE:
            return OP_AGET_BYTE;
        case OP_APUT_CHAR:
            return OP_AGET_CHAR;
        case OP_APUT_SHORT:
            return OP_AGET_SHORT;
        case OP_SPUT:
            return OP_SGET;
        case OP_SPUT_WIDE:
            return OP_SGET_WIDE;
        case OP_SPUT_OBJECT:
            return OP_SGET_OBJECT;
        case OP_SPUT_BOOLEAN:
            return OP_SGET_BOOLEAN;
        case OP_SPUT_BYTE:
            return OP_SGET_BYTE;
        case OP_SPUT_CHAR:
            return OP_SGET_CHAR;
        case OP_SPUT_SHORT:
            return OP_SGET_SHORT;
        default:
            //Do nothing in the general case
            break;
    }

    return opcode;
}

/**
 * @brief Is the opcode making disambiguation impossible with just looking at operands?
 * @param opcode the Opcode in question
 */
bool canOperandsDisambiguate (int opcode)
{
    switch (opcode)
    {
        //Cannot disambiguate object gets
        case OP_IGET_OBJECT:
        case OP_IGET_OBJECT_QUICK:
        case OP_IPUT_OBJECT:
        case OP_IPUT_OBJECT_QUICK:
        case OP_IGET_OBJECT_VOLATILE:
        case OP_IPUT_OBJECT_VOLATILE:
            return false;
    }
    return true;
}

/**
 * @brief Are the instructions similar regarding operands/opcode?
 * @details The primary goal of this function is utility for memory aliasing algortihm.
 * This function is responsible for reporting whether two instructions points to different
 * memory locations. In this case it reports false. If this function cannot ensure that
 * location is different it should report true. Additionally this function is used in recursion
 * to detect whether two instruction produces the same value. The rule is the same: if we are not
 * sure that result of two instructions are different the function should report true.
 * @param first the first MIR
 * @param second the second MIR
 * @param consideredMIRs a map containing MIRs that have a finalized color
 * @return the function returns true if the two instructions may alias
 */
static bool instructionsMayAlias (MIR *first, MIR *second, const std::map<MIR *, bool> &consideredMIRs)
{
    //Simple first
    if (first == second)
    {
        return true;
    }

    //If both instructions have been considered, we only have to look at their color
    if ( (consideredMIRs.find (first) != consideredMIRs.end ()) &&
         (consideredMIRs.find (second) != consideredMIRs.end ()))
    {
        return first->color.aliasingColor == second->color.aliasingColor;
    }

    //Then check opcode and vA, vB, and vC
    const DecodedInstruction &dfirst = first->dalvikInsn;
    const DecodedInstruction &dsecond = second->dalvikInsn;

    //Get opcodes
    int fopcode = dfirst.opcode;
    int sopcode = dsecond.opcode;

    //Hash the opcodes
    fopcode = hashOpcode (fopcode);
    sopcode = hashOpcode (sopcode);

    //If different opcodes, we are done here
    if (fopcode != sopcode)
    {
        //It is very difficult decision
        //Two different opcodes can produce the same result.
        //For example: a+= 1 and a-=-1;
        //Other example would be a value got by const bytecode and value got by iget bytecode.
        //It is very difficult to disambiguate them here so we report them as similar instructions.
        return true;
    }

    //Extended instructions are always considered different (simplification of the algorithm)
    int opcode = dfirst.opcode;

    if (opcode >= static_cast<int> (kMirOpFirst))
    {
        return false;
    }

    /*
      Sometimes the operands aren't sufficient to prove disambiguation
      For example, two iget-object are almost impossible to disambiguate
       iget-object v1, v2, 0x8
       iget-object v3, v5, 0x16

       How can your prove that v2.0x8 is != v5.0x16?

       Even with v2 != v5, that proves nothing

       The only way would be able to either prove v1 != v3 via an if in the trace
       Or if we had object types from v1 and v3 to show that they are incompatible
       For the moment, this patch does not do that, so we check the opcode and see if we
       can disambiguate
    */

    if (canOperandsDisambiguate (dfirst.opcode) == false || canOperandsDisambiguate (dsecond.opcode) == false)
    {
        //Can't disambiguate, return that they are similar
        return true;
    }

    //Now that operands are sufficient, what about the constants, we want to distinguish for example:
    // iget-wide v0, v1, #12
    // iput-wide v5, v1, #20
    if (compareConstants (dfirst, dsecond) == false)
    {
        return false;
    }

    //Now what we really care about is the MIRs defining the uses
    //Do they have the same color or not?

    //First get the SSA representation
    SSARepresentation *ssaRepFirst = first->ssaRep;
    SSARepresentation *ssaRepSecond = second->ssaRep;

    //Paranoid
    assert (ssaRepFirst != 0 && ssaRepSecond != 0);

    //Check the number of uses, this comes from the hash function, it can actually hash different opcodes
    int numUsesFirst = ssaRepFirst->numUses;
    int numUsesSecond = ssaRepSecond->numUses;

    //However, we might only care about a certain index of them
    int startUseIndexFirst = dvmCompilerGetStartUseIndex (dfirst.opcode);
    int startUseIndexSecond = dvmCompilerGetStartUseIndex (dsecond.opcode);

    //But they must have the same number of uses when we remove the index
    if (numUsesFirst - startUseIndexFirst != numUsesSecond - startUseIndexSecond)
    {
        return false;
    }

    //Now iterate through the defWhere
    MIR **defWhereFirst = ssaRepFirst->defWhere;
    MIR **defWhereSecond = ssaRepSecond->defWhere;

    //Paranoid
    assert (numUsesFirst <= 0 || defWhereFirst != 0);
    assert (numUsesSecond <= 0 || defWhereSecond != 0);

    //Let's iterate through it
    for (int i = 0; i + startUseIndexFirst < numUsesFirst; i++)
    {
        MIR *defFirst = defWhereFirst[i + startUseIndexFirst];
        MIR *defSecond = defWhereSecond[i + startUseIndexSecond];

        //It is possible the use is not defined in the trace
        if (defFirst == 0 || defSecond == 0)
        {
            return true;
        }
        else
        {
            //In this case, we have two new instructions, if they are the same, it's easy: skip it
            if (defFirst != defSecond)
            {
                //Too bad, we have more work to do, are they now similar?
                if (instructionsMayAlias (defFirst, defSecond, consideredMIRs) == false)
                {
                    //If not, we are done
                    return false;
                }
            }
        }
    }

    //We arrived here so we know the instructions are similar
    return true;
}

/**
 * @brief Handle colors for the BB, distinguish colors for SSA registers
 * @param bb the BasicBlock
 * @param workList the workList of instructions
 * @param currentColor the current color being used
 */
static void handleColors (BasicBlock *bb, std::map<int, std::vector<MIR *> > &workList, unsigned int &currentColor)
{
    //This map will help track which MIRs have finalized colored
    std::map<MIR *, bool> consideredMIRs;

    //Go through the list of instructions again
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get the opcode
        int opcode = mir->dalvikInsn.opcode;

        //Hash it
        opcode = hashOpcode (opcode);

        //Get the right vector list
        std::vector<MIR *> &list = workList[opcode];

        //An iterator that points to the first topologically superior of 'mir'
        std::vector<MIR *>::iterator firstSuperior = list.begin() + 1;

        //Now traverse same group
        for (std::vector<MIR *>::iterator i = list.begin (); i != list.end (); i++)
        {
            //Get local version
            MIR *other = *i;

            //Skip topologically inferior instructions: reduces useless recalculations
            if (other->topologicalOrder > mir->topologicalOrder)
            {
                //Ok, now we know we currently have the same color but should we is the real question
                // To be of the same color, we now must look at our uses and see if their defines are the same or the same color
                // We also remember which MIRs we've handled to not look at them multiple times as we work our way upwards
                // Because we do this in order of traversal, we only need to recolor the current considered one
                bool res = instructionsMayAlias (mir, other, consideredMIRs) ;

                if (res == false)
                {
                    //Only recolor if we have the same color
                    if (other->color.aliasingColor == mir->color.aliasingColor)
                    {
                        //We can change a color only if it was not finalized, otherwise
                        //we will break the info about similarity of other MIRs
                        if (consideredMIRs.find (other) == consideredMIRs.end ())
                        {
                            //Ahh, we need a new color for them
                            other->color.aliasingColor = currentColor;

                            //Increment the color
                            currentColor++;
                        }
                    }
                }
                else
                {
                    //Similar instruction, so actually, use the lowest of the colors
                    //This comes from the fact that if you had A,B,B in the list
                    //A will provoke a new color for each B, but they should be similar; so this rectifies that

                    unsigned int mirColor = mir->color.aliasingColor;
                    unsigned int otherColor = other->color.aliasingColor;

                    if (mirColor < otherColor)
                    {
                        other->color.aliasingColor = mirColor;
                    }
                    else if (mirColor > otherColor)
                    {
                        //We're going to change the color of current 'mir', which might
                        //be dangerous, since we can lose the color connection to previously
                        //colored mirs. So, we have to walk through all the group's mirs,
                        //which are topologically superiours of current 'mir' and update the
                        //color information of those mirs, which were in the same color with
                        //current 'mir'.

                        for (std::vector<MIR *>::iterator j = firstSuperior; j != i; j++)
                        {
                            MIR *superior = *j;
                            if (superior->color.aliasingColor == mirColor)
                            {
                                superior->color.aliasingColor = otherColor;
                            }
                        }

                        mir->color.aliasingColor = otherColor;
                    }

                    //In this case, the color is finalized for other as well
                    consideredMIRs[other] = true;
                }
            }
            else
            {
                //We still didn't reach the superior
                firstSuperior = i + 1;
            }
        }

        //This color is now finalized
        consideredMIRs[mir] = true;
    }
}

/**
 * @brief Link the colored instructions together
 * @param bb the BasicBlock
 */
static void linkColors (const BasicBlock *bb)
{
    //Map to remember last instruction per color
    std::map<unsigned int, MIR *> colorMap;

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get color
        unsigned int currentColor = mir->color.aliasingColor;

        //Find last mir
        MIR *last = colorMap[currentColor];

        //First link mir to last
        mir->color.prev = last;

        //If we have a last, link it too
        if (last != 0)
        {
            last->color.next = mir;
        }

        //Update last
        colorMap[currentColor] = mir;
    }
}

 /**
  * @brief Memory Aliasing pass: calculates which memory instruction aliases together
  * @details The basic algorithm is:
  *   - For each bytecode, use a hash to put Xput instructions with Xget, put them in separate buckets
  *   - Then, for each color:
  *      If we can disambiguate (we can't disambiguate easily get/put object bytecodes, see above comment when we try
  *          Then create a new color
  *
  *  We can disambiguate if it's not a get/put object and the constant is different
  * @param cUnit the CompilationUnit
  * @param bb the BasicBlock
  * @return whether the pass changed something in the BasicBlock
  */
/**
 */
bool dvmCompilerMemoryAliasing (CompilationUnit *cUnit, BasicBlock *bb)
{
    //We have a map: (opcode -> list of instructions)
    std::map<int, std::vector<MIR *> > workList;

    //Color scheme
    unsigned int currentColor = 0;

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        DecodedInstruction &insn = mir->dalvikInsn;

        //Add in the instruction to the map
        int opcode = insn.opcode;
        //Hash the opcode
        opcode = hashOpcode (opcode);

        //But first, we want to color the instruction
        //Get the associated vector
        std::vector<MIR *> &list = workList[opcode];

        //If the list does not have a color yet, let's create a new one
        if (list.size () == 0)
        {
            mir->color.aliasingColor = currentColor;
            currentColor++;
        }
        else
        {
            //Copy the color from the first instruction of the list
            mir->color.aliasingColor = list[0]->color.aliasingColor;
        }

        //Now add to the vector
        list.push_back (mir);
    }

    //Once we have a list of base colors, we need to distinguish between uses
    //For example, we now consider:
    //  - Two adds to be same even if they use totally different registers
    //  - Two loads from different memory areas
    handleColors (bb, workList, currentColor);

    //Before finishing the pass, one last traversal to actually link the color links together
    linkColors (bb);

    //Did not change the BasicBlock
    return false;
}

//The actual entry function to the memory aliasing pass
void dvmCompilerMemoryAliasing (CompilationUnit *cUnit)
{
    if (dvmCompilerVerySimpleLoopGate (cUnit, 0) == false)
    {
        return;
    }

    dvmCompilerDataFlowAnalysisDispatcher (cUnit, dvmCompilerMemoryAliasing, kPredecessorsFirstTraversal, false);
}


/**
 * @brief Check whether all previoues memory accesses are variants
 * @param current the MIR instruction
 * @return whether all previoues memory accesses are variants
 */
static bool checkAllPrevAreVariant (const MIR *current)
{
    for (MIR *consideredMir = current->prev; consideredMir != 0; consideredMir = consideredMir->prev)
    {
        //Get the opcode
        int opcode = consideredMir->dalvikInsn.opcode;

        //Get flags for it
        long long dfAttributes = dvmCompilerDataFlowAttributes[opcode];

        //Check if getter/setter
        if ( (dfAttributes & DF_IS_SETTER) != 0 || (dfAttributes & DF_IS_GETTER) != 0)
        {
            if (consideredMir->invariant == true)
            {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Helper function handling the BasicBlock of the loop
 * @param info the LoopInformation
 * @param bb the BasicBlock
 */
static void handleVariantBB (LoopInformation *info, BasicBlock *bb)
{
    //The algortihm is as follows:
    // - if it is an extanded MIR => it is a variant
    // - if it is a memory access
    //   - if loop contains volatile access => all memory accesses are variant
    //   - if any of its uses is variant => it is variant
    //   - if there are both getter/setter accesses to the same memory => all accesses
    //        to this memory are variant
    // - if it is not setter/getter => it is variant if any of its uses is variant
    // Otherwise it is an invariant

    //Get a local version of the loop information
    //We have a bitvector for the current variants
    BitVector *variants = dvmCompilerAllocBitVector (1, true);

    //Keeps the info about what colors are invariant
    std::set<int> variantColor;

    bool forceGetterSetterAsVariant = false;

    bool redo = true;
    while (redo == true)
    {
        redo = false;

        for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
        {
            //If it's extended, we mark it as variant
            int opcode = mir->dalvikInsn.opcode;

            if (opcode >= static_cast<int> (kMirOpFirst))
            {
                markMIRAsVariant (mir, variants);
                continue;
            }

            long long dfAttributes = dvmCompilerDataFlowAttributes[opcode];
            bool isGetterOrSetter = (dfAttributes & DF_IS_SETTER) != 0 || (dfAttributes & DF_IS_GETTER) != 0;

            if (isGetterOrSetter == true)
            {
                if (forceGetterSetterAsVariant == true)
                {
                    markMIRAsVariant (mir, variants);
                    continue;
                }

                MIR *highest = dvmCompilerFindHighestMIRInColor (mir);
                //If we are highest we should consider the chain of mirs with the same color
                if (highest == mir)
                {
                    bool isVariant = handleChainOfTheSameColor (highest, variants, forceGetterSetterAsVariant);

                    if (forceGetterSetterAsVariant == true)
                    {
                        //It was detected that all memory accesses whould be marked as variant
                        //So if any memory access was marked as invariant we should restart the process.
                        if (checkAllPrevAreVariant (mir) == false)
                        {
                            redo = true;
                            break;
                        }
                        continue;
                    }

                    if (isVariant == true)
                    {
                        //Mark this color as variant
                        variantColor.insert (mir->color.aliasingColor);

                        //Our MIR has already been marked as variant so we can jump to next
                        continue;
                    }
                }
                else
                {
                    //We have already considered in a chain, now we need to check
                    //whether we have already been marked as variant
                    bool isVariant = variantColor.find (mir->color.aliasingColor) != variantColor.end ();

                    if (isVariant == true)
                    {
                        //Our MIR has already been marked as variant so we can jump to next
                        continue;
                    }
                }
            }

            //The last thing we chould check is uses
            if (dvmCompilerUsesAreInvariant (mir, variants) == true)
            {
                //Mark it as so
                mir->invariant = true;
            }
            else
            {
                //Mark it as variant
                markMIRAsVariant (mir, variants);
            }
        }
    }

    //Set variants
    info->setVariants (variants);
}

//Entry point to loop invariant detection routine
void dvmCompilerVariant (CompilationUnit *cUnit)
{
    if (dvmCompilerVerySimpleLoopGate (cUnit, 0) == false)
    {
        return;
    }

    //Get the loop information
    LoopInformation *info = cUnit->loopInformation;
    assert (info != 0);

    //It is a simple loop, so only 1 basic block
    BasicBlock *entry = info->getEntryBlock ();
    assert (entry != 0);

    //Call helper
    handleVariantBB (info, entry);
}
