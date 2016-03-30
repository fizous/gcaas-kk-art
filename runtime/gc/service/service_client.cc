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
using ::art::gc::gcservice::GCSrvcMemInfoOOM;

namespace art {

namespace gcservice {

GCServiceClient* GCServiceClient::service_client_ = NULL;


GCServiceClient::GCServiceClient(gc::space::SharableDlMallocSpace* sharable_space,
    int index, int enable_trim) :
        index_(index),
        enable_trimming_(enable_trim),
        last_process_state_(-1),
        sharable_space_(sharable_space),
        gcservice_client_lock_ (new Mutex("GCServiceClient lock")) {

  if(true) {
      ipcHeap_ =
          new gc::collector::IPCHeap(&(sharable_space_->sharable_space_data_->heap_meta_),
                                                    Runtime::Current()->GetHeap());

      ipcHeap_->CreateCollectors();
  } else {
    Runtime::Current()->GetHeap()->GCPSrvcReinitMarkSweep(new gc::collector::MarkSweep(Runtime::Current()->GetHeap(),
        true));
  }
}

void GCServiceClient::FillAshMemMapData(android::IPCAShmemMap* recP,
    AShmemMap* shmem_map) {
  recP->begin_ = (unsigned int)shmem_map->begin_;
  recP->fd_ = shmem_map->fd_;
  recP->flags_ = shmem_map->flags_;
  recP->prot_ = shmem_map->prot_;
  recP->size_ = shmem_map->size_;
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

}


bool GCServiceClient::RequestUpdateStats(void) {
  if(service_client_ == NULL)
    return false;
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;

  _alloc->handShake_->ReqUpdateStats();



  return true;


}

bool GCServiceClient::RequestConcGC(void) {
  if(service_client_ == NULL)
    return false;
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;

  Thread* self = Thread::Current();


  MutexLock mu(self, *service_client_->gcservice_client_lock_);

  if(!service_client_->ShouldPushNewGCRequest(gc::gcservice::GC_SERVICE_TASK_CONC)) {
    return true;
  }

  service_client_->updateProcessState();
  uint64_t _curr_bytes_Allocated = static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated());
  uint64_t _curr_time_ns =  NanoTime();

  gc::gcservice::GCServiceReq* _req_entry =
      _alloc->handShake_->ReqConcCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);


  if(_req_entry != NULL) {
    service_client_->setConcRequestTime(_curr_time_ns, _curr_bytes_Allocated);
    service_client_->active_requests_.push_back(_req_entry);

    return true;
  }

  return true;



}

bool GCServiceClient::RemoveGCSrvcActiveRequest(gc::gcservice::GC_SERVICE_TASK task) {
  Thread* self = Thread::Current();
  MutexLock mu(self, *service_client_->gcservice_client_lock_);
  std::vector<gc::gcservice::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == task) {
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
  GCServiceGlobalAllocator* _alloc =
        GCServiceGlobalAllocator::allocator_instant_;

  if(_alloc->fwdGCAllocation()) { // we need to fwd this to daemon
    LOG(ERROR) << "GCServiceClient::RequestAllocateGC..Forwarding";
    _alloc->handShake_->ReqAllocationGC();
    return true;
  }
  LOG(ERROR) << "GCServiceClient::RequestAllocateGC..Not Forwarding";
  return false;
}


bool GCServiceClient::RequestInternalGC(gc::collector::GcType gc_type, gc::GcCause gc_cause,
    bool clear_soft_references, gc::collector::GcType* gctype) {
  if(service_client_ == NULL) {
    return false;
  }

  *gctype = service_client_->ipcHeap_->CollectGarbageIPC(gc_type, gc_cause,
      clear_soft_references);
  return true;
}

bool GCServiceClient::RequestWaitForConcurrentGC(gc::collector::GcType* type) {
  if(service_client_ == NULL) {
    return false;
  }
  Thread* self = Thread::Current();


  *type  = service_client_->ipcHeap_->WaitForConcurrentIPCGcToComplete(self);

  return true;


}


bool GCServiceClient::ShouldPushNewGCRequest(gc::gcservice::GC_SERVICE_TASK task)    {
  std::vector<gc::gcservice::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == gc::gcservice::GC_SERVICE_TASK_EXPLICIT ||
        (*it)->req_type_ == gc::gcservice::GC_SERVICE_TASK_CONC) {
      return false;
    }
    ++it;
  }
  return true;
}


bool GCServiceClient::ShouldPushNewRequest(gc::gcservice::GC_SERVICE_TASK task)    {
  std::vector<gc::gcservice::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == task) {
      return false;
    }
    ++it;
  }
  return true;
}

bool GCServiceClient::RequestExplicitGC(void) {
  if(service_client_ == NULL)
    return false;
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;

  Thread* self = Thread::Current();

  MutexLock mu(self, *service_client_->gcservice_client_lock_);

  if(!service_client_->ShouldPushNewGCRequest(gc::gcservice::GC_SERVICE_TASK_EXPLICIT)) {
    return true;
  }
  service_client_->updateProcessState();
  uint64_t _curr_bytes_Allocated = static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated());
  uint64_t _curr_time_ns =  NanoTime();

  gc::gcservice::GCServiceReq* _req_entry =
      _alloc->handShake_->ReqExplicitCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);

  if(_req_entry != NULL) {
    service_client_->setExplRequestTime(_curr_time_ns,
                                        _curr_bytes_Allocated);
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
  MutexLock mu(self, *service_client_->gcservice_client_lock_);
  if(!service_client_->ShouldPushNewRequest(gc::gcservice::GC_SERVICE_TASK_TRIM)) {
    return;
  }
  LOG(ERROR) << "GCServiceClient::RequestHeapTrim";
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;
  _alloc->handShake_->ReqHeapTrim();
}

void GCServiceClient::FinalizeHeapAfterInit(void) {
  GCServiceGlobalAllocator* _alloc = GCServiceGlobalAllocator::allocator_instant_;
  ipcHeap_->StartCollectorDaemon();
  _alloc->handShake_->ReqRegistration(sharable_space_->sharable_space_data_);
  ipcHeap_->BlockForServerInitialization(&sharable_space_->sharable_space_data_->register_gc_);
}

void GCServiceClient::updateProcessState(void) {
  int my_new_process_state = ipcHeap_->local_heap_->GetLastProcessStateID();
  if(last_process_state_ == -1) {//first time to do it
    last_process_state_ = my_new_process_state;
  } else { //check if there is a change since last time we checked
    if(last_process_state_ != my_new_process_state) {//there is a change in the status of the process
      LOG(ERROR) << "GCServiceClient::updateProcessState: 00: " << last_process_state_;
      gc::space::AgentMemInfo* _mem_info_rec = GetMemInfoRec();
      _mem_info_rec->oom_label_ = last_process_state_;
      _mem_info_rec->resize_factor_ = GCSrvcMemInfoOOM::GetResizeFactor(_mem_info_rec);
    } else {
      LOG(ERROR) << "GCServiceClient::updateProcessState: 01: " << last_process_state_;
      if(GetMemInfoRec()->policy_method_ == gc::space::IPC_OOM_LABEL_POLICY_NURSERY)
        return;
      LOG(ERROR) << "GCServiceClient::updateProcessState: 02: " << last_process_state_;
      RequestUpdateStats();

    }
  }
}

}//namespace gcservice
}//namespace art
