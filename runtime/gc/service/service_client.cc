/*
 * service_client.cc
 *
 *  Created on: Sep 29, 2015
 *      Author: hussein
 */


#include "gc/space/dlmalloc_space.h"
#include "gc/service/service_client.h"
#include "gc/service/global_allocator.h"
#include "runtime.h"
#include "ipcfs/ipcfs.h"
#include "gc/collector/ipc_mark_sweep.h"

namespace art {

namespace gcservice {

GCServiceClient* GCServiceClient::service_client_ = NULL;


GCServiceClient::GCServiceClient(gc::space::SharableDlMallocSpace* sharable_space,
    int index) : index_(index), sharable_space_(sharable_space) {
  collector_ =
      new gc::collector::IPCMarkSweep(Runtime::Current()->GetHeap(),
          false, "mark-sweep-", &(sharable_space_->sharable_space_data_->heap_meta_));
//  heap_meta_ = GCService::service_->GetAllocator()->AllocateHeapMeta();
//  GCSERV_CLIENT_ILOG << " address of the heap meta is: " <<
//      reinterpret_cast<void*>(heap_meta_);
//  heap_meta_->vm_status_ = GCSERVICE_STATUS_STARTING;
}

void GCServiceClient::FillAshMemMapData(android::IPCAShmemMap* recP,
    AShmemMap* shmem_map) {
  recP->begin_ = (unsigned int)shmem_map->begin_;
  recP->fd_ = shmem_map->fd_;
  recP->flags_ = shmem_map->flags_;
  recP->prot_ = shmem_map->prot_;
  recP->size_ = shmem_map->size_;

  LOG(ERROR) << "FillAshMemMapData: " <<
      StringPrintf("fd: %d, flags:%d, prot:%d, size:%d",
      recP->fd_, recP->flags_, recP->prot_, recP->size_);
}

void GCServiceClient::FillMemMapData(android::FileMapperParameters* rec) {
  FillAshMemMapData(&rec->mem_maps_[0],
      &(sharable_space_->sharable_space_data_->test_memory_));
  FillAshMemMapData(&rec->mem_maps_[1],
      &(sharable_space_->sharable_space_data_->dlmalloc_space_data_.memory_));
  FillAshMemMapData(&rec->mem_maps_[2],
      &(sharable_space_->sharable_space_data_->card_table_data_.mem_map_));
  FillAshMemMapData(&rec->mem_maps_[3],
      &(sharable_space_->sharable_space_data_->live_bitmap_.mem_map_));
}


void GCServiceClient::InitClient(const char* se_name_c_str) {
  //Thread* self = Thread::Current();

  Runtime* runtime = Runtime::Current();
  gc::Heap* heap = runtime->GetHeap();
  gc::space::SharableDlMallocSpace* _sharable_space =
        reinterpret_cast<gc::space::SharableDlMallocSpace*>(heap->GetAllocSpace());//PostZygoteForkGCService();

  if(!_sharable_space->RegisterGlobalCollector(se_name_c_str))
    return;
  LOG(ERROR) << " {InitClient} ";
  service_client_ = new GCServiceClient(_sharable_space,
      _sharable_space->GetSpaceIndex());
}


void GCServiceClient::FinalizeInitClient() {
  if(service_client_ == NULL)
    return;
  service_client_->FinalizeHeapAfterInit();
}


void GCServiceClient::RequestConcGC(void) {
  if(service_client_ == NULL)
    return;
  gc::gcservice::GCServiceGlobalAllocator* _alloc =
      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqConcCollection();
}


void GCServiceClient::RequestHeapTrim(void) {
  if(service_client_ == NULL)
    return;
  gc::gcservice::GCServiceGlobalAllocator* _alloc =
      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqHeapTrim();
}

void GCServiceClient::FinalizeHeapAfterInit(void) {


  int* _test_fd = &(sharable_space_->sharable_space_data_->test_memory_.fd_);
  LOG(ERROR) << "GCServiceClient::FinalizeHeapAfterInit ... testing: client sends FD:" <<
      *_test_fd;

  gc::gcservice::GCServiceGlobalAllocator* _alloc =
      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqRegistration(sharable_space_->sharable_space_data_);
  //_alloc->handShake_->GetMapperRecord(sharable_space_->sharable_space_data_);



//  android::FileMapperParameters mapperParams;
//  mapperParams.process_id_ = getpid();
//  mapperParams.fd_count_ = 1;
//  mapperParams.space_index_  =
//      sharable_space_->sharable_space_data_->space_index_;
//  mapperParams.fds_[0] = _test_fd;
//  bool _svcRes =
//    android::FileMapperService::MapFds(&mapperParams);
//
//  if(_svcRes) {
//    LOG(ERROR) << " __________ GCServiceClient::FinalizeHeapAfterInit:  succeeded";
//  } else {
//    LOG(ERROR) << " __________ GCServiceClient::FinalizeHeapAfterInit:  Failed";
//  }
}



}//namespace gcservice
}//namespace art
