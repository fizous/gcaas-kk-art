/*
 * global_allocator.cc
 *
 *  Created on: Sep 13, 2015
 *      Author: hussein
 */
#include <string>
#include <cutils/ashmem.h>
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"
#include "runtime.h"
#include "mem_map.h"
#include "gc/service/global_allocator.h"
#include "gc/service/service_space.h"

namespace art {
namespace gc {
namespace gcservice{

GCServiceGlobalAllocator* GCServiceGlobalAllocator::allocator_instant_ = NULL;


GCServiceGlobalAllocator* GCServiceGlobalAllocator::CreateServiceAllocator(void) {
  if(allocator_instant_ != NULL) {
    return allocator_instant_;
  }
  allocator_instant_ = new GCServiceGlobalAllocator(kGCServicePageCapacity);
  return allocator_instant_;
}

byte* GCServiceGlobalAllocator::GCSrvcAllocateSharedSpace(void) {
  GCServiceGlobalAllocator* _inst = CreateServiceAllocator();
  return _inst->allocateSharedSpace();
}




void GCServiceGlobalAllocator::initServiceHeader(void) {
  GCServiceHeader* _header_addr = &region_header_->service_header_;

  _header_addr->status_ = GCSERVICE_STATUS_NONE;
  _header_addr->counter_ = 0;

  SharedFutexData* _futexAddress = &_header_addr->lock_.futex_head_;
  SharedConditionVarData* _condAddress = &_header_addr->lock_.cond_var_;


  _header_addr->mu_   = new InterProcessMutex("GCServiceD Mutex", _futexAddress);
  _header_addr->cond_ = new InterProcessConditionVariable("GCServiceD CondVar",
      *_header_addr->mu_, _condAddress);
}

GCServiceGlobalAllocator::GCServiceGlobalAllocator(int pages) :
    region_header_(NULL) {
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
  MemMap::AShmemFillData(&region_header_->ashmem_meta_, memoryName, begin,
      memory_size, begin, memory_size, prot);
  region_header_->current_addr_ =
      begin + SERVICE_ALLOC_ALIGN_BYTE(GCSrvcGlobalRegionHeader);

  initServiceHeader();
}


byte* GCServiceGlobalAllocator::allocateSharedSpace(void) {
  size_t _allocation_size =
      SERVICE_ALLOC_ALIGN_BYTE(space::GCSrvceDlMallocSpace);
  Thread* self = Thread::Current();
  IterProcMutexLock interProcMu(self, *region_header_->service_header_.mu_);

  int _counter = region_header_->service_header_.counter_++;
  byte* _addr = allocate(_allocation_size);
  region_header_->service_header_.cond_->Broadcast(self);

  return _addr;
}


}//namespace gcservice

}//namespace gc
}//namespace art




