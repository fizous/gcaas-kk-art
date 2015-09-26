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
#include "dlmalloc_space.h"
#include "dlmalloc_space-inl.h"
#include "gc/service/service_space.h"
#include "gc/accounting/card_table.h"
#include "gc/heap.h"
#include "mirror/object-inl.h"
#include "runtime.h"
#include "thread.h"
#include "utils.h"

#include <valgrind.h>
#include <../memcheck/memcheck.h>
#include "gc_profiler/MProfiler.h"
namespace art {

namespace mprofiler {
	class MProfiler;
}


namespace gc {
namespace space {

// TODO: Remove define macro
#define CHECK_MEMORY_CALL(call, args, what) \
  do { \
    int rc = call args; \
    if (UNLIKELY(rc != 0)) { \
      errno = rc; \
      PLOG(FATAL) << # call << " failed for " << what; \
    } \
  } while (false)

static const bool kPrefetchDuringDlMallocFreeList = true;

// Number of bytes to use as a red zone (rdz). A red zone of this size will be placed before and
// after each allocation. 8 bytes provides long/double alignment.
const size_t kValgrindRedZoneBytes = 8;

// A specialization of DlMallocSpace that provides information to valgrind wrt allocations.
class ValgrindDlMallocSpace : public DlMallocSpace {
 public:
  virtual mirror::Object* AllocWithGrowth(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
    void* obj_with_rdz = DlMallocSpace::AllocWithGrowth(self, num_bytes + 2 * kValgrindRedZoneBytes,
                                                        bytes_allocated);
    if (obj_with_rdz == NULL) {
      return NULL;
    }
    mirror::Object* result = reinterpret_cast<mirror::Object*>(
        reinterpret_cast<byte*>(obj_with_rdz) + kValgrindRedZoneBytes);
    // Make redzones as no access.
    VALGRIND_MAKE_MEM_NOACCESS(obj_with_rdz, kValgrindRedZoneBytes);
    VALGRIND_MAKE_MEM_NOACCESS(reinterpret_cast<byte*>(result) + num_bytes, kValgrindRedZoneBytes);
    return result;
  }

  virtual mirror::Object* Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
    void* obj_with_rdz = DlMallocSpace::Alloc(self, num_bytes + 2 * kValgrindRedZoneBytes,
                                              bytes_allocated);
    if (obj_with_rdz == NULL) {
     return NULL;
    }
    mirror::Object* result = reinterpret_cast<mirror::Object*>(
        reinterpret_cast<byte*>(obj_with_rdz) + kValgrindRedZoneBytes);
    // Make redzones as no access.
    VALGRIND_MAKE_MEM_NOACCESS(obj_with_rdz, kValgrindRedZoneBytes);
    VALGRIND_MAKE_MEM_NOACCESS(reinterpret_cast<byte*>(result) + num_bytes, kValgrindRedZoneBytes);
    return result;
  }

  virtual size_t AllocationSize(const mirror::Object* obj) {
    size_t result = DlMallocSpace::AllocationSize(reinterpret_cast<const mirror::Object*>(
        reinterpret_cast<const byte*>(obj) - kValgrindRedZoneBytes));
    return result - 2 * kValgrindRedZoneBytes;
  }

  size_t GCPGetAllocationSize(const mirror::Object* obj) {
  	return AllocationSize(obj);
  }

  virtual size_t Free(Thread* self, mirror::Object* ptr) {
    void* obj_after_rdz = reinterpret_cast<void*>(ptr);
    void* obj_with_rdz = reinterpret_cast<byte*>(obj_after_rdz) - kValgrindRedZoneBytes;
    // Make redzones undefined.
    size_t allocation_size = DlMallocSpace::AllocationSize(
        reinterpret_cast<mirror::Object*>(obj_with_rdz));
    VALGRIND_MAKE_MEM_UNDEFINED(obj_with_rdz, allocation_size);
    size_t freed = DlMallocSpace::Free(self, reinterpret_cast<mirror::Object*>(obj_with_rdz));
    return freed - 2 * kValgrindRedZoneBytes;
  }

  virtual size_t FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) {
    size_t freed = 0;
    for (size_t i = 0; i < num_ptrs; i++) {
      freed += Free(self, ptrs[i]);
    }
    return freed;
  }

  ValgrindDlMallocSpace(const std::string& name, MEM_MAP* mem_map, void* mspace, byte* begin,
                        byte* end, size_t growth_limit, size_t initial_size) :
      DlMallocSpace(name, mem_map, mspace, begin, end, growth_limit) {
    VALGRIND_MAKE_MEM_UNDEFINED(mem_map->Begin() + initial_size, mem_map->Size() - initial_size);
  }

  virtual ~ValgrindDlMallocSpace() {
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ValgrindDlMallocSpace);
};

//size_t DlMallocSpace::bitmap_index_ = 0;
size_t GCSrvDlMallocSpace::bitmap_index_ = 0;

bool DlMallocSpace::CreateBitmaps(byte* heap_begin, size_t heap_capacity,
    bool shareMem) {
  bool _result = true;
  dlmalloc_space_data_->bitmap_index_++;

  live_bitmap_.reset(reinterpret_cast<accounting::SPACE_BITMAP*>(accounting::SPACE_BITMAP::Create(
       StringPrintf("allocspace %s live-bitmap %d", GetName(),
           static_cast<int>(dlmalloc_space_data_->bitmap_index_)),
       Begin(), Capacity(), shareMem)));
   DCHECK(live_bitmap_.get() != NULL) << "could not create allocspace live bitmap #"
       << dlmalloc_space_data_->bitmap_index_;

   mark_bitmap_.reset(reinterpret_cast<accounting::SPACE_BITMAP*>(accounting::SPACE_BITMAP::Create(
       StringPrintf("allocspace %s mark-bitmap %d", GetName(),
           static_cast<int>(dlmalloc_space_data_->bitmap_index_)),
       Begin(), Capacity(), shareMem)));
   DCHECK(mark_bitmap_.get() != NULL) <<
       "could not create allocspace mark bitmap #" <<
       dlmalloc_space_data_->bitmap_index_;

   return _result;
}



DlMallocSpace::DlMallocSpace(const std::string& name, MEM_MAP* mem_map, void* mspace, byte* begin,
                       byte* end, size_t growth_limit, bool shareMem)
    : MemMapSpace(name, mem_map, end - begin, kGcRetentionPolicyAlwaysCollect),// {//,
      lock_data_("allocation space lock", kAllocSpaceLock) {
  LOG(ERROR) << "DlMallocSpace::DlMallocSpace-->Allocating dlmalloc_space_data_";
  dlmalloc_space_data_ = reinterpret_cast<GCSrvDlMallocSpace*>(calloc(1,
      SERVICE_ALLOC_ALIGN_BYTE(GCSrvDlMallocSpace)));

  dlmalloc_space_data_->lock_ = &lock_data_;//new Mutex("allocation space lock", kAllocSpaceLock) DEFAULT_MUTEX_ACQUIRED_AFTER;
  dlmalloc_space_data_->recent_free_pos_ = 0;
  dlmalloc_space_data_->num_bytes_allocated_ = 0;
  dlmalloc_space_data_->num_objects_allocated_ = 0;
  dlmalloc_space_data_->total_bytes_allocated_ = 0;
  dlmalloc_space_data_->total_objects_allocated_= 0;
  dlmalloc_space_data_->mspace_ = mspace;
  dlmalloc_space_data_->growth_limit_ = growth_limit;

  CHECK(mspace != NULL);


  static const uintptr_t kGcCardSize =
      static_cast<uintptr_t>(accounting::ConstantsCardTable::kCardSize);
  CHECK(IsAligned<kGcCardSize>(reinterpret_cast<uintptr_t>(mem_map->Begin())));
  CHECK(IsAligned<kGcCardSize>(reinterpret_cast<uintptr_t>(mem_map->End())));

#if true || ART_GC_SERVICE

  CreateBitmaps(Begin(), Capacity(), shareMem);

#else
  live_bitmap_.reset(accounting::SpaceBitmap::Create(
      StringPrintf("allocspace %s live-bitmap %d", name.c_str(), static_cast<int>(bitmap_index)),
      Begin(), Capacity()));
  DCHECK(live_bitmap_.get() != NULL) << "could not create allocspace live bitmap #" << bitmap_index;

  mark_bitmap_.reset(accounting::SpaceBitmap::Create(
      StringPrintf("allocspace %s mark-bitmap %d", name.c_str(), static_cast<int>(bitmap_index)),
      Begin(), Capacity()));
  DCHECK(live_bitmap_.get() != NULL) << "could not create allocspace mark bitmap #" << bitmap_index;
#endif
  for (auto& freed : dlmalloc_space_data_->recent_freed_objects_) {
    freed.first = nullptr;
    freed.second = nullptr;
  }
}

DLMALLOC_SPACE_T* DlMallocSpace::Create(const std::string& name, size_t initial_size, size_t
                                     growth_limit, size_t capacity,
                                     byte* requested_begin, bool shareMem) {
  // Memory we promise to dlmalloc before it asks for morecore.
  // Note: making this value large means that large allocations are unlikely to succeed as dlmalloc
  // will ask for this memory from sys_alloc which will fail as the footprint (this value plus the
  // size of the large allocation) will be greater than the footprint limit.
  size_t starting_size = kPageSize;
  uint64_t start_time = 0;
  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
    start_time = NanoTime();
    VLOG(startup) << "Space::CreateAllocSpace entering " << name
                  << " initial_size=" << PrettySize(initial_size)
                  << " growth_limit=" << PrettySize(growth_limit)
                  << " capacity=" << PrettySize(capacity)
                  << " requested_begin=" << reinterpret_cast<void*>(requested_begin);
  }

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

  UniquePtr<MEM_MAP> mem_map(MEM_MAP::MapAnonymous(name.c_str(), requested_begin, capacity,
                                                 PROT_READ | PROT_WRITE, shareMem));
  if (mem_map.get() == NULL) {
    LOG(ERROR) << "Failed to allocate pages for alloc space (" << name << ") of size "
        << PrettySize(capacity);
    return NULL;
  }

  void* mspace = CreateMallocSpace(mem_map->Begin(), starting_size, initial_size);
  if (mspace == NULL) {
    LOG(ERROR) << "Failed to initialize mspace for alloc space (" << name << ")";
    return NULL;
  }

  // Protect memory beyond the initial size.
  byte* end = mem_map->Begin() + starting_size;
  if (capacity - initial_size > 0) {
    CHECK_MEMORY_CALL(mprotect, (end, capacity - initial_size, PROT_NONE), name);
  }

  // Everything is set so record in immutable structure and leave
  MEM_MAP* mem_map_ptr = mem_map.release();
  DL_MALLOC_SPACE* space;
  if (RUNNING_ON_VALGRIND > 0) {
    space = new ValgrindDlMallocSpace(name, mem_map_ptr, mspace, mem_map_ptr->Begin(), end,
                                      growth_limit, initial_size);
  } else {
    space = new DlMallocSpace(name, mem_map_ptr, mspace, mem_map_ptr->Begin(), end, growth_limit, shareMem);
  }
  if (VLOG_IS_ON(heap) || VLOG_IS_ON(startup)) {
    LOG(INFO) << "Space::CreateAllocSpace exiting (" << PrettyDuration(NanoTime() - start_time)
        << " ) " << *space;
  }
  return space;
}

void* DlMallocSpace::CreateMallocSpace(void* begin, size_t morecore_start, size_t initial_size) {
  // clear errno to allow PLOG on error
  errno = 0;
  // create mspace using our backing storage starting at begin and with a footprint of
  // morecore_start. Don't use an internal dlmalloc lock (as we already hold heap lock). When
  // morecore_start bytes of memory is exhaused morecore will be called.
  void* msp = create_mspace_with_base(begin, morecore_start, false /*locked*/);
  if (msp != NULL) {
    // Do not allow morecore requests to succeed beyond the initial size of the heap
    mspace_set_footprint_limit(msp, initial_size);
  } else {
    PLOG(ERROR) << "create_mspace_with_base failed";
  }
  return msp;
}

void DlMallocSpace::SwapBitmaps() {
  live_bitmap_.swap(mark_bitmap_);
  // Swap names to get more descriptive diagnostics.
  std::string temp_name(live_bitmap_->GetName());
  live_bitmap_->SetName(mark_bitmap_->GetName());
  mark_bitmap_->SetName(temp_name);
}

mirror::Object* DlMallocSpace::Alloc(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
  return AllocNonvirtual(self, num_bytes, bytes_allocated);
}

mirror::Object* DlMallocSpace::AllocWithGrowth(Thread* self, size_t num_bytes, size_t* bytes_allocated) {
  mirror::Object* result;
  {
    MutexLock mu(self, lock_data_);
    // Grow as much as possible within the mspace.
    size_t max_allowed = Capacity();
    mspace_set_footprint_limit(GetMspace(), max_allowed);
    // Try the allocation.
    result = AllocWithoutGrowthLocked(num_bytes, bytes_allocated);
    // Shrink back down as small as possible.
    size_t footprint = mspace_footprint(GetMspace());
    mspace_set_footprint_limit(GetMspace(), footprint);
  }
  if (result != NULL) {
    // Zero freshly allocated memory, done while not holding the space's lock.
    memset(result, 0, num_bytes);
  }
  // Return the new allocation or NULL.
  CHECK(!kDebugSpaces || result == NULL || Contains(result));
  return result;
}

void DlMallocSpace::SetGrowthLimit(size_t growth_limit) {
  growth_limit = RoundUp(growth_limit, kPageSize);
  SetInternalGrowthLimit(growth_limit);
  if (Size() > Capacity()) {
    SetEnd(Begin() + growth_limit);
  }
}

DLMALLOC_SPACE_T* DlMallocSpace::CreateZygoteSpace(const char* alloc_space_name,
    bool shareMem) {
  SetEnd(reinterpret_cast<byte*>(RoundUp(reinterpret_cast<uintptr_t>(End()), kPageSize)));
  DCHECK(IsAligned<accounting::ConstantsCardTable::kCardSize>(Begin()));
  DCHECK(IsAligned<accounting::ConstantsCardTable::kCardSize>(End()));
  DCHECK(IsAligned<kPageSize>(Begin()));
  DCHECK(IsAligned<kPageSize>(End()));
  size_t size = RoundUp(Size(), kPageSize);
  // Trim the heap so that we minimize the size of the Zygote space.
  Trim();
  // Trim our mem-map to free unused pages.
  GetMemMap()->UnMapAtEnd(End());
  // TODO: Not hardcode these in?
  const size_t starting_size = kPageSize;
  const size_t initial_size = 2 * MB;
  // Remaining size is for the new alloc space.
  //TODO: fizo: check what's wrong here?
  const size_t growth_limit = Capacity() - size;
  const size_t capacity = Capacity() - size;
  VLOG(heap) << "Begin " << reinterpret_cast<const void*>(Begin()) << "\n"
             << "End " << reinterpret_cast<const void*>(End()) << "\n"
             << "Size " << size << "\n"
             << "GrowthLimit " << Capacity() << "\n"
             << "Capacity " << Capacity();
  SetGrowthLimit(RoundUp(size, kPageSize));
  SetFootprintLimit(RoundUp(size, kPageSize));
  // FIXME: Do we need reference counted pointers here?
  // Make the two spaces share the same mark bitmaps since the bitmaps span both of the spaces.
  VLOG(heap) << "Creating new AllocSpace: ";
  VLOG(heap) << "Size " << GetMemMap()->Size();
  VLOG(heap) << "GrowthLimit " << PrettySize(growth_limit);
  VLOG(heap) << "Capacity " << PrettySize(capacity);
  MEM_MAP* _space_mem_map = NULL;
  if(shareMem) {
    _space_mem_map = MEM_MAP::CreateStructedMemMap(alloc_space_name, End(),
        capacity, PROT_READ | PROT_WRITE, shareMem);
  } else {
    _space_mem_map = MEM_MAP::MapAnonymous(alloc_space_name, End(),
        capacity, PROT_READ | PROT_WRITE, shareMem);
  }
  UniquePtr<MEM_MAP> mem_map(_space_mem_map);
  void* mspace = CreateMallocSpace(End(), starting_size, initial_size);
  // Protect memory beyond the initial size.
  byte* end = mem_map->Begin() + starting_size;
  if (capacity - initial_size > 0) {
    CHECK_MEMORY_CALL(mprotect, (end, capacity - initial_size, PROT_NONE),
        alloc_space_name);
  }
  DL_MALLOC_SPACE* alloc_space = NULL;
  if(shareMem) {
    alloc_space =
        new SharableDlMallocSpace(alloc_space_name, mem_map.release(), mspace, End(), end,
            growth_limit, shareMem, SharableDlMallocSpace::AllocateDataMemory());
  } else {
    alloc_space =
          new DlMallocSpace(alloc_space_name, mem_map.release(), mspace, End(), end,
              growth_limit, shareMem);
  }

  live_bitmap_->SetHeapLimit(reinterpret_cast<uintptr_t>(End()));
  CHECK_EQ(live_bitmap_->HeapLimit(), reinterpret_cast<uintptr_t>(End()));
  mark_bitmap_->SetHeapLimit(reinterpret_cast<uintptr_t>(End()));
  CHECK_EQ(mark_bitmap_->HeapLimit(), reinterpret_cast<uintptr_t>(End()));
  VLOG(heap) << "zygote space creation done";
  return alloc_space;
}

mirror::Class* DlMallocSpace::FindRecentFreedObject(const mirror::Object* obj) {
  size_t pos = GetRecentFreePos();
  // Start at the most recently freed object and work our way back since there may be duplicates
  // caused by dlmalloc reusing memory.
  if (kRecentFreeCount > 0) {
    for (size_t i = 0; i + 1 < kRecentFreeCount + 1; ++i) {
      pos = pos != 0 ? pos - 1 : kRecentFreeMask;
      if (dlmalloc_space_data_->recent_freed_objects_[pos].first == obj) {
        return dlmalloc_space_data_->recent_freed_objects_[pos].second;
      }
    }
  }
  return nullptr;
}

void DlMallocSpace::RegisterRecentFree(mirror::Object* ptr) {
  dlmalloc_space_data_->recent_freed_objects_[GetRecentFreePos()].first = ptr;
  dlmalloc_space_data_->recent_freed_objects_[GetRecentFreePos()].second = ptr->GetClass();
  SetRecentFreePos((GetRecentFreePos() + 1) & kRecentFreeMask);
}


size_t DlMallocSpace::Free(Thread* self, mirror::Object* ptr) {
  MutexLock mu(self, lock_data_);
  if (kDebugSpaces) {
    CHECK(ptr != NULL);
    CHECK(Contains(ptr)) << "Free (" << ptr << ") not in bounds of heap " << *this;
  }
  const size_t bytes_freed = InternalAllocationSize(ptr);
  //num_bytes_allocated_ -= bytes_freed;
  UpdateBytesAllocated(-bytes_freed);
  UpdateObjectsAllocated(-1);//--num_objects_allocated_;
  //GCMMP_HANDLE_FINE_GRAINED_FREE(AllocationNoOverhead(ptr), bytes_freed);
  GCMMP_HANDLE_FINE_PRECISE_FREE(AllocationNoOverhead(ptr), ptr);
  if (kRecentFreeCount > 0) {
    RegisterRecentFree(ptr);
  }
  mspace_free(GetMspace(), ptr);
  return bytes_freed;
}

size_t DlMallocSpace::FreeList(Thread* self, size_t num_ptrs, mirror::Object** ptrs) {
  DCHECK(ptrs != NULL);

  // Don't need the lock to calculate the size of the freed pointers.
  size_t bytes_freed = 0;
  size_t _lastFreedBytes = 0;
  for (size_t i = 0; i < num_ptrs; i++) {
    mirror::Object* ptr = ptrs[i];
    const size_t look_ahead = 8;
    if (kPrefetchDuringDlMallocFreeList && i + look_ahead < num_ptrs) {
      // The head of chunk for the allocation is sizeof(size_t) behind the allocation.
      __builtin_prefetch(reinterpret_cast<char*>(ptrs[i + look_ahead]) - sizeof(size_t));
    }
    GCMMP_HANDLE_FINE_PRECISE_FREE(AllocationNoOverhead(ptr),ptr);
    _lastFreedBytes = InternalAllocationSize(ptr);

    bytes_freed += _lastFreedBytes;
  }

  if (kRecentFreeCount > 0) {
    MutexLock mu(self, lock_data_);
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
    MutexLock mu(self, lock_data_);
    UpdateBytesAllocated(-bytes_freed);
    UpdateObjectsAllocated(-num_ptrs);//num_objects_allocated_ -= num_ptrs;
    mspace_bulk_free(GetMspace(), reinterpret_cast<void**>(ptrs), num_ptrs);
    return bytes_freed;
  }
}

// Callback from dlmalloc when it needs to increase the footprint
extern "C" void* art_heap_morecore(void* mspace, intptr_t increment) {
  Heap* heap = Runtime::Current()->GetHeap();
  DCHECK_EQ(heap->GetAllocSpace()->GetMspace(), mspace);
  return heap->GetAllocSpace()->MoreCore(increment);
}

void* DlMallocSpace::MoreCore(intptr_t increment) {
  dlmalloc_space_data_->lock_->AssertHeld(Thread::Current());
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
    SetEnd(new_end);
  }
  return original_end;
}

// Virtual functions can't get inlined.
inline size_t DlMallocSpace::InternalAllocationSize(const mirror::Object* obj) {
  return AllocationSizeNonvirtual(obj);
}

size_t DlMallocSpace::AllocationSize(const mirror::Object* obj) {
  return InternalAllocationSize(obj);
}

size_t DlMallocSpace::GCPGetAllocationSize(const mirror::Object* obj){
	return AllocationNoOverhead(obj);
}

//size_t DlMallocSpace::GetObjectSize(const mirror::Object* obj) {
//	return AllocationNoOverhead(obj);
//}

size_t DlMallocSpace::Trim() {
	mprofiler::VMProfiler::MProfMarkStartTrimHWEvent();
  MutexLock mu(Thread::Current(), lock_data_);
  // Trim to release memory at the end of the space.
  mspace_trim(GetMspace(), 0);
  // Visit space looking for page-sized holes to advise the kernel we don't need.
  size_t reclaimed = 0;
  mspace_inspect_all(GetMspace(), DlmallocMadviseCallback, &reclaimed);
  mprofiler::VMProfiler::MProfMarkEndTrimHWEvent();
  return reclaimed;
}

void DlMallocSpace::Walk(void(*callback)(void *start, void *end, size_t num_bytes, void* callback_arg),
                      void* arg) {
  MutexLock mu(Thread::Current(), lock_data_);
  mspace_inspect_all(GetMspace(), callback, arg);
  callback(NULL, NULL, 0, arg);  // Indicate end of a space.
}

size_t DlMallocSpace::GetFootprint() {
  MutexLock mu(Thread::Current(), lock_data_);
  return mspace_footprint(GetMspace());
}

size_t DlMallocSpace::GetFootprintLimit() {
  MutexLock mu(Thread::Current(), lock_data_);
  return mspace_footprint_limit(GetMspace());
}

void DlMallocSpace::SetFootprintLimit(size_t new_size) {
  MutexLock mu(Thread::Current(), lock_data_);
  VLOG(heap) << "DLMallocSpace::SetFootprintLimit " << PrettySize(new_size);
  // Compare against the actual footprint, rather than the Size(), because the heap may not have
  // grown all the way to the allowed size yet.
  size_t current_space_size = mspace_footprint(GetMspace());
  if (new_size < current_space_size) {
    // Don't let the space grow any more.
    new_size = current_space_size;
  }
  mspace_set_footprint_limit(GetMspace(), new_size);
}

void DlMallocSpace::Dump(std::ostream& os) const {
  os << GetType()
      << " begin=" << reinterpret_cast<void*>(Begin())
      << ",end=" << reinterpret_cast<void*>(End())
      << ",size=" << PrettySize(Size()) << ",capacity=" << PrettySize(Capacity())
      << ",name=\"" << GetName() << "\"]";
}


SharedDlMallocSpace* DlMallocSpace::CreateZygoteSpaceWithSharedSpace(const char* alloc_space_name) {
  SetEnd(reinterpret_cast<byte*>(RoundUp(reinterpret_cast<uintptr_t>(End()), kPageSize)));
  DCHECK(IsAligned<accounting::CARD_TABLE::kCardSize>(Begin()));
  DCHECK(IsAligned<accounting::CARD_TABLE::kCardSize>(End()));
  DCHECK(IsAligned<kPageSize>(Begin()));
  DCHECK(IsAligned<kPageSize>(End()));
  size_t size = RoundUp(Size(), kPageSize);
  // Trim the heap so that we minimize the size of the Zygote space.
  Trim();
  // Trim our mem-map to free unused pages.
  GetMemMap()->UnMapAtEnd(End());
  // TODO: Not hardcode these in?
 // const size_t starting_size = kPageSize;
  const size_t initial_size = 2 * MB;
  // Remaining size is for the new alloc space.
  const size_t growth_limit = Capacity() - size;
  const size_t capacity = Capacity() - size;
  LOG(ERROR) << "CreateZygoteSpaceWithSharedSpace-->Begin "
             << reinterpret_cast<const void*>(Begin()) << "\n"
             << "End " << reinterpret_cast<const void*>(End()) << "\n"
             << "Size " << size << "\n"
             << "GrowthLimit " << Capacity() << "\n"
             << "Capacity " << Capacity();
  SetGrowthLimit(RoundUp(size, kPageSize));
  SetFootprintLimit(RoundUp(size, kPageSize));
  // FIXME: Do we need reference counted pointers here?
  // Make the two spaces share the same mark bitmaps since the bitmaps span both of the spaces.
  VLOG(heap) << "Creating new AllocSpace: ";
  VLOG(heap) << "Size " << GetMemMap()->Size();
  VLOG(heap) << "GrowthLimit " << PrettySize(growth_limit);
  VLOG(heap) << "Capacity " << PrettySize(capacity);
  //UniquePtr<MemMap> mem_map(MemMap::MapAnonymous(alloc_space_name, End(), capacity, PROT_READ | PROT_WRITE));
  //void* mspace = CreateMallocSpace(end_, starting_size, initial_size);
  // Protect memory beyond the initial size.
  //byte* end = mem_map->Begin() + starting_size;
  //if (capacity - initial_size > 0) {
  //  CHECK_MEMORY_CALL(mprotect, (end, capacity - initial_size, PROT_NONE), alloc_space_name);
  //}


  SharedDlMallocSpace* _shared_space = SharedDlMallocSpace::Create(alloc_space_name,
      initial_size, growth_limit, capacity, End());

  live_bitmap_->SetHeapLimit(reinterpret_cast<uintptr_t>(End()));
  CHECK_EQ(live_bitmap_->HeapLimit(), reinterpret_cast<uintptr_t>(End()));
  mark_bitmap_->SetHeapLimit(reinterpret_cast<uintptr_t>(End()));
  CHECK_EQ(mark_bitmap_->HeapLimit(), reinterpret_cast<uintptr_t>(End()));
  VLOG(heap) << "zygote space creation done";
  return _shared_space;
}




IDlMallocSpace* IDlMallocSpace::CreateDlMallocSpace(const std::string& name,
    size_t initial_size, size_t growth_limit,
    size_t capacity, byte* requested_begin, bool shareMem) {
  return NULL;
}


SharableDlMallocSpace::SharableDlMallocSpace(const std::string& name, MEM_MAP* mem_map, void* mspace,
    byte* begin, byte* end, size_t growth_limit, bool shareMem,
    GCSrvSharableDlMallocSpace* sharable_data) : DlMallocSpace (name, mem_map, mspace,
        begin, end, growth_limit, shareMem),
        sharable_space_data_(sharable_data),
        dlmalloc_space_data_(&(sharable_space_data_->dlmalloc_space_data_)) {

}

GCSrvSharableDlMallocSpace* SharableDlMallocSpace::AllocateDataMemory(void) {
  return reinterpret_cast<GCSrvSharableDlMallocSpace*>(
      calloc(1, SERVICE_ALLOC_ALIGN_BYTE(GCSrvSharableDlMallocSpace)));
}
///*
// * Initialize a HeapBitmap so that it points to a bitmap large
// * enough to cover a heap at <base> of <maxSize> bytes, where
// * objects are guaranteed to be HB_OBJECT_ALIGNMENT-aligned.
// */
//bool SharableDlMallocSpace::SpaceBitmapInit(accounting::GCSrvceBitmap *hb,
//    const std::string& name, byte* heap_begin, size_t heap_capacity,
//    size_t bitmap_size) {
//  std::string _str = StringPrintf("allocspace %s live-bitmap %d", name.c_str(),
//      static_cast<int>(dlmalloc_space_data_->bitmap_index_));
//  AShmemMap* _ashmem = MEM_MAP::CreateAShmemMap(&hb->mem_map_, _str.c_str(),
//      NULL, bitmap_size, PROT_READ | PROT_WRITE);
//
//  if (_ashmem == NULL) {
//    LOG(ERROR) << "Failed to allocate bitmap " << name;
//    return false;
//  }
//  hb->bitmap_begin_ = reinterpret_cast<word*>(MEM_MAP::AshmemBegin(&hb->mem_map_));
//  hb->bitmap_size_  = bitmap_size;
//  hb->heap_begin_   = reinterpret_cast<uintptr_t>(heap_begin);
//
//  memcpy(hb->name_, name.c_str(), name.size());
//  hb->name_[name.size()] = '\0';
//  return true;
//}
//
//bool SharableDlMallocSpace::CreateBitmaps(byte* heap_begin, size_t heap_capacity,
//    bool shareMem) {
//  bool _result = true;
//  dlmalloc_space_data_->bitmap_index_++;
//  // Round up since heap_capacity is not necessarily a multiple of kAlignment * kBitsPerWord.
//  size_t bitmap_size =
//      BitmapOffsetToIndex(RoundUp(heap_capacity, kAlignment * kBitsPerWord)) * kWordSize;
//
//  _result = SpaceBitmapInit(&sharable_space_data_->live_bitmap_, "live-bitmap", heap_begin,
//      heap_capacity, bitmap_size);
//
//  if(_result) {
//    _result = SpaceBitmapInit(&sharable_space_data_->mark_bitmap_, "mark-bitmap",
//                    heap_begin, heap_capacity, bitmap_size);
//  }
//  return _result;
//}

// Swap the live and mark bitmaps of this space. This is used by the GC for
// concurrent sweeping.
//void StructuredDlMallocSpaceImpl::SwapBitmaps() {
//
//}
//
//
//void StructuredDlMallocSpaceImpl::SetFootprintLimit(size_t limit) {
//
//}
//
//void StructuredDlMallocSpaceImpl::SetInternalGrowthLimit(size_t new_growth_limit) {
//
//}

}  // namespace space
}  // namespace gc
}  // namespace art
