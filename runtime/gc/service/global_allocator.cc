/*
 * global_allocator.cc
 *
 *  Created on: Sep 13, 2015
 *      Author: hussein
 */


#include <string>
#include <cutils/ashmem.h>

#include "globals.h"
#include "mem_map.h"
#include "ipcfs/ipcfs.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"
#include "runtime.h"
#include "mem_map.h"

#include "gc/service/global_allocator.h"
#include "gc/service/service_client.h"
#include "gc/service/service_space.h"
#include "mirror/class-inl.h"
#include "mirror/class_loader.h"

using ::art::mirror::Class;
using ::art::mirror::Object;

namespace art {
namespace gc {
namespace gcservice{

GCServiceGlobalAllocator* GCServiceGlobalAllocator::allocator_instant_ = NULL;
//int GCServiceGlobalAllocator::GCPAllowSharedMemMaps = 0;

void GCServiceGlobalAllocator::InitGCSrvcOptions(GCSrvc_Options* opts_addr) {

  opts_addr->fgd_growth_mutiplier_ = 1.5;
  opts_addr->trim_conf_ = GC_SERVICE_HANDLE_TRIM_ALLOWED;
  // By default share the three spaces
  opts_addr->share_zygote_space_ =
      GC_SERVICE_SHARE_SPACES_HEAP_BITMAPS | GC_SERVICE_SHARE_SPACES_ZYGOTE |
      GC_SERVICE_SHARE_SPACES_ALLOC;
  opts_addr->fwd_gc_alloc_ = GC_SERVICE_HANDLE_ALLOC_DAEMON;
  opts_addr->page_capacity_ = 64;
  opts_addr->handle_system_server_ = GC_SERVICE_HANDLE_SYS_SERVER_DISALLOWED;
  opts_addr->gcservc_apps_list_path_("/data/anr/srvc_benchmarks");

  const char* _conf_path = getenv("GC_SERVICE_CONF_PATH");
  if(_conf_path == NULL) {
    LOG(ERROR) << "Configuration Path is NULL";
    return;
  }
  opts_addr->gcservc_conf_path_ = _conf_path;
  std::vector<std::string> _conf_list;
  std::string _file_lines;
  if (!ReadFileToString(srvc_options_.gcservc_conf_path_, &_file_lines)) {
    LOG(ERROR) << "(couldn't read " << srvc_options_.gcservc_conf_path_ << ")\n";

    return;
  }
  Split(_file_lines, '\n', _conf_list);
  for(auto& option: _conf_list) {
    GCServiceGlobalAllocator::GCSrvcOption(option, opts_addr);
  }


}


bool GCServiceGlobalAllocator::GCSrvcOption(const std::string& option, GCSrvc_Options* opts_addr) {
  if (StartsWith(option, "-Xgcsrvc.")) {
    std::vector<std::string> gcsrvc_options;
    Split(option.substr(strlen("-Xgcsrvc.")), '.', gcsrvc_options);
    for (size_t i = 0; i < gcsrvc_options.size(); ++i) {
      if (gcsrvc_options[i] == "fgd_growth_") {
        opts_addr->fgd_growth_mutiplier_ = (atoi(gcsrvc_options[++i].c_str())) / 100.0;
        return true;
      } else if (gcsrvc_options[i] == "trim") {
        opts_addr->trim_conf_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "share_zygote") {
        opts_addr->share_zygote_space_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "fwd_gc_alloc") {
        opts_addr->fwd_gc_alloc_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "page_capacity") {
        opts_addr->page_capacity_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "handle_system_server") {
        opts_addr->handle_system_server_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      }
    }
  }
  return false;
}


bool GCServiceGlobalAllocator::GCSrvcIsSharingSpacesEnabled() {
  if(allocator_instant_ == NULL) {
    return false;
  }
  int _gc_srvc_status =
      android_atomic_release_load(&allocator_instant_->region_header_->service_header_.status_);
  return ((GCSERVICE_STATUS_SYS_SERVER_CREATED & _gc_srvc_status) > 0);
}

void GCServiceGlobalAllocator::GCSrvcNotifySystemServer() {
  if(allocator_instant_ == NULL) {
    return;
  }
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self, *allocator_instant_->region_header_->service_header_.mu_);
  GCServiceHeader* _header = &(allocator_instant_->region_header_->service_header_);
  int _status = 0;
  int _expected = GCSERVICE_STATUS_RUNNING;
  int _new_status = 0;
  do {
    _status = _header->status_;
    _new_status = (_status | GCSERVICE_STATUS_SYS_SERVER_CREATED);
  } while(android_atomic_cas(_expected, _new_status, &_header->status_) != 0);
}

GCServiceGlobalAllocator* GCServiceGlobalAllocator::CreateServiceAllocator(void) {
  if(allocator_instant_ != NULL) {
    return allocator_instant_;
  }
  allocator_instant_ = new GCServiceGlobalAllocator(/*kGCServicePageCapacity*/);


  return allocator_instant_;
}

space::GCSrvSharableDlMallocSpace* GCServiceGlobalAllocator::GCSrvcAllocateSharableSpace(int* index_p) {
  GCServiceGlobalAllocator* _inst = CreateServiceAllocator();
  return reinterpret_cast<space::GCSrvSharableDlMallocSpace*>(
      _inst->AllocateSharableSpace(index_p));
}


bool GCServiceGlobalAllocator::ShouldForkService() {
  if(allocator_instant_ == NULL) {
    CreateServiceAllocator();
    allocator_instant_->region_header_->service_header_.status_ =
                                                          GCSERVICE_STATUS_NONE;
    return true;
  } else {
    if(allocator_instant_->region_header_->service_header_.status_ == GCSERVICE_STATUS_NONE)
      return true;
  }
  return false;
}


void GCServiceGlobalAllocator::BlockOnGCProcessCreation(pid_t pid) {
  Thread* self = Thread::Current();
  GCSERVICE_ALLOC_VLOG(ERROR) << ">>>> GCServiceGlobalAllocator::BlockOnGCProcessCreation";
  IPMutexLock interProcMu(self, *allocator_instant_->region_header_->service_header_.mu_);
  allocator_instant_->UpdateForkService(pid);
  GCSERVICE_ALLOC_VLOG(ERROR) << ">>>> Going to Wait GCServiceGlobalAllocator::BlockOnGCProcessCreation";
  while(allocator_instant_->region_header_->service_header_.status_ < GCSERVICE_STATUS_RUNNING) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      allocator_instant_->region_header_->service_header_.cond_->Wait(self);
    }
  }

  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);
  GCSERVICE_ALLOC_VLOG(ERROR) << ">>>> Leaving blocked status GCServiceGlobalAllocator::BlockOnGCProcessCreation";

}


bool GCServiceGlobalAllocator::ShouldNotifyForZygoteForkRelease(void) {
  if(allocator_instant_ == NULL) {
    return false;
  }
  GCSERVICE_ALLOC_VLOG(ERROR) <<
      "XXXXXX GCServiceGlobalAllocator::ShouldNotifyForZygoteForkRelease XXXXXX";
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self,
      *allocator_instant_->region_header_->service_header_.mu_);
  allocator_instant_->ResetSemaphore();
  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);
  GCSERVICE_ALLOC_VLOG(ERROR) <<
      "XXXXXX LEaving GCServiceGlobalAllocator::ShouldNotifyForZygoteForkRelease XXXXXX";
  return true;
}


void GCServiceGlobalAllocator::RaiseSemaphore() {
  android_atomic_acquire_store(1,
      &(region_header_->service_header_.zygote_creation_busy_));
}

void GCServiceGlobalAllocator::ResetSemaphore() {
  android_atomic_acquire_store(0,
      &(region_header_->service_header_.zygote_creation_busy_));

}

void GCServiceGlobalAllocator::BlockOnGCZygoteCreation(void) {
  if(allocator_instant_ == NULL) {
    return;
  }
  GCSERVICE_ALLOC_VLOG(ERROR) << "XXXXXX GCServiceGlobalAllocator::BlockOnGCZygoteCreation XXXXXX";
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self,
      *allocator_instant_->region_header_->service_header_.mu_);

  while(allocator_instant_->region_header_->service_header_.zygote_creation_busy_ == 1) {
    allocator_instant_->region_header_->service_header_.cond_->Wait(self);
  }
  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);

  GCSERVICE_ALLOC_VLOG(ERROR) << "XXXXXX LEaving GCServiceGlobalAllocator::BlockOnGCZygoteCreation XXXXXX";
}

void GCServiceGlobalAllocator::UpdateForkService(pid_t pid) {
  region_header_->service_header_.status_ =
        GCSERVICE_STATUS_WAITINGSERVER;
  region_header_->service_header_.service_pid_ = pid;

}

void GCServiceGlobalAllocator::initServiceHeader(void) {
  GCServiceHeader* _header_addr = &region_header_->service_header_;
  _header_addr->service_pid_ = -1;
  _header_addr->status_ = GCSERVICE_STATUS_NONE;
  _header_addr->counter_ = 0;

  //_header_addr->service_status_ = GCSERVICE_STATUS_NONE;
  SharedFutexData* _futexAddress = &_header_addr->lock_.futex_head_;
  SharedConditionVarData* _condAddress = &_header_addr->lock_.cond_var_;


  _header_addr->mu_   = new InterProcessMutex("GCServiceD Mutex", _futexAddress);
  _header_addr->cond_ = new InterProcessConditionVariable("GCServiceD CondVar",
      *_header_addr->mu_, _condAddress);

  android_atomic_acquire_store(0,
      &(_header_addr->zygote_creation_busy_));

  handShake_ = new GCSrvcClientHandShake(&(region_header_->gc_handshake_));
}

GCServiceGlobalAllocator::GCServiceGlobalAllocator(/*int pages*/) :
    handShake_(NULL), region_header_(NULL) {
  int prot = PROT_READ | PROT_WRITE;
  int fileDescript = -1;
  InitGCSrvcOptions(&srvc_options_);

  size_t memory_size = srvc_options_.page_capacity_ * kPageSize;

  int flags = MAP_SHARED;

  fileDescript = ashmem_create_region("GlobalAllocator", memory_size);

  byte* begin =
      reinterpret_cast<byte*>(mmap(NULL, memory_size, prot, flags,
           fileDescript, 0));

  if (begin == NULL) {
    GCSERVICE_ALLOC_VLOG(ERROR) <<
          "Failed to allocate pages for service allocator (" <<
          "ServiceAllocator" << ") of size "
          << PrettySize(memory_size);
    return;
  }

  region_header_ = reinterpret_cast<GCSrvcGlobalRegionHeader*>(begin);
  std::string memoryName("GlobalAllocator");

  //fill the data of the global header
  MEM_MAP::AShmemFillData(&region_header_->ashmem_meta_, begin,
      memory_size, begin, memory_size, prot, flags, fileDescript, memoryName, NULL);
  region_header_->current_addr_ =
      begin + SERVICE_ALLOC_ALIGN_BYTE(GCSrvcGlobalRegionHeader);

  initServiceHeader();


  GCSERVICE_ALLOC_VLOG(ERROR) << "<<<<<<GCServiceGlobalAllocator>>>>>>";
}

GCServiceHeader* GCServiceGlobalAllocator::GetServiceHeader(void) {
  return &(GCServiceGlobalAllocator::allocator_instant_->region_header_->service_header_);
}


GCSrvcClientHandShake* GCServiceGlobalAllocator::GetServiceHandShaker(void) {
  return (GCServiceGlobalAllocator::allocator_instant_->handShake_);
}

int GCServiceGlobalAllocator::GetTrimConfig(void) {
  return GCServiceGlobalAllocator::allocator_instant_->srvc_options_.trim_conf_;
}

byte* GCServiceGlobalAllocator::AllocateSharableSpace(int* index_p) {
  size_t _allocation_size =
      SERVICE_ALLOC_ALIGN_BYTE(space::GCSrvSharableDlMallocSpace);
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self, *region_header_->service_header_.mu_);

  int _counter = region_header_->service_header_.counter_++;
  byte* _addr = allocate(_allocation_size);
  region_header_->service_header_.cond_->Broadcast(self);

  GCSERVICE_ALLOC_VLOG(ERROR) << "printing counter in GCService: " << _counter;
  GCSERVICE_ALLOC_VLOG(ERROR) << "printing counter in GCService: "
      << reinterpret_cast<void*>(region_header_->current_addr_) << ", end is: "
      << reinterpret_cast<void*>(region_header_->ashmem_meta_.begin_ + region_header_->ashmem_meta_.size_);
  *index_p = _counter;
  return _addr;
}


void GCSrvcClientHandShake::ResetRequestEntry(GCServiceReq* entry) {
  memset(entry, 0, sizeof(GCServiceReq));
}

void GCSrvcClientHandShake::ResetProcessMap(android::FileMapperParameters* record) {
  memset(record, 0, sizeof(android::FileMapperParameters));
}

void GCSrvcClientHandShake::Init() {
  gcservice_data_->available_ = KGCRequestBufferCapacity;
  gcservice_data_->queued_ = 0;
  gcservice_data_->tail_ = 0;
  gcservice_data_->head_ = KGCRequestBufferCapacity - 1;

  SharedFutexData* _futexAddress = &gcservice_data_->lock_.futex_head_;
  SharedConditionVarData* _condAddress = &gcservice_data_->lock_.cond_var_;

  gcservice_data_->mu_   = new InterProcessMutex("HandShake Mutex", _futexAddress);
  gcservice_data_->cond_ = new InterProcessConditionVariable("HandShake CondVar",
      *gcservice_data_->mu_, _condAddress);

  gcservice_data_->mapper_head_ = KProcessMapperCapacity - 1;
  gcservice_data_->mapper_tail_ = 0;
  for(int i = 0; i < KProcessMapperCapacity; i++) {
    ResetProcessMap(&(gcservice_data_->process_mappers_[i]));
  }

  for(int i = 0; i < KGCRequestBufferCapacity; i++) {
    ResetRequestEntry(&(gcservice_data_->entries_[i]));
  }

//  mem_data_->available_ = KProcessMapperCapacity;
//  mem_data_->queued_ = 0;
//  mem_data_->tail_ = 0;
//  mem_data_->head_ = KProcessMapperCapacity - 1;

//  SharedFutexData* _futexAddress = &mem_data_->lock_.futex_head_;
//  SharedConditionVarData* _condAddress = &mem_data_->lock_.cond_var_;


//  mem_data_->mu_   = new InterProcessMutex("HandShake Mutex", _futexAddress);
//  mem_data_->cond_ = new InterProcessConditionVariable("HandShake CondVar",
//      *mem_data_->mu_, _condAddress);

//  for(int i = 0; i < KProcessMapperCapacity; i++) {
//    ResetProcessMap(&(mem_data_->process_mappers_[i]));
//  }
}

//android::FileMapperParameters* GCSrvcClientHandShake::GetMapperRecord(void* params) {
//  gc::space::GCSrvSharableDlMallocSpace* _shared_space =
//      reinterpret_cast<gc::space::GCSrvSharableDlMallocSpace*>(params);
//
//  Thread* self = Thread::Current();
//  IPMutexLock interProcMu(self, *mem_data_->mu_);
//  mem_data_->head_ = (mem_data_->head_ + 1) % KProcessMapperCapacity;
//  android::FileMapperParameters* _rec =
//      &(mem_data_->process_mappers_[mem_data_->head_]);
//  _rec->process_id_  = getpid();
//  _rec->space_index_ = _shared_space->space_index_;
//  _rec->fd_count_ = IPC_FILE_MAPPER_CAPACITY;
//  _rec->shared_space_addr_ = _shared_space;
//
//  art::gcservice::GCServiceClient::service_client_->FillMemMapData(_rec);
//
//  //memcpy((void*)_rec->fds_, fdArr, IPC_FILE_MAPPER_CAPACITY * sizeof(int));
//  //memcpy((void*)_rec->byte_counts_, byte_counts, IPC_FILE_MAPPER_CAPACITY * sizeof(int));
//
//  bool _svcRes =
//    android::FileMapperService::MapFds(_rec);
//
//  if(_svcRes) {
//    GCSERVICE_ALLOC_VLOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  succeeded; " <<
//        _rec->process_id_ << ", "<< _rec->space_index_ <<", "<< _rec->fd_count_
//        <<", "<< _rec->mem_maps_[0].fd_;
//
//
//  } else {
//    GCSERVICE_ALLOC_VLOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  Failed";
//  }
//
//  mem_data_->available_ -= 1;
//  mem_data_->queued_++;
//
//  mem_data_->cond_->Broadcast(self);
//
//  return _rec;
//}




GCSrvcClientHandShake::GCSrvcClientHandShake(GCServiceRequestsBuffer* alloc_mem) :
    gcservice_data_(alloc_mem) {
  Init();
}

//void GCSrvcClientHandShake::ProcessQueuedMapper(android::MappedPairProcessFD* entry){
// // Thread* self = Thread::Current();
//  //GCSERVICE_ALLOC_VLOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper Locking mem_data_->mu_";
//  //IPMutexLock interProcMu(self, *mem_data_->mu_);
//  GCSERVICE_ALLOC_VLOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper  " << mem_data_->queued_;
//  while(mem_data_->queued_ > 0) {
//    GCSERVICE_ALLOC_VLOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper after mem_data_->queued_ " << mem_data_->queued_;
//    android::FileMapperParameters* _rec =
//       &(mem_data_->process_mappers_[mem_data_->tail_]);
//    GCSERVICE_ALLOC_VLOG(ERROR) << "Process Indexing tail.. " << mem_data_->tail_ <<", head is " << mem_data_->head_;
//    GCSERVICE_ALLOC_VLOG(ERROR) << "Process existing record:.. " << _rec->space_index_ <<
//        ", " << _rec->process_id_;
//
//
//    memcpy((void*)entry->first, _rec, sizeof(android::FileMapperParameters));
//    android::FileMapperParameters* _recSecond = entry->second;
//
//
//
//    _recSecond->process_id_ = _rec->process_id_;
//    _recSecond->space_index_ = _rec->space_index_;
//    _recSecond->fd_count_ = _rec->fd_count_;
//    for(int i = 0; i < _recSecond->fd_count_; i++) {
//      memcpy((void*)&(_recSecond->mem_maps_[i]), &(_rec->mem_maps_[i]),
//          sizeof(android::IPCAShmemMap));
//    }
//
//
//    bool _svcRes =
//      android::FileMapperService::GetMapFds(_recSecond);
//    if(_svcRes) {
//      for(int i = 0; i < _recSecond->fd_count_; i++) {
//        android::IPCAShmemMap* _result = &(_recSecond->mem_maps_[i]);
//
//        GCSERVICE_ALLOC_VLOG(ERROR) << "ProcessQueuedMapper: " << i << "-----" <<
//            StringPrintf("fd: %d, flags:%d, prot:%d, size:%s",
//                _result->fd_, _result->flags_, _result->prot_,
//                PrettySize(_result->size_).c_str());
//
//        _result->flags_ &= MAP_SHARED;
//        //_result->prot_ = PROT_READ | PROT_WRITE;
//        GCSERVICE_ALLOC_VLOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper:  succeeded.." << _result->fd_;
//
//        byte* actual = reinterpret_cast<byte*>(mmap(NULL, _result->size_,
//            _result->prot_, _result->flags_, _result->fd_, 0));
//
//        if(actual == MAP_FAILED) {
//          GCSERVICE_ALLOC_VLOG(ERROR) << "MMap failed in creating file descriptor..." << _result->fd_
//              << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
//              << ", prot: " << _result->prot_;
//        } else {
//          GCSERVICE_ALLOC_VLOG(ERROR) << "MMap succeeded in creating file descriptor..." <<
//              _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
//                  _result->fd_, reinterpret_cast<void*>(actual),
//                  *(reinterpret_cast<unsigned int*>(actual)))
//                  << ", size: " << PrettySize(_result->size_) << ", flags: " <<
//                  _result->flags_ << ", prot: " << _result->prot_;
//
//        }
//      }
//    } else {
//      GCSERVICE_ALLOC_VLOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper: Failed";
//    }
//    mem_data_->tail_ = (mem_data_->tail_ + 1) % KProcessMapperCapacity;
//    mem_data_->available_ += 1;
//    mem_data_->queued_--;
//  }
//  GCSERVICE_ALLOC_VLOG(ERROR) << " ______GCSrvcClientHandShake::ProcessQueuedMapper after while"
//      << mem_data_->queued_;
//  //mem_data_->cond_->Broadcast(self);
//}


#define GC_BUFFER_PUSH_REQUEST(ENTRY, THREAD) \
    ScopedThreadStateChange tsc(THREAD, kWaitingForGCProcess);  \
    IPMutexLock interProcMu(THREAD, *gcservice_data_->mu_);\
    while(gcservice_data_->available_ == 0) {\
      LOG(ERROR) << "Push: no space available";\
      gcservice_data_->cond_->Wait(THREAD);\
    }\
    GCSERVICE_ALLOC_VLOG(ERROR) << "passed the condition of the available space";\
    gcservice_data_->head_ = ((gcservice_data_->head_ + 1) %  KGCRequestBufferCapacity); \
    gcservice_data_->available_ = gcservice_data_->available_ - 1;\
    gcservice_data_->queued_ = gcservice_data_->queued_ + 1;\
    GCSERVICE_ALLOC_VLOG(ERROR) << "push: updating entry# " << gcservice_data_->head_;\
    ENTRY = &(gcservice_data_->entries_[gcservice_data_->head_]);\
    ENTRY->pid_ = getpid();\
    ENTRY->status_ = GC_SERVICE_REQ_NEW;


void GCSrvcClientHandShake::ReqConcCollection(void* args) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_CONC;
  _entry->data_addr_ = (uintptr_t)args;

  GCSERVICE_ALLOC_VLOG(ERROR) << "GCSrvcClientHandShake::ReqConcCollection";

  gcservice_data_->cond_->Broadcast(self);
}

void GCSrvcClientHandShake::ReqExplicitCollection(void* args) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_EXPLICIT;
  _entry->data_addr_ = (uintptr_t)args;

  GCSERVICE_ALLOC_VLOG(ERROR) << "GCSrvcClientHandShake::ReqExplicitCollection";

  gcservice_data_->cond_->Broadcast(self);
}


void GCSrvcClientHandShake::ReqRegistration(void* params) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_REG;

  GCSERVICE_ALLOC_VLOG(ERROR) << "ReqRegistration: entry address: " << reinterpret_cast<void*>(_entry);

  gc::space::GCSrvSharableDlMallocSpace* _shared_space =
      reinterpret_cast<gc::space::GCSrvSharableDlMallocSpace*>(params);


  gcservice_data_->mapper_head_ =
      ((gcservice_data_->mapper_head_ + 1) % KProcessMapperCapacity);
  android::FileMapperParameters* _rec =
      &(gcservice_data_->process_mappers_[gcservice_data_->mapper_head_]);
  _rec->process_id_  = getpid();
  _rec->space_index_ = _shared_space->space_index_;
  _rec->fd_count_ = IPC_FILE_MAPPER_CAPACITY;
  _rec->shared_space_addr_ = _shared_space;
  //_rec->java_lang_Class_cached_ = Class::GetJavaLangClass();

  _entry->data_addr_ = reinterpret_cast<uintptr_t>(_rec);

  art::gcservice::GCServiceClient::service_client_->FillMemMapData(_rec);
  bool _svcRes =
    android::FileMapperService::MapFds(_rec);
  if(_svcRes) {
    GCSERVICE_ALLOC_VLOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  succeeded; " <<
        _rec->process_id_ << ", "<< _rec->space_index_ <<", "<< _rec->fd_count_
        <<", "<< _rec->mem_maps_[0].fd_;


  } else {
    LOG(FATAL) << " __________ GCSrvcClientHandShake::GetMapperRecord:  Failed";
  }

  gcservice_data_->cond_->Broadcast(self);
}


void GCSrvcClientHandShake::ReqAllocationGC() {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_GC_ALLOC;
  GCSERVICE_ALLOC_VLOG(ERROR) << "GCSrvcClientHandShake::ReqGCAlloc";
  gcservice_data_->cond_->Broadcast(self);
}

void GCSrvcClientHandShake::ReqHeapTrim() {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_TRIM;
  //GCSERVICE_ALLOC_VLOG(ERROR)
  LOG(ERROR) << "GCSrvcClientHandShake::ReqHeapTrim";
  gcservice_data_->cond_->Broadcast(self);
}

// TODO: Remove define macro
#define CHECK_MEMORY_CALL_SRVC(call, args, what) \
  do { \
    int rc = call args; \
    if (UNLIKELY(rc != 0)) { \
      errno = rc; \
      PLOG(FATAL) << # call << " failed for " << what; \
    } \
  } while (false)

void GCSrvcClientHandShake::ProcessGCRequest(void* args) {
  GCServiceReq* _entry = NULL;
  _entry = &(gcservice_data_->entries_[gcservice_data_->tail_]);
  GCSERVICE_ALLOC_VLOG(ERROR) << "ProcessGCRequest: tail=" <<
      gcservice_data_->tail_ << ", " << "address: " <<
      reinterpret_cast<void*>(_entry);
  gcservice_data_->tail_ =
      ((gcservice_data_->tail_ + 1) % KGCRequestBufferCapacity);
  gcservice_data_->available_ = gcservice_data_->available_ + 1;
  gcservice_data_->queued_ = gcservice_data_->queued_ - 1;




  GC_SERVICE_TASK _req_type =
      static_cast<GC_SERVICE_TASK>(_entry->req_type_);


  GCSERVICE_ALLOC_VLOG(ERROR) << " ~~~~ Request type: " << _req_type <<
      " ~~~~~ " << _entry->req_type_;

  if(_req_type == GC_SERVICE_TASK_REG) {
    GCServiceDaemon* _daemon = reinterpret_cast<GCServiceDaemon*>(args);
    android::FileMapperParameters* _fMapsP =
        reinterpret_cast<android::FileMapperParameters*>(_entry->data_addr_);
    GCSERVICE_ALLOC_VLOG(ERROR) << "Process Indexing tail.. " <<
        gcservice_data_->tail_ <<
        ", head is " << gcservice_data_->head_;
    GCSERVICE_ALLOC_VLOG(ERROR) << "Process existing record:.. " <<
        _fMapsP->space_index_ <<
        ", " << _fMapsP->process_id_;
    gcservice_data_->mapper_tail_ =
        ((gcservice_data_->mapper_tail_ + 1) % KProcessMapperCapacity);



    android::FileMapperParameters* _f_map_params_a =
        reinterpret_cast<android::FileMapperParameters*>(calloc(1,
            sizeof(android::FileMapperParameters)));
    android::FileMapperParameters* _f_map_params_b =
        reinterpret_cast<android::FileMapperParameters*>(calloc(1,
            sizeof(android::FileMapperParameters)));


    android::MappedPairProcessFD* _newPairEntry =
        new std::pair<android::FileMapperParameters*,
        android::FileMapperParameters*>(_f_map_params_a, _f_map_params_b);


    memcpy((void*)_newPairEntry->first, _fMapsP,
        sizeof(android::FileMapperParameters));
    android::FileMapperParameters* _recSecond = _newPairEntry->second;


    _recSecond->process_id_ = _fMapsP->process_id_;
    _recSecond->space_index_ = _fMapsP->space_index_;
    _recSecond->fd_count_ = _fMapsP->fd_count_;
    _recSecond->shared_space_addr_ = _fMapsP->shared_space_addr_;
    for(int i = 0; i < _recSecond->fd_count_; i++) {
      memcpy((void*)&(_recSecond->mem_maps_[i]), &(_fMapsP->mem_maps_[i]),
          sizeof(android::IPCAShmemMap));
    }
    bool _svcRes =
        android::FileMapperService::GetMapFds(_recSecond);
    uintptr_t _mapping_addr = (uintptr_t) 0x00000000;
    if(_svcRes) {
      /*GCServiceProcess::process_->import_address_*/;
      for(int i = 0; i < _recSecond->fd_count_; i++) {
        _mapping_addr =
            MemBaseMap::GetHighestMemMap(_mapping_addr);
        android::IPCAShmemMap* _result = &(_recSecond->mem_maps_[i]);
        //_result->size_ = 4096;
        GCSERVICE_ALLOC_VLOG(ERROR) << "ProcessQueuedMapper: " << i <<
            "-----" <<
            StringPrintf("fd: %d, flags:%d, prot:%d, size:%s.. will try at addr: 0x%08x",
            _result->fd_, _result->flags_, _result->prot_,
            PrettySize(_result->size_).c_str(), _mapping_addr);

        //_result->flags_ &= MAP_SHARED;
        //_result->prot_ = PROT_READ | PROT_WRITE;



        byte* actual = reinterpret_cast<byte*>(mmap((void*)(_mapping_addr),
            _result->size_, _result->prot_, _result->flags_ | MAP_FIXED ,
            _result->fd_, 0));

        if(actual == MAP_FAILED) {
          LOG(FATAL) << "MMap failed in creating file descriptor..." << _result->fd_
              << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
              << ", prot: " << _result->prot_ << ", address: "
              << StringPrintf("0x%08x",_mapping_addr);
        } else {
          GCSERVICE_ALLOC_VLOG(ERROR) << "MMap succeeded in creating file descriptor..." <<
              _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
                  _result->fd_, reinterpret_cast<void*>(actual),
                  *(reinterpret_cast<unsigned int*>(actual)))
                  << ", size: " << PrettySize(_result->size_) << ", flags: " <<
                  _result->flags_ << ", prot: " << _result->prot_ <<
                  ", _result->begin_:" << reinterpret_cast<void*>(_result->begin_);

          _result->begin_ = reinterpret_cast<unsigned int>(actual);
          //_mapping_addr += RoundUp(_result->size_, kPageSize);
//          int _munmap_result = munmap(actual, _result->size_);
//                    if (_munmap_result == -1) {
//                      GCSERVICE_ALLOC_VLOG(ERROR) << "munmap failed";
//                    }

          GCSERVICE_ALLOC_VLOG(ERROR) << "............ >>> _mapping_addr = " <<
              StringPrintf("0x%08x",_mapping_addr) << ", actual = " <<
              reinterpret_cast<void*>(actual);
/*          int _munmap_result = munmap(actual, _result->size_);
          if (_munmap_result == -1) {
            GCSERVICE_ALLOC_VLOG(ERROR) << "munmap failed";
          }
          byte* actual_after_unmap = reinterpret_cast<byte*>(mmap((void*)(_mapping_addr), _result->size_,
              _result->prot_, _result->flags_ , _result->fd_, 0));
          if(actual == MAP_FAILED) {
            GCSERVICE_ALLOC_VLOG(ERROR) << "ReMMap failed in creating file descriptor..." << _result->fd_
                << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
                << ", prot: " << _result->prot_ << ", address: " << reinterpret_cast<void*>(_mapping_addr);
          } else {
            GCSERVICE_ALLOC_VLOG(ERROR) << "ReMMap succeeded in creating file descriptor..." <<
                _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
                    _result->fd_, reinterpret_cast<void*>(actual_after_unmap),
                    *(reinterpret_cast<unsigned int*>(actual_after_unmap)))
                    << ", size: " << PrettySize(_result->size_) << ", flags: " <<
                    _result->flags_ << ", prot: " << _result->prot_ <<
                    ", _result->begin_:" << reinterpret_cast<void*>(_result->begin_);
          }
*/
          //_mapping_addr += RoundUp(_result->size_, kPageSize);
          if(false) {
            if(i == 1) { //test that we can remap the pages

  //            byte* test_remap_address = reinterpret_cast<byte*>(mremap((void*)(_recSecond->mem_maps_[i+1].begin_),
  //                _result->size_,
  //                _result->size_, MREMAP_MAYMOVE | MREMAP_FIXED));
  //
  //
  //            if(test_remap_address == MAP_FAILED) {
  //              GCSERVICE_ALLOC_VLOG(ERROR) << "MMap failed in creating file descriptor..." << _result->fd_
  //                  << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
  //                  << ", prot: " << _result->prot_ << ", address: " << reinterpret_cast<void*>(_recSecond->mem_maps_[i+1].begin_);
  //            } else {
  //              GCSERVICE_ALLOC_VLOG(ERROR) << "MMap succeeded in creating file descriptor..." <<
  //                  _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
  //                      _result->fd_, reinterpret_cast<void*>(test_remap_address),
  //                      *(reinterpret_cast<unsigned int*>(test_remap_address)))
  //                      << ", size: " << PrettySize(_result->size_) << ", flags: " <<
  //                      _result->flags_ << ", prot: " << _result->prot_ <<
  //                      ", _result->begin_:" << reinterpret_cast<void*>(_recSecond->mem_maps_[i+1].begin_);
  //            }




              int _munmap_result = munmap(actual, _result->size_);
              if (_munmap_result == -1) {
                LOG(FATAL) << "munmap failed";
              } else {



                byte* test_remap_address = reinterpret_cast<byte*>(mmap((void*)(_recSecond->mem_maps_[i+1].begin_), _result->size_,
                    _result->prot_, _result->flags_ & MAP_SHARED, _result->fd_, 0));



  //              byte* test_remap_address = reinterpret_cast<byte*>(mmap((void*)(_recSecond->mem_maps_[i+1].begin_), _result->size_,
  //                  _result->prot_, _result->flags_ | MAP_FIXED, _result->fd_, 0));
                if(test_remap_address == MAP_FAILED) {
                  PLOG(FATAL);
                  LOG(ERROR) << "ReMMap failed in creating file descriptor..." << _result->fd_
                      << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
                      << ", prot: " << _result->prot_ << ", address: " << reinterpret_cast<void*>(_recSecond->mem_maps_[i+1].begin_);
                } else {
                  GCSERVICE_ALLOC_VLOG(ERROR) << "ReMMap succeeded in creating file descriptor..." <<
                      _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
                          _result->fd_, reinterpret_cast<void*>(test_remap_address),
                          *(reinterpret_cast<unsigned int*>(test_remap_address)))
                          << ", size: " << PrettySize(_result->size_) << ", flags: " <<
                          _result->flags_ << ", prot: " << _result->prot_ <<
                          ", _result->begin_:" << reinterpret_cast<void*>(_result->begin_);

  //                int _remap_fd = remap_file_pages(test_remap_address, _result->size_, 0, 0, 0);
  //                GCSERVICE_ALLOC_VLOG(ERROR) << "_remap_fd = " << _remap_fd;
                }

              }

            }
          }

        }
      }
      _daemon->client_agents_.push_back(new GCSrvceAgent(_newPairEntry));

    } else {
      LOG(FATAL) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper: Failed";
    }
  } else if (_req_type == GC_SERVICE_TASK_CONC) {
    GCSERVICE_ALLOC_VLOG(ERROR) << " processing concurrent Request ~~~~ Request type: " <<
        _req_type << " ~~~~~ " << _entry->req_type_ <<
        (GCServiceProcess::process_ == NULL ? "process is null" : "process is not null");
    GCServiceDaemon* _dmon =  GCServiceProcess::process_->daemon_;
    if(_dmon == NULL) {
      GCSERVICE_ALLOC_VLOG(ERROR) << "_dmon is null: " << _entry->pid_;
    } else {
      GCSERVICE_ALLOC_VLOG(ERROR) << "_dmon is not null: " << _entry->pid_;
      GCSrvceAgent* _agent =
          GCServiceProcess::process_->daemon_->GetAgentByPid(_entry->pid_);
      if(_agent == NULL) {
        GCSERVICE_ALLOC_VLOG(ERROR) << "_agent is null: " << _entry->pid_;
      } else {
        _agent->collector_->SignalCollector(GC_SERVICE_TASK_CONC);
      }
    }



  } else if (_req_type == GC_SERVICE_TASK_TRIM) {
   // GCSERVICE_ALLOC_VLOG(ERROR)
//    LOG(ERROR) << " processing Trim Request ~~~~ Request type: " <<
//        _req_type << " ~~~~~ " << _entry->req_type_;
    GCSrvceAgent* _agent =
        GCServiceProcess::process_->daemon_->GetAgentByPid(_entry->pid_);

    if(GCServiceGlobalAllocator::allocator_instant_->isTrimHandlingEnabled()) {
      _agent->collector_->SignalCollector(GC_SERVICE_TASK_TRIM);
    }
  } else if (_req_type == GC_SERVICE_TASK_GC_ALLOC) {
    GCSERVICE_ALLOC_VLOG(ERROR) << " processing Allocation GC Request ~~~~ Request type: " <<
        _req_type << " ~~~~~ " << _entry->req_type_;
    //GCServiceDaemon* _dmon =  GCServiceProcess::process_->daemon_;
    //GCSrvceAgent* _agent =
    //    GCServiceProcess::process_->daemon_->GetAgentByPid(_entry->pid_);
    //_agent->collector_->SignalCollector();
  } else if (_req_type == GC_SERVICE_TASK_EXPLICIT) {
    GCSERVICE_ALLOC_VLOG(ERROR) << " processing EXplicit GC Request ~~~~ Request type: " <<
        _req_type << " ~~~~~ " << _entry->req_type_;
    //GCServiceDaemon* _dmon =  GCServiceProcess::process_->daemon_;
    GCSrvceAgent* _agent =
        GCServiceProcess::process_->daemon_->GetAgentByPid(_entry->pid_);
    _agent->collector_->SignalCollector(GC_SERVICE_TASK_EXPLICIT);
  }

}


void GCSrvcClientHandShake::ListenToRequests(void* args) {
  Thread* self = Thread::Current();

  ScopedThreadStateChange tsc(self, kWaitingForGcToComplete);
  {
    IPMutexLock interProcMu(self, *gcservice_data_->mu_);
    GCSERVICE_ALLOC_VLOG(ERROR) << "ListenToRequests: after locking the gcserviceData mutex";
    while(gcservice_data_->queued_ == 0) {
      GCSERVICE_ALLOC_VLOG(ERROR) << "Pull: waiting for broadcast ";
      gcservice_data_->cond_->Wait(self);
      GCSERVICE_ALLOC_VLOG(ERROR) << "ListenToRequests: Somehow we received signal: " <<
          gcservice_data_->queued_;
    }
    GCSERVICE_ALLOC_VLOG(ERROR) << "before calling processGCRequest";
    ProcessGCRequest(args);
    gcservice_data_->cond_->Broadcast(self);
  }
}


}//namespace gcservice
}//namespace gc
}//namespace art




