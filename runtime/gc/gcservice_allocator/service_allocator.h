/*
 * service_allocator.h
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_


#include "gc/gcservice_allocator/service_allocator.h"
#include "gc/gcservice_allocator/shared_space_bitmap.h"

namespace art {
namespace gc {


typedef struct SharedRegionMeta_S {
  // This bitmap itself, word sized for efficiency in scanning.
  byte* begin_;
  size_t size_;
  size_t end_;
  size_t current_addr_;
  int fd_;
}SharedRegionMeta;

typedef struct SharedHeapMetada_S {
  SynchronizedLockHead lock_header_;
  accounting::SharedSpaceBitmapMeta bitmap_meta_;
  int pid_;
  InterProcessMutex* ipc_global_mu_;
  InterProcessConditionVariable* ipc_global_cond_;
} SharedHeapMetada;

class ServiceAllocator {
public:
  // Alignment of objects within spaces.
  static const size_t kAlignment = 8;
  // Alignment of objects within spaces.
  static const size_t PageCapacity = 64;

  static ServiceAllocator* CreateServiceAllocator(void);

  byte* Begin() {
    return memory_meta_->begin_;
  }


  byte* GetAddr() {
    return memory_meta_->current_addr_;
  }

  byte* allocate(size_t num_bytes) {
    byte* _addr = memory_meta_->current_addr_;
    memory_meta_->current_addr_ +=  RoundUp(num_bytes, kAlignment);
    return _addr;
  }

  mprofiler::GCDaemonMetaData* GetGCServiceMeta(void);

  SharedHeapMetada* AllocateHeapMeta(void);

  SharedHeapMetada* GetHeapMetaArr(void);

  // constructor
  ServiceAllocator(int pages);
  // destructor
  ~ServiceAllocator();

  void free();


private:
  // Size of this bitmap.
  SharedRegionMeta* memory_meta_;
  mprofiler::GCDaemonMetaData* service_meta_;
  SharedHeapMetada* heap_meta_arr_;
}; //ServiceAllocator





}//namespace gc
}//namespace art


#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_ */
