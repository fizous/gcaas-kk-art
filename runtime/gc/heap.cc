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

#include "heap.h"

#define ATRACE_TAG ATRACE_TAG_DALVIK
#include <cutils/trace.h>

#include <limits>
#include <vector>
#include <valgrind.h>

#include "base/stl_util.h"
#include "common_throws.h"
#include "cutils/sched_policy.h"
#include "debugger.h"
#include "gc/accounting/atomic_stack.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/accounting/heap_bitmap-inl.h"
#include "gc/accounting/mod_union_table-inl.h"
#include "gc/accounting/space_bitmap-inl.h"
#include "gc/collector/mark_sweep-inl.h"
#include "gc/collector/partial_mark_sweep.h"
#include "gc/collector/sticky_mark_sweep.h"
#include "gc/space/dlmalloc_space-inl.h"
#include "gc/space/image_space.h"
#include "gc/space/large_object_space.h"
#include "gc/space/space-inl.h"
#include "image.h"
#include "invoke_arg_array_builder.h"
#include "mirror/art_field-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "object_utils.h"
#include "os.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "sirt_ref.h"
#include "thread_list.h"
#include "UniquePtr.h"
#include "well_known_classes.h"
#include "gc_profiler/MProfiler.h"
#include "gc_profiler/MProfilerTypes.h"

#if (ART_GC_SERVICE)
#include "service/global_allocator.h"
#include "service/service_space.h"
#include "service/service_client.h"

using ::art::gc::service::GCServiceGlobalAllocator;
using ::art::gc::service::GCServiceClient;
#endif

namespace art {
namespace gc {

static constexpr bool kGCALotMode = false;
static constexpr size_t kGcAlotInterval = KB;
static constexpr bool kDumpGcPerformanceOnShutdown = false;
// Minimum amount of remaining bytes before a concurrent GC is triggered.
static constexpr size_t kMinConcurrentRemainingBytes = 128 * KB;
// If true, measure the total allocation time.
static constexpr bool kMeasureAllocationTime = false;

Heap::Heap(size_t initial_size, size_t growth_limit, size_t min_free, size_t max_free,
           double target_utilization, size_t capacity, const std::string& original_image_file_name,
           bool concurrent_gc, size_t parallel_gc_threads, size_t conc_gc_threads,
           bool low_memory_mode, size_t long_pause_log_threshold, size_t long_gc_log_threshold,
           bool ignore_max_footprint)
    : alloc_space_(NULL),
      card_table_(NULL),
      concurrent_gc_(concurrent_gc),
      parallel_gc_threads_(parallel_gc_threads),
      conc_gc_threads_(conc_gc_threads),
      low_memory_mode_(low_memory_mode),
      long_pause_log_threshold_(long_pause_log_threshold),
      long_gc_log_threshold_(long_gc_log_threshold),
      ignore_max_footprint_(ignore_max_footprint),
      have_zygote_space_(false),
      soft_ref_queue_lock_(NULL),
      weak_ref_queue_lock_(NULL),
      finalizer_ref_queue_lock_(NULL),
      phantom_ref_queue_lock_(NULL),
      is_gc_running_(false),

#if (ART_GC_SERVICE)
#else
      last_gc_type_(collector::kGcTypeNone),
      next_gc_type_(collector::kGcTypePartial),
#endif
      capacity_(capacity),
#if (ART_GC_SERVICE)
#else
      growth_limit_(growth_limit),
      max_allowed_footprint_(initial_size),
      native_footprint_gc_watermark_(initial_size),
      native_footprint_limit_(2 * initial_size),
#endif

      activity_thread_class_(NULL),
      application_thread_class_(NULL),
      activity_thread_(NULL),
      application_thread_(NULL),
      last_process_state_id_(NULL),
      // Initially care about pauses in case we never get notified of process states, or if the JNI
      // code becomes broken.
      care_about_pause_times_(true),
#if (ART_GC_SERVICE)
#else
      concurrent_start_bytes_(concurrent_gc_ ? initial_size - kMinConcurrentRemainingBytes
          :  std::numeric_limits<size_t>::max()),
      total_bytes_freed_ever_(0),
      total_objects_freed_ever_(0),
#endif
      large_object_threshold_(GC_HEAP_LARGE_OBJECT_THRESHOLD),
#if (ART_GC_SERVICE)
#else
      num_bytes_allocated_(0),
      native_bytes_allocated_(0),
#endif
      gc_memory_overhead_(0),
      verify_missing_card_marks_(false),
      verify_system_weaks_(false),
      verify_pre_gc_heap_(false),
      verify_post_gc_heap_(false),
      verify_mod_union_table_(false),
      min_alloc_space_size_for_sticky_gc_(2 * MB),
      min_remaining_space_for_sticky_gc_(1 * MB),
#if (ART_GC_SERVICE)
      sub_record_meta_(reinterpret_cast<space::GCSrvcHeapSubRecord*>(calloc(1, sizeof(space::GCSrvcHeapSubRecord)))),
#else
      last_trim_time_ms_(0),
      allocation_rate_(0),
#endif
      /* For GC a lot mode, we limit the allocations stacks to be kGcAlotInterval allocations. This
       * causes a lot of GC since we do a GC for alloc whenever the stack is full. When heap
       * verification is enabled, we limit the size of allocation stacks to speed up their
       * searching.
       */
      max_allocation_stack_size_(kGCALotMode ? kGcAlotInterval
          : (kDesiredHeapVerification > kNoHeapVerification) ? KB : MB),
      reference_referent_offset_(0),
      reference_queue_offset_(0),
      reference_queueNext_offset_(0),
      reference_pendingNext_offset_(0),
      finalizer_reference_zombie_offset_(0),
#if (ART_GC_SERVICE)
#else
      min_free_(min_free),
      max_free_(max_free),
      target_utilization_(target_utilization),
      total_wait_time_(0),
#endif
      total_allocation_time_(0),
      verify_object_mode_(kHeapVerificationNotPermitted),
      running_on_valgrind_(RUNNING_ON_VALGRIND) {
  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
    LOG(INFO) << "Heap() entering";
  }
#if (ART_GC_SERVICE)
  SetLastTimeTrim(0);
  SetAllocationRate(0);
  SetTotalBytesFreedEver(0);
  SetTotalObjectsFreedEver(0);
  SetGrowthLimit(growth_limit);
  SetConcStartBytes(concurrent_gc_ ? initial_size - kMinConcurrentRemainingBytes
          :  std::numeric_limits<size_t>::max());
  SetTargetUtilization(target_utilization);
  SetMinFree(min_free);
  SetMaxFree(max_free);
  SetTotalWaitTime(0);
  SetNextGCType(collector::kGcTypePartial);
  SetLastGCType(collector::kGcTypeNone);
  SetNumBytesAllocated(0);
  SetNativeBytesAllocated(0);
  SetMaxAllowedFootPrint(initial_size);
  SetNativeFootPrintGCWaterMark(initial_size);
  SetNativeFootPrintLimit(2 * initial_size);

//  LOG(ERROR) << "the runtime is a compiler ? " <<
//      Runtime::Current()->IsCompiler() << ", parentID: " << getppid();
  if(Runtime::Current()->IsZygote() && !Runtime::Current()->IsCompiler()) {
    //LOG(ERROR) << "Zygote Process: We will initialize the Global Allocator now";
    GCServiceGlobalAllocator::CreateServiceAllocator();
   // LOG(ERROR) << "Zygote: Done initializing with global allocator";
  }
  if(Runtime::Current()->IsCompiler()) {
    live_bitmap_.reset(new accounting::HeapBitmap(/*this*/));
    mark_bitmap_.reset(new accounting::HeapBitmap(/*this*/));
  } else {
    live_bitmap_.reset(accounting::BaseHeapBitmap::CreateHeapBitmap(/*this,*/
        (!Runtime::Current()->IsCompiler()) && GC_SERVICE_SHARABLE_HEAP_BITMAP));
    mark_bitmap_.reset(accounting::BaseHeapBitmap::CreateHeapBitmap(/*this,*/
        (!Runtime::Current()->IsCompiler()) && GC_SERVICE_SHARABLE_HEAP_BITMAP));
  }

#else
  live_bitmap_.reset(new accounting::HeapBitmap(this));
  mark_bitmap_.reset(new accounting::HeapBitmap(this));
#endif
  // Requested begin for the alloc space, to follow the mapped image and oat files
  byte* requested_alloc_space_begin = NULL;
  std::string image_file_name(original_image_file_name);
  if (!image_file_name.empty()) {
    space::ImageSpace* image_space = space::ImageSpace::Create(image_file_name);
    CHECK(image_space != NULL) << "Failed to create space for " << image_file_name;
//    GCMMP_VLOG(INFO) << "HeapCreation: opening image file: " << image_file_name;
    AddContinuousSpace(image_space);
    // Oat files referenced by image files immediately follow them in memory, ensure alloc space
    // isn't going to get in the middle
    byte* oat_file_end_addr = image_space->GetImageHeader().GetOatFileEnd();
    CHECK_GT(oat_file_end_addr, image_space->End());
    if (oat_file_end_addr > requested_alloc_space_begin) {
      requested_alloc_space_begin =
          reinterpret_cast<byte*>(RoundUp(reinterpret_cast<uintptr_t>(oat_file_end_addr),
                                          kPageSize));
    }
  } else {
//  	GCMMP_VLOG(INFO) << "HeapCreation: image file name was empty: ";
  }

  alloc_space_ = space::DlMallocSpace::Create(Runtime::Current()->IsZygote() ?
                                              "zygote space" : "alloc space",
                                              initial_size,
                                              growth_limit, capacity,
                                              requested_alloc_space_begin);
  CHECK(alloc_space_ != NULL) << "Failed to create alloc space";
  alloc_space_->SetFootprintLimit(alloc_space_->Capacity());
  AddContinuousSpace(alloc_space_);

  //DumpSpaces();
  // Allocate the large object space.
  if(!GC_HEAP_SRVCE_NO_LOS) {
    const bool kUseFreeListSpaceForLOS = false;
    if (kUseFreeListSpaceForLOS) {
      large_object_space_ = space::FreeListSpace::Create("large object space", NULL, capacity);
    } else {
      large_object_space_ = space::LargeObjectMapSpace::Create("large object space");
    }
    CHECK(large_object_space_ != NULL) << "Failed to create large object space";
    AddDiscontinuousSpace(large_object_space_);
  } else {
    large_object_space_ = NULL;
  }

  // Compute heap capacity. Continuous spaces are sorted in order of Begin().
  byte* heap_begin = continuous_spaces_.front()->Begin();
  size_t heap_capacity = continuous_spaces_.back()->End() - continuous_spaces_.front()->Begin();
  if (continuous_spaces_.back()->IsDlMallocSpace()) {
    heap_capacity += continuous_spaces_.back()->AsDlMallocSpace()->NonGrowthLimitCapacity();
  }

  // Allocate the card table.
  card_table_.reset(accounting::CARD_TABLE::Create(heap_begin, heap_capacity));
  CHECK(card_table_.get() != NULL) << "Failed to create card table";

  image_mod_union_table_.reset(new accounting::ModUnionTableToZygoteAllocspace(this));
  CHECK(image_mod_union_table_.get() != NULL) << "Failed to create image mod-union table";

  zygote_mod_union_table_.reset(new accounting::ModUnionTableCardCache(this));
  CHECK(zygote_mod_union_table_.get() != NULL) << "Failed to create Zygote mod-union table";

  // TODO: Count objects in the image space here.
  SetNumBytesAllocated(0);

  // Default mark stack size in bytes.
  static const size_t default_mark_stack_size = /*128 * KB*/ 64 * KB;
  mark_stack_.reset(accounting::ATOMIC_OBJ_STACK_T::Create("mark stack", default_mark_stack_size,
      false && (!Runtime::Current()->IsCompiler()) && (ART_GC_SERVICE)));
  allocation_stack_.reset(accounting::ATOMIC_OBJ_STACK_T::Create("allocation stack",
                                                          max_allocation_stack_size_,
       false &&       (!Runtime::Current()->IsCompiler()) &&       (ART_GC_SERVICE)));
  live_stack_.reset(accounting::ATOMIC_OBJ_STACK_T::Create("live stack",
                                                    max_allocation_stack_size_,
       false && (!Runtime::Current()->IsCompiler()) &&    (ART_GC_SERVICE)));

  // It's still too early to take a lock because there are no threads yet, but we can create locks
  // now. We don't create it earlier to make it clear that you can't use locks during heap
  // initialization.
  gc_complete_lock_ = new Mutex("GC complete lock");
  gc_complete_cond_.reset(new ConditionVariable("GC complete condition variable",
                                                *gc_complete_lock_));

  // Create the reference queue locks, this is required so for parallel object scanning in the GC.
  soft_ref_queue_lock_ = new Mutex("Soft reference queue lock");
  weak_ref_queue_lock_ = new Mutex("Weak reference queue lock");
  finalizer_ref_queue_lock_ = new Mutex("Finalizer reference queue lock");
  phantom_ref_queue_lock_ = new Mutex("Phantom reference queue lock");

  SetLastGCTime(NanoTime());
  SetLastGCSize(GetBytesAllocated());

  if (ignore_max_footprint_) {
    SetIdealFootprint(std::numeric_limits<size_t>::max());
    SetConcStartBytes(GetMaxAllowedFootPrint());
  }

  // Create our garbage collectors.
  for (size_t i = 0; i < 2; ++i) {
    const bool concurrent = i != 0;
    mark_sweep_collectors_.push_back(new collector::MarkSweep(this, concurrent));
    mark_sweep_collectors_.push_back(new collector::PartialMarkSweep(this, concurrent));
    mark_sweep_collectors_.push_back(new collector::StickyMarkSweep(this, concurrent));
  }

  CHECK_NE(GetMaxAllowedFootPrint(), 0U);
  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
    LOG(INFO) << "Heap() exiting";
  }
}

void Heap::CreateThreadPool() {
  const size_t num_threads = std::max(parallel_gc_threads_, conc_gc_threads_);
  if (num_threads != 0) {
    thread_pool_.reset(new ThreadPool(num_threads));
  }
//  byte* zygote_space_begin =  alloc_space_->Begin();
//  byte* zygote_space_end =  alloc_space_->End();
////  LOG(ERROR) << "+++ Zygotebegin = " << reinterpret_cast<void*>(zygote_space_begin)
////      << "+++ Zygoteend = " << reinterpret_cast<void*>(zygote_space_end);
//  byte* image_space_begin =  GetImageSpace()->Begin();
//  byte* image_space_end =  GetImageSpace()->End();


//  LOG(ERROR) << "FinalizeHeapAfterInit --> imagespaceBegin: "
//      << reinterpret_cast<void*>(image_space_begin)
//      << ", imageSpaceEnd: " << reinterpret_cast<void*>(image_space_end);
}

void Heap::DeleteThreadPool() {
  thread_pool_.reset(nullptr);
}

static bool ReadStaticInt(JNIEnvExt* env, jclass clz, const char* name,
                                                              int* out_value) {
  CHECK(out_value != NULL);
  jfieldID field = env->GetStaticFieldID(clz, name, "I");
  if (field == NULL) {
    env->ExceptionClear();
    return false;
  }
  *out_value = env->GetStaticIntField(clz, field);
  return true;
}



void Heap::initializeProcessStateObjects(void) {
  Thread* self = Thread::Current();
  JNIEnvExt* env = self->GetJniEnv();

  jclass clz = env->FindClass("android/app/ActivityThread");
  if (clz == NULL) {
    env->ExceptionClear();
    LOG(WARNING) << "Could not find activity thread class in process state change";
    return;
  }
  activity_thread_class_ = reinterpret_cast<jclass>(env->NewGlobalRef(clz));

  if (activity_thread_class_ != NULL && activity_thread_ == NULL) {
    jmethodID current_activity_method = env->GetStaticMethodID(activity_thread_class_,
                                                               "currentActivityThread",
                                                               "()Landroid/app/ActivityThread;");
    if (current_activity_method == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get method for currentActivityThread";
      return;
    }

    jobject obj = env->CallStaticObjectMethod(activity_thread_class_, current_activity_method);
    if (obj == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get current activity";
      return;
    }
    activity_thread_ = env->NewGlobalRef(obj);
  }

  if (activity_thread_class_ != NULL && activity_thread_ == NULL) {
    jmethodID current_activity_method = env->GetStaticMethodID(activity_thread_class_,
                                                               "currentActivityThread",
                                                               "()Landroid/app/ActivityThread;");
    if (current_activity_method == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get method for currentActivityThread";
      return;
    }

    jobject obj = env->CallStaticObjectMethod(activity_thread_class_, current_activity_method);
    if (obj == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get current activity";
      return;
    }
    activity_thread_ = env->NewGlobalRef(obj);
  }

  if (application_thread_class_ == NULL) {
    jclass clz = env->FindClass("android/app/ActivityThread$ApplicationThread");
    if (clz == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get application thread class";
      return;
    }
    application_thread_class_ = reinterpret_cast<jclass>(env->NewGlobalRef(clz));
    last_process_state_id_ = env->GetFieldID(application_thread_class_, "mLastProcessState", "I");
    if (last_process_state_id_ == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get last process state member";
      return;
    }
  }

  if (application_thread_class_ != NULL && application_thread_ == NULL) {
    jmethodID get_application_thread =
        env->GetMethodID(activity_thread_class_, "getApplicationThread",
                         "()Landroid/app/ActivityThread$ApplicationThread;");
    if (get_application_thread == NULL) {
      LOG(WARNING) << "Could not get method ID for get application thread";
      return;
    }

    jobject obj = env->CallObjectMethod(activity_thread_, get_application_thread);
    if (obj == NULL) {
      LOG(WARNING) << "Could not get application thread";
      return;
    }

    application_thread_ = env->NewGlobalRef(obj);
  }
}

int Heap::GetLastProcessStateID(void) {
  Thread* self = Thread::Current();
  JNIEnvExt* env = self->GetJniEnv();

  if (!have_zygote_space_) {
    return -1;
  }

  if(activity_thread_class_ == NULL) {
    initializeProcessStateObjects();
  }


  if (application_thread_class_ != NULL && application_thread_ == NULL) {
    jmethodID get_application_thread =
        env->GetMethodID(activity_thread_class_, "getApplicationThread",
                         "()Landroid/app/ActivityThread$ApplicationThread;");
    if (get_application_thread == NULL) {
      LOG(WARNING) << "Could not get method ID for get application thread";
      return -1;
    }

    jobject obj = env->CallObjectMethod(activity_thread_, get_application_thread);
    if (obj == NULL) {
      LOG(WARNING) << "Could not get application thread";
      return -1;
    }

    application_thread_ = env->NewGlobalRef(obj);
  }

  if (application_thread_ != NULL && last_process_state_id_ != NULL) {
    int process_state = env->GetIntField(application_thread_, last_process_state_id_);
    env->ExceptionClear();

    return process_state;
  }
  return -1;
}

void Heap::ListenForProcessStateChange() {
  VLOG(heap) << "Heap notified of process state change";

  Thread* self = Thread::Current();
  JNIEnvExt* env = self->GetJniEnv();

  if (!have_zygote_space_) {
    return;
  }

  if (activity_thread_class_ == NULL) {
    jclass clz = env->FindClass("android/app/ActivityThread");
    if (clz == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not find activity thread class in process state change";
      return;
    }
    activity_thread_class_ = reinterpret_cast<jclass>(env->NewGlobalRef(clz));
  }

  if (activity_thread_class_ != NULL && activity_thread_ == NULL) {
    jmethodID current_activity_method = env->GetStaticMethodID(activity_thread_class_,
                                                               "currentActivityThread",
                                                               "()Landroid/app/ActivityThread;");
    if (current_activity_method == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get method for currentActivityThread";
      return;
    }

    jobject obj = env->CallStaticObjectMethod(activity_thread_class_, current_activity_method);
    if (obj == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get current activity";
      return;
    }
    activity_thread_ = env->NewGlobalRef(obj);
  }

  if (process_state_cares_about_pause_time_.empty()) {
    // Just attempt to do this the first time.
    jclass clz = env->FindClass("android/app/ActivityManager");
    if (clz == NULL) {
      LOG(WARNING) << "Activity manager class is null";
      return;
    }
    ScopedLocalRef<jclass> activity_manager(env, clz);
    std::vector<const char*> care_about_pauses;
    care_about_pauses.push_back("PROCESS_STATE_TOP");
    care_about_pauses.push_back("PROCESS_STATE_IMPORTANT_BACKGROUND");
    // Attempt to read the constants and classify them as whether or not we care about pause times.
    for (size_t i = 0; i < care_about_pauses.size(); ++i) {
      int process_state = 0;
      if (ReadStaticInt(env, activity_manager.get(), care_about_pauses[i], &process_state)) {
        process_state_cares_about_pause_time_.insert(process_state);
        VLOG(heap)<< "Adding process state " << process_state
                   << " to set of states which care about pause time";
      }
    }
  }

  if (application_thread_class_ == NULL) {
    jclass clz = env->FindClass("android/app/ActivityThread$ApplicationThread");
    if (clz == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get application thread class";
      return;
    }
    application_thread_class_ = reinterpret_cast<jclass>(env->NewGlobalRef(clz));
    last_process_state_id_ = env->GetFieldID(application_thread_class_, "mLastProcessState", "I");
    if (last_process_state_id_ == NULL) {
      env->ExceptionClear();
      LOG(WARNING) << "Could not get last process state member";
      return;
    }
  }

  if (application_thread_class_ != NULL && application_thread_ == NULL) {
    jmethodID get_application_thread =
        env->GetMethodID(activity_thread_class_, "getApplicationThread",
                         "()Landroid/app/ActivityThread$ApplicationThread;");
    if (get_application_thread == NULL) {
      LOG(WARNING) << "Could not get method ID for get application thread";
      return;
    }

    jobject obj = env->CallObjectMethod(activity_thread_, get_application_thread);
    if (obj == NULL) {
      LOG(WARNING) << "Could not get application thread";
      return;
    }

    application_thread_ = env->NewGlobalRef(obj);
  }

  if (application_thread_ != NULL && last_process_state_id_ != NULL) {
    int process_state = env->GetIntField(application_thread_, last_process_state_id_);
    env->ExceptionClear();

    care_about_pause_times_ = process_state_cares_about_pause_time_.find(process_state) !=
        process_state_cares_about_pause_time_.end();

    VLOG(heap) << "New process state " << process_state
               << " care about pauses " << care_about_pause_times_;
  }
}

void Heap::AddContinuousSpace(space::ABSTRACT_CONTINUOUS_SPACE_T* space) {
  WriterMutexLock mu(Thread::Current(), *Locks::heap_bitmap_lock_);
  DCHECK(space != NULL);
  DCHECK(space->GetLiveBitmap() != NULL);
  live_bitmap_->AddContinuousSpaceBitmap(space->GetLiveBitmap());
  DCHECK(space->GetMarkBitmap() != NULL);
  mark_bitmap_->AddContinuousSpaceBitmap(space->GetMarkBitmap());
  continuous_spaces_.push_back(space);
  if (space->IsDlMallocSpace() && !space->IsLargeObjectSpace()) {
    alloc_space_ = space->AsDlMallocSpace();
  }

  // Ensure that spaces remain sorted in increasing order of start address (required for CMS finger)
  std::sort(continuous_spaces_.begin(), continuous_spaces_.end(),
            [](const space::ABSTRACT_CONTINUOUS_SPACE_T* a,
                            const space::ABSTRACT_CONTINUOUS_SPACE_T* b) {
              return a->Begin() < b->Begin();
            });

  // Ensure that ImageSpaces < ZygoteSpaces < AllocSpaces so that we can do address based checks to
  // avoid redundant marking.
  bool seen_zygote = false, seen_alloc = false;
  for (const auto& space : continuous_spaces_) {
    if (space->IsImageSpace()) {
      DCHECK(!seen_zygote);
      DCHECK(!seen_alloc);
    } else if (space->IsZygoteSpace()) {
      DCHECK(!seen_alloc);
      seen_zygote = true;
    } else if (space->IsDlMallocSpace()) {
      seen_alloc = true;
    }
  }
}

void Heap::RegisterGCAllocation(size_t bytes) {
  if (this != NULL) {
    gc_memory_overhead_.fetch_add(bytes);
  }
}

void Heap::RegisterGCDeAllocation(size_t bytes) {
  if (this != NULL) {
    gc_memory_overhead_.fetch_sub(bytes);
  }
}

void Heap::AddDiscontinuousSpace(space::DiscontinuousSpace* space) {
  WriterMutexLock mu(Thread::Current(), *Locks::heap_bitmap_lock_);
  DCHECK(space != NULL);
  DCHECK(space->GetLiveObjects() != NULL);
  live_bitmap_->AddDiscontinuousObjectSet(space->GetLiveObjects());
  DCHECK(space->GetMarkObjects() != NULL);
  mark_bitmap_->AddDiscontinuousObjectSet(space->GetMarkObjects());
  discontinuous_spaces_.push_back(space);
}

void Heap::DumpGcPerformanceInfo(std::ostream& os) {
  // Dump cumulative timings.
  os << "Dumping cumulative Gc timings\n";
  uint64_t total_duration = 0;

  // Dump cumulative loggers for each GC type.
  uint64_t total_paused_time = 0;
  for (const auto& collector : mark_sweep_collectors_) {
    CumulativeLogger& logger = collector->GetCumulativeTimings();
    if (logger.GetTotalNs() != 0) {
      os << Dumpable<CumulativeLogger>(logger);
      const uint64_t total_ns = logger.GetTotalNs();
      const uint64_t total_pause_ns = collector->GetTotalPausedTimeNs();
      double seconds = NsToMs(logger.GetTotalNs()) / 1000.0;
      const uint64_t freed_bytes = collector->GetTotalFreedBytes();
      const uint64_t freed_objects = collector->GetTotalFreedObjects();
      os << collector->GetName() << " total time: " << PrettyDuration(total_ns) << "\n"
         << collector->GetName() << " paused time: " << PrettyDuration(total_pause_ns) << "\n"
         << collector->GetName() << " freed: " << freed_objects
         << " objects with total size " << PrettySize(freed_bytes) << "\n"
         << collector->GetName() << " throughput: " << freed_objects / seconds << "/s / "
         << PrettySize(freed_bytes / seconds) << "/s\n";
      total_duration += total_ns;
      total_paused_time += total_pause_ns;
    }
  }
  uint64_t allocation_time = static_cast<uint64_t>(total_allocation_time_) * kTimeAdjust;
  size_t total_objects_allocated = GetObjectsAllocatedEver();
  size_t total_bytes_allocated = GetBytesAllocatedEver();
  if (total_duration != 0) {
    const double total_seconds = static_cast<double>(total_duration / 1000) / 1000000.0;
    os << "Total time spent in GC: " << PrettyDuration(total_duration) << "\n";
    os << "Mean GC size throughput: "
       << PrettySize(GetBytesFreedEver() / total_seconds) << "/s\n";
    os << "Mean GC object throughput: "
       << (GetObjectsFreedEver() / total_seconds) << " objects/s\n";
  }
  os << "Total number of allocations: " << total_objects_allocated << "\n";
  os << "Total bytes allocated " << PrettySize(total_bytes_allocated) << "\n";
  if (kMeasureAllocationTime) {
    os << "Total time spent allocating: " << PrettyDuration(allocation_time) << "\n";
    os << "Mean allocation time: " << PrettyDuration(allocation_time / total_objects_allocated)
       << "\n";
  }
  os << "Total mutator paused time: " << PrettyDuration(total_paused_time) << "\n";
  os << "Total time waiting for GC to complete: " << PrettyDuration(GetTotalWaitTime()) << "\n";
  os << "Approximate GC data structures memory overhead: " << gc_memory_overhead_;
}

Heap::~Heap() {
  if (kDumpGcPerformanceOnShutdown) {
    DumpGcPerformanceInfo(LOG(INFO));
  }

  STLDeleteElements(&mark_sweep_collectors_);

  // If we don't reset then the mark stack complains in it's destructor.
  allocation_stack_->Reset();
  live_stack_->Reset();

  VLOG(heap) << "~Heap()";
  // We can't take the heap lock here because there might be a daemon thread suspended with the
  // heap lock held. We know though that no non-daemon threads are executing, and we know that
  // all daemon threads are suspended, and we also know that the threads list have been deleted, so
  // those threads can't resume. We're the only running thread, and we can do whatever we like...
  STLDeleteElements(&continuous_spaces_);
  STLDeleteElements(&discontinuous_spaces_);
  delete gc_complete_lock_;
  delete soft_ref_queue_lock_;
  delete weak_ref_queue_lock_;
  delete finalizer_ref_queue_lock_;
  delete phantom_ref_queue_lock_;
}

space::ContinuousSpace* Heap::FindContinuousSpaceFromObject(const mirror::Object* obj,
                                                            bool fail_ok) const {
  for (const auto& space : continuous_spaces_) {
    if (space->Contains(obj)) {
      return space;
    }
  }
  if (!fail_ok) {
    LOG(FATAL) << "object " << reinterpret_cast<const void*>(obj) << " not inside any spaces!";
  }
  return NULL;
}

space::DiscontinuousSpace* Heap::FindDiscontinuousSpaceFromObject(const mirror::Object* obj,
                                                                  bool fail_ok) const {
  for (const auto& space : discontinuous_spaces_) {
    if (space->Contains(obj)) {
      return space;
    }
  }
  if (!fail_ok) {
    LOG(FATAL) << "object " << reinterpret_cast<const void*>(obj) << " not inside any spaces!";
  }
  return NULL;
}

space::Space* Heap::FindSpaceFromObject(const mirror::Object* obj, bool fail_ok) const {
  space::Space* result = FindContinuousSpaceFromObject(obj, true);
  if (result != NULL) {
    return result;
  }
  return FindDiscontinuousSpaceFromObject(obj, true);
}


size_t Heap::GCPGetObjectAllocatedSpace(const mirror::Object* obj) {
	space::Space* _space = FindSpaceFromObject(obj, true);
	if(_space == NULL)
		return 0;
	return _space->GCPGetAllocationSize(obj);

}

space::ImageSpace* Heap::GetImageSpace() const {
  for (const auto& space : continuous_spaces_) {
    if (space->IsImageSpace()) {
      return space->AsImageSpace();
    }
  }
  return NULL;
}

static void MSpaceChunkCallback(void* start, void* end, size_t used_bytes, void* arg) {
  size_t chunk_size = reinterpret_cast<uint8_t*>(end) - reinterpret_cast<uint8_t*>(start);
  if (used_bytes < chunk_size) {
    size_t chunk_free_bytes = chunk_size - used_bytes;
    size_t& max_contiguous_allocation = *reinterpret_cast<size_t*>(arg);
    max_contiguous_allocation = std::max(max_contiguous_allocation, chunk_free_bytes);
  }
}

static void MSpaceFragCallback(void* start, void* end, size_t used_bytes, void* arg) {
  size_t chunk_size = reinterpret_cast<uint8_t*>(end) - reinterpret_cast<uint8_t*>(start);
  mprofiler::FragGCProfiler* profiler = reinterpret_cast<mprofiler::FragGCProfiler*>(arg);
  size_t chunk_free_bytes = 0;
  if (used_bytes < chunk_size) {
    chunk_free_bytes = chunk_size - used_bytes;
   // max_contiguous_allocation = std::max(max_contiguous_allocation, chunk_free_bytes);
  }
  if(chunk_free_bytes >= 8) {
    LOG(ERROR) << "startFrag=" << start << ", used=" <<used_bytes<<", free=" << chunk_free_bytes;
  }
  profiler->gcpAddFragSegment(used_bytes, chunk_free_bytes);
}

void Heap::GetMaxContigAlloc(void* args) {
  GetAllocSpace()->AsDlMallocSpace()->Walk(MSpaceFragCallback,
                                           args);
}

mirror::Object* Heap::AllocObject(Thread* self, mirror::Class* c, size_t byte_count) {
  DCHECK(c == NULL || (c->IsClassClass() && byte_count >= sizeof(mirror::Class)) ||
         (c->IsVariableSize() || c->GetObjectSize() == byte_count) ||
         strlen(ClassHelper(c).GetDescriptor()) == 0);
  DCHECK_GE(byte_count, sizeof(mirror::Object));

  mirror::Object* obj = NULL;
  size_t bytes_allocated = 0;
  uint64_t allocation_start = 0;
  if (UNLIKELY(kMeasureAllocationTime)) {
    allocation_start = NanoTime() / kTimeAdjust;
  }

  // We need to have a zygote space or else our newly allocated large object can end up in the
  // Zygote resulting in it being prematurely freed.
  // We can only do this for primitive objects since large objects will not be within the card table
  // range. This also means that we rely on SetClass not dirtying the object's card.
  bool large_object_allocation =
      byte_count >= large_object_threshold_ && have_zygote_space_ && c->IsPrimitiveArray();
  if (UNLIKELY(large_object_allocation)) {
    obj = Allocate(self, large_object_space_, byte_count, &bytes_allocated);
    // Make sure that our large object didn't get placed anywhere within the space interval or else
    // it breaks the immune range.
    DCHECK(obj == NULL ||
           reinterpret_cast<byte*>(obj) < continuous_spaces_.front()->Begin() ||
           reinterpret_cast<byte*>(obj) >= continuous_spaces_.back()->End());
  } else {
    obj = Allocate(self, alloc_space_, byte_count, &bytes_allocated);
    // Ensure that we did not allocate into a zygote space.
    DCHECK(obj == NULL || !have_zygote_space_ || !FindSpaceFromObject(obj, false)->IsZygoteSpace());
  }

  if (LIKELY(obj != NULL)) {
    obj->SetClass(c);

    // Record allocation after since we want to use the atomic add for the atomic fence to guard
    // the SetClass since we do not want the class to appear NULL in another thread.
    RecordAllocation(bytes_allocated, obj);

    if (Dbg::IsAllocTrackingEnabled()) {
      Dbg::RecordAllocation(c, byte_count);
    }
    if (UNLIKELY(static_cast<size_t>(GetBytesAllocated()) >= GetConcStartBytes())) {
      // The SirtRef is necessary since the calls in RequestConcurrentGC are a safepoint.
      SirtRef<mirror::Object> ref(self, obj);
      RequestConcurrentGC(self);
    }
    if (kDesiredHeapVerification > kNoHeapVerification) {
      VerifyObject(obj);
    }

    if (UNLIKELY(kMeasureAllocationTime)) {
      total_allocation_time_.fetch_add(NanoTime() / kTimeAdjust - allocation_start);
    }

    return obj;
  } else {
    std::ostringstream oss;
    int64_t total_bytes_free = GetFreeMemory();
    oss << "Failed to allocate a " << byte_count << " byte allocation with " << total_bytes_free
        << " free bytes";
    // If the allocation failed due to fragmentation, print out the largest continuous allocation.
    if (!large_object_allocation && total_bytes_free >= byte_count) {
      size_t max_contiguous_allocation = 0;
      for (const auto& space : continuous_spaces_) {
        if (space->IsDlMallocSpace()) {
          space->AsDlMallocSpace()->Walk(MSpaceChunkCallback, &max_contiguous_allocation);
        }
      }
      oss << "; failed due to fragmentation (largest possible contiguous allocation "
          <<  max_contiguous_allocation << " bytes)";
    }
    self->ThrowOutOfMemoryError(oss.str().c_str());
    return NULL;
  }
}





bool Heap::IsHeapAddress(const mirror::Object* obj) {
  // Note: we deliberately don't take the lock here, and mustn't test anything that would
  // require taking the lock.
  if (obj == NULL) {
    return true;
  }
  if (UNLIKELY(!IsAligned<kObjectAlignment>(obj))) {
    return false;
  }
  return FindSpaceFromObject(obj, true) != NULL;
}

bool Heap::IsLiveObjectLocked(const mirror::Object* obj, bool search_allocation_stack,
                              bool search_live_stack, bool sorted) {
  // Locks::heap_bitmap_lock_->AssertReaderHeld(Thread::Current());
  if (obj == NULL || UNLIKELY(!IsAligned<kObjectAlignment>(obj))) {
    return false;
  }
  space::ContinuousSpace* c_space = FindContinuousSpaceFromObject(obj, true);
  space::DiscontinuousSpace* d_space = NULL;
  if (c_space != NULL) {
    if (c_space->GetLiveBitmap()->Test(obj)) {
      return true;
    }
  } else {
    d_space = FindDiscontinuousSpaceFromObject(obj, true);
    if (d_space != NULL) {
      if (d_space->GetLiveObjects()->Test(obj)) {
        return true;
      }
    }
  }
  // This is covering the allocation/live stack swapping that is done without mutators suspended.
  for (size_t i = 0; i < (sorted ? 1 : 5); ++i) {
    if (i > 0) {
      NanoSleep(MsToNs(10));
    }

    if (search_allocation_stack) {
      if (sorted) {
        if (allocation_stack_->ContainsSorted(const_cast<mirror::Object*>(obj))) {
          return true;
        }
      } else if (allocation_stack_->Contains(const_cast<mirror::Object*>(obj))) {
        return true;
      }
    }

    if (search_live_stack) {
      if (sorted) {
        if (live_stack_->ContainsSorted(const_cast<mirror::Object*>(obj))) {
          return true;
        }
      } else if (live_stack_->Contains(const_cast<mirror::Object*>(obj))) {
        return true;
      }
    }
  }
  // We need to check the bitmaps again since there is a race where we mark something as live and
  // then clear the stack containing it.
  if (c_space != NULL) {
    if (c_space->GetLiveBitmap()->Test(obj)) {
      return true;
    }
  } else {
    d_space = FindDiscontinuousSpaceFromObject(obj, true);
    if (d_space != NULL && d_space->GetLiveObjects()->Test(obj)) {
      return true;
    }
  }
  return false;
}

void Heap::VerifyObjectImpl(const mirror::Object* obj) {
  if (Thread::Current() == NULL ||
      Runtime::Current()->GetThreadList()->GetLockOwner() == Thread::Current()->GetTid()) {
    return;
  }
  VerifyObjectBody(obj);
}


byte* Heap::GetMaxAddress() {
  //TODO: I ignore the LOS here
  return continuous_spaces_.back()->End();
}

void Heap::GCPSrvcReinitMarkSweep(collector::MarkSweep* newCollector) {
//  LOG(ERROR) << "Precollectors size is: " << mark_sweep_collectors_.size();
//  for (const auto& cur_collector : mark_sweep_collectors_) {
//    LOG(ERROR) << "A-Collector: " << cur_collector->GetName() <<", type: " << cur_collector->GetGcType();
//  }
  std::vector<collector::MarkSweep*>::iterator iter = mark_sweep_collectors_.begin();
  while( iter != mark_sweep_collectors_.end()) {
    if(((*iter)->GetGcType() == newCollector->GetGcType())
        && (((*iter)->IsConcurrent() == newCollector->IsConcurrent()))) {
//      LOG(ERROR) << "Removing original Marksweep; gc_type:" <<
//          newCollector->GetGcType() << "conc: " << newCollector->IsConcurrent();
      mark_sweep_collectors_.erase(iter);
      break;
    }
    ++iter;
  }
  //LOG(ERROR) << "Postcollectors size is: " << mark_sweep_collectors_.size();
  //LOG(ERROR) << "Added the new marksweep (IPC)";
  mark_sweep_collectors_.push_back(newCollector);
  //LOG(ERROR) << "before leave size is: " << mark_sweep_collectors_.size();

//  for (const auto& cur_collector : mark_sweep_collectors_) {
//    LOG(ERROR) << "B-Collector: " << cur_collector->GetName() << ", type: " <<
//        cur_collector->GetGcType();
//  }



  if(false) {
    DumpGcPerformanceInfo(LOG(ERROR));
  }
}

void Heap::DumpSpaces() {
  for (const auto& space : continuous_spaces_) {
#if (ART_GC_SERVICE)
    accounting::BaseBitmap* live_bitmap = space->GetLiveBitmap();
    accounting::BaseBitmap* mark_bitmap = space->GetMarkBitmap();
#else
    accounting::SpaceBitmap* live_bitmap = space->GetLiveBitmap();
    accounting::SpaceBitmap* mark_bitmap = space->GetMarkBitmap();
#endif
    LOG(INFO) << space << " " << *space << "\n"
              << live_bitmap << " " << *live_bitmap << "\n"
              << mark_bitmap << " " << *mark_bitmap;
#if (ART_GC_SERVICE)
    if(mark_bitmap->IsStructuredBitmap()) {
      accounting::SharedSpaceBitmap* shared_spc_beets =
          (accounting::SharedSpaceBitmap*)mark_bitmap;
      LOG(INFO) << "mark_bitmap_address = " << shared_spc_beets->bitmap_data_;
      shared_spc_beets = (accounting::SharedSpaceBitmap*)live_bitmap;
      LOG(INFO) << "live_bitmap_address = " << shared_spc_beets->bitmap_data_;
    }
#endif
  }
  for (const auto& space : discontinuous_spaces_) {
    LOG(INFO) << space << " " << *space << "\n";
  }
}

void Heap::VerifyObjectBody(const mirror::Object* obj) {
  CHECK(IsAligned<kObjectAlignment>(obj)) << "Object isn't aligned: " << obj;
  // Ignore early dawn of the universe verifications.
  if (UNLIKELY(static_cast<size_t>(GetAtomicBytesAllocated()) < 10 * KB)) {
    return;
  }
  const byte* raw_addr = reinterpret_cast<const byte*>(obj) +
      mirror::Object::ClassOffset().Int32Value();
  const mirror::Class* c = *reinterpret_cast<mirror::Class* const *>(raw_addr);
  if (UNLIKELY(c == NULL)) {
    LOG(FATAL) << "Null class in object: " << obj;
  } else if (UNLIKELY(!IsAligned<kObjectAlignment>(c))) {
    LOG(FATAL) << "Class isn't aligned: " << c << " in object: " << obj;
  }
  // Check obj.getClass().getClass() == obj.getClass().getClass().getClass()
  // Note: we don't use the accessors here as they have internal sanity checks
  // that we don't want to run
  raw_addr = reinterpret_cast<const byte*>(c) +
                                  mirror::Object::ClassOffset().Int32Value();
  const mirror::Class* c_c = *reinterpret_cast<mirror::Class* const *>(raw_addr);
  raw_addr = reinterpret_cast<const byte*>(c_c) + mirror::Object::ClassOffset().Int32Value();
  const mirror::Class* c_c_c = *reinterpret_cast<mirror::Class* const *>(raw_addr);
  CHECK_EQ(c_c, c_c_c);

  if (verify_object_mode_ != kVerifyAllFast) {
    // TODO: the bitmap tests below are racy if VerifyObjectBody is called without the
    //       heap_bitmap_lock_.
    if (!IsLiveObjectLocked(obj)) {
      DumpSpaces();
      LOG(FATAL) << "Object is dead: " << obj;
    }
    if (!IsLiveObjectLocked(c)) {
      LOG(FATAL) << "Class of object is dead: " << c << " in object: " << obj;
    }
  }
}

void Heap::VerificationCallback(mirror::Object* obj, void* arg) {
  DCHECK(obj != NULL);
  reinterpret_cast<Heap*>(arg)->VerifyObjectBody(obj);
}

void Heap::VerifyHeap() {
  ReaderMutexLock mu(Thread::Current(), *Locks::heap_bitmap_lock_);
  GetLiveBitmap()->Walk(Heap::VerificationCallback, this);
}

inline void Heap::RecordAllocation(size_t size, mirror::Object* obj) {
  DCHECK(obj != NULL);
  DCHECK_GT(size, 0u);
  IncAtomicBytesAllocated(size);

  if (Runtime::Current()->HasStatsEnabled()) {
    RuntimeStats* thread_stats = Thread::Current()->GetStats();
    ++thread_stats->allocated_objects;
    thread_stats->allocated_bytes += size;

    // TODO: Update these atomically.
    RuntimeStats* global_stats = Runtime::Current()->GetStats();
    ++global_stats->allocated_objects;
    global_stats->allocated_bytes += size;
  }

  // This is safe to do since the GC will never free objects which are neither in the allocation
  // stack or the live bitmap.
//  Thread* self = Thread::Current();

  while (!allocation_stack_->AtomicPushBack(obj)) {


    GCP_MARK_START_ALLOC_GC_HW_EVENT;
    GCP_MARK_START_GC_HAT_TIME_EVENT(Thread::Current());
#if (ART_GC_SERVICE)
    LOG(FATAL) << "Heap::RecordAllocation-START..AtomicPushBack..allocation stack is " << allocation_stack_->Capacity();
    //service::GCServiceClient::RequestAllocateGC();
    LOG(ERROR) << "Heap::RecordAllocation-----AtomicPushBack";
#else
    CollectGarbageInternal(collector::kGcTypeSticky, kGcCauseForAlloc, false);
#endif
    GCP_MARK_END_GC_HAT_TIME_EVENT(Thread::Current());
    GCP_MARK_END_ALLOC_GC_HW_EVENT;
  }

#if (ART_GC_SERVICE)
  if(GCServiceClient::ShouldNotifyAllocationCapacity(allocation_stack_->Size(),
                                                  allocation_stack_->Capacity())) {

//    LOG(ERROR) << "Sending alloc_stack.." << allocation_stack_->Size()
//        << ", capacity:"<<allocation_stack_->Capacity();
    GCServiceClient::RequestAllocateGC();
  }
#endif

}

void Heap::RecordFree(size_t freed_objects, size_t freed_bytes) {
  DCHECK_LE(freed_bytes, static_cast<size_t>(GetBytesAllocated()));
  IncAtomicBytesAllocated(-freed_bytes);

  if (Runtime::Current()->HasStatsEnabled()) {
    RuntimeStats* thread_stats = Thread::Current()->GetStats();
    thread_stats->freed_objects += freed_objects;
    thread_stats->freed_bytes += freed_bytes;

    // TODO: Do this concurrently.
    RuntimeStats* global_stats = Runtime::Current()->GetStats();
    global_stats->freed_objects += freed_objects;
    global_stats->freed_bytes += freed_bytes;
  }
}

inline bool Heap::IsOutOfMemoryOnAllocation(size_t alloc_size, bool grow) {
  size_t new_footprint = GetBytesAllocated() + alloc_size;
  if (UNLIKELY(new_footprint > GetMaxAllowedFootPrint())) {
    if (UNLIKELY(new_footprint > GetGrowthLimit())) {
      return true;
    }
    if (!concurrent_gc_) {
      if (!grow) {
        return true;
      } else {
        SetMaxAllowedFootPrint(new_footprint);
      }
    }
  }
  return false;
}

inline mirror::Object* Heap::TryToAllocate(Thread* self,
                                    space::AllocSpace* space, size_t alloc_size,
                                           bool grow, size_t* bytes_allocated) {
  if (UNLIKELY(IsOutOfMemoryOnAllocation(alloc_size, grow))) {
    return NULL;
  }
  return space->Alloc(self, alloc_size, bytes_allocated);
}

// DlMallocSpace-specific version.
inline mirror::Object* Heap::TryToAllocate(Thread* self, space::DL_MALLOC_SPACE* space, size_t alloc_size,
                                           bool grow, size_t* bytes_allocated) {
  if (UNLIKELY(IsOutOfMemoryOnAllocation(alloc_size, grow))) {
    return NULL;
  }
  if (LIKELY(!running_on_valgrind_)) {
    return space->AllocNonvirtual(self, alloc_size, bytes_allocated);
  } else {
    return space->Alloc(self, alloc_size, bytes_allocated);
  }
}

template <class T>
inline mirror::Object* Heap::Allocate(Thread* self, T* space, size_t alloc_size,
                                      size_t* bytes_allocated) {
  // Since allocation can cause a GC which will need to SuspendAll, make sure all allocations are
  // done in the runnable state where suspension is expected.
  DCHECK_EQ(self->GetState(), kRunnable);
  self->AssertThreadSuspensionIsAllowable();

  //GCP_ADD_EXTRA_BYES(alloc_size);

  mirror::Object* ptr = TryToAllocate(self, space, alloc_size, false, bytes_allocated);
  if (ptr != NULL) {
    return ptr;
  }
  GCP_MARK_START_GC_HAT_TIME_EVENT(self);
#if (ART_GC_SERVICE)
  if(GCServiceClient::service_client_ != NULL) {
    LOG(ERROR) << "Heap::Allocate..going to call Allocate with Internal GC";
  }
#endif
  mirror::Object* ptrAfterGC = AllocateInternalWithGc(self, space, alloc_size, bytes_allocated);
#if (ART_GC_SERVICE)
  if(GCServiceClient::service_client_ != NULL) {
    LOG(ERROR) << "Heap::Allocate..------------------";
  }
#endif
  GCP_MARK_END_GC_HAT_TIME_EVENT(self);
  return ptrAfterGC;
}

mirror::Object* Heap::AllocateInternalWithGc(Thread* self, space::AllocSpace* space,
                                             size_t alloc_size, size_t* bytes_allocated) {
  mirror::Object* ptr;

  // The allocation failed. If the GC is running, block until it completes, and then retry the
  // allocation.
  collector::GcType last_gc = WaitForConcurrentGcToComplete(self, false);
  if (last_gc != collector::kGcTypeNone) {
    // A GC was in progress and we blocked, retry allocation now that memory has been freed.
    ptr = TryToAllocate(self, space, alloc_size, false, bytes_allocated);
    if (ptr != NULL) {
      return ptr;
    }
  }

  // Loop through our different Gc types and try to Gc until we get enough free memory.
  for (size_t i = static_cast<size_t>(last_gc) + 1;
      i < static_cast<size_t>(collector::kGcTypeMax); ++i) {
    bool run_gc = false;
    collector::GcType gc_type = static_cast<collector::GcType>(i);
    switch (gc_type) {
      case collector::kGcTypeSticky: {
          const size_t alloc_space_size = alloc_space_->Size();
          run_gc = alloc_space_size > min_alloc_space_size_for_sticky_gc_ &&
              alloc_space_->Capacity() - alloc_space_size >= min_remaining_space_for_sticky_gc_;
          break;
        }
      case collector::kGcTypePartial:
        run_gc = have_zygote_space_;
        break;
      case collector::kGcTypeFull:
        run_gc = true;
        break;
      default:
        break;
    }

    if (run_gc) {
#if (ART_GC_SERVICE)
      if(GCServiceClient::RequestAllocateGC()) {
        LOG(ERROR) << "Heap::AllocateInternalWithGc...00";

        // Allocations have failed after GCs;  this is an exceptional state.
        // Try harder, growing the heap if necessary.
        ptr = TryToAllocate(self, space, alloc_size, true, bytes_allocated);
        if (ptr != NULL) {
          return ptr;
        }
      } else {
        LOG(ERROR) << "Heap::AllocateInternalWithGc...01";

        GCP_MARK_START_ALLOC_GC_HW_EVENT;
        // If we actually ran a different type of Gc than requested, we can skip the index forwards.
        collector::GcType gc_type_ran = CollectGarbageInternal(gc_type, kGcCauseForAlloc, false);
        GCP_MARK_END_ALLOC_GC_HW_EVENT;
        DCHECK_GE(static_cast<size_t>(gc_type_ran), i);
        i = static_cast<size_t>(gc_type_ran);

        // Did we free sufficient memory for the allocation to succeed?
        ptr = TryToAllocate(self, space, alloc_size, false, bytes_allocated);
        if (ptr != NULL) {
          return ptr;
        }
      }
#else
      GCP_MARK_START_ALLOC_GC_HW_EVENT;
      // If we actually ran a different type of Gc than requested, we can skip the index forwards.
      collector::GcType gc_type_ran = CollectGarbageInternal(gc_type, kGcCauseForAlloc, false);
      GCP_MARK_END_ALLOC_GC_HW_EVENT;
      DCHECK_GE(static_cast<size_t>(gc_type_ran), i);
      i = static_cast<size_t>(gc_type_ran);

      // Did we free sufficient memory for the allocation to succeed?
      ptr = TryToAllocate(self, space, alloc_size, false, bytes_allocated);
      if (ptr != NULL) {
        return ptr;
      }
#endif

    }
  }

  // Allocations have failed after GCs;  this is an exceptional state.
  // Try harder, growing the heap if necessary.
  ptr = TryToAllocate(self, space, alloc_size, true, bytes_allocated);
  if (ptr != NULL) {
    return ptr;
  }

  // Most allocations should have succeeded by now, so the heap is really full, really fragmented,
  // or the requested size is really big. Do another GC, collecting SoftReferences this time. The
  // VM spec requires that all SoftReferences have been collected and cleared before throwing OOME.

  // OLD-TODO: wait for the finalizers from the previous GC to finish
  VLOG(gc) << "Forcing collection of SoftReferences for " << PrettySize(alloc_size)
           << " allocation";

  // We don't need a WaitForConcurrentGcToComplete here either.
  GCP_MARK_START_ALLOC_GC_HW_EVENT;
#if (ART_GC_SERVICE)
  if(GCServiceClient::service_client_ != NULL) {
    LOG(ERROR) << "Heap::AllocateInternalWithGc.. 01.. Start.. Going to call CollectwithInternalGC";
  }
#endif
  CollectGarbageInternal(collector::kGcTypeFull, kGcCauseForAlloc, true);
#if (ART_GC_SERVICE)
  if(GCServiceClient::service_client_ != NULL) {
    LOG(ERROR) << "Heap::AllocateInternalWithGc.. 02.. END.. Done call CollectwithInternalGC";
  }
#endif
  GCP_MARK_END_ALLOC_GC_HW_EVENT;
  return TryToAllocate(self, space, alloc_size, true, bytes_allocated);
}

void Heap::SetTargetHeapUtilization(float target) {
  DCHECK_GT(target, 0.0f);  // asserted in Java code
  DCHECK_LT(target, 1.0f);
  SetTargetUtilization(target);
  //target_utilization_ = target;
}

size_t Heap::GetObjectsAllocated() const {
  size_t total = 0;
  typedef std::vector<space::ContinuousSpace*>::const_iterator It;
  for (It it = continuous_spaces_.begin(), end = continuous_spaces_.end(); it != end; ++it) {
    space::ContinuousSpace* space = *it;
    if (space->IsDlMallocSpace()) {
      total += space->AsDlMallocSpace()->GetObjectsAllocated();
    }
  }
  typedef std::vector<space::DiscontinuousSpace*>::const_iterator It2;
  for (It2 it = discontinuous_spaces_.begin(), end = discontinuous_spaces_.end(); it != end; ++it) {
    space::DiscontinuousSpace* space = *it;
    total += space->AsLargeObjectSpace()->GetObjectsAllocated();
  }
  return total;
}

size_t Heap::GetObjectsAllocatedEver() const {
  size_t total = 0;
  typedef std::vector<space::ContinuousSpace*>::const_iterator It;
  for (It it = continuous_spaces_.begin(), end = continuous_spaces_.end(); it != end; ++it) {
    space::ContinuousSpace* space = *it;
    if (space->IsDlMallocSpace()) {
      total += space->AsDlMallocSpace()->GetTotalObjectsAllocated();
    }
  }
  typedef std::vector<space::DiscontinuousSpace*>::const_iterator It2;
  for (It2 it = discontinuous_spaces_.begin(), end = discontinuous_spaces_.end(); it != end; ++it) {
    space::DiscontinuousSpace* space = *it;
    total += space->AsLargeObjectSpace()->GetTotalObjectsAllocated();
  }
  return total;
}

size_t Heap::GetBytesAllocatedEver() const {
  size_t total = 0;
  typedef std::vector<space::ContinuousSpace*>::const_iterator It;
  for (It it = continuous_spaces_.begin(), end = continuous_spaces_.end(); it != end; ++it) {
    space::ContinuousSpace* space = *it;
    if (space->IsDlMallocSpace()) {
      total += space->AsDlMallocSpace()->GetTotalBytesAllocated();
    }
  }
  typedef std::vector<space::DiscontinuousSpace*>::const_iterator It2;
  for (It2 it = discontinuous_spaces_.begin(), end = discontinuous_spaces_.end(); it != end; ++it) {
    space::DiscontinuousSpace* space = *it;
    total += space->AsLargeObjectSpace()->GetTotalBytesAllocated();
  }
  return total;
}

class InstanceCounter {
 public:
  InstanceCounter(const std::vector<mirror::Class*>& classes, bool use_is_assignable_from, uint64_t* counts)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      : classes_(classes), use_is_assignable_from_(use_is_assignable_from), counts_(counts) {
  }

  void operator()(const mirror::Object* o) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    for (size_t i = 0; i < classes_.size(); ++i) {
      const mirror::Class* instance_class = o->GetClass();
      if (use_is_assignable_from_) {
        if (instance_class != NULL && classes_[i]->IsAssignableFrom(instance_class)) {
          ++counts_[i];
        }
      } else {
        if (instance_class == classes_[i]) {
          ++counts_[i];
        }
      }
    }
  }

 private:
  const std::vector<mirror::Class*>& classes_;
  bool use_is_assignable_from_;
  uint64_t* const counts_;

  DISALLOW_COPY_AND_ASSIGN(InstanceCounter);
};

void Heap::CountInstances(const std::vector<mirror::Class*>& classes, bool use_is_assignable_from,
                          uint64_t* counts) {
  // We only want reachable instances, so do a GC. This also ensures that the alloc stack
  // is empty, so the live bitmap is the only place we need to look.
  Thread* self = Thread::Current();
  self->TransitionFromRunnableToSuspended(kNative);
  CollectGarbage(false);
  self->TransitionFromSuspendedToRunnable();

  InstanceCounter counter(classes, use_is_assignable_from, counts);
  ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
  GetLiveBitmap()->Visit(counter);
}

class InstanceCollector {
 public:
  InstanceCollector(mirror::Class* c, int32_t max_count, std::vector<mirror::Object*>& instances)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      : class_(c), max_count_(max_count), instances_(instances) {
  }

  void operator()(const mirror::Object* o) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    const mirror::Class* instance_class = o->GetClass();
    if (instance_class == class_) {
      if (max_count_ == 0 || instances_.size() < max_count_) {
        instances_.push_back(const_cast<mirror::Object*>(o));
      }
    }
  }

 private:
  mirror::Class* class_;
  uint32_t max_count_;
  std::vector<mirror::Object*>& instances_;

  DISALLOW_COPY_AND_ASSIGN(InstanceCollector);
};

void Heap::GetInstances(mirror::Class* c, int32_t max_count,
                        std::vector<mirror::Object*>& instances) {
  // We only want reachable instances, so do a GC. This also ensures that the alloc stack
  // is empty, so the live bitmap is the only place we need to look.
  Thread* self = Thread::Current();
  self->TransitionFromRunnableToSuspended(kNative);
  CollectGarbage(false);
  self->TransitionFromSuspendedToRunnable();

  InstanceCollector collector(c, max_count, instances);
  ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
  GetLiveBitmap()->Visit(collector);
}

class ReferringObjectsFinder {
 public:
  ReferringObjectsFinder(mirror::Object* object, int32_t max_count,
                         std::vector<mirror::Object*>& referring_objects)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      : object_(object), max_count_(max_count), referring_objects_(referring_objects) {
  }

  // For bitmap Visit.
  // TODO: Fix lock analysis to not use NO_THREAD_SAFETY_ANALYSIS, requires support for
  // annotalysis on visitors.
  void operator()(const mirror::Object* o) const NO_THREAD_SAFETY_ANALYSIS {
    collector::MarkSweep::VisitObjectReferences(o, *this);
  }

  // For MarkSweep::VisitObjectReferences.
  void operator()(const mirror::Object* referrer, const mirror::Object* object,
                  const MemberOffset&, bool) const {
    if (object == object_ && (max_count_ == 0 || referring_objects_.size() < max_count_)) {
      referring_objects_.push_back(const_cast<mirror::Object*>(referrer));
    }
  }

 private:
  mirror::Object* object_;
  uint32_t max_count_;
  std::vector<mirror::Object*>& referring_objects_;

  DISALLOW_COPY_AND_ASSIGN(ReferringObjectsFinder);
};

void Heap::GetReferringObjects(mirror::Object* o, int32_t max_count,
                               std::vector<mirror::Object*>& referring_objects) {
  // We only want reachable instances, so do a GC. This also ensures that the alloc stack
  // is empty, so the live bitmap is the only place we need to look.
  Thread* self = Thread::Current();
  self->TransitionFromRunnableToSuspended(kNative);
  CollectGarbage(false);
  self->TransitionFromSuspendedToRunnable();

  ReferringObjectsFinder finder(o, max_count, referring_objects);
  ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
  GetLiveBitmap()->Visit(finder);
}

void Heap::CollectGarbageForProfile(bool clear_soft_references) {
	 Thread* self = Thread::Current();
	  //LOG(ERROR) << "vmprofiler: explicit call.." << self->GetTid();
//	  mprofiler::VMProfiler::MProfMarkGCExplTimeEvent(self);
	  WaitForConcurrentGcToComplete(self, false);
	  CollectGarbageInternal(collector::kGcTypeFull, kGcCauseProfile, clear_soft_references);
}

void Heap::CollectGarbage(bool clear_soft_references) {
  // Even if we waited for a GC we still need to do another GC since weaks allocated during the
  // last GC will not have necessarily been cleared.

  bool doRequest = true;

#if (ART_GC_SERVICE)
  doRequest = !(GCServiceClient::RequestExplicitGC());
#endif

  if(doRequest) {
    GCP_MARK_START_EXPL_GC_HW_EVENT;
    Thread* self = Thread::Current();
    //LOG(ERROR) << "vmprofiler: explicit call.." << self->GetTid();
    GCP_MARK_START_EXPL_GC_TIME_EVENT(self);
    WaitForConcurrentGcToComplete(self, false);
    CollectGarbageInternal(collector::kGcTypeFull, kGcCauseExplicit, clear_soft_references);
    GCP_MARK_END_EXPL_GC_TIME_EVENT(self);
    GCP_MARK_END_EXPL_GC_HW_EVENT;
  }
}

void Heap::PreZygoteForkNoSpaceFork() {
  static Mutex zygote_creation_lock_("zygote creation lock", kZygoteCreationLock);
  // Do this before acquiring the zygote creation lock so that we don't get lock order violations.
  CollectGarbage(false);

  Thread* self = Thread::Current();
  MutexLock mu(self, zygote_creation_lock_);

  {
    // Flush the alloc stack.
    WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
    FlushAllocStack();
  }

}



#if (ART_GC_SERVICE)
void Heap::FixHeapBitmapEntries() {
  accounting::SharedHeapBitmap* mark_bitmap =
      (accounting::SharedHeapBitmap*) GetMarkBitmap();
  accounting::SharedHeapBitmap* live_bitmap =
      (accounting::SharedHeapBitmap*) GetLiveBitmap();
  mark_bitmap->FixDataEntries();
  live_bitmap->FixDataEntries();

}
void Heap::PostZygoteForkWithSpaceFork(bool shared_space) {
  static Mutex zygote_creation_lock_("zygote creation lock", kZygoteCreationLock);
  Thread* self = Thread::Current();
  MutexLock mu(self, zygote_creation_lock_);

  // Try to see if we have any Zygote spaces.
  if (have_zygote_space_) {
    return;
  }

  space::DLMALLOC_SPACE_T* zygote_space = alloc_space_;

  // Turns the current alloc space into a Zygote space and obtain the new alloc space composed
  // of the remaining available heap memory.
  if(shared_space) {

//    LOG(ERROR) << "...........VERIFY BEFORE RESHARING................";
//    DumpSpaces();

    int _space_index = 0;
    space::GCSrvSharableDlMallocSpace* _struct_alloc_space =
        GCServiceGlobalAllocator::GCSrvcAllocateSharableSpace(&_space_index);

    _struct_alloc_space->space_index_ = _space_index;
//    space::GCSrvSharableDlMallocSpace* _struct_alloc_space =
//          space::SharableDlMallocSpace::AllocateDataMemory();service

    if(false  /*!GetCardTable()->shareCardTable(&_struct_alloc_space->card_table_data_)*/) {
      LOG(ERROR) << "Error in sharing the Card table";
    } else {
    //  LOG(ERROR) << "Success in sharing the Card table";
      {
     //   LOG(ERROR) << "Locking heapbitmap lock";

        WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
       // LOG(ERROR) << "Resharing live_heap_bitmap_data_";
        live_bitmap_.reset(accounting::BaseHeapBitmap::ReShareHeapBitmap(/*this,*/
            reinterpret_cast<accounting::SharedHeapBitmap*>(live_bitmap_.release()),
                                  &_struct_alloc_space->heap_meta_.live_heap_bitmap_data_));
        //LOG(ERROR) << "Resharing mark_heap_bitmap_data_";
        mark_bitmap_.reset(accounting::BaseHeapBitmap::ReShareHeapBitmap(/*this,*/
            reinterpret_cast<accounting::SharedHeapBitmap*>(mark_bitmap_.release()),
                                  &_struct_alloc_space->heap_meta_.mark_heap_bitmap_data_));
      // LOG(ERROR) << "Done Resharing mark_heap_bitmap_data_";
        if(false) {
          allocation_stack_.reset(accounting::ATOMIC_OBJ_STACK_T::ShareStack(allocation_stack_.release(),
              &(_struct_alloc_space->heap_meta_.alloc_stack_data_), true,
              max_allocation_stack_size_));

          live_stack_.reset(accounting::ATOMIC_OBJ_STACK_T::ShareStack(live_stack_.release(),
              &(_struct_alloc_space->heap_meta_.live_stack_data_), true,
              max_allocation_stack_size_));
        }


        static const size_t default_mark_stack_size = std::max(256 * KB,max_allocation_stack_size_) ;
        mark_stack_.reset(accounting::ATOMIC_OBJ_STACK_T::ShareStack(mark_stack_.release(),
            &(_struct_alloc_space->heap_meta_.mark_stack_data_), true,
            default_mark_stack_size));
        alloc_space_ = zygote_space->CreateSharableZygoteSpace("alloc space",
            _struct_alloc_space, shared_space);

        alloc_space_->SetHeapMeta(zygote_space, GetImageSpace()->cont_space_data_);
        FixHeapBitmapEntries();
      }


    }

  } else {
    alloc_space_ = zygote_space->CreateZygoteSpace("alloc space", shared_space);
  }
  alloc_space_->SetFootprintLimit(alloc_space_->Capacity());

  // Change the GC retention policy of the zygote space to only collect when full.
  zygote_space->SetGcRetentionPolicy(space::kGcRetentionPolicyFullCollect);
  AddContinuousSpace(alloc_space_);
  have_zygote_space_ = true;

  // Reset the cumulative loggers since we now have a few additional timing phases.
  for (const auto& collector : mark_sweep_collectors_) {
    collector->ResetCumulativeStatistics();
  }
  //LOG(ERROR) << "...........BEfore RTURTNING FROM POST ZYGOTE................";
  //DumpSpaces();
  return;
 // } else {
    // Turns the current alloc space into a Zygote space and obtain the new alloc space composed
    // of the remaining available heap memory.
//    space::DlMallocSpace* zygote_space = alloc_space_;
//    alloc_space_ = zygote_space->CreateZygoteSpaceWithSharedSpace("alloc space");
//    alloc_space_->SetFootprintLimit(alloc_space_->Capacity());
//
//    // Change the GC retention policy of the zygote space to only collect when full.
//    zygote_space->SetGcRetentionPolicy(space::kGcRetentionPolicyFullCollect);
//    AddContinuousSpace(alloc_space_);
//    have_zygote_space_ = true;
//
//    // Reset the cumulative loggers since we now have a few additional timing phases.
//    for (const auto& collector : mark_sweep_collectors_) {
//      collector->ResetCumulativeStatistics();
//    }
 // }
  //create shared space here
}

#endif

//typedef struct FragmentationInfoTEST_S {
//  uint64_t sum_;
//  uint64_t max_;
//}FragmentationInfoTEST;
//
//static void MSpaceSumFragChunkCallbackZygote(void* start, void* end, size_t used_bytes, void* arg) {
//  size_t chunk_size = reinterpret_cast<uint8_t*>(end) - reinterpret_cast<uint8_t*>(start);
//  if (used_bytes < chunk_size) {
//    uint64_t chunk_free_bytes = chunk_size - used_bytes;
//    if (chunk_free_bytes >= 8) {
//      FragmentationInfoTEST* _info = reinterpret_cast<FragmentationInfoTEST*>(arg);
//      _info->max_ = std::max(_info->max_, chunk_free_bytes);
//      _info->sum_ = _info->sum_ +  chunk_free_bytes;
////      uint64_t& max_contiguous_allocation = *reinterpret_cast<uint64_t*>(arg);
////      max_contiguous_allocation = max_contiguous_allocation + chunk_free_bytes;
//    }
//  }
//}

void Heap::PreZygoteFork() {
  static Mutex zygote_creation_lock_("zygote creation lock", kZygoteCreationLock);
  // Do this before acquiring the zygote creation lock so that we don't get lock order violations.
  CollectGarbage(false);
  Thread* self = Thread::Current();
  MutexLock mu(self, zygote_creation_lock_);

  // Try to see if we have any Zygote spaces.
  if (have_zygote_space_) {
    return;
  }

  VLOG(heap) << "Starting PreZygoteFork with alloc space size " << PrettySize(alloc_space_->Size());

  {
    // Flush the alloc stack.
    WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
    FlushAllocStack();
  }
//  //Fizo: remember to remove that..
//  LOG(ERROR) << "FFFFFFF check fragmentation in zygote space FFFFFFF";
//  FragmentationInfoTEST _frag_info;
//  _frag_info.max_ = 0;
//  _frag_info.sum_ = 0;
//  alloc_space_->Walk(MSpaceSumFragChunkCallbackZygote, &_frag_info);
//  LOG(ERROR) << "XXXX Fragmentation zygote space .. max= " << _frag_info.max_ << ", sum = " << _frag_info.sum_;
  // Turns the current alloc space into a Zygote space and obtain the new alloc space composed
  // of the remaining available heap memory.
  space::DLMALLOC_SPACE_T* zygote_space = alloc_space_;
  alloc_space_ = zygote_space->CreateZygoteSpace("alloc space");
  alloc_space_->SetFootprintLimit(alloc_space_->Capacity());

  // Change the GC retention policy of the zygote space to only collect when full.
  zygote_space->SetGcRetentionPolicy(space::kGcRetentionPolicyFullCollect);

//  _frag_info.max_ = 0;
//  _frag_info.sum_ = 0;
//  zygote_space->Walk(MSpaceSumFragChunkCallbackZygote, &_frag_info);
//  LOG(ERROR) << "XXXX Fragmentation zygote space after creating alloc space..  max= " << _frag_info.max_ << ", sum=" << _frag_info.sum_;

  AddContinuousSpace(alloc_space_);
  have_zygote_space_ = true;

  // Reset the cumulative loggers since we now have a few additional timing phases.
  for (const auto& collector : mark_sweep_collectors_) {
    collector->ResetCumulativeStatistics();
  }
}

void Heap::FlushAllocStack() {
  MarkAllocStack(alloc_space_->GetLiveBitmap(), GC_HEAP_SRVCE_NO_LOS ? NULL : large_object_space_->GetLiveObjects(),
                 allocation_stack_.get());
  allocation_stack_->Reset();
}


#if (ART_GC_SERVICE)
void Heap::MarkAllocStack(accounting::BaseBitmap* bitmap, accounting::SpaceSetMap* large_objects,
                          accounting::ATOMIC_OBJ_STACK_T* stack) {
#else
void Heap::MarkAllocStack(accounting::SpaceBitmap* bitmap, accounting::SpaceSetMap* large_objects,
                          accounting::ATOMIC_OBJ_STACK_T* stack) {
#endif
  mirror::Object** limit = stack->End();
  for (mirror::Object** it = stack->Begin(); it != limit; ++it) {
    const mirror::Object* obj = *it;
    DCHECK(obj != NULL);
    if (LIKELY(bitmap->HasAddress(obj))) {
      bitmap->Set(obj);
    } else {
      if(GC_HEAP_SRVCE_NO_LOS){
        LOG(FATAL) << "ERROR Heap::MarkAllocStack: " <<
            StringPrintf("%p, object address = %p", stack->GetBaseAddress(), obj) <<
            ", it= " << StringPrintf("%p", it) <<
            ", limit=" << StringPrintf("%p", limit) <<
            ", begin: " << reinterpret_cast<const void*>(stack->Begin()) <<
            ", end: " << reinterpret_cast<const void*>(stack->End()) <<
            ", bitmap_heap_begin: " << StringPrintf("0x%08x", bitmap->HeapBegin()) <<
            ", bitmap_heap_limit: " << StringPrintf("0x%08x", bitmap->HeapLimit()) <<
            ", stack_capacity=" << stack->Capacity() << ", stack_size=" << stack->Size();

      } else {
        large_objects->Set(obj);
      }
    }
  }
}


const char* gc_cause_and_type_strings[4][4] = {
    {"", "GC Alloc Sticky", "GC Alloc Partial", "GC Alloc Full"},
    {"", "GC Background Sticky", "GC Background Partial", "GC Background Full"},
    {"", "GC Explicit Sticky", "GC Explicit Partial", "GC Explicit Full"},
		{"", "GC Profile Sticky", "GC Profile Partial", "GC Profile Full"}};

collector::GcType Heap::CollectGarbageInternal(collector::GcType gc_type, GcCause gc_cause,
                                               bool clear_soft_references) {
#if (ART_GC_SERVICE)
  collector::GcType returned_gc_type = collector::kGcTypeNone;
  if(GCServiceClient::service_client_ != NULL)
    LOG(ERROR) << "Heap::CollectGarbageInternal...00.. checking request internal";
  if(GCServiceClient::RequestInternalGC(gc_type, gc_cause,
      clear_soft_references, &returned_gc_type)) {
    return returned_gc_type;
  }
#endif
  Thread* self = Thread::Current();

  ScopedThreadStateChange tsc(self, kWaitingPerformingGc);
  Locks::mutator_lock_->AssertNotHeld(self);

  if (self->IsHandlingStackOverflow()) {
    LOG(WARNING) << "Performing GC on a thread that is handling a stack overflow.";
  }


  // Ensure there is only one GC at a time.
  bool start_collect = false;
  while (!start_collect) {
    {
      MutexLock mu(self, *gc_complete_lock_);
      if (!is_gc_running_) {
        is_gc_running_ = true;
        start_collect = true;
      }
    }
    if (!start_collect) {
      // TODO: timinglog this.
      WaitForConcurrentGcToComplete(self, false);

      // TODO: if another thread beat this one to do the GC, perhaps we should just return here?
      //       Not doing at the moment to ensure soft references are cleared.
    }
  }

  GCP_MARK_PRE_COLLECTION;
  gc_complete_lock_->AssertNotHeld(self);

  if (gc_cause == kGcCauseForAlloc && Runtime::Current()->HasStatsEnabled()) {
    ++Runtime::Current()->GetStats()->gc_for_alloc_count;
    ++Thread::Current()->GetStats()->gc_for_alloc_count;
  }

  uint64_t gc_start_time_ns = NanoTime();
  uint64_t gc_start_size = GetBytesAllocated();
  // Approximate allocation rate in bytes / second.
  if (UNLIKELY(gc_start_time_ns == GetLastGCTime())) {
    LOG(WARNING) << "Timers are broken (gc_start_time == last_gc_time_).";
  }
  uint64_t ms_delta = NsToMs(gc_start_time_ns - GetLastGCTime());
  if (ms_delta != 0) {
    SetAllocationRate(((gc_start_size - GetLastGCSize()) * 1000) / ms_delta);
    VLOG(heap) << "Allocation rate: " << PrettySize(GetAllocationRate()) << "/s";
  }

  if (gc_type == collector::kGcTypeSticky &&
      alloc_space_->Size() < min_alloc_space_size_for_sticky_gc_) {
    gc_type = collector::kGcTypePartial;
  }

//  if(gc_cause == kGcCauseBackground)
//    LOG(ERROR) << "GCMMP collect -> " << gc_cause_and_type_strings[gc_cause][gc_type] << " from thread ID:" << self->GetTid();
  DCHECK_LT(gc_type, collector::kGcTypeMax);
  DCHECK_NE(gc_type, collector::kGcTypeNone);
  DCHECK_LE(gc_cause, kGcCauseExplicit);

  ATRACE_BEGIN(gc_cause_and_type_strings[gc_cause][gc_type]);

  collector::MarkSweep* collector = NULL;
  for (const auto& cur_collector : mark_sweep_collectors_) {
    if (cur_collector->IsConcurrent() == concurrent_gc_ && cur_collector->GetGcType() == gc_type) {
      collector = cur_collector;
//      if(gc_cause == kGcCauseBackground)
//        LOG(ERROR) << "========collector: " << collector->GetName();
      break;
    }
  }
  CHECK(collector != NULL)
      << "Could not find garbage collector with concurrent=" << concurrent_gc_
      << " and type=" << gc_type;

  collector->clear_soft_references_ = clear_soft_references;
  collector->Run();
  IncTotalObjectsFreedEver(collector->GetFreedObjects());
  IncTotalBytesFreedEver(collector->GetFreedBytes());
  if (care_about_pause_times_) {
    const size_t duration = collector->GetDurationNs();
    std::vector<uint64_t> pauses = collector->GetPauseTimes();
    // GC for alloc pauses the allocating thread, so consider it as a pause.
    bool was_slow = duration > long_gc_log_threshold_ ||
            (gc_cause == kGcCauseForAlloc && duration > long_pause_log_threshold_);
    if (!was_slow) {
      for (uint64_t pause : pauses) {
        was_slow = was_slow || pause > long_pause_log_threshold_;
      }
    }

    if (was_slow) {
        const size_t percent_free = GetPercentFree();
        const size_t current_heap_size = GetBytesAllocated();
        const size_t total_memory = GetTotalMemory();
        std::ostringstream pause_string;
        for (size_t i = 0; i < pauses.size(); ++i) {
            pause_string << PrettyDuration((pauses[i] / 1000) * 1000)
                         << ((i != pauses.size() - 1) ? ", " : "");
        }
      if(!(ART_USE_GC_PROFILER || ART_USE_GC_PROFILER_REF_DIST))
      	LOG(INFO) << gc_cause << " " << collector->GetName()
                  << " GC freed "  <<  collector->GetFreedObjects() << "("
                  << PrettySize(collector->GetFreedBytes()) << ") AllocSpace objects, "
                  << collector->GetFreedLargeObjects() << "("
                  << PrettySize(collector->GetFreedLargeObjectBytes()) << ") LOS objects, "
                  << percent_free << "% free, " << PrettySize(current_heap_size) << "/"
                  << PrettySize(total_memory) << ", " << "paused " << pause_string.str()
                  << " total " << PrettyDuration((duration / 1000) * 1000);
        if (VLOG_IS_ON(heap)) {
            LOG(INFO) << Dumpable<base::TimingLogger>(collector->GetTimings());
        }
    }
  }
//  LOG(INFO) << gc_cause << " " << collector->GetName()
//            << " GC freed "  <<  collector->GetFreedObjects() << "("
//            << PrettySize(collector->GetFreedBytes()) << ") AllocSpace objects, ";
  {
      MutexLock mu(self, *gc_complete_lock_);
      GCP_MARK_POST_COLLECTION;
      is_gc_running_ = false;
      SetLastGCType(gc_type);
      // Wake anyone who may have been waiting for the GC to complete.
      gc_complete_cond_->Broadcast(self);
  }

  ATRACE_END();

  // Inform DDMS that a GC completed.
  Dbg::GcDidFinish();
  return gc_type;
}

void Heap::UpdateAndMarkModUnion(collector::MarkSweep* mark_sweep, base::TimingLogger& timings,
                                 collector::GcType gc_type) {

//  LOG(ERROR) << " ##### Heap::UpdateAndMarkModUnion:: gctype = " << gc_type
//      << "; IPC= " << (mark_sweep->IsInterprocess() ? "true" : "false");
  if (gc_type == collector::kGcTypeSticky) {
    // Don't need to do anything for mod union table in this case since we are only scanning dirty
    // cards.
    return;
  }

  base::TimingLogger::ScopedSplit split("UpdateModUnionTable", &timings);
  // Update zygote mod union table.
  if (gc_type == collector::kGcTypePartial) {
    base::TimingLogger::ScopedSplit split("UpdateZygoteModUnionTable", &timings);
    zygote_mod_union_table_->Update();

    timings.NewSplit("ZygoteMarkReferences");
    zygote_mod_union_table_->MarkReferences(mark_sweep);
  }

  // Processes the cards we cleared earlier and adds their objects into the mod-union table.
  timings.NewSplit("UpdateModUnionTable");
  image_mod_union_table_->Update();

  // Scans all objects in the mod-union table.
  timings.NewSplit("MarkImageToAllocSpaceReferences");
  image_mod_union_table_->MarkReferences(mark_sweep);
}

static void RootMatchesObjectVisitor(const mirror::Object* root, void* arg) {
  mirror::Object* obj = reinterpret_cast<mirror::Object*>(arg);
  if (root == obj) {
    LOG(INFO) << "Object " << obj << " is a root";
  }
}

class ScanVisitor {
 public:
  void operator()(const mirror::Object* obj) const {
    LOG(ERROR) << "Would have rescanned object " << obj;
  }
};

// Verify a reference from an object.
class VerifyReferenceVisitor {
 public:
  explicit VerifyReferenceVisitor(Heap* heap)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_, Locks::heap_bitmap_lock_)
      : heap_(heap), failed_(false) {}

  bool Failed() const {
    return failed_;
  }

  // TODO: Fix lock analysis to not use NO_THREAD_SAFETY_ANALYSIS, requires support for smarter
  // analysis on visitors.
  void operator()(const mirror::Object* obj, const mirror::Object* ref,
                  const MemberOffset& offset, bool /* is_static */) const
      NO_THREAD_SAFETY_ANALYSIS {
    // Verify that the reference is live.
    if (UNLIKELY(ref != NULL && !IsLive(ref))) {
      accounting::CARD_TABLE* card_table = heap_->GetCardTable();
      accounting::ATOMIC_OBJ_STACK_T* alloc_stack = heap_->allocation_stack_.get();
      accounting::ATOMIC_OBJ_STACK_T* live_stack = heap_->live_stack_.get();

      if (!failed_) {
        // Print message on only on first failure to prevent spam.
        LOG(ERROR) << "!!!!!!!!!!!!!!Heap corruption detected!!!!!!!!!!!!!!!!!!!";
        failed_ = true;
      }
      if (obj != nullptr) {
        byte* card_addr = card_table->CardFromAddr(obj);
        LOG(ERROR) << "Object " << obj << " references dead object " << ref << " at offset "
                   << offset << "\n card value = " << static_cast<int>(*card_addr);
        if (heap_->IsHeapAddress(obj->GetClass())) {
          LOG(ERROR) << "Obj type " << PrettyTypeOf(obj);
        } else {
          LOG(ERROR) << "Object " << obj << " class(" << obj->GetClass() << ") not a heap address";
        }

        // Attmept to find the class inside of the recently freed objects.
        space::ContinuousSpace* ref_space = heap_->FindContinuousSpaceFromObject(ref, true);
        if (ref_space->IsDlMallocSpace()) {
          space::DL_MALLOC_SPACE* space = ref_space->AsDlMallocSpace();
          mirror::Class* ref_class = space->FindRecentFreedObject(ref);
          if (ref_class != nullptr) {
            LOG(ERROR) << "Reference " << ref << " found as a recently freed object with class "
                       << PrettyClass(ref_class);
          } else {
            LOG(ERROR) << "Reference " << ref << " not found as a recently freed object";
          }
        }

        if (ref->GetClass() != nullptr && heap_->IsHeapAddress(ref->GetClass()) &&
            ref->GetClass()->IsClass()) {
          LOG(ERROR) << "Ref type " << PrettyTypeOf(ref);
        } else {
          LOG(ERROR) << "Ref " << ref << " class(" << ref->GetClass()
                     << ") is not a valid heap address";
        }

        card_table->CheckAddrIsInCardTable(reinterpret_cast<const byte*>(obj));
        void* cover_begin = card_table->AddrFromCard(card_addr);
        void* cover_end = reinterpret_cast<void*>(reinterpret_cast<size_t>(cover_begin) +
            accounting::ConstantsCardTable::kCardSize);
        LOG(ERROR) << "Card " << reinterpret_cast<void*>(card_addr) << " covers " << cover_begin
            << "-" << cover_end;
#if (ART_GC_SERVICE)
        accounting::BaseBitmap* bitmap = heap_->GetLiveBitmap()->GetContinuousSpaceBitmap(obj);
#else
        accounting::SpaceBitmap* bitmap = heap_->GetLiveBitmap()->GetContinuousSpaceBitmap(obj);
#endif
        // Print out how the object is live.
        if (bitmap != NULL && bitmap->Test(obj)) {
          LOG(ERROR) << "Object " << obj << " found in live bitmap";
        }
        if (alloc_stack->Contains(const_cast<mirror::Object*>(obj))) {
          LOG(ERROR) << "Object " << obj << " found in allocation stack";
        }
        if (live_stack->Contains(const_cast<mirror::Object*>(obj))) {
          LOG(ERROR) << "Object " << obj << " found in live stack";
        }
        if (alloc_stack->Contains(const_cast<mirror::Object*>(ref))) {
          LOG(ERROR) << "Ref " << ref << " found in allocation stack";
        }
        if (live_stack->Contains(const_cast<mirror::Object*>(ref))) {
          LOG(ERROR) << "Ref " << ref << " found in live stack";
        }
        // Attempt to see if the card table missed the reference.
        ScanVisitor scan_visitor;
        byte* byte_cover_begin = reinterpret_cast<byte*>(card_table->AddrFromCard(card_addr));
        card_table->Scan(bitmap, byte_cover_begin,
                         byte_cover_begin + accounting::ConstantsCardTable::kCardSize, scan_visitor);

        // Search to see if any of the roots reference our object.
        void* arg = const_cast<void*>(reinterpret_cast<const void*>(obj));
        Runtime::Current()->VisitRoots(&RootMatchesObjectVisitor, arg, false, false);

        // Search to see if any of the roots reference our reference.
        arg = const_cast<void*>(reinterpret_cast<const void*>(ref));
        Runtime::Current()->VisitRoots(&RootMatchesObjectVisitor, arg, false, false);
      } else {
        LOG(ERROR) << "Root references dead object " << ref << "\nRef type " << PrettyTypeOf(ref);
      }
    }
  }

  bool IsLive(const mirror::Object* obj) const NO_THREAD_SAFETY_ANALYSIS {
    return heap_->IsLiveObjectLocked(obj, true, false, true);
  }

  static void VerifyRoots(const mirror::Object* root, void* arg) {
    VerifyReferenceVisitor* visitor = reinterpret_cast<VerifyReferenceVisitor*>(arg);
    (*visitor)(NULL, root, MemberOffset(0), true);
  }

 private:
  Heap* const heap_;
  mutable bool failed_;
};

// Verify all references within an object, for use with HeapBitmap::Visit.
class VerifyObjectVisitor {
 public:
  explicit VerifyObjectVisitor(Heap* heap) : heap_(heap), failed_(false) {}

  void operator()(const mirror::Object* obj) const
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_, Locks::heap_bitmap_lock_) {
    // Note: we are verifying the references in obj but not obj itself, this is because obj must
    // be live or else how did we find it in the live bitmap?
    VerifyReferenceVisitor visitor(heap_);
    // The class doesn't count as a reference but we should verify it anyways.
    visitor(obj, obj->GetClass(), MemberOffset(0), false);
    collector::MarkSweep::VisitObjectReferences(obj, visitor);
    failed_ = failed_ || visitor.Failed();
  }

  bool Failed() const {
    return failed_;
  }

 private:
  Heap* const heap_;
  mutable bool failed_;
};

// Must do this with mutators suspended since we are directly accessing the allocation stacks.
bool Heap::VerifyHeapReferences() {
  Locks::mutator_lock_->AssertExclusiveHeld(Thread::Current());
  // Lets sort our allocation stacks so that we can efficiently binary search them.
  allocation_stack_->Sort();
  live_stack_->Sort();
  // Perform the verification.
  VerifyObjectVisitor visitor(this);
  Runtime::Current()->VisitRoots(VerifyReferenceVisitor::VerifyRoots, &visitor, false, false);
  GetLiveBitmap()->Visit(visitor);
  // Verify objects in the allocation stack since these will be objects which were:
  // 1. Allocated prior to the GC (pre GC verification).
  // 2. Allocated during the GC (pre sweep GC verification).
  for (mirror::Object** it = allocation_stack_->Begin(); it != allocation_stack_->End(); ++it) {
    visitor(*it);
  }
  // We don't want to verify the objects in the live stack since they themselves may be
  // pointing to dead objects if they are not reachable.
  if (visitor.Failed()) {
    // Dump mod-union tables.
    image_mod_union_table_->Dump(LOG(ERROR) << "Image mod-union table: ");
    zygote_mod_union_table_->Dump(LOG(ERROR) << "Zygote mod-union table: ");
    DumpSpaces();
    return false;
  }
  return true;
}

class VerifyReferenceCardVisitor {
 public:
  VerifyReferenceCardVisitor(Heap* heap, bool* failed)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_,
                            Locks::heap_bitmap_lock_)
      : heap_(heap), failed_(failed) {
  }

  // TODO: Fix lock analysis to not use NO_THREAD_SAFETY_ANALYSIS, requires support for
  // annotalysis on visitors.
  void operator()(const mirror::Object* obj, const mirror::Object* ref, const MemberOffset& offset,
                  bool is_static) const NO_THREAD_SAFETY_ANALYSIS {
    // Filter out class references since changing an object's class does not mark the card as dirty.
    // Also handles large objects, since the only reference they hold is a class reference.
    if (ref != NULL && !ref->IsClass()) {
      accounting::CARD_TABLE* card_table = heap_->GetCardTable();
      // If the object is not dirty and it is referencing something in the live stack other than
      // class, then it must be on a dirty card.
      if (!card_table->AddrIsInCardTable(obj)) {
        LOG(ERROR) << "Object " << obj << " is not in the address range of the card table";
        *failed_ = true;
      } else if (!card_table->IsDirty(obj)) {
        // Card should be either kCardDirty if it got re-dirtied after we aged it, or
        // kCardDirty - 1 if it didnt get touched since we aged it.
        accounting::ATOMIC_OBJ_STACK_T* live_stack = heap_->live_stack_.get();
        if (live_stack->ContainsSorted(const_cast<mirror::Object*>(ref))) {
          if (live_stack->ContainsSorted(const_cast<mirror::Object*>(obj))) {
            LOG(ERROR) << "Object " << obj << " found in live stack";
          }
          if (heap_->GetLiveBitmap()->Test(obj)) {
            LOG(ERROR) << "Object " << obj << " found in live bitmap";
          }
          LOG(ERROR) << "Object " << obj << " " << PrettyTypeOf(obj)
                    << " references " << ref << " " << PrettyTypeOf(ref) << " in live stack";

          // Print which field of the object is dead.
          if (!obj->IsObjectArray()) {
            const mirror::Class* klass = is_static ? obj->AsClass() : obj->GetClass();
            CHECK(klass != NULL);
            const mirror::ObjectArray<mirror::ArtField>* fields = is_static ? klass->GetSFields()
                                                                            : klass->GetIFields();
            CHECK(fields != NULL);
            for (int32_t i = 0; i < fields->GetLength(); ++i) {
              const mirror::ArtField* cur = fields->Get(i);
              if (cur->GetOffset().Int32Value() == offset.Int32Value()) {
                LOG(ERROR) << (is_static ? "Static " : "") << "field in the live stack is "
                          << PrettyField(cur);
                break;
              }
            }
          } else {
            const mirror::ObjectArray<mirror::Object>* object_array =
                obj->AsObjectArray<mirror::Object>();
            for (int32_t i = 0; i < object_array->GetLength(); ++i) {
              if (object_array->Get(i) == ref) {
                LOG(ERROR) << (is_static ? "Static " : "") << "obj[" << i << "] = ref";
              }
            }
          }

          *failed_ = true;
        }
      }
    }
  }

 private:
  Heap* const heap_;
  bool* const failed_;
};

class VerifyLiveStackReferences {
 public:
  explicit VerifyLiveStackReferences(Heap* heap)
      : heap_(heap),
        failed_(false) {}

  void operator()(const mirror::Object* obj) const
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_, Locks::heap_bitmap_lock_) {
    VerifyReferenceCardVisitor visitor(heap_, const_cast<bool*>(&failed_));
    collector::MarkSweep::VisitObjectReferences(obj, visitor);
  }

  bool Failed() const {
    return failed_;
  }

 private:
  Heap* const heap_;
  bool failed_;
};

bool Heap::VerifyMissingCardMarks() {
  Locks::mutator_lock_->AssertExclusiveHeld(Thread::Current());

  // We need to sort the live stack since we binary search it.
  live_stack_->Sort();
  VerifyLiveStackReferences visitor(this);
  GetLiveBitmap()->Visit(visitor);

  // We can verify objects in the live stack since none of these should reference dead objects.
  for (mirror::Object** it = live_stack_->Begin(); it != live_stack_->End(); ++it) {
    visitor(*it);
  }

  if (visitor.Failed()) {
    DumpSpaces();
    return false;
  }
  return true;
}

void Heap::SwapStacks() {
#if (ART_GC_SERVICE)
  if(!accounting::ATOMIC_OBJ_STACK_T::SwapStacks(allocation_stack_.get(),
                                                          live_stack_.get())) {
    allocation_stack_.swap(live_stack_);
  }
#else
  allocation_stack_.swap(live_stack_);
#endif
}

void Heap::ProcessCards(base::TimingLogger& timings) {
  // Clear cards and keep track of cards cleared in the mod-union table.
  for (const auto& space : continuous_spaces_) {
    if (space->IsImageSpace()) {
      base::TimingLogger::ScopedSplit split("ImageModUnionClearCards", &timings);
      image_mod_union_table_->ClearCards(space);
    } else if (space->IsZygoteSpace()) {
      base::TimingLogger::ScopedSplit split("ZygoteModUnionClearCards", &timings);
      zygote_mod_union_table_->ClearCards(space);
    } else {
      base::TimingLogger::ScopedSplit split("AllocSpaceClearCards", &timings);
      // No mod union table for the AllocSpace. Age the cards so that the GC knows that these cards
      // were dirty before the GC started.
      card_table_->ModifyCardsAtomic(space->Begin(), space->End(),
                                              AgeCardVisitor(), VoidFunctor());
    }
  }
}

void Heap::PreGcVerification(collector::GarbageCollector* gc) {
  ThreadList* thread_list = Runtime::Current()->GetThreadList();
  Thread* self = Thread::Current();

  if (verify_pre_gc_heap_) {
    thread_list->SuspendAll();
    {
      ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
      if (!VerifyHeapReferences()) {
        LOG(FATAL) << "Pre " << gc->GetName() << " heap verification failed";
      }
    }
    thread_list->ResumeAll();
  }

  // Check that all objects which reference things in the live stack are on dirty cards.
  if (verify_missing_card_marks_) {
    thread_list->SuspendAll();
    {
      ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
      SwapStacks();
      // Sort the live stack so that we can quickly binary search it later.
      if (!VerifyMissingCardMarks()) {
        LOG(FATAL) << "Pre " << gc->GetName() << " missing card mark verification failed";
      }
      SwapStacks();
    }
    thread_list->ResumeAll();
  }

  if (verify_mod_union_table_) {
    thread_list->SuspendAll();
    ReaderMutexLock reader_lock(self, *Locks::heap_bitmap_lock_);
    zygote_mod_union_table_->Update();
    zygote_mod_union_table_->Verify();
    image_mod_union_table_->Update();
    image_mod_union_table_->Verify();
    thread_list->ResumeAll();
  }
}

void Heap::PreSweepingGcVerification(collector::GarbageCollector* gc) {
  // Called before sweeping occurs since we want to make sure we are not going so reclaim any
  // reachable objects.
  if (verify_post_gc_heap_) {
    Thread* self = Thread::Current();
    CHECK_NE(self->GetState(), kRunnable);
    {
      WriterMutexLock mu(self, *Locks::heap_bitmap_lock_);
      // Swapping bound bitmaps does nothing.
      gc->SwapBitmaps();
      if (!VerifyHeapReferences()) {
        LOG(FATAL) << "Pre sweeping " << gc->GetName() << " GC verification failed";
      }
      gc->SwapBitmaps();
    }
  }
}

void Heap::gcpIncMutationCnt(void) {
  mprofiler::VMProfiler* mP = Runtime::Current()->GetVMProfiler();
  if(LIKELY(mP == NULL || !mP->IsProfilingRunning()))
    return;
	art::mprofiler::GCHistogramDataManager::GCPIncMutations(Thread::Current());
}

void Heap::gcpIncMutationCnt(const mirror::Object* dst, size_t elementPos,
		size_t length) {
	mprofiler::VMProfiler* mP = Runtime::Current()->GetVMProfiler();
	if(LIKELY(mP == NULL || !mP->IsProfilingRunning()))
		return;
	mirror::Class* klass = dst->GetClass();
	if(klass == NULL)
		return;
	if (UNLIKELY(klass->IsArrayClass())) {
		if (klass->IsObjectArrayClass()) {
			const mirror::ObjectArray<mirror::Object>* array =
					dst->AsObjectArray<mirror::Object>();
			MemberOffset
			dst_offset(mirror::Array::DataOffset(sizeof(mirror::Object*)).Int32Value() +
					elementPos * sizeof(mirror::Object*));
			for (size_t i = elementPos; i < length; ++i) {
//				 const mirror::Object* element =
//						 array->GetWithoutChecks(static_cast<int32_t>(i));
				mirror::Object* object =
				              array->GetFieldObject<mirror::Object*>(dst_offset, false);
				if(object == NULL)
					 continue;
				 //size_t width = sizeof(mirror::Object*);
				 //MemberOffset offset(i * width + mirror::Array::DataOffset(width).Int32Value());
				 mprofiler::VMProfiler::MProfRefDistance(array, dst_offset.Uint32Value(), object);
				 dst_offset = MemberOffset(dst_offset.Uint32Value() + sizeof(mirror::Object*));
			}
		}
	}
}


void Heap::gcpIncMutationCnt(const mirror::Object* dst, MemberOffset offset,
		const mirror::Object* new_value) {
	mprofiler::VMProfiler::MProfRefDistance(dst, offset.Uint32Value(), new_value);
}

void Heap::PostGcVerification(collector::GarbageCollector* gc) {
  if (verify_system_weaks_) {
    Thread* self = Thread::Current();
    ReaderMutexLock mu(self, *Locks::heap_bitmap_lock_);
    collector::MarkSweep* mark_sweep = down_cast<collector::MarkSweep*>(gc);
    mark_sweep->VerifySystemWeaks();
  }
}

collector::GcType Heap::WaitForConcurrentGcToComplete(Thread* self, bool profWaitTime) {
#if (ART_GC_SERVICE)
  collector::GcType returned_gc_type = collector::kGcTypeNone;
  if(GCServiceClient::RequestWaitForConcurrentGC(&returned_gc_type)) {
    return returned_gc_type;
  }
#endif
  collector::GcType last_gc_type = collector::kGcTypeNone;
  if (concurrent_gc_) {
    ATRACE_BEGIN("GC: Wait For Concurrent");
    bool do_wait;
    if(profWaitTime)
      GCP_MARK_START_WAIT_TIME_EVENT(self);
    uint64_t wait_start = NanoTime();
    {
      // Check if GC is running holding gc_complete_lock_.
      MutexLock mu(self, *gc_complete_lock_);
      do_wait = is_gc_running_;
    }
    if (do_wait) {
      uint64_t wait_time;
      // We must wait, change thread state then sleep on gc_complete_cond_;
      ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGcToComplete);
      {
        MutexLock mu(self, *gc_complete_lock_);
        while (is_gc_running_) {
          gc_complete_cond_->Wait(self);
        }
        last_gc_type = GetLastGCType();
        wait_time = NanoTime() - wait_start;
        IncTotalWaitTime(wait_time);
      }
      if (wait_time > long_pause_log_threshold_) {
        LOG(INFO) << "WaitForConcurrentGcToComplete blocked for " << PrettyDuration(wait_time);
      }
    }
    if(profWaitTime)
      GCP_MARK_END_WAIT_TIME_EVENT(self);
    ATRACE_END();
  }
  return last_gc_type;
}

void Heap::DumpForSigQuit(std::ostream& os) {
  os << "Heap: " << GetPercentFree() << "% free, " << PrettySize(GetBytesAllocated()) << "/"
     << PrettySize(GetTotalMemory()) << "; " << GetObjectsAllocated() << " objects\n";
  DumpGcPerformanceInfo(os);
}

size_t Heap::GetPercentFree() {
  return static_cast<size_t>(100.0f * static_cast<float>(GetFreeMemory()) / GetTotalMemory());
}

size_t Heap::GetConcStartBytes(bool forProfile) const {
  size_t _conc_start = GetConcStartBytesValue();
  if(!forProfile) {
    return _conc_start;
  }
  if(_conc_start == std::numeric_limits<size_t>::max()) {
    return GetMaxAllowedFootPrint();
  }
	return _conc_start;
}


void Heap::SetIdealFootprint(size_t max_allowed_footprint) {
  if (max_allowed_footprint > GetMaxMemory()) {
    VLOG(gc)  << "Clamp target GC heap from "
              << PrettySize(max_allowed_footprint) << " to "
              << PrettySize(GetMaxMemory());
    max_allowed_footprint = GetMaxMemory();
  }
  SetMaxAllowedFootPrint(max_allowed_footprint);
}

void Heap::UpdateMaxNativeFootprint() {
  size_t native_size = GetNativeBytesAllocated();
  // TODO: Tune the native heap utilization to be a value other than the java heap utilization.
  size_t target_size = native_size / GetTargetHeapUtilization();
  if (target_size > native_size + GetMaxFree()) {
    target_size = native_size + GetMaxFree();
  } else if (target_size < native_size + GetMinFree()) {
    target_size = native_size + GetMinFree();
  }
  SetNativeFootPrintGCWaterMark(target_size);
  SetNativeFootPrintLimit(2 * target_size - native_size);
}

void Heap::GCSrvcUpdateMaxNativeFootprint(size_t min_free, size_t max_free) {
  size_t native_size = GetNativeBytesAllocated();
  // TODO: Tune the native heap utilization to be a value other than the java heap utilization.
  size_t target_size = native_size / GetTargetHeapUtilization();
  if (target_size > native_size + max_free) {
    target_size = native_size + max_free;
  } else if (target_size < native_size + min_free) {
    target_size = native_size + min_free;
  }
  SetNativeFootPrintGCWaterMark(target_size);
  SetNativeFootPrintLimit(2 * target_size - native_size);
}


// How much more we grow the heap when we are a foreground app instead of background.
static double foreground_heap_growth_multiplier_ = 1.0;//5;

double Heap::HeapGrowthMultiplier() const {
  // If we don't care about pause times we are background, so return 1.0.
  if (!CareAboutPauseTimes() || IsLowMemoryMode()) {
    return 1.0;
  }
  return foreground_heap_growth_multiplier_;
}

void Heap::GCSrvcGrowForUtilization(collector::GcType gc_type,
                                    uint64_t gc_duration,
                                    double adjusted_resize_factor,
                                    size_t* adjusted_max_free_p,
                                    double conc_latency) {
  // We know what our utilization is at this moment.
  // This doesn't actually resize any memory. It just lets the heap grow more when necessary.
  const size_t bytes_allocated = GetBytesAllocated();
//  LOG(ERROR) << "Heap::GCSrvcGrowForUtilization..bytes_allocated=" << bytes_allocated <<
//      ", alloc_space_->GetBytesAllocated=" << alloc_space_->GetBytesAllocated();
  size_t target_size;
  //const double multiplier = HeapGrowthMultiplier();  // Use the multiplier to grow more for
  // foreground.
  const size_t adjusted_min_free = static_cast<size_t>(GetMinFree() * adjusted_resize_factor);
  const size_t adjusted_max_free = static_cast<size_t>(GetMaxFree() * adjusted_resize_factor);
  *adjusted_max_free_p = adjusted_max_free;

  SetLastGCSize(bytes_allocated);
  SetLastGCTime(NanoTime());
//  LOG(ERROR) << "GCSrvcGrowForUtilization ....... GCtype is " << gc_type;
  if (gc_type != collector::kGcTypeSticky) {
    // Grow the heap for non sticky GC.
    target_size = bytes_allocated / GetTargetHeapUtilization();
    if (target_size > bytes_allocated + adjusted_max_free) {
      target_size = bytes_allocated + adjusted_max_free;
    } else if (target_size < bytes_allocated + adjusted_min_free) {
      target_size = bytes_allocated + adjusted_min_free;
    }
    SetNextGCType(collector::kGcTypeSticky);
  } else {
    // Based on how close the current heap size is to the target size, decide
    // whether or not to do a partial or sticky GC next.
    if (bytes_allocated + adjusted_min_free <= GetMaxAllowedFootPrint()) {
      SetNextGCType(collector::kGcTypeSticky);
    } else {
      SetNextGCType(collector::kGcTypePartial);
    }

    // If we have freed enough memory, shrink the heap back down.
    if (bytes_allocated + adjusted_max_free < GetMaxAllowedFootPrint()) {
      target_size = bytes_allocated + adjusted_max_free;
    } else {
      target_size = std::max(bytes_allocated, GetMaxAllowedFootPrint());
    }
  }


  if (!ignore_max_footprint_) {
    SetIdealFootprint(target_size);

    if (concurrent_gc_) {
      // Calculate when to perform the next ConcurrentGC.

      // Calculate the estimated GC duration.
      double gc_duration_seconds = NsToMs(gc_duration) / 1000.0;
      // Estimate how many remaining bytes we will have when we need to start the next GC.
      size_t remaining_bytes = GetAllocationRate() * gc_duration_seconds;
      size_t _conc_lead = static_cast<size_t>(conc_latency);//gc_duration_seconds;
      //LOG(ERROR) << "---remaining_bytes=" << remaining_bytes<< ", conc_lead=" <<_conc_lead;
      remaining_bytes = _conc_lead + std::max(remaining_bytes, kMinConcurrentRemainingBytes);
      //LOG(ERROR) << "updated_remaining_bytes="<<remaining_bytes;
      if (UNLIKELY(remaining_bytes > GetMaxAllowedFootPrint())) {
        // A never going to happen situation that from the estimated allocation rate we will exceed
        // the applications entire footprint with the given estimated allocation rate. Schedule
        // another GC straight away.
        SetConcStartBytes(bytes_allocated);
      } else {
        // Start a concurrent GC when we get close to the estimated remaining bytes. When the
        // allocation rate is very high, remaining_bytes could tell us that we should start a GC
        // right away.
        SetConcStartBytes(std::max(GetMaxAllowedFootPrint() - remaining_bytes, bytes_allocated));
      }
      DCHECK_LE(GetConcStartBytes(), GetMaxAllowedFootPrint());
      DCHECK_LE(GetMaxAllowedFootPrint(), GetGrowthLimit());
    }
  }
  GCSrvcUpdateMaxNativeFootprint(adjusted_min_free, adjusted_max_free);

}


void Heap::GrowForUtilization(collector::GcType gc_type, uint64_t gc_duration) {
  // We know what our utilization is at this moment.
  // This doesn't actually resize any memory. It just lets the heap grow more when necessary.
  const size_t bytes_allocated = GetBytesAllocated();
  SetLastGCSize(bytes_allocated);
  SetLastGCTime(NanoTime());

  size_t target_size;
  if (gc_type != collector::kGcTypeSticky) {
    // Grow the heap for non sticky GC.
    target_size = bytes_allocated / GetTargetHeapUtilization();
    if (target_size > bytes_allocated + GetMaxFree()) {
      target_size = bytes_allocated + GetMaxFree();
    } else if (target_size < bytes_allocated + GetMinFree()) {
      target_size = bytes_allocated + GetMinFree();
    }
    SetNextGCType(collector::kGcTypeSticky);
  } else {
    // Based on how close the current heap size is to the target size, decide
    // whether or not to do a partial or sticky GC next.
    if (bytes_allocated + GetMinFree() <= GetMaxAllowedFootPrint()) {
      SetNextGCType(collector::kGcTypeSticky);
    } else {
      SetNextGCType(collector::kGcTypePartial);
    }

    // If we have freed enough memory, shrink the heap back down.
    if (bytes_allocated + GetMaxFree() < GetMaxAllowedFootPrint()) {
      target_size = bytes_allocated + GetMaxFree();
    } else {
      target_size = std::max(bytes_allocated, GetMaxAllowedFootPrint());
    }
  }

  if (!ignore_max_footprint_) {
    SetIdealFootprint(target_size);

    if (concurrent_gc_) {
      // Calculate when to perform the next ConcurrentGC.

      // Calculate the estimated GC duration.
      double gc_duration_seconds = NsToMs(gc_duration) / 1000.0;
      // Estimate how many remaining bytes we will have when we need to start the next GC.
      size_t remaining_bytes = GetAllocationRate() * gc_duration_seconds;
      remaining_bytes = std::max(remaining_bytes, kMinConcurrentRemainingBytes);
      if (UNLIKELY(remaining_bytes > GetMaxAllowedFootPrint())) {
        // A never going to happen situation that from the estimated allocation rate we will exceed
        // the applications entire footprint with the given estimated allocation rate. Schedule
        // another GC straight away.
        SetConcStartBytes(bytes_allocated);
      } else {
        // Start a concurrent GC when we get close to the estimated remaining bytes. When the
        // allocation rate is very high, remaining_bytes could tell us that we should start a GC
        // right away.
        SetConcStartBytes(std::max(GetMaxAllowedFootPrint() - remaining_bytes, bytes_allocated));
      }
      DCHECK_LE(GetConcStartBytes(), GetMaxAllowedFootPrint());
      DCHECK_LE(GetMaxAllowedFootPrint(), GetGrowthLimit());
    }
  }

  UpdateMaxNativeFootprint();
}


//void Heap::SetNextGCType(collector::GcType gc_type) {
//  if(!art::gc::service::GCServiceClient::SetNextGCType(gc_type)) {
//    next_gc_type_ = gc_type;
//  }
//
//}

//void Heap::SetConcurrentStartBytes(size_t new_value) {
//  if(!art::gc::service::GCServiceClient::SetConcStartBytes(new_value)) {
//    SetConcStartBytes(new_value);
//  }
//}

//size_t Heap::GetConcurrentStartBytes(void) {
//  size_t return_val = 0;
//  if(!art::gc::service::GCServiceClient::GetConcStartBytes(&return_val)) {
//    return_val = GetConcStartBytes();
//  }
//  return return_val;
//}

//collector::GcType Heap::GetNextGCType(void) {
//  collector::GcType gc_type;
//
//  if(!art::gc::service::GCServiceClient::GetNextGCType(&gc_type)) {
//    return next_gc_type_;
//  }
//  return gc_type;
//}

void Heap::ClearGrowthLimit() {
  SetGrowthLimit(capacity_);
  alloc_space_->ClearGrowthLimit();
}

void Heap::SetReferenceOffsets(MemberOffset reference_referent_offset,
                                MemberOffset reference_queue_offset,
                                MemberOffset reference_queueNext_offset,
                                MemberOffset reference_pendingNext_offset,
                                MemberOffset finalizer_reference_zombie_offset) {
  reference_referent_offset_ = reference_referent_offset;
  reference_queue_offset_ = reference_queue_offset;
  reference_queueNext_offset_ = reference_queueNext_offset;
  reference_pendingNext_offset_ = reference_pendingNext_offset;
  finalizer_reference_zombie_offset_ = finalizer_reference_zombie_offset;
  CHECK_NE(reference_referent_offset_.Uint32Value(), 0U);
  CHECK_NE(reference_queue_offset_.Uint32Value(), 0U);
  CHECK_NE(reference_queueNext_offset_.Uint32Value(), 0U);
  CHECK_NE(reference_pendingNext_offset_.Uint32Value(), 0U);
  CHECK_NE(finalizer_reference_zombie_offset_.Uint32Value(), 0U);
}

mirror::Object* Heap::GetReferenceReferent(mirror::Object* reference) {
  DCHECK(reference != NULL);
  DCHECK_NE(reference_referent_offset_.Uint32Value(), 0U);
  return reference->GetFieldObject<mirror::Object*>(reference_referent_offset_, true);
}




void Heap::ClearReferenceReferent(mirror::Object* reference) {
  DCHECK(reference != NULL);
  DCHECK_NE(reference_referent_offset_.Uint32Value(), 0U);
  reference->SetFieldObject(reference_referent_offset_, NULL, true);
}

// Returns true if the reference object has not yet been enqueued.
bool Heap::IsEnqueuable(const mirror::Object* ref) {
  DCHECK(ref != NULL);
  const mirror::Object* queue =
      ref->GetFieldObject<mirror::Object*>(reference_queue_offset_, false);
  const mirror::Object* queue_next =
      ref->GetFieldObject<mirror::Object*>(reference_queueNext_offset_, false);
  return (queue != NULL) && (queue_next == NULL);
}

void Heap::EnqueueReference(mirror::Object* ref, mirror::Object** cleared_reference_list) {
  DCHECK(ref != NULL);
  CHECK(ref->GetFieldObject<mirror::Object*>(reference_queue_offset_, false) != NULL);
  CHECK(ref->GetFieldObject<mirror::Object*>(reference_queueNext_offset_, false) == NULL);
  EnqueuePendingReference(ref, cleared_reference_list);
}

bool Heap::IsEnqueued(mirror::Object* ref) {
  // Since the references are stored as cyclic lists it means that once enqueued, the pending next
  // will always be non-null.
  return ref->GetFieldObject<mirror::Object*>(GetReferencePendingNextOffset(), false) != nullptr;
}


bool Heap::IsEnqueuedNoLock(mirror::Object* ref) {
  // Since the references are stored as cyclic lists it means that once enqueued, the pending next
  // will always be non-null.
  return ref->GetFieldObject<mirror::Object*>(GetReferencePendingNextOffset(), false) != nullptr;
}

void Heap::EnqueuePendingReference(mirror::Object* ref, mirror::Object** list) {
  DCHECK(ref != NULL);
  DCHECK(list != NULL);
  if (*list == NULL) {
    // 1 element cyclic queue, ie: Reference ref = ..; ref.pendingNext = ref;
    ref->SetFieldObject(reference_pendingNext_offset_, ref, false);
    *list = ref;
  } else {
    mirror::Object* head =
        (*list)->GetFieldObject<mirror::Object*>(reference_pendingNext_offset_, false);
    ref->SetFieldObject(reference_pendingNext_offset_, head, false);
    (*list)->SetFieldObject(reference_pendingNext_offset_, ref, false);
  }
}

#if (ART_GC_SERVICE)
void Heap::EnqueuePendingReferenceNoLock(mirror::Object* ref, mirror::Object** list) {
  DCHECK(ref != NULL);
  DCHECK(list != NULL);
  if (*list == NULL) {
    // 1 element cyclic queue, ie: Reference ref = ..; ref.pendingNext = ref;
    ref->SetFieldObject(reference_pendingNext_offset_, ref, false);
    *list = ref;
  } else {
    mirror::Object* head =
        (*list)->GetFieldObject<mirror::Object*>(reference_pendingNext_offset_, false);
    ref->SetFieldObject(reference_pendingNext_offset_, head, false);
    (*list)->SetFieldObject(reference_pendingNext_offset_, ref, false);
  }
}
#endif

mirror::Object* Heap::DequeuePendingReference(mirror::Object** list) {
  DCHECK(list != NULL);
  DCHECK(*list != NULL);
  mirror::Object* head = (*list)->GetFieldObject<mirror::Object*>(reference_pendingNext_offset_,
                                                                  false);
  mirror::Object* ref;

  // Note: the following code is thread-safe because it is only called from ProcessReferences which
  // is single threaded.
  if (*list == head) {
    ref = *list;
    *list = NULL;
  } else {
    mirror::Object* next = head->GetFieldObject<mirror::Object*>(reference_pendingNext_offset_,
                                                                 false);
    (*list)->SetFieldObject(reference_pendingNext_offset_, next, false);
    ref = head;
  }
  ref->SetFieldObject(reference_pendingNext_offset_, NULL, false);
  return ref;
}

void Heap::AddFinalizerReference(Thread* self, mirror::Object* object) {
  ScopedObjectAccess soa(self);
  JValue result;
  ArgArray arg_array(NULL, 0);
  arg_array.Append(reinterpret_cast<uint32_t>(object));
  soa.DecodeMethod(WellKnownClasses::java_lang_ref_FinalizerReference_add)->Invoke(self,
      arg_array.GetArray(), arg_array.GetNumBytes(), &result, 'V');
}

void Heap::EnqueueClearedReferences(mirror::Object** cleared) {
  DCHECK(cleared != NULL);
  if (*cleared != NULL) {
    // When a runtime isn't started there are no reference queues to care about so ignore.
    if (LIKELY(Runtime::Current()->IsStarted())) {
      ScopedObjectAccess soa(Thread::Current());
      JValue result;
      ArgArray arg_array(NULL, 0);
      arg_array.Append(reinterpret_cast<uint32_t>(*cleared));
      soa.DecodeMethod(WellKnownClasses::java_lang_ref_ReferenceQueue_add)->Invoke(soa.Self(),
          arg_array.GetArray(), arg_array.GetNumBytes(), &result, 'V');
    }
    *cleared = NULL;
  }
}


void Heap::GCServiceSignalConcGC(Thread* self) {
  // Make sure that we can do a concurrent GC.
  LOG(ERROR) << "Signaling ConcurrentGC Request";
  Runtime* runtime = Runtime::Current();
  DCHECK(concurrent_gc_);
  if (runtime == NULL || !runtime->IsFinishedStarting() ||
      !runtime->IsConcurrentGcEnabled()) {
    return;
  }
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    if (runtime->IsShuttingDown()) {
      return;
    }
  }
  if (self->IsHandlingStackOverflow()) {
    return;
  }

  JNIEnv* env = self->GetJniEnv();
  DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
  DCHECK(WellKnownClasses::java_lang_Daemons_requestGC != NULL);
  env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
                            WellKnownClasses::java_lang_Daemons_requestGC);
  CHECK(!env->ExceptionCheck());
  return;
}

void Heap::RequestConcurrentGC(Thread* self) {
  // Make sure that we can do a concurrent GC.
  Runtime* runtime = Runtime::Current();
  DCHECK(concurrent_gc_);
  if (runtime == NULL || !runtime->IsFinishedStarting() ||
      !runtime->IsConcurrentGcEnabled()) {
    return;
  }
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    if (runtime->IsShuttingDown()) {
      return;
    }
  }
  if (self->IsHandlingStackOverflow()) {
    return;
  }

  // We already have a request pending, no reason to start more until we update
  // concurrent_start_bytes_.
  SetConcStartBytes(std::numeric_limits<size_t>::max());

#if (ART_GC_SERVICE)
  if(!GCServiceClient::RequestConcGC()) {
    JNIEnv* env = self->GetJniEnv();
    DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
    DCHECK(WellKnownClasses::java_lang_Daemons_requestGC != NULL);
    env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
                              WellKnownClasses::java_lang_Daemons_requestGC);
    CHECK(!env->ExceptionCheck());
    return;
  } else {
    /**/
   // LOG(ERROR) << "Skipping request ConcGC without handshaking the GCService";
//    JNIEnv* env = self->GetJniEnv();
//    DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
//    DCHECK(WellKnownClasses::java_lang_Daemons_requestGC != NULL);
//    env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
//                              WellKnownClasses::java_lang_Daemons_requestGC);
//    CHECK(!env->ExceptionCheck());
  }
#else
  JNIEnv* env = self->GetJniEnv();
  DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
  DCHECK(WellKnownClasses::java_lang_Daemons_requestGC != NULL);
  env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
                            WellKnownClasses::java_lang_Daemons_requestGC);
  CHECK(!env->ExceptionCheck());
#endif
}

void Heap::ConcurrentGC(Thread* self) {

  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    if (Runtime::Current()->IsShuttingDown()) {
      return;
    }
  }
  //LOG(ERROR) << "<<<<vmprofiler: concurrent: "<< self->GetTid();
  GCP_MARK_START_CONC_GC_HW_EVENT;
  //mprofiler::VMProfiler::MProfMarkGCHatTimeEvent(self);
  // Wait for any GCs currently running to finish.
  if (WaitForConcurrentGcToComplete(self, false) == collector::kGcTypeNone) {
    CollectGarbageInternal(GetNextGCType(), kGcCauseBackground, false);
  }
  GCP_MARK_END_CONC_GC_HW_EVENT;
  //LOG(ERROR) << ">>>vmprofiler: concurrent: "<< self->GetTid();
  //mprofiler::VMProfiler::MProfMarkEndGCHatTimeEvent(self);
}


bool Heap::RequestHeapTrimIfNeeded(size_t adjusted_max_free,
                                   bool care_about_pauses,
                                   bool send_remote_req) {
  uint64_t ms_time = MilliTime();
  float utilization =
      static_cast<float>(alloc_space_->GetBytesAllocated()) / alloc_space_->Size();

  if ((utilization > 0.75f && !IsLowMemoryMode()) || ((ms_time - GetLastTimeTrim()) < 2 * 1000)) {
    // Don't bother trimming the alloc space if it's more than 75% utilized and low memory mode is
    // not enabled, or if a heap trim occurred in the last two seconds.
    return false;
  }


  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    Runtime* runtime = Runtime::Current();
    if (runtime == NULL || !runtime->IsFinishedStarting() || runtime->IsShuttingDown()) {
      // Heap trimming isn't supported without a Java runtime or Daemons (such as at dex2oat time)
      // Also: we do not wish to start a heap trim if the runtime is shutting down (a racy check
      // as we don't hold the lock while requesting the trim).
      return false;
    }
  }
//  SetLastTimeTrim(ms_time);

  if (care_about_pauses) {
    return false;
  }



#if (ART_GC_SERVICE)
  if(send_remote_req) {
    //LOG(ERROR) << "Heap::RequestHeapTrimIfNeeded()";
    GCServiceClient::RequestHeapTrim();
  }
#endif

  return true;
//  LOG(ERROR) << "RequestHeapTrimIfNeeded: passes care about pause times";
//   if(false){
//
//
//
//
//   }
//    JNIEnv* env = self->GetJniEnv();
//    DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
//    DCHECK(WellKnownClasses::java_lang_Daemons_requestHeapTrim != NULL);
//    env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
//                              WellKnownClasses::java_lang_Daemons_requestHeapTrim);
//    CHECK(!env->ExceptionCheck());
//
//    return true;
}

void Heap::RequestHeapTrim() {
  // GC completed and now we must decide whether to request a heap trim (advising pages back to the
  // kernel) or not. Issuing a request will also cause trimming of the libc heap. As a trim scans
  // a space it will hold its lock and can become a cause of jank.
  // Note, the large object space self trims and the Zygote space was trimmed and unchanging since
  // forking.

  // We don't have a good measure of how worthwhile a trim might be. We can't use the live bitmap
  // because that only marks object heads, so a large array looks like lots of empty space. We
  // don't just call dlmalloc all the time, because the cost of an _attempted_ trim is proportional
  // to utilization (which is probably inversely proportional to how much benefit we can expect).
  // We could try mincore(2) but that's only a measure of how many pages we haven't given away,
  // not how much use we're making of those pages.
  uint64_t ms_time = MilliTime();
  float utilization =
      static_cast<float>(alloc_space_->GetBytesAllocated()) / alloc_space_->Size();
//  LOG(ERROR) << "Heap::RequestHeapTrim: utilization=" << utilization
//        << ", delta_time=" << (((ms_time - GetLastTimeTrim()) < 2 * 1000) ? "true": "false");
  if ((utilization > 0.75f && !IsLowMemoryMode()) || ((ms_time - GetLastTimeTrim()) < 2 * 1000)) {
    // Don't bother trimming the alloc space if it's more than 75% utilized and low memory mode is
    // not enabled, or if a heap trim occurred in the last two seconds.
    return;
  }

  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::runtime_shutdown_lock_);
    Runtime* runtime = Runtime::Current();
    if (runtime == NULL || !runtime->IsFinishedStarting() || runtime->IsShuttingDown()) {
      // Heap trimming isn't supported without a Java runtime or Daemons (such as at dex2oat time)
      // Also: we do not wish to start a heap trim if the runtime is shutting down (a racy check
      // as we don't hold the lock while requesting the trim).
      return;
    }
  }

  SetLastTimeTrim(ms_time);
  ListenForProcessStateChange();
  //LOG(ERROR) << "--------- Request Heap::Trim() ; care about pause time-------------";
  // Trim only if we do not currently care about pause times.
  if (!care_about_pause_times_) {

   //LOG(ERROR) << "--------- Request Heap::Trim() ; no care about pause time-------------";
   if(false){
     #if (ART_GC_SERVICE)

      GCServiceClient::RequestHeapTrim();
    #endif
   }
    JNIEnv* env = self->GetJniEnv();
    DCHECK(WellKnownClasses::java_lang_Daemons != NULL);
    DCHECK(WellKnownClasses::java_lang_Daemons_requestHeapTrim != NULL);
    env->CallStaticVoidMethod(WellKnownClasses::java_lang_Daemons,
                              WellKnownClasses::java_lang_Daemons_requestHeapTrim);
    CHECK(!env->ExceptionCheck());
  }
}

size_t Heap::Trim() {
//	LOG(ERROR) << "--------- Heap::Trim() -------------";
  //GCP_MARK_START_TRIM_HW_EVENT;
  // Handle a requested heap trim on a thread outside of the main GC thread.
  size_t _trimmed = alloc_space_->Trim();

  //GCP_MARK_END_TRIM_HW_EVENT;
  return _trimmed;
}

bool Heap::IsGCRequestPending() const {
 // size_t return_val = 0;
 // if(!art::gc::service::GCServiceClient::GetConcStartBytes(&return_val)) {
//  size_t return_val = GetConcStartBytes();
 // }
  return (std::numeric_limits<size_t>::max() != GetConcStartBytes());
}

void Heap::RegisterNativeAllocation(int bytes) {
  // Total number of native bytes allocated.
  IncAtomicNativeBytesAllocated(bytes);
  Thread* self = Thread::Current();
  if (static_cast<size_t>(GetNativeBytesAllocated()) > GetNativeFootPrintGCWaterMark()) {
    // The second watermark is higher than the gc watermark. If you hit this it means you are
    // allocating native objects faster than the GC can keep up with.
    if (static_cast<size_t>(GetNativeBytesAllocated()) > GetNativeFootPrintLimit()) {
        JNIEnv* env = self->GetJniEnv();
        // Can't do this in WellKnownClasses::Init since System is not properly set up at that
        // point.
        if (WellKnownClasses::java_lang_System_runFinalization == NULL) {
          DCHECK(WellKnownClasses::java_lang_System != NULL);
          WellKnownClasses::java_lang_System_runFinalization =
              CacheMethod(env, WellKnownClasses::java_lang_System, true, "runFinalization", "()V");
          assert(WellKnownClasses::java_lang_System_runFinalization != NULL);
        }
        if (WaitForConcurrentGcToComplete(self, true) != collector::kGcTypeNone) {
          // Just finished a GC, attempt to run finalizers.
          env->CallStaticVoidMethod(WellKnownClasses::java_lang_System,
                                    WellKnownClasses::java_lang_System_runFinalization);
          CHECK(!env->ExceptionCheck());
        }

        // If we still are over the watermark, attempt a GC for alloc and run finalizers.
        if (static_cast<size_t>(GetNativeBytesAllocated()) > GetNativeFootPrintLimit()) {
          GCP_MARK_START_GC_HAT_TIME_EVENT(self);
        	GCP_MARK_START_ALLOC_GC_HW_EVENT;
#if (ART_GC_SERVICE)
    LOG(ERROR) << "Heap::RegisterNativeAllocation-START";
#endif
          CollectGarbageInternal(collector::kGcTypePartial, kGcCauseForAlloc, false);
#if (ART_GC_SERVICE)
    LOG(ERROR) << "Heap::RegisterNativeAllocation-------";
#endif
          GCP_MARK_END_GC_HAT_TIME_EVENT(self);
          GCP_MARK_END_ALLOC_GC_HW_EVENT;
          env->CallStaticVoidMethod(WellKnownClasses::java_lang_System,
                                    WellKnownClasses::java_lang_System_runFinalization);
          CHECK(!env->ExceptionCheck());
        }
        // We have just run finalizers, update the native watermark since it is very likely that
        // finalizers released native managed allocations.
        UpdateMaxNativeFootprint();
    } else {
      if (!IsGCRequestPending()) {
        RequestConcurrentGC(self);
      }
    }
  }
}

void Heap::RegisterNativeFree(int bytes) {
  int expected_size, new_size;
  do {
      expected_size = GetAtomicNativeBytesAllocated();
      new_size = expected_size - bytes;
      if (new_size < 0) {
        ThrowRuntimeException("attempted to free %d native bytes with only %d native bytes registered as allocated",
                              bytes, expected_size);
        break;
      }
  } while (!CASAtomicNativeBytesAllocated(expected_size, new_size));
}

int64_t Heap::GetTotalMemory() const {
  int64_t ret = 0;
  for (const auto& space : continuous_spaces_) {
    if (space->IsImageSpace()) {
      // Currently don't include the image space.
    } else if (space->IsDlMallocSpace()) {
      // Zygote or alloc space
      ret += space->AsDlMallocSpace()->GetFootprint();
    }
  }
  for (const auto& space : discontinuous_spaces_) {
    if (space->IsLargeObjectSpace()) {
      ret += space->AsLargeObjectSpace()->GetBytesAllocated();
    }
  }
  return ret;
}

#if (ART_GC_SERVICE)
void Heap::SetSubHeapMetaData(space::GCSrvcHeapSubRecord* new_address) {
  memcpy(new_address, sub_record_meta_, sizeof(space::GCSrvcHeapSubRecord));
  free(sub_record_meta_);
  sub_record_meta_ = new_address;
}
#endif

}  // namespace gc
}  // namespace art
