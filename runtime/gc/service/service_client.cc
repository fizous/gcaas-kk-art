/*
 * service_client.cc
 *
 *  Created on: Sep 29, 2015
 *      Author: hussein
 */

#include "ipcfs/ipcfs.h"
#include "gc/space/dlmalloc_space.h"
#include "gc/service/service_client.h"
#include "runtime.h"

namespace art {

namespace gcservice {

GCServiceClient* GCServiceClient::service_client_ = NULL;


GCServiceClient::GCServiceClient(gc::space::SharableDlMallocSpace* sharable_space,
    int index) : index_(index), sharable_space_(sharable_space) {
//  heap_meta_ = GCService::service_->GetAllocator()->AllocateHeapMeta();
//  GCSERV_CLIENT_ILOG << " address of the heap meta is: " <<
//      reinterpret_cast<void*>(heap_meta_);
//  heap_meta_->vm_status_ = GCSERVICE_STATUS_STARTING;
}

void GCServiceClient::InitClient(const char* se_name_c_str) {
  //Thread* self = Thread::Current();
  Runtime* runtime = Runtime::Current();
  gc::Heap* heap = runtime->GetHeap();
  LOG(ERROR) << " {InitClient} ";
  gc::space::SharableDlMallocSpace* _sharable_space =
      reinterpret_cast<gc::space::SharableDlMallocSpace*>(heap->GetAllocSpace());//PostZygoteForkGCService();

  _sharable_space->RegisterGlobalCollector(se_name_c_str);
  service_client_ = new GCServiceClient(_sharable_space,
      _sharable_space->GetSpaceIndex());
}


void GCServiceClient::FinalizeInitClient() {
  if(service_client_ == NULL)
    return;
  service_client_->FinalizeHeapAfterInit();
}

void GCServiceClient::FinalizeHeapAfterInit(void) {
  int _test_fd = sharable_space_->sharable_space_data_->test_memory_.fd_;
  LOG(ERROR) << "GCServiceClient::FinalizeHeapAfterInit ... testing: client sends FD:" <<
      _test_fd;
  bool _svcRes =
    android::FileMapperService::RegisterFD(_test_fd);

  if(_svcRes) {
    LOG(ERROR) << " __________ GCServiceClient::FinalizeHeapAfterInit:  succeeded";
  } else {
    LOG(ERROR) << " __________ GCServiceClient::FinalizeHeapAfterInit:  Failed";
  }
}



}//namespace gcservice
}//namespace art
