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

#include <vector>
#include <map>
#include <set>

#include "Dalvik.h"
#include "Compiler.h"
#include "CompilerIR.h"
#include "Dataflow.h"
#include "PassDriver.h"
#include "Utility.h"
#include "enc_wrapper.h"

#define PRINT_BUFFER_LEN 1024

void dvmCompilerPrintEmittedCodeBlock (unsigned char *startAddr,
                                       unsigned char *endAddr)
{
    char strbuf[PRINT_BUFFER_LEN];
    unsigned char *addr;
    unsigned char *next_addr;

    static const unsigned char nops[10][9] = {
        { 0, },                                                     // 0, this line is dummy and not used in the loop below
        { 0x90, },                                                  // 1-byte NOP
        { 0x66, 0x90, },                                            // 2
        { 0x0F, 0x1F, 0x00, },                                      // 3
        { 0x0F, 0x1F, 0x40, 0x00, },                                // 4
        { 0x0F, 0x1F, 0x44, 0x00, 0x00, },                          // 5
        { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00, },                    // 6
        { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, },              // 7
        { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, },        // 8
        { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 9-byte NOP
    };
    int nopSize;
    int pos;

    if (gDvmJit.printBinary == true)
    {
        // print binary in bytes
        int n = 0;
        for (addr = startAddr; addr < endAddr; addr++)
        {
            n += snprintf (&strbuf[n], PRINT_BUFFER_LEN-n, "0x%x, ", *addr);
            if (n > PRINT_BUFFER_LEN - 10)
            {
                ALOGD ("## %s", strbuf);
                n = 0;
            }
        }
        if (n > 0)
        {
            ALOGD ("## %s", strbuf);
        }
    }

    // print disassembled instructions
    addr = startAddr;
    while (addr < endAddr)
    {
        next_addr = reinterpret_cast<unsigned char *>
            (decoder_disassemble_instr(reinterpret_cast<char *>(addr),
                                       strbuf, PRINT_BUFFER_LEN));
        if (addr != next_addr)
        {
            ALOGD ("**  %p: %s", addr, strbuf);
        }
        else
        {
            for (nopSize = 1; nopSize < 10; nopSize++)
            {
                for (pos = 0; pos < nopSize; pos++)
                {
                    if (addr[pos] != nops[nopSize][pos])
                    {
                        break;
                    }
                }
                if (pos == nopSize)
                {
                    ALOGD ("**  %p: NOP (%d byte)", addr, nopSize);
                    next_addr += nopSize;
                    break;
                }
            }
            if (nopSize == 10)
            {
                ALOGD ("** unable to decode binary at %p", addr);
                break;
            }
        }
        addr = next_addr;
    }
}

/**
 * @brief Print the content of chaining cell block in code cache to LOG.
 * @param startAddr - starting address of the chaining cell block in code cache
 * @param blockType - chaining cell block type
 */
void printChainingCellBlocks (char *startAddr, BBType blockType)
{
    unsigned int *ui_ptr;

    if (startAddr == 0 || blockType >= kChainingCellGap)
    {
        return;
    }

    // Chaining cell block starts with a 5B "call rel32" at [startAddr, ui_ptr).
    ui_ptr = (unsigned int *) ((unsigned char *)(startAddr+5));

    switch (blockType)
    {
        case kChainingCellNormal:
            ALOGD ("** // Normal Chaining Cell");
            dvmCompilerPrintEmittedCodeBlock ((unsigned char *) startAddr, (unsigned char *) ui_ptr);
            ALOGD ("**  %p: %#x \t// next bytecode PC", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// code address to be patched", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %d \t// isSwitch flag", (void*)ui_ptr, *ui_ptr);
            break;

        case kChainingCellInvokeSingleton:
            ALOGD ("** // InvokeSingleton Chaining Cell");
            dvmCompilerPrintEmittedCodeBlock ((unsigned char *) startAddr, (unsigned char *) ui_ptr);
            ALOGD ("**  %p: %#x \t// next bytecode PC", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// code address to be patched", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            break;

        case kChainingCellHot:
            ALOGD ("** // Hot Chaining Cell");
            dvmCompilerPrintEmittedCodeBlock ((unsigned char *) startAddr, (unsigned char *) ui_ptr);
            ALOGD ("**  %p: %#x \t// next bytecode PC", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// code address to be patched", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %d   \t// above needs an IP-relative offset", (void*)ui_ptr, *ui_ptr);
            break;

        case kChainingCellBackwardBranch:
            ALOGD ("** // BackwardBranch Chaining Cell");
            dvmCompilerPrintEmittedCodeBlock ((unsigned char *) startAddr, (unsigned char *) ui_ptr);
            ALOGD ("**  %p: %#x \t// next bytecode PC", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// code address to be patched", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// address of loop header block", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// address of VR write-back block", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// address of loop pre-header block", (void*)ui_ptr, *ui_ptr);
            break;

        case kChainingCellInvokePredicted:
            ui_ptr = (unsigned int *) startAddr;
            ALOGD ("** // InvokePredicted Chaining Cell: %p",
                  (void*) startAddr);
            ALOGD ("**  %p: %#x \t// to be patched", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// to be patched", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// class", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// method", (void*)ui_ptr, *ui_ptr);
            ui_ptr++;
            ALOGD ("**  %p: %#x \t// staged class", (void*)ui_ptr, *ui_ptr);
            break;

        default:
            ALOGD ("printChainingCellBlocks: Unknown chaining cell type %u!",
                  blockType);
            break;      // Not yet supported! Do nothing.
    }
}

char *dvmCompilerPrintTrace (CompilationUnit *cUnit)
{
    std::vector<std::pair<BBType, char*> > &code_block_table = *cUnit->code_block_table;
    char *code_ptr, *next_code_ptr;
    BBType blk_type;
    int k;
    int max = code_block_table.size () - 1;

    ALOGD ("-------- Emit trace for [%s%s@%#x] binary code starts at %p (cache start %p)",
          cUnit->method->clazz->descriptor, cUnit->method->name,
          cUnit->traceDesc->trace[0].info.frag.startOffset,
          cUnit->baseAddr, gDvmJit.codeCache);
    ALOGD ("** %s%s@%#x:", cUnit->method->clazz->descriptor,
          cUnit->method->name, cUnit->traceDesc->trace[0].info.frag.startOffset);

    code_ptr = 0;
    next_code_ptr = 0;
    for (k = 0; k < max; k++)
    {
        blk_type = code_block_table[k].first;
        code_ptr = code_block_table[k].second;
        next_code_ptr = code_block_table[k+1].second;

        switch (blk_type)
        {
        case kExceptionHandling:
            if (code_ptr < next_code_ptr)
            {
                ALOGD ("** // exception handling VR restores");
                // print like a normal code block
                dvmCompilerPrintEmittedCodeBlock ((unsigned char *) code_ptr,
                                      (unsigned char *) next_code_ptr);
            }
            break;

        case kDalvikByteCode:
        case kFromInterpreter:
            if (code_ptr < next_code_ptr)
            {
                dvmCompilerPrintEmittedCodeBlock ((unsigned char *) code_ptr,
                                      (unsigned char *) next_code_ptr);
            }
            break;

        case kChainingCellNormal:
        case kChainingCellHot:
        case kChainingCellInvokeSingleton:
        case kChainingCellInvokePredicted:
        case kChainingCellBackwardBranch:
            printChainingCellBlocks (code_ptr, blk_type);
            break;

        default:          // no print for other block types
            break;
        }
    }

    return next_code_ptr;
}

char *dvmCompilerPrintChainingCellCounts (char *chainingCellCountAddr, ChainCellCounts &chainCellCounts)
{
    char * next_code_ptr = chainingCellCountAddr;
    // print the chaining cell count section
    uint32_t padding = (4 - ( (u4)next_code_ptr % 4)) % 4;
    next_code_ptr = reinterpret_cast<char*>(reinterpret_cast<unsigned int>(next_code_ptr + padding));
    ALOGD ("** // chaining cell counts section (4B aligned)");
    for (int i = 0; i < kChainingCellGap; i++)
    {
        ALOGD ("**  %p: %u", (void*) next_code_ptr, chainCellCounts.u.count[i]);
        next_code_ptr += sizeof(chainCellCounts.u.count[i]);
    }
    return next_code_ptr;
}

void dvmCompilerPrintChainingCellOffsetHeader (u2 *pCCOffsetSection)
{
    // print the chaining cell offset header content
    ALOGD ("** // Patched (offset to chaining cell counts)@%p = %#x",
          (void*)pCCOffsetSection, *pCCOffsetSection);
    ALOGD ("** // Patched (offset to chaining cell blocks)@%p = %#x",
          (void*)&pCCOffsetSection[1], pCCOffsetSection[1]);
}
