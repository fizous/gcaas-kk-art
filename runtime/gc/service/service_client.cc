/*
 * service_client.cc
 *
 *  Created on: Sep 29, 2015
 *      Author: hussein
 */


#include "gc/service/service_client.h"

#include "gc/service/global_allocator.h"
#include "gc/space/dlmalloc_space.h"
#include "runtime.h"
#include "ipcfs/ipcfs.h"
#include "gc/collector/ipc_mark_sweep.h"

namespace art {

namespace gcservice {

GCServiceClient* GCServiceClient::service_client_ = NULL;


GCServiceClient::GCServiceClient(gc::space::SharableDlMallocSpace* sharable_space,
    int index) : index_(index), sharable_space_(sharable_space) {
  if(true)
  {
    ipcHeap_ = new gc::collector::IPCHeap(&(sharable_space_->sharable_space_data_->heap_meta_),
        Runtime::Current()->GetHeap());

    ipcHeap_->CreateCollectors();


//    collector_ =
//      new gc::collector::IPCMarkSweep(&(sharable_space_->sharable_space_data_->heap_meta_),Runtime::Current()->GetHeap(),
//          true, "mark-sweep-");
//
//    Runtime::Current()->GetHeap()->GCPSrvcReinitMarkSweep(collector_);
  } else {
    Runtime::Current()->GetHeap()->GCPSrvcReinitMarkSweep(new gc::collector::MarkSweep(Runtime::Current()->GetHeap(),
        true));
  }
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
      &(sharable_space_->sharable_space_data_->dlmalloc_space_data_.memory_));
  FillAshMemMapData(&rec->mem_maps_[1],
      &(sharable_space_->sharable_space_data_->live_stack_data_.memory_));
  FillAshMemMapData(&rec->mem_maps_[2],
      &(sharable_space_->sharable_space_data_->live_bitmap_.mem_map_));
  FillAshMemMapData(&rec->mem_maps_[3],
      &(sharable_space_->sharable_space_data_->mark_bitmap_.mem_map_));
//  FillAshMemMapData(&rec->mem_maps_[3],
//      &(sharable_space_->sharable_space_data_->test_memory_));
//  FillAshMemMapData(&rec->mem_maps_[2],
//      &(sharable_space_->sharable_space_data_->card_table_data_.mem_map_));
//  FillAshMemMapData(&rec->mem_maps_[3],
//      &(sharable_space_->sharable_space_data_->live_bitmap_.mem_map_));
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

bool GCServiceClient::SetNextGCType(gc::collector::GcType gc_type) {
  if(service_client_ == NULL) {
    return false;
  }
//  gc::gcservice::GCServiceGlobalAllocator* _alloc =
//        gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  service_client_->ipcHeap_->meta_->next_gc_type_ = gc_type;
  return true;
}

bool GCServiceClient::GetNextGCType(gc::collector::GcType* gc_type) {
  if(service_client_ == NULL) {
    return false;
  }
//  gc::gcservice::GCServiceGlobalAllocator* _alloc =
//        gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  *gc_type = service_client_->ipcHeap_->meta_->next_gc_type_;
  return true;
}

bool GCServiceClient::GetConcStartBytes(size_t* conc_start) {
  if(service_client_ == NULL) {
    return false;
  }
  *conc_start = service_client_->ipcHeap_->meta_->concurrent_start_bytes_;
  return true;
}

bool GCServiceClient::SetConcStartBytes(size_t conc_start) {
  if(service_client_ == NULL) {
    return false;
  }
  service_client_->ipcHeap_->meta_->concurrent_start_bytes_  = conc_start;
  return true;
}

bool GCServiceClient::RequestConcGC(void) {
  if(service_client_ == NULL)
    return false;
  gc::gcservice::GCServiceGlobalAllocator* _alloc =
      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqConcCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);
  return true;
}


bool GCServiceClient::RequestAllocateGC(void) {
  if(service_client_ == NULL) {
    return false;
  }
  gc::gcservice::GCServiceGlobalAllocator* _alloc =
        gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;

  if(gc::gcservice::GCServiceGlobalAllocator::kGCServiceFWDAllocationGC ==
      gc::gcservice::GC_SERVICE_HANDLE_ALLOC_DAEMON) { // we need to fwd this to daemon
    _alloc->handShake_->ReqAllocationGC();
    return true;
  }

}


bool GCServiceClient::RequestInternalGC(gc::collector::GcType gc_type, gc::GcCause gc_cause,
    bool clear_soft_references, gc::collector::GcType* gctype) {
  if(service_client_ == NULL) {
    return false;
  }
  LOG(ERROR) << " <<<<<<<<<<< GCServiceClient::RequestInternalGC >>>>>>>>>> type: " << gc_type << ", gCause: " << gc_cause;
  *gctype = service_client_->ipcHeap_->CollectGarbageIPC(gc_type, gc_cause,
      clear_soft_references);
  LOG(ERROR) << " >>>>>>>>>> GCServiceClient::RequestInternalGC -- returned <<<<<<<<<< " << *gctype;
  return true;
}

bool GCServiceClient::RequestWaitForConcurrentGC(gc::collector::GcType* type) {
  if(service_client_ == NULL) {
    return false;
  }
  Thread* self = Thread::Current();
  LOG(ERROR) << " <<<<< GCServiceClient::RequestWaitForConcurrentGC >>>> " << self->GetTid();

  *type  = service_client_->ipcHeap_->WaitForConcurrentIPCGcToComplete(self);
  LOG(ERROR) << " >>>>> GCServiceClient::RequestWaitForConcurrentGC <<<<< " << *type;
  return true;

//  gc::gcservice::GCServiceGlobalAllocator* _alloc =
//        gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
//
//  if(gc::gcservice::GCServiceGlobalAllocator::kGCServiceFWDAllocationGC ==
//      gc::gcservice::GC_SERVICE_HANDLE_ALLOC_DAEMON) { // we need to fwd this to daemon
//    _alloc->handShake_->ReqAllocationGC();
//    return true;
//  }

}

bool GCServiceClient::RequestExplicitGC(void) {
  if(service_client_ == NULL)
    return false;
  LOG(ERROR) << "    skipping the explicit GC operation......";
  return true;
//  gc::gcservice::GCServiceGlobalAllocator* _alloc =
//      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
//  _alloc->handShake_->ReqExplicitCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);
//  return true;
}


void GCServiceClient::RequestHeapTrim(void) {
  if(service_client_ == NULL)
    return;
  LOG(ERROR) << "^^^^^^^^^ Going to request trim ^^^^^^^^^^^";
  if(true)
    return;
  gc::gcservice::GCServiceGlobalAllocator* _alloc =
      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqHeapTrim();
}

void GCServiceClient::FinalizeHeapAfterInit(void) {

if(0) {
  int* _test_fd = &(sharable_space_->sharable_space_data_->test_memory_.fd_);
  LOG(ERROR) << "GCServiceClient::FinalizeHeapAfterInit ... testing: client sends FD:" <<
      *_test_fd;
}
  gc::gcservice::GCServiceGlobalAllocator* _alloc =
      gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
  ipcHeap_->StartCollectorDaemon();
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
