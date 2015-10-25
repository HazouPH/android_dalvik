/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "BBOptimization.h"
#include "Checks.h"
#include "CompilerIR.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "InvariantRemoval.h"
#include "LoopInformation.h"
#include "Pass.h"
#include "PassDriver.h"
#include "Utility.h"

#include <set>
#include <vector>

/**
 * @brief Are the uses of the MIR instruction all defined in the chosen map
 * @details For setter we also need to check whether all accesses to the same memory
 * were also in chosen map because memory is also some kind of use for MIR
 * @param cUnit the CompilationUnit
 * @param chosen the chosen map, the MIRs that have been chosen to be hoisted
 * @param mir the MIR instruction we care about
 * @param isSetter whether the MIR instruction is setter
 * @return whether all of mir's uses' definitions are in chosen
 */
static bool usesAreInChosen (CompilationUnit *cUnit, std::map<MIR *, bool> &chosen, MIR *mir, bool isSetter)
{
    //Go through the ssa
    SSARepresentation *ssaRep = mir->ssaRep;

    if (ssaRep == 0)
    {
        //Conservative
        return false;
    }

    int max = ssaRep->numUses;

    for (int i = 0; i < max; i++)
    {
        //Go to defwhere
        MIR *defined = ssaRep->defWhere[i];

        if (defined != 0)
        {
            //It must be in the chosen
            if (chosen.find (defined) == chosen.end ())
            {
                //Wasn't there when we looked for it
                return false;
            }
        }
    }

    //If it is a setter we need to ensure that there is no other accesses to the same area in the loop
    //Our coloring is not perfect and it will detect that access to the same array has the same color
    //As a result two aput bytecodes where the first one is invariant but the second one is not
    //can have the same color but the first one can update array element after it is updated by second one
    //so it cannot be sinked
    if (isSetter == true)
    {
        if (mir->color.prev != 0 || mir->color.next != 0)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Find the invariants in the peeled code
 * @param cUnit the CompilationUnit
 * @param bb the BasicBlock
 * @return did we change the BasicBlock
 */
static bool findInvariantsInPeelHelper (CompilationUnit *cUnit, BasicBlock *bb)
{
    std::map<MIR *, bool> chosen;

    //Only care about peeled BasicBlocks
    if (bb->peeled == false)
    {
        //We did not change the BasicBlock
        return false;
    }

    //Ok, let's go through the BasicBlocks and filter, but add the selection
    std::vector<MIR *> *selection = static_cast<std::vector<MIR *> *> (cUnit->passData);

    /* The following loop parses the basic block and finds the invariant. Before accepting it though,
       it first checks if all of the definitions of its uses have been chosen as well. If so, we select it.

       There is one exception, see the comment below.
     */
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get the opcode
        Opcode opcode = mir->dalvikInsn.opcode;

        //We are currently refusing CONST_WIDEs for hoisting because we have a CSO backend system called DLCI
        //that puts them at the end of the trace and handles things better than we could hope to achive
        //We also refuse other constants because the BE is actually smart about them and handles them correctly
        //NB: The only reason we might want to handle it ourselves is that, by not hoisting them, we can't hoist subsequent instructions
        //    We should potentially look if there is a use that would need a constant and select the constant at that point if it's the only one there
        //    However, if that one can't be hoisted later on and we did hoist the constant, we should put the constant back in... so a bit of work to get this all set-up

        //Get the flags
        long long dfFlags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if ((dfFlags & DF_SETS_CONST) != 0)
        {
            continue;
        }

        //Don't hoist any extended MIRs
        if (opcode >= static_cast<Opcode> (kMirOpFirst))
        {
            continue;
        }

        int dexFlags = dvmCompilerGetOpcodeFlags (opcode);

        //Get the dex information, if it can branch, don't hoist it
        if ( (dexFlags & kInstrCanBranch) != 0)
        {
            continue;
        }

        //Is it an invariant
        if (mir->invariant == true)
        {
            //Also do not take it if any of its use is not in the chosen
            //For setters we need to check whether all previous accesses to this
            //memory are also in chosen.
            bool isSetter = (dfFlags & DF_IS_SETTER) != 0;
            if (usesAreInChosen (cUnit, chosen, mir, isSetter) == true)
            {
                selection->push_back (mir);
                chosen[mir] = true;
            }
        }
    }

    //Did not change the BasicBlock
    return false;
}

/**
 * @brief Wrapper to find the invariants in the peeled basic blocks
 * @param cUnit the CompilationUnit
 * @param selection the vector of MIR instructions chosen as being invariant and hoist-able
 */
static void findInvariantsInPeel (CompilationUnit *cUnit, std::vector<MIR *> &selection)
{
    //Ok, let's go through the BasicBlocks and filter, but add the selection
    cUnit->passData = static_cast<void *> (&selection);

    //Now go find the invariants
    dvmCompilerDataFlowAnalysisDispatcher (cUnit, findInvariantsInPeelHelper, kAllNodes, false);

    //Clear it
    cUnit->passData = 0;
}

/**
 * @brief Find a replacement register for the definitions of a given MIR instruction
 * @details TODO we can a bit smarter here, if the color corresponding to our MIR is in
 *          some replacement register we can use this register and eliminate the hoisting
            of this MIR.
 * @param cUnit the CompilationUnit
 * @param mir the MIR instruction
 * @param current the current scratch register being used
 * @param max the maximum number of scratch registers available
 * @param chosen the chosen MIRs to be hoisted by the algorithm
 * @param localValueNumberingDef the map providing a quick look-up between definition and LVN
 * @param replacements the map providing the replacament association between current register and scratch register
 * @param available map sent by value denotes which scratch registers cannot be re-used
 * @return did we find a replacement for the MIR instruction?
 */
static bool findReplacement (CompilationUnit *cUnit, MIR* mir, unsigned int &current, unsigned int max,
                             std::map<MIR *, bool> &chosen, std::map<int, int> &localValueNumberingDef,
                             std::map<int, int> &replacements, std::map<int, bool> available)
{
    //How many defines does this instruction require
    int defs = mir->ssaRep->numDefs;

    //If no defs, then we are good to go
    if (defs == 0)
    {
        return true;
    }

    //As a simplification, let us not reuse if mir is wide
    //  This means that we won't try to find replacements for a wide instruction
    //  Basically because we'd need to add a lot of logic to get two consecutive scratch registers...
    //  So instead, we see if we can use two new ones
    if (defs == 1)
    {
        //First, go through the mirs, is there one no longer needed
        for (std::map<MIR *, bool>::iterator it = chosen.begin (); it != chosen.end (); it++)
        {
            //Get MIR
            MIR *start = it->first;

            //Get SSA Representation
            SSARepresentation *ssaRep = start->ssaRep;

            //Paranoid
            assert (ssaRep != 0);

            //If the instruction has no define, skip it
            unsigned int max = ssaRep->numDefs;

            //Go through each element
            unsigned int i = 0;
            for (i = 0; i < max; i++)
            {
                //Get usedNext chain for the first def
                SUsedChain *chain = start->ssaRep->usedNext[i];

                //Suppose it is in the list: there must be at least one element to consider reusing it
                bool localReUseIt = (chain != 0);

                //Go down the chain, are all the instructions in the chosen map?
                while (chain != 0)
                {
                    //Corner case: if the instruction is ourself, then we can skip, we can reuse the register
                    if (chain->mir != mir)
                    {
                        //Check if it's chosen, if it is we can reuse it
                        localReUseIt = (chosen.find (chain->mir) != chosen.end ());

                        if (localReUseIt == false)
                        {
                            break;
                        }
                        else
                        {
                            //Ok we might not be able to depending on the topological order
                            MIR *chained = chain->mir;

                            if (chained->topologicalOrder >= mir->topologicalOrder)
                            {
                                localReUseIt = false;
                                break;
                            }
                        }
                    }

                    //Go to the next use
                    chain = chain->nextUse;
                }

                //Get replacement
                int def = ssaRep->defs[i];
                int replacement = replacements[def];

                //If localReUseIt is false, break
                if (localReUseIt == false)
                {
                    //Mark it as not available
                    available[replacement] = false;
                }
                else
                {
                    //Now see if it has an entry?
                    if (available.find (replacement) == available.end ())
                    {
                        //Mark it as available
                        available[replacement] = true;
                    }
                }
            }
        }

        for (std::map<int, bool>::const_iterator it = available.begin (); it != available.end (); it++)
        {
            int reg = it->first;
            bool val = it->second;

            if (val == true)
            {
                //Get the define register, we know we only have one define at this point of the code
                int def = mir->ssaRep->defs[0];

                //Add to replacement
                replacements[def] = reg;

                // We are re-using reg so we need to update color table then
                for (std::map<int, int>::iterator it = localValueNumberingDef.begin (); it != localValueNumberingDef.end ();)
                {
                    if (it->second == reg)
                    {
                        localValueNumberingDef.erase (it++);
                    }
                    else
                    {
                        it++;
                    }
                }

                // Set new color
                int color = mir->localValueNumber;
                localValueNumberingDef[color] = reg;

                //Report success
                return true;
            }
        }
    }

    //Cannot reuse registers, do we still have a scratch registers for it?
    if (current + defs <= max)
    {
        //Get the local value numbering color
        int color = mir->localValueNumber;

        //Get replacement:
        int replacement = 0;

        //If we already have it in the map, use that
        std::map<int, int>::const_iterator it = localValueNumberingDef.find (color);

        if (it != localValueNumberingDef.end ())
        {
            replacement = it->second;
        }
        else
        {
            //Get replacement
            replacement = dvmCompilerGetFreeScratchRegister (cUnit, defs);

            //Mark it in
            localValueNumberingDef[color] = replacement;

            //Increase scratch number used
            current += defs;

            PASS_LOG (ALOGI, cUnit, "Obtained scratch register v%u for invariant hoisting", replacement);
        }

        //Mark each def with its replacement
        for (int i = 0; i < defs; i++)
        {
            //Get the define register
            int def = mir->ssaRep->defs[i];
            //Add to replacement
            replacements[def] = replacement;
        }

        //Accept it
        return true;
    }

    //We cannot find a replacement
    return false;
}

/**
 * @brief Hoist the invariants
 * @details The function actually does the hoisting by taking the selection, hoisting the instructions in selection and filling the moves vector with any necessary move instruction
 * @param cUnit the CompilationUnit
 * @param loopInfo the LoopInformation
 * @param selection the instructions selected to be hoisted
 * @param moves the instructions to be sunk after hoisting is done
 */
static void hoistInvariants (CompilationUnit *cUnit, LoopInformation *loopInfo, std::vector<MIR *> &selection, std::vector<MIR *> &moves)
{
    /**
     * The algorithm of the following function is:
     *
     *   - Go through the peeled iteration and find the instructions we can hoist
     *      - Copy the instruction
     *      - For each instruction, find a scratch register to use
     *          - Depending on the instruction, we might require a move from scratch to original VR, so fill that in the moves vector
     *      - Try to rename the uses of the instruction's defines in order to use the scratch
     *          - If successful, remove the original instruction and hoist the copy into the preheader
     */

    //Local maps to help the algorithm
    std::map<int, int> localValueNumberingDef;
    std::map<int, int> replacements;

    unsigned int max = dvmCompilerGetMaxScratchRegisters ();
    unsigned int current = cUnit->numUsedScratchRegisters;

    //Map of chosen instruction that have been finally hoisted
    std::map<MIR *, bool> chosen;

    //This map contains scratch virtual registers which cannot be re-used since the moment they are added into this map
    std::map<int, bool> available;

    //Map of replacement registers that failed, all future replacements using these registers must fail because we haven't hoisted that replacement
    std::map<int, bool> failedHoisting;

    //Get the preheader
    BasicBlock *preHeader = loopInfo->getPreHeader ();

    //Now install the selected instructions into the preheader
    for (std::vector<MIR *>::const_iterator iter = selection.begin (); iter != selection.end (); iter++)
    {
        //Get the invariant mir for the peeled iteration
        MIR *peeledMir = *iter;

        //If we didn't hoist the instruction's dependencies, we are done
        bool isSetter = (dvmCompilerDataFlowAttributes[peeledMir->dalvikInsn.opcode] & DF_IS_SETTER) != 0;
        if (usesAreInChosen (cUnit, chosen, peeledMir, isSetter) == false)
        {
            continue;
        }

        //The invariant instructions in peel must be copied from loop
        if (peeledMir->copiedFrom == 0)
        {
            continue;
        }

        //Get the equivalent mir from the loop
        MIR *mirInLoop = peeledMir->copiedFrom;

        //Make a copy of the mir from loop because we want to hoist it into preheader
        MIR *copy = dvmCompilerCopyMIR (mirInLoop);

        //Copy the ssa as well so we don't have to regenerate in middle of this pass
        //and we need it in order to find the uses of the define from this MIR.
        //But we need to be careful and reset it from the copy after we're done.
        copy->ssaRep = mirInLoop->ssaRep;

        //Paranoid
        if (peeledMir->ssaRep == 0 || mirInLoop->ssaRep == 0)
        {
            continue;
        }

        //We are ready to try to hoist, do we have a replacement?
        bool found = findReplacement (cUnit, peeledMir, current, max, chosen, localValueNumberingDef, replacements, available);

        //If failed, skip it
        if (found == false)
        {
            continue;
        }

        //Did we succeed to rewrite the instruction?
        bool rewriteSuccess = false;
        //Do we need a move?
        bool needMove = false;
        //If we do need a move, from what VR to what VR?
        int oldReg = 0, newReg = 0;

        //If there are no defs, we are done
        if (mirInLoop->ssaRep->numDefs == 0)
        {
            rewriteSuccess = true;
        }
        else
        {
            //If we successfully rewrite selected MIRs to use temporary, we will need
            //to have a move from the temp back to original register at all loop exits.
            //Since the problem is simplified by the pass' gate and we have only one BB
            //with one backward branch and one loop exit block, those are the only places
            //we need to sink.
            //And since all exit points are dominated by the single BB, we just need to
            //figure out if the ssa we replaced is the last define live out of the single
            //BB. If it is, then we need to generate a move.
            //There is only one BB so it's easy here, we will need to generalize otherwise
            needMove = dvmCompilerIsSsaLiveOutOfBB (cUnit, loopInfo->getEntryBlock (),
                    mirInLoop->ssaRep->defs[0]) == true;

            //Now we need to do an additional check if def is wide then both defs should
            //reach the end of BB or not reach. If one reaches but another does not then
            //we should fail
            if (mirInLoop->ssaRep->numDefs == 2)
            {
                bool needMoveWide = dvmCompilerIsSsaLiveOutOfBB (cUnit, loopInfo->getEntryBlock (),
                        mirInLoop->ssaRep->defs[1]) == true;

                if (needMoveWide != needMove)
                {
                    continue;
                }
            }

            //So since our selection looked through the peel, we should have a mapping between the
            //ssa define of peeled MIR and the replacement temporary we want to use.
            int oldSsa = peeledMir->ssaRep->defs[0];

            //Get the actual register
            oldReg = dvmExtractSSARegister (cUnit, oldSsa);

            //Now try to rewrite the define if we find the temp VR we are supposed to use
            if (replacements.find (oldSsa) != replacements.end ())
            {
                newReg = replacements[oldSsa];

                rewriteSuccess = dvmCompilerRewriteMirDef (copy, oldReg, newReg);

                if (rewriteSuccess == true && needMove == true)
                {
                    //Now if there is a Phi node for our oldReg we should update its uses
                    //with new reg as well
                    MIR *phi = loopInfo->getPhiInstruction (cUnit, oldReg);

                    if (phi != 0)
                    {
                        rewriteSuccess = dvmCompilerRewriteMirUses (phi, oldReg, newReg);
                    }
                }
            }
        }

        //If we successfully completed the rewrite, then we can add it to the preheader
        if (rewriteSuccess == true)
        {
            //Add the hoisted MIR to loop preheader
            dvmCompilerAppendMIR (preHeader, copy);

            if (needMove == true)
            {
                //Since we want to ensure correctness, we make sure that we copy back from rewritten
                //register to the original register at all loop exits. So we can generate moves now.
                bool isWide = (mirInLoop->ssaRep->numDefs > 1);
                MIR *move = dvmCompilerNewMoveMir (newReg, oldReg, isWide);

                moves.push_back (move);

                //We did a move, so since that moment the scratch register cannot be re-used for
                //other purposes because we use it after the loop
                available[newReg] = false;
            }

            //Since we reused the ssaRep from the MIR we copied from, we must reset it right now
            copy->ssaRep = 0;

            //Finally, we can remove the mir from the loop
            dvmCompilerRemoveMIR (mirInLoop);

            //Mark that we did hoist that instruction
            chosen[peeledMir] = true;
        }
    }
}

/**
 * @brief Helper function to find the Iget/Iput couple that we'd like to hoist and sink and fill the vector
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param bb the BasicBlock
 * @param candidates the vector of Iget/Iput pairs
 */
static void findIgetIputCandidatesHelper (CompilationUnit *cUnit, const LoopInformation *info, const BasicBlock *bb, std::vector<std::pair<MIR *, MIR *> > &candidates)
{
    //Go through the instructions
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Get the flags
        long long flags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        //Is it a Iget ? and not wide
        if ( ( (flags & DF_IS_GETTER) != 0))
        {
            //Get color chain
            SInstructionColor &color = mir->color;

            //Ok we have a get, let's see if it's the first of the color
            if (color.prev == 0)
            {
                //It is, good, let's see if the next is a put
                MIR *potentialPut = color.next;

                //First not null and same BB
                if (potentialPut != 0 && potentialPut->bb == mir->bb)
                {
                    //It should also be the only other MIR of the color
                    if (potentialPut->color.next == 0)
                    {
                        //Ok, now is it a put?
                        long long flags = dvmCompilerDataFlowAttributes[potentialPut->dalvikInsn.opcode];

                        //Is it a Iput ?
                        if ( (flags & DF_IS_SETTER) != 0)
                        {
                            //We have our pair, let's make sure they are using the same registers for getting/setting
                            if ( (mir->dalvikInsn.vA == potentialPut->dalvikInsn.vA) &&
                                 (mir->dalvikInsn.vB == potentialPut->dalvikInsn.vB) &&
                                 (mir->dalvikInsn.vC == potentialPut->dalvikInsn.vC))
                            {
                                //Final question is: are all the uses of the get invariant to the loop
                                SSARepresentation *ssaRep = mir->ssaRep;

                                //Suppose it is invariant
                                bool usesAreInvariant = true;
                                //Also are the definitions of each use not in the loop?
                                bool notInLoop = true;

                                //If no SSA, be conservative
                                if (ssaRep == 0)
                                {
                                    usesAreInvariant = false;
                                }
                                else
                                {
                                    //Walk the uses
                                    int max = ssaRep->numUses;
                                    for (int i = 0; i < max; i++)
                                    {
                                        //Get local version
                                        int use = ssaRep->uses[i];

                                        //If not invariant, mark the boolean flag and bail
                                        if (info->isInvariant (use) == false)
                                        {
                                            usesAreInvariant = false;
                                            break;
                                        }

                                        //Ok, what about in the loop?
                                        MIR *defined = ssaRep->defWhere[i];

                                        //If it is in the loop, mark the boolean flag and bail
                                        if (defined != 0 && info->contains (defined->bb) == true)
                                        {
                                            notInLoop = false;
                                            break;
                                        }
                                    }
                                }

                                //Also is the put must be putting the last ssa of the loop
                                bool lastSSA = dvmCompilerIsSsaLiveOutOfBB (cUnit, info->getEntryBlock (), potentialPut->ssaRep->uses[0]) == true;
                                //Second use if there
                                if (potentialPut->ssaRep->numUses > 1)
                                {
                                    lastSSA = (lastSSA == true) && dvmCompilerIsSsaLiveOutOfBB (cUnit, info->getEntryBlock (), potentialPut->ssaRep->uses[1]) == true;
                                }

                                //If both flags are true, we can continue
                                if (usesAreInvariant == true && notInLoop == true && lastSSA == true)
                                {
                                    //Ok, we can add it to the candidates now
                                    candidates.push_back (std::make_pair<MIR *, MIR*> (mir, potentialPut));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief Updates def of mir and later uses with scratch register
 * @param cUnit the CompilationUnit,
 * @param info the LoopInformation
 * @param mir the MIR instruction
 * @param scratch the scratch register to use as a temporary
 * @param toMove output describing any items to add them to all loop exits
 * @return true if success
 */
static bool updateWithScratch (CompilationUnit *cUnit, const LoopInformation *info, MIR *mir, int scratch, std::vector<MIR *>& toMove)
{
    //First rewrite the instruction
    int oldReg = mir->dalvikInsn.vA;
    bool success = dvmCompilerRewriteMirDef (mir, oldReg, scratch);

    //If we failed to remove it, we are done
    if (success == false)
    {
        return false;
    }

    //If we successfully rewrite selected MIRs to use temporary, we will need
    //to have a move from the temp back to original register at all loop exits.
    //Since the problem is simplified and we have only one BB with one backward
    //branch and one loop exit block, those are the only places we need to sink.
    //And since all exit points are dominated by the single BB, we just need to
    //figure out if the ssa we replaced is the last define live out of the single
    //BB. If it is, then we need to generate a move.

    //There is only one BB so it's easy here, we will need to generalize otherwise
    bool needMove = dvmCompilerIsSsaLiveOutOfBB (cUnit, info->getEntryBlock (), mir->ssaRep->defs[0]) == true;

    if (needMove == true)
    {
        //Now if there is a Phi node for our oldReg we should update its uses
        //with new reg as well
        MIR *phi = info->getPhiInstruction (cUnit, oldReg);

        if (phi != 0)
        {
            success = dvmCompilerRewriteMirUses (phi, oldReg, scratch);
        }

        if (success == false)
        {
            return false;
        }

        //Since we want to ensure correctness, we make sure that we copy back from rewritten
        //register to the original register at all loop exits. So we can generate moves now.
        bool isWide = (mir->ssaRep->numDefs > 1);
        MIR *move = dvmCompilerNewMoveMir (scratch, oldReg, isWide);

        toMove.push_back (move);
    }

    return true;
}

/**
 * @brief Hoist an iget instruction
 * @param cUnit the CompilationUnit,
 * @param info the LoopInformation
 * @param mir the MIR instruction
 * @param scratch the scratch register to use as a temporary
 * @param toMove output describing any items to add them to all loop exits
 */
static void hoistIget (CompilationUnit *cUnit, const LoopInformation *info, MIR *mir, int scratch, std::vector<MIR *>& toMove)
{
    /*
     * Simple algorithm: try to rewrite the definition and its uses
     * If we fail in rewriting/removing the instruction, we are done.
     * However, no need to tell the iput about this, it can get sunk independently
     */
    bool success = updateWithScratch (cUnit, info, mir, scratch, toMove);

    //If we failed to update it, we are done
    if (success == false)
    {
        return;
    }

    //Simplest to hoist it is to remove it, set it in the preheader, and use a temporary for it
    bool res = dvmCompilerRemoveMIR (mir);

    //If we failed to remove it, we are done
    if (res == false)
    {
        return;
    }

    //Get the preheader
    BasicBlock *preheader = info->getPreHeader ();

    //Append to it
    dvmCompilerAppendMIR (preheader, mir);
}

/**
 * @brief Try to sink an iput instruction, doing a few checks before
 * @param cUnit the CompilationUnit,
 * @param info the LoopInformation
 * @param mir the MIR instruction
 * @param iget associated iget MIR instruction
 * @param scratch the scratch register to use as a temporary
 * @param toMove output describing any items to add them to all loop exits
 */
static void sinkIput (CompilationUnit *cUnit, LoopInformation *info, MIR *mir, MIR *iget, int scratch, std::vector<MIR *>& toMove)
{
    //Get the SSA representation
    SSARepresentation *ssaRep = mir->ssaRep;

    //Paranoid
    assert (ssaRep != 0 && ssaRep->numUses > 0);

    //Iput has the data to be stored in uses[0] and its definition is in defWhere[0]
    //First thing to do is actually go to the define of the mir that does the data for the iput
    MIR *defined = ssaRep->defWhere[0];

    //Normally, because the iget's vA is == to the iput's vA, we should have at least one defWhere
    assert (defined != 0 && defined->ssaRep != 0);

    //If the iget instruction is different than the iput (which should always be the case otherwise this iget/iput is useless)
    if (iget != defined)
    {
        bool rewriteSuccess = updateWithScratch (cUnit, info, defined, scratch, toMove);

        //Paranoid: if the rewrite failed, we have nothing to do anymore, we can't safely sink it
        //Note that we don't care if the iget got hoisted or not, the hoist of the iget and the sinking of the iput
        //can be done independently
        if (rewriteSuccess == false)
        {
            return;
        }
    }

    //Once done, we can sink it
    dvmCompilerRemoveMIR (mir);

    toMove.push_back (mir);
}

/**
 * @brief Find the Iget/Iput couple that we'd like to hoist and sink and fill the vector
 * @param cUnit the CompilationUnit
 * @param info the LoopInformation
 * @param candidates the vector of Iget/Iput pairs
 */
static void findIgetIputCandidates (CompilationUnit *cUnit, LoopInformation *info, std::vector<std::pair<MIR *, MIR *> > &candidates)
{
    /*
     *  The function only adds Iget/Iput couples of the same color, who's address is an invariant,
     *  if the Iget has a topological order before the iput, in the same BasicBlock, and if there is only the iget and iput in the color
     *
     *  We know this is restrictive but it is correct :-), we can loosen certain restrictions later if needed
     */

     //Go through the loops' BasicBlocks
    //Get the BasicBlocks
    const BitVector *blocks = info->getBasicBlocks ();

    //Go through each block
    BitVectorIterator bvIterator;

    dvmBitVectorIteratorInit ( const_cast<BitVector *> (blocks), &bvIterator);
    while (true)
    {
        BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector (bvIterator, cUnit->blockList);

        //Check if done
        if (bb == 0)
        {
            break;
        }

        //Call helper function
        findIgetIputCandidatesHelper (cUnit, info, bb, candidates);
    }
}

void dvmCompilerIgetIputRemoval (CompilationUnit *cUnit, Pass *curPass)
{
    //Get loop information
    LoopInformation *info = cUnit->loopInformation;

    //Paranoid
    assert (info != 0);

    //Peel the loop
    bool res = dvmCompilerPeel (cUnit, info);

    //See if we succeeded
    if (res == false)
    {
        //Cannot continue without peeling
        return;
    }

    //First job is to find if we have a color with only a get/put in it?
    std::vector<std::pair<MIR *, MIR*> > candidates;

    //Find IGet/IPut candidates
    findIgetIputCandidates (cUnit, info, candidates);

    //For each candidate couple, see if we still have a temporary register
    for (std::vector<std::pair<MIR *, MIR*> >::iterator it = candidates.begin (); it != candidates.end (); it++)
    {
        //Get the pair
        std::pair<MIR *, MIR *> &p = *it;

        //Get Iget
        MIR *iget = p.first;

        //Get Iput
        MIR *iput = p.second;

        //Paranoid
        assert (iget->ssaRep != 0);

        //Width for the couple
        unsigned int width = iget->ssaRep->numDefs;

        //First, do we have temporaries for it?
        int scratch = dvmCompilerGetFreeScratchRegister (cUnit, width);

        //Do we have some?
        if (scratch > -1)
        {
            PASS_LOG (ALOGI, cUnit, "Obtained scratch register v%u for getter/setter pair", scratch);

            //container for instruction to sink
            std::vector<MIR *> toMove;

            //Hoist the iget
            hoistIget (cUnit, info, iget, scratch, toMove);

            //Sink the iput
            sinkIput (cUnit, info, iput, iget, scratch, toMove);

            //Actually sink them
            info->sinkInstructions (cUnit, toMove);
        }
    }
}

void dvmCompilerInvariantRemoval (CompilationUnit *cUnit, Pass *curPass)
{
    //To make it to this point, we know we have a simple loop
    LoopInformation *loopInfo = cUnit->loopInformation;

    assert (loopInfo != 0);

    //Peel the loop
    bool res = dvmCompilerPeel (cUnit, loopInfo);

    //See if we succeeded
    if (res == false)
    {
        //Cannot continue without peeling
        return;
    }

    //Needed to hold the selection of MIRs to consider
    std::vector<MIR *> selection;

    //Find the invariant instructions in peel basicblocks
    findInvariantsInPeel (cUnit, selection);

    //If selection is empty, we are done
    if (selection.size () == 0)
    {
        return;
    }

    //In case we hoist anything and need to rewrite, let's keep track if there are any moves that
    //we need to sink to the loop exits.
    std::vector<MIR *> moves;

    //Now go through the invariants
    hoistInvariants (cUnit, loopInfo, selection, moves);

    //Finally, sink any moves
    loopInfo->sinkInstructions (cUnit, moves);
}

static bool invariantRemovalGateHelper (const CompilationUnit *cUnit, LoopInformation *info)
{
    //If not nested, just return false
    //This is a restriction because for the moment variant, memory aliasing are required by this patch
    //Therefore, we need to be first generalize them before fully enabling this pass
    if (info->getNested () != 0)
    {
        return false;
    }

    //This is now the inner loop...

    //Do we have the invariant information?
    BitVector *variants = info->getVariants ();

    //If not, bail
    if (variants == 0)
    {
        return false;
    }

    if (info->guaranteedToThrowFirstIteration (cUnit) == false)
    {
        return false;
    }

    //Report success
    return true;
}

bool dvmCompilerInvariantRemovalGate (const CompilationUnit *cUnit, Pass *curPass)
{
    //Get the loop
    LoopInformation *loopInfo = cUnit->loopInformation;

    //If no loop information, bail
    if (loopInfo == 0)
    {
        return false;
    }

    //We only care about the inner loop
    return invariantRemovalGateHelper (cUnit, loopInfo);
}

/**
 * @brief Finds all getters and setters in a given basic block
 * @details This function iterates over MIRs of given BasicBlock
 * and find all getters and setters basing on DataFlow attributes and whether
 * the instruction is volatile. This set covers all getters and setters.
 * Found instructions are places in STL vector of MIRs coming by data parameter.
 * @param cUnit The compilation unit
 * @param bb The basic block to check
 * @param data A vector of MIR pointers where to store the getters and setters.
 * Runtime type of this parameter should be (std::vector<MIR *> *).
 * @return Returns true if successfully gone through basic block and filled data.
 * However the vecore can be empty if no instruction were found.
 */
static bool findGettersSetters (CompilationUnit *cUnit, BasicBlock *bb, void *data)
{
    if (data == 0)
    {
        //No place to put the findings
        return false;
    }

    //We should have been given a set where we can store getters and setters
    std::set<MIR *> &gettersSetters = *(reinterpret_cast<std::set<MIR *> *> (data));

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        long long dfFlags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if (((dfFlags & DF_IS_SETTER) != 0) || ((dfFlags & DF_IS_GETTER) != 0) || dvmCompilerIsOpcodeVolatile (mir->dalvikInsn.opcode) == true)
        {
            gettersSetters.insert (mir);
        }
    }

    //Successfully looked
    return true;
}

/**
 * @brief Checks that no MIRs in basic block throw exceptions. Skips the MIRs passed in data.
 * @param cUnit The compilation unit
 * @param bb The basic block whose MIRs to examine
 * @param data The set of mirs to skip when looking at exceptions
 * @return Returns true if it can promise no exceptions are thrown
 */
static bool promiseNoExceptions (CompilationUnit *cUnit, BasicBlock *bb, void *data)
{
    if (data == 0)
    {
        //Cannot promise no exceptions
        return false;
    }

    //We should have been given a set of MIRs to skip
    std::set<MIR *> &mirsToSkip = *(reinterpret_cast<std::set<MIR *> *> (data));

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //If the MIR cannot be skipped, then check it cannot bail
        if (mirsToSkip.find (mir) == mirsToSkip.end ())
        {
            if (backendCanBailOut (cUnit, mir) == true)
            {
                //Exception may be thrown
                return false;
            }
        }
    }

    //No exceptions will be thrown
    return true;
}

/**
 * @brief Is the opcode an iput or iget of object?
 * @param opcode the Opcode in question
 * @return Returns whether we have an object iput/iget
 */
bool isObjectGetterSetter (int opcode)
{
    switch (opcode)
    {
        case OP_IGET_OBJECT:
        case OP_IGET_OBJECT_QUICK:
        case OP_IPUT_OBJECT:
        case OP_IPUT_OBJECT_QUICK:
        case OP_IGET_OBJECT_VOLATILE:
        case OP_IPUT_OBJECT_VOLATILE:
        case OP_APUT_OBJECT:
        case OP_AGET_OBJECT:
        case OP_SGET_OBJECT:
        case OP_SPUT_OBJECT:
        case OP_SGET_OBJECT_VOLATILE:
        case OP_SPUT_OBJECT_VOLATILE:
            return true;
    }

    //We do not have an object getter/setter
    return false;
}

/**
 * @brief Goes through a basic block and checks if any of those MIRs clobber memory
 * @details The function iterates over instructions of given BasicBlock and return false
 * if it observes at least one instruction which clobbers memory. The function uses
 * DataFlow attribute for clobbering instructions.
 * Limitation: due to no enough information about setting/getting objects we consider
 * such instructions as clobbering ones.
 * @param cUnit The compilation unit
 * @param bb The basic block whose MIRs to check
 * @param data Unused argument
 * @return Returns true if no memory is clobbered
 */
static bool checkIfNoClobberMemory (CompilationUnit *cUnit, BasicBlock *bb, void *data)
{
    //Unused
    (void) cUnit;

    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        long long dfFlags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        if ((dfFlags & DF_CLOBBERS_MEMORY) != 0)
        {
            //We DO clobber memory
            return false;
        }

        //Now check if we have an object update because that may clobber memory
        if (isObjectGetterSetter (mir->dalvikInsn.opcode) == true)
        {
            //We don't know much about memory once we update an object
            return false;
        }

        //We may also have memory clobbering if we do an invoke because we don't
        //really know what happens in the callee.
        if ((dvmCompilerGetOpcodeFlags (mir->dalvikInsn.opcode) & kInstrInvoke) != 0)
        {
            return false;
        }
    }

    //We don't actually clobber memory
    return true;
}

/**
 * @brief check whether SSA reg is the same as at start of bb
 * @details this function checks whether given virtual register is not change up
 * to the beginning of the given basic block. It is true if SSA subscript is the same
 * on the entrance to the BasicBlock.
 * @param cUnit Compilation context
 * @param bb The basic block where reg to check
 * @param data STL (reg, subscript) pair
 * @return Returns true if SSA subscript is same in use and start of BB
 */
static bool whetherSSARegIsTheSameOnEnter (CompilationUnit *cUnit, BasicBlock *bb, void *data)
{
    if (bb->dataFlowInfo == 0 || bb->dataFlowInfo->dalvikToSSAMapEntrance == 0)
    {
        //Cannot say for sure, so answer no
        return false;
    }
    std::pair<int, int> *reg = (std::pair<int, int> *)data;

    int exitRegVersion = DECODE_SUB (bb->dataFlowInfo->dalvikToSSAMapEntrance[reg->first]);

    return reg->second == exitRegVersion;
}

/**
 * @brief check whether basic block with id represented by data dominates bb
 * @details If there is no domination information for the given Basickblock we consider
 * that there is no domination. To use this function domination information should be avaiable.
 * @param cUnit Compilation context
 * @param bb The dominated basic block
 * @param data id of the block which dominates bb
 * @return Returns true basic block with id represented by data dominates bb.
 *  if there is no domination information return false.
 */
static bool whetherDominatesBasicBlock (CompilationUnit *cUnit, BasicBlock *bb, void *data)
{
    if (bb->dominators == 0)
    {
        //Cannot say for sure, so answer no
        return false;
    }
    int *id = (int *)data;

    return dvmIsBitSet (bb->dominators, *id);
}

/**
 * @brief Tries to select invariant setters for sinking. Put instructions into selection vector returned.
 * @details This function iterates over given setters/getters and checks whether it can be sunk from the loop.
 * The setter/getter can be sunk if it is not volatile instruction, otherwise according to specification
 * other threads should see the value in memory immidiately. Also the instruction should be executed
 * on the path to each loop exit because sunk instruction will be executed always and to avoid execution of
 * wrong operation we must ensure that it was executed in a loop.
 * The setter can be sunk if memory location of setter is invariant in a loop, this memory location is not used
 * in a loop by other istructions and the value put in a loop is the same in the instruction and at the end of the loop.
 * The getter can be sunk if memory location of getter is invariant, this memory location is not used
 * in a loop by other istructions and defined register is not used in the loop.
 * Limitations:
 * 1) There is only one getter/setter - to avoid checking whether the same memory location
 * is used in a loop.
 * 2) Only setter is considered - to avoid checking whether the assigned register is used in a loop
 * 3) Instruction should not have null or bound check - to avoid dealing with potential exception
 * @param cUnit The compilation unit
 * @param info The loop information
 * @param gettersSetters The set of all getters and setters in the loop
 * @return Returns the MIRs selected for possible sinking
 */
static std::set<MIR *> selectInvariants (CompilationUnit *cUnit, LoopInformation *info, std::set<MIR *> &gettersSetters)
{
    //Keep track of selections
    std::set<MIR *> selections;

    //We only sink if we find only one memory operation because otherwise we have a hard time disambiguating without coloring
    if (gettersSetters.size () == 1)
    {
        for (std::set<MIR *>::const_iterator iter = gettersSetters.begin (); iter != gettersSetters.end (); iter++)
        {
            MIR *mir = *iter;

            //Get the dataflow flags
            long long dfFlags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

            if ((dfFlags & DF_IS_SETTER) == 0)
            {
                //Only sink setters
                continue;
            }

            //If the MIR is a volatile, skip it
            if (dvmCompilerIsOpcodeVolatile (mir->dalvikInsn.opcode) == true)
            {
                continue;
            }

            //Skip instructions that need a range check because otherwise we need additional work when sinking
            //in order to deal with potential exception.
            if ((dfFlags & DF_HAS_NR_CHECKS) != 0 && (mir->OptimizationFlags & MIR_IGNORE_RANGE_CHECK) == 0)
            {
                continue;
            }

            if (mir->ssaRep == 0)
            {
                //Cannot do anything without ssa
                continue;
            }

            //Only sink if we dominate all the loop exits
            if (info->iterateThroughLoopExitBlocks (cUnit, whetherDominatesBasicBlock, &mir->bb->id) == false)
            {
                continue;
            }

            //Determine start index where the object pointer register is
            int startIndex = dvmCompilerGetStartUseIndex (mir->dalvikInsn.opcode);

            //Assume we have no changes till end of the loop
            bool isMemLocationIsInvariant = true;

            //We should check that all uses pointing to mem location are invariant in a loop
            //There is a limitation here. Invariant detection pass has a stricter gate than ours.
            //As a result we can reach here and do not have invariant info available. In this case
            //we use backup check. We consider use is invariant if it is defined outside of the loop
            //and leaves a loop with the same SSA subscript.
            bool isInvariantInfoAvailable = info->getVariants () != 0;

            for (int use = startIndex; use < mir->ssaRep->numUses && isMemLocationIsInvariant == true; use++)
            {
                if (isInvariantInfoAvailable == true)
                {
                    int ssaReg = mir->ssaRep->uses[use];
                    isMemLocationIsInvariant = info->isInvariant (ssaReg);
                }
                else
                {
                    //Backup plan
                    MIR *def = mir->ssaRep->defWhere[use];
                    isMemLocationIsInvariant = def == 0 || info->contains (def->bb) == false;

                    //If we pass the check for def, check also out
                    if (isMemLocationIsInvariant == true)
                    {
                        int useValue = mir->ssaRep->uses[use];
                        std::pair<int, int> reg (dvmExtractSSARegister (cUnit, useValue), dvmExtractSSASubscript (cUnit, useValue));

                        isMemLocationIsInvariant = info->iterateThroughLoopExitBlocks (cUnit, whetherSSARegIsTheSameOnEnter, &reg);
                    }
                }
            }

            if (isMemLocationIsInvariant == false)
            {
                continue;
            }

            //Assume we have no changes till end of the loop
            bool isValueTheSameOnExit = true;

            //We should ensure that the value of the VR we plan to set will be the same at all loop exits.
            for (int use = 0; use < startIndex && isValueTheSameOnExit == true; use++)
            {
                int useValue = mir->ssaRep->uses[use];
                std::pair<int, int> reg (dvmExtractSSARegister (cUnit, useValue), dvmExtractSSASubscript (cUnit, useValue));

                isValueTheSameOnExit = info->iterateThroughLoopExitBlocks (cUnit, whetherSSARegIsTheSameOnEnter, &reg);
            }

            if (isValueTheSameOnExit == false)
            {
                continue;
            }

            //Finally we should check that noone is touching the same memory.
            //However, right now we only accept when we have solely one iput so we don't need to check now.

            //Add it to selection list
            selections.insert (mir);
        }
    }

    //We have finished selecting
    return selections;
}

/**
 * @brief Goes through the selections MIRs and tries to generate hoisted null checks if needed.
 * @details For all MIRs that receive hoisted null checks or don't need null check, they are part
 * of the final selections.
 * @param cUnit The compilation unit
 * @param info The loop information
 * @param selections Input: The MIRs we are trying to sink.
 * Output: The MIRs that can be safely sunk because they received hoisted null check.
 */
static void handleNullCheckHoisting (CompilationUnit *cUnit, LoopInformation *info, std::set<MIR *> &selections)
{
    std::set<MIR *> finalSelections;

    for (std::set<MIR *>::const_iterator iter = selections.begin (); iter != selections.end (); iter++)
    {
        MIR *mir = *iter;

        //Get the dataflow flags
        long long dfFlags = dvmCompilerDataFlowAttributes[mir->dalvikInsn.opcode];

        //Check if we need to have object use but null check has not been done
        if ((dfFlags & DF_HAS_OBJECT_CHECKS) != 0 && (mir->OptimizationFlags & MIR_IGNORE_NULL_CHECK) == 0)
        {
            //Determine the object register index
            int index = -1;
            switch (dfFlags & DF_HAS_OBJECT_CHECKS)
            {
                case DF_NULL_OBJECT_CHECK_0:
                    index = 0;
                    break;
                case DF_NULL_OBJECT_CHECK_1:
                    index = 1;
                    break;
                case DF_NULL_OBJECT_CHECK_2:
                    index = 2;
                    break;
                default:
                    break;
            }

            //Check that we can find the object register
            if (index >= 0 && mir->ssaRep != 0 && mir->ssaRep->uses != 0)
            {
                int ssaReg = mir->ssaRep->uses[index];
                int objectDalvikReg = dvmExtractSSARegister (cUnit, ssaReg);

                //Now try to generate a hoisted null check. We put it in preheader because it dominates exit
                bool generatedNullCheck = dvmCompilerGenerateNullCheckHoist (info->getPreHeader (), objectDalvikReg);

                if (generatedNullCheck == true)
                {
                    //If we successfully generated the check then we mark this mir as having been checked
                    mir->OptimizationFlags |= MIR_IGNORE_NULL_CHECK;

                    finalSelections.insert (mir);
                }
            }
        }
        else
        {
            //We do not need a null check and thus we can safely sink it
            finalSelections.insert (mir);
        }
    }

    //We need to swap in the final selections for those which we successfully generated null check for
    selections.swap (finalSelections);
}

/**
 * @brief try to remove selected MIRs from original locations
 * @param selections set of selected MIRs for removing
 * @return returns true if all MIR's were successfully removed
 */
static bool removeSelectedMirs(std::set<MIR *> selections)
{
    for (std::set<MIR *>::iterator mirs = selections.begin(); mirs != selections.end(); ++mirs)
    {
        if (dvmCompilerRemoveMIR(*mirs) != true)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief This is per-loop function for Invariant Sinking optimization. It drives the optimization and
 * works as a gate for specific loop.
 * @details For the given loop this function checks whether the optimization can be applied in general,
 * tries to select some instructions for optimization, handle the null check hoisting and adds
 * sunk instruction to all loop exit blocks
 * Limitations:
 * 1) Loop has exactly one exit block
 * 2) Loop has exactly one backward branch
 * 3) Loop does not contain instructions clobbering memory
 * 4) handleNullCheckHoisting in reality does not work because instructions with null check are not selected
 * @param cUnit The compilation unit
 * @param info The loop for which to sink at exit
 * @param data Unused argument
 * @return Always returns true to signify that we have tried sinking invariants for this loop even
 * if we don't do anything.
 */
static bool sinkInvariants (CompilationUnit *cUnit, LoopInformation *info, void *data)
{
    //First, check we don't have multiple exits and we only have one backward branch
    if (dvmCountSetBits (info->getExitLoops ()) != 1)
    {
        //We have more than one exit so we stop early
        return true;
    }

    if (dvmCountSetBits (info->getBackwardBranches ()) != 1)
    {
        //We have more than one exit so we stop early
        return true;
    }

    //Second check that we don't clobber any memory
    if (info->iterateThroughLoopBasicBlocks (cUnit, checkIfNoClobberMemory) == false)
    {
        //We take the conservative approach that we cannot sink any memory operations.
        return true;
    }

    std::set<MIR *> gettersSetters, selections;

    //Third find all getters and setters
    if (info->iterateThroughLoopBasicBlocks (cUnit, findGettersSetters,
            reinterpret_cast<void *> (&gettersSetters)) == false)
    {
        //We ran into an issue while looking for getters/setters
        return true;
    }

    //Check if we have any getters and setters because if we don't, we have no work
    if (gettersSetters.size () == 0)
    {
        return true;
    }

    //Fourth, select any setters that can be moved
    selections = selectInvariants (cUnit, info, gettersSetters);

    //We have no work to do if we made no selections
    if (selections.size () == 0)
    {
        return true;
    }

    //Fifth check if excluding our selections we can still throw exception.
    if (info->iterateThroughLoopBasicBlocks (cUnit, promiseNoExceptions,
            reinterpret_cast<void *> (&selections)) == false)
    {
        //Apparently we cannot be promised no exceptions so we bail before sinking
        return true;
    }

    //Try to remove selected MIR's from its original location
    if (removeSelectedMirs(selections) != true)
    {
        //We can't remove some MIR's unexpectedly. Stop compilation.
        cUnit->quitLoopMode = true;
        return true;
    }

    //Sixth generate any needed null checks
    handleNullCheckHoisting (cUnit, info, selections);

    //Finally move the final selections to the exit
    std::vector<MIR *> instructionsToSink;
    instructionsToSink.insert (instructionsToSink.begin (), selections.begin (), selections.end ());
    info->addInstructionsToExits (cUnit, instructionsToSink);

    return true;
}

/**
 * @brief This is entry function to Invariant Sinking optimization.
 * @details The function simply iterates over loops and invokes the worker function sinkInvariants.
 * @param cUnit The compilation unit
 * @param curPass Unused argument
 */
void dvmCompilerInvariantSinking (CompilationUnit *cUnit, Pass *curPass)
{
    //Gate made sure that we have at least one loop
    LoopInformation *info = cUnit->loopInformation;
    assert (info != 0);

    //Apply transformation on all loops
    info->iterate (cUnit, sinkInvariants);
}

/**
 * @brief This is gate for Invariant Sinking optimization.
 * @details The optimization can be applied if trace is new system loop and actually has a loop.
 * @param cUnit The compilation unit
 * @param curPass pointer to current pass data structure
 */
bool dvmCompilerInvariantSinkingGate (const CompilationUnit *cUnit, Pass *curPass)
{
    //Unused argument
    (void) curPass;

    //We only apply optimizations if we have the new loop system
    if (dvmCompilerTraceIsLoopNewSystem (cUnit, curPass) == false)
    {
        return false;
    }

    //Now let's go through the loop information
    LoopInformation *info = cUnit->loopInformation;

    if (info == 0)
    {
        //We have no loops and thus we cannot sink any invariants
        return false;
    }

    return true;
}

