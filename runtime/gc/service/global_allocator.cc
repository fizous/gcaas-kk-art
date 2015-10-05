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
#include "gc/service/service_space.h"
#include "gc/service/service_client.h"

namespace art {
namespace gc {
namespace gcservice{

GCServiceGlobalAllocator* GCServiceGlobalAllocator::allocator_instant_ = NULL;
int GCServiceGlobalAllocator::GCPAllowSharedMemMaps = 0;

GCServiceGlobalAllocator* GCServiceGlobalAllocator::CreateServiceAllocator(void) {
  if(allocator_instant_ != NULL) {
    return allocator_instant_;
  }
  allocator_instant_ = new GCServiceGlobalAllocator(kGCServicePageCapacity);


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
    allocator_instant_->region_header_->service_header_.status_ = GCSERVICE_STATUS_NONE;
    return true;
  } else {
    if(allocator_instant_->region_header_->service_header_.status_ == GCSERVICE_STATUS_NONE)
      return true;
  }
  return false;
}


void GCServiceGlobalAllocator::BlockOnGCProcessCreation(pid_t pid) {
  Thread* self = Thread::Current();
  LOG(ERROR) << ">>>> GCServiceGlobalAllocator::BlockOnGCProcessCreation";
  IPMutexLock interProcMu(self, *allocator_instant_->region_header_->service_header_.mu_);
  allocator_instant_->UpdateForkService(pid);
  LOG(ERROR) << ">>>> Going to Wait GCServiceGlobalAllocator::BlockOnGCProcessCreation";
  while(allocator_instant_->region_header_->service_header_.status_ < GCSERVICE_STATUS_RUNNING) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      allocator_instant_->region_header_->service_header_.cond_->Wait(self);
    }
  }

  allocator_instant_->region_header_->service_header_.cond_->Broadcast(self);
  LOG(ERROR) << ">>>> Leaving blocked status GCServiceGlobalAllocator::BlockOnGCProcessCreation";

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
  _header_addr->service_status_ = GCSERVICE_STATUS_NONE;
  SharedFutexData* _futexAddress = &_header_addr->lock_.futex_head_;
  SharedConditionVarData* _condAddress = &_header_addr->lock_.cond_var_;


  _header_addr->mu_   = new InterProcessMutex("GCServiceD Mutex", _futexAddress);
  _header_addr->cond_ = new InterProcessConditionVariable("GCServiceD CondVar",
      *_header_addr->mu_, _condAddress);


  handShake_ = new GCSrvcClientHandShake(&(region_header_->gc_handshake_));
}

GCServiceGlobalAllocator::GCServiceGlobalAllocator(int pages) :
    handShake_(NULL), region_header_(NULL) {
  int prot = PROT_READ | PROT_WRITE;
  int fileDescript = -1;
  size_t memory_size = pages * kPageSize;
  int flags = MAP_SHARED;
  fileDescript = ashmem_create_region("GlobalAllocator", memory_size);
  byte* begin =
      reinterpret_cast<byte*>(mmap(NULL, memory_size, prot, flags,
           fileDescript, 0));

  if (begin == NULL) {
    LOG(ERROR) << "Failed to allocate pages for service allocator (" <<
          "ServiceAllocator" << ") of size "
          << PrettySize(memory_size);
    return;
  }

  region_header_ = reinterpret_cast<GCSrvcGlobalRegionHeader*>(begin);
  std::string memoryName("GlobalAllocator");

  //fill the data of the global header
  MEM_MAP::AShmemFillData(&region_header_->ashmem_meta_, memoryName, begin,
      memory_size, begin, memory_size, prot, flags, fileDescript);
  region_header_->current_addr_ =
      begin + SERVICE_ALLOC_ALIGN_BYTE(GCSrvcGlobalRegionHeader);

  initServiceHeader();


  LOG(ERROR) << "<<<<<<GCServiceGlobalAllocator>>>>>>";
}

GCServiceHeader* GCServiceGlobalAllocator::GetServiceHeader(void) {
  return &(GCServiceGlobalAllocator::allocator_instant_->region_header_->service_header_);
}


GCSrvcClientHandShake* GCServiceGlobalAllocator::GetServiceHandShaker(void) {
  return (GCServiceGlobalAllocator::allocator_instant_->handShake_);
}

byte* GCServiceGlobalAllocator::AllocateSharableSpace(int* index_p) {
  size_t _allocation_size =
      SERVICE_ALLOC_ALIGN_BYTE(space::GCSrvSharableDlMallocSpace);
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self, *region_header_->service_header_.mu_);

  int _counter = region_header_->service_header_.counter_++;
  byte* _addr = allocate(_allocation_size);
  region_header_->service_header_.cond_->Broadcast(self);

  LOG(ERROR) << "printing counter in GCService: " << _counter;
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
  gcservice_data_->available_ = KProcessMapperCapacity;
  gcservice_data_->queued_ = 0;
  gcservice_data_->tail_ = 0;
  gcservice_data_->head_ = KProcessMapperCapacity - 1;

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
//    LOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  succeeded; " <<
//        _rec->process_id_ << ", "<< _rec->space_index_ <<", "<< _rec->fd_count_
//        <<", "<< _rec->mem_maps_[0].fd_;
//
//
//  } else {
//    LOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  Failed";
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
//  //LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper Locking mem_data_->mu_";
//  //IPMutexLock interProcMu(self, *mem_data_->mu_);
//  LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper  " << mem_data_->queued_;
//  while(mem_data_->queued_ > 0) {
//    LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper after mem_data_->queued_ " << mem_data_->queued_;
//    android::FileMapperParameters* _rec =
//       &(mem_data_->process_mappers_[mem_data_->tail_]);
//    LOG(ERROR) << "Process Indexing tail.. " << mem_data_->tail_ <<", head is " << mem_data_->head_;
//    LOG(ERROR) << "Process existing record:.. " << _rec->space_index_ <<
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
//        LOG(ERROR) << "ProcessQueuedMapper: " << i << "-----" <<
//            StringPrintf("fd: %d, flags:%d, prot:%d, size:%s",
//                _result->fd_, _result->flags_, _result->prot_,
//                PrettySize(_result->size_).c_str());
//
//        _result->flags_ &= MAP_SHARED;
//        //_result->prot_ = PROT_READ | PROT_WRITE;
//        LOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper:  succeeded.." << _result->fd_;
//
//        byte* actual = reinterpret_cast<byte*>(mmap(NULL, _result->size_,
//            _result->prot_, _result->flags_, _result->fd_, 0));
//
//        if(actual == MAP_FAILED) {
//          LOG(ERROR) << "MMap failed in creating file descriptor..." << _result->fd_
//              << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
//              << ", prot: " << _result->prot_;
//        } else {
//          LOG(ERROR) << "MMap succeeded in creating file descriptor..." <<
//              _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
//                  _result->fd_, reinterpret_cast<void*>(actual),
//                  *(reinterpret_cast<unsigned int*>(actual)))
//                  << ", size: " << PrettySize(_result->size_) << ", flags: " <<
//                  _result->flags_ << ", prot: " << _result->prot_;
//
//        }
//      }
//    } else {
//      LOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper: Failed";
//    }
//    mem_data_->tail_ = (mem_data_->tail_ + 1) % KProcessMapperCapacity;
//    mem_data_->available_ += 1;
//    mem_data_->queued_--;
//  }
//  LOG(ERROR) << " ______GCSrvcClientHandShake::ProcessQueuedMapper after while"
//      << mem_data_->queued_;
//  //mem_data_->cond_->Broadcast(self);
//}


#define GC_BUFFER_PUSH_REQUEST(entry, self) \
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);  \
    IPMutexLock interProcMu(self, *gcservice_data_->mu_);\
    while(gcservice_data_->available_ == 0) {\
      LOG(ERROR) << "Push: no space available";\
      gcservice_data_->cond_->Wait(self);\
    }\
    LOG(ERROR) << "passed the condition of the available space";\
    gcservice_data_->head_ = ((gcservice_data_->head_ + 1) %  GC_SERVICE_BUFFER_REQ_CAP); \
    gcservice_data_->available_ -= 1;\
    gcservice_data_->queued_ += 1;\
    entry = &(gcservice_data_->entries_[gcservice_data_->head_]);\
    entry->pid_ = getpid();\
    entry->status_ = GC_SERVICE_REQ_NEW;


void GCSrvcClientHandShake::ReqConcCollection() {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_CONC;

  gcservice_data_->cond_->Broadcast(self);
}




void GCSrvcClientHandShake::ReqRegistration(void* params) {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_REG;
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

  _entry->data_addr_ = reinterpret_cast<uintptr_t>(_rec);

  art::gcservice::GCServiceClient::service_client_->FillMemMapData(_rec);
  bool _svcRes =
    android::FileMapperService::MapFds(_rec);
  if(_svcRes) {
    LOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  succeeded; " <<
        _rec->process_id_ << ", "<< _rec->space_index_ <<", "<< _rec->fd_count_
        <<", "<< _rec->mem_maps_[0].fd_;


  } else {
    LOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  Failed";
  }

  gcservice_data_->cond_->Broadcast(self);
}


void GCSrvcClientHandShake::ReqHeapTrim() {
  Thread* self = Thread::Current();
  GCServiceReq* _entry = NULL;

  GC_BUFFER_PUSH_REQUEST(_entry, self);

  _entry->req_type_ = GC_SERVICE_TASK_TRIM;

  gcservice_data_->cond_->Broadcast(self);
}


void GCSrvcClientHandShake::ProcessGCRequest(void* args) {
  GCServiceReq* _entry = NULL;
  _entry = &(gcservice_data_->entries_[gcservice_data_->tail_]);
  gcservice_data_->tail_ = ((gcservice_data_->tail_ + 1) % KProcessMapperCapacity);
  gcservice_data_->available_ = gcservice_data_->available_ + 1;
  gcservice_data_->queued_ = gcservice_data_->queued_ - 1;

  GC_SERVICE_TASK _req_type =
      static_cast<GC_SERVICE_TASK>(_entry->req_type_);


  if(_req_type == GC_SERVICE_TASK_REG) {
    android::FileMapperParameters* _fMapsP =
        reinterpret_cast<android::FileMapperParameters*>(_entry->data_addr_);
    LOG(ERROR) << "Process Indexing tail.. " << gcservice_data_->tail_ <<
        ", head is " << gcservice_data_->head_;
    LOG(ERROR) << "Process existing record:.. " << _fMapsP->space_index_ <<
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
    for(int i = 0; i < _recSecond->fd_count_; i++) {
      memcpy((void*)&(_recSecond->mem_maps_[i]), &(_fMapsP->mem_maps_[i]),
          sizeof(android::IPCAShmemMap));
    }
    bool _svcRes =
        android::FileMapperService::GetMapFds(_recSecond);
    if(_svcRes) {
      for(int i = 0; i < _recSecond->fd_count_; i++) {
        android::IPCAShmemMap* _result = &(_recSecond->mem_maps_[i]);

        LOG(ERROR) << "ProcessQueuedMapper: " << i << "-----" <<
            StringPrintf("fd: %d, flags:%d, prot:%d, size:%s",
                _result->fd_, _result->flags_, _result->prot_,
                PrettySize(_result->size_).c_str());

        _result->flags_ &= MAP_SHARED;
        //_result->prot_ = PROT_READ | PROT_WRITE;
        LOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper:  succeeded.." << _result->fd_;

        byte* actual = reinterpret_cast<byte*>(mmap(NULL, _result->size_,
            _result->prot_, _result->flags_, _result->fd_, 0));

        if(actual == MAP_FAILED) {
          LOG(ERROR) << "MMap failed in creating file descriptor..." << _result->fd_
              << ", size: " << PrettySize(_result->size_) << ", flags: " << _result->flags_
              << ", prot: " << _result->prot_;
        } else {
          LOG(ERROR) << "MMap succeeded in creating file descriptor..." <<
              _result->fd_ <<  StringPrintf(" fd:%d, address: %p; content: 0x%x",
                  _result->fd_, reinterpret_cast<void*>(actual),
                  *(reinterpret_cast<unsigned int*>(actual)))
                  << ", size: " << PrettySize(_result->size_) << ", flags: " <<
                  _result->flags_ << ", prot: " << _result->prot_;

        }
      }

      GCServiceDaemon* daemon = reinterpret_cast<GCServiceDaemon*>(args);
      daemon->client_agents_.push_back(GCSrvceAgent(_newPairEntry));

    } else {
      LOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper: Failed";
    }
  }
}


void GCSrvcClientHandShake::ListenToRequests(void* args) {
  Thread* self = Thread::Current();

  ScopedThreadStateChange tsc(self, kWaitingForGcToComplete);
  {
    IPMutexLock interProcMu(self, *gcservice_data_->mu_);
    LOG(ERROR) << "ListenToRequests: after locking the gcserviceData mutex";
    while(gcservice_data_->queued_ == 0) {
      LOG(ERROR) << "Pull: waiting for broadcast ";
      gcservice_data_->cond_->Wait(self);
      LOG(ERROR) << "ListenToRequests: Somehow we received signal: " << gcservice_data_->queued_;
    }
    LOG(ERROR) << "before calling processGCRequest";
    ProcessGCRequest(args);
    gcservice_data_->cond_->Broadcast(self);
  }
}


}//namespace gcservice
}//namespace gc
}//namespace art




