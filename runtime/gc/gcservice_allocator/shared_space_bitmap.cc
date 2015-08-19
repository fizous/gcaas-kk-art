/*
 * shared_space_bitmap.cc
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#include "utils.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "locks.h"
#include "os.h"
#include "gc/accounting/space_bitmap.h"
#include "gc/gcservice_allocator/service_allocator.h"
#include "gc/gcservice_allocator/shared_space_bitmap.h"

namespace art {

namespace gc {
namespace accounting {




SharedSpaceBitmap* SharedSpaceBitmap::CreateFromLocalSpaceBitmap(SpaceBitmap* localBitmap) {


  Runtime* runtime = Runtime::Current();
  SharedSpaceBitmapMeta* _meta = runtime->shared_heap_->getSharedSpaceBitmap();

  SharedSpaceBitmap* _sharedBitmMap =
      new SharedSpaceBitmap(_meta, reinterpret_cast<byte*>(localBitmap->Begin()),
          localBitmap->HeapBegin(), localBitmap->Size());

  return _sharedBitmMap;
}


SharedSpaceBitmap::~SharedSpaceBitmap() {

}
SharedSpaceBitmap::SharedSpaceBitmap(SharedSpaceBitmapMeta* meta,
                    byte* bitmap_begin,
                    const uintptr_t heap_begin,
                    size_t bitmap_size) : bitmap_meta_(meta) {
  bitmap_meta_->meta_.begin_ = reinterpret_cast<byte*>(bitmap_begin);
  bitmap_meta_->heap_begin_ = heap_begin;
  bitmap_meta_->meta_.size_ = bitmap_size;
}


void SharedSpaceBitmap::Clear() {
  if (Begin() != NULL) {
    // This returns the memory to the system.  Successive page faults will return zeroed memory.
    int result = madvise(Begin(), Size(), MADV_DONTNEED);
    if (result == -1) {
      PLOG(FATAL) << "madvise failed clearing sharedBitmapSpace";
    }
  }
}


void SharedSpaceBitmap::SetHeapLimit(uintptr_t new_end) {
  DCHECK(IsAligned<kBitsPerWord * SpaceBitmap::kAlignment>(new_end));
  size_t new_size = SpaceBitmap::OffsetToIndex(new_end - Begin()) * kWordSize;
  if (new_size < Size()) {
    Size(new_size);
  }
  // Not sure if doing this trim is necessary, since nothing past the end of the heap capacity
  // should be marked.
}


}//namespace accounting
}//namespace gc
}//namespace art
