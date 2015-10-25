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

#include "Dalvik.h"
#include "Pass.h"

//Constructor and Destructor
Pass::Pass (const std::string &name,
        DataFlowAnalysisMode traversalType,
        void *data,
        bool (*gatePtr) (const CompilationUnit *, Pass *),
        void (*startPtr) (CompilationUnit *, Pass *),
        void (*endPtr) (CompilationUnit *, Pass *),
        bool (*doWorkPtr) (CompilationUnit *, BasicBlock *),
        void (*freeDataPtr) (void *),
        unsigned int flags)
{
    this->passName = name;
    this->traversalType = traversalType;
    this->data = data;
    this->gatePtr = gatePtr;
    this->startPtr = startPtr;
    this->endPtr = endPtr;
    this->doWorkPtr = doWorkPtr;
    this->freeDataPtr = freeDataPtr;
    this->flags = flags;
    this->next = 0;
    this->previous = 0;
}

void Pass::freePassData (void)
{
    //Do we have to free anything ?
    if (freeDataPtr != 0)
    {
        freeDataPtr (data), data = 0;
    }

    /* We set data again to 0, it might have been done, but
     * let's be paranoid and let us not remove the set 0 in the
     * if to keep us from making subsequent post free mistakes
     */
    data = 0;
}

const std::string &Pass::getName (void) const
{
    return passName;
}

DataFlowAnalysisMode Pass::getTraversal (void) const
{
    return traversalType;
}

void *Pass::getData (void) const
{
    return data;
}

void Pass::setData (void *data)
{
    this->data = data;
}

bool Pass::getFlag (OptimizationFlag flag) const
{
    unsigned int max = ~0;

    //Paranoid: trying to get something too high
    if (flag >= max)
        return false;

    return (flags & flag);
}

void Pass::setFlag (OptimizationFlag flag, bool value)
{
    unsigned int max = ~0;

    //Paranoid: trying to set something too high
    if (flag >= max)
        return;

    //Are we setting it or clearing it?
    if (value == true)
    {
        flags |= flag;
    }
    else
    {
        //Reverse mask
        unsigned int mask = ~flag;

        flags &= mask;
    }
}

bool Pass::gate (const CompilationUnit *cUnit, Pass *curPass) const
{
    //Do we have the pointer?
    if (gatePtr != 0)
    {
        return gatePtr (cUnit, curPass);
    }

    //Otherwise, say yes
    return true;
}

void Pass::start (CompilationUnit *cUnit, Pass *curPass) const
{
    //Do we have the pointer?
    if (startPtr != 0)
    {
        return startPtr (cUnit, curPass);
    }

}

void Pass::end (CompilationUnit *cUnit, Pass *curPass) const
{
    //Do we have the pointer?
    if (endPtr != 0)
    {
        return endPtr (cUnit, curPass);
    }
}

fctWorkPtr Pass::getWork (void) const
{
    return doWorkPtr;
}
