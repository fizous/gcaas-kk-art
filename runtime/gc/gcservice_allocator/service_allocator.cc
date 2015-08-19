/*
 * service_allocator.cc
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */
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


ServiceAllocator::ServiceAllocator(int pages) :
    service_meta_(NULL),
    heap_meta_arr_(NULL) {

  int fileDescript = 0;
  size_t memory_size = pages * kPageSize;
  MemMap* mu_mem_map =
        MemMap::MapSharedMemoryAnonymous("ServiceAllocator", NULL,
            memory_size  , PROT_READ | PROT_WRITE, &fileDescript);


  if (mu_mem_map == NULL) {
    LOG(ERROR) << "Failed to allocate pages for service allocator (" <<
          "ServiceAllocator" << ") of size "
          << PrettySize(1024);
    return;
  }


//  Thread* self = Thread::Current();

  memory_meta_ =
      reinterpret_cast<SharedRegionMeta*>(mu_mem_map->Begin());
  size_t shift = RoundUp(sizeof(SharedRegionMeta), kAlignment);
  memset((void*) memory_meta_, 0, shift);

  memory_meta_->begin_ = mu_mem_map->Begin();
  memory_meta_->current_addr_ = memory_meta_->begin_;

  memory_meta_->fd_ = fileDescript;
  memory_meta_->size_ = memory_size;


  service_meta_ =
        reinterpret_cast<mprofiler::GCDaemonMetaData*>(allocate(sizeof(mprofiler::GCDaemonMetaData)));

  heap_meta_arr_ = reinterpret_cast<SharedHeapMetada*>(memory_meta_->current_addr_);


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
  return reinterpret_cast<SharedHeapMetada*>(allocate(sizeof(SharedHeapMetada)));
}




ServiceAllocator::~ServiceAllocator() {

}

}//namespace gc
}//namespace art
