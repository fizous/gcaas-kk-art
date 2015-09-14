/*
 * service_space.cc
 *
 *  Created on: Sep 12, 2015
 *      Author: hussein
 */


#include "utils.h"
#include "runtime.h"
#include "gc/space/space.h"
#include "gc/service/service_space.h"
#include "gc/allocator/dlmalloc.h"
#include "gc/space/dlmalloc_space.h"
#include "mirror/object-inl.h"
#include "gc/service/global_allocator.h"

namespace art {
namespace gc {
namespace space {


#define CHECK_SHARED_MEMORY_CALL(call, args, what) \
  do { \
    int rc = call args; \
    if (UNLIKELY(rc != 0)) { \
      errno = rc; \
      PLOG(FATAL) << # call << " failed for " << what; \
    } \
  } while (false)

size_t GCSrvceDlMallocSpace::bitmap_index_ = 0;
static const bool kPrefetchDuringSharedDlMallocSpaceFreeList = true;


void SharedDlMallocSpace::Dump(std::ostream& os) const {
  os << GetType()
      << " begin=" << reinterpret_cast<void*>(Begin())
      << ",end=" << reinterpret_cast<void*>(End())
      << ",size=" << PrettySize(Size()) << ",capacity=" << PrettySize(Capacity())
      << ",name=\"" << GetName() << "\"]";
}


mirror::Object* SharedDlMallocSpace::AllocWithoutGrowthLocked(size_t num_bytes,
    size_t* bytes_allocated) {
  size_t extendedSize = num_bytes;
  size_t calculatedSize  = 0;
  size_t checkingSize = 0;
  GCP_ADD_EXTRA_BYTES(num_bytes, extendedSize);
  mirror::Object* result =
      reinterpret_cast<mirror::Object*>(mspace_malloc(alloc_space_->mspace_,
          extendedSize));
  if (result != NULL) {
    if (kDebugSpaces) {
      CHECK(Contains(result)) << "Allocation (" << reinterpret_cast<void*>(result)
            << ") not in bounds of allocation space " << *this;
    }
    size_t allocation_size = AllocationSizeNonvirtual(result);

    DCHECK(bytes_allocated != NULL);
    *bytes_allocated = allocation_size;
    alloc_space_->num_bytes_allocated_ += allocation_size;
    alloc_space_->total_bytes_allocated_ += allocation_size;

    size_t tempSize = AllocationNoOverhead(result);
    GCP_REMOVE_EXTRA_BYTES(tempSize, calculatedSize);
    GCP_REMOVE_EXTRA_BYTES(allocation_size - kChunkOverhead, checkingSize);

    if(calculatedSize != checkingSize)
      LOG(ERROR) << "NumBytes= " << num_bytes << ", Usable size:" << tempSize <<
      ", allocSize: " << allocation_size << ", checkingSize: " << checkingSize
      << " != calculatedSize: " << calculatedSize << "; diff=" <<
      checkingSize - calculatedSize;
    //Fizo: should tune this
    GCMMP_NOTIFY_ALLOCATION(AllocationNoOverhead(result), num_bytes, result);
    ++alloc_space_->total_objects_allocated_;
    ++alloc_space_->num_objects_allocated_;
  }
  return result;
}
// Allocate num_bytes allowing the underlying mspace to grow.
mirror::Object* SharedDlMallocSpace::Alloc(Thread* self, size_t num_bytes,
    size_t* bytes_allocated) {
  mirror::Object* obj;
  {
    IterProcMutexLock interProcMu(self, *mu_);
    obj = AllocWithoutGrowthLocked(num_bytes, bytes_allocated);
  }
  if (obj != NULL) {
    // Zero freshly allocated memory, done while not holding the space's lock.
    memset(obj, 0, num_bytes);
  }
  return obj;
}

mirror::Object* SharedDlMallocSpace::AllocWithGrowth(Thread* self,
    size_t num_bytes, size_t* bytes_allocated) {
  mirror::Object* result;
  {
    IterProcMutexLock interProcMu(self, *mu_);
    // Grow as much as possible within the mspace.
    size_t max_allowed = Capacity();
    mspace_set_footprint_limit(alloc_space_->mspace_, max_allowed);
    // Try the allocation.
    result = AllocWithoutGrowthLocked(num_bytes, bytes_allocated);
    // Shrink back down as small as possible.
    size_t footprint = mspace_footprint(alloc_space_->mspace_);
    mspace_set_footprint_limit(alloc_space_->mspace_, footprint);
  }
  if (result != NULL) {
    // Zero freshly allocated memory, done while not holding the space's lock.
    memset(result, 0, num_bytes);
  }
  // Return the new allocation or NULL.
  CHECK(!kDebugSpaces || result == NULL || Contains(result));
  return result;
}

// Returns the number of bytes that the space has currently obtained from the system. This is
// greater or equal to the amount of live data in the space.
size_t SharedDlMallocSpace::GetFootprint() {
  Thread* self = Thread::Current();
  IterProcMutexLock interProcMu(self, *mu_);
  return mspace_footprint(alloc_space_->mspace_);
}

// Returns the number of bytes that the heap is allowed to obtain from the system via MoreCore.
size_t SharedDlMallocSpace::GetFootprintLimit() {
  Thread* self = Thread::Current();
  IterProcMutexLock interProcMu(self, *mu_);
  return mspace_footprint_limit(alloc_space_->mspace_);
}

// Set the maximum number of bytes that the heap is allowed to obtain from the system via
// MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
// allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
void SharedDlMallocSpace::SetFootprintLimit(size_t new_size) {
  Thread* self = Thread::Current();
  IterProcMutexLock interProcMu(self, *mu_);
  VLOG(heap) << "SharedDlMallocSpace::SetFootprintLimit " << PrettySize(new_size);
  // Compare against the actual footprint, rather than the Size(), because the heap may not have
  // grown all the way to the allowed size yet.
  size_t current_space_size = mspace_footprint(alloc_space_->mspace_);
  if (new_size < current_space_size) {
    // Don't let the space grow any more.
    new_size = current_space_size;
  }
  mspace_set_footprint_limit(alloc_space_->mspace_, new_size);
}

size_t SharedDlMallocSpace::Trim() {
  Thread* self = Thread::Current();
  IterProcMutexLock interProcMu(self, *mu_);
  // Trim to release memory at the end of the space.
  mspace_trim(alloc_space_->mspace_, 0);
  // Visit space looking for page-sized holes to advise the kernel we don't need.
  size_t reclaimed = 0;
  mspace_inspect_all(alloc_space_->mspace_, DlmallocMadviseCallback, &reclaimed);
  return reclaimed;
}

void SharedDlMallocSpace::SetGrowthLimit(size_t growth_limit) {
  growth_limit = RoundUp(growth_limit, kPageSize);
  alloc_space_->growth_limit_ = growth_limit;
  if (Size() > alloc_space_->growth_limit_) {
    alloc_space_->continuous_space_.end_ =
        alloc_space_->continuous_space_.begin_ + growth_limit;
  }
}

mirror::Class* SharedDlMallocSpace::FindRecentFreedObject(const mirror::Object* obj) {
  size_t pos = alloc_space_->recent_free_pos_;
  // Start at the most recently freed object and work our way back since there may be duplicates
  // caused by dlmalloc reusing memory.
  if (kRecentFreeCountService > 0) {
    for (size_t i = 0; i + 1 < kRecentFreeCountService + 1; ++i) {
      pos = pos != 0 ? pos - 1 : kRecentFreeMaskService;
      if (alloc_space_->recent_freed_objects_[pos].first == obj) {
        return alloc_space_->recent_freed_objects_[pos].second;
      }
    }
  }
  return nullptr;
}

void SharedDlMallocSpace::RegisterRecentFree(mirror::Object* ptr) {
  alloc_space_->recent_freed_objects_[alloc_space_->recent_free_pos_].first = ptr;
  alloc_space_->recent_freed_objects_[alloc_space_->recent_free_pos_].second = ptr->GetClass();
  alloc_space_->recent_free_pos_ = (alloc_space_->recent_free_pos_ + 1) & kRecentFreeMaskService;
}


// Virtual functions can't get inlined.
size_t SharedDlMallocSpace::InternalAllocationSize(const mirror::Object* obj) {
  return AllocationSizeNonvirtual(obj);
}

size_t SharedDlMallocSpace::Free(Thread* self, mirror::Object* ptr) {
  Thread* self = Thread::Current();
  IterProcMutexLock interProcMu(self, *mu_);
  if (kDebugSpaces) {
    CHECK(ptr != NULL);
    CHECK(Contains(ptr)) << "Free (" << ptr << ") not in bounds of heap " << *this;
  }
  const size_t bytes_freed = InternalAllocationSize(ptr);
  alloc_space_->num_bytes_allocated_ -= bytes_freed;
  --alloc_space_->num_objects_allocated_;
  //GCMMP_HANDLE_FINE_GRAINED_FREE(AllocationNoOverhead(ptr), bytes_freed);
  GCMMP_HANDLE_FINE_PRECISE_FREE(AllocationNoOverhead(ptr), ptr);
  if (kRecentFreeCountService > 0) {
    RegisterRecentFree(ptr);
  }
  mspace_free(alloc_space_->mspace_, ptr);
  return bytes_freed;
}

size_t SharedDlMallocSpace::FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) {
  Thread* self = Thread::Current();
  DCHECK(ptrs != NULL);

  // Don't need the lock to calculate the size of the freed pointers.
  size_t bytes_freed = 0;
  size_t _lastFreedBytes = 0;
  for (size_t i = 0; i < num_ptrs; i++) {
    mirror::Object* ptr = ptrs[i];
    const size_t look_ahead = 8;
    if (kPrefetchDuringSharedDlMallocSpaceFreeList && i + look_ahead < num_ptrs) {
      // The head of chunk for the allocation is sizeof(size_t) behind the allocation.
      __builtin_prefetch(reinterpret_cast<char*>(ptrs[i + look_ahead]) - sizeof(size_t));
    }
    GCMMP_HANDLE_FINE_PRECISE_FREE(AllocationNoOverhead(ptr),ptr);
    _lastFreedBytes = InternalAllocationSize(ptr);

    bytes_freed += _lastFreedBytes;
  }

  if (kRecentFreeCountService > 0) {
    IterProcMutexLock interProcMu(self, *mu_);
    for (size_t i = 0; i < num_ptrs; i++) {
      RegisterRecentFree(ptrs[i]);
    }
  }

  if (kDebugSpaces) {
    size_t num_broken_ptrs = 0;
    for (size_t i = 0; i < num_ptrs; i++) {
      if (!Contains(ptrs[i])) {
        num_broken_ptrs++;
        LOG(ERROR) << "FreeList[" << i << "] (" << ptrs[i] << ") not in bounds of heap " << *this;
      } else {
        size_t size = mspace_usable_size(ptrs[i]);
        memset(ptrs[i], 0xEF, size);
      }
    }
    CHECK_EQ(num_broken_ptrs, 0u);
  }

  {
    IterProcMutexLock interProcMu(self, *mu_);
    alloc_space_->num_bytes_allocated_ -= bytes_freed;
    alloc_space_->num_objects_allocated_ -= num_ptrs;
    mspace_bulk_free(alloc_space_->mspace_, reinterpret_cast<void**>(ptrs), num_ptrs);
    return bytes_freed;
  }
}


//// Callback from dlmalloc when it needs to increase the footprint
//extern "C" void* art_heap_morecore(void* mspace, intptr_t increment) {
//  Heap* heap = Runtime::Current()->GetHeap();
//  DCHECK_EQ(heap->GetAllocSpace()->GetMspace(), mspace);
//  return heap->GetAllocSpace()->MoreCore(increment);
//}

void* SharedDlMallocSpace::MoreCore(intptr_t increment) {
  mu_->AssertHeld(Thread::Current());
  byte* original_end = End();
  if (increment != 0) {
    VLOG(heap) << "DlMallocSpace::MoreCore " << PrettySize(increment);
    byte* new_end = original_end + increment;
    if (increment > 0) {
      // Should never be asked to increase the allocation beyond the capacity of the space. Enforced
      // by mspace_set_footprint_limit.
      CHECK_LE(new_end, Begin() + Capacity());
      CHECK_MEMORY_CALL(mprotect, (original_end, increment, PROT_READ | PROT_WRITE), GetName());
    } else {
      // Should never be asked for negative footprint (ie before begin)
      CHECK_GT(original_end + increment, Begin());
      // Advise we don't need the pages and protect them
      // TODO: by removing permissions to the pages we may be causing TLB shoot-down which can be
      // expensive (note the same isn't true for giving permissions to a page as the protected
      // page shouldn't be in a TLB). We should investigate performance impact of just
      // removing ignoring the memory protection change here and in Space::CreateAllocSpace. It's
      // likely just a useful debug feature.
      size_t size = -increment;
      CHECK_MEMORY_CALL(madvise, (new_end, size, MADV_DONTNEED), GetName());
      CHECK_MEMORY_CALL(mprotect, (new_end, size, PROT_NONE), GetName());
    }
    // Update end_
    alloc_space_->continuous_space_.end_ = new_end;
  }
  return original_end;
}


void SharedDlMallocSpace::Walk(void(*callback)(void *start, void *end,
    size_t num_bytes, void* callback_arg), void* arg) {
  IterProcMutexLock interProcMu(Thread::Current(), *mu_);
  mspace_inspect_all(alloc_space_->mspace_, callback, arg);
  callback(NULL, NULL, 0, arg);  // Indicate end of a space.
}

void SharedDlMallocSpace::SwapBitmaps() {
  GCSrvceBitmap tmp = alloc_space_->live_bitmap_;
  alloc_space_->live_bitmap_ = alloc_space_->mark_bitmap_;
  alloc_space_->mark_bitmap_ = tmp;
}


// Create a AllocSpace with the requested sizes. The requested
// base address is not guaranteed to be granted, if it is required,
// the caller should call Begin on the returned space to confirm
// the request was granted.
SharedDlMallocSpace* SharedDlMallocSpace::Create(const std::string& name,
                             size_t initial_size, size_t growth_limit,
                             size_t capacity, byte* requested_begin) {
  // Memory we promise to dlmalloc before it asks for morecore.
  // Note: making this value large means that large allocations are unlikely to succeed as dlmalloc
  // will ask for this memory from sys_alloc which will fail as the footprint (this value plus the
  // size of the large allocation) will be greater than the footprint limit.
  size_t starting_size = kPageSize;

//  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
//    uint64_t start_time = 0;
//    start_time = NanoTime();
//    VLOG(startup) << "Space::CreateAllocSpace entering " << name
//                  << " initial_size=" << PrettySize(initial_size)
//                  << " growth_limit=" << PrettySize(growth_limit)
//                  << " capacity=" << PrettySize(capacity)
//                  << " requested_begin=" << reinterpret_cast<void*>(requested_begin);
//  }

  // Sanity check arguments
  if (starting_size > initial_size) {
    initial_size = starting_size;
  }
  if (initial_size > growth_limit) {
    LOG(ERROR) << "Failed to create alloc space (" << name << ") where the initial size ("
        << PrettySize(initial_size) << ") is larger than its capacity ("
        << PrettySize(growth_limit) << ")";
    return NULL;
  }
  if (growth_limit > capacity) {
    LOG(ERROR) << "Failed to create alloc space (" << name << ") where the growth limit capacity ("
        << PrettySize(growth_limit) << ") is larger than the capacity ("
        << PrettySize(capacity) << ")";
    return NULL;
  }


  // Page align growth limit and capacity which will be used to manage mmapped storage
  growth_limit = RoundUp(growth_limit, kPageSize);
  capacity = RoundUp(capacity, kPageSize);

  SharedDlMallocSpace* _new_space = new SharedDlMallocSpace(name,
      initial_size, growth_limit, capacity,
      requested_begin, starting_size);

  return _new_space;

}

SharedDlMallocSpace::SharedDlMallocSpace(const std::string& name,
    size_t initial_size, size_t growth_limit, size_t capacity,
    byte* requested_begin, size_t starting_size) {

  alloc_space_ =
      reinterpret_cast<GCSrvceDlMallocSpace*>(gcservice::GCServiceGlobalAllocator::GCSrvcAllocateSharedSpace());
      /*reinterpret_cast<GCSrvceDlMallocSpace*>(calloc(1,
          SERVICE_ALLOC_ALIGN_BYTE(GCSrvceDlMallocSpace)));*/


  mu_   = new InterProcessMutex("shared-space mutex", &alloc_space_->lock_.futex_head_);
  cond_ = new InterProcessConditionVariable("shared-space CondVar", *mu_,
      &alloc_space_->lock_.cond_var_);


  AShmemMap* _ashmem_mem = MemMap::CreateAShmemMap(&alloc_space_->memory_,
        name.c_str(), requested_begin, capacity, PROT_READ | PROT_WRITE);

  if (_ashmem_mem == NULL) {
    LOG(ERROR) << "Failed to allocate pages for alloc space (" << name << ") of size "
        << PrettySize(capacity);
    free(alloc_space_);
    alloc_space_ = NULL;
    return;
  }

  alloc_space_->mspace_ =
      DlMallocSpace::CreateMallocSpace(MemMap::AshmemBegin(&alloc_space_->memory_),
          starting_size, initial_size);

  if (alloc_space_->mspace_ == NULL) {
    LOG(ERROR) << "Failed to initialize mspace for alloc space (" << name << ")";
    free(alloc_space_);
    alloc_space_ = NULL;
    return;
  }


  // Protect memory beyond the initial size.
  byte* end = MemMap::AshmemBegin(&alloc_space_->memory_) + starting_size;
  if (capacity - initial_size > 0) {
    CHECK_SHARED_MEMORY_CALL(mprotect, (end, capacity - initial_size,
        PROT_NONE), name);
  }

  strcpy(alloc_space_->continuous_space_.space_header_.name_, name.c_str());

  alloc_space_->continuous_space_.space_header_.gc_retention_policy_ =
      kGcRetentionPolicyAlwaysCollect;

  alloc_space_->continuous_space_.begin_ =
      MemMap::AshmemBegin(&alloc_space_->memory_);
  alloc_space_->continuous_space_.end_ = end;
  alloc_space_->growth_limit_ = growth_limit;

  alloc_space_->total_objects_allocated_ = 0;
  alloc_space_->num_bytes_allocated_ = 0;
  alloc_space_->num_objects_allocated_ = 0;
  alloc_space_->total_objects_allocated_ = 0;

  CreateBitmaps(alloc_space_->continuous_space_.begin_,
      alloc_space_->growth_limit_);

  alloc_space_->recent_free_pos_ = 0;

  for (auto& freed : alloc_space_->recent_freed_objects_) {
    freed.first = nullptr;
    freed.second = nullptr;
  }
}


bool SharedDlMallocSpace::CreateBitmaps(byte* heap_begin, size_t heap_capacity) {
  bool _result = true;
  alloc_space_->bitmap_index_++;
  // Round up since heap_capacity is not necessarily a multiple of kAlignment * kBitsPerWord.
  size_t bitmap_size =
      BitmapOffsetToIndex(RoundUp(heap_capacity, kAlignment * kBitsPerWord)) * kWordSize;

  _result = SpaceBitmapInit(&alloc_space_->live_bitmap_, "live-bitmap", heap_begin,
      heap_capacity, bitmap_size);

  if(_result) {
    _result = SpaceBitmapInit(&alloc_space_->mark_bitmap_, "mark-bitmap",
                    heap_begin, heap_capacity, bitmap_size);
  }
  return _result;
}


/*
 * Initialize a HeapBitmap so that it points to a bitmap large
 * enough to cover a heap at <base> of <maxSize> bytes, where
 * objects are guaranteed to be HB_OBJECT_ALIGNMENT-aligned.
 */
bool SharedDlMallocSpace::SpaceBitmapInit(GCSrvceBitmap *hb,
    const std::string& name, byte* heap_begin, size_t heap_capacity,
    size_t bitmap_size) {
  std::string _str = StringPrintf("allocspace %s live-bitmap %d", name.c_str(),
      static_cast<int>(alloc_space_->bitmap_index_));
  AShmemMap* _ashmem = MemMap::CreateAShmemMap(&hb->mem_map_, _str.c_str(),
      NULL, bitmap_size, PROT_READ | PROT_WRITE);

  if (_ashmem == NULL) {
    LOG(ERROR) << "Failed to allocate bitmap " << name;
    return false;
  }
  hb->bitmap_begin_ = reinterpret_cast<word*>(MemMap::AshmemBegin(&hb->mem_map_));
  hb->bitmap_size_  = bitmap_size;
  hb->heap_begin_   = reinterpret_cast<uintptr_t>(heap_begin);
  strcpy(hb->name_, name.c_str());

  return true;
}





}//namespace space




}//namespace gc
}//namespace art
