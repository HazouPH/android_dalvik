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
#include "HeapInfo.h"

void dvmDumpHeapInfo(ts_buf *buff) {
    int vHeapSize  = dvmGetHeapDebugInfo(kVirtualHeapSize);
    int vHeapAlloc = dvmGetHeapDebugInfo(kVirtualHeapAllocated);
    int vHeapMax   = dvmGetHeapDebugInfo(kVirtualHeapMaximumSize);
    int nHeapSize  = dvmGetHeapDebugInfo(kNativeHeapSize);
    int nHeapAlloc = dvmGetHeapDebugInfo(kNativeHeapAllocated);

    writeDebugMessage(buff, "\tGC heap address: %p\n", gDvm.gcHeap);
    writeDebugMessage(buff, "\theap max size:   %d (%dK)\n", vHeapMax, (vHeapMax / 1024));
    writeDebugMessage(buff, "\theap size:       %d (%d%%)\n", vHeapSize, (vHeapSize / (vHeapMax / 100)));
    writeDebugMessage(buff, "\theap allocated:  %d (%d%%)\n", vHeapAlloc, (vHeapAlloc / (vHeapMax / 100)));
    if (nHeapSize != -1) { // current GC support this value
        writeDebugMessage(buff, "\tnative heap size:       %d (%dK)\n", nHeapSize, (nHeapSize / 1024));
        writeDebugMessage(buff, "\tnative heap allocated:  %d (%dK)\n", nHeapAlloc, (nHeapAlloc / 1024));
    }
}

