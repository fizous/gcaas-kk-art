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

#ifndef ART_RUNTIME_GC_COLLECTOR_MARK_SWEEP_H_
#define ART_RUNTIME_GC_COLLECTOR_MARK_SWEEP_H_

#include "atomic_integer.h"
#include "barrier.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "garbage_collector.h"
#include "offsets.h"
#include "root_visitor.h"
#include "UniquePtr.h"
#include "gc/space/space.h"


#ifndef ATOMIC_STACK_KLASS
  #if (ART_GC_SERVICE)
    #define ATOMIC_STACK_KLASS    StructuredAtomicStack
    #define ATOMIC_OBJ_STACK_T    StructuredObjectStack
  #else
    #define ATOMIC_STACK_KLASS    AtomicStack
    #define ATOMIC_OBJ_STACK_T    ObjectStack
  #endif
#endif






namespace art {

namespace mirror {
  class Class;
  class Object;
  template<class T> class ObjectArray;
}  // namespace mirror

class StackVisitor;
class Thread;

namespace gc {

namespace accounting {
  template <typename T> class ATOMIC_STACK_KLASS;
  class MarkIfReachesAllocspaceVisitor;
  class ModUnionClearCardVisitor;
  class ModUnionVisitor;
  class ModUnionTableBitmap;
  class MarkStackChunk;
  typedef ATOMIC_STACK_KLASS<mirror::Object*> ATOMIC_OBJ_STACK_T;
#if (ART_GC_SERVICE)
#define SPACE_BITMAP BaseBitmap
#ifndef ABSTRACT_CONTINUOUS_SPACE_T
#define ABSTRACT_CONTINUOUS_SPACE_T ContinuousSpace
#endif
#else
#define SPACE_BITMAP SpaceBitmap
#endif
  class SPACE_BITMAP;
}  // namespace accounting

namespace space {
  class ABSTRACT_CONTINUOUS_SPACE_T;
}  // namespace space

class Heap;

namespace collector {







class MarkSweep : public GarbageCollector {
 public:
#if (ART_GC_SERVICE)

  explicit MarkSweep(Heap* heap, bool is_concurrent,
      space::GCSrvceCashedReferences* cashed_reference_record =
          (space::GCSrvceCashedReferences*) calloc(1, sizeof(space::GCSrvceCashedReferences)),
          space::GCSrvceCashedStatsCounters* stats_record =
              (space::GCSrvceCashedStatsCounters*) calloc(1, sizeof(space::GCSrvceCashedStatsCounters)),
      const std::string& name_prefix = "");
#else
  explicit MarkSweep(Heap* heap, bool is_concurrent,
      const std::string& name_prefix = "");

#endif
  ~MarkSweep() {}

  virtual void InitializePhase();
  virtual bool IsConcurrent() const;
  virtual bool HandleDirtyObjectsPhase() EXCLUSIVE_LOCKS_REQUIRED(Locks::mutator_lock_);
  virtual void MarkingPhase() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  virtual void ReclaimPhase() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  virtual void FinishPhase();
  virtual void ClearMarkHolders(void);
  virtual void ApplyTrimming();
  virtual void MarkReachableObjects()
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  virtual GcType GetGcType() const {
    return kGcTypeFull;
  }
  virtual bool IsInterprocess() const {
    return false;
  }

  // Initializes internal structures.
  void Init();

  // Find the default mark bitmap.
  void FindDefaultMarkBitmap();

  // Marks the root set at the start of a garbage collection.
  void MarkRoots()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void MarkNonThreadRoots()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void MarkConcurrentRoots();
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void MarkRootsCheckpoint(Thread* self)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Verify that image roots point to only marked objects within the alloc space.
  void VerifyImageRoots()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Builds a mark stack and recursively mark until it empties.
  virtual void RecursiveMark()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Make a space immune, immune spaces have all live objects marked - that is the mark and
  // live bitmaps are bound together.
  void ImmuneSpace(space::ABSTRACT_CONTINUOUS_SPACE_T* space)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Bind the live bits to the mark bits of bitmaps for spaces that are never collected, ie
  // the image. Mark that portion of the heap as immune.
  virtual void BindBitmaps() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void BindLiveToMarkBitmap(space::ABSTRACT_CONTINUOUS_SPACE_T* space)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void UnBindBitmaps()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Builds a mark stack with objects on dirty cards and recursively mark until it empties.
  void RecursiveMarkDirtyObjects(bool paused, byte minimum_age)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Remarks the root set after completing the concurrent mark.
  void ReMarkRoots()
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void ResetCollectorStats(void);

  void ProcessReferences(Thread* self)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Sweeps unmarked objects to complete the garbage collection.
  virtual void Sweep(bool swap_bitmaps) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Sweeps unmarked objects to complete the garbage collection.
  void SweepLargeObjects(bool swap_bitmaps) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Sweep only pointers within an array. WARNING: Trashes objects.
  void SweepArray(accounting::ATOMIC_OBJ_STACK_T* allocation_stack_, bool swap_bitmaps)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);



  // Proxy for external access to ScanObject.
  void ScanRoot(const mirror::Object* obj)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Blackens an object.
  void ScanObject(const mirror::Object* obj)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // TODO: enable thread safety analysis when in use by multiple worker threads.
  template <typename MarkVisitor>
  void ScanObjectVisit(const mirror::Object* obj, const MarkVisitor& visitor)
      NO_THREAD_SAFETY_ANALYSIS;





#if (ART_GC_SERVICE)
  size_t GetFreedBytes() const {
    return stats_counters_->freed_bytes_;
  }
  void IncFreedBytes(size_t val) {
    android_atomic_add(static_cast<int32_t>(val), &(stats_counters_->freed_bytes_));
  }

  size_t GetFreedLargeObjectBytes() const {
    return stats_counters_->freed_large_object_bytes_;
  }

  void IncFreedLargeObjectBytes(size_t val) {
    android_atomic_add(static_cast<int32_t>(val), &(stats_counters_->freed_large_object_bytes_));
  }

  void IncFreedObjects(size_t val) {
    android_atomic_add(static_cast<int32_t>(val), &(stats_counters_->freed_objects_));
  }

  size_t GetFreedObjects() const {
    return stats_counters_->freed_objects_;
  }

  size_t GetFreedLargeObjects() const {
    return stats_counters_->freed_large_objects_;
  }

  void IncFreedLargeObjects(size_t val) {
    android_atomic_add(static_cast<int32_t>(val), &(stats_counters_->freed_large_objects_));
  }



  void IncTotalTimeNs(uint64_t param) {
    time_stats_->total_time_ns_ += param;
  }

  void IncTotalPausedTimeNs(uint64_t param) {
    time_stats_->total_paused_time_ns_ += param;
  }

  void IncTotalFreedObjects(uint64_t param) {
    time_stats_->total_freed_objects_ += param;
  }

  void IncTotalFreedBytes(uint64_t param) {
    time_stats_->total_freed_bytes_ += param;
  }

  uint64_t GetTotalTimeNs() const {
    return time_stats_->total_time_ns_;
  }

  uint64_t GetTotalPausedTimeNs() const {
    return time_stats_->total_paused_time_ns_;
  }

  uint64_t GetTotalFreedObjects() const {
    return time_stats_->total_freed_objects_;
  }

  uint64_t GetTotalFreedBytes() const {
    return time_stats_->total_freed_bytes_;
  }
  virtual mirror::Object* GetImmuneBegin() const{
    return cashed_references_record_->immune_begin_;
  }

  virtual  mirror::Object* GetImmuneEnd() const {
    return cashed_references_record_->immune_end_;
  }

  // Immune range, every object inside the immune range is assumed to be marked.



  mirror::Object** GetSoftReferenceList() {
    return &cashed_references_record_->soft_reference_list_;
  }

  mirror::Object** GetWeakReferenceList() {
    return &cashed_references_record_->weak_reference_list_;
  }

  mirror::Object** GetFinalizerReferenceList() {
    return &cashed_references_record_->finalizer_reference_list_;
  }


  mirror::Object** GetPhantomReferenceList() {
    return &cashed_references_record_->phantom_reference_list_;
  }


  mirror::Object** GetClearedReferenceList() {
    return &cashed_references_record_->cleared_reference_list_;
  }

  mirror::Class* GetCachedJavaLangClass(void) const{
    return cashed_references_record_->java_lang_Class_;
  }

  void SetSoftReferenceList(mirror::Object* obj) {
    cashed_references_record_->soft_reference_list_ = obj;
  }

  void SetWeakReferenceList(mirror::Object* obj) {
    cashed_references_record_->weak_reference_list_ = obj;
  }

  void SetFinalizerReferenceList(mirror::Object* obj) {
    cashed_references_record_->finalizer_reference_list_ = obj;
  }


  void SetPhantomReferenceList(mirror::Object* obj) {
    cashed_references_record_->phantom_reference_list_ = obj;
  }


  void SetClearedReferenceList(mirror::Object* obj) {
    cashed_references_record_->cleared_reference_list_ = obj;
  }

  void SetCachedJavaLangClass(mirror::Class* address) {
    cashed_references_record_->java_lang_Class_ = address;
  }
#else
  size_t GetFreedBytes() const {
    return freed_bytes_;
  }
  void IncFreedBytes(size_t val) {
    freed_bytes_.fetch_add(val);
  }

  size_t GetFreedLargeObjectBytes() const {
    return freed_large_object_bytes_;
  }

  void IncFreedLargeObjectBytes(size_t val) {
    freed_large_object_bytes_.fetch_add(val);
  }

  void IncFreedObjects(size_t val) {
    freed_objects_.fetch_add(val);
  }

  size_t GetFreedObjects() const {
    return freed_objects_;
  }

  size_t GetFreedLargeObjects() const {
    return freed_large_objects_;
  }

  void IncFreedLargeObjects(size_t val) {
    freed_large_objects_.fetch_add(val);
  }




  void IncTotalTimeNs(uint64_t param) {
    total_time_ns_ += param;
  }

  void IncTotalPausedTimeNs(uint64_t param) {
    total_paused_time_ns_ += param;
  }

  void IncTotalFreedObjects(uint64_t param) {
    total_freed_objects_ += param;
  }

  void IncTotalFreedBytes(uint64_t param) {
    total_freed_bytes_ += param;
  }

  uint64_t GetTotalTimeNs() const {
    return total_time_ns_;
  }

  uint64_t GetTotalPausedTimeNs() const {
    return total_paused_time_ns_;
  }

  uint64_t GetTotalFreedObjects() const {
    return total_freed_objects_;
  }

  uint64_t GetTotalFreedBytes() const {
    return total_freed_bytes_;
  }

  virtual mirror::Object* GetImmuneBegin() const{
    return immune_begin_;
  }

  virtual  mirror::Object* GetImmuneEnd() const {
    return immune_end_;
  }

  // Immune range, every object inside the immune range is assumed to be marked.



  mirror::Object** GetSoftReferenceList() {
    return &soft_reference_list_;
  }

  mirror::Object** GetWeakReferenceList() {
    return &weak_reference_list_;
  }

  mirror::Object** GetFinalizerReferenceList() {
    return &finalizer_reference_list_;
  }


  mirror::Object** GetPhantomReferenceList() {
    return &phantom_reference_list_;
  }


  mirror::Object** GetClearedReferenceList() {
    return &cleared_reference_list_;
  }

  mirror::Class* GetCachedJavaLangClass(void) const{
    return java_lang_Class_;
  }

  void SetSoftReferenceList(mirror::Object* obj) {
    soft_reference_list_ = obj;
  }

  void SetWeakReferenceList(mirror::Object* obj) {
    weak_reference_list_ = obj;
  }

  void SetFinalizerReferenceList(mirror::Object* obj) {
    finalizer_reference_list_ = obj;
  }


  void SetPhantomReferenceList(mirror::Object* obj) {
    phantom_reference_list_ = obj;
  }


  void SetClearedReferenceList(mirror::Object* obj) {
    cleared_reference_list_ = obj;
  }

  void SetCachedJavaLangClass(mirror::Class* address) {
    java_lang_Class_ = address;
  }

#endif



  // Everything inside the immune range is assumed to be marked.
  virtual void SetImmuneRange(mirror::Object* begin, mirror::Object* end);

  void SweepSystemWeaks()
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  static bool VerifyIsLiveCallback(const mirror::Object* obj, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void VerifySystemWeaks()
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Verify that an object is live, either in a live bitmap or in the allocation stack.
  void VerifyIsLive(const mirror::Object* obj)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  template <typename Visitor>
  static void VisitObjectReferences(const mirror::Object* obj, const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_,
                            Locks::mutator_lock_);

  static void MarkObjectCallback(const mirror::Object* root, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);
  static void MarkObjectCallbackNoLock(const mirror::Object* root, void* arg);

  static void MarkRootParallelCallback(const mirror::Object* root, void* arg);

  // Marks an object.
  void MarkObject(const mirror::Object* obj)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void MarkRoot(const mirror::Object* obj)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  Barrier& GetBarrier() {
    return *gc_barrier_;
  }

  void SetClearSoftReferences(bool val) {
    clear_soft_references_ = val;
  }

  void ArraysVerifierScan(const mirror::Object* object,
      void* heap_beetmap = NULL);

#if (ART_GC_SERVICE)
  space::GCSrvceCashedStatsCounters* stats_counters_;

  size_t GetClassCount() const{
    return stats_counters_->class_count_;
  }

  size_t GetArrayCount() const{
    return stats_counters_->array_count_;
  }


  size_t GetOtherCount() const{
    return stats_counters_->other_count_;
  }

  void IncCardsScanned(int val) {
    android_atomic_add(val, &stats_counters_->cards_scanned_);
  }


  void SetCardsScanned(int val) {
    stats_counters_->cards_scanned_ = val;
  }

  int32_t GetCardsScanned() {
    return android_atomic_release_load(&stats_counters_->cards_scanned_);
  }


  int32_t GetReferenceCount() {
    return android_atomic_release_load(&stats_counters_->reference_count_);
  }

  void IncClassCount(int val) {
    android_atomic_add(val, &stats_counters_->class_count_);
  }


  void IncReferenceCount(int val) {
    android_atomic_add(val, &stats_counters_->reference_count_);
  }
  void IncArrayCount(int val) {
    android_atomic_add(val, &stats_counters_->array_count_);
  }


  void IncOtherCount(int val) {
    android_atomic_add(val, &stats_counters_->other_count_);
  }
#else


  size_t GetClassCount() const{
     return class_count_.load();
   }

   size_t GetArrayCount() const{
     return array_count_.load();
   }


   size_t GetOtherCount() const{
     return other_count_.load();
   }

   void IncCardsScanned(int val) {
     cards_scanned_.fetch_add(val);
   }


   void SetCardsScanned(int val) {
     cards_scanned_ = val;
   }

   int32_t GetCardsScanned() {
     return cards_scanned_.load();
   }


   int32_t GetReferenceCount() {
     return reference_count_.load();
   }


   void IncClassCount(int val) {
     class_count_.fetch_add(val);
   }



   void IncReferenceCount(int val) {
     reference_count_.fetch_add(val);
   }
   void IncArrayCount(int val) {
     array_count_.fetch_add(val);
   }


   void IncOtherCount(int val) {
     other_count_.fetch_add(val);
   }

#endif


 protected:
  // Returns true if the object has its bit set in the mark bitmap.
  bool IsMarked(const mirror::Object* object) const;
#if (ART_GC_SERVICE)
  // Returns true if the object has its bit set in the mark bitmap.
  bool IsMarkedNoLocks(const mirror::Object* object,
      void* heap_beetmap = NULL) const;
#else
  // Returns true if the object has its bit set in the mark bitmap.
  bool IsMarkedNoLocks(const mirror::Object* object,
      void* heap_beetmap = NULL) const;
#endif
  static bool IsMarkedCallback(const mirror::Object* object, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  static bool IsMarkedArrayCallback(const mirror::Object* object, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  static void ReMarkObjectVisitor(const mirror::Object* root, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  static void VerifyImageRootVisitor(mirror::Object* root, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_,
                            Locks::mutator_lock_);

  void MarkObjectNonNull(const mirror::Object* obj)
        SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
        EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Unmarks an object by clearing the bit inside of the corresponding bitmap, or if it is in a
  // space set, removing the object from the set.
  void UnMarkObjectNonNull(const mirror::Object* obj)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Mark the vm thread roots.
  virtual void MarkThreadRoots(Thread* self)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Marks an object atomically, safe to use from multiple threads.
  void MarkObjectNonNullParallel(const mirror::Object* obj);

  // Marks or unmarks a large object based on whether or not set is true. If set is true, then we
  // mark, otherwise we unmark.
  bool MarkLargeObject(const mirror::Object* obj, bool set)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Returns true if we need to add obj to a mark stack.
  bool MarkObjectParallel(const mirror::Object* obj) NO_THREAD_SAFETY_ANALYSIS;

  static void SweepCallback(size_t num_ptrs, mirror::Object** ptrs, void* arg)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Special sweep for zygote that just marks objects / dirties cards.
  static void ZygoteSweepCallback(size_t num_ptrs, mirror::Object** ptrs, void* arg)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  void CheckReference(const mirror::Object* obj, const mirror::Object* ref, MemberOffset offset,
                      bool is_static)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  void CheckObject(const mirror::Object* obj)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  // Verify the roots of the heap and print out information related to any invalid roots.
  // Called in MarkObject, so may we may not hold the mutator lock.
  void VerifyRoots()
      NO_THREAD_SAFETY_ANALYSIS;

  // Expand mark stack to 2x its current size.
  void ExpandMarkStack() EXCLUSIVE_LOCKS_REQUIRED(mark_stack_lock_);
  void ResizeMarkStack(size_t new_size) EXCLUSIVE_LOCKS_REQUIRED(mark_stack_lock_);

  // Returns how many threads we should use for the current GC phase based on if we are paused,
  // whether or not we care about pauses.
  size_t GetThreadCount(bool paused) const;

  // Returns true if an object is inside of the immune region (assumed to be marked).
  bool IsImmune(const mirror::Object* obj) const {
    return obj >= GetImmuneBegin() && obj < GetImmuneEnd();
  }

  static void VerifyRootCallback(const mirror::Object* root, void* arg, size_t vreg,
                                 const StackVisitor *visitor);

  void VerifyRoot(const mirror::Object* root, size_t vreg, const StackVisitor* visitor)
      NO_THREAD_SAFETY_ANALYSIS;

  template <typename Visitor>
  static void VisitInstanceFieldsReferences(const mirror::Class* klass, const mirror::Object* obj,
                                            const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  // Visit the header, static field references, and interface pointers of a class object.
  template <typename Visitor>
  static void VisitClassReferences(const mirror::Class* klass, const mirror::Object* obj,
                                   const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  template <typename Visitor>
  static void VisitStaticFieldsReferences(const mirror::Class* klass, const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  template <typename Visitor>
  static void VisitFieldsReferences(const mirror::Object* obj, uint32_t ref_offsets, bool is_static,
                                    const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  // Visit all of the references in an object array.
  template <typename Visitor>
  static void VisitObjectArrayReferences(const mirror::ObjectArray<mirror::Object>* array,
                                         const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  // Visits the header and field references of a data object.
  template <typename Visitor>
  static void VisitOtherReferences(const mirror::Class* klass, const mirror::Object* obj,
                                   const Visitor& visitor)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_) {
    return VisitInstanceFieldsReferences(klass, obj, visitor);
  }

  // Blackens objects grayed during a garbage collection.
  void ScanGrayObjects(bool paused, byte minimum_age)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Schedules an unmarked object for reference processing.
  void DelayReferenceReferent(mirror::Class* klass, mirror::Object* reference)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  // Recursively blackens objects on the mark stack.
  virtual void ProcessMarkStack(bool paused)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  virtual void ProcessMarkStackParallel(size_t thread_count)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void EnqueueFinalizerReferences(mirror::Object** ref)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void PreserveSomeSoftReferences(mirror::Object** ref)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void ClearWhiteReferences(mirror::Object** list)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_, Locks::mutator_lock_);

  void ProcessReferences(mirror::Object** soft_references, bool clear_soft_references,
                         mirror::Object** weak_references,
                         mirror::Object** finalizer_references,
                         mirror::Object** phantom_references)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void SweepJniWeakGlobals(IsMarkedTester is_marked, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Whether or not we count how many of each type of object were scanned.
  static const bool kCountScannedTypes = false;

  // Current space, we check this space first to avoid searching for the appropriate space for an
  // object.
  accounting::SPACE_BITMAP* current_mark_bitmap_;
#if (ART_GC_SERVICE)
#else
  // Cache java.lang.Class for optimization.
  mirror::Class* java_lang_Class_;
#endif
  accounting::ATOMIC_OBJ_STACK_T* mark_stack_;


  // Parallel finger.
  AtomicInteger atomic_finger_;


#if (ART_GC_SERVICE)
#else

  // Immune range, every object inside the immune range is assumed to be marked.
  mirror::Object* immune_begin_;
  mirror::Object* immune_end_;

  mirror::Object* soft_reference_list_;
  mirror::Object* weak_reference_list_;
  mirror::Object* finalizer_reference_list_;
  mirror::Object* phantom_reference_list_;
  mirror::Object* cleared_reference_list_;



  // Number of non large object bytes freed in this collection.
  AtomicInteger freed_bytes_;
  // Number of large object bytes freed.
  AtomicInteger freed_large_object_bytes_;
  // Number of objects freed in this collection.
  AtomicInteger freed_objects_;
  // Number of freed large objects.
  AtomicInteger freed_large_objects_;
  // Number of classes scanned, if kCountScannedTypes.
  AtomicInteger class_count_;
  // Number of arrays scanned, if kCountScannedTypes.
  AtomicInteger array_count_;
  // Number of non-class/arrays scanned, if kCountScannedTypes.
  AtomicInteger other_count_;
  AtomicInteger reference_count_;
  AtomicInteger cards_scanned_;
#endif
  AtomicInteger large_object_test_;
  AtomicInteger large_object_mark_;
  AtomicInteger classes_marked_;
  AtomicInteger overhead_time_;
  AtomicInteger work_chunks_created_;
  AtomicInteger work_chunks_deleted_;


  // Verification.
  size_t live_stack_freeze_size_;

  UniquePtr<Barrier> gc_barrier_;
  Mutex large_object_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  Mutex mark_stack_lock_ ACQUIRED_AFTER(Locks::classlinker_classes_lock_);

  const bool is_concurrent_;
  bool clear_soft_references_;

#if (ART_GC_SERVICE)
  space::GCSrvceCashedReferences* cashed_references_record_;
#else
#endif


 private:
  friend class AddIfReachesAllocSpaceVisitor;  // Used by mod-union table.
  friend class CardScanTask;
  friend class CheckBitmapVisitor;
  friend class CheckReferenceVisitor;
  friend class art::gc::Heap;
  friend class InternTableEntryIsUnmarked;
  friend class MarkIfReachesAllocspaceVisitor;
  friend class ModUnionCheckReferences;
  friend class ModUnionClearCardVisitor;
  friend class ModUnionReferenceVisitor;
  friend class ModUnionVisitor;
  friend class ModUnionTableBitmap;
  friend class ModUnionTableReferenceCache;
  friend class ModUnionScanImageRootVisitor;
  friend class ScanBitmapVisitor;
  friend class ScanImageRootVisitor;
  template<bool kUseFinger> friend class MarkStackTask;
  friend class FifoMarkStackChunk;

  DISALLOW_COPY_AND_ASSIGN(MarkSweep);
};


struct SweepCallbackContext {
  MarkSweep* mark_sweep;
  space::AllocSpace* space;
  Thread* self;
};


}  // namespace collector
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_COLLECTOR_MARK_SWEEP_H_
