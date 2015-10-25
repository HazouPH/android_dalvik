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

#include "SSAWalkData.h"

SSAWalkData::SSAWalkData (CompilationUnit *cUnit)
{
    //Set up the free chain list
    freeChainsList = & (cUnit->globalDefUseChain);
    freeChains = cUnit->globalDefUseChain;
}

SSAWalkData::~SSAWalkData (void)
{
    //Clear maps
    lastChain.clear ();
    definitions.clear ();
}

SUsedChain *SSAWalkData::getUsedChain (void)
{
    SUsedChain *res = freeChains;

    //If we don't have one
    if (res == 0)
    {
        res = static_cast<SUsedChain *> (dvmCompilerNew (sizeof (*res), true));

        //Attach it to global free chains
        res->nextChain = *freeChainsList;
        *freeChainsList = res;
    }
    else
    {
        //We can move forward in the freeChains list
        freeChains = freeChains->nextChain;
    }

    //Reset it
    res->nextUse = 0;
    res->prevUse = 0;
    res->mir = 0;

    return res;
}

SUsedChain *SSAWalkData::getLastChain (int value)
{
    return lastChain[value];
}

void SSAWalkData::setLastChain (SUsedChain *chain, int value)
{
    lastChain[value] = chain;
}

void SSAWalkData::setDefinition (MIR *insn, int value)
{
    definitions[value] = insn;
}

MIR *SSAWalkData::getDefinition (int value) const
{
    std::map<int, MIR *>::const_iterator it = definitions.find (value);

    //If not found, return 0
    if (it == definitions.end ())
    {
        return 0;
    }

    //Otherwise, we can return it
    return it->second;
}

void SSAWalkData::addUseToDefChain (int useIdx, MIR *used, MIR *defined)
{
    SSARepresentation *ssaRep = used->ssaRep;

    //Set defWhere for this instruction
    ssaRep->defWhere[useIdx] = defined;

    if (defined == 0)
    {
        return;
    }

    //We have a need of a new chain element
    SUsedChain *elem = getUsedChain ();

    //Set its mir
    elem->mir = used;

    //Get use value
    int value = ssaRep->uses[useIdx];

    //Get the last use for this element
    SUsedChain *last = getLastChain (value);

    //Last, set it as the new last
    setLastChain (elem, value);

    //If we have one, just chain
    if (last != 0)
    {
        last->nextUse = elem;
        elem->prevUse = last;
        return;
    }

    //It's the first, tell defined about it
    SSARepresentation *defSSA = defined->ssaRep;

    //Paranoid
    assert (defSSA != 0);

    //Go through the defines and find value
    int max = defSSA->numDefs;
    for (int j = 0; j < max; j++)
    {
        if (defSSA->defs[j] == value)
        {
             defSSA->usedNext[j] = elem;
             return;
        }
    }

    //We should have found it
    assert (false);
}

void SSAWalkData::handleNoDefinitions (void)
{
    //Go through each no definition
    for (std::vector<SWalkDataNoDefine>::const_iterator it = noDefine.begin ();
                                                          it != noDefine.end ();
                                                          it++)
    {
        //Get local versions
        const SWalkDataNoDefine &info = *it;
        MIR * mir = info.mir;
        int idx = info.index;

        SSARepresentation *ssaRep = mir->ssaRep;

        //Paranoid
        assert (ssaRep != 0);
        assert (0 <= idx && idx < ssaRep->numUses);

        //Get use value
        int value = ssaRep->uses[idx];

        //Set defWhere for this instruction
        MIR *defined = getDefinition (value);

        addUseToDefChain (idx, mir, defined);
    }
}

void SSAWalkData::addNoDefine (MIR *mir, int idx)
{
    //Create the data and set it
    SWalkDataNoDefine info = {mir, idx};
    noDefine.push_back (info);
}
