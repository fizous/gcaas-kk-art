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
#include "gc/gcservice/service_allocator.h"
#include "gc_profiler/MProfiler.h"

namespace art {
namespace gcservice {


ServiceAllocator* ServiceAllocator::service_allocator_ = NULL;


//SharedMemMapMeta* SharedMemMap::CreateSharedMemory(const char* name,
//    size_t byte_count, int prot) {
//  int flags = MAP_SHARED;
//  SharedMemMapMeta* _meta_record = ServiceAllocator::AllocShMemMapMeta();
//  _meta_record->base_size_ = RoundUp(byte_count, kPageSize);
//  _meta_record->size_ = 0;
//  _meta_record->fd_   = ashmem_create_region(name, _meta_record->base_size_);
//  _meta_record->prot_ = prot;
//  if (_meta_record->fd_  == -1) {
//    LOG(ERROR) << "ashmem_create_region failed (" << name << ")";
//    return NULL;
//  }
//  _meta_record->owner_begin_ =
//      reinterpret_cast<byte*>(mmap(NULL, _meta_record->base_size_, prot, flags,
//          _meta_record->fd_, 0));
//  if (_meta_record->owner_begin_ == MAP_FAILED) {
//    LOG(ERROR) << "mmap(" <<name<< ")" << ", " <<
//        _meta_record->base_size_ << ", " << prot << ", " << flags << ", " <<
//        _meta_record->fd_ << ", 0) failed for " << name << "\n";
//    return NULL;
//  }
//  return _meta_record;
//}

SharedSpaceMeta* GetSpaceMetAddr(SharedHeapMetada*) {

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
  size_t shift = SERVICE_ALLOC_ALIGN_BYTE(SharedRegionMeta);
  memset((void*) memory_meta_, 0, shift);

  memory_meta_->meta_.owner_begin_ = begin;
  memory_meta_->meta_.fd_ = fileDescript;
  memory_meta_->meta_.prot_ = prot;
  memory_meta_->meta_.base_size_ = memory_size;
  memory_meta_->current_addr_ = memory_meta_->meta_.owner_begin_+ used_bytes;
  memory_meta_->meta_.size_ = used_bytes;

  service_meta_ =
        reinterpret_cast<GCServiceMetaData*>(allocate(SERVICE_ALLOC_ALIGN_BYTE(GCServiceMetaData)));

  heap_meta_arr_ =
      reinterpret_cast<SharedHeapMetada*>(memory_meta_->current_addr_);


  GCSERV_ALLOC_ILOG << "done allocating shared header: " <<
      "\nbegin: " << reinterpret_cast<void*>(memory_meta_->meta_.owner_begin_) << ", " <<
      "\nfd: " << memory_meta_->meta_.fd_ << ", " <<
      "\nsize: " << memory_meta_->meta_.size_;

}


/* called by the zygote process */
ServiceAllocator* ServiceAllocator::CreateServiceAllocator() {
  if(service_allocator_ == NULL) {
    ServiceAllocator::service_allocator_ = new ServiceAllocator(PageCapacity);
  }
  return service_allocator_;
}

GCServiceMetaData* ServiceAllocator::GetGCServiceMeta(void) {
  return service_meta_;
}

SharedHeapMetada* ServiceAllocator::GetHeapMetaArr(int _index) {
  return &heap_meta_arr_[_index];
}

SharedHeapMetada* ServiceAllocator::AllocateHeapMeta(void) {
  size_t hMetaSize = SERVICE_ALLOC_ALIGN_BYTE(SharedHeapMetada);
  SharedHeapMetada* metaP =
      reinterpret_cast<SharedHeapMetada*>(ServiceAllocator::service_allocator_->allocate(hMetaSize));
  return metaP;
}

SharedMemMapMeta* ServiceAllocator::AllocShMemMapMeta(void) {
  size_t memMetaSize = SERVICE_ALLOC_ALIGN_BYTE(SharedMemMapMeta);
  SharedMemMapMeta* memMeta =
      reinterpret_cast<SharedMemMapMeta*>(ServiceAllocator::service_allocator_->allocate(memMetaSize));
  return memMeta;
}

ServiceAllocator::~ServiceAllocator() {

}

}//namespace gcservice
}//namespace art
