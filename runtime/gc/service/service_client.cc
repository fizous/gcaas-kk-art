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
#include "thread.h"
#include "thread_list.h"


using ::art::gc::gcservice::GCServiceGlobalAllocator;

namespace art {

namespace gcservice {

GCServiceClient* GCServiceClient::service_client_ = NULL;


GCServiceClient::GCServiceClient(gc::space::SharableDlMallocSpace* sharable_space,
    int index, int enable_trim) :
        index_(index),
        enable_trimming_(enable_trim),
        sharable_space_(sharable_space),
        gcservice_client_lock_ (new Mutex("GCServiceClient lock")) {

  if(true) {

    //Thread* self = Thread::Current();


    // Grab exclusively the mutator lock, set state to Runnable without checking for a pending
    // suspend request as we're going to suspend soon anyway. We set the state to Runnable to avoid
    // giving away the mutator lock.

      ipcHeap_ =
          new gc::collector::IPCHeap(&(sharable_space_->sharable_space_data_->heap_meta_),
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

  IPC_MS_VLOG(INFO) << "FillAshMemMapData: " <<
      StringPrintf("fd: %d, flags:%d, prot:%d, size:%d",
      recP->fd_, recP->flags_, recP->prot_, recP->size_);
}

void GCServiceClient::FillMemMapData(android::FileMapperParameters* rec) {
  int _index = 0;

  GCServiceGlobalAllocator* _alloc =
        GCServiceGlobalAllocator::allocator_instant_;

  if(_alloc->shareZygoteSpace()) {
    FillAshMemMapData(&rec->mem_maps_[_index++],
        &(sharable_space_->sharable_space_data_->heap_meta_.reshared_zygote_.zygote_space_));
  }
  FillAshMemMapData(&rec->mem_maps_[_index++],
      &(sharable_space_->sharable_space_data_->dlmalloc_space_data_.memory_));
  FillAshMemMapData(&rec->mem_maps_[_index++],
      &(sharable_space_->sharable_space_data_->heap_meta_.mark_stack_data_.memory_));
  FillAshMemMapData(&rec->mem_maps_[_index++],
      &(sharable_space_->sharable_space_data_->mark_bitmap_.mem_map_));
  FillAshMemMapData(&rec->mem_maps_[_index++],
      &(sharable_space_->sharable_space_data_->live_bitmap_.mem_map_));

  if(_alloc->shareHeapBitmapsSpace()) {
    FillAshMemMapData(&rec->mem_maps_[_index++],
        &(sharable_space_->sharable_space_data_->heap_meta_.reshared_zygote_.mark_bitmap_.mem_map_));
    FillAshMemMapData(&rec->mem_maps_[_index++],
        &(sharable_space_->sharable_space_data_->heap_meta_.reshared_zygote_.live_bitmap_.mem_map_));
  }

//  FillAshMemMapData(&rec->mem_maps_[3],
//      &(sharable_space_->sharable_space_data_->test_memory_));
//  FillAshMemMapData(&rec->mem_maps_[2],
//      &(sharable_space_->sharable_space_data_->card_table_data_.mem_map_));
//  FillAshMemMapData(&rec->mem_maps_[3],
//      &(sharable_space_->sharable_space_data_->live_bitmap_.mem_map_));
}


void GCServiceClient::InitClient(const char* se_name_c_str, int trim_config) {
  //Thread* self = Thread::Current();

  Runtime* runtime = Runtime::Current();



  Thread* self = Thread::Current();
  ThreadList* thread_list = runtime->GetThreadList();

  // Grab exclusively the mutator lock, set state to Runnable without checking for a pending
  // suspend request as we're going to suspend soon anyway. We set the state to Runnable to avoid
  // giving away the mutator lock.
  bool result = true;
  thread_list->SuspendAll();
  {
    gc::Heap* heap = runtime->GetHeap();
    ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
    gc::space::SharableDlMallocSpace* _sharable_space =
          reinterpret_cast<gc::space::SharableDlMallocSpace*>(heap->GetAllocSpace());//PostZygoteForkGCService();

    result = _sharable_space->RegisterGlobalCollector(se_name_c_str);

    if(result) {
      IPC_MS_VLOG(INFO) << " {InitClient} ";
      service_client_ = new GCServiceClient(_sharable_space,
          _sharable_space->GetSpaceIndex(), trim_config);
    }
  }
  thread_list->ResumeAll();
}


void GCServiceClient::FinalizeInitClient() {
  if(service_client_ != NULL) {
    service_client_->FinalizeHeapAfterInit();
  }
  GCServiceGlobalAllocator::ShouldNotifyForZygoteForkRelease();
  //Runtime::Current()->GetHeap()->DumpSpaces();

}

//bool GCServiceClient::SetNextGCType(gc::collector::GcType gc_type) {
//  if(service_client_ == NULL) {
//    return false;
//  }
////  gc::gcservice::GCServiceGlobalAllocator* _alloc =
////        gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
//  service_client_->ipcHeap_->meta_->next_gc_type_ = gc_type;
//  return true;
//}
//
//bool GCServiceClient::GetNextGCType(gc::collector::GcType* gc_type) {
//  if(service_client_ == NULL) {
//    return false;
//  }
////  gc::gcservice::GCServiceGlobalAllocator* _alloc =
////        gc::gcservice::GCServiceGlobalAllocator::allocator_instant_;
//  *gc_type = service_client_->ipcHeap_->meta_->next_gc_type_;
//  return true;
//}

//bool GCServiceClient::GetConcStartBytes(size_t* conc_start) {
//  if(service_client_ == NULL) {
//    return false;
//  }
//  *conc_start = service_client_->ipcHeap_->meta_->concurrent_start_bytes_;
//  return true;
//}

//bool GCServiceClient::SetConcStartBytes(size_t conc_start) {
//  if(service_client_ == NULL) {
//    return false;
//  }
//  service_client_->ipcHeap_->meta_->concurrent_start_bytes_  = conc_start;
//  return true;
//}

bool GCServiceClient::RequestConcGC(void) {
  if(service_client_ == NULL)
    return false;
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;

  Thread* self = Thread::Current();

  LOG(ERROR) << "XXXXXXXXXXXXXX GCServiceClient::RequestConcGC XXXXXXXXXX"
      << self->GetTid();

  MutexLock mu(self, *service_client_->gcservice_client_lock_);

  if(!service_client_->ShouldPushNewGCRequest(gc::gcservice::GC_SERVICE_TASK_CONC)) {
    return true;
  }

  gc::gcservice::GCServiceReq* _req_entry =
      _alloc->handShake_->ReqConcCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);


  if(_req_entry != NULL) {
    service_client_->setConcRequestTime(NanoTime(),
                     static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated()));
    service_client_->active_requests_.push_back(_req_entry);
    return true;
  }

  return true;



}

bool GCServiceClient::RemoveGCSrvcActiveRequest(gc::gcservice::GC_SERVICE_TASK task) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "GCServiceClient::RemoveGCSrvcActiveRequest.." << task;
  MutexLock mu(self, *service_client_->gcservice_client_lock_);
  std::vector<gc::gcservice::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == task) {
      LOG(ERROR) << "GCServiceClient::RemoveGCSrvcActiveRequest " << (*it)->req_type_;
      service_client_->active_requests_.erase(it);
      break;
    }
    ++it;
  }
  return true;
}


bool GCServiceClient::RequestAllocateGC(void) {
  if(service_client_ == NULL) {
    return false;
  }
  LOG(ERROR) << "XXXXXXXXXXXXXX REQUEST ALLOC GCCCCCCCCCCCCCCCCCCCCC";
  GCServiceGlobalAllocator* _alloc =
        GCServiceGlobalAllocator::allocator_instant_;

  if(_alloc->fwdGCAllocation()) { // we need to fwd this to daemon
    _alloc->handShake_->ReqAllocationGC();
    return true;
  }

  return false;
}


bool GCServiceClient::RequestInternalGC(gc::collector::GcType gc_type, gc::GcCause gc_cause,
    bool clear_soft_references, gc::collector::GcType* gctype) {
  if(service_client_ == NULL) {
    return false;
  }
  IPC_MS_VLOG(INFO) << " <<<<<<<<<<< GCServiceClient::RequestInternalGC >>>>>>>>>> type: " << gc_type << ", gCause: " << gc_cause;
  *gctype = service_client_->ipcHeap_->CollectGarbageIPC(gc_type, gc_cause,
      clear_soft_references);
  IPC_MS_VLOG(INFO) << " >>>>>>>>>> GCServiceClient::RequestInternalGC -- returned <<<<<<<<<< " << *gctype;
  return true;
}

bool GCServiceClient::RequestWaitForConcurrentGC(gc::collector::GcType* type) {
  if(service_client_ == NULL) {
    return false;
  }
  Thread* self = Thread::Current();

  IPC_MS_VLOG(INFO) << " <<<<< GCServiceClient::RequestWaitForConcurrentGC >>>> " << self->GetTid();

  *type  = service_client_->ipcHeap_->WaitForConcurrentIPCGcToComplete(self);
  IPC_MS_VLOG(INFO) << " >>>>> GCServiceClient::RequestWaitForConcurrentGC <<<<< " << *type;
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


bool GCServiceClient::ShouldPushNewGCRequest(gc::gcservice::GC_SERVICE_TASK task)    {
  std::vector<gc::gcservice::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == gc::gcservice::GC_SERVICE_TASK_EXPLICIT ||
        (*it)->req_type_ == gc::gcservice::GC_SERVICE_TASK_CONC) {
      LOG(ERROR) << "----GCServiceClient::ShouldPushNewGCRequest previous Request was already active: " << (*it)->req_type_ <<
          ", status = " << (*it)->status_;
      return false;
    }
    ++it;
  }
  return true;
}


bool GCServiceClient::ShouldPushNewTrimRequest(gc::gcservice::GC_SERVICE_TASK task)    {
  std::vector<gc::gcservice::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == task) {
      LOG(ERROR) << "----GCServiceClient::ShouldPushNewTrimRequest previous Request was already active: " << (*it)->req_type_ <<
          ", status = " << (*it)->status_;
      return false;
    }
    ++it;
  }
  return true;
}

bool GCServiceClient::RequestExplicitGC(void) {
  if(service_client_ == NULL)
    return false;
//  LOG(ERROR) << "    skipping the explicit GC operation......";
//  return true;
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;

  Thread* self = Thread::Current();

  LOG(ERROR) << "XXXXXXXXXXXXXX GCServiceClient::RequestExplicitGC XXXXXXXXXX"
      << self->GetTid();

  MutexLock mu(self, *service_client_->gcservice_client_lock_);

  if(!service_client_->ShouldPushNewGCRequest(gc::gcservice::GC_SERVICE_TASK_EXPLICIT)) {
    return true;
  }

  gc::gcservice::GCServiceReq* _req_entry =
      _alloc->handShake_->ReqExplicitCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);

  if(_req_entry != NULL) {
    service_client_->setExplRequestTime(NanoTime(),
                                          static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated()));
    service_client_->active_requests_.push_back(_req_entry);
  }
  return true;
}


void GCServiceClient::RequestHeapTrim(void) {
  if(service_client_ == NULL)
    return;
  if(!service_client_->isTrimRequestsEnabled())
    return;
  Thread* self = Thread::Current();
  LOG(ERROR) << "GCServiceClient::RequestHeapTrim " << self->GetTid();
  MutexLock mu(self, *service_client_->gcservice_client_lock_);
  if(!service_client_->ShouldPushNewTrimRequest(gc::gcservice::GC_SERVICE_TASK_TRIM)) {
    return;
  }
  IPC_MS_VLOG(INFO) << "^^^^^^^^^ Going to request trim ^^^^^^^^^^^";
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqHeapTrim();
}

void GCServiceClient::FinalizeHeapAfterInit(void) {

//if(0) {
//  int* _test_fd = &(sharable_space_->sharable_space_data_->test_memory_.fd_);
//  LOG(ERROR) << "GCServiceClient::FinalizeHeapAfterInit ... testing: client sends FD:" <<
//      *_test_fd;
//}
  GCServiceGlobalAllocator* _alloc = GCServiceGlobalAllocator::allocator_instant_;
  ipcHeap_->StartCollectorDaemon();
  _alloc->handShake_->ReqRegistration(sharable_space_->sharable_space_data_);
  ipcHeap_->BlockForServerInitialization(&sharable_space_->sharable_space_data_->register_gc_);


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
