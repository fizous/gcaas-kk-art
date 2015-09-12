/*
 * service_space.cc
 *
 *  Created on: Sep 12, 2015
 *      Author: hussein
 */


#include "utils.h"
#include "runtime.h"
#include "gc/service/service_space.h"
#include "gc/allocator/dlmalloc.h"
#include "gc/space/dlmalloc_space.h"

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

void SharedDlMallocSpace::Dump(std::ostream& os) const {
  os << GetType()
      << " begin=" << reinterpret_cast<void*>(Begin())
      << ",end=" << reinterpret_cast<void*>(End())
      << ",size=" << PrettySize(Size()) << ",capacity=" << PrettySize(Capacity())
      << ",name=\"" << GetName() << "\"]";
}

// Returns the number of bytes that the space has currently obtained from the system. This is
// greater or equal to the amount of live data in the space.
size_t SharedDlMallocSpace::GetFootprint() {
  //MutexLock mu(Thread::Current(), lock_);
  return mspace_footprint(alloc_space_->mspace_);
}

// Returns the number of bytes that the heap is allowed to obtain from the system via MoreCore.
size_t SharedDlMallocSpace::GetFootprintLimit() {
  //MutexLock mu(Thread::Current(), lock_);
  return mspace_footprint_limit(alloc_space_->mspace_);
}

// Set the maximum number of bytes that the heap is allowed to obtain from the system via
// MoreCore. Note this is used to stop the mspace growing beyond the limit to Capacity. When
// allocations fail we GC before increasing the footprint limit and allowing the mspace to grow.
void SharedDlMallocSpace::SetFootprintLimit(size_t new_size) {
//  MutexLock mu(Thread::Current(), lock_);
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
  //MutexLock mu(Thread::Current(), lock_);
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


void SharedDlMallocSpace::RegisterRecentFree(mirror::Object* ptr) {
  alloc_space_->recent_freed_objects_[alloc_space_->recent_free_pos_].first = ptr;
  alloc_space_->recent_freed_objects_[alloc_space_->recent_free_pos_].second = ptr->GetClass();
  alloc_space_->recent_free_pos_ = (alloc_space_->recent_free_pos_ + 1) & kRecentFreeMask;
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

  SharedDlMallocSpace* _new_space = new SharedDlMallocSpace(name,
      initial_size, growth_limit, capacity,
      requested_begin, starting_size);

  return _new_space;

}

SharedDlMallocSpace::SharedDlMallocSpace(const std::string& name,
    size_t initial_size, size_t growth_limit, size_t capacity,
    byte* requested_begin, size_t starting_size) {

  static const uintptr_t kGcCardSize =
      static_cast<uintptr_t>(accounting::CardTable::kCardSize);

  alloc_space_ =
      reinterpret_cast<GCSrvceDlMallocSpace*>(calloc(1,
          SERVICE_ALLOC_ALIGN_BYTE(GCSrvceDlMallocSpace)));

  AShmemMap* _ashmem_mem = MemMap::CreateAShmemMap(&alloc_space_->memory_,
        name.c_str(), requested_begin, capacity, PROT_READ | PROT_WRITE);

  if (_ashmem_mem == NULL) {
    LOG(ERROR) << "Failed to allocate pages for alloc space (" << name << ") of size "
        << PrettySize(capacity);
    return NULL;
  }

  alloc_space_->mspace_ =
      DlMallocSpace::CreateMallocSpace(MemMap::Begin(&alloc_space_->memory_),
          starting_size, initial_size);

  if (alloc_space_->mspace_ == NULL) {
    LOG(ERROR) << "Failed to initialize mspace for alloc space (" << name << ")";
    return NULL;
  }


  // Protect memory beyond the initial size.
  byte* end = MemMap::Begin(&alloc_space_->memory_) + starting_size;
  if (capacity - initial_size > 0) {
    CHECK_SHARED_MEMORY_CALL(mprotect, (end, capacity - initial_size,
        PROT_NONE), name);
  }

  strcpy(alloc_space_->continuous_space_.space_header_.name_, name.c_str());
  alloc_space_->continuous_space_.space_header_.gc_retention_policy_ =
      kGcRetentionPolicyAlwaysCollect;

  alloc_space_->continuous_space_.begin_ = MemMap::Begin(&alloc_space_->memory_);
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
  size_t bitmap_index = alloc_space_->bitmap_index_++;
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

  AShmemMap* _ashmem = MemMap::CreateAShmemMap(&hb->mem_map_,
       StringPrintf("allocspace %s live-bitmap %d", name.c_str(),
           static_cast<int>(alloc_space_->bitmap_index_)),
      NULL, bitmap_size, PROT_READ | PROT_WRITE);

  if (_ashmem == NULL) {
    LOG(ERROR) << "Failed to allocate bitmap " << name;
    return false;
  }
  hb->bitmap_begin_ = reinterpret_cast<word*>(MemMap::Begin(&hb->mem_map_));
  hb->bitmap_size_  = bitmap_size;
  hb->heap_begin_   = reinterpret_cast<uintptr_t>(heap_begin);
  strcpy(hb->name_, name.c_str());

  return true;
}




}//namespace space
}//namespace gc
}//namespace art
