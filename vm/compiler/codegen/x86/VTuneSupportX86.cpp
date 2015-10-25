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
#if defined(VTUNE_DALVIK)

#include <algorithm>

#include "libdex/Leb128.h"
#include "Lower.h"
#include "VTuneSupportX86.h"

/* @brief Get line info from mapFromBCtoNCG.
 * @param method pointer to a Method
 * @param lineInfoList result vector
 */
static void getLineInfoForByteCode(const Method* method, std::vector<LineNumberInfo>& lineInfoList) {
    const DexCode* dexCode = dvmGetMethodCode(method);

    for (u4 offset = 0, i = 1; offset < dexCode->insnsSize; ++i) {
        if (mapFromBCtoNCG[offset] != -1) {
            LineNumberInfo lineInfo;
            lineInfo.Offset = mapFromBCtoNCG[offset];
            lineInfo.LineNumber = i;
            lineInfoList.push_back(lineInfo);
        }
        offset += dexGetWidthFromInstruction(dexCode->insns + offset);
    }
}

/* @brief Get line info from dex debug info and mapFromBCtoNCG.
 * @param method pointer to a Method
 * @param lineInfoList result vector
 */
static void getLineInfoForJavaCode(const Method* method, std::vector<LineNumberInfo>& lineInfoList) {
    const DexCode* dexCode = dvmGetMethodCode(method);
    LineNumberInfo lineInfo;

    DexFile* pDexFile = method->clazz->pDvmDex->pDexFile;
    const u1 *dbgstream = dexGetDebugInfoStream(pDexFile, dexCode);
    if (method->clazz->sourceFile == NULL || dbgstream == NULL) {
        return;
    }

    // Reads debug information from dex file to get BC to Java line mapping.
    int adjopcode;
    u4 address = 0;
    u4 line = readUnsignedLeb128(&dbgstream);

    // skip parameters
    for(u4 paramCount = readUnsignedLeb128(&dbgstream); paramCount != 0; --paramCount) {
        readUnsignedLeb128(&dbgstream);
    }

    for(bool isEndSequence = false; isEndSequence == false; ) {
        u1 opcode = *dbgstream++;
        switch (opcode) {
        case DBG_END_SEQUENCE:
            isEndSequence = true;
            break;

        case DBG_ADVANCE_PC:
            address += readUnsignedLeb128(&dbgstream);
            break;

        case DBG_ADVANCE_LINE:
            line += readSignedLeb128(&dbgstream);
            break;

        case DBG_START_LOCAL:
        case DBG_START_LOCAL_EXTENDED:
            readUnsignedLeb128(&dbgstream);
            readUnsignedLeb128(&dbgstream);
            readUnsignedLeb128(&dbgstream);

            if (opcode == DBG_START_LOCAL_EXTENDED) {
                readUnsignedLeb128(&dbgstream);
            }
            break;

        case DBG_END_LOCAL:
        case DBG_RESTART_LOCAL:
            readUnsignedLeb128(&dbgstream);
            break;

        case DBG_SET_PROLOGUE_END:
        case DBG_SET_EPILOGUE_BEGIN:
        case DBG_SET_FILE:
            break;

        default:
            adjopcode = opcode - DBG_FIRST_SPECIAL;
            address += adjopcode / DBG_LINE_RANGE;
            line += DBG_LINE_BASE + (adjopcode % DBG_LINE_RANGE);

            if (mapFromBCtoNCG[address] != -1) {
                lineInfo.Offset = mapFromBCtoNCG[address];
                lineInfo.LineNumber = line;
                lineInfoList.push_back(lineInfo);
            }
            break;
        }
    }
}

/* @brief Order line numbers by their offset. Used by std::sort. */
struct SortLineNumberInfoByOffset {
    bool operator()(LineNumberInfo const& lhs, LineNumberInfo const& rhs) {
        return lhs.Offset < rhs.Offset;
    }
};

/* @brief Calculates method full name as dexdump does.
 * @param method pointer to a Method
 * @return null-terminated string with method's full name
 */
static char* makeDexDumpMethodName(const Method* method) {
    char const* name = method->name;
    u4 name_len = strlen(name);
    char* signature = dexProtoCopyMethodDescriptor(&method->prototype);
    u4 signature_len = strlen(signature);
    char const* classD = method->clazz->descriptor;
    u4 classD_len = strlen(classD);
    u4 fullSignature_offset = 0;

    // clculate result len =
    // classname without leading 'L'
    // + method name + ':'
    // + signature +'\0'
    u4 fullSignature_len = classD_len - 1 + name_len + 1 + signature_len + 1;
    char *fullSignature = static_cast<char*>(dvmCompilerNew(fullSignature_len, false));

    // copy classD without leading 'L'
    strncpy(fullSignature, classD + 1, fullSignature_len);
    fullSignature_offset = classD_len - 1;

    // change '/' and ';' to '.'
    for (u4 i = 0; i < fullSignature_offset; ++i) {
        if (fullSignature[i] == ';' || fullSignature[i] == '/') {
            fullSignature[i] = '.';
        }
    }

    // copy method name
    strncpy(fullSignature + fullSignature_offset, name, fullSignature_len - fullSignature_offset);
    fullSignature_offset += name_len;
    fullSignature[fullSignature_offset] = ':';
    ++fullSignature_offset;

    // copy signature
    strncpy(fullSignature + fullSignature_offset, signature, fullSignature_len - fullSignature_offset);
    fullSignature_offset += signature_len + 1; // '\0'

    assert(fullSignature_len == fullSignature_offset);
    free(signature), signature = 0;

    return fullSignature;
}

void getLineInfo(CompilationUnit *cUnit, iJIT_Method_Load &jitMethod, std::vector<LineNumberInfo> &lineInfoList) {
    const Method* method = cUnit->method;

    // get the line table
    if (gDvmJit.vtuneInfo == kVTuneInfoByteCode) {
        jitMethod.source_file_name = makeDexDumpMethodName(method);
        getLineInfoForByteCode(method, lineInfoList);
    } else if (gDvmJit.vtuneInfo == kVTuneInfoJavaCode) {
        getLineInfoForJavaCode(method, lineInfoList);
    }

    // sort the table if not empty
    if (lineInfoList.empty() == false) {
        std::sort(lineInfoList.begin(), lineInfoList.end(), SortLineNumberInfoByOffset());

        // shift offsets
        for (unsigned i = 0; i < lineInfoList.size() - 1; ++i) {
            lineInfoList[i].Offset = lineInfoList[i + 1].Offset;
        }
        lineInfoList[lineInfoList.size() - 1].Offset = jitMethod.method_size;

        jitMethod.line_number_size = lineInfoList.size();
        jitMethod.line_number_table = &lineInfoList[0];
    }
}

static const char * FORMAT_CODE = NULL;

/* @brief Dump the format of the region of the trace to the VTune ittnotify
 * @param cUnit pointer to the CompilationUnit
 * @param jitMethod iJIT_Method_Load to send to VTune
 * @param addr start address of the region
 * @param size size of the region
 * @param format printf format of the region if it is a data region,
 *               otherwise (i.e. region contains code) NULL
 */
static void dvmCompilerReportBlockToVtune(CompilationUnit *cUnit, iJIT_Method_Load &jitMethod, void *addr, unsigned int size, const char *format = FORMAT_CODE) {
    jitMethod.method_load_address = addr;
    jitMethod.method_size = size;
    jitMethod.class_id = 2; // update format, leave bytes as is
    jitMethod.user_data = const_cast<char*>(format);
    jitMethod.user_data_size = format == NULL ? 0 : strlen(format);

    int res = notifyVTune(iJVM_EVENT_TYPE_METHOD_UPDATE, (void*)&jitMethod);
    if (gDvmJit.printMe == true) {
        LOGD("JIT API: %s %s block of '%s' method: id=%u, address=%p, size=%d."
                , res == 0 ? "failed to report" : "reported"
                , format == NULL ? "code" : "data"
                , cUnit->method->name, jitMethod.method_id
                , jitMethod.method_load_address ,jitMethod.method_size);
    }
}

/* @brief Pair of size and printf format used to describe formatting of a region. */
struct Block {
    int size;
    const char *format;
};

/*
 * Chain cell disassembly dsecriptions as an array of list of fields' sizes and formats.
 * Every list ends with Block{size=0, format=0} and describes one type of chain cells.
 */

/* @brief Sizes and formats of Normal Chaining Cell fields. */
static Block CC_FORMAT_NORMAL[] = { // size = 17
        {0, "Normal Chaining Cell"}
        , {5, FORMAT_CODE}
        , {4, "rPC: %#x"}
        , {4, "codePtr: %#x"}
        , {4, "isSwitch: %d"}
        , {0, NULL}};

/* @brief Sizes and formats of Hot Chaining Cell fields. */
static Block CC_FORMAT_HOT[] = { // size = 17
        {0, "Hot Chaining Cell"}
        , {5, FORMAT_CODE}
        , {4, "rPC: %#x"}
        , {4, "codePtr: %#x"}
        , {4, "isMove: %d"}
        , {0, NULL}};

/* @brief Sizes and formats of Singleton Chaining Cell fields. */
static Block CC_FORMAT_SINGLETON[] = { // size = 17
        {0, "Singleton Chaining Cell"}
        , {5, FORMAT_CODE}
        , {4, "rPC: %#x"}
        , {4, "codePtr: %#x"}
        , {4, "unused"}
        , {0, NULL}};

/* @brief Sizes and formats of Predicted Chaining Cell fields. */
static Block CC_FORMAT_PREDICTED[] = { // size = 20
        {0, "Predicted Chaining Cell"}
        , {5, FORMAT_CODE}
        , {3, "padding"}
        , {4, "class: %#x"}
        , {4, "method: %#x"}
        , {4, "rechainCount: %#x"}
        , {0, NULL}};

/* @brief Sizes and formats of Backward Branch Chaining Cell fields. */
static Block CC_FORMAT_BACKWARD_BRANCH[] = { // size = 25
        {0, "Backward Branch Chaining Cell"}
        , {5, FORMAT_CODE}
        , {4, "rPC: %#x"}
        , {4, "codePtr: %#x"}
        , {4, "loop header: %#x"}
        , {4, "VR write-back: %#x"}
        , {4, "loop pre-header: %#x"}
        , {0, NULL}};

/* @brief Array of sizes and formats of all kinds of Chaining Cells.
 *
 * The order corresponds to the BBType enumeration.
 */
static Block *chainCellDescriptions[] = {
        CC_FORMAT_NORMAL, CC_FORMAT_HOT,
        CC_FORMAT_SINGLETON, CC_FORMAT_PREDICTED,
        CC_FORMAT_BACKWARD_BRANCH
};

void dvmCompilerReportChainCellDataToVTune(CompilationUnit *cUnit, unsigned int method_id) {
    // right before the trace start 4 bytes contain:
    // 2 bytes of chain cell ChainCellCounts' offset followed by
    // 2 bytes of the first chain cell's offset
    char * startAddress = (char*) cUnit->baseAddr;
    u2 countOffset = ((u2*)(startAddress - 4))[0];
    u2 chainCellsOffset = ((u2*)(startAddress - 4))[1];
    ChainCellCounts *chainCellCounts = ((ChainCellCounts*)(startAddress + countOffset));
    char *pChainCell = startAddress + chainCellsOffset;

    iJIT_Method_Load jitMethod;
    memset(&jitMethod, 0, sizeof(jitMethod));
    jitMethod.method_id = method_id;

    assert(kChainingCellGap == sizeof(chainCellDescriptions)/sizeof(chainCellDescriptions[0]));

    // iterate over all chain cells of the trace
    // for each kind of chain cell
    for(int cellKind = 0; cellKind < kChainingCellGap; ++cellKind) {
        Block *ccDescr = chainCellDescriptions[cellKind];

        // for each chain cell of this kind according to the
        int cellSize = 0;
        for(int cellIdx = 0
                ; cellIdx < chainCellCounts->u.count[cellKind]
                ; ++cellIdx, pChainCell += cellSize) {

            // each Predicted Chain Cell is alligned to 4-byte boundary
            if (cellKind == kChainingCellInvokePredicted) {
                int padding = (4 - ((u4) pChainCell & 3)) & 3;
                if (padding != 0) {
                    dvmCompilerReportBlockToVtune(cUnit, jitMethod, pChainCell, padding, "padding");
                }
                pChainCell += padding;
            }

            // report every field of the chain cell
            int offset = 0;
            for(Block *block = ccDescr;
                block->size != 0 || block->format != 0;
                offset += block->size, ++block) {
                dvmCompilerReportBlockToVtune(cUnit, jitMethod, pChainCell + offset, block->size, block->format);
            }
            assert(cellSize == 0 || cellSize == offset);
            cellSize = offset;
        }
    }
}

#endif
