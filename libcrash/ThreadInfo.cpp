/*
**
** Copyright 2013, Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
*/

#include "Dalvik.h"
#include "ThreadInfo.h"

/*
 * @brief Extract the object that is the target of a monitor-enter instruction
 * in the top stack frame of "thread".
 * @details Note: the other thread might be alive, so this has to work carefully.
 * The thread list lock must be held.
 * @param thread pointer to the thread object
 * @param pLockObj
 * @param pOwner
 * @return Returns "true" if we successfully recover the object.  "*pOwner" will
 * be NULL if we can't determine the owner for some reason (e.g. race
 * condition on ownership transfer).
 */
static bool extractMonitorEnterObject(Thread *thread, Object **pLockObj,
    Thread **pOwner)
{
    u4 *framePtr = thread->interpSave.curFrame;

    if (framePtr == 0 || dvmIsBreakFrame(framePtr) == true) {
        return false;
    }

    const StackSaveArea *saveArea = SAVEAREA_FROM_FP(framePtr);
    const Method *method = saveArea->method;
    const u2 *currentPc = saveArea->xtra.currentPc;

    /* check Method* */
    if (dvmLinearAllocContains(method, sizeof(Method)) == false) {
        return false;
    }

    /* check currentPc */
    u4 insnsSize = dvmGetMethodInsnsSize(method);
    if (currentPc < method->insns ||
        currentPc >= method->insns + insnsSize)
    {
        return false;
    }

    /* check the instruction */
    if ((*currentPc & 0xff) != OP_MONITOR_ENTER) {
        return false;
    }

    /* get and check the register index */
    unsigned int reg = *currentPc >> 8;
    if (reg >= method->registersSize) {
        return false;
    }

    /* get and check the object in that register */
    Object *obj = (Object *)framePtr[reg];
    if (obj != 0 && dvmIsHeapAddress(obj) == false) {
        return false;
    }
    *pLockObj = obj;

    /*
     * Try to determine the object's lock holder; it's okay if this fails.
     *
     * We're assuming the thread list lock is already held by this thread.
     * If it's not, we may be living dangerously if we have to scan through
     * the thread list to find a match.  (The VM will generally be in a
     * suspended state when executing here, so this is a minor concern
     * unless we're dumping while threads are running, in which case there's
     * a good chance of stuff blowing up anyway.)
     */
    if (obj != 0) {
        *pOwner = dvmGetObjectLockHolder(obj);
        return true;
    }

    return false;
}

/*
 * @brief Dump stack frames, starting from the specified frame and moving down.
 * @details Each frame holds a pointer to the currently executing method, and the
 * saved program counter from the caller ("previous" frame).  This means
 * we don't have the PC for the current method on the stack, which is
 * pretty reasonable since it's in the "PC register" for the VM.  Because
 * exceptions need to show the correct line number we actually *do* have
 * an updated version in the fame's "xtra.currentPc", but it's unreliable.
 * Note "framePtr" could be NULL in rare circumstances.
 * @param buff pointer to the structure that describes output buffer
 * @param framePtr pointer to the frame stack
 * @param thread pointer to the thread object
 */
static void dumpFrames(ts_buf *buff, void *framePtr, Thread *thread)
{
    const StackSaveArea *saveArea;
    const Method *method;
    int checkCount = 0;
    const u2 *currentPc = 0;
    bool first = true;
    const int MAX_METHOD_NAME_SIZE = 256;
    char methodName[MAX_METHOD_NAME_SIZE];

    /*
     * The "currentPc" is updated whenever we execute an instruction that
     * might throw an exception.  Show it here.
     */
    if (framePtr != 0 && dvmIsBreakFrame((u4 *)framePtr) == false) {
        saveArea = SAVEAREA_FROM_FP(framePtr);

        if (saveArea->xtra.currentPc != 0) {
            currentPc = saveArea->xtra.currentPc;
        }
    }

    while (framePtr != 0) {
        saveArea = SAVEAREA_FROM_FP(framePtr);
        method = saveArea->method;

        if (dvmIsBreakFrame((u4 *)framePtr) == false) {
            int relPc;

            if (currentPc != 0) {
                relPc = currentPc - saveArea->method->insns;
            }
            else {
                relPc = -1;
            }

            dvmHumanReadableMethodWithOutSignature(method, methodName, MAX_METHOD_NAME_SIZE);
            if (dvmIsNativeMethod(method) == true) {
                writeDebugMessage(buff, "  at %s(Native Method)\n",
                        methodName);
            } else {
                writeDebugMessage(buff, "  at %s(%s:%s%d)\n",
                        methodName, dvmGetMethodSourceFile(method),
                        (relPc >= 0 && first) ? "~" : "",
                        relPc < 0 ? -1 : dvmLineNumFromPC(method, relPc));
            }

            if (first == true) {
                /*
                 * Decorate WAIT and MONITOR threads with some detail on
                 * the first frame.
                 *
                 * warning: wait status not stable, even in suspend
                 */
                if (thread->status == THREAD_WAIT ||
                    thread->status == THREAD_TIMED_WAIT)
                {
                    Monitor *mon = thread->waitMonitor;
                    Object *obj = dvmGetMonitorObject(mon);
                    if (obj != 0) {
                        Thread *joinThread = 0;
                        if (obj->clazz == gDvm.classJavaLangVMThread) {
                            joinThread = dvmGetThreadFromThreadObject(obj);
                        }
                        writeDebugMessage(buff, "on", obj, joinThread);
                    }
                } else if (thread->status == THREAD_MONITOR) {
                    Object *obj = 0;
                    Thread *owner = 0;
                    if (extractMonitorEnterObject(thread, &obj, &owner) == true) {
                        writeDebugMessage(buff, "to lock", obj, owner);
                    }
                }
            }
        }

        /*
         * Get saved PC for previous frame.  There's no savedPc in a "break"
         * frame, because that represents native or interpreted code
         * invoked by the VM.  The saved PC is sitting in the "PC register",
         * a local variable on the native stack.
         */
        currentPc = saveArea->savedPc;

        first = false;

        if (saveArea->prevFrame != 0 && saveArea->prevFrame <= framePtr) {
            writeDebugMessage(buff, "Warning: loop in stack trace at frame %d (%p -> %p)",
                checkCount, framePtr, saveArea->prevFrame);
            break;
        }
        framePtr = saveArea->prevFrame;

        checkCount++;
        if (checkCount > 300) {
            writeDebugMessage(buff,
                "  ***** printed %d frames, not showing any more\n",
                checkCount);
            break;
        }
    }
}

void dvmDumpThreadStack(ts_buf *buff, Thread *thread)
{
    if (thread == 0) {
        return;
    }

    dumpFrames(buff, thread->interpSave.curFrame, thread);
}

/*
 * @brief Dump thread specific information
 * @param buff pointer to the structure that describes output buffer
 * @param thread pointer to the thread object
 */
static void dumpThreadInfo(ts_buf *buff, Thread *thread)
{
    Object *threadObj;
    Object *groupObj;
    StringObject *nameStr;
    const int MAX_THREAD_NAME_SIZE = 256;
    char threadName[MAX_THREAD_NAME_SIZE];
    const int MAX_GROUP_NAME_SIZE = 256;
    char groupName[MAX_GROUP_NAME_SIZE] = {0};
    bool isDaemon;

    /*
     * Get the java.lang.Thread object.  This function gets called from
     * some weird debug contexts, so it's possible that there's a GC in
     * progress on some other thread.  To decrease the chances of the
     * thread object being moved out from under us, we add the reference
     * to the tracked allocation list, which pins it in place.
     *
     * If threadObj is 0, the thread is still in the process of being
     * attached to the VM, and there's really nothing interesting to
     * say about it yet.
     */
    threadObj = thread->threadObj;
    if (threadObj == 0) {
        return;
    }

    nameStr = (StringObject *) dvmGetFieldObject(threadObj, gDvm.offJavaLangThread_name);
    dvmConvertStringToCstr(nameStr, threadName, MAX_THREAD_NAME_SIZE);

    isDaemon = dvmGetFieldBoolean(threadObj, gDvm.offJavaLangThread_daemon);

    /* a null value for group is not expected, but deal with it anyway */
    groupObj = (Object *) dvmGetFieldObject(threadObj, gDvm.offJavaLangThread_group);

    if (groupObj != 0) {
        nameStr = (StringObject *)
            dvmGetFieldObject(groupObj, gDvm.offJavaLangThreadGroup_name);
        dvmConvertStringToCstr(nameStr, groupName, MAX_GROUP_NAME_SIZE);
    }

    pthread_t handle = thread->handle;
    size_t stack_size;
    pthread_attr_t attr;
    void *stack_addr;

    pthread_getattr_np(handle, &attr);
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);

    /*
     *  self        group    name     daemon? sysid    obj              stack        JIT?
     * 0x5e96d690 "system" "Compiler" daemon  tid=453 obj=0x42082050 (stack: 0x0000) JIT
     */
    writeDebugMessage(buff,
        "%p \"%s\" \"%s\" %s %d obj=%p (stack: %p) %s\n",
        thread,
        groupName[0] == '\0' ? "(null; initializing?)" : groupName,
        threadName,
        isDaemon ? " daemon" : "",
        thread->systemTid,
        thread->threadObj,
        stack_addr,
#if defined(WITH_JIT)
        thread->inJitCodeCache ? " JIT" : ""
#else
        ""
#endif
        );
}

void dvmDumpThreadList(ts_buf *buff)
{
    Thread *thread;

    thread = gDvm.threadList;
    while (thread != 0) {
        dumpThreadInfo(buff, thread);

        /* verify link */
        assert(thread->next == 0 || thread->next->prev == thread);

        thread = thread->next;
    }
}

