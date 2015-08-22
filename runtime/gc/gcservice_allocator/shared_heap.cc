/*
 * shared_heap.cc
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */


#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"
#include "gc/gcservice_allocator/shared_heap.h"



namespace art {
namespace gc {

SharedHeap::SharedHeap(int _pid, SharedHeapMetada* metadata) :
    shared_metadata_(metadata) {
  Thread* self = Thread::Current();
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        " : ----- new shared heap:0 -------  pid: " << getpid() <<
        ", meta is stored at addr: " <<
        reinterpret_cast<void*>(metadata);
  SharedFutexData* _futexAddress =
      &shared_metadata_->lock_header_.futex_head_;
  SharedConditionVarData* _condAddress =
      &shared_metadata_->lock_header_.cond_var_;
  ipc_global_mu_ =
      new InterProcessMutex("sharedHeap Mutex", _futexAddress);
  ipc_global_cond_ =
      new InterProcessConditionVariable("sharedHeap CondVar",
          *ipc_global_mu_, _condAddress);
  shared_metadata_->pid_ = _pid;

  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        "----- +++ initializing cardtable +++ -------";

  card_table_ =
      accounting::SharedCardTable::CreateSharedCardTable(&shared_metadata_->card_table_meta_,
      Runtime::Current()->GetHeap()->GetCardTable());


  SharedFutexData* _futexConcReqAdd =
      &shared_metadata_->gc_conc_requests.futex_head_;
  SharedConditionVarData* _condConcReqAdd =
      &shared_metadata_->gc_conc_requests.cond_var_;

  conc_req_mu_= new InterProcessMutex("conc_req Mutex", _futexConcReqAdd);
  conc_req_cond_ =
        new InterProcessConditionVariable("conc_req CondVar",
            *conc_req_mu_, _condAddress);


  shared_metadata_->vm_status_ = GCSERVICE_STATUS_RUNNING;

  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        "-----new shared heap: done -------";
}


SharedHeap::SharedHeap(SharedHeapMetada* metadata) :
    shared_metadata_(metadata) {
  Thread* self = Thread::Current();
  GCSERV_DAEM_VLOG(INFO) << self->GetTid() <<
        " : ----- new server heap:0 -------  pid: " << shared_metadata_->pid_ <<
        ", meta is stored at addr: " <<
        reinterpret_cast<void*>(shared_metadata_);


  GCSERV_DAEM_VLOG(INFO) << self->GetTid() <<
        "-----new server heap: done -------";
}


SharedHeap* SharedHeap::CreateSharedHeap(ServiceAllocator* service_alloc) {
  Thread* self = Thread::Current();
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        "-----CreateSharedHeap:0 -------";
  SharedHeapMetada *_heapHeaderHolder =
      service_alloc->AllocateHeapMeta();
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        "-----CreateSharedHeap:1 -------";
  memset((void*)_heapHeaderHolder, 0, sizeof(SharedHeapMetada));
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        "-----CreateSharedHeap:2 -------";
  SharedHeap* _shared_heap = new SharedHeap(getpid(), _heapHeaderHolder);
  GCSERV_CLIENT_VLOG(INFO) << self->GetTid() <<
        "-----CreateSharedHeap:3 -------";
  return _shared_heap;
}


SharedHeap* SharedHeap::ConstructHeapServer(int vm_index) {
  SharedHeapMetada* _shared_heap_meta =
      ServiceAllocator::service_allocator->GetHeapMetaArr(vm_index);
  SharedHeap* shared_heap = new shared_heap(_shared_heap_meta);
}


SharedSpaceBitmapMeta* SharedHeap::getSharedSpaceBitmap(void) {
  return NULL;
}


}//gc
}//art
