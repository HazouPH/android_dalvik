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

#include <limits.h>
#include <list>
#include <sys/mman.h>

#include "Analysis.h"
#include "BasicBlockPCG.h"
#include "CodeGeneration.h"
#include "CompilationErrorPCG.h"
#include "CompilationUnitPCG.h"
#include "Compiler.h"
#include "CompilerInternals.h"
#include "libpcg.h"
#include "Labels.h"
#include "Lower.h"
#include "LowerCall.h"
#include "NcgHelper.h"
#include "PassDriver.h"
#include "PersistentInfo.h"
#include "Singleton.h"
#include "UtilityPCG.h"
#include "X86Common.h"

/**
 * @brief Return an appropriate ISA string based on gDvmJit configuration
 * @return An ISA string appropriate for PCG configuration
 */
static const char * pcgGetISALevel (void)
{
    const char * returnString = "atom_ssse3";

    if (dvmCompilerArchitectureSupportsSSE42 () == true)
    {
        returnString = "atom_sse4.2";
    }

    return returnString;
}

/**
 * @brief Configure the code generation
 * @param cUnit the CompilationUnitPCG
 */
static void pcgConfigureTrace (CompilationUnitPCG *cUnit)
{
    CGConfigureRoutine ("esp_frame", "on",(void*)0);

    if (cUnit->checkDebugMask (DebugMaskPil) == true)
    {
        CGConfigureRoutine ("debug_level", "1",(void*)0);
    }

    const char * isaString = pcgGetISALevel ();

    std::string s;
    if (dvmExtractBackendOption ("TargetIsa", s))
    {
        isaString = s.c_str();
    }

    if (cUnit->checkDebugMask (DebugMaskDisasm) == true)
    {
        ALOGD ("Setting PCG to use ISA string %s", isaString);
    }

    CGConfigureRoutine ("target_arch", isaString,
                        "eliminate_frame_pointer", "off",
                        "stack_alignment_compatibility", "0",
                        "expand_32b_idiv_irem", "false",
                        (void*)0);

    if (cUnit->checkDebugMask (DebugMaskAsm) == true)
    {
        CGSetAsmOutputFile (stderr);
    }
}

/**
 * @brief PCG version of the function to lower middle-level IR to low-level IR
 * @details Updated global state:
 *      gDvmJit.codeCacheByteUsed
 * @param cUnitME the CompilationUnit
 * @param info the JitTranslationInfo
 */
void pcgDvmCompilerMIR2LIR (CompilationUnit *cUnitME, JitTranslationInfo* info)
{
    //Create a client context for the error system
    jmp_buf client_context;
    jmp_buf *prev_context = CGGetSetjmpContext();

    //Create a cUnit for this cUnitME
    CompilationUnitPCG cUnit (cUnitME);

    // cUnit->baseAddr gives the address of the entry point for the compiled
    // trace.  Note that this might not be the first available byte in the code
    // cache, because we might need to add padding to align the start of the
    // routine.  We set this to 0 here, because that is a signal to the
    // caller that an error occured during compilation.  We will set it to a
    // valid value after successfull compilation.
    cUnitME->baseAddr = 0;
    info->codeAddress = 0;

    //Prepare the set jump system
    if (setjmp(client_context) == 0)
    {
        //If that succeeded, we can proceed
        //Provide to PCG the context
        CGSetSetjmpContext (&client_context);

        if (cUnitME->printMe == true) {
            cUnit.setDebugLevel (DebugMaskDisasm);
        }

        if (cUnit.checkDebugMask (DebugMaskTrace) == true)
        {
            CGSetTraceOutputFile (stdout);
        }

        CGCreateRoutine (&cUnit);

        // clear any previous JIT errors
        cUnit.errorHandler->clearErrors ();

        //If analysis succeeds continue
        if (dvmCompilerPcgNewRegisterizeVRAnalysis (&cUnit) == true)
        {
            if (cUnit.registerizeAnalysisDone () == true)
            {
                dvmCompilerDataFlowAnalysisDispatcher (&cUnit, dvmCompilerPcgFillReferencedSsaVector,
                        kAllNodes, false);

                dvmCompilerPcgModSSANum (&cUnit);

                pcgConfigureTrace (&cUnit);

                bool success = dvmCompilerPcgGenerateIlForTrace (&cUnit, info);

                // Note that if !success, we leave cUnit->baseAddr as 0.
                if (success == true)
                {
                    CGCompileRoutine (&cUnit);

                    UNPROTECT_CODE_CACHE((char*)gDvmJit.codeCache + gDvmJit.codeCacheByteUsed,
                                         gDvmJit.codeCacheSize - gDvmJit.codeCacheByteUsed);
                    dvmCompilerPcgEmitCode (&cUnit, info);
                    PROTECT_CODE_CACHE((char*)gDvmJit.codeCache + gDvmJit.codeCacheByteUsed,
                                         gDvmJit.codeCacheSize - gDvmJit.codeCacheByteUsed);
                }
            }
        }

        CGEndRoutine (0);
    }
    else {
        ALOGD ("JIT_INFO: PCG did not compile the trace %s%s@%#x\n",
                cUnit.method->clazz->descriptor, cUnit.method->name,
                cUnit.traceDesc->trace[0].info.frag.startOffset);
    }

    //Set jmp back
    CGSetSetjmpContext(prev_context);

    // Delete information about local symbols
    for (LocalSymbolIterator it = cUnit.localSymbolBegin();
         it != cUnit.localSymbolEnd();
         ++it)
    {
        singletonPtr<PersistentInfo> ()->eraseSymbolInfo(*it);
    }

    //Copy back
    CompilationUnit *cUnitLimited = &cUnit;
    *cUnitME = *cUnitLimited;
}

/**
 * @brief Create a call back
 * @param name the name of the function
 * @param addr the address of the function
 */
static void dvmCompilerPcgCreateCallBack (const char * name, void *addr)
{
    // Callbacks are all module level, so we can pass a NULL cUnit to
    // dvmCompilerPcgCreateSymbol.
    CompilationUnitPCG *cUnit = NULL;
    singletonPtr<PersistentInfo> ()->setCallBack (name, dvmCompilerPcgCreateSymbol (cUnit, name, addr, false, false, true));
}


//External definition for the module initialization
extern "C" int64_t __muldi3(int64_t, int64_t);
extern "C" int64_t __divdi3(int64_t, int64_t);
extern "C" int64_t __udivdi3(uint64_t, uint64_t);
extern "C" int64_t __moddi3(int64_t, int64_t);
extern "C" int64_t __umoddi3(uint64_t, uint64_t);
extern "C" int64_t __ashldi3(int64_t, int32_t);
extern "C" int64_t __ashrdi3(int64_t, int32_t);
extern "C" int64_t __lshrdi3(int64_t, int32_t);

/**
 * @brief PCG module level initialization
 */
void pcgModuleBegin (void)
{
    CGCreateModule (0);

    //Add muldi3
    dvmCompilerPcgCreateCallBack("__muldi3", (void *) __muldi3);
    dvmCompilerPcgCreateCallBack("__divdi3", (void *) __divdi3);
    dvmCompilerPcgCreateCallBack("__udivdi3", (void *) __udivdi3);
    dvmCompilerPcgCreateCallBack("__moddi3", (void *) __moddi3);
    dvmCompilerPcgCreateCallBack("__umoddi3", (void *) __umoddi3);
    dvmCompilerPcgCreateCallBack("__ashldi3", (void *) __ashldi3);
    dvmCompilerPcgCreateCallBack("__ashrdi3", (void *) __ashrdi3);
    dvmCompilerPcgCreateCallBack("__lshrdi3", (void *) __lshrdi3);

#ifdef DEBUG_HOOK
    void *hookPtr = dvmCompilerPcgCreateHookFunction ();
    dvmCompilerPcgCreateCallBack ("debugHook",  hookPtr);
#endif
}

/**
 * @brief Allocate a BasicBlockPCG and return its BasicBlock parent
 * @return the BasicBlock pointer
 */
static BasicBlock *pcgBBAllocator (void)
{
    // Make space on arena for this BB
    void * space = dvmCompilerNew(sizeof(BasicBlockPCG), true);

    // Ensure that constructor is called
    BasicBlockPCG * newBB = new (space) BasicBlockPCG;

    // Paranoid because dvmCompilerNew should never return 0
    assert(newBB != 0);

    return newBB;
}

/**
 * @brief Used to obtain the maximum number of scratch registers that PCG backend can support
 * @return Returns the maximum number of scratch
 */
static unsigned int pcgGetMaxScratch (void)
{
    //We want to support a large number of scratch VRs so that from point of view of middle-end
    //the supply of them seems unlimited. However, due to how CGTemps are represented, we are
    //limited in how many we can create to around 2^32. However, we want to use CGTemps for
    //registerizing VRs, vectorization, and possibly others. Thus here we just set a limit
    //on scratch registers to USHRT_MAX. Technically we can have even more than that but
    //from point of view of middle-end, it would be surprising if we ran into a limitation soon.
    return USHRT_MAX;
}

/**
 * @brief Initialization of the plugin for the PCG back-end
 */
extern "C" void setupPcgJit (void)
{
    //Now set the default function pointers
    SJitFramework &jitFramework = gDvmJit.jitFramework;

    jitFramework.backEndGate = dvmCompilerPcgSupportTrace;
    jitFramework.backEndFunction = pcgDvmCompilerMIR2LIR;
    jitFramework.middleEndFunction = dvmCompileTrace;
    jitFramework.backEndSymbolCreationCallback = dvmCompilerPcgCreateCallBack;
    jitFramework.backEndBasicBlockAllocation = pcgBBAllocator;
    jitFramework.backEndDumpSpecificBB = 0;
    jitFramework.backEndInvokeArgsDone = dvmCompilerPcgHandleInvokeArgsHeader;
    jitFramework.backendSupportExtendedOp = dvmCompilerPcgSupportsExtendedOp;
    jitFramework.backEndCompilationErrorHandlerAllocation = dvmCompilerPcgNewCompilationErrorHandler;
    jitFramework.scratchRegAvail = pcgGetMaxScratch;

    // Register callbacks
    CGRegisterCallbackRoutine("CGGetMemConstSymbolFromClient",
                              (void*)pcgDvmClientGetMemConstSymbol);
}

/*
 * @brief Fix the pass driver for PCG
 */
static void passHandler (void)
{
    //PCG does not need write back information or registerization
    dvmCompilerRemovePass ("Write_Back_Registers");
    dvmCompilerRemovePass ("Registerization_ME");
}


/**
 * @brief Plugin initialization
 * @return true if successfully initialized, false if otherwise
 */
extern "C" bool dalvikPluginInit (void)
{
    if (dvmCompilerDataStructureSizeCheck(sizeof(DvmJitGlobals)) == false)
    {
        ALOGE ("PCG error: Critical datastructures in the DVM ME and PCG GL have different sizes, not loading.");
        return false;
    }

    ALOGI ("\n+++++++++++++ Using PCG. +++++++++++++++++++++\n");

    // Initialize PCG.
    pcgModuleBegin ();

    setupPcgJit ();

    //Remove ME passes that PCG does not require
    passHandler ();

    return true;
}
