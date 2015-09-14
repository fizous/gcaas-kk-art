/*
 * service_space.h
 *
 *  Created on: Sep 11, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SERVICE_SPACE_SERVICE_SPACE_H_
#define ART_RUNTIME_GC_SERVICE_SPACE_SERVICE_SPACE_H_

#include "globals.h"
#include "utils.h"
#include "gc/space/space_common.h"
#include "gc/space/dlmalloc_space.h"

namespace art {
namespace gc {
namespace space {

// Recent allocation buffer.
static constexpr size_t kRecentFreeCountService = kDebugSpaces ? (1 << 16) : 0;
static constexpr size_t kRecentFreeMaskService = kRecentFreeCountService - 1;

typedef struct GCSrvceSpace_S {
  char name_[64];
  // When should objects within this space be reclaimed? Not constant as we vary it in the case
  // of Zygote forking.
  GcRetentionPolicy gc_retention_policy_;
}  __attribute__((aligned(8))) GCSrvceSpace;

typedef struct GCSrvceContinuousSpace_S {
  GCSrvceSpace space_header_;
  // The beginning of the storage for fast access.
  byte* /*const*/ begin_;

  // Current end of the space.
  byte* end_;
}  __attribute__((aligned(8))) GCSrvceContinuousSpace;



typedef struct GCSrvceBitmap_S {
  // Backing storage for bitmap.
  AShmemMap mem_map_;

  // This bitmap itself, word sized for efficiency in scanning.
  word* /*const*/ bitmap_begin_;

  // Size of this bitmap.
  size_t bitmap_size_;

  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  /*const*/ uintptr_t heap_begin_;

  char name_[64];
} __attribute__((aligned(8)))  GCSrvceBitmap;


class SharedDlMallocSpace;
class DlMallocSpace;

typedef struct GCSrvceDlMallocSpace_S {
  GCSrvceContinuousSpace continuous_space_;

  /* allocated space memory */
  AShmemMap memory_;

  // Underlying malloc space
  void* /*const*/ mspace_;

  // The capacity of the alloc space until such time that ClearGrowthLimit is called.
  // The underlying mem_map_ controls the maximum size we allow the heap to grow to. The growth
  // limit is a value <= to the mem_map_ capacity used for ergonomic reasons because of the zygote.
  // Prior to forking the zygote the heap will have a maximally sized mem_map_ but the growth_limit_
  // will be set to a lower value. The growth_limit_ is used as the capacity of the alloc_space_,
  // however, capacity normally can't vary. In the case of the growth_limit_ it can be cleared
  // one time by a call to ClearGrowthLimit.
  size_t growth_limit_;

  std::pair<const mirror::Object*, mirror::Class*>
                    recent_freed_objects_[kRecentFreeCountService];
  size_t recent_free_pos_;

  // Approximate number of bytes which have been allocated into the space.
  size_t num_bytes_allocated_;
  size_t num_objects_allocated_;
  size_t total_bytes_allocated_;
  size_t total_objects_allocated_;


  GCSrvceBitmap live_bitmap_;
  GCSrvceBitmap mark_bitmap_;



  static size_t bitmap_index_;


  SynchronizedLockHead lock_;
}  __attribute__((aligned(8))) GCSrvceDlMallocSpace;


class SharedDlMallocSpace : public SharableSpace , public ContinuousSpace,
                            public AllocSpace {

 public:
  // Alignment of objects within spaces.
  static const size_t kAlignment = 8;

  // The boundary tag overhead.
  static const size_t kChunkOverhead = kWordSize;

  typedef void(*SharedDlSpaceWalkCallback)(void *start, void *end, size_t num_bytes, void* callback_arg);

  SpaceType GetType() const {
    if (GetGcRetentionPolicy() == kGcRetentionPolicyFullCollect) {
      return kSpaceTypeZygoteSpace;
    } else {
      return kSpaceTypeAllocSpace;
    }
  }
  // <offset> is the difference from .base to a pointer address.
  // <index> is the index of .bits that contains the bit representing
  //         <offset>.
  static size_t BitmapOffsetToIndex(size_t offset) {
    return offset / kAlignment / kBitsPerWord;
  }
  // Create a AllocSpace with the requested sizes. The requested
  // base address is not guaranteed to be granted, if it is required,
  // the caller should call Begin on the returned space to confirm
  // the request was granted.
  static SharedDlMallocSpace* Create(const std::string& name,
                               GcRetentionPolicy retentionPolicy,
                               size_t initial_size, size_t growth_limit,
                               size_t capacity, byte* requested_begin);

  SharedDlMallocSpace(const std::string& name,
      size_t initial_size, size_t growth_limit, size_t capacity,
      byte* requested_begin, size_t starting_size);

  // Allocate num_bytes allowing the underlying mspace to grow.
  mirror::Object* Alloc(Thread* self, size_t num_bytes,
      size_t* bytes_allocated);

  mirror::Object* AllocWithoutGrowthLocked(size_t num_bytes,
      size_t* bytes_allocated);

  mirror::Object* AllocWithGrowth(Thread* self,
      size_t num_bytes, size_t* bytes_allocated);

  size_t AllocationSizeNonvirtual(const mirror::Object* obj) {
    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj))) +
        kChunkOverhead;
  }

  size_t AllocationNoOverhead(const mirror::Object* obj) {
    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj)));
  }

  size_t InternalAllocationSize(const mirror::Object* obj);

  // Return the storage space required by obj.
  size_t AllocationSize(const mirror::Object* obj);

  size_t Free(Thread* self, mirror::Object* ptr);
  size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs);

  void* MoreCore(intptr_t increment);

  // Perform a mspace_inspect_all which calls back for each allocation chunk. The chunk may not be
  // in use, indicated by num_bytes equaling zero.
  void Walk(SharedDlSpaceWalkCallback callback, void* arg) LOCKS_EXCLUDED(*mu_);

  // Name of the space. May vary, for example before/after the Zygote fork.
  const char* GetName() const {
    return alloc_space_->continuous_space_.space_header_.name_;
  }

  // The policy of when objects are collected associated with this space.
  GcRetentionPolicy GetGcRetentionPolicy() const {
    return alloc_space_->continuous_space_.space_header_.gc_retention_policy_;
  }

  uint64_t GetBytesAllocated() const {
    return alloc_space_->num_bytes_allocated_;
  }

  uint64_t GetObjectsAllocated() const {
    return alloc_space_->num_objects_allocated_;
  }

  uint64_t GetTotalBytesAllocated() const {
    return alloc_space_->total_bytes_allocated_;
  }

  uint64_t GetTotalObjectsAllocated() const {
    return alloc_space_->total_objects_allocated_;
  }

  // Address at which the space begins
  byte* Begin() const {
    return alloc_space_->continuous_space_.begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  byte* End() const {
    return alloc_space_->continuous_space_.end_;
  }

  // Current size of space
  size_t Size() const {
    return End() - Begin();
  }

  // Override capacity so that we only return the possibly limited capacity
  size_t Capacity() const {
    return alloc_space_->growth_limit_;
  }

  // The total amount of memory reserved for the alloc space.
  size_t NonGrowthLimitCapacity() const {
    return MemMap::AshmemSize(&alloc_space_->memory_);
  }

  // Removes the fork time growth limit on capacity, allowing the application to allocate up to the
  // maximum reserved size of the heap.
  void ClearGrowthLimit() {
    alloc_space_->growth_limit_ = NonGrowthLimitCapacity();
  }

  void Dump(std::ostream& os) const;

  // Returns the number of bytes that the space has currently obtained from the
  // system. This is greater or equal to the amount of live data in the space.
  size_t GetFootprint();

  // Returns the number of bytes that the heap is allowed to obtain from the
  // system via MoreCore.
  size_t GetFootprintLimit();

  // Set the maximum number of bytes that the heap is allowed to obtain from
  // the system via MoreCore. Note this is used to stop the mspace growing
  // beyond the limit to Capacity. When allocations fail we GC before
  // increasing the footprint limit and allowing the mspace to grow.
  void SetFootprintLimit(size_t limit);

  // Hands unused pages back to the system.
  size_t Trim();

  void SetGrowthLimit(size_t growth_limit);

  // Swap the live and mark bitmaps of this space. This is used by the GC for
  // concurrent sweeping.
  void SwapBitmaps();

  accounting::SpaceBitmap* GetLiveBitmap() const{return NULL;}
  accounting::SpaceBitmap* GetMarkBitmap() const{return NULL;}

  // Returns the class of a recently freed object.
  mirror::Class* FindRecentFreedObject(const mirror::Object* obj);

  bool CreateBitmaps(byte* heap_begin, size_t heap_capacity);
  bool SpaceBitmapInit(GCSrvceBitmap *hb,
      const std::string& name, byte* heap_begin, size_t heap_capacity,
      size_t bitmap_size);

 private:
  GCSrvceDlMallocSpace* alloc_space_;
  InterProcessMutex*    mu_;
  InterProcessConditionVariable* cond_;
  void RegisterRecentFree(mirror::Object* ptr);
};//SharedDlMallocSpace

}//namespace space


}//namespace gc
}//namespace art












#endif /* ART_RUNTIME_GC_SERVICE_SPACE_SERVICE_SPACE_H_ */
