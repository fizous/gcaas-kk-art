/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_HEAP_H_
#define ART_RUNTIME_GC_HEAP_H_

#include <iosfwd>
#include <string>
#include <vector>

#include "atomic_integer.h"
#include "base/timing_logger.h"
#include "gc/accounting/atomic_stack.h"
#include "gc/accounting/card_table.h"
#include "gc/collector/gc_type.h"
#include "gc/space/space.h"
#include "globals.h"
#include "gtest/gtest.h"
#include "jni.h"
#include "locks.h"
#include "offsets.h"
#include "safe_map.h"
#include "thread_pool.h"



#if (ART_GC_SERVICE)
 #define GC_HEAP_LARGE_OBJECT_THRESHOLD (std::numeric_limits<size_t>::max()) //prevent allocations from going to large space
 #define GC_HEAP_SRVCE_NO_LOS     true
 #define DL_MALLOC_SPACE DlMallocSpace
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
  #if (ART_USE_GC_DEFAULT_PROFILER || ART_USE_GC_PROFILER)
    #define GC_HEAP_LARGE_OBJECT_THRESHOLD (std::numeric_limits<size_t>::max())
  #else
    #define GC_HEAP_LARGE_OBJECT_THRESHOLD (3 * kPageSize)
  #endif
 #define GC_HEAP_SRVCE_NO_LOS     false
 #define DL_MALLOC_SPACE DlMallocSpace
 #define DLMALLOC_SPACE_T DlMallocSpace
 #define CONTINUOUS_SPACE_T ContinuousSpace
#endif

namespace art {

class ConditionVariable;
class Mutex;
class StackVisitor;
class Thread;
class TimingLogger;

namespace mirror {
  class Class;
  class Object;
}  // namespace mirror



namespace gc {
namespace accounting {
#if (ART_GC_SERVICE)
class BaseHeapBitmap;
#else
class HeapBitmap;
#endif
  class ModUnionTable;
  class SpaceSetMap;
}  // namespace accounting

namespace collector {
  class GarbageCollector;
  class MarkSweep;
  class IPCHeap;
}  // namespace collector

namespace space {
  class AllocSpace;
  class DiscontinuousSpace;
  class DL_MALLOC_SPACE;
  class DLMALLOC_SPACE_T;
  class ImageSpace;
  class LargeObjectSpace;
  class Space;
  class SpaceTest;
  class ABSTRACT_CONTINUOUS_SPACE_T;
}  // namespace space


class AgeCardVisitor {
 public:
  byte operator()(byte card) const {
    if (card == accounting::ConstantsCardTable::kCardDirty) {
      return card - 1;
    } else {
      return 0;
    }
  }
};

// What caused the GC?
enum GcCause {
  // GC triggered by a failed allocation. Thread doing allocation is blocked waiting for GC before
  // retrying allocation.
  kGcCauseForAlloc,
  // A background GC trying to ensure there is free memory ahead of allocations.
  kGcCauseBackground,
  // An explicit System.gc() call.
  kGcCauseExplicit,
  // An explicit System.gc() call for the profiler.
  kGcCauseProfile,
};
std::ostream& operator<<(std::ostream& os, const GcCause& policy);

// How we want to sanity check the heap's correctness.
enum HeapVerificationMode {
  kHeapVerificationNotPermitted,  // Too early in runtime start-up for heap to be verified.
  kNoHeapVerification,  // Production default.
  kVerifyAllFast,  // Sanity check all heap accesses with quick(er) tests.
  kVerifyAll  // Sanity check all heap accesses.
};
static constexpr HeapVerificationMode kDesiredHeapVerification = kNoHeapVerification;

class Heap {
 public:
  static constexpr size_t kDefaultInitialSize = 2 * MB;
  static constexpr size_t kDefaultMaximumSize = 32 * MB;
  static constexpr size_t kDefaultMaxFree = 2 * MB;
  static constexpr size_t kDefaultMinFree = kDefaultMaxFree / 4;
  static constexpr size_t kDefaultLongPauseLogThreshold = MsToNs(5);
  static constexpr size_t kDefaultLongGCLogThreshold = MsToNs(100);

  // Default target utilization.
  static constexpr double kDefaultTargetUtilization = 0.5;

  // Used so that we don't overflow the allocation time atomic integer.
  static constexpr size_t kTimeAdjust = 1024;

  // Create a heap with the requested sizes. The possible empty
  // image_file_names names specify Spaces to load based on
  // ImageWriter output.
  explicit Heap(size_t initial_size, size_t growth_limit, size_t min_free,
                size_t max_free, double target_utilization, size_t capacity,
                const std::string& original_image_file_name, bool concurrent_gc,
                size_t parallel_gc_threads, size_t conc_gc_threads, bool low_memory_mode,
                size_t long_pause_threshold, size_t long_gc_threshold, bool ignore_max_footprint);

  ~Heap();

  void GetMaxContigAlloc(void* args);
  // Allocates and initializes storage for an object instance.
  mirror::Object* AllocObject(Thread* self, mirror::Class* klass, size_t num_bytes)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void RegisterNativeAllocation(int bytes)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void RegisterNativeFree(int bytes) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // The given reference is believed to be to an object in the Java heap, check the soundness of it.
  void VerifyObjectImpl(const mirror::Object* o);
  void VerifyObject(const mirror::Object* o) {
    if (o != NULL && this != NULL && verify_object_mode_ > kNoHeapVerification) {
      VerifyObjectImpl(o);
    }
  }

  // Check sanity of all live references.
  void VerifyHeap() LOCKS_EXCLUDED(Locks::heap_bitmap_lock_);
  bool VerifyHeapReferences()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  bool VerifyMissingCardMarks()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // A weaker test than IsLiveObject or VerifyObject that doesn't require the heap lock,
  // and doesn't abort on error, allowing the caller to report more
  // meaningful diagnostics.
  bool IsHeapAddress(const mirror::Object* obj);

  // Returns true if 'obj' is a live heap object, false otherwise (including for invalid addresses).
  // Requires the heap lock to be held.
  bool IsLiveObjectLocked(const mirror::Object* obj, bool search_allocation_stack = true,
                          bool search_live_stack = true, bool sorted = false)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Initiates an explicit garbage collection.
  void CollectGarbageForProfile(bool clear_soft_references) LOCKS_EXCLUDED(Locks::mutator_lock_);

  // Initiates an explicit garbage collection.
  void CollectGarbage(bool clear_soft_references) LOCKS_EXCLUDED(Locks::mutator_lock_);

  // Does a concurrent GC, should only be called by the GC daemon thread
  // through runtime.
  void ConcurrentGC(Thread* self) LOCKS_EXCLUDED(Locks::runtime_shutdown_lock_);

  // Implements VMDebug.countInstancesOfClass and JDWP VM_InstanceCount.
  // The boolean decides whether to use IsAssignableFrom or == when comparing classes.
  void CountInstances(const std::vector<mirror::Class*>& classes, bool use_is_assignable_from,
                      uint64_t* counts)
      LOCKS_EXCLUDED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  // Implements JDWP RT_Instances.
  void GetInstances(mirror::Class* c, int32_t max_count, std::vector<mirror::Object*>& instances)
      LOCKS_EXCLUDED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  // Implements JDWP OR_ReferringObjects.
  void GetReferringObjects(mirror::Object* o, int32_t max_count, std::vector<mirror::Object*>& referring_objects)
      LOCKS_EXCLUDED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Removes the growth limit on the alloc space so it may grow to its maximum capacity. Used to
  // implement dalvik.system.VMRuntime.clearGrowthLimit.
  void ClearGrowthLimit();



  // Data structure memory usage tracking.
  void RegisterGCAllocation(size_t bytes);
  void RegisterGCDeAllocation(size_t bytes);

  // Set target ideal heap utilization ratio, implements
  // dalvik.system.VMRuntime.setTargetHeapUtilization.
  void SetTargetHeapUtilization(float target);

  // For the alloc space, sets the maximum number of bytes that the heap is allowed to allocate
  // from the system. Doesn't allow the space to exceed its growth limit.
  void SetIdealFootprint(size_t max_allowed_footprint);

  // Blocks the caller until the garbage collector becomes idle and returns
  // true if we waited for the GC to complete.
  collector::GcType WaitForConcurrentGcToComplete(Thread* self,
                                                  bool profWaitTime) LOCKS_EXCLUDED(gc_complete_lock_);

  const std::vector<space::ABSTRACT_CONTINUOUS_SPACE_T*>& GetContinuousSpaces() const {
    return continuous_spaces_;
  }

  const std::vector<space::DiscontinuousSpace*>& GetDiscontinuousSpaces() const {
    return discontinuous_spaces_;
  }

  void SetReferenceOffsets(MemberOffset reference_referent_offset,
                           MemberOffset reference_queue_offset,
                           MemberOffset reference_queueNext_offset,
                           MemberOffset reference_pendingNext_offset,
                           MemberOffset finalizer_reference_zombie_offset);

  mirror::Object* GetReferenceReferent(mirror::Object* reference);
  void ClearReferenceReferent(mirror::Object* reference) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Returns true if the reference object has not yet been enqueued.
  bool IsEnqueuable(const mirror::Object* ref);
  void EnqueueReference(mirror::Object* ref, mirror::Object** list)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  bool IsEnqueued(mirror::Object* ref) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  bool IsEnqueuedNoLock(mirror::Object* ref) ;
#if (ART_GC_SERVICE)
  void EnqueuePendingReferenceNoLock(mirror::Object* ref, mirror::Object** list);
#endif
  void EnqueuePendingReference(mirror::Object* ref, mirror::Object** list)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  mirror::Object* DequeuePendingReference(mirror::Object** list)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  MemberOffset GetReferencePendingNextOffset() {
    DCHECK_NE(reference_pendingNext_offset_.Uint32Value(), 0U);
    return reference_pendingNext_offset_;
  }

  MemberOffset GetFinalizerReferenceZombieOffset() {
    DCHECK_NE(finalizer_reference_zombie_offset_.Uint32Value(), 0U);
    return finalizer_reference_zombie_offset_;
  }

  // Enable verification of object references when the runtime is sufficiently initialized.
  void EnableObjectValidation() {
    verify_object_mode_ = kDesiredHeapVerification;
    if (verify_object_mode_ > kNoHeapVerification) {
      VerifyHeap();
    }
  }

  // Disable object reference verification for image writing.
  void DisableObjectValidation() {
    verify_object_mode_ = kHeapVerificationNotPermitted;
  }

  // Other checks may be performed if we know the heap should be in a sane state.
  bool IsObjectValidationEnabled() const {
    return kDesiredHeapVerification > kNoHeapVerification &&
        verify_object_mode_ > kHeapVerificationNotPermitted;
  }

  // Returns true if low memory mode is enabled.
  bool IsLowMemoryMode() const {
    return low_memory_mode_;
  }

  void RecordFree(size_t freed_objects, size_t freed_bytes);

  void gcpIncMutationCnt(void);


  void gcpIncMutationCnt(const mirror::Object* dst, MemberOffset offset,
  		const mirror::Object* new_value);

  void gcpIncMutationCnt(const mirror::Object* dst, size_t elementPos,
  		size_t length);
#if ART_USE_GC_PROFILER_REF_DIST
  // Must be called if a field of an Object in the heap changes, and before any GC safe-point.
  // The call is not needed if NULL is stored in the field.
  void WriteBarrierField(const mirror::Object* dst, MemberOffset offset,
  		const mirror::Object* new_value) {
  	gcpIncMutationCnt(dst, offset, new_value);
    card_table_->MarkCard(dst);
  }

  // Write barrier for array operations that update many field positions
  void WriteBarrierArray(const mirror::Object* dst, int start_offset,
                         size_t length /*TODO: element_count or byte_count?*/) {
  	gcpIncMutationCnt(dst, (size_t)start_offset, length);
    card_table_->MarkCard(dst);
  }
#else
  // Must be called if a field of an Object in the heap changes, and before any GC safe-point.
  // The call is not needed if NULL is stored in the field.
  void WriteBarrierField(const mirror::Object* dst, MemberOffset /*offset*/,
  		const mirror::Object* /*new_value*/) {
#if ART_USE_GC_PROFILER
  	gcpIncMutationCnt();
#endif
    card_table_->MarkCard(dst);
  }

  // Write barrier for array operations that update many field positions
  void WriteBarrierArray(const mirror::Object* dst, int /*start_offset*/,
                         size_t /*length TODO: element_count or byte_count?*/) {
#if ART_USE_GC_PROFILER
  	gcpIncMutationCnt();
#endif
    card_table_->MarkCard(dst);
  }
#endif //ART_USE_GC_PROFILER_REF_DIST


  accounting::CARD_TABLE* GetCardTable() const {
    return card_table_.get();
  }

  void AddFinalizerReference(Thread* self, mirror::Object* object);



#if (ART_GC_SERVICE)
  // Target ideal heap utilization ratio, implements
  // dalvik.system.VMRuntime.getTargetHeapUtilization.
  double GetTargetHeapUtilization() const {

    return sub_record_meta_->target_utilization_;
  }

  // Returns the number of bytes currently allocated.
  size_t GetBytesAllocated() const {
    return sub_record_meta_->num_bytes_allocated_;
  }

  int32_t GetAtomicBytesAllocated() {
    return android_atomic_release_load(&(sub_record_meta_->num_bytes_allocated_));
  }

  void IncAtomicBytesAllocated(int val) {
    android_atomic_add(val, &(sub_record_meta_->num_bytes_allocated_));
  }

  void SetNumBytesAllocated(size_t val) {
    sub_record_meta_->num_bytes_allocated_ = val;
  }

  // Returns the number of bytes currently allocated.
  size_t GetNativeBytesAllocated() const {
    return sub_record_meta_->native_bytes_allocated_;
  }

  int32_t GetAtomicNativeBytesAllocated() {
    return android_atomic_release_load(&(sub_record_meta_->native_bytes_allocated_));
  }

  void SetNativeBytesAllocated(size_t val) {
    sub_record_meta_->native_bytes_allocated_ = val;
  }


  void IncAtomicNativeBytesAllocated(int val) {
    android_atomic_add(val, &(sub_record_meta_->native_bytes_allocated_));
  }

  bool CASAtomicNativeBytesAllocated(int32_t expected, int32_t new_val) {
    return
        android_atomic_cas(expected, new_val,
            &(sub_record_meta_->native_bytes_allocated_)) == 0;
  }

#else

  int32_t GetAtomicBytesAllocated() {
    return num_bytes_allocated_.load();
  }

  void IncAtomicBytesAllocated(int val) {
    num_bytes_allocated_.fetch_add(val);
  }


  void IncAtomicNativeBytesAllocated(int val) {
    native_bytes_allocated_.fetch_add(val);
  }

  int32_t GetAtomicNativeBytesAllocated() {
    return native_bytes_allocated_.load();
  }

  bool CASAtomicNativeBytesAllocated(int32_t expected, int32_t new_val) {
    return native_bytes_allocated_.compare_and_swap(expected, new_val);
  }

  // Returns the number of bytes currently allocated.
  size_t GetNativeBytesAllocated() const {
    return native_bytes_allocated_;
  }

  double GetTargetHeapUtilization() const {

    return target_utilization_;
  }

  size_t GetBytesAllocated() const {
    return num_bytes_allocated_;
  }

  void SetNumBytesAllocated(size_t val) {
    num_bytes_allocated_ = val;
  }
  void SetNativeBytesAllocated(size_t val) {
    native_bytes_allocated_ = val;
  }
#endif


  // Returns the number of objects currently allocated.
  size_t GetObjectsAllocated() const;

  // Returns the total number of objects allocated since the heap was created.
  size_t GetObjectsAllocatedEver() const;

  // Returns the total number of bytes allocated since the heap was created.
  size_t GetBytesAllocatedEver() const;

#if (ART_GC_SERVICE)
  // Returns the total number of objects freed since the heap was created.
  size_t GetObjectsFreedEver() const {
    return sub_record_meta_->total_objects_freed_ever_;
  }

  // Returns the total number of bytes freed since the heap was created.
  size_t GetBytesFreedEver() const {
    return sub_record_meta_->total_bytes_freed_ever_;
  }

  size_t GetMaxAllowedFootPrint() const {
    return sub_record_meta_->max_allowed_footprint_;
  }
#else
  size_t GetMaxAllowedFootPrint() const {
    return max_allowed_footprint_;
  }
  // Returns the total number of objects freed since the heap was created.
  size_t GetObjectsFreedEver() const {
    return total_objects_freed_ever_;
  }

  // Returns the total number of bytes freed since the heap was created.
  size_t GetBytesFreedEver() const {
    return total_bytes_freed_ever_;
  }
#endif
  size_t GetMinAllocSpaceSizeForSticky() const {
    return min_alloc_space_size_for_sticky_gc_;
  }

  // Implements java.lang.Runtime.maxMemory, returning the maximum amount of memory a program can
  // consume. For a regular VM this would relate to the -Xmx option and would return -1 if no Xmx
  // were specified. Android apps start with a growth limit (small heap size) which is
  // cleared/extended for large apps.
  int64_t GetMaxMemory() const {
    return GetGrowthLimit();
  }

  // Implements java.lang.Runtime.totalMemory, returning the amount of memory consumed by an
  // application.
  int64_t GetTotalMemory() const;

  // Implements java.lang.Runtime.freeMemory.
  int64_t GetFreeMemory() const {
    return GetTotalMemory() - GetBytesAllocated();
  }
  size_t GetConcStartBytes(bool forProfile = false) const;


  // Get the space that corresponds to an object's address. Current implementation searches all
  // spaces in turn. If fail_ok is false then failing to find a space will cause an abort.
  // TODO: consider using faster data structure like binary tree.
  space::CONTINUOUS_SPACE_T* FindContinuousSpaceFromObject(const mirror::Object*, bool fail_ok) const;
  space::DiscontinuousSpace* FindDiscontinuousSpaceFromObject(const mirror::Object*,
                                                              bool fail_ok) const;
  space::Space* FindSpaceFromObject(const mirror::Object*, bool fail_ok) const;
  size_t GCPGetObjectAllocatedSpace(const mirror::Object* obj);

  void DumpForSigQuit(std::ostream& os);

  size_t Trim();

#if (ART_GC_SERVICE)
  accounting::BaseHeapBitmap* GetLiveBitmap() SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    return live_bitmap_.get();
  }

  accounting::BaseHeapBitmap* GetMarkBitmap() SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    return mark_bitmap_.get();
  }
#else
  accounting::HeapBitmap* GetLiveBitmap() SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    return live_bitmap_.get();
  }

  accounting::HeapBitmap* GetMarkBitmap() SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    return mark_bitmap_.get();
  }
#endif
  accounting::ATOMIC_OBJ_STACK_T* GetLiveStack() SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    return live_stack_.get();
  }

  void PreZygoteForkNoSpaceFork() LOCKS_EXCLUDED(Locks::heap_bitmap_lock_);
  void PreZygoteFork() LOCKS_EXCLUDED(Locks::heap_bitmap_lock_);



  void FixHeapBitmapEntries()EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  // Mark and empty stack.
  void FlushAllocStack()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
#if (ART_GC_SERVICE)
  void PostZygoteForkWithSpaceFork(bool);
  void SetSubHeapMetaData(space::GCSrvcHeapSubRecord* new_address);
  // Mark all the objects in the allocation stack in the specified bitmap.
  void MarkAllocStack(accounting::BaseBitmap* bitmap, accounting::SpaceSetMap* large_objects,
                      accounting::ATOMIC_OBJ_STACK_T* stack)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
#else
  // Mark all the objects in the allocation stack in the specified bitmap.
  void MarkAllocStack(accounting::SpaceBitmap* bitmap, accounting::SpaceSetMap* large_objects,
                      accounting::ATOMIC_OBJ_STACK_T* stack)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
#endif
  // Update and mark mod union table based on gc type.
  void UpdateAndMarkModUnion(collector::MarkSweep* mark_sweep, base::TimingLogger& timings,
                             collector::GcType gc_type)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Gets called when we get notified by ActivityThread that the process state has changed.
  void ListenForProcessStateChange();
  int GetLastProcessStateID(void);
  void initializeProcessStateObjects(void);
  // DEPRECATED: Should remove in "near" future when support for multiple image spaces is added.
  // Assumes there is only one image space.
  space::ImageSpace* GetImageSpace() const;

  space::DLMALLOC_SPACE_T* GetAllocSpace() const {
    return alloc_space_;
  }

  space::LargeObjectSpace* GetLargeObjectsSpace() const {
    return large_object_space_;
  }

  Mutex* GetSoftRefQueueLock() {
    return soft_ref_queue_lock_;
  }

  Mutex* GetWeakRefQueueLock() {
    return weak_ref_queue_lock_;
  }

  Mutex* GetFinalizerRefQueueLock() {
    return finalizer_ref_queue_lock_;
  }

  Mutex* GetPhantomRefQueueLock() {
    return phantom_ref_queue_lock_;
  }

  void DumpSpaces();

  // GC performance measuring
  void DumpGcPerformanceInfo(std::ostream& os);

  // Returns true if we currently care about pause times.
  bool CareAboutPauseTimes() const {
    return care_about_pause_times_;
  }

  // Thread pool.
  void CreateThreadPool();
  void DeleteThreadPool();
  ThreadPool* GetThreadPool() {
    return thread_pool_.get();
  }
  size_t GetParallelGCThreadCount() const {
    return parallel_gc_threads_;
  }
  size_t GetConcGCThreadCount() const {
    return conc_gc_threads_;
  }

  size_t GetHeapCapacity() const {
  	return capacity_;
  }

  byte* GetMaxAddress();


  void GCPSrvcReinitMarkSweep(collector::MarkSweep* newCollector);
  void GCServiceSignalConcGC(Thread* self) LOCKS_EXCLUDED(Locks::runtime_shutdown_lock_);


//  void SetNextGCType(collector::GcType gc_type);

//  size_t GetConcurrentStartBytes(void);
//  collector::GcType GetNextGCType(void);

  void PreGcVerification(collector::GarbageCollector* gc);

  accounting::ATOMIC_OBJ_STACK_T* GetHeapMarkStack(void){
    return mark_stack_.get();
  }
  // Clear cards and update the mod union table.
  void ProcessCards(base::TimingLogger& timings);

  // Swap the allocation stack with the live stack.
  void SwapStacks();



 private:
  // Allocates uninitialized storage. Passing in a null space tries to place the object in the
  // large object space.
  template <class T> mirror::Object* Allocate(Thread* self, T* space, size_t num_bytes, size_t* bytes_allocated)
      LOCKS_EXCLUDED(Locks::thread_suspend_count_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Handles Allocate()'s slow allocation path with GC involved after
  // an initial allocation attempt failed.
  mirror::Object* AllocateInternalWithGc(Thread* self, space::AllocSpace* space, size_t num_bytes,
                                         size_t* bytes_allocated)
      LOCKS_EXCLUDED(Locks::thread_suspend_count_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Try to allocate a number of bytes, this function never does any GCs.
  mirror::Object* TryToAllocate(Thread* self, space::AllocSpace* space, size_t alloc_size, bool grow,
                                size_t* bytes_allocated)
      LOCKS_EXCLUDED(Locks::thread_suspend_count_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Try to allocate a number of bytes, this function never does any GCs. DlMallocSpace-specialized version.
  mirror::Object* TryToAllocate(Thread* self, space::DL_MALLOC_SPACE* space, size_t alloc_size, bool grow,
                                size_t* bytes_allocated)
      LOCKS_EXCLUDED(Locks::thread_suspend_count_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  bool IsOutOfMemoryOnAllocation(size_t alloc_size, bool grow);

  // Pushes a list of cleared references out to the managed heap.
  void EnqueueClearedReferences(mirror::Object** cleared_references);


  bool RequestHeapTrimIfNeeded(size_t adjusted_max_free, bool care_about_pauses,
                               bool send_remote_req) LOCKS_EXCLUDED(Locks::runtime_shutdown_lock_);
  void RequestHeapTrim() LOCKS_EXCLUDED(Locks::runtime_shutdown_lock_);

  void RequestConcurrentGC(Thread* self) LOCKS_EXCLUDED(Locks::runtime_shutdown_lock_);
  bool IsGCRequestPending() const;

  void RecordAllocation(size_t size, mirror::Object* object)
      LOCKS_EXCLUDED(GlobalSynchronization::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Sometimes CollectGarbageInternal decides to run a different Gc than you requested. Returns
  // which type of Gc was actually ran.
  collector::GcType CollectGarbageInternal(collector::GcType gc_plan, GcCause gc_cause,
                                           bool clear_soft_references)
      LOCKS_EXCLUDED(gc_complete_lock_,
                     Locks::heap_bitmap_lock_,
                     Locks::thread_suspend_count_lock_);


  void PreSweepingGcVerification(collector::GarbageCollector* gc)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void PostGcVerification(collector::GarbageCollector* gc);

  // Update the watermark for the native allocated bytes based on the current number of native
  // bytes allocated and the target utilization ratio.
  void UpdateMaxNativeFootprint();
  void GCSrvcUpdateMaxNativeFootprint(size_t min_free, size_t max_free);
  // Given the current contents of the alloc space, increase the allowed heap footprint to match
  // the target utilization ratio.  This should only be called immediately after a full garbage
  // collection.
  void GrowForUtilization(collector::GcType gc_type, uint64_t gc_duration);


  // Returns the heap growth multiplier, this affects how much we grow the heap after a GC.
  // Scales heap growth, min free, and max free.
  double HeapGrowthMultiplier() const;
  // Given the current contents of the alloc space, increase the allowed heap footprint to match
  // the target utilization ratio.  This should only be called immediately after a full garbage
  // collection.
  void GCSrvcGrowForUtilization(collector::GcType gc_type,
                                uint64_t gc_duration,
                                double adjusted_resize_factor,
                                size_t* adjusted_max_free,
                                double conc_latency);

  size_t GetPercentFree();

  void AddContinuousSpace(space::ABSTRACT_CONTINUOUS_SPACE_T* space) LOCKS_EXCLUDED(Locks::heap_bitmap_lock_);
  void AddDiscontinuousSpace(space::DiscontinuousSpace* space)
      LOCKS_EXCLUDED(Locks::heap_bitmap_lock_);

  // No thread saftey analysis since we call this everywhere and it is impossible to find a proper
  // lock ordering for it.
  void VerifyObjectBody(const mirror::Object *obj) NO_THREAD_SAFETY_ANALYSIS;

  static void VerificationCallback(mirror::Object* obj, void* arg)
      SHARED_LOCKS_REQUIRED(GlobalSychronization::heap_bitmap_lock_);





  // All-known continuous spaces, where objects lie within fixed bounds.
  std::vector<space::ABSTRACT_CONTINUOUS_SPACE_T*> continuous_spaces_;

  // All-known discontinuous spaces, where objects may be placed throughout virtual memory.
  std::vector<space::DiscontinuousSpace*> discontinuous_spaces_;

  // The allocation space we are currently allocating into.
  space::DLMALLOC_SPACE_T* alloc_space_;

  // The large object space we are currently allocating into.
  space::LargeObjectSpace* large_object_space_;

  // The card table, dirtied by the write barrier.
  UniquePtr<accounting::CARD_TABLE> card_table_;

  // The mod-union table remembers all of the references from the image space to the alloc /
  // zygote spaces to allow the card table to be cleared.
  UniquePtr<accounting::ModUnionTable> image_mod_union_table_;

  // This table holds all of the references from the zygote space to the alloc space.
  UniquePtr<accounting::ModUnionTable> zygote_mod_union_table_;

  // What kind of concurrency behavior is the runtime after? True for concurrent mark sweep GC,
  // false for stop-the-world mark sweep.
  const bool concurrent_gc_;

  // How many GC threads we may use for paused parts of garbage collection.
  const size_t parallel_gc_threads_;

  // How many GC threads we may use for unpaused parts of garbage collection.
  const size_t conc_gc_threads_;

  // Boolean for if we are in low memory mode.
  const bool low_memory_mode_;

  // If we get a pause longer than long pause log threshold, then we print out the GC after it
  // finishes.
  const size_t long_pause_log_threshold_;

  // If we get a GC longer than long GC log threshold, then we print out the GC after it finishes.
  const size_t long_gc_log_threshold_;

  // If we ignore the max footprint it lets the heap grow until it hits the heap capacity, this is
  // useful for benchmarking since it reduces time spent in GC to a low %.
  const bool ignore_max_footprint_;

  // If we have a zygote space.
  bool have_zygote_space_;

  // Guards access to the state of GC, associated conditional variable is used to signal when a GC
  // completes.
  Mutex* gc_complete_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> gc_complete_cond_ GUARDED_BY(gc_complete_lock_);

  // Mutexes held when adding references to reference queues.
  // TODO: move to a UniquePtr, currently annotalysis is confused that UniquePtr isn't lockable.
  Mutex* soft_ref_queue_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  Mutex* weak_ref_queue_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  Mutex* finalizer_ref_queue_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  Mutex* phantom_ref_queue_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  // True while the garbage collector is running.
  volatile bool is_gc_running_ GUARDED_BY(gc_complete_lock_);

  // Last Gc type we ran. Used by WaitForConcurrentGc to know which Gc was waited on.
#if (ART_GC_SERVICE)
#else
  volatile collector::GcType last_gc_type_ GUARDED_BY(gc_complete_lock_);
  collector::GcType next_gc_type_;
#endif

  // Maximum size that the heap can reach.
  const size_t capacity_;

#if (ART_GC_SERVICE)
#else
  // The size the heap is limited to. This is initially smaller than capacity, but for largeHeap
  // programs it is "cleared" making it the same as capacity.
  size_t growth_limit_;

  // When the number of bytes allocated exceeds the footprint TryAllocate returns NULL indicating
  // a GC should be triggered.
  size_t max_allowed_footprint_;

  // The watermark at which a concurrent GC is requested by registerNativeAllocation.
  size_t native_footprint_gc_watermark_;

  size_t native_footprint_limit_;
#endif


  // Activity manager members.
  jclass activity_thread_class_;
  jclass application_thread_class_;
  jobject activity_thread_;
  jobject application_thread_;
  jfieldID last_process_state_id_;

  // Process states which care about pause times.
  std::set<int> process_state_cares_about_pause_time_;

  // Whether or not we currently care about pause times.
  bool care_about_pause_times_;
#if (ART_GC_SERVICE)
#else
  // When num_bytes_allocated_ exceeds this amount then a concurrent GC should be requested so that
  // it completes ahead of an allocation failing.
  size_t concurrent_start_bytes_;

  // Since the heap was created, how many bytes have been freed.
  size_t total_bytes_freed_ever_;

  // Since the heap was created, how many objects have been freed.
  size_t total_objects_freed_ever_;
#endif


  // Primitive objects larger than this size are put in the large object space.
  const size_t large_object_threshold_;

#if (ART_GC_SERVICE)
#else
  // Number of bytes allocated.  Adjusted after each allocation and free.
  AtomicInteger num_bytes_allocated_;

  // Bytes which are allocated and managed by native code but still need to be accounted for.
  AtomicInteger native_bytes_allocated_;
#endif
  // Data structure GC overhead.
  AtomicInteger gc_memory_overhead_;

  // Heap verification flags.
  const bool verify_missing_card_marks_;
  const bool verify_system_weaks_;
  const bool verify_pre_gc_heap_;
  const bool verify_post_gc_heap_;
  const bool verify_mod_union_table_;

  // Parallel GC data structures.
  UniquePtr<ThreadPool> thread_pool_;

  // Sticky mark bits GC has some overhead, so if we have less a few megabytes of AllocSpace then
  // it's probably better to just do a partial GC.
  const size_t min_alloc_space_size_for_sticky_gc_;

  // Minimum remaining size for sticky GC. Since sticky GC doesn't free up as much memory as a
  // normal GC, it is important to not use it when we are almost out of memory.
  const size_t min_remaining_space_for_sticky_gc_;





  // For a GC cycle, a bitmap that is set corresponding to the
#if (ART_GC_SERVICE)
  space::GCSrvcHeapSubRecord* sub_record_meta_;

  UniquePtr<accounting::BaseHeapBitmap> live_bitmap_ GUARDED_BY(Locks::heap_bitmap_lock_);
  UniquePtr<accounting::BaseHeapBitmap> mark_bitmap_ GUARDED_BY(Locks::heap_bitmap_lock_);


  uint64_t GetLastTimeTrim() {
    return sub_record_meta_->last_trim_time_ms_;
  }

  uint64_t GetLastGCTime() {
    return sub_record_meta_->last_gc_time_ns_;
  }

  uint64_t GetLastGCSize() {
    return sub_record_meta_->last_gc_size_;
  }

  uint64_t GetAllocationRate() {
    return sub_record_meta_->allocation_rate_;
  }

  void SetAllocationRate(uint64_t rate) {
    sub_record_meta_->allocation_rate_ = rate;
  }

  void SetLastGCSize(size_t param) {
    sub_record_meta_->last_gc_size_ = param;
  }

  void SetLastGCTime(uint64_t param) {
    sub_record_meta_->last_gc_time_ns_ = param;
  }

  void SetLastTimeTrim(uint64_t param) {
    sub_record_meta_->last_trim_time_ms_ = param;
  }


  void SetTotalBytesFreedEver(size_t param) {
    sub_record_meta_->total_bytes_freed_ever_ = param;
  }

  void SetTotalObjectsFreedEver(size_t param) {
    sub_record_meta_->total_objects_freed_ever_ = param;
  }


  void IncTotalBytesFreedEver(size_t param) {
    sub_record_meta_->total_bytes_freed_ever_ += param;
  }

  void IncTotalObjectsFreedEver(size_t param) {
    sub_record_meta_->total_objects_freed_ever_ += param;
  }

  void SetGrowthLimit(size_t growth) {
    sub_record_meta_->growth_limit_ = growth;
  }


  size_t GetGrowthLimit() const {
    return sub_record_meta_->growth_limit_;
  }


  void SetConcStartBytes(size_t concStart) {
    sub_record_meta_->concurrent_start_bytes_ = concStart;
  }

  size_t GetConcStartBytesValue() const {
    return sub_record_meta_->concurrent_start_bytes_;
  }

  void SetTargetUtilization(double targetUtil) {
    sub_record_meta_->target_utilization_ = targetUtil;
  }


  void SetMinFree(size_t minFree) {
    sub_record_meta_->min_free_ = minFree;
  }

  void SetMaxFree(size_t maxFree) {
    sub_record_meta_->max_free_ = maxFree;
  }

  size_t GetMinFree() {
    return sub_record_meta_->min_free_;
  }

  size_t GetMaxFree() {
    return sub_record_meta_->max_free_;
  }

  void SetTotalWaitTime(uint64_t param) {
    sub_record_meta_->total_wait_time_ = param;
  }

  uint64_t GetTotalWaitTime() {
    return sub_record_meta_->total_wait_time_;
  }

  void IncTotalWaitTime(uint64_t param) {
    sub_record_meta_->total_wait_time_ += param;
  }

  collector::GcType GetNextGCType() const {
    return sub_record_meta_->next_gc_type_;
  }

  void SetNextGCType(collector::GcType val)  {
    sub_record_meta_->next_gc_type_ = val;
  }

  collector::GcType GetLastGCType() const {
    return static_cast<collector::GcType>(android_atomic_release_load(
        reinterpret_cast<int*>(&(sub_record_meta_->last_gc_type_))));
  }

  void SetLastGCType(collector::GcType val)  {
    android_atomic_acquire_store(static_cast<int>(val),
        reinterpret_cast<int*>(&(sub_record_meta_->last_gc_type_)));
  }


  size_t GetNativeFootPrintLimit() const {
    return sub_record_meta_->native_footprint_limit_;
  }

  void SetNativeFootPrintLimit(size_t param)  {
    sub_record_meta_->native_footprint_limit_ = param;
  }

  void SetNativeFootPrintGCWaterMark(size_t param)  {
    sub_record_meta_->native_footprint_gc_watermark_ = param;
  }

  size_t GetNativeFootPrintGCWaterMark() const {
    return sub_record_meta_->native_footprint_gc_watermark_;
  }

  void SetMaxAllowedFootPrint(size_t param) {
    sub_record_meta_->max_allowed_footprint_ = param;
  }
#else





  UniquePtr<accounting::HeapBitmap> live_bitmap_ GUARDED_BY(Locks::heap_bitmap_lock_);
  UniquePtr<accounting::HeapBitmap> mark_bitmap_ GUARDED_BY(Locks::heap_bitmap_lock_);

  // The last time a heap trim occurred.
  uint64_t last_trim_time_ms_;

  // The nanosecond time at which the last GC ended.
  uint64_t last_gc_time_ns_;

  // How many bytes were allocated at the end of the last GC.
  uint64_t last_gc_size_;

  // Estimated allocation rate (bytes / second). Computed between the time of the last GC cycle
  // and the start of the current one.
  uint64_t allocation_rate_;

  size_t GetNativeFootPrintGCWaterMark() const {
    return native_footprint_gc_watermark_;
  }

  void IncTotalWaitTime(uint64_t param) {
    total_wait_time_ += param;
  }

  void SetNextGCType(collector::GcType val) {
    next_gc_type_ = val;
  }

  void SetLastGCType(collector::GcType val)  EXCLUSIVE_LOCKS_REQUIRED(gc_complete_lock_) {
    last_gc_type_ = val;
  }

  collector::GcType GetLastGCType() const EXCLUSIVE_LOCKS_REQUIRED(gc_complete_lock_)  {
    return last_gc_type_;
  }

  collector::GcType GetNextGCType() const {
    return next_gc_type_;
  }

  void IncTotalBytesFreedEver(size_t param) {
    total_bytes_freed_ever_ += param;
  }

  void SetNativeFootPrintGCWaterMark(size_t param)  {
    native_footprint_gc_watermark_ = param;
  }


  size_t GetNativeFootPrintLimit() const {
    return native_footprint_limit_;
  }

  size_t GetMinFree() {
    return min_free_;
  }

  size_t GetMaxFree() {
    return max_free_;
  }

  void IncTotalObjectsFreedEver(size_t param) {
    total_objects_freed_ever_ += param;
  }

  void SetNativeFootPrintLimit(size_t param)  {
    native_footprint_limit_ = param;
  }

  size_t GetConcStartBytesValue() const {
    return concurrent_start_bytes_;
  }

  size_t GetGrowthLimit() const {
    return growth_limit_;
  }

  uint64_t GetLastTimeTrim() {
    return last_trim_time_ms_;
  }

  uint64_t GetLastGCTime() {
    return last_gc_time_ns_;
  }

  uint64_t GetLastGCSize() {
    return last_gc_size_;
  }

  uint64_t GetAllocationRate() {
    return allocation_rate_;
  }

  void SetAllocationRate(uint64_t rate) {
    allocation_rate_ = rate;
  }

  void SetLastTimeTrim(uint64_t param) {
    last_trim_time_ms_ = param;
  }

  void SetLastGCSize(size_t param) {
    last_gc_size_ = param;
  }

  void SetLastGCTime(uint64_t param) {
    last_gc_time_ns_ = param;
  }

  void SetGrowthLimit(size_t growth) {
    growth_limit_ = growth;
  }

  void SetConcStartBytes(size_t concStart) {
    concurrent_start_bytes_ = concStart;
  }

  void SetTargetUtilization(double targetUtil) {
    target_utilization_ = targetUtil;
  }

  void SetMinFree(size_t minFree) {
    min_free_ = minFree;
  }

  void SetMaxFree(size_t maxFree) {
    max_free_ = maxFree;
  }

  void SetTotalWaitTime(uint64_t param) {
    total_wait_time_ = param;
  }

  uint64_t GetTotalWaitTime() {
    return total_wait_time_;
  }

  void SetMaxAllowedFootPrint(size_t param) {
    max_allowed_footprint_ = param;
  }
#endif
  // Mark stack that we reuse to avoid re-allocating the mark stack.
  UniquePtr<accounting::ATOMIC_OBJ_STACK_T> mark_stack_;

  // Allocation stack, new allocations go here so that we can do sticky mark bits. This enables us
  // to use the live bitmap as the old mark bitmap.
  const size_t max_allocation_stack_size_;
  bool is_allocation_stack_sorted_;
  UniquePtr<accounting::ATOMIC_OBJ_STACK_T> allocation_stack_;

  // Second allocation stack so that we can process allocation with the heap unlocked.
  UniquePtr<accounting::ATOMIC_OBJ_STACK_T> live_stack_;

  // offset of java.lang.ref.Reference.referent
  MemberOffset reference_referent_offset_;

  // offset of java.lang.ref.Reference.queue
  MemberOffset reference_queue_offset_;

  // offset of java.lang.ref.Reference.queueNext
  MemberOffset reference_queueNext_offset_;

  // offset of java.lang.ref.Reference.pendingNext
  MemberOffset reference_pendingNext_offset_;

  // offset of java.lang.ref.FinalizerReference.zombie
  MemberOffset finalizer_reference_zombie_offset_;

#if (ART_GC_SERVICE)


#else
  // Minimum free guarantees that you always have at least min_free_ free bytes after growing for
  // utilization, regardless of target utilization ratio.
  size_t min_free_;

  // The ideal maximum free size, when we grow the heap for utilization.
  size_t max_free_;

  // Target ideal heap utilization ratio
  double target_utilization_;

  // Total time which mutators are paused or waiting for GC to complete.
  uint64_t total_wait_time_;
#endif


  // Total number of objects allocated in microseconds.
  AtomicInteger total_allocation_time_;

  // The current state of heap verification, may be enabled or disabled.
  HeapVerificationMode verify_object_mode_;

  std::vector<collector::MarkSweep*> mark_sweep_collectors_;

  const bool running_on_valgrind_;

  friend class collector::MarkSweep;
  friend class collector::IPCHeap;
  friend class VerifyReferenceCardVisitor;
  friend class VerifyReferenceVisitor;
  friend class VerifyObjectVisitor;
  friend class ScopedHeapLock;
  friend class space::SpaceTest;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Heap);
};

}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_HEAP_H_
