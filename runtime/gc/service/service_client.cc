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


//using ::art::gc::service::GCServiceGlobalAllocator;
//using ::art::gc::service::GCSrvcMemInfoOOM;

namespace art {
namespace gc {
namespace service {

GCServiceClient* GCServiceClient::service_client_ = NULL;


GCServiceClient::GCServiceClient(gc::space::SharableDlMallocSpace* sharable_space,
    int index, int enable_trim) :
        index_(index),
        enable_trimming_(enable_trim),
        last_process_state_(-1),
        last_process_oom_(-1),
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

  if(!service_client_->ShouldPushNewGCRequest(GC_SERVICE_TASK_CONC)) {
    return true;
  }

  service_client_->updateProcessState();
  uint64_t _curr_bytes_Allocated = static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated());
  uint64_t _curr_time_ns =  NanoTime();

  gc::service::GCServiceReq* _req_entry =
      _alloc->handShake_->ReqConcCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);


  if(_req_entry != NULL) {
    service_client_->setMemInfoMarkStamp(_curr_time_ns, _curr_bytes_Allocated,
                                         GC_SERVICE_TASK_CONC);
    service_client_->active_requests_.push_back(_req_entry);
    return true;
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

  if(!service_client_->ShouldPushNewGCRequest(GC_SERVICE_TASK_EXPLICIT)) {
    return true;
  }
  service_client_->updateProcessState();
  uint64_t _curr_bytes_Allocated =
      static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated());
  uint64_t _curr_time_ns =  NanoTime();

  gc::service::GCServiceReq* _req_entry =
      _alloc->handShake_->ReqExplicitCollection(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);

  if(_req_entry != NULL) {
    service_client_->setMemInfoMarkStamp(_curr_time_ns, _curr_bytes_Allocated,
                                         GC_SERVICE_TASK_EXPLICIT);
    service_client_->active_requests_.push_back(_req_entry);
  }
  return true;
}

bool GCServiceClient::RequestAllocateGC(void) {
  if(service_client_ == NULL) {
    return false;
  }
  GCServiceGlobalAllocator* _alloc =
        GCServiceGlobalAllocator::allocator_instant_;
  if(_alloc->fwdGCAllocation()) {
    Thread* self = Thread::Current();
    MutexLock mu(self, *service_client_->gcservice_client_lock_);
    if(!service_client_->ShouldPushNewGCRequest(GC_SERVICE_TASK_GC_ALLOC)) {
      return true;
    }
    service_client_->updateProcessState();
    uint64_t _curr_bytes_Allocated = static_cast<uint64_t>(service_client_->ipcHeap_->local_heap_->GetBytesAllocated());
    uint64_t _curr_time_ns =  NanoTime();

  // we need to fwd this to daemon
    gc::service::GCServiceReq* _req_entry =
        _alloc->handShake_->ReqAllocationGC(&service_client_->sharable_space_->sharable_space_data_->heap_meta_);
    if(_req_entry != NULL) {
      service_client_->setMemInfoMarkStamp(_curr_time_ns, _curr_bytes_Allocated,
                                           GC_SERVICE_TASK_GC_ALLOC);
      service_client_->active_requests_.push_back(_req_entry);

      LOG(ERROR) << "Submitted the request to the AllocGC.." << self->GetTid();
      return true;
    }
  }
  LOG(ERROR) << "GCServiceClient::RequestAllocateGC..Not Forwarding";
  return false;
}

bool GCServiceClient::RemoveGCSrvcActiveRequest(GC_SERVICE_TASK task) {
  Thread* self = Thread::Current();
  bool _return_result = false;
  MutexLock mu(self, *service_client_->gcservice_client_lock_);
  std::vector<gc::service::GCServiceReq*>::iterator it;
  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if((*it)->req_type_ == task) {
      //LOG(ERROR) << "RemoveGCSrvcActiveRequest....task type= " << task << ", addr = " << (*it) << ", status =" << (*it)->status_;
      service_client_->active_requests_.erase(it);

      _return_result = true;
      break;
    }
    ++it;
  }
  if(!_return_result) {
    LOG(ERROR) << "RemoveGCSrvcActiveRequest..NOT FOUND..task type= " << task;
  }
  return true;
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


bool GCServiceClient::ShouldPushNewGCRequest(GC_SERVICE_TASK task)    {
  std::vector<GCServiceReq*>::iterator it;
  int _req_type = task;
  if((task & GC_SERVICE_TASK_GC_ANY) > 0) {
    _req_type = GC_SERVICE_TASK_GC_ANY;
  }

  for (it = service_client_->active_requests_.begin(); it != service_client_->active_requests_.end(); /* DONT increment here*/) {
    if(((*it)->req_type_ & _req_type) > 0) {
      return false;
    }
    ++it;
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
  if(!service_client_->ShouldPushNewGCRequest(GC_SERVICE_TASK_TRIM)) {
    return;
  }
//  LOG(ERROR) << self->GetTid() << ".....GCServiceClient::RequestHeapTrim..label="
//      << service_client_->GetMemInfoRec()->oom_label_
//      << ", last_process_state=" << service_client_->last_process_state_
//      << ", info_rec->label=" << service_client_->GetMemInfoRec()->oom_label_
//      << ", policy = " << service_client_->GetMemInfoRec()->policy_method_
//      << ", care about pause = " << GCSrvcMemInfoOOM::CareAboutPauseTimes(service_client_->GetMemInfoRec());
  GCServiceGlobalAllocator* _alloc =
      GCServiceGlobalAllocator::allocator_instant_;
  gc::service::GCServiceReq* _req_entry = _alloc->handShake_->ReqHeapTrim();
  if(_req_entry != NULL) {
    service_client_->active_requests_.push_back(_req_entry);
  }
}

void GCServiceClient::FinalizeHeapAfterInit(void) {
  GCServiceGlobalAllocator* _alloc = GCServiceGlobalAllocator::allocator_instant_;
  ipcHeap_->StartCollectorDaemon();
  _alloc->handShake_->ReqRegistration(sharable_space_->sharable_space_data_);
  ipcHeap_->BlockForServerInitialization(&sharable_space_->sharable_space_data_->register_gc_);
}

void GCServiceClient::updateProcessState(void) {

  int my_new_process_state = ipcHeap_->local_heap_->GetLastProcessStateID();
  bool _req_update = false;
  if(last_process_state_ != my_new_process_state) {//there is a change in the status of the process

    int _my_new_oom_adj = last_process_oom_;
    if(GetProcessOOMAdj(&_my_new_oom_adj)) {
//      LOG(ERROR)<< Thread::Current()->GetTid() << ".....GCServiceClient::updateProcessState: 00: last_proces_state="
//          << last_process_state_ << ", new_state = " << my_new_process_state
//          << ", last_oom = " << last_process_oom_ << ", my_new_oom="
//          << _my_new_oom_adj;
      gc::space::AgentMemInfo* _mem_info_rec = NULL;
      if(_my_new_oom_adj != last_process_oom_) {
        _mem_info_rec = GetMemInfoRec();
        _mem_info_rec->oom_label_ = _my_new_oom_adj;
        _mem_info_rec->resize_factor_ = GCSrvcMemInfoOOM::GetResizeFactor(_mem_info_rec);
        _req_update = (GetMemInfoRec()->policy_method_ != gc::space::IPC_OOM_LABEL_POLICY_NURSERY);
      }

      if(_req_update) {
        last_process_state_ = my_new_process_state;
        last_process_oom_ = _my_new_oom_adj;
//        LOG(ERROR)<< Thread::Current()->GetTid() << ".....GCServiceClient::updateProcessState..01 sending update stats request.."
//            << ", label:" << _mem_info_rec->oom_label_ << ", resize:"<< _mem_info_rec->resize_factor_;
        RequestUpdateStats();
      }
    }
  }
}


}//namespace service
}//namespace gc
}//namespace art
