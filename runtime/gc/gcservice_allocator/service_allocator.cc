/*
 * service_allocator.cc
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#include <sys/mman.h>
#include <sys/types.h>
#include <cutils/ashmem.h>
#include "os.h"
#include "globals.h"
#include "base/mutex.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "gc/gcservice_allocator/service_allocator.h"
#include "gc_profiler/GCService.h"
#include "gc_profiler/MProfiler.h"

namespace art {
namespace gc {


SharedMemMapMeta* SharedMemMap::CreateSharedMemory(const char* name,
    size_t byte_count, int prot) {
  int flags = MAP_SHARED;
  SharedMemMapMeta* _meta_record = ServiceAllocator::AllocShMemMapMeta();
  _meta_record->size_ = RoundUp(byte_count, kPageSize);
  _meta_record->fd_   = ashmem_create_region(name, _meta_record->size_);
  _meta_record->prot_ = prot;
  if (_meta_record->fd_  == -1) {
    LOG(ERROR) << "ashmem_create_region failed (" << name << ")";
    return NULL;
  }
  _meta_record->owner_begin_ =
      reinterpret_cast<byte*>(mmap(NULL, _meta_record->size_, prot, flags,
          _meta_record->fd_, 0));
  if (_meta_record->owner_begin_ == MAP_FAILED) {
    LOG(ERROR) << "mmap(" <<name<< ")" << ", " <<
        _meta_record->size_ << ", " << prot << ", " << flags << ", " <<
        _meta_record->fd_ << ", 0) failed for " << name << "\n";
    return NULL;
  }
  return _meta_record;
}



ServiceAllocator::ServiceAllocator(int pages) :
    service_meta_(NULL),
    heap_meta_arr_(NULL) {

  int prot = PROT_READ | PROT_WRITE;
  int fileDescript = 0;
  size_t memory_size = pages * kPageSize;
  int flags = MAP_SHARED;
  fileDescript = ashmem_create_region("ServiceAllocator", memory_size);
  byte* begin =
      reinterpret_cast<byte*>(mmap(NULL, memory_size, prot, flags,
           fileDescript, 0));

  if (begin == NULL) {
    LOG(ERROR) << "Failed to allocate pages for service allocator (" <<
          "ServiceAllocator" << ") of size "
          << PrettySize(memory_size);
    return;
  }
  memory_meta_ =
      reinterpret_cast<SharedRegionMeta*>(begin);
  size_t used_bytes = SERVICE_ALLOC_ALIGN_BYTE(SharedRegionMeta);
  memset((void*) memory_meta_, 0, used_bytes);
//  Thread* self = Thread::Current();

  memory_meta_ =
      reinterpret_cast<SharedRegionMeta*>(begin);
  size_t shift = RoundUp(sizeof(SharedRegionMeta), kAlignment);
  memset((void*) memory_meta_, 0, shift);

  memory_meta_->meta_.owner_begin_ = begin;
  memory_meta_->meta_.fd_ = fileDescript;
  memory_meta_->meta_.prot_ = prot;
  memory_meta_->meta_.size_ = memory_size;
  memory_meta_->current_addr_ = memory_meta_->meta_.owner_begin_+ used_bytes;


  service_meta_ =
        reinterpret_cast<mprofiler::GCDaemonMetaData*>(allocate(SERVICE_ALLOC_ALIGN_BYTE(mprofiler::GCDaemonMetaData)));

  heap_meta_arr_ =
      reinterpret_cast<SharedHeapMetada*>(memory_meta_->current_addr_);


  GCSERV_ALLOC_VLOG(INFO) << "done allocating shared header: " <<
      "\nbegin: " << reinterpret_cast<void*>(memory_meta_->meta_.owner_begin_) << ", " <<
      "\nfd: " << memory_meta_->meta_.fd_ << ", " <<
      "\nsize: " << memory_meta_->meta_.size_ <<

  mprofiler::GCServiceDaemon::InitServiceMetaData(service_meta_);
}


/* called by the zygote process */
ServiceAllocator* ServiceAllocator::CreateServiceAllocator() {
  ServiceAllocator* serviceAllocator = new ServiceAllocator(PageCapacity);
  return serviceAllocator;
}

mprofiler::GCDaemonMetaData* ServiceAllocator::GetGCServiceMeta(void) {
  return service_meta_;
}

SharedHeapMetada* ServiceAllocator::GetHeapMetaArr(void) {
  return heap_meta_arr_;
}

SharedHeapMetada* ServiceAllocator::AllocateHeapMeta(void) {
  return reinterpret_cast<SharedHeapMetada*>(allocate(SERVICE_ALLOC_ALIGN_BYTE(SharedHeapMetada)));
}

SharedMemMapMeta* ServiceAllocator::AllocShMemMapMeta(void) {
  return reinterpret_cast<SharedHeapMetada*>(allocate(SERVICE_ALLOC_ALIGN_BYTE(SharedMemMap)));
}

ServiceAllocator::~ServiceAllocator() {

}

}//namespace gc
}//namespace art
