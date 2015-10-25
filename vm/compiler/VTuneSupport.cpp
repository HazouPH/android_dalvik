/*
* Copyright (C) 2010-2012 Intel Corporation
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
#if defined(VTUNE_DALVIK)

#include <vector>
#include <algorithm>

#include "Dalvik.h"
#include "compiler/CompilerUtility.h"
#include "CompilerIR.h"

#include "VTuneSupport.h"
#ifdef ARCH_IA32
#include "codegen/x86/VTuneSupportX86.h"
#endif

/*
 * This part is written for Dalvik in addition to the original code of VTune's jitprofiling.c.
 *
 * MethodCode and MethodCodeMap are needed to get method_id by the address when
 * iJVM_EVENT_TYPE_METHOD_UPDATE events are fired as the chain cell patching
 * does not provide method_ids, which are needed by VTune.
 * MethodCodeMap is built with the data provided with the
 * iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED.
 */

/*
 * @brief Method Code Map element denotes an address range with method's id.
 */
class MethodCode {
    friend class MethodCodeMap;

protected:
    unsigned id; /**< @brief method id for VTune */
    const char *addr; /**< @brief start address of the region */
    unsigned size; /**< @brief size of the region */

public:
    /* @brief Constructor with all fields. */
    MethodCode(unsigned id, const char * addr, unsigned size)
    : id(id), addr(addr), size(size) {
        ;
    }

    /* @brief Ordered by the start addresses. */
    bool operator<(const MethodCode &cmp) const {
        return addr < cmp.addr;
    }

    /* @brief Exclusive end of the block */
    const char* end() const {
        return addr + size;
    }

    /* @brief Checks if the block contains the specified address*/
    bool contains(const char * addr) {
        return this->addr <= addr && addr < end();
    }
};

/* @brief Maintains a non-overlapped ordered set of code blocks with their method IDs
 *
 * Two main methods are implemented:
 * - add a range with its associated method_id
 * - find a range for a specified address inside it
 */
class MethodCodeMap {
private:
    std::vector<MethodCode> table; /**< @brief The ordered set of ranges */

public:
    /* @brief Adds a new code block with the specified method_id
     * @param method_id method id for VTune
     * @param codeAddress start address of the region
     * @param size size of the region
     */
    void setMethodIdForAddress(unsigned method_id, void * codeAddress, unsigned size);

    /* @brief Returns a method_id of the latest added range, which contains the
     *        specified address.
     * @param codeAddress address inside a range for which method_id is needed
     */
    unsigned getMethodIdForAddress(void * codeAddress);

    /* @brief Removes a method, which covers the specified address
     * @param codeAddress address inside a range for which method_id is needed
     *
     * This method is not used and added for completeness.
     */
    void unsetMethodIdForAddress(void * codeAddress);
};

void MethodCodeMap::setMethodIdForAddress(unsigned method_id, void * codeAddress, unsigned size) {
    if (size == 0) {
        return;
    }

    MethodCode methodCode(method_id, (char*)codeAddress, size);

    // it is the most frequent case expected
    // if no addresses were added greater than the new codeAddress:
    // just put the new range to the end of the table
    if (table.size() == 0 || table.back().end() <= codeAddress) {
        table.push_back(methodCode);
        return;
    }

    // find the proper place for the new range and insert
    std::vector<MethodCode>::iterator it = std::upper_bound(table.begin(), table.end(), methodCode);
    std::vector<MethodCode>::iterator newIt = table.insert(it, methodCode);

    // we need to fix consistency after the new range inserted:
    // all overlapped ranges should be:
    // - removed (if lay within the new range)
    // - split (if contain the new range)
    // - adjust their start or end (if overlap the new range)

    // fix overlapped successor if any
    std::vector<MethodCode>::iterator firstRightNonOverlapped
        = std::upper_bound(newIt+1, table.end(), MethodCode(0, methodCode.addr + size -1, 0));
    std::vector<MethodCode>::iterator lastChangeIt = firstRightNonOverlapped-1;
    if (newIt < lastChangeIt && lastChangeIt != table.end()) {
        // fix the first if it overlapps
        if (lastChangeIt->end() > newIt->end()) {
            unsigned diff = newIt->end() - lastChangeIt->addr;
            lastChangeIt->size -= diff;
            // assert: lastChangeIt->size > 0
            lastChangeIt->addr += diff;
            --lastChangeIt;
        }
        // erase all the ranges included by the new one
        newIt = table.erase(newIt+1, lastChangeIt+1) - 1;
    }

    //fix overlapped predecessor if any
    // Note: cannot have more than one overlapping predecessor
    // due to the consistency before the addition and
    // we have just erased all the ranges, which were fully with the new range
    if (newIt != table.begin()) {
        std::vector<MethodCode>::iterator prevIt = newIt - 1;
        if (prevIt->end() > newIt->addr) {
            unsigned newPrevSize = newIt->addr - prevIt->addr;

            if (prevIt->end() > newIt->end()) {
                // split the range (into its left and right parts)
                // as it contains the new range
                MethodCode rightPart(prevIt->id, newIt->end(),
                        static_cast<unsigned int> (prevIt->end() - newIt->end()));
                prevIt = table.insert(newIt+1, rightPart) - 2;
            }
            if (newPrevSize == 0) {
                table.erase(prevIt);
            } else {
                prevIt->size = newPrevSize;
            }
        }
    }
}

unsigned MethodCodeMap::getMethodIdForAddress(void * codeAddress) {
    MethodCode methodCode(0,(char*)codeAddress,0);
    std::vector<MethodCode>::iterator it = std::upper_bound(table.begin(), table.end(), methodCode);
    if (it == table.begin()) {
        return 0;
    }
    --it;
    // assert: codeAddress >= it->addr;
    return (codeAddress < it->end() ? it->id : 0);
}

void MethodCodeMap::unsetMethodIdForAddress(void * codeAddress) {
    MethodCode methodCode(0,(char*)codeAddress,0);
    std::vector<MethodCode>::iterator it = std::upper_bound(table.begin(), table.end(), methodCode);
    if (it == table.begin()) {
        return;
    }
    --it;
    if (it->addr <= codeAddress && codeAddress < it->end()) {
        table.erase(it);
    }
}

int notifyVTune(iJIT_JVM_EVENT event_type, void *eventSpecificData) {
    static MethodCodeMap methodCodeMap;

    if (event_type == iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED) {
        // remember the method range with the method_id
        piJIT_Method_Load pMethodLoadArgs = ((piJIT_Method_Load) eventSpecificData);
        assert(pMethodLoadArgs->method_id != 0);
        if (pMethodLoadArgs->method_id == 0) {
            return 0; // failed
        }
        methodCodeMap.setMethodIdForAddress(pMethodLoadArgs->method_id,
            pMethodLoadArgs->method_load_address, pMethodLoadArgs->method_size);
    } else if (event_type == iJVM_EVENT_TYPE_METHOD_UPDATE) {
        piJIT_Method_Load pMethodLoadArgs = ((piJIT_Method_Load) eventSpecificData);
        if (pMethodLoadArgs->method_id == 0) {
            // resolve method_id by the given address
            pMethodLoadArgs->method_id = methodCodeMap.getMethodIdForAddress(pMethodLoadArgs->method_load_address);
            assert(pMethodLoadArgs->method_id != 0);
            if (pMethodLoadArgs->method_id == 0) {
              return 0; // failed
            }
        }
    }

    return iJIT_NotifyEvent(event_type, eventSpecificData);
}

void sendTraceInfoToVTune(CompilationUnit *cUnit, JitTraceDescription *desc) {
    if (gDvmJit.printMe == true) {
        LOGD("JIT API: write a trace of '%s' method in jit file.", cUnit->method->name);
    }

    DexStringCache params_string;
    dexStringCacheInit(&params_string);
    const char* params = dexProtoGetParameterDescriptors(&desc->method->prototype, &params_string);
    int lenParams = params == 0 ? 0 : strlen(params);
    int lenName = strlen(desc->method->name);
    int len = lenName + lenParams + 3;

    char* name_with_params = (char*) dvmCompilerNew(len, false);
    memcpy(name_with_params, desc->method->name, lenName);
    name_with_params[lenName] = '(';
    if (lenParams != 0) {
        memcpy(name_with_params + lenName + 1, params, lenParams);
    }
    name_with_params[lenName + 1 + lenParams] = ')';
    name_with_params[lenName + 1 + lenParams + 1] = 0;
    dexStringCacheRelease(&params_string);

    // Structure to load all the trace details to be sent later to VTune
    iJIT_Method_Load jitMethod;
    memset(&jitMethod, 0, sizeof(iJIT_Method_Load));
    jitMethod.method_id = (int)(cUnit->method);
    jitMethod.method_name = name_with_params;
    jitMethod.class_file_name = (char*)(cUnit->method->clazz->descriptor);
    jitMethod.method_load_address = cUnit->baseAddr;
    jitMethod.method_size = *(u2 *)((char *)cUnit->baseAddr - 4);
    jitMethod.source_file_name = (char*)(cUnit->method->clazz->sourceFile);

#ifdef ARCH_IA32
    std::vector<LineNumberInfo> lineInfoList;
    getLineInfo(cUnit, jitMethod, lineInfoList);
#endif

    // Send the trace load event to the VTune
    int res = notifyVTune(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void*)&jitMethod);
    if (res != 0) {
#ifdef ARCH_IA32
        if (gDvmJit.vtuneVersion >= VTUNE_VERSION_EXPERIMENTAL) { // TODO: some future version
            dvmCompilerReportChainCellDataToVTune(cUnit, jitMethod.method_id);
        }
#endif
        if (gDvmJit.printMe == true) {
            LOGD("JIT API: a trace of '%s' method was written successfully: id=%u, address=%p, size=%d."
                    , cUnit->method->name, jitMethod.method_id, jitMethod.method_load_address ,jitMethod.method_size);
        }
    } else if (gDvmJit.printMe == true) {
        LOGD("JIT API: failed to write a trace of '%s' method: id=%u, address=%p, size=%d."
                , cUnit->method->name, jitMethod.method_id, jitMethod.method_load_address ,jitMethod.method_size);
    }
}

#endif
