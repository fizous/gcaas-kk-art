/*
 * compactor.cc
 *
 *  Created on: Apr 7, 2016
 *      Author: hussein
 */

#include "thread.h"
#include "thread_list.h"
#include "thread-inl.h"
#include "gc/accounting/heap_bitmap.h"
#include "gc/accounting/space_bitmap-inl.h"
#include "gc/collector/compactor.h"
#include "gc/space/dlmalloc_space.h"
#include "gc/space/space.h"
#include "gc/space/space-inl.h"
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
  const byte* src = reinterpret_cast<const byte*>(obj);
  byte* dst = reinterpret_cast<byte*>(result);

  memcpy(dst, src, obj_size);
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



class CompactFixableVisitor {
 public:
  SpaceCompactor* compactor_;
  CompactFixableVisitor(SpaceCompactor* space_compactor) :
    compactor_(space_compactor) {

  }

  void operator()(const mirror::Object* o) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if(o != NULL) {
      size_t n = o->SizeOf();
      compactor_->allocateSpaceObject(o, n);
      compactor_->compacted_cnt_++;
    }
  }
  DISALLOW_COPY_AND_ASSIGN(CompactFixableVisitor);
};

void SpaceCompactor::FillNewObjects(void) {
//  for(const auto& ref : forwarded_objects_) {
//    const byte* src = reinterpret_cast<const byte*>(ref.first);
//    byte* dst = reinterpret_cast<byte*>(ref.second);
////    size_t n = ref.first->SizeOf();
////    memcpy(dst, src, n);
//  }
}

void SpaceCompactor::startCompaction(void) {
  LOG(ERROR) << "Inside SpaceCompactor::startCompaction()";

  Runtime* runtime = Runtime::Current();

  // We only want reachable instances, so do a GC. This also ensures that the alloc stack
  // is empty, so the live bitmap is the only place we need to look.
  Thread* self = Thread::Current();
  {
    ReaderMutexLock mu(self, *Locks::mutator_lock_);
    self->TransitionFromRunnableToSuspended(kNative);
    local_heap_->CollectGarbage(true);
    self->TransitionFromSuspendedToRunnable();
  }
  LOG(ERROR) << "Done Non-Concurrent Collection: " << self->GetTid();

  ThreadList* thread_list = runtime->GetThreadList();
  thread_list->SuspendAll();
  {
    if(false) {
    ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
    local_heap_->DisableObjectValidation();
    size_t capacity = original_space_->Capacity();
    original_space_->Trim();
    original_space_->SetEnd(reinterpret_cast<byte*>(RoundUp(reinterpret_cast<uintptr_t>(original_space_->End()), kPageSize)));
    original_space_->GetMemMap()->UnMapAtEnd(original_space_->End());
    original_space_->Trim();
    LOG(ERROR) << "original space size is: being:"
        << reinterpret_cast<void*>(original_space_->Begin()) << ", end: " << reinterpret_cast<void*>(original_space_->End()) <<
        ", capacity is:" << capacity << ", size is : " << original_space_->Size();



      compact_space_ = gc::space::DlMallocSpace::Create("compacted_space",
                                                          original_space_->Size(),
                                                          capacity, capacity,
                                                          original_space_->End(), false);
      LOG(ERROR) << "new dlmalloc space size is: being:"
          << reinterpret_cast<void*>(compact_space_->Begin()) << ", end: " << reinterpret_cast<void*>(compact_space_->End()) <<
          ", capacity is:" << compact_space_->Capacity();

      immune_begin_ = reinterpret_cast<mirror::Object*>(original_space_->Begin());
      immune_end_ = reinterpret_cast<mirror::Object*>(original_space_->End());
      accounting::SPACE_BITMAP* _live_bitmap =
          original_space_->GetLiveBitmap();
      CompactVisitor compact_visitor(this);
      _live_bitmap->VisitMarkedRange(_live_bitmap->HeapBegin(),
                                     _live_bitmap->HeapLimit(),
                                     compact_visitor);
    }
    LOG(ERROR) << "Start copying and fixing Objects";

    //here we should copy and fix all broken references;

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


