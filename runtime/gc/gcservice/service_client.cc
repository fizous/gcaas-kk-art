/*
 * sevice_client.cc
 *
 *  Created on: Sep 2, 2015
 *      Author: hussein
 */


#include "gc/gcservice/common.h"
#include "gc/gcservice/gcservice.h"
#include "gc/gcservice/service_client.h"
#include "gc/space/dlmalloc_space.h"
#include "gc/space/dlmalloc_space-inl.h"
#include "gc/allocator/dlmalloc.h"

namespace art {

namespace gcservice {

GCServiceClient* GCServiceClient::service_client_ = NULL;


void GCServiceClient::FinalizeInitClient() {
  if(service_client_) {
    Thread* self = Thread::Current();
    GCSERV_CLIENT_ILOG << " +++Finalizing Initialization+++ " << self->GetTid();
    service_client_->FinalizeHeapAfterInit();
  }
}
void GCServiceClient::InitClient() {
  if(GCService::service_ == NULL) {
    LOG(ERROR) << "The GCService Was not initialized";
    return;
  }
  if(service_client_) {
    LOG(ERROR) << "The Client was already initialized";
    return;
  }
  GCSERV_CLIENT_ILOG << " {InitClient} ";
  Thread* self = Thread::Current();
  GCSERV_CLIENT_ILOG << " +++Registering for GCService+++ " << self->GetTid();
  {
    IterProcMutexLock interProcMu(self, *GCService::service_->_Mu());
    int _counter = GCService::service_->_IncCounter();
    GCSERV_CLIENT_ILOG << " the serviceIndex: " << _counter;
    service_client_ = new GCServiceClient(_counter);
    GCService::service_->_Cond()->Broadcast(self);
  }
  service_client_->ConstructHeap();
}

void GCServiceClient::ConstructHeap() {
  Runtime* runtime = Runtime::Current();
  gc::Heap* heap = runtime->GetHeap();

  heap->ShareHeapForGCService(&heap_meta_->alloc_space_meta_.mem_meta_,
      &heap_meta_->card_table_meta_.mem_meta_);

  heap_meta_->vm_status_ = GCSERVICE_STATUS_RUNNING;
}


void GCServiceClient::FinalizeHeapAfterInit() {
  Runtime* runtime = Runtime::Current();
  gc::Heap* heap = runtime->GetHeap();

  heap->SetZygoteProtection();
}

GCServiceClient::GCServiceClient(int index) : index_(index) {
  heap_meta_ = GCService::service_->GetAllocator()->AllocateHeapMeta();
  GCSERV_CLIENT_ILOG << " address of the heap meta is: " <<
      reinterpret_cast<void*>(heap_meta_);
  heap_meta_->vm_status_ = GCSERVICE_STATUS_STARTING;
}

}//namespace gcservice
}//namespace art
