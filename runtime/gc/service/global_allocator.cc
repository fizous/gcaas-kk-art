/*
 * global_allocator.cc
 *
 *  Created on: Sep 13, 2015
 *      Author: hussein
 */
#include <string>
#include "mem_map.h"
#include <cutils/ashmem.h>
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


void GCSrvcClientHandShake::ResetProcessMap(android::FileMapperParameters* record) {
  memset(record, 0, sizeof(android::FileMapperParameters));
}

void GCSrvcClientHandShake::Init() {
  mem_data_->available_ = KProcessMapperCapacity;
  mem_data_->queued_ = 0;
  mem_data_->tail_ = 0;
  mem_data_->head_ = KProcessMapperCapacity - 1;

  SharedFutexData* _futexAddress = &mem_data_->lock_.futex_head_;
  SharedConditionVarData* _condAddress = &mem_data_->lock_.cond_var_;


  mem_data_->mu_   = new InterProcessMutex("HandShake Mutex", _futexAddress);
  mem_data_->cond_ = new InterProcessConditionVariable("HandShake CondVar",
      *mem_data_->mu_, _condAddress);

  for(int i = 0; i < KProcessMapperCapacity; i++) {
    ResetProcessMap(&(mem_data_->process_mappers_[i]));
  }
}

android::FileMapperParameters* GCSrvcClientHandShake::GetMapperRecord(int index,
    int* fdArr, int* byte_counts) {
  Thread* self = Thread::Current();
  IPMutexLock interProcMu(self, *mem_data_->mu_);
  mem_data_->head_ = (mem_data_->head_ + 1) % KProcessMapperCapacity;
  android::FileMapperParameters* _rec =
      &(mem_data_->process_mappers_[mem_data_->head_]);


  space::SharableDlMallocSpace* _space =
      GCServiceClient::service_client_->GetSharableSpace();

  _space->FillMemoryMappers(_rec);


//  _rec->process_id_  = getpid();
//  _rec->space_index_ = index;
//  _rec->fd_count_ = IPC_FILE_MAPPER_CAPACITY;
//  memcpy((void*)_rec->fds_, fdArr, IPC_FILE_MAPPER_CAPACITY * sizeof(int));
//  memcpy((void*)_rec->byte_counts_, byte_counts, IPC_FILE_MAPPER_CAPACITY * sizeof(int));

  bool _svcRes =
    android::FileMapperService::MapFds(_rec);

  if(_svcRes) {
    LOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  succeeded; " <<
        _rec->process_id_ << ", "<< _rec->space_index_ <<", "<< _rec->fd_count_;


  } else {
    LOG(ERROR) << " __________ GCSrvcClientHandShake::GetMapperRecord:  Failed";
  }

  mem_data_->available_ -= 1;
  mem_data_->queued_++;

  mem_data_->cond_->Broadcast(self);

  return _rec;
}




GCSrvcClientHandShake::GCSrvcClientHandShake(GCServiceClientHandShake* alloc_mem) :
    mem_data_(alloc_mem) {
  Init();
}

void GCSrvcClientHandShake::ProcessQueuedMapper(android::MappedPairProcessFD* entry){
 // Thread* self = Thread::Current();
  //LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper Locking mem_data_->mu_";
  //IPMutexLock interProcMu(self, *mem_data_->mu_);
  LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper  " << mem_data_->queued_;
  while(mem_data_->queued_ > 0) {
    LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper after mem_data_->queued_ " << mem_data_->queued_;
    android::FileMapperParameters* _rec =
       &(mem_data_->process_mappers_[mem_data_->tail_]);
    LOG(ERROR) << "Process Indexing tail.. " << mem_data_->tail_ <<", head is " << mem_data_->head_;
    LOG(ERROR) << "Process existing record:.. " << _rec->space_index_ <<
        ", " << _rec->process_id_;


    memcpy((void*)entry->first, _rec, sizeof(android::FileMapperParameters));


    android::FileMapperParameters* _recSecond = entry->second;
    _recSecond->process_id_ = _rec->process_id_;
    _recSecond->space_index_ = _rec->space_index_;
    _recSecond->fd_count_ = _rec->fd_count_;
    bool _svcRes =
      android::FileMapperService::GetMapFds(_recSecond);
    if(_svcRes) {
      LOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper:  "
          "succeeded..";

      int _fd_value = 0;
      size_t _bytes_cnt = 0;
      for(int i = 0; i < _recSecond->fd_count_; i++) {
        _fd_value = _recSecond->mapped_recs_[i].fd_;
        _bytes_cnt = entry->first->mapped_recs_[i].size_;
        _recSecond->mapped_recs_[i].size_ = _bytes_cnt;
        _recSecond->mapped_recs_[i].flags_ = entry->first->mapped_recs_[i].flags_;
        _recSecond->mapped_recs_[i].prot_ = entry->first->mapped_recs_[i].prot_;
        byte* actual = reinterpret_cast<byte*>(mmap(NULL,
            _bytes_cnt,
            _recSecond->mapped_recs_[i].prot_,
            _recSecond->mapped_recs_[i].flags_,
            _recSecond->mapped_recs_[i].fd_, 0));
        if(actual == MAP_FAILED) {
          LOG(ERROR) << "MMap failed in creating file descriptor..." << _fd_value;
        } else {
          LOG(ERROR) << "MMap succeeded in creating file descriptor..." <<
              _fd_value << " " <<
              StringPrintf("fd[%d], size = %u, address: %p; content: 0x%x",
                 i, _bytes_cnt, reinterpret_cast<void*>(actual),
                  *(reinterpret_cast<unsigned int*>(actual))) ;

        }
      }
    } else {
      LOG(ERROR) << " __________ GCSrvcClientHandShake::ProcessQueuedMapper:  Failed";
    }
    mem_data_->tail_ = (mem_data_->tail_ + 1) % KProcessMapperCapacity;
    mem_data_->available_ += 1;
    mem_data_->queued_--;
  }
  LOG(ERROR) << " __________GCSrvcClientHandShake::ProcessQueuedMapper after while" << mem_data_->queued_;
  //mem_data_->cond_->Broadcast(self);
}


}//namespace gcservice

}//namespace gc
}//namespace art




