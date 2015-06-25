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

#ifndef ART_RUNTIME_GC_SPACE_DLMALLOC_SPACE_INL_H_
#define ART_RUNTIME_GC_SPACE_DLMALLOC_SPACE_INL_H_

#include "dlmalloc_space.h"
#include "gc_profiler/MProfiler.h"
namespace art {
namespace mprofiler {
	class MProfiler;
}

namespace gc {
namespace space {

inline mirror::Object* DlMallocSpace::AllocNonvirtual(Thread* self, size_t num_bytes,
                                                      size_t* bytes_allocated) {
  mirror::Object* obj;
  {
    MutexLock mu(self, lock_);
    obj = AllocWithoutGrowthLocked(num_bytes, bytes_allocated);
  }
  if (obj != NULL) {
    // Zero freshly allocated memory, done while not holding the space's lock.
    memset(obj, 0, num_bytes);
  }
  return obj;
}

inline mirror::Object* DlMallocSpace::AllocWithoutGrowthLocked(size_t num_bytes, size_t* bytes_allocated) {
	size_t extendedSize = num_bytes;
	size_t calculatedSize  = 0;
	size_t checkingSize = 0;
	GCP_ADD_EXTRA_BYTES(num_bytes, extendedSize);
  mirror::Object* result = reinterpret_cast<mirror::Object*>(mspace_malloc(mspace_, extendedSize));
  if (result != NULL) {
    if (kDebugSpaces) {
      CHECK(Contains(result)) << "Allocation (" << reinterpret_cast<void*>(result)
            << ") not in bounds of allocation space " << *this;
    }
    size_t allocation_size = AllocationSizeNonvirtual(result);

    DCHECK(bytes_allocated != NULL);
    *bytes_allocated = allocation_size;
    num_bytes_allocated_ += allocation_size;
    total_bytes_allocated_ += allocation_size;
    //Fizo: should tune this
    size_t tempSize = AllocationNoOverhead(result);
    GCP_REMOVE_EXTRA_BYTES(tempSize, calculatedSize);
    GCP_REMOVE_EXTRA_BYTES(allocation_size - kChunkOverhead, checkingSize);

    if(calculatedSize != checkingSize)
    	LOG(ERROR) << "NumBytes= "<<num_bytes<<", Usable size:" << tempSize << ", allocSize: "<< allocation_size<<", checkingSize: "<< checkingSize<<" != calculatedSize: " << calculatedSize << "; diff="<< checkingSize - calculatedSize;

    art::mprofiler::VMProfiler::MProfNotifyAlloc(calculatedSize, allocation_size);
    ++total_objects_allocated_;
    ++num_objects_allocated_;
  }
  return result;
}

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_DLMALLOC_SPACE_INL_H_
