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

#ifndef ART_RUNTIME_GC_SPACE_SPACE_H_
#define ART_RUNTIME_GC_SPACE_SPACE_H_

#include <string>
#include "gc/allocator/dlmalloc.h"
#include "UniquePtr.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "gc/accounting/space_bitmap.h"
#include "gc/collector/gc_type.h"
#include "globals.h"
#include "image.h"
#include "mem_map.h"


#define MARKSWEEP_COLLECTORS_ARRAY_CAPACITY   6


#if (true || ART_GC_SERVICE)
#define DL_MALLOC_SPACE  DlMallocSpace
#ifndef DLMALLOC_SPACE_T
#define DLMALLOC_SPACE_T DlMallocSpace
//AbstractDLmallocSpace
#endif
#ifndef ABSTRACT_CONTINUOUS_SPACE_T
#define ABSTRACT_CONTINUOUS_SPACE_T ContinuousSpace
//AbstractContinuousSpace
#endif
#define CONTINUOUS_SPACE_T ContinuousSpace
#else
#define DL_MALLOC_SPACE  DlMallocSpace
#define CONTINUOUS_SPACE_T ContinuousSpace
#endif

namespace art {
namespace mirror {
  class Object;
}  // namespace mirror

namespace gc {

namespace accounting {
  class BaseBitmap;
  class SpaceBitmap;
}  // namespace accounting

class Heap;

namespace space {

class DL_MALLOC_SPACE;
class ImageSpace;
class LargeObjectSpace;
class ABSTRACT_CONTINUOUS_SPACE_T;
class DLMALLOC_SPACE_T;

static constexpr bool kDebugSpaces = kIsDebugBuild;
static constexpr size_t kRecentFreeCount = kDebugSpaces ? (1 << 16) : 0;
static constexpr size_t kRecentFreeMask = kRecentFreeCount - 1;
// See Space::GetGcRetentionPolicy.
enum GcRetentionPolicy {
  // Objects are retained forever with this policy for a space.
  kGcRetentionPolicyNeverCollect,
  // Every GC cycle will attempt to collect objects in this space.
  kGcRetentionPolicyAlwaysCollect,
  // Objects will be considered for collection only in "full" GC cycles, ie faster partial
  // collections won't scan these areas such as the Zygote.
  kGcRetentionPolicyFullCollect,
};
std::ostream& operator<<(std::ostream& os, const GcRetentionPolicy& policy);

enum SpaceType {
  kSpaceTypeImageSpace,
  kSpaceTypeAllocSpace,
  kSpaceTypeZygoteSpace,
  kSpaceTypeLargeObjectSpace,
};
std::ostream& operator<<(std::ostream& os, const SpaceType& space_type);


typedef struct GCSrvceSpace_S {
  char name_[MEM_MAP_NAME_LENGTH];
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



typedef struct GCSrvDlMallocSpace_S {
  GCSrvceContinuousSpace cont_space_data_;
  /* allocated space memory */
  AShmemMap memory_;
  std::pair<const mirror::Object*, mirror::Class*>
                    recent_freed_objects_[kRecentFreeCount];
  size_t recent_free_pos_;
  // Approximate number of bytes which have been allocated into the space.
  size_t num_bytes_allocated_;
  size_t num_objects_allocated_;
  size_t total_bytes_allocated_;
  size_t total_objects_allocated_;

  static size_t bitmap_index_;

  // Underlying malloc space
  void* mspace_;

  // The capacity of the alloc space until such time that ClearGrowthLimit is called.
  // The underlying mem_map_ controls the maximum size we allow the heap to grow to. The growth
  // limit is a value <= to the mem_map_ capacity used for ergonomic reasons because of the zygote.
  // Prior to forking the zygote the heap will have a maximally sized mem_map_ but the growth_limit_
  // will be set to a lower value. The growth_limit_ is used as the capacity of the alloc_space_,
  // however, capacity normally can't vary. In the case of the growth_limit_ it can be cleared
  // one time by a call to ClearGrowthLimit.
  size_t growth_limit_;


  // Used to ensure mutual exclusion when the allocation spaces data structures are being modified.
  BaseMutex* lock_ ;//DEFAULT_MUTEX_ACQUIRED_AFTER;


}__attribute__((aligned(8))) GCSrvDlMallocSpace;


typedef enum {
  IPC_GC_PHASE_NONE = 0,
  IPC_GC_PHASE_PRE_INIT,
  IPC_GC_PHASE_INIT,
  IPC_GC_PHASE_ROOT_MARK,
  IPC_GC_PHASE_ROOT_CONC_MARK,
  IPC_GC_PHASE_MARK_REACHABLES,
  IPC_GC_PHASE_MARK_RECURSIVE,
  IPC_GC_PHASE_PRE_CONC_ROOT_MARK,
  IPC_GC_PHASE_CONC_MARK,
  IPC_GC_PHASE_RECLAIM,
  IPC_GC_PHASE_FINISH,
  IPC_GC_PHASE_POST_FINISH,
  IPC_GC_PHASE_MAX
} IPC_GC_PHASE_ENUM;


typedef struct GCSrvceCashedReferences_S {
  // Immune range, every object inside the immune range is assumed to be marked.
  mirror::Object* immune_begin_;
  mirror::Object* immune_end_;


  mirror::Object* soft_reference_list_;
  mirror::Object* weak_reference_list_;
  mirror::Object* finalizer_reference_list_;
  mirror::Object* phantom_reference_list_;
  mirror::Object* cleared_reference_list_;

  // Cache java.lang.Class for optimization.
  mirror::Class* java_lang_Class_;
} __attribute__((aligned(8))) GCSrvceCashedReferences;

typedef struct GCSrvceCollectorTimeStats_S {
  uint64_t total_time_ns_;
  uint64_t total_paused_time_ns_;
  uint64_t total_freed_objects_;
  uint64_t total_freed_bytes_;
} __attribute__((aligned(8))) GCSrvceCollectorTimeStats;

typedef struct GCSrvceCashedStatsCounters_S {
  // Number of non large object bytes freed in this collection.
  volatile int32_t freed_bytes_;
  // Number of large object bytes freed.
  volatile int32_t freed_large_object_bytes_;
  // Number of objects freed in this collection.
  volatile int32_t freed_objects_;
  // Number of freed large objects.
  volatile int32_t freed_large_objects_;


  volatile int32_t array_count_;
  volatile int32_t class_count_;
  volatile int32_t other_count_;
  volatile int32_t reference_count_;
  volatile int32_t cards_scanned_;


  GCSrvceCollectorTimeStats total_stats_;
}__attribute__((aligned(8))) GCSrvceCashedStatsCounters;

typedef struct GCSrvceCashedReferenceOffsets_S {
  size_t reference_referent_offset_;
  size_t reference_queue_offset_;
  size_t reference_queueNext_offset_;
  size_t reference_pendingNext_offset_;
  size_t finalizer_reference_zombie_offset_;
}__attribute__((aligned(8))) GCSrvceCashedReferenceOffsets;



typedef struct GCSrvSharableCollectorData_S {
  GCSrvceCashedReferences cashed_references_;
  GCSrvceCashedStatsCounters cashed_stats_;
  volatile IPC_GC_PHASE_ENUM gc_phase_;

  accounting::GCSrvceBitmap* volatile current_mark_bitmap_;

  int is_concurrent_;
} __attribute__((aligned(8))) GCSrvSharableCollectorData;

// this struct is used to capture metadata of the zygote space
// during the sharing process.
typedef struct GCSrvcZygoteResharingRec_S{
  /* allocated space memory for zygote*/
  AShmemMap zygote_space_;
  accounting::GCSrvceBitmap live_bitmap_;
  accounting::GCSrvceBitmap mark_bitmap_;
} __attribute__((aligned(8))) GCSrvcZygoteResharingRec;



typedef struct GCSrvcHeapSubRecord_S {
  // Since the heap was created, how many bytes have been freed.
  size_t total_bytes_freed_ever_;

  // Since the heap was created, how many objects have been freed.
  size_t total_objects_freed_ever_;

  // The last time a heap trim occurred.
  uint64_t last_trim_time_ms_;

  // The nanosecond time at which the last GC ended.
  uint64_t last_gc_time_ns_;

  // How many bytes were allocated at the end of the last GC.
  uint64_t last_gc_size_;

  // Estimated allocation rate (bytes / second). Computed between the time of the last GC cycle
  // and the start of the current one.
  uint64_t allocation_rate_;
} __attribute__((aligned(8))) GCSrvcHeapSubRecord;






typedef struct GCSrvSharableHeapData_S {
  /* gc barrier */
  SynchronizedLockHead gc_barrier_lock_;
  /* GC synchronization locks used for phases*/
  SynchronizedLockHead phase_lock_;
  /* GC concurrent signals */
  // Guards access to the state of GC, associated conditional variable is used to signal when a GC
  // completes.
  SynchronizedLockHead conc_lock_;
  // Guards access to the state of GC, associated conditional variable is used to signal when a GC
  // completes.
  SynchronizedLockHead gc_complete_lock_;

  /* allocated space memory for zygote*/
  GCSrvcZygoteResharingRec reshared_zygote_;
 // AShmemMap zygote_space_;

  accounting::GCSrvceSharedHeapBitmap live_heap_bitmap_data_;
  accounting::GCSrvceSharedHeapBitmap mark_heap_bitmap_data_;

  StructuredObjectStackData live_stack_data_;
  StructuredObjectStackData mark_stack_data_;
  StructuredObjectStackData alloc_stack_data_;

  GCSrvcHeapSubRecord sub_record_meta_;

  GCSrvSharableCollectorData collectors_[MARKSWEEP_COLLECTORS_ARRAY_CAPACITY];

  GCSrvceCashedReferenceOffsets reference_offsets_;

  byte* const image_space_begin_;
  byte* const image_space_end_;
  byte* const zygote_begin_;
  byte* const zygote_end_;


  /************ collectors array ******/
//  GCSrvSharableCollectorData collectors_[6];
  volatile int collect_index_;
  GCSrvSharableCollectorData* volatile current_collector_;
  /****** variables from original Marksweep members *****/


//  volatile IPC_GC_PHASE_ENUM gc_phase_;

  volatile int barrier_count_;

  // used to signal the gc daemon. gurded by conc_lock_
  volatile int conc_flag_;
  volatile int gc_type_;

  /* collection stats */
//  volatile int32_t freed_objects_;
//  volatile int32_t freed_bytes_;
  volatile int32_t conc_count_;
  volatile int32_t explicit_count_;

  /****** variables from original heap members *****/
  // Total time which mutators are paused or waiting for GC to complete.
  uint64_t total_wait_time_;
  // What kind of concurrency behavior is the runtime after? True for concurrent mark sweep GC,
  // false for stop-the-world mark sweep.
  int concurrent_gc_;
  // True while the garbage collector is running. guarded by gc_complete_lock_
  volatile int is_gc_running_;
//  // Guards access to the state of GC, associated conditional variable is used to signal when a GC
//  // completes.
//  volatile int is_gc_complete_;
  // Since the heap was created, how many bytes have been freed.
//  size_t total_bytes_freed_ever_;
//
//  // Since the heap was created, how many objects have been freed.
//  size_t total_objects_freed_ever_;

  // Last Gc type we ran. Used by WaitForConcurrentGc to know which Gc was waited on.
  //guarded by (gc_complete_lock_);
  volatile gc::collector::GcType last_gc_type_;
  gc::collector::GcType next_gc_type_;
  // When num_bytes_allocated_ exceeds this amount then a concurrent GC should be requested so that
  // it completes ahead of an allocation failing.
  size_t concurrent_start_bytes_;

  /*
   * process state of the application. this helps to know the priority  of the
   * app and apply the the trimming with minimum pause overheads.
   */
  volatile int process_state_;




} __attribute__((aligned(8))) GCSrvSharableHeapData;

typedef struct GCSrvSharableDlMallocSpace_S {

  GCSrvDlMallocSpace dlmalloc_space_data_;

  SynchronizedLockHead ip_lock_;

  accounting::GCSrvceBitmap live_bitmap_;
  accounting::GCSrvceBitmap mark_bitmap_;
  accounting::GCSrvceBitmap temp_bitmap_;

  CardBaseTableFields card_table_data_;



  /* heap data */
  GCSrvSharableHeapData heap_meta_;

  InterProcessConditionVariable* cond_;

  volatile int register_gc_;

  volatile int space_index_;
  /* allocated space memory */
//  AShmemMap test_memory_;


}__attribute__((aligned(8))) GCSrvSharableDlMallocSpace;



#if (true || ART_GC_SERVICE)


// A space contains memory allocated for managed objects.
class Space {
 public:
  // Dump space. Also key method for C++ vtables.
  virtual void Dump(std::ostream& os) const;

  // Name of the space. May vary, for example before/after the Zygote fork.
  const char* GetName() const {
    return space_data_->name_;
  }

  // The policy of when objects are collected associated with this space.
  GcRetentionPolicy GetGcRetentionPolicy() const {
    return space_data_->gc_retention_policy_;
  }


  // Does the space support allocation?
  virtual bool CanAllocateInto() const {
    return true;
  }

  // Is the given object contained within this space?
  virtual bool Contains(const mirror::Object* obj) const = 0;

//  // get the allocated memory for that object?
//  virtual size_t GetObjectSize(const mirror::Object* obj) const = 0;

  // The kind of space this: image, alloc, zygote, large object.
  virtual SpaceType GetType() const = 0;

  // Is this an image space, ie one backed by a memory mapped image file.
  bool IsImageSpace() const {
    return GetType() == kSpaceTypeImageSpace;
  }
  ImageSpace* AsImageSpace();

  // Is this a dlmalloc backed allocation space?
  bool IsDlMallocSpace() const {
    SpaceType type = GetType();
    return type == kSpaceTypeAllocSpace || type == kSpaceTypeZygoteSpace;
  }
  DL_MALLOC_SPACE* AsDlMallocSpace();

  // Is this the space allocated into by the Zygote and no-longer in use?
  bool IsZygoteSpace() const {
    return GetType() == kSpaceTypeZygoteSpace;
  }

  // Does this space hold large objects and implement the large object space abstraction?
  bool IsLargeObjectSpace() const {
    return GetType() == kSpaceTypeLargeObjectSpace;
  }

  virtual bool IsSharableAllocSpace() {
    return false;
  }
  LargeObjectSpace* AsLargeObjectSpace();

  virtual ~Space() {}

  void SetGcRetentionPolicy(GcRetentionPolicy gc_retention_policy) {
    space_data_->gc_retention_policy_ = gc_retention_policy;
  }
  // Return the storage space required by obj.
  virtual size_t GCPGetAllocationSize(const mirror::Object*){return 0;}

  static GCSrvceSpace* AllocateSpaceData() {
    return reinterpret_cast<GCSrvceSpace*>(calloc(1,
        SERVICE_ALLOC_ALIGN_BYTE(GCSrvceSpace)));
  }
  GCSrvceSpace* space_data_;
 protected:
  Space(const std::string& name, GcRetentionPolicy gc_retention_policy,
      GCSrvceSpace* memory_alloc = NULL);

  DISALLOW_COPY_AND_ASSIGN(Space);
};
std::ostream& operator<<(std::ostream& os, const Space& space);


// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
class ContinuousSpace : public Space {
 public:

  // Address at which the space begins
  byte* Begin() const {
    return cont_space_data_->begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  byte* End() const {
    return cont_space_data_->end_;
  }

  // Current size of space
  size_t Size() const {
    return End() - Begin();
  }

  void SetEnd(byte* new_end)  {
    cont_space_data_->end_ = new_end;
  }


  virtual accounting::SPACE_BITMAP* GetLiveBitmap() const = 0;
  virtual accounting::SPACE_BITMAP* GetMarkBitmap() const = 0;

  virtual bool HasBitmapsBound(void) {
    return (GetLiveBitmap() == GetMarkBitmap());
  }

  // Is object within this space? We check to see if the pointer is beyond the end first as
  // continuous spaces are iterated over from low to high.
  bool HasAddress(const mirror::Object* obj) const {
    const byte* byte_ptr = reinterpret_cast<const byte*>(obj);
    return byte_ptr < End() && byte_ptr >= Begin();
  }

  bool Contains(const mirror::Object* obj) const {
    return HasAddress(obj);
  }

  virtual ~ContinuousSpace() {}

  GCSrvceContinuousSpace* cont_space_data_;

  static GCSrvceContinuousSpace* AllocateContSpaceMemory() {
    LOG(ERROR) << "Allocating Memory GCSrvceContinuousSpace";
    return reinterpret_cast<GCSrvceContinuousSpace*>(calloc(1,
        SERVICE_ALLOC_ALIGN_BYTE(GCSrvceContinuousSpace)));
  }
 protected:
  ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
                  byte* begin, byte* end,
                  GCSrvceContinuousSpace* cont_space_data = NULL);
 private:
  DISALLOW_COPY_AND_ASSIGN(ContinuousSpace);
};


class MemMapSpace : public ContinuousSpace {
 public:
  // Maximum which the mapped space can grow to.
  virtual size_t Capacity() const {
    return mem_map_->Size();
  }

  // Size of the space without a limit on its growth. By default this is just the Capacity, but
  // for the allocation space we support starting with a small heap and then extending it.
  virtual size_t NonGrowthLimitCapacity() const {
    return Capacity();
  }

 protected:
  MemMapSpace(const std::string& name, MEM_MAP* mem_map, size_t initial_size,
              GcRetentionPolicy gc_retention_policy,
              GCSrvceContinuousSpace* cont_space_data = NULL);

  MEM_MAP* GetMemMap() {
    return mem_map_.get();
  }

  const MEM_MAP* GetMemMap() const {
    return mem_map_.get();
  }

 private:
  // Underlying storage of the space
  UniquePtr<MEM_MAP> mem_map_;

  DISALLOW_COPY_AND_ASSIGN(MemMapSpace);
};

















//// A space contains memory allocated for managed objects.
//class InterfaceSpace {
// public:
//  // Dump space. Also key method for C++ vtables.
//  virtual void Dump(std::ostream& os) const = 0;
//
//
//  virtual GcRetentionPolicy GetGcRetentionPolicy() const = 0;
//
//  // Does the space support allocation?
//  virtual bool CanAllocateInto() const {
//    return true;
//  }
//
//
//  // Is the given object contained within this space?
//  virtual bool Contains(const mirror::Object* obj) const = 0;
//
//
//  // The kind of space this: image, alloc, zygote, large object.
//  virtual SpaceType GetType() const = 0;
//
//
//  // Is this an image space, ie one backed by a memory mapped image file.
//  virtual bool IsImageSpace() const {
//    return GetType() == kSpaceTypeImageSpace;
//  }
//
//  virtual ImageSpace* AsImageSpace() = 0;
//
//  // Is this a dlmalloc backed allocation space?
//  virtual bool IsDlMallocSpace() const {
//    SpaceType type = GetType();
//    return type == kSpaceTypeAllocSpace || type == kSpaceTypeZygoteSpace;
//  }
//
//
//  virtual DL_MALLOC_SPACE* AsDlMallocSpace() = 0;
//
//
//  // Is this the space allocated into by the Zygote and no-longer in use?
//  virtual bool IsZygoteSpace() const {
//    return GetType() == kSpaceTypeZygoteSpace;
//  }
//
//  // Does this space hold large objects and implement the large object space abstraction?
//  virtual bool IsLargeObjectSpace() const {
//    return GetType() == kSpaceTypeLargeObjectSpace;
//  }
//  virtual LargeObjectSpace* AsLargeObjectSpace() = 0;
//
//
//  // Return the storage space required by obj.
//  virtual size_t GCPGetAllocationSize(const mirror::Object*){return 0;}
//
//
// protected:
//  InterfaceSpace() {}
//  virtual ~InterfaceSpace() {}
//
// private:
//  DISALLOW_COPY_AND_ASSIGN(InterfaceSpace);
//};


//// A space contains memory allocated for managed objects.
//class Space : public InterfaceSpace {
// public:
//  // Dump space. Also key method for C++ vtables.
//  virtual void Dump(std::ostream& os) const;
//
//  // Name of the space. May vary, for example before/after the Zygote fork.
//  const char* GetName() const {
//    return name_.c_str();
//  }
//
//  // The policy of when objects are collected associated with this space.
//  virtual GcRetentionPolicy GetGcRetentionPolicy() const {
//    return gc_retention_policy_;
//  }
//
//
//  // Is the given object contained within this space?
//  //virtual bool Contains(const mirror::Object* obj) const = 0;
//
////  // get the allocated memory for that object?
////  virtual size_t GetObjectSize(const mirror::Object* obj) const = 0;
//
//  // The kind of space this: image, alloc, zygote, large object.
//  virtual SpaceType GetType() const = 0;
//
//  ImageSpace* AsImageSpace();
//  DL_MALLOC_SPACE* AsDlMallocSpace();
//  LargeObjectSpace* AsLargeObjectSpace();
//
//  virtual ~Space() {}
//
//
//  void SetGcRetentionPolicy(GcRetentionPolicy gc_retention_policy) {
//    gc_retention_policy_ = gc_retention_policy;
//  }
//
// protected:
//  Space(const std::string& name, GcRetentionPolicy gc_retention_policy);
//
//
//
//  // Name of the space that may vary due to the Zygote fork.
//  std::string name_;
//
// private:
//  // When should objects within this space be reclaimed? Not constant as we vary it in the case
//  // of Zygote forking.
//  GcRetentionPolicy gc_retention_policy_;
//
////  friend class art::gc::Heap;
//
//  DISALLOW_COPY_AND_ASSIGN(Space);
//};
//std::ostream& operator<<(std::ostream& os, const Space& space);


// AllocSpace interface.
class AllocSpace {
 public:
  // Number of bytes currently allocated.
  virtual uint64_t GetBytesAllocated() const = 0;
  // Number of objects currently allocated.
  virtual uint64_t GetObjectsAllocated() const = 0;
  // Number of bytes allocated since the space was created.
  virtual uint64_t GetTotalBytesAllocated() const = 0;
  // Number of objects allocated since the space was created.
  virtual uint64_t GetTotalObjectsAllocated() const = 0;

  virtual void UpdateBytesAllocated(int) = 0;

  virtual void UpdateObjectsAllocated(int) = 0;

  virtual void UpdateTotalBytesAllocated(int) = 0;

  virtual void UpdateTotalObjectsAllocated(int) = 0;
  // Allocate num_bytes without allowing growth. If the allocation
  // succeeds, the output parameter bytes_allocated will be set to the
  // actually allocated bytes which is >= num_bytes.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) = 0;

  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj) = 0;

  // Returns how many bytes were freed.
  virtual size_t Free(Thread* self, mirror::Object* ptr) = 0;

  // Returns how many bytes were freed.
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) = 0;

 protected:
  AllocSpace() {}
  virtual ~AllocSpace() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AllocSpace);
};

//// Continuous spaces have bitmaps, and an address range. Although not required, objects within
//// continuous spaces can be marked in the card table.
//class IContinuousSpace {
// public:
//  // Address at which the space begins
//  virtual byte* Begin() const = 0;
//  virtual byte* End() const = 0;
//  virtual void SetEnd(byte*)  = 0;
//  // Current size of space
//  virtual size_t Size() const {
//    return End() - Begin();
//  }
//
//  virtual accounting::SPACE_BITMAP* GetLiveBitmap() const = 0;
//  virtual accounting::SPACE_BITMAP* GetMarkBitmap() const = 0;
//
//  // Is object within this space? We check to see if the pointer is beyond the end first as
//  // continuous spaces are iterated over from low to high.
//  bool HasAddress(const mirror::Object* obj) const {
//    const byte* byte_ptr = reinterpret_cast<const byte*>(obj);
//    return byte_ptr < End() && byte_ptr >= Begin();
//  }
//
//  ABSTRACT_CONTINUOUS_SPACE_T* AsAbstractContSpace() {
//    return down_cast<ABSTRACT_CONTINUOUS_SPACE_T*>(down_cast<ABSTRACT_CONTINUOUS_SPACE_T*>(this));
//  }
//  virtual ~IContinuousSpace() {}
//  IContinuousSpace(){}
//};


//class AbstractContinuousSpace : public IContinuousSpace,
//  public InterfaceSpace {
// public:
//   AbstractDLmallocSpace* AsAbstractDlMallocSpace(){
//     return down_cast<AbstractDLmallocSpace*>(down_cast<IContinuousSpace*>(this));
//   }
//   AbstractContinuousSpace(){}
//   virtual ~AbstractContinuousSpace(){}
//};
// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
//class ContinuousSpace : public IContinuousSpace, public Space {
// public:
//
//  byte* Begin() const {
//    return begin_;
//  }
//
//  // Address at which the space ends, which may vary as the space is filled.
//  byte* End() const {
//    return end_;
//  }
//
//  // Current size of space
//  size_t Size() const {
//    return End() - Begin();
//  }
//
//  void SetEnd(byte* new_end)  {
//    end_ = new_end;
//  }
//
//  bool Contains(const mirror::Object* obj) const {
//    return HasAddress(obj);
//  }
//
//  virtual ~ContinuousSpace() {}
//
// protected:
//  ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
//                  byte* begin, byte* end) :
//      Space(name, gc_retention_policy), begin_(begin), end_(end) {
//  }
//
//  // The beginning of the storage for fast access.
//  byte* const begin_;
//
//  // Current end of the space.
//  byte* end_;
//
// private:
//  DISALLOW_COPY_AND_ASSIGN(ContinuousSpace);
//};


// A space where objects may be allocated higgledy-piggledy throughout virtual memory. Currently
// the card table can't cover these objects and so the write barrier shouldn't be triggered. This
// is suitable for use for large primitive arrays.
class DiscontinuousSpace : public Space {
 public:
  accounting::SpaceSetMap* GetLiveObjects() const {
    return live_objects_.get();
  }

  accounting::SpaceSetMap* GetMarkObjects() const {
    return mark_objects_.get();
  }

  virtual ~DiscontinuousSpace() {}

 protected:
  DiscontinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy);

  UniquePtr<accounting::SpaceSetMap> live_objects_;
  UniquePtr<accounting::SpaceSetMap> mark_objects_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscontinuousSpace);
};


//class IMemMapSpace /*: public IContinuousSpace*/ {
// public:
//  // Maximum which the mapped space can grow to.
//  virtual size_t Capacity() const  = 0;
//
//  // Size of the space without a limit on its growth. By default this is just the Capacity, but
//  // for the allocation space we support starting with a small heap and then extending it.
//  virtual size_t NonGrowthLimitCapacity() const = 0;
//
//  virtual MEM_MAP* GetMemMap() = 0;
//
//  virtual ~IMemMapSpace(){}
//  IMemMapSpace(){}
// private:
//  DISALLOW_COPY_AND_ASSIGN(IMemMapSpace);
//};




//class MemMapSpace : public IMemMapSpace, public ContinuousSpace {
// public:
//  // Maximum which the mapped space can grow to.
//  virtual size_t Capacity() const {
//    return mem_map_->Size();
//  }
//
//  // Size of the space without a limit on its growth. By default this is just the Capacity, but
//  // for the allocation space we support starting with a small heap and then extending it.
//  virtual size_t NonGrowthLimitCapacity() const {
//    return Capacity();
//  }
//
// protected:
//  MemMapSpace(const std::string& name, MEM_MAP* mem_map, size_t initial_size,
//              GcRetentionPolicy gc_retention_policy)
//      : ContinuousSpace(name, gc_retention_policy,
//                        mem_map->Begin(), mem_map->Begin() + initial_size),
//        mem_map_(mem_map) {
//  }
//
//  MEM_MAP* GetMemMap() {
//    return mem_map_.get();
//  }
//
//  const MEM_MAP* GetMemMap() const {
//    return mem_map_.get();
//  }
//
// private:
//  // Underlying storage of the space
//  UniquePtr<MEM_MAP> mem_map_;
//
//  DISALLOW_COPY_AND_ASSIGN(MemMapSpace);
//};


class IDlMallocSpace : public AllocSpace {
 public:
  typedef void(*WalkCallback)(void *start, void *end, size_t num_bytes, void* callback_arg);
  // Create a AllocSpace with the requested sizes. The requested
  // base address is not guaranteed to be granted, if it is required,
  // the caller should call Begin on the returned space to confirm
  // the request was granted.
  static IDlMallocSpace* CreateDlMallocSpace(const std::string& name,
      size_t initial_size, size_t growth_limit,
      size_t capacity, byte* requested_begin, bool shareMem = false);


  virtual void SwapBitmaps() = 0;
  virtual void SetInternalGrowthLimit(size_t) = 0;

  // Set the maximum number of bytes that the heap is allowed to obtain from the system via
  // MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
  // allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
  virtual void SetFootprintLimit(size_t limit) = 0;

  virtual void* GetMspace() const = 0;

  virtual void SetGrowthLimit(size_t growth_limit) = 0;


  virtual void* MoreCore(intptr_t increment) = 0;

  // Hands unused pages back to the system.
  virtual size_t Trim() = 0;

  // Returns the number of bytes that the space has currently obtained from the system. This is
  // greater or equal to the amount of live data in the space.
  virtual size_t GetFootprint() = 0;

  // Returns the number of bytes that the heap is allowed to obtain from the system via MoreCore.
  size_t GetFootprintLimit();

  size_t AllocationNoOverhead(const mirror::Object* obj) {
    return mspace_usable_size(const_cast<void*>(reinterpret_cast<const void*>(obj)));
  }

  //virtual AbstractContinuousSpace* AsAbstractContinuousSpace() = 0;

 protected:
  IDlMallocSpace(){}
  virtual ~IDlMallocSpace(){}
 private:

};//IDlMallocSpace


//class AbstractDLmallocSpace: public IMemMapSpace, public IContinuousSpace,
//    public  InterfaceSpace, public IDlMallocSpace {
//public:
//
//
//protected:
//  virtual ~AbstractDLmallocSpace() {}
//  AbstractDLmallocSpace(){}
//  //virtual ~AbstractDLmallocSpace() {}
//};
//
class SharableSpace {

};

#else


// A space contains memory allocated for managed objects.
class Space {
 public:
  // Dump space. Also key method for C++ vtables.
  virtual void Dump(std::ostream& os) const;

  // Name of the space. May vary, for example before/after the Zygote fork.
  const char* GetName() const {
    return name_.c_str();
  }

#if (true || ART_GC_SERVICE)
  // The policy of when objects are collected associated with this space.
  GcRetentionPolicy GetGcRetentionPolicy() const {
    return gc_retention_policy_;
  }
#else
  // The policy of when objects are collected associated with this space.
  virtual GcRetentionPolicy GetGcRetentionPolicy() const {
    return gc_retention_policy_;
  }
#endif

  // Does the space support allocation?
  virtual bool CanAllocateInto() const {
    return true;
  }

  // Is the given object contained within this space?
  virtual bool Contains(const mirror::Object* obj) const = 0;

//  // get the allocated memory for that object?
//  virtual size_t GetObjectSize(const mirror::Object* obj) const = 0;

  // The kind of space this: image, alloc, zygote, large object.
  virtual SpaceType GetType() const = 0;

  // Is this an image space, ie one backed by a memory mapped image file.
  bool IsImageSpace() const {
    return GetType() == kSpaceTypeImageSpace;
  }
  ImageSpace* AsImageSpace();

  // Is this a dlmalloc backed allocation space?
  bool IsDlMallocSpace() const {
    SpaceType type = GetType();
    return type == kSpaceTypeAllocSpace || type == kSpaceTypeZygoteSpace;
  }
  DL_MALLOC_SPACE* AsDlMallocSpace();

  // Is this the space allocated into by the Zygote and no-longer in use?
  bool IsZygoteSpace() const {
    return GetType() == kSpaceTypeZygoteSpace;
  }

  // Does this space hold large objects and implement the large object space abstraction?
  bool IsLargeObjectSpace() const {
    return GetType() == kSpaceTypeLargeObjectSpace;
  }
  LargeObjectSpace* AsLargeObjectSpace();

  virtual ~Space() {}


  // Return the storage space required by obj.
  virtual size_t GCPGetAllocationSize(const mirror::Object*){return 0;}
 protected:
  Space(const std::string& name, GcRetentionPolicy gc_retention_policy);

  void SetGcRetentionPolicy(GcRetentionPolicy gc_retention_policy) {
    gc_retention_policy_ = gc_retention_policy;
  }

  // Name of the space that may vary due to the Zygote fork.
  std::string name_;

 private:
  // When should objects within this space be reclaimed? Not constant as we vary it in the case
  // of Zygote forking.
  GcRetentionPolicy gc_retention_policy_;

  friend class art::gc::Heap;

  DISALLOW_COPY_AND_ASSIGN(Space);
};
std::ostream& operator<<(std::ostream& os, const Space& space);

// AllocSpace interface.
class AllocSpace {
 public:
  // Number of bytes currently allocated.
  virtual uint64_t GetBytesAllocated() const = 0;
  // Number of objects currently allocated.
  virtual uint64_t GetObjectsAllocated() const = 0;
  // Number of bytes allocated since the space was created.
  virtual uint64_t GetTotalBytesAllocated() const = 0;
  // Number of objects allocated since the space was created.
  virtual uint64_t GetTotalObjectsAllocated() const = 0;

  virtual void UpdateBytesAllocated(int) = 0;

  virtual void UpdateObjectsAllocated(int) = 0;

  virtual void UpdateTotalBytesAllocated(int) = 0;

  virtual void UpdateTotalObjectsAllocated(int) = 0;
  // Allocate num_bytes without allowing growth. If the allocation
  // succeeds, the output parameter bytes_allocated will be set to the
  // actually allocated bytes which is >= num_bytes.
  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) = 0;

  // Return the storage space required by obj.
  virtual size_t AllocationSize(const mirror::Object* obj) = 0;

  // Returns how many bytes were freed.
  virtual size_t Free(Thread* self, mirror::Object* ptr) = 0;

  // Returns how many bytes were freed.
  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) = 0;

 protected:
  AllocSpace() {}
  virtual ~AllocSpace() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(AllocSpace);
};

// Continuous spaces have bitmaps, and an address range. Although not required, objects within
// continuous spaces can be marked in the card table.
class ContinuousSpace : public Space {
 public:
  // Address at which the space begins

#if (true || ART_GC_SERVICE)
  virtual byte* Begin() const {
    return begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  virtual byte* End() const {
    return end_;
  }


  virtual void SetEnd(byte* new_end)  {
    end_ = new_end;
  }

  // Current size of space
  virtual size_t Size() const {
    return End() - Begin();
  }
#else
  byte* Begin() const {
    return begin_;
  }

  // Address at which the space ends, which may vary as the space is filled.
  byte* End() const {
    return end_;
  }

  // Current size of space
  size_t Size() const {
    return End() - Begin();
  }

  void SetEnd(byte* new_end)  {
    end_ = new_end;
  }
#endif

  virtual accounting::SPACE_BITMAP* GetLiveBitmap() const = 0;
  virtual accounting::SPACE_BITMAP* GetMarkBitmap() const = 0;

  // Is object within this space? We check to see if the pointer is beyond the end first as
  // continuous spaces are iterated over from low to high.
  bool HasAddress(const mirror::Object* obj) const {
    const byte* byte_ptr = reinterpret_cast<const byte*>(obj);
    return byte_ptr < End() && byte_ptr >= Begin();
  }

  bool Contains(const mirror::Object* obj) const {
    return HasAddress(obj);
  }

  virtual ~ContinuousSpace() {}

 protected:
  ContinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy,
                  byte* begin, byte* end) :
      Space(name, gc_retention_policy), begin_(begin), end_(end) {
  }


  // The beginning of the storage for fast access.
  byte* const begin_;

  // Current end of the space.
  byte* end_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ContinuousSpace);
};

// A space where objects may be allocated higgledy-piggledy throughout virtual memory. Currently
// the card table can't cover these objects and so the write barrier shouldn't be triggered. This
// is suitable for use for large primitive arrays.
class DiscontinuousSpace : public Space {
 public:
  accounting::SpaceSetMap* GetLiveObjects() const {
    return live_objects_.get();
  }

  accounting::SpaceSetMap* GetMarkObjects() const {
    return mark_objects_.get();
  }

  virtual ~DiscontinuousSpace() {}

 protected:
  DiscontinuousSpace(const std::string& name, GcRetentionPolicy gc_retention_policy);

  UniquePtr<accounting::SpaceSetMap> live_objects_;
  UniquePtr<accounting::SpaceSetMap> mark_objects_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DiscontinuousSpace);
};

class MemMapSpace : public ContinuousSpace {
 public:
  // Maximum which the mapped space can grow to.
  virtual size_t Capacity() const {
    return mem_map_->Size();
  }

  // Size of the space without a limit on its growth. By default this is just the Capacity, but
  // for the allocation space we support starting with a small heap and then extending it.
  virtual size_t NonGrowthLimitCapacity() const {
    return Capacity();
  }

 protected:
  MemMapSpace(const std::string& name, MEM_MAP* mem_map, size_t initial_size,
              GcRetentionPolicy gc_retention_policy)
      : ContinuousSpace(name, gc_retention_policy,
                        mem_map->Begin(), mem_map->Begin() + initial_size),
        mem_map_(mem_map) {
  }

  MEM_MAP* GetMemMap() {
    return mem_map_.get();
  }

  const MEM_MAP* GetMemMap() const {
    return mem_map_.get();
  }

 private:
  // Underlying storage of the space
  UniquePtr<MEM_MAP> mem_map_;

  DISALLOW_COPY_AND_ASSIGN(MemMapSpace);
};


#endif

}  // namespace space
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_SPACE_SPACE_H_
