/*
 * Copyright (C) 2008 The Android Open Source Project
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
/*
 * Signal catcher thread.
 */
#ifndef DALVIK_SIGNALCATCHER_H_
#define DALVIK_SIGNALCATCHER_H_

bool dvmSignalCatcherStartup(void);
void dvmSignalCatcherShutdown(void);

/*
 * Set the current signal handler to ignore
 * fatal signals.  This can be used during
 * VM shutdown stage
 */
void dvmIgnoreSignalsOnVMExit(void);

#endif  // DALVIK_SIGNALCATCHER_H_
