/*
* Copyright (C) 2012 Intel Corporation
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

#include <dlfcn.h>

#include "AccumulationSinking.h"
#include "BBOptimization.h"
#include "Checks.h"
#include "Dalvik.h"
#include "Dataflow.h"
#include "InvariantRemoval.h"
#include "Loop.h"
#include "PassDriver.h"
#include "SinkCastOpt.h"
#include "LoopRegisterUsage.h"
#include "Pass.h"
#include "RegisterizationME.h"
#include "Vectorization.h"
#include "Utility.h"

/**
 * @brief Static functions defined below
 */
static void handlePassFlag (CompilationUnit *cUnit, Pass *pass);
static bool checkLoopsGate (const CompilationUnit *cUnit, Pass *pass);

/**
 * @brief Three macros to help pass definitions
 */
#define START_PASSES \
    static Pass gPasses[] = {

#define NEW_PASS(NAME,TYPE,DATA,GATE,START,END,WORK,FREE,FLAG) \
        Pass ( \
            NAME, TYPE, DATA, GATE, START, END, WORK, FREE, FLAG \
        )

#define END_PASSES \
    };

START_PASSES
    //Check if the loops are like we want right now (nested or not, branches or not)
    NEW_PASS ("Reject_Loops", kAllNodes, 0, checkLoopsGate, 0, 0, 0, 0, 0),
    //This loop formation is used when the new loop filtering is active
    NEW_PASS ("Form_Loop", kAllNodes, 0, dvmCompilerTraceIsLoopNewSystem,
                dvmCompilerFormLoop, 0, 0, 0, kOptimizationBasicBlockChange | kLoopStructureChange),
    NEW_PASS ("Test_Loop", kAllNodes, 0, dvmCompilerTraceIsLoopNewSystem,
                dvmCompilerTestLoop, 0, 0, 0, 0),
    //This loop formation is used when the old loop filtering is active
    NEW_PASS ("Old_Loop_Formation", kAllNodesAndNew, 0, dvmCompilerTraceIsLoopOldSystem,
                dvmCompilerFormOldLoop, 0, 0, 0, kOptimizationBasicBlockChange | kLoopStructureChange),
    //Now check that bytecodes reference fully resolved classes, methods, and fields.
    //We only do this check for loops because we can bring in parts that are not so hot.
    NEW_PASS ("Check_References", kAllNodes, 0, dvmCompilerTraceIsLoopNewSystem,
            0, 0, dvmCompilerCheckReferences, 0, 0),
    //At this point, we finish with the loops, so to increase optimization scope we start inlining
    NEW_PASS ("Method_Inlining", kAllNodesAndNew, 0, 0,
                0, 0, dvmCompilerMethodInlining, 0, kOptimizationBasicBlockChange | kLoopStructureChange),
    NEW_PASS ("Check_Removal", kPredecessorsFirstTraversal, 0, 0,
                dvmCompilerStartCheckRemoval, dvmCompilerEndCheckRemoval, dvmCompilerCheckRemoval, 0, kOptimizationDefUsesChange),
    //The 2addr pass should come before any pass that need register rewriting.
    NEW_PASS ("Convert_2addr_to_normal", kAllNodes, 0, 0,
                0, 0, dvmCompilerConvert2addr, 0, kOptimizationBasicBlockChange),
    //Memory Aliasing only works on one basic block so let's try to merge first
    //For the moment, no pass will create new blocks, etc. so this is fine up here
    //Otherwise, we might want to duplicate the merge or make the gate for memory aliasing smarter
    NEW_PASS ("Remove_Gotos", kAllNodes, 0, 0, 0, NULL, dvmCompilerRemoveGoto, 0, kOptimizationBasicBlockChange),
    NEW_PASS ("Merge_Blocks", kAllNodes, 0, 0,
                0, 0, dvmCompilerMergeBasicBlocks, 0,
                kOptimizationBasicBlockChange | kLoopStructureChange | kOptimizationNeedIterative),
    NEW_PASS ("Invariant_Removal", kAllNodes, 0, dvmCompilerInvariantRemovalGate,
              dvmCompilerInvariantRemoval, 0, 0, 0, kOptimizationBasicBlockChange),
    NEW_PASS ("Iget_Iput_Removal", kAllNodes, 0, dvmCompilerInvariantRemovalGate,
              dvmCompilerIgetIputRemoval, 0, 0, 0, kOptimizationBasicBlockChange),
    NEW_PASS ("Sink_Casts", kAllNodes, 0, dvmCompilerTraceIsLoopNewSystem,
               dvmCompilerSinkCasts, 0, 0, 0, kOptimizationBasicBlockChange),
    NEW_PASS ("Accumulation_Sinking", kAllNodes, 0, dvmCompilerSinkAccumulationsGate,
            0, dvmCompilerAccumulationSinking, 0, 0, kOptimizationBasicBlockChange),
    NEW_PASS ("Vectorization", kAllNodes, 0, dvmCompilerTraceIsLoopNewSystem,
            dvmCompilerVectorize, 0, 0, 0, kOptimizationBasicBlockChange | kLoopStructureChange),
    NEW_PASS ("Invariant_sinking", kAllNodes, 0, dvmCompilerInvariantSinkingGate,
            dvmCompilerInvariantSinking, 0, 0, 0, kOptimizationBasicBlockChange),
    //Loop could be transformed at this point (e.g. loop peeling), so new opportunities're possible for Checks_Removal
    NEW_PASS ("Check_Removal", kPredecessorsFirstTraversal, 0, 0,
                dvmCompilerStartCheckRemoval, dvmCompilerEndCheckRemoval, dvmCompilerCheckRemoval, 0, kOptimizationDefUsesChange),
    NEW_PASS ("Copy_Propagation_Move_Return", kAllNodes, 0, 0,
                0, 0, dvmCompilerCopyPropagationMoveReturn, 0, kOptimizationDefUsesChange),
    //This should be after the last optimization that changes instruction sequence or BB logic
    //Note: reorder is safe and should be after
    //We do registerization for all traces to provide spill information to the BE
    NEW_PASS ("Write_Back_Registers", kAllNodes, 0, 0, 0, 0, dvmCompilerWriteBackAll, 0, 0),
    NEW_PASS ("Registerization_ME", kAllNodes, 0, dvmCompilerTraceIsLoopNewSystem,
                0, dvmCompilerRegisterize, 0, 0, 0),
    //From here we start preparing the CFG for the backend
    NEW_PASS ("Fix_Chaining_Cells", kAllNodesAndNew, 0, 0,
                0, 0, dvmCompilerFixChainingCells, 0, kOptimizationBasicBlockChange | kLoopStructureChange),
    NEW_PASS ("Add_Invoke_ChainingCells", kAllNodesAndNew, 0, 0,
                0, 0, dvmCompilerAddInvokeSupportBlocks, 0, kOptimizationBasicBlockChange),
    NEW_PASS ("Insert_LoopHelper_Blocks", kAllNodesAndNew, 0, 0,
                    0, dvmCompilerInsertLoopHelperBlocks, 0, 0, kOptimizationBasicBlockChange | kLoopStructureChange),
    NEW_PASS ("Reorder_Blocks", kBreadthFirstTraversal, 0, 0,
                dvmCompilerReorder, 0, 0, 0, kOptimizationBasicBlockChange | kLoopStructureChange),

    /* Last element must have and be the only one to have a NULL name, it's our ending check */
    NEW_PASS ("",       /** Pass name */
         kAllNodes,     /** Type of traversal */
         0,             /** Data */
         0,             /** Gate function */
         0,             /** Start function */
         0,             /** End function */
         0,             /** Work function */
         0,             /** Free function */
         0              /** Flags */
         ),
END_PASSES

void dvmCompilerBuildPassList (void)
{
    //Our pass system above is a table and is easy to create as it is, but our plugin system might want to reorder things,
    //So let's make it a pass list
    unsigned int max = sizeof (gPasses) / sizeof (gPasses[0]);

    //If at least one, we have work to do
    if (max > 0)
    {
        //Attach first
        gDvmJit.jitFramework.firstPass = gPasses;

        //Set last
        Pass *last = gPasses;

        for (unsigned int i = 1; i < max; i++)
        {
            //Get local version
            Pass *ptr = gPasses + i;

            //Paranoid
            assert (ptr != 0);

            //Attach it to last
            last->setNext (ptr);
            ptr->setPrevious (last);

            //Update last
            last = ptr;
        }
    }
}

/**
  * @brief Handle any pass flag that requires clean-up
  * @param cUnit the CompilationUnit
  * @param pass the Pass
  */
void handlePassFlag (CompilationUnit *cUnit, Pass *pass)
{
    if ((pass->getFlag (kOptimizationBasicBlockChange) == true) ||
            (pass->getFlag (kOptimizationDefUsesChange) == true))
    {
        bool buildLoopInfo = pass->getFlag (kLoopStructureChange) == true;
        dvmCompilerCalculateBasicBlockInformation (cUnit, false, buildLoopInfo);
    }
}

bool dvmCompilerRunPass (CompilationUnit *cUnit, Pass *curPass)
{
    //Paranoid
    if (cUnit == 0 || curPass == 0 || curPass->getName() == "")
    {
        return false;
    }

    //Check the pass gate first
    bool applyPass = curPass->gate (cUnit, curPass);

    //If the pass gate said ok
    if (applyPass == true)
    {
        //Applying the pass: first start, doWork, and end calls
        curPass->start (cUnit, curPass);

        //Get work function
        bool (*doWork) (CompilationUnit *, BasicBlock *) = curPass->getWork ();

        if (doWork != 0)
        {
            //Set the data in cUnit
            cUnit->passData = curPass->getData ();

            //Determine if pass is iterative
            bool isIterative = curPass->getFlag (kOptimizationNeedIterative);

            dvmCompilerDataFlowAnalysisDispatcher (cUnit, doWork, curPass->getTraversal (), isIterative);
            cUnit->passData = 0;
        }

        curPass->end (cUnit, curPass);

        //Do we need any clean up?
        handlePassFlag (cUnit, curPass);

        //Let's clean up any data we used during the pass
        curPass->freePassData ();
    }

    //If the pass gate passed, we can declare success
    return applyPass;
}

bool dvmCompilerRunPass (CompilationUnit *cUnit, const char *passName)
{
    //Paranoid
    if (cUnit == 0 || passName == 0)
    {
        return false;
    }

    //Go through all the passes and find the requested pass
    Pass *pass = gDvmJit.jitFramework.firstPass;

    //To record successful application of a pass
    bool success = false;

    //Now loop and check the name
    while (pass != 0 && pass->getName() != "")
    {
        //See if the name matches
        if (strcmp(pass->getName().c_str(), passName) == 0)
        {
            success = dvmCompilerRunPass (cUnit, pass);

            //There will be only one matching pass
            break;
        }

        //Go to the next pass
        pass = pass->getNext ();
    }

    return success;
}

 /**
  * @brief The loop
  * @param cUnit the CompilationUnit
  */
void dvmCompilerLaunchPassDriver (CompilationUnit *cUnit)
{
    //Go through the different elements
    Pass *curPass = gDvmJit.jitFramework.firstPass;

    //As long as we have a pass and we haven't decided to quit the loop mode
    while (curPass->getName () != "" && cUnit->quitLoopMode == false)
    {
        //First, did someone want us to skip this pass?
        //If they did, they used the option -Xjitignorepass:"Pass name"
        //So if strstr returns something, someone wanted to ignore that pass
        if (gDvmJit.ignorePasses != 0 && strstr (gDvmJit.ignorePasses, curPass->getName ().c_str ()) != 0)
        {
            curPass = curPass->getNext ();
            continue;
        }

        //Save the print pass flag because we may be updating it.
        //At end of pass, we always restore the printPass flag because it is a compilation unit property and we
        //don't want to leave it permanently changed. For example, someone may enable verbosity by creating a plugin
        //that changes the printPass flag for just a single trace and not globally.
        bool oldPrintPass = cUnit->printPass;

        //Now check if this pass is requested for debug. First we check if all passes should be debugged.
        //Then we check whether just this particular pass should be debugged.
        if (gDvmJit.debugAllPasses == true
                || (gDvmJit.debugPasses != 0 && strstr (gDvmJit.debugPasses, curPass->getName ().c_str ()) != 0))
        {
            cUnit->printPass = true;
        }
        bool dumpCFGAfterOpt = (cUnit->printPass == true && gDvmJit.debugDumpCFGAfterLoopOpt == true);


        //Do we have a general gate defined?
        bool (*generalGate) (const CompilationUnit *, Pass *) = gDvmJit.jitFramework.generalGate;

        //We suppose we can apply the pass
        bool applyPass = true;

        //Apply the general gate
        if (generalGate != 0)
        {
            applyPass = generalGate (cUnit, curPass);
        }

        //If the general gate did not invalidate the pass, continue
        if (applyPass == true)
        {
            //Apply the pass (Ignore the return value of this call)
            dvmCompilerRunPass (cUnit, curPass);
            if (dumpCFGAfterOpt == true)
            {
                ALOGD("Compilation unit's CFG after pass %s",  curPass->getName().c_str());
                dvmCompilerDumpCompilationUnit(cUnit);
                ALOGD("End Compilation unit's CFG after pass %s",  curPass->getName().c_str());
            }
        } else if (dumpCFGAfterOpt == true)
        {
            ALOGD("Loop optimization pass %s not applied - gate returned false",  curPass->getName().c_str());
        }

        //Now restore the printPass flag. For explanation why we restore it, please see description of when we save.
        cUnit->printPass = oldPrintPass;

        //Go to next pass
        curPass = curPass->getNext ();
    }
}

bool dvmCompilerTraceIsLoop (const CompilationUnit *cUnit, Pass *curPass)
{
    (void) curPass;
    return cUnit->jitMode == kJitLoop;
}

bool dvmCompilerTraceIsLoopNewSystem (const CompilationUnit *cUnit, Pass *curPass)
{
    (void) curPass;
    return dvmCompilerTraceIsLoop (cUnit, curPass) && (gDvmJit.oldLoopDetection == false);
}

bool dvmCompilerTraceIsLoopOldSystem (const CompilationUnit *cUnit, Pass *curPass)
{
    (void) curPass;
    return dvmCompilerTraceIsLoop (cUnit, curPass) && (gDvmJit.oldLoopDetection == true);
}

void dvmCompilerPrintPassNames (void)
{
    //Go through the different elements
    Pass *curPass = gPasses;

    ALOGI ("Loop Passes are:");
    //As long as we have a pass and we haven't decided to quit the loop mode
    while (curPass->getName () != "")
    {
        ALOGI ("\t-%s", curPass->getName ().c_str ());
        curPass = curPass->getNext ();
    }
}

void dvmCompilerPrintIgnorePasses (void)
{
    //Suppose the worse
    bool ignoredSomething = false;

    //Go through the different elements
    Pass *curPass = gPasses;
    ALOGI ("Ignoring Passes:");
    //As long as we have a pass and we haven't decided to quit the loop mode
    while (curPass->getName () != "")
    {
        if (gDvmJit.ignorePasses != 0 && strstr (gDvmJit.ignorePasses, curPass->getName ().c_str ()) != 0)
        {
            ALOGI ("\t-%s", curPass->getName ().c_str ());
            //Set flag to true, we are going to ignore something
            ignoredSomething = true;
        }
        curPass = curPass->getNext ();
    }

    //If nothing got ignored, print out information about it
    if (ignoredSomething == false)
    {
        //Paranoid
        if (gDvmJit.ignorePasses == 0)
        {
            ALOGI ("\tThe ignore pass information is missing, please use -Xjitignorepasses");
        }
        else
        {
            ALOGI ("\t- Nothing got ignored, you must put in the parameter of -Xjitignorepasses the exact spelling of one of the passes");
            ALOGI ("\t- Here is what you provided %s", gDvmJit.ignorePasses);
            ALOGI ("\t- Here are the loop passes for reference:");
            dvmCompilerPrintPassNames ();
        }
    }
}


/**
 * @brief Helper function to dvmCompilerCheckLoops to check number of exits for each loop
 * @param info loop information to check
 * @param data required by interface, not used.
 */
static bool exitLoopHelper (LoopInformation *info, void *data)
{
    return dvmCountSetBits (info->getExitLoops ()) <= 1;
}

/**
 * @brief Check if the loops are formed the way we want (nested / branches on / off)
 * @param cUnit the CompilationUnit
 * @return Whether to continue as a loop or bail
 */
static bool dvmCompilerCheckLoops (CompilationUnit *cUnit)
{
    // Build loop information
    LoopInformation *loopInfo = LoopInformation::getLoopInformation (cUnit, 0);
    cUnit->loopInformation = loopInfo;

    if (loopInfo == 0)
    {
        BAIL_LOOP_COMPILATION();
    }
    else
    {
        //We must be in loop mode if we got here
        if (dvmCompilerTraceIsLoop (cUnit, 0) == false)
        {
            return false;
        }

        // we are rejecting sibling loops at top level
        if (loopInfo->getNextSibling () !=0)
        {
            BAIL_LOOP_COMPILATION();
        }

        // Check that we have no more than one loop output
        // Note we are working before loop formation, so exit block is real one
        if (loopInfo->iterate (exitLoopHelper) == false)
        {
            BAIL_LOOP_COMPILATION();
        }

        //If it is nested and option says no, bail
        if (gDvmJit.nestedLoops == false)
        {
            // We do not have sibling so it is enough to check first
            if (loopInfo->getNested () != 0)
            {
                BAIL_LOOP_COMPILATION();
            }

            //Ok second possibility to reject is if we only want simple loops
            if (gDvmJit.branchLoops == false)
            {
                //Now check if we have only one backward branch: to only enable "simple" loops
                const BitVector *backwards = loopInfo->getBackwardBranches ();

                if (backwards == 0)
                {
                    BAIL_LOOP_COMPILATION();
                }

                if (dvmCountSetBits (backwards) != 1)
                {
                    BAIL_LOOP_COMPILATION();
                }

                //Finally, get the basic block for the backward branch
                int backwardIdx = dvmHighestBitSet (backwards);

                if (backwardIdx < 0)
                {
                    BAIL_LOOP_COMPILATION();
                }

                BasicBlock *backward = (BasicBlock *) dvmGrowableListGetElement (&cUnit->blockList, backwardIdx);

                //If nil or no domination loopInformation, bail
                if (backward == 0 || backward->dominators == 0)
                {
                    BAIL_LOOP_COMPILATION();
                }

                //Now go through each BB and see if it dominates backward
                BitVector *blocks = loopInfo->getBasicBlocks ();

                BitVectorIterator bvIterator;
                dvmBitVectorIteratorInit(blocks, &bvIterator);
                for (BasicBlock *bb = dvmCompilerGetNextBasicBlockViaBitVector(bvIterator, cUnit->blockList); bb != 0;
                        bb = dvmCompilerGetNextBasicBlockViaBitVector(bvIterator, cUnit->blockList))
                {
                    if (bb == backward)
                    {
                        continue;
                    }

                    //We bail if it doesn't dominate
                    if (dvmIsBitSet (backward->dominators, bb->id) == 0)
                    {
                        BAIL_LOOP_COMPILATION();
                    }
                }
            }
        }
    }

    return true;
}

/**
 * @brief Check if the loops are formed the way we want (nested / branches on / off)
 * @param cUnit the CompilationUnit
 * @param curPass the current Pass
 * @return Whether to continue as a loop or bail
 */
bool checkLoopsGate (const CompilationUnit *cUnit, Pass *curPass)
{
    //Unused parameter
    (void) curPass;

    return dvmCompilerCheckLoops (const_cast<CompilationUnit *>(cUnit));
}

/**
 * @brief Used to check whether resolution is required for opcode
 * @param opcode The opcode to check
 * @return Returns true if resolution is required. Returns false if backend can handle no resolution.
 */
static bool mustResolve (Opcode opcode)
{
    switch (opcode)
    {
        case OP_IGET_QUICK:
        case OP_IGET_WIDE_QUICK:
        case OP_IGET_OBJECT_QUICK:
        case OP_IPUT_QUICK:
        case OP_IPUT_WIDE_QUICK:
        case OP_IPUT_OBJECT_QUICK:
        case OP_INVOKE_VIRTUAL_QUICK:
        case OP_INVOKE_VIRTUAL_QUICK_RANGE:
        case OP_INVOKE_SUPER_QUICK:
        case OP_INVOKE_SUPER_QUICK_RANGE:
            //Quick versions do not need resolution because they use index generated
            //during the verification stage.
            return false;
        case OP_INVOKE_INTERFACE:
        case OP_INVOKE_INTERFACE_RANGE:
            //The backends generate code that can look up method invoked which
            //includes being able to do resolution.
            return false;
        default:
            break;
    }

    return true;
}

bool dvmCompilerCheckReferences (CompilationUnit *cUnit, BasicBlock *bb)
{
    //Check all of the MIRs in this basic block
    for (MIR *mir = bb->firstMIRInsn; mir != 0; mir = mir->next)
    {
        //Skip bytecodes whose resolution we do not care about
        if (mustResolve (mir->dalvikInsn.opcode) == false)
        {
            continue;
        }

        bool resolved = dvmCompilerCheckResolvedReferences (cUnit->method, &mir->dalvikInsn,
                false /*tryToResolve*/);

        if (resolved == false)
        {
            PASS_LOG (ALOGD, cUnit, "Check_References: Failed to resolve references for %s",
                    dvmCompilerGetDalvikDisassembly (&mir->dalvikInsn, 0));

            //We found an unresolved reference
            cUnit->quitLoopMode = true;

            //We have not changed the BB
            return false;
        }
    }

    //All references checked are resolved but we have not updated the BB
    return false;
}

//Get a given pass
Pass *dvmCompilerGetPass (const char *name)
{
    //Go through the different elements
    Pass *curPass = gDvmJit.jitFramework.firstPass;

    //Find the pass we care about
    while (curPass->getName () != "" && curPass->getName () != name)
    {
        //Next pass
        curPass = curPass->getNext ();
    }

    //If not found
    if (curPass->getName () == "")
    {
        return 0;
    }

    //Otherwise return the pass
    return curPass;
}

//Remove a given pass
bool dvmCompilerRemovePass (const char *name)
{
    //Find the pass
    Pass *curPass = dvmCompilerGetPass (name);

    //Paranoid: didn't find the name
    if (curPass == 0)
    {
        ALOGI ("\tRemoving a pass could not find the reference pass name, here is what you provided %s", name);
        ALOGI ("\t- Here are the loop passes for reference:");
        dvmCompilerPrintPassNames ();
        return false;
    }

    Pass *previous = curPass->getPrevious ();
    Pass *next = curPass->getNext ();

    //Update previous
    if (previous != 0)
    {
        previous->setNext (next);
    }
    else
    {
        //Update global list otherwise
        gDvmJit.jitFramework.firstPass = next;
    }

    //Update next
    if (next != 0)
    {
        next->setPrevious (previous);
    }

    //Report success
    return true;
}

//Insert a user pass
bool dvmCompilerInsertUserPass (Pass *newPass, const char *name, enum PassInstrumentation mode)
{
    //Find the pass
    Pass *curPass = dvmCompilerGetPass (name);

    //Paranoid: didn't find the name
    if (curPass == 0)
    {
        ALOGI ("Pass Modification could not find the reference pass name, here is what you provided %s", name);
        ALOGI ("\t- Here are the loop passes for reference:");
        dvmCompilerPrintPassNames ();
        return false;
    }

    //We have the pass reference, what we do now depends on the mode
    switch (mode)
    {
        case kPassInstrumentationReplace:
            {
                Pass *previous = curPass->getPrevious ();
                Pass *next = curPass->getNext ();

                //Update links
                if (previous != 0)
                {
                    previous->setNext (newPass);
                }
                else
                {
                    gDvmJit.jitFramework.firstPass = curPass;
                }

                if (next != 0)
                {
                    next->setPrevious (newPass);
                }

                newPass->setNext (next);
                newPass->setPrevious (previous);
            }
            break;
        case kPassInstrumentationInsertBefore:
            {
                Pass *previous = curPass->getPrevious ();

                //Update links
                newPass->setPrevious (previous);
                newPass->setNext (curPass);

                //If curPass was the first pass
                if (previous == 0)
                {
                    //Update it now
                    gDvmJit.jitFramework.firstPass = newPass;
                }
                else
                {
                    previous->setNext (newPass);
                }

                curPass->setPrevious (newPass);
            }
            break;
        case kPassInstrumentationInsertAfter:
            {
                Pass *next = curPass->getNext ();

                //Update links
                newPass->setNext (next);
                newPass->setPrevious (curPass);
                curPass->setNext (newPass);

                //Handle next
                if (next != 0)
                {
                    next->setPrevious (newPass);
                }
            }
            break;
        default:
            break;
    }

    //Report success
    return true;
}

void dvmCompilerSetGeneralGate (bool (*general) (const CompilationUnit *, Pass*))
{
    gDvmJit.jitFramework.generalGate = general;
}

//Replace a given pass gate
bool dvmCompilerReplaceGate (const char *name, bool (*gate) (const CompilationUnit *, Pass*))
{
    //Find the pass
    Pass *curPass = dvmCompilerGetPass (name);

    //Paranoid: didn't find the name
    if (curPass == 0)
    {
        ALOGI ("Pass Modification could not find the reference pass name, here is what you provided %s", name);
        ALOGI ("\t- Here are the loop passes for reference:");
        dvmCompilerPrintPassNames ();
        return false;
    }

    //Set the gate
    curPass->setGate (gate);

    //Report success
    return true;
}

bool dvmCompilerReplaceEnd (const char *name, void (*endWork) (CompilationUnit *, Pass*))
{
    //Find the pass
    Pass *curPass = dvmCompilerGetPass (name);

    //Paranoid: didn't find the name
    if (curPass == 0)
    {
        ALOGI ("Pass Modification could not find the reference pass name, here is what you provided %s", name);
        ALOGI ("\t- Here are the loop passes for reference:");
        dvmCompilerPrintPassNames ();
        return false;
    }

    //Set the end work function
    curPass->setEndWork (endWork);

    //Report success
    return true;
}

//Handle a user plugin
void dvmCompilerHandleUserPlugin (const char *fileName)
{
    //Let us be optimistic
    bool failure = false;

    //Open the file
    void *userHandle = dlopen (fileName, RTLD_NOW);

    if (userHandle != 0)
    {
        //Open now the init function
        void *tmp = dlsym (userHandle, "dalvikPluginInit");

        if (tmp == 0)
        {
            ALOGD ("PLUGIN: Problem with %s, cannot find dalvikPluginInit function\n", fileName);

            //Set failure flag
            failure = true;
        }
        else
        {
            //Transform it into a function pointer
            bool (*pluginInitialization) (void) = (bool (*) (void)) (tmp);

            //Call it
            failure = (pluginInitialization () == false);
        }
    }
    else
    {
        ALOGD ("PLUGIN: Problem opening user plugin file %s", fileName);
        ALOGD ("PLUGIN: dlerror() reports %s", dlerror());

        //Set failure flag
        failure = true;
    }

    //If the failure flag is on
    if (failure == true)
    {
        ALOGD ("PLUGIN: Initialization function in %s failed", fileName);
        //Is the failure on the user plugin fatal?
        if (gDvmJit.userpluginfatal == true)
        {
            dvmAbort ();
        }

        //Signal we failed loading a plugin
        gDvmJit.userpluginfailed = true;
    }
}
