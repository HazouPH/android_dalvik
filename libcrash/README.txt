This directory contains the library to work during tombstone generation.
The code should works from the dalvik's signal handler to collect
some information and stores it to the special shared memory buffer.
The second part of code works as part of debuggerd server. It reads memory
buffer through the ptrace API  and stores data to tombstone file.

Two system properties were defined to control this process:
system.debug.plugins - the name of the library where code for data collection
                       and access is located
system.debug.data.size - the size of data to store to tombstone file from
                       target buffer.

Additinonal information for dalvikvm process may looks like this one:
==============
dump_specific_ps_info: library name: libcrash.so
processing specific data for tid = 2155, threadname = dalvikvm

Java frames:
  at Main.testcrash(Main.java:~33)
  at Main.main(Main.java:17)
  at dalvik.system.NativeStart.main(Native Method)

Threads:
0x8000a280 "main" "main"  2155 obj=0x409cbf80 (stack: 0xbffe0000)
0x8002e300 "system" "Compiler"  daemon 2161 obj=0x409d6a60 (stack: 0x45981000)
0x80026e50 "system" "Signal Catcher"  daemon 2160 obj=0x409d6950 (stack: 0x4587c000)
0x800267d0 "system" "GC"  daemon 2159 obj=0x409d6850 (stack: 0x45777000)
0x80026360 "system" "FinalizerWatchdogDaemon"  daemon 2158 obj=0x409d6620 (stack: 0x45677000)
0x8002cc90 "system" "FinalizerDaemon"  daemon 2157 obj=0x409d6350 (stack: 0x45572000)
0x80028070 "system" "ReferenceQueueDaemon"  daemon 2156 obj=0x409d6060 (stack: 0x4546d000)

Heap information:
    GC heap address: 0x80008100
    heap max size:   16777216 (16384K)
    heap size:       131072 (0%)
    heap allocated:  121248 (0%)

Compiler information:
    memory usage 49696 bytes
    work queue length is 0
==============
