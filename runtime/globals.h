/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_GLOBALS_H_
#define ART_RUNTIME_GLOBALS_H_

#include <stddef.h>
#include <stdint.h>

namespace art {

typedef uint8_t byte;
typedef intptr_t word;
typedef uintptr_t uword;

const size_t KB = 1024;
const size_t MB = KB * KB;
const size_t GB = KB * KB * KB;

const int kWordSize = sizeof(word);
const int kPointerSize = sizeof(void*);

const int kBitsPerByte = 8;
const int kBitsPerByteLog2 = 3;
const int kBitsPerWord = kWordSize * kBitsPerByte;
const int kWordHighBitMask = 1 << (kBitsPerWord - 1);

// Required stack alignment
const int kStackAlignment = 16;

// Required object alignment
const int kObjectAlignment = 8;

// ARM instruction alignment. ARM processors require code to be 4-byte aligned,
// but ARM ELF requires 8..
const int kArmAlignment = 8;

// MIPS instruction alignment.  MIPS processors require code to be 4-byte aligned.
// TODO: Can this be 4?
const int kMipsAlignment = 8;

// X86 instruction alignment. This is the recommended alignment for maximum performance.
const int kX86Alignment = 16;

// System page size. We check this against sysconf(_SC_PAGE_SIZE) at runtime, but use a simple
// compile-time constant so the compiler can generate better code.
const int kPageSize = 4096;

// Whether or not this is a debug build. Useful in conditionals where NDEBUG isn't.
#if defined(NDEBUG)
const bool kIsDebugBuild = false;
#else
const bool kIsDebugBuild = true;
#endif

// Whether or not this is a target (vs host) build. Useful in conditionals where ART_TARGET isn't.
#if defined(ART_TARGET)
const bool kIsTargetBuild = true;
#else
const bool kIsTargetBuild = false;
#endif


#ifndef ART_GC_SERVICE
  #define ART_GC_SERVICE true
#endif

#ifndef ART_GC_SERVICE_VERBOSE
  #define ART_GC_SERVICE_VERBOSE false
#endif

#if ART_GC_SERVICE
  #define IPC_MS_VLOG(severity)                 if (ART_GC_SERVICE_VERBOSE) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream()
//  #ifndef ART_GC_SERVICE_VERBOSE
//    #define IPC_MARKSWEEP_VLOG_ON                 (ART_GC_SERVICE_VERBOSE || 1)
//    #define IPC_MS_VLOG(severity)                 if (IPC_MARKSWEEP_VLOG_ON) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream()
//  #else
//    #define IPC_MARKSWEEP_VLOG_ON                 true
//    #define IPC_MS_VLOG(severity)                   ((void) 0)
//  #endif
#endif

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))


}  // namespace art

#endif  // ART_RUNTIME_GLOBALS_H_
