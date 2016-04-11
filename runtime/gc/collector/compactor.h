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

  typedef   SafeMap<const mirror::Object*, mirror::Object*, std::less<const mirror::Object*>,
      gc::accounting::GCAllocator<std::pair<const mirror::Object*, mirror::Object*> >> FwdedOBJs;

  // Map of Object to where it will be at runtime.
  FwdedOBJs forwarded_objects_;
  // Immune range, every object inside the immune range is assumed to be marked.
  mirror::Object* immune_begin_;
  mirror::Object* immune_end_;

  byte* byte_start_;
  byte* byte_end_;



  SpaceCompactor(Heap*);
  void allocateSpaceObject(const mirror::Object* obj,
                           size_t obj_size);
  //initialize compaction which start by stopping the world
  void startCompaction(void);
  void FillNewObjects(void);

  void FinalizeCompaction(void);

  template <class referenceKlass>
  const referenceKlass* MapValueToServer(const referenceKlass* original_obj,
                                         bool* ismoved) const;


  void FixupInstanceFields(const mirror::Object* orig, mirror::Object* copy)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void FixupStaticFields(const mirror::Class* orig, mirror::Class* copy)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void FixupClass(const mirror::Class* orig, mirror::Class* copy)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void FixupFields(const mirror::Object* orig,
                   mirror::Object* copy,
                                uint32_t ref_offsets,
                                bool is_static)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void FixupObject(const mirror::Object* orig, mirror::Object* copy)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void FixupObjectArray(const mirror::ObjectArray<mirror::Object>* orig,
                        mirror::ObjectArray<mirror::Object>* copy)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void FixupMethod(const mirror::ArtMethod* orig, mirror::ArtMethod* copy)
          SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
};


}//collector
}//gc
}//art

#endif /* ART_RUNTIME_GC_COLLECTOR_COMPACTOR_H_ */
