/*
 * compactor.cc
 *
 *  Created on: Apr 7, 2016
 *      Author: hussein
 */

#include "thread.h"
#include "thread_list.h"
#include "gc/collector/compactor.h"
#include "gc/space/dlmalloc_space.h"
#include "mirror/object-inl.h"

namespace art {
namespace gc {
namespace collector {



inline void SpaceCompactor::allocateSpaceObject(const mirror::Object* obj,
                                         size_t obj_size) {
  size_t actual_space = 0;
  mirror::Object* result = compact_space_->publicAllocWithoutGrowthLocked(obj_size,
                                                            &actual_space);
  forwarded_objects_.Put(obj, result);

}

SpaceCompactor::SpaceCompactor(Heap* vmHeap) : local_heap_(vmHeap),
    objects_cnt_(0),
    compacted_cnt_(0),
    original_space_(local_heap_->GetAllocSpace()->AsDlMallocSpace()),
    compact_space_(NULL),
    immune_begin_ (NULL),
    immune_end_ (NULL){




}

class CompactVisitor {
 public:
  SpaceCompactor* compactor_;
  CompactVisitor(SpaceCompactor* space_compactor) :
    compactor_(space_compactor) {

  }

  void operator()(const mirror::Object* o) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if(o != NULL) {
      size_t n = o->SizeOf();
      compactor_->allocateSpaceObject(o, n);
      compactor_->compacted_cnt_++;
    }
  }
  DISALLOW_COPY_AND_ASSIGN(CompactVisitor);
};

void SpaceCompactor::startCompaction(void) {
  LOG(ERROR) << "Inside SpaceCompactor::startCompaction()";

  Runtime* runtime = Runtime::Current();
  Thread* self = Thread::Current();

  // We only want reachable instances, so do a GC. This also ensures that the alloc stack
  // is empty, so the live bitmap is the only place we need to look.
  Thread* self = Thread::Current();
  self->TransitionFromRunnableToSuspended(kNative);
  local_heap_->CollectGarbage(true);
  self->TransitionFromSuspendedToRunnable();
  LOG(ERROR) << "Done Non-Concurrent Collection: " << self->GetTid();

  ThreadList* thread_list = runtime->GetThreadList();
  thread_list->SuspendAll();
  {
    ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
    compact_space_ = original_space_->CreateZygoteSpace("compacted_space", false);
    immune_begin_ = original_space_->Begin();
    immune_end_ = original_space_->End();
    accounting::SPACE_BITMAP* _live_bitmap =
        original_space_->GetLiveBitmap();
    CompactVisitor compact_visitor(this);
    _live_bitmap->VisitMarkedRange(_live_bitmap->HeapBegin(),
                                   _live_bitmap->HeapLimit(), compact_visitor);
  }
  thread_list->ResumeAll();
}

void SpaceCompactor::FinalizeCompaction(void) {
  LOG(ERROR) << "compacted count = " << compacted_cnt_;
  for(const auto& ref : forwarded_objects_) {
    LOG(ERROR) << "fwd.. Obj:" << ref.first << ", fwded:" << ref.second;
  }
}

}//collector
}//gc
}//art


