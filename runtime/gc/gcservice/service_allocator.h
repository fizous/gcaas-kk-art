/*
 * service_allocator.h
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_
#define ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_

#include <sys/mman.h>
#include <sys/types.h>
#include <cutils/ashmem.h>
#include "base/mutex.h"
#include "os.h"
#include "globals.h"
#include "gc_profiler/MProfiler.h"
#include "gc/gcservice/common.h"


#define SERVICE_ALLOC_ALIGN_BYTE(T) (RoundUp(sizeof(T), ServiceAllocator::kAlignment))

namespace art {





namespace gcservice {


//class SharedMemMap {
//public:
//  static SharedMemMapMeta* CreateSharedMemory(const char *name,
//      size_t byte_count, int prot = PROT_READ | PROT_WRITE);
//private:
//  SharedMemMapMeta* mem_;
//};//SharedMemMap



typedef struct SharedRegionMeta_S {
  // This bitmap itself, word sized for efficiency in scanning.
  SharedMemMapMeta meta_;
  byte* current_addr_;
} SharedRegionMeta;


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


//typedef struct SharedContinuousSpaceMeta_S {
//  byte* begin_;
//  byte* end_;
//  SharedSpaceBitmapMeta bitmap_meta_;
//}SharedContinuousSpaceMeta;







class ServiceAllocator {
public:
  // Alignment of objects within spaces.
  static const size_t kAlignment = 8;
  // Alignment of objects within spaces.
  static const size_t PageCapacity = 64;

//  static size_t GetSharedHeapMetaSize(void) {
//    size_t totalSize =
//        (2 * SERVICE_ALLOC_ALIGN_BYTE(SynchronizedLockHead);
//  }

  static ServiceAllocator* CreateServiceAllocator(void);
  static SharedMemMapMeta* AllocShMemMapMeta(void);
//  static SharedSpaceMeta* GetSpaceMetAddr(SharedHeapMetada*);

  byte* Begin() {
    return memory_meta_->meta_.owner_begin_;
  }


  byte* GetAddr() {
    return memory_meta_->current_addr_;
  }

  byte* allocate(size_t num_bytes) {
    byte* _addr = memory_meta_->current_addr_;
    size_t allocated_bytes = RoundUp(num_bytes, kAlignment);
    memory_meta_->current_addr_ +=  allocated_bytes;
    memory_meta_->meta_.size_ += allocated_bytes;
    return _addr;
  }

  GCServiceMetaData* GetGCServiceMeta(void);

  SharedHeapMetada* AllocateHeapMeta(void);

  SharedHeapMetada* GetHeapMetaArr(int);

  // constructor
  ServiceAllocator(int pages);
  // destructor
  ~ServiceAllocator();

  void free();



private:
  // Size of this bitmap.
  SharedRegionMeta* memory_meta_;
  static ServiceAllocator* service_allocator_;
  GCServiceMetaData* service_meta_;
  SharedHeapMetada* heap_meta_arr_;
}; //ServiceAllocator





}//namespace gc
}//namespace art


#endif /* ART_RUNTIME_GC_GCSERVICE_ALLOCATOR_SERVICE_ALLOCATOR_H_ */
