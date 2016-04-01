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
using ::art::gc::service::GCServiceClient;
using ::art::gc::space::GCSrvSharableDlMallocSpace;

namespace art {
namespace gc {
namespace service{

GCServiceGlobalAllocator* GCServiceGlobalAllocator::allocator_instant_ = NULL;

//int GCServiceGlobalAllocator::GCPAllowSharedMemMaps = 0;

void GCServiceGlobalAllocator::InitGCSrvcOptions(GCSrvc_Options* opts_addr) {

  LOG(ERROR) << "<<<<<<GCServiceGlobalAllocator::InitGCSrvcOptions>>>>>>>>";


  opts_addr->trim_conf_ = /*GC_SERVICE_HANDLE_TRIM_DISALLOWED*/GC_SERVICE_HANDLE_TRIM_ALLOWED;
  // By default share the three spaces
  opts_addr->share_zygote_space_ =
      GC_SERVICE_SHARE_SPACES_HEAP_BITMAPS | GC_SERVICE_SHARE_SPACES_ZYGOTE |
      GC_SERVICE_SHARE_SPACES_ALLOC;
  opts_addr->fwd_gc_alloc_ = GC_SERVICE_HANDLE_ALLOC_DAEMON;
  opts_addr->page_capacity_ = 64;
  opts_addr->handle_system_server_ = GC_SERVICE_HANDLE_SYS_SERVER_DISALLOWED;
  opts_addr->daemon_affinity_ = GC_SRVC_DAEMON_AFFINITY_DISALLOWED;



  opts_addr->nursery_grow_adj_ = 1.6;
  opts_addr->fgd_growth_mutiplier_ = 1.5;
  opts_addr->nursery_slots_threshold_ = 10;
  opts_addr->add_conc_remote_latency_ = GC_SERVICE_OPTS_LATENCY_ADD;
  opts_addr->save_mem_profile_ = GC_SERVICE_OPTS_SAVE_PROF_DISABLE;
  opts_addr->work_stealing_workers_ = 3;
  opts_addr->power_strategies_ = GC_SERVICE_OPTS_POWER_POLICY_NONE;

  opts_addr->info_history_size_ = MEM_INFO_WINDOW_SIZE;


  const char* _bench_list_path = getenv("GC_SERVICE_BENCHMARK_LIST");

  if(_bench_list_path == NULL) {
    _bench_list_path = "/data/anr/benchmarks/srvc_benchmarks.list";
  }
  opts_addr->gcservc_apps_list_path_ = std::string(_bench_list_path);

  const char* _conf_path = getenv("GC_SERVICE_CONF_PATH");
  if(_conf_path != NULL) {
    LOG(ERROR) << "Configuration Path is no NULL.." <<  _conf_path;

    opts_addr->gcservc_conf_path_ = _conf_path;
    std::vector<std::string> _conf_list;
    std::string _file_lines;
    if (!ReadFileToString(opts_addr->gcservc_conf_path_, &_file_lines)) {
      LOG(ERROR) << "(couldn't read " << opts_addr->gcservc_conf_path_ << ")\n";
    } else {
      LOG(ERROR) << "configurations are:\n" << _file_lines;
      Split(_file_lines, '\n', _conf_list);
      for(auto& option: _conf_list) {
        GCServiceGlobalAllocator::GCSrvcOption(option, opts_addr);
      }
    }
  }

  std::string _apps_file_lines;
  if (!ReadFileToString(opts_addr->gcservc_apps_list_path_, &_apps_file_lines)) {
    LOG(ERROR) << "(couldn't read " << opts_addr->gcservc_apps_list_path_ << ")\n";
  }
  else {
//    LOG(ERROR) << "applications List: " << _apps_file_lines;
    Split(_apps_file_lines, '\n', app_list_);
  }



  LOG(ERROR) << ">>>>>>>>GCServiceGlobalAllocator::InitGCSrvcOptions<<<<<<<<";

}


bool GCServiceGlobalAllocator::GCSrvcOption(const std::string& option, GCSrvc_Options* opts_addr) {
  LOG(ERROR) << "full_option=" << option;
  if (StartsWith(option, "-Xgcsrvc.")) {
    std::vector<std::string> gcsrvc_options;
    Split(option.substr(strlen("-Xgcsrvc.")), '.', gcsrvc_options);
    for (size_t i = 0; i < gcsrvc_options.size(); ++i) {
      LOG(ERROR) << "option["<< i<< "]"<< gcsrvc_options[i];
      if (gcsrvc_options[i] == "fgd_growth") {
        opts_addr->fgd_growth_mutiplier_ = (atoi(gcsrvc_options[++i].c_str())) / 100.0;
        LOG(ERROR) << "option["<< i << "].value="<< opts_addr->fgd_growth_mutiplier_;
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
      } else if (gcsrvc_options[i] == "hsyserver") {
        opts_addr->handle_system_server_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "affinity") {
        opts_addr->daemon_affinity_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "nurserygrow") {
        opts_addr->nursery_grow_adj_ = (atoi(gcsrvc_options[++i].c_str())) / 100.0;
        LOG(ERROR) << "option["<< i << "].value="<< opts_addr->nursery_grow_adj_;
        return true;
      } else if (gcsrvc_options[i] == "nurserysize") {
        opts_addr->nursery_slots_threshold_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "latency") {
        opts_addr->add_conc_remote_latency_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "profile") {
        opts_addr->save_mem_profile_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "workers") {
        opts_addr->work_stealing_workers_ = atoi(gcsrvc_options[++i].c_str());
        return true;
      } else if (gcsrvc_options[i] == "power") {
        opts_addr->power_strategies_ = atoi(gcsrvc_options[++i].c_str());
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



bool GCServiceGlobalAllocator::GCSrvcIsClientDaemonPinned(int* cpu,
                                                          bool* complementary,
                                                          bool checkPropagation) {
  if(allocator_instant_ == NULL) {
    return false;
  }
  int affin =  allocator_instant_->srvc_options_.daemon_affinity_;
  if((affin & GC_SRVC_DAEMON_AFFINITY_ALLOWED ) == 0) {
    return false;
  }
  if(checkPropagation) {
    if((GC_SRVC_DAEMON_AFFINITY_PROPAGATE & affin) == 0) {
      return false;
    }
  }

  *complementary = ((affin & GC_SRVC_DAEMON_AFFINITY_COMPLEMENTARY) > 0);
  *cpu = (affin & GC_SRVC_DAEMON_AFFINITY_CORE_MASK) ;
  return true;
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

GCSrvSharableDlMallocSpace* GCServiceGlobalAllocator::GCSrvcAllocateSharableSpace(int* index_p) {
  GCServiceGlobalAllocator* _inst = CreateServiceAllocator();
  return reinterpret_cast<GCSrvSharableDlMallocSpace*>(
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


bool GCServiceGlobalAllocator::ShouldRegisterApp(const char* se_name_c_str) {
  if(allocator_instant_ != NULL) {
    for (size_t i = 0; i < allocator_instant_->app_list_.size(); i++) {
      if (strcmp(se_name_c_str, allocator_instant_->app_list_[i].c_str()) == 0) {
        return true;
        //android_atomic_acquire_store(1, &(sharable_space_data_->register_gc_));
      }
    }
  }
  return false;
}

void GCServiceGlobalAllocator::BlockOnGCProcessCreation(pid_t pid) {
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self, *allocator_instant_->region_header_->service_header_.mu_);
  allocator_instant_->UpdateForkService(pid);
  while(allocator_instant_->region_header_->service_header_.status_ < GCSERVICE_STATUS_RUNNING) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      allocator_instant_->region_header_->service_header_.cond_->Wait(self);
    }
  }

  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);

}


bool GCServiceGlobalAllocator::ShouldNotifyForZygoteForkRelease(void) {
  if(allocator_instant_ == NULL) {
    return false;
  }
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self,
      *allocator_instant_->region_header_->service_header_.mu_);
  allocator_instant_->ResetSemaphore();
  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);
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
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self,
      *allocator_instant_->region_header_->service_header_.mu_);

  while(allocator_instant_->region_header_->service_header_.zygote_creation_busy_ == 1) {
    allocator_instant_->region_header_->service_header_.cond_->Wait(self);
  }
  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);
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
      SERVICE_ALLOC_ALIGN_BYTE(GCSrvSharableDlMallocSpace);
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

}







GCSrvcClientHandShake::GCSrvcClientHandShake(GCServiceRequestsBuffer* alloc_mem) :
    gcservice_data_(alloc_mem) {
  Init();
}



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


GCServiceReq* GCSrvcClientHandShake::ReqConcCollection(void* args) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_CONC;
  _entry->data_addr_ = (uintptr_t)args;

  gcservice_data_->cond_->Broadcast(self);
  return _entry;
}


GCServiceReq* GCSrvcClientHandShake::ReqAllocationGC(void* args) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;
  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_GC_ALLOC;
  _entry->data_addr_ = (uintptr_t)args;
  gcservice_data_->cond_->Broadcast(self);
  return _entry;
}

GCServiceReq* GCSrvcClientHandShake::ReqExplicitCollection(void* args) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_EXPLICIT;
  _entry->data_addr_ = (uintptr_t)args;


  gcservice_data_->cond_->Broadcast(self);
  return _entry;
}

GCServiceReq* GCSrvcClientHandShake::ReqHeapTrim() {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_TRIM;

  gcservice_data_->cond_->Broadcast(self);

  return _entry;
}

void GCSrvcClientHandShake::ReqUpdateStats(void) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_STATS;



  gcservice_data_->cond_->Broadcast(self);
  return;
}


void GCSrvcClientHandShake::ReqRegistration(void* params) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_REG;


  GCSrvSharableDlMallocSpace* _shared_space =
      reinterpret_cast<GCSrvSharableDlMallocSpace*>(params);


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

  GCServiceClient::service_client_->FillMemMapData(_rec);
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






// TODO: Remove define macro
#define CHECK_MEMORY_CALL_SRVC(call, args, what) \
  do { \
    int rc = call args; \
    if (UNLIKELY(rc != 0)) { \
      errno = rc; \
      PLOG(FATAL) << # call << " failed for " << what; \
    } \
  } while (false)

GC_SERVICE_TASK GCSrvcClientHandShake::ProcessGCRequest(void* args) {
  GCServiceReq* _entry = NULL;
  GC_SERVICE_TASK _process_result = GC_SERVICE_TASK_NOP;
  _entry = &(gcservice_data_->entries_[gcservice_data_->tail_]);

  if(_entry->status_ == GC_SERVICE_REQ_NEW) {
    _entry->status_ = GC_SERVICE_REQ_STARTED;
  } else {
    if(_entry->status_ == GC_SERVICE_REQ_NONE) {
      LOG(ERROR) << "Request status is not correct..." << _entry->status_;
    }
  }

  gcservice_data_->tail_ =
      ((gcservice_data_->tail_ + 1) % KGCRequestBufferCapacity);
  gcservice_data_->available_ = gcservice_data_->available_ + 1;
  gcservice_data_->queued_ = gcservice_data_->queued_ - 1;




  GC_SERVICE_TASK _req_type =
      static_cast<GC_SERVICE_TASK>(_entry->req_type_);


  if(_req_type == GC_SERVICE_TASK_REG) {
    GCServiceDaemon* _daemon = reinterpret_cast<GCServiceDaemon*>(args);
    android::FileMapperParameters* _fMapsP =
        reinterpret_cast<android::FileMapperParameters*>(_entry->data_addr_);

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
     // _daemon->client_agents_.push_back(new GCSrvceAgent(_newPairEntry));
      _daemon->agents_map_.Put(_newPairEntry->first->process_id_,
                               new GCSrvceAgent(_newPairEntry));
      _entry->status_ = GC_SERVICE_REQ_NONE;
      _process_result = GC_SERVICE_TASK_REG;
    } else {
      LOG(FATAL) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper: Failed";
    }
  } else if (_req_type == GC_SERVICE_TASK_STATS) {
    GCServiceDaemon* _dmon =  GCServiceProcess::process_->daemon_;
    if(_dmon != NULL) {
      //_entry->status_ = GC_SERVICE_REQ_COMPLETE;
      _entry->status_ = GC_SERVICE_REQ_NONE;
      _process_result = _req_type;
    }
  } else {

     GCServiceDaemon* _dmon =  GCServiceProcess::process_->daemon_;
     if(_dmon != NULL) {
       _entry->status_ = GC_SERVICE_REQ_STARTED;
       GCSrvceAgent* _agent = _dmon->GetAgentByPid(_entry->pid_);
       if(_agent != NULL) {
         bool _fwd_request = true;
         if (_req_type == GC_SERVICE_TASK_TRIM) {
           if(!GCServiceGlobalAllocator::allocator_instant_->isTrimHandlingEnabled()) {
             _fwd_request = false;
           }
         } /*else if (_req_type == GC_SERVICE_TASK_GC_ALLOC) {
           LOG(ERROR) << "GC_SERVICE_TASK_GC_ALLOC is not handled";
           _fwd_request = false;
         }*/
         if(_fwd_request) {
           if(_agent->signalMyCollectorDaemon(_entry)) {
             _process_result = _req_type;
           }
         }
       }
     }
  }

  return _process_result;

}


void GCSrvcClientHandShake::ListenToRequests(void* args) {
  Thread* self = Thread::Current();
  GC_SERVICE_TASK _srvc_task;
  ScopedThreadStateChange tsc(self, kWaitingForGcToComplete);
  {
    IPMutexLock interProcMu(self, *gcservice_data_->mu_);
    while(gcservice_data_->queued_ == 0) {
      gcservice_data_->cond_->Wait(self);

    }
    _srvc_task = ProcessGCRequest(args);
    gcservice_data_->cond_->Broadcast(self);
    if(_srvc_task != GC_SERVICE_TASK_NOP) {
      GCServiceProcess::process_->daemon_->UpdateGlobalProcessStates(_srvc_task);
    }
  }

}


}//namespace service
}//namespace gc
}//namespace art




