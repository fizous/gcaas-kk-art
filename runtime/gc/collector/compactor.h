/*
 * compactor.h
 *
 *  Created on: Apr 7, 2016
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_COLLECTOR_COMPACTOR_H_
#define ART_RUNTIME_GC_COLLECTOR_COMPACTOR_H_

#include "gc/heap.h"
#include "gc/space/dlmalloc_space.h"
#include "mirror/object-inl.h"
#include "gc/accounting/gc_allocator.h"

namespace art {
namespace gc {

namespace collector {

class SpaceCompactor {
 public:
  Heap* local_heap_;
  uint64_t objects_cnt_;
  uint64_t compacted_cnt_;

  space::DLMALLOC_SPACE_T* original_space_;
  space::DLMALLOC_SPACE_T* compact_space_;
  // Map of Object to where it will be at runtime.
  SafeMap<const mirror::Object*, mirror::Object*, std::less<const mirror::Object*>,
    gc::accounting::GCAllocator<std::pair<const mirror::Object*, mirror::Object*> >> forwarded_objects_;
  // Immune range, every object inside the immune range is assumed to be marked.
  mirror::Object* immune_begin_;
  mirror::Object* immune_end_;



  SpaceCompactor(Heap*);
  void allocateSpaceObject(const mirror::Object* obj,
                           size_t obj_size);
  //initialize compaction which start by stopping the world
  void startCompaction(void);
  void FillNewObjects(void);

  void FinalizeCompaction(void);
};


}//collector
}//gc
}//art

#endif /* ART_RUNTIME_GC_COLLECTOR_COMPACTOR_H_ */
