/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_GC_SPACE_DLMALLOC_SPACE_H_
#define ART_RUNTIME_GC_SPACE_DLMALLOC_SPACE_H_

#include "space.h"
#include "thread.h"
#include "gc/heap.h"
#include "gc/allocator/dlmalloc.h"
#include "gc/accounting/space_bitmap.h"
#include "gc_profiler/MProfiler.h"



#ifndef DLMALLOC_SPACE_LOCK_MACRO
  #if (ART_GC_SERVICE)
    #define DLMALLOC_SPACE_LOCK_MACRO MutexLock mu(Thread::Current(), *getMu())
  #else
    #define DLMALLOC_SPACE_LOCK_MACRO MutexLock mu(Thread::Current(), lock_)
  #endif
#endif


namespace art {
namespace mprofiler {
	class MProfiler;
}

namespace gc {

namespace collector {
  class MarkSweep;
}  // namespace collector

namespace space {

//class SharedDlMallocSpace;

#if (ART_GC_SERVICE)





//class StructuredDlMallocSpaceImpl : public IDlMallocSpace {
// public:
//  SpaceType GetType() const {
//    if (GetGcRetentionPolicy() == kGcRetentionPolicyFullCollect) {
//      return kSpaceTypeZygoteSpace;
//    } else {
//      return kSpaceTypeAllocSpace;
//    }
//  }
//
//
//  // Swap the live and mark bitmaps of this space. This is used by the GC for
//  // concurrent sweeping.
//  void SwapBitmaps();
//  void SetInternalGrowthLimit(size_t);
//  void SetFootprintLimit(size_t limit);
// protected:
//  StructuredDlMallocSpaceImpl(){}
// private:
//  virtual ~StructuredDlMallocSpaceImpl(){}
//};//class StructuredDlMallocSpaceImpl
//
//class DlMallocSpaceImpl : public MemMapSpace//, public AllocSpace {
//
//};

// An alloc space is a space where objects may be allocated and garbage collected.
class DlMallocSpace : public MemMapSpace, public IDlMallocSpace//, public AllocSpace
                      /*public AbstractDLmallocSpace*/ {
 public:
  typedef void(*WalkCallback)(void *start, void *end, size_t num_bytes, void* callback_arg);

  // The boundary tag overhead.
  static const size_t kChunkOverhead = kWordSize;
  //Todo: Fizo: I do not know why I need this?
  static const size_t kAlignment = 8;
  SpaceType GetType() const {
    if (GetGcRetentionPolicy() == kGcRetentionPolicyFullCollect) {
      return kSpaceTypeZygoteSpace;
    } else {
      return kSpaceTypeAllocSpace;
    }
  }


  static size_t BitmapOffsetToIndex(size_t offset) {
    return offset / kAlignment / kBitsPerWord;
  }
  // Create a AllocSpace with the requested sizes. The requested
  // base address is not guaranteed to be granted, if it is required,
  // the caller should call Begin on the returned space to confirm
  // the request was granted.
  static DLMALLOC_SPACE_T* Create(const std::string& name, size_t initial_size, size_t growth_limit,
                               size_t capacity, byte* requested_begin, bool shareMem = false);

  // Allocate num_bytes without allowing the underlying mspace to grow.
  virtual mirror::Object* AllocWithGrowth(Thread* self, size_t num_bytes,
                                          size_t* bytes_allocated) LOCKS_EXCLUDED(getMu());

  // Allocate num_bytes allowing the underlying mspace to grow.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated);
  virtual accounting::SPACE_BITMAP* UnBindBitmaps(void);
  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj);
  virtual size_t Free(Thread* self, mirror::Object* ptr);
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs);
  virtual size_t FreeListAgent(Thread* self, size_t num_ptrs, mirror::Object** ptrs);

  mirror::Object* AllocNonvirtual(Thread* self, size_t num_bytes, size_t* bytes_allocated);

  size_t AllocationSizeNonvirtual(const mirror::Object* obj) {
    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj))) +
        kChunkOverhead;
  }


  void AllocationSizes(const mirror::Object* obj, size_t* nonVirtualNoOverhead, size_t* nonVirtual) {
    *nonVirtualNoOverhead = mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj)));
    *nonVirtual = *nonVirtualNoOverhead + kChunkOverhead;
  }

//  size_t AllocationNoOverhead(const mirror::Object* obj) {
//    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj)));
//  }

  size_t GCPGetAllocationSize(const mirror::Object*);

  void* MoreCore(intptr_t increment);

  void* GetMspace() const {
    return dlmalloc_space_data_->mspace_;
  }

  // Hands unused pages back to the system.
  size_t Trim();

  // Perform a mspace_inspect_all which calls back for each allocation chunk. The chunk may not be
  // in use, indicated by num_bytes equaling zero.
  void Walk(WalkCallback callback, void* arg) LOCKS_EXCLUDED(getMu());

  // Returns the number of bytes that the space has currently obtained from the system. This is
  // greater or equal to the amount of live data in the space.
  size_t GetFootprint();

  // Returns the number of bytes that the heap is allowed to obtain from the system via MoreCore.
  size_t GetFootprintLimit();

  // Set the maximum number of bytes that the heap is allowed to obtain from the system via
  // MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
  // allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
  void SetFootprintLimit(size_t limit);

  // Removes the fork time growth limit on capacity, allowing the application to allocate up to the
  // maximum reserved size of the heap.
  void ClearGrowthLimit() {
    SetInternalGrowthLimit(NonGrowthLimitCapacity());
  }

  // Override capacity so that we only return the possibly limited capacity
  size_t Capacity() const {
    return dlmalloc_space_data_->growth_limit_;
  }

  // The total amount of memory reserved for the alloc space.
  size_t NonGrowthLimitCapacity() const {
    return GetMemMap()->Size();
  }

  accounting::SPACE_BITMAP* GetLiveBitmap() const {
    return live_bitmap_.get();
  }

  accounting::SPACE_BITMAP* GetMarkBitmap() const {
    return mark_bitmap_.get();
  }

  void Dump(std::ostream& os) const;

  void SetGrowthLimit(size_t growth_limit);

  //fizo, we need to override this
  // Swap the live and mark bitmaps of this space. This is used by the GC for concurrent sweeping.
  virtual void SwapBitmaps();
  virtual void BindLiveToMarkBitmaps(void);

//  virtual void BindLiveToMarkBitmap(void) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//  virtual void UnBindBitmaps(void) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  // Turn ourself into a zygote space and return a new alloc space which has our unused memory.
  DLMALLOC_SPACE_T* CreateZygoteSpace(const char* alloc_space_name,
      bool shareMem = false);

  DLMALLOC_SPACE_T* CreateSharableZygoteSpace(const char* alloc_space_name,
      GCSrvSharableDlMallocSpace* sharable_dlmalloc_space, bool shareMem = false);


  virtual uint64_t GetBytesAllocated() const {
    return dlmalloc_space_data_->num_bytes_allocated_;
  }

  virtual uint64_t GetObjectsAllocated() const {
    return dlmalloc_space_data_->num_objects_allocated_;
  }

  virtual uint64_t GetTotalBytesAllocated() const {
    return dlmalloc_space_data_->total_bytes_allocated_;
  }

  virtual uint64_t GetTotalObjectsAllocated() const {
    return dlmalloc_space_data_->total_objects_allocated_;
  }

  virtual void UpdateBytesAllocated(int delta) {
    dlmalloc_space_data_->num_bytes_allocated_ += delta;
  }

  virtual void UpdateObjectsAllocated(int delta) {
    dlmalloc_space_data_->num_objects_allocated_ += delta;
  }

  virtual void UpdateTotalBytesAllocated(int delta) {
    dlmalloc_space_data_->total_bytes_allocated_ += delta;
  }

  virtual void UpdateTotalObjectsAllocated(int delta) {
    dlmalloc_space_data_->total_objects_allocated_ += delta;
  }


  size_t GetRecentFreePos(){
    return dlmalloc_space_data_->recent_free_pos_;
  }

  void SetRecentFreePos(size_t newVal){
    dlmalloc_space_data_->recent_free_pos_ = newVal;
  }

  void SetRecentFreeObject(mirror::Object* ptr){
    dlmalloc_space_data_->recent_freed_objects_[GetRecentFreePos()].first = ptr;
    dlmalloc_space_data_->recent_freed_objects_[GetRecentFreePos()].second = ptr->GetClass();
  }

  // Returns the class of a recently freed object.
  mirror::Class* FindRecentFreedObject(const mirror::Object* obj);
  static void* CreateMallocSpace(void* base, size_t morecore_start, size_t initial_size);


  virtual void SetInternalGrowthLimit(size_t new_growth_limit) {
    dlmalloc_space_data_->growth_limit_ = new_growth_limit;
  }

  Mutex* getMu() LOCK_RETURNED(lock_data_) { return reinterpret_cast<Mutex*>(dlmalloc_space_data_->lock_); }

  virtual bool CreateBitmaps(byte* heap_begin, size_t heap_capacity,
      bool shareMem = false);

  GCSrvDlMallocSpace* dlmalloc_space_data_;
//  SharedDlMallocSpace* CreateZygoteSpaceWithSharedSpace(const char* alloc_space_name);


  virtual bool RegisterGlobalCollector(const char* se_name_c_str);
//  AbstractContinuousSpace* AsAbstractContinuousSpace() {
//    return reinterpret_cast<AbstractContinuousSpace*>(this);
//  }

//  AbstractDLmallocSpace* AsAbstractDlMalloc() {
//    return reinterpret_cast<AbstractDLmallocSpace*>(this);
//  }
  // Recent allocation buffer.

  virtual void SetHeapMeta(DlMallocSpace*, GCSrvceContinuousSpace*){}

  const mirror::Object* GetObjectRecentFreeObject(int pos){
    return dlmalloc_space_data_->recent_freed_objects_[pos].first;
  }

  mirror::Class* GetClassRecentFreeObject(int pos){
    return dlmalloc_space_data_->recent_freed_objects_[pos].second;
  }
  mirror::Object* publicAllocWithoutGrowthLocked(size_t num_bytes, size_t* bytes_allocated) {

    mirror::Object* result = reinterpret_cast<mirror::Object*>(mspace_malloc(GetMspace(), num_bytes));
    if(result != NULL) {
      *bytes_allocated = AllocationSizeNonvirtual(result);
      return result;
    }
    LOG(ERROR) << "publicAllocWithoutGrowthLocked..could not allocate object";
    return NULL;
  }
 protected:
  DlMallocSpace(const std::string& name, MEM_MAP* mem_map, void* mspace,
      byte* begin, byte* end, size_t growth_limit, bool shareMem = false,
      GCSrvDlMallocSpace* space_data_mem = NULL);
  ~DlMallocSpace(){}
  UniquePtr<accounting::SPACE_BITMAP> live_bitmap_;
  UniquePtr<accounting::SPACE_BITMAP> mark_bitmap_;
  UniquePtr<accounting::SPACE_BITMAP> temp_bitmap_;
 private:
  size_t InternalAllocationSize(const mirror::Object* obj);
  mirror::Object* AllocWithoutGrowthLocked(size_t num_bytes, size_t* bytes_allocated)
      EXCLUSIVE_LOCKS_REQUIRED(getMu());
  bool Init(size_t initial_size, size_t maximum_size, size_t growth_size, byte* requested_base);
  void RegisterRecentFree(mirror::Object* ptr);


//  UniquePtr<accounting::SPACE_BITMAP> live_bitmap_;
//  UniquePtr<accounting::SPACE_BITMAP> mark_bitmap_;
//  UniquePtr<accounting::SPACE_BITMAP> temp_bitmap_;

  //std::pair<const mirror::Object*, mirror::Class*> recent_freed_objects_[kRecentFreeCount];
//  size_t recent_free_pos_;
//
//  // Approximate number of bytes which have been allocated into the space.
//  size_t num_bytes_allocated_;
//  size_t num_objects_allocated_;
//  size_t total_bytes_allocated_;
//  size_t total_objects_allocated_;

  //static size_t bitmap_index_;



//  // Used to ensure mutual exclusion when the allocation spaces data structures are being modified.
   Mutex lock_data_ DEFAULT_MUTEX_ACQUIRED_AFTER;

//  // Underlying malloc space
//  void* const mspace_;

  // The capacity of the alloc space until such time that ClearGrowthLimit is called.
  // The underlying mem_map_ controls the maximum size we allow the heap to grow to. The growth
  // limit is a value <= to the mem_map_ capacity used for ergonomic reasons because of the zygote.
  // Prior to forking the zygote the heap will have a maximally sized mem_map_ but the growth_limit_
  // will be set to a lower value. The growth_limit_ is used as the capacity of the alloc_space_,
  // however, capacity normally can't vary. In the case of the growth_limit_ it can be cleared
  // one time by a call to ClearGrowthLimit.
//  size_t growth_limit_;

  friend class collector::MarkSweep;

  DISALLOW_COPY_AND_ASSIGN(DlMallocSpace);
};


class SharableDlMallocSpace : public DlMallocSpace {
 public:
  static const char* ProfiledBenchmarks[];
  static GCSrvSharableDlMallocSpace* AllocateDataMemory();

  SharableDlMallocSpace(const std::string& name, MEM_MAP* mem_map, void* mspace,
      byte* begin, byte* end, size_t growth_limit, bool shareMem = false,
      GCSrvSharableDlMallocSpace* sharable_data = NULL);

//  bool CreateBitmaps(byte* heap_begin, size_t heap_capacity);
//  bool SpaceBitmapInit(accounting::GCSrvceBitmap *hb,
//      const std::string& name, byte* heap_begin, size_t heap_capacity,
//      size_t bitmap_size);
  bool CreateBitmaps(byte* heap_begin, size_t heap_capacity,
        bool shareMem = false);
  bool CreateSharableBitmaps(byte* heap_begin, size_t heap_capacity,
      bool shareMem);
  bool RegisterGlobalCollector(const char* se_name_c_str);

  int GetSpaceIndex(void) {
    return sharable_space_data_->space_index_;
  }

  void SetHeapMeta(DlMallocSpace* old_alloc_space,
      GCSrvceContinuousSpace* image_space_data);



  bool IsSharableAllocSpace() {
    return true;
  }

//  bool HasBitmapsBound(void) {
//    return (bound_mark_bitmaps_ == 1);
//  }
//  void SwapBitmaps();
//  void BindLiveToMarkBitmaps(void);
//  accounting::SPACE_BITMAP* UnBindBitmaps(void);
//  void BindLiveToMarkBitmap(void) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//  void UnBindBitmaps(void) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
//  void BindLiveToMarkBitmaps(void);

  GCSrvSharableDlMallocSpace* sharable_space_data_;
  GCSrvDlMallocSpace* dlmalloc_space_data_;
  volatile int bound_mark_bitmaps_;
  std::vector<std::string> app_list_;
};//class SharableDlMallocSpace




#else
// An alloc space is a space where objects may be allocated and garbage collected.
class DlMallocSpace : public MemMapSpace, public AllocSpace
                      /*public AbstractDLmallocSpace*/ {
 public:
  typedef void(*WalkCallback)(void *start, void *end, size_t num_bytes, void* callback_arg);

  // The boundary tag overhead.
  static const size_t kChunkOverhead = kWordSize;
  //Todo: Fizo: I do not know why I need this?
  static const size_t kAlignment = 8;
  SpaceType GetType() const {
    if (GetGcRetentionPolicy() == kGcRetentionPolicyFullCollect) {
      return kSpaceTypeZygoteSpace;
    } else {
      return kSpaceTypeAllocSpace;
    }
  }


  static size_t BitmapOffsetToIndex(size_t offset) {
    return offset / kAlignment / kBitsPerWord;
  }
  // Create a AllocSpace with the requested sizes. The requested
  // base address is not guaranteed to be granted, if it is required,
  // the caller should call Begin on the returned space to confirm
  // the request was granted.
  static DL_MALLOC_SPACE* Create(const std::string& name, size_t initial_size, size_t growth_limit,
                               size_t capacity, byte* requested_begin, bool shareMem = false);

  // Allocate num_bytes without allowing the underlying mspace to grow.
  virtual mirror::Object* AllocWithGrowth(Thread* self, size_t num_bytes,
                                          size_t* bytes_allocated) LOCKS_EXCLUDED(lock_);

  // Allocate num_bytes allowing the underlying mspace to grow.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated);
  virtual bool RegisterGlobalCollector(const char* se_name_c_str);
  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj);
  virtual size_t Free(Thread* self, mirror::Object* ptr);
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs);

  mirror::Object* AllocNonvirtual(Thread* self, size_t num_bytes, size_t* bytes_allocated);

  size_t AllocationSizeNonvirtual(const mirror::Object* obj) {
    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj))) +
        kChunkOverhead;
  }

  size_t AllocationNoOverhead(const mirror::Object* obj) {
    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj)));
  }

  void AllocationSizes(const mirror::Object* obj, size_t* nonVirtualNoOverhead, size_t* nonVirtual) {
    *nonVirtualNoOverhead = mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj)));
    *nonVirtual = *nonVirtualNoOverhead + kChunkOverhead;
  }

  size_t GCPGetAllocationSize(const mirror::Object*);

  void* MoreCore(intptr_t increment);

  void* GetMspace() const {
    return mspace_;
  }

  // Hands unused pages back to the system.
  size_t Trim();

  // Perform a mspace_inspect_all which calls back for each allocation chunk. The chunk may not be
  // in use, indicated by num_bytes equaling zero.
  void Walk(WalkCallback callback, void* arg) LOCKS_EXCLUDED(lock_);

  // Returns the number of bytes that the space has currently obtained from the system. This is
  // greater or equal to the amount of live data in the space.
  size_t GetFootprint();

  // Returns the number of bytes that the heap is allowed to obtain from the system via MoreCore.
  size_t GetFootprintLimit();

  // Set the maximum number of bytes that the heap is allowed to obtain from the system via
  // MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
  // allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
  void SetFootprintLimit(size_t limit);

  // Removes the fork time growth limit on capacity, allowing the application to allocate up to the
  // maximum reserved size of the heap.
  void ClearGrowthLimit() {
    SetInternalGrowthLimit(NonGrowthLimitCapacity());
  }

  // Override capacity so that we only return the possibly limited capacity
  size_t Capacity() const {
    return growth_limit_;
  }

  // The total amount of memory reserved for the alloc space.
  size_t NonGrowthLimitCapacity() const {
    return GetMemMap()->Size();
  }

  accounting::SPACE_BITMAP* GetLiveBitmap() const {
    return live_bitmap_.get();
  }

  accounting::SPACE_BITMAP* GetMarkBitmap() const {
    return mark_bitmap_.get();
  }

  void Dump(std::ostream& os) const;

  void SetGrowthLimit(size_t growth_limit);

  //fizo, we need to override this
  // Swap the live and mark bitmaps of this space. This is used by the GC for concurrent sweeping.
  void SwapBitmaps();
  virtual accounting::SPACE_BITMAP* UnBindBitmaps(void);
  // Turn ourself into a zygote space and return a new alloc space which has our unused memory.
  DL_MALLOC_SPACE* CreateZygoteSpace(const char* alloc_space_name, bool shareMem = false);

  virtual uint64_t GetBytesAllocated() const {
    return num_bytes_allocated_;
  }

  virtual uint64_t GetObjectsAllocated() const {
    return num_objects_allocated_;
  }

  virtual uint64_t GetTotalBytesAllocated() const {
    return total_bytes_allocated_;
  }

  virtual uint64_t GetTotalObjectsAllocated() const {
    return total_objects_allocated_;
  }

  virtual void UpdateBytesAllocated(int delta) {
    num_bytes_allocated_ += delta;
  }

  virtual void UpdateObjectsAllocated(int delta) {
    num_objects_allocated_ += delta;
  }

  virtual void UpdateTotalBytesAllocated(int delta) {
    total_bytes_allocated_ += delta;
  }

  virtual void UpdateTotalObjectsAllocated(int delta) {
    total_objects_allocated_ += delta;
  }

  // Returns the class of a recently freed object.
  mirror::Class* FindRecentFreedObject(const mirror::Object* obj);
  static void* CreateMallocSpace(void* base, size_t morecore_start, size_t initial_size);


  virtual void SetInternalGrowthLimit(size_t new_growth_limit) {
    growth_limit_ = new_growth_limit;
  }

//  SharedDlMallocSpace* CreateZygoteSpaceWithSharedSpace(const char* alloc_space_name);

  size_t GetRecentFreePos(){
    return recent_free_pos_;
  }


  void SetRecentFreePos(size_t newVal){
    recent_free_pos_ = newVal;
  }

  const mirror::Object* GetObjectRecentFreeObject(int pos) {
    return recent_freed_objects_[pos].first;
  }

  mirror::Class* GetClassRecentFreeObject(int pos){
    return recent_freed_objects_[pos].second;
  }

  void SetRecentFreeObject(mirror::Object* ptr){
    recent_freed_objects_[GetRecentFreePos()].first = ptr;
    recent_freed_objects_[GetRecentFreePos()].second = ptr->GetClass();
  }


  mirror::Object* publicAllocWithoutGrowthLocked(size_t num_bytes, size_t* bytes_allocated) {

    mirror::Object* result = reinterpret_cast<mirror::Object*>(mspace_malloc(GetMspace(), num_bytes));
    if(result != NULL) {
      *bytes_allocated = AllocationSizeNonvirtual(result);
      return result;
    }
    LOG(ERROR) << "publicAllocWithoutGrowthLocked..could not allocate object";
    return NULL;
  }

 protected:
  DlMallocSpace(const std::string& name, MEM_MAP* mem_map, void* mspace,
      byte* begin, byte* end, size_t growth_limit, bool shareMem = false);

 private:
  size_t InternalAllocationSize(const mirror::Object* obj);
  mirror::Object* AllocWithoutGrowthLocked(size_t num_bytes, size_t* bytes_allocated)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  bool Init(size_t initial_size, size_t maximum_size, size_t growth_size, byte* requested_base);
  void RegisterRecentFree(mirror::Object* ptr);


  UniquePtr<accounting::SPACE_BITMAP> live_bitmap_;
  UniquePtr<accounting::SPACE_BITMAP> mark_bitmap_;
  UniquePtr<accounting::SPACE_BITMAP> temp_bitmap_;
  // Recent allocation buffer.
  static constexpr size_t kRecentFreeCount = kDebugSpaces ? (1 << 16) : 0;
  static constexpr size_t kRecentFreeMask = kRecentFreeCount - 1;
  std::pair<const mirror::Object*, mirror::Class*> recent_freed_objects_[kRecentFreeCount];
  size_t recent_free_pos_;

  // Approximate number of bytes which have been allocated into the space.
  uint64_t num_bytes_allocated_;
  uint64_t num_objects_allocated_;
  uint64_t total_bytes_allocated_;
  uint64_t total_objects_allocated_;

  static size_t bitmap_index_;



  // Used to ensure mutual exclusion when the allocation spaces data structures are being modified.
  Mutex lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  // Underlying malloc space
  void* const mspace_;

  // The capacity of the alloc space until such time that ClearGrowthLimit is called.
  // The underlying mem_map_ controls the maximum size we allow the heap to grow to. The growth
  // limit is a value <= to the mem_map_ capacity used for ergonomic reasons because of the zygote.
  // Prior to forking the zygote the heap will have a maximally sized mem_map_ but the growth_limit_
  // will be set to a lower value. The growth_limit_ is used as the capacity of the alloc_space_,
  // however, capacity normally can't vary. In the case of the growth_limit_ it can be cleared
  // one time by a call to ClearGrowthLimit.
  size_t growth_limit_;

  friend class collector::MarkSweep;

  DISALLOW_COPY_AND_ASSIGN(DlMallocSpace);
};


#endif
}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_DLMALLOC_SPACE_H_
