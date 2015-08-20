/*
 * service_allocator.h
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_

#include "os.h"
#include "globals.h"
#include "base/mutex.h"
#include "gc_profiler/MProfiler.h"
#include "gc_profiler/GCService.h"
#include "gc/gcservice_allocator/service_allocator.h"


#define SERVICE_ALLOC_ALIGN_BYTE(T) (RoundUp(sizeof(T), ServiceAllocator::kAlignment))

namespace art {
namespace gc {


typedef struct SharedMemMapMeta_S {
  byte* owner_begin_;
  byte* owner_base_begin_;
  size_t size_;
  int fd_;
  int prot_;
} SharedMemMapMeta;


class SharedMemMap {
public:
  static SharedMemMapMeta* CreateSharedMemory(const char *name,
      size_t byte_count, int prot = PROT_READ | PROT_WRITE);
private:
  SharedMemMapMeta* mem_;
};//SharedMemMap



typedef struct SharedRegionMeta_S {
  // This bitmap itself, word sized for efficiency in scanning.
  SharedMemMapMeta meta_;
  byte* current_addr_;
} SharedRegionMeta;

typedef struct SharedSpaceBitmapMeta_S {
  SharedRegionMeta meta_;
  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  uintptr_t heap_begin_;
} SharedSpaceBitmapMeta;

typedef struct SharedCardTableMeta_S {
  byte* biased_begin_;
  byte* begin_;
  size_t offset_;
}SharedCardTableMeta;

typedef struct SharedAtomicStackMeta_S {

  // Back index (index after the last element pushed).
  volatile int32_t back_index_;

  // Front index, used for implementing PopFront.
  volatile int32_t front_index_;
  // Maximum number of elements.
  size_t capacity_;
  // Memory mapping of the atomic stack.
  SharedRegionMeta mem_meta_;
}SharedAtomicStackMeta;


typedef struct SharedContinuousSpaceMeta_S {
  byte* begin_;
  byte* end_;
  SharedSpaceBitmapMeta bitmap_meta_;
}SharedContinuousSpaceMeta;


typedef struct SharedHeapMetada_S {
  SynchronizedLockHead lock_header_;
  SharedCardTableMeta card_table_meta;
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
  static SharedMemMapMeta* AllocShMemMapMeta(void);


  byte* Begin() {
    return memory_meta_->meta_.owner_begin_;
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
