/*
 * shared_heap.cc
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */




#include "gc/gcservice_allocator/shared_heap.h"



namespace art {
namespace gc {

SharedHeap::SharedHeap(int _pid, SharedHeapMetada* metadata) :
    shared_metadata_(metadata) {
  SharedFutexData* _futexAddress =
      &shared_metadata_->lock_header_.futex_head_;
  SharedConditionVarData* _condAddress =
      &shared_metadata_->lock_header_.cond_var_;
  shared_metadata_->ipc_global_mu_ =
      new InterProcessMutex("sharedHeap Mutex", _futexAddress);
  shared_metadata_->ipc_global_cond_ =
      new InterProcessConditionVariable("sharedHeap CondVar",
          *shared_metadata_->ipc_global_mu_, _condAddress);
  shared_metadata_->pid_ = _pid;
}


SharedHeap* SharedHeap::CreateSharedHeap(ServiceAllocator* service_alloc) {
  SharedHeapMetada *_heapHeaderHolder =
      service_alloc->AllocateHeapMeta();
  memset((void*)_heapHeaderHolder, 0, sizeof(gc::SharedHeapMetada));
  SharedHeap* _shared_heap = new SharedHeap(getpid(), _heapHeaderHolder);

  return _shared_heap;
}


accounting::SharedSpaceBitmapMeta* SharedHeap::getSharedSpaceBitmap(void) {
  return &shared_metadata_->bitmap_meta_;
}


}//gc
}//art
