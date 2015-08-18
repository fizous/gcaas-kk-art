/*
 * shared_space_bitmap.cc
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#include "utils.h"



#include "gc/accounting/space_bitmap.h"
#include "gc/gcservice_allocator/shared_space_bitmap.h"

namespace art {

namespace gc {
namespace accounting {




SharedSpaceBitmap* SharedSpaceBitmap::CreateFromLocalSpaceBitmap(SpaceBitmap* localBitmap) {


  Runtime* runtime = Runtime::Current();
  SharedSpaceBitmapMeta* _meta = runtime->shared_heap_->getSharedSpaceBitmap();

  SharedSpaceBitmap* _sharedBitmMap =
      new SharedSpaceBitmap(_meta, localBitmap->Begin(),
          localBitmap->HeapBegin(), localBitmap->Size());

  return _sharedBitmMap;
}

SharedSpaceBitmap::SharedSpaceBitmap(SharedSpaceBitmapMeta* meta,
                    word* bitmap_begin,
                    byte* heap_begin,
                    size_t bitmap_size) {
  bitmap_meta_ = meta;
  bitmap_meta_->bitmap_begin_ = bitmap_begin;
  bitmap_meta_->heap_begin_ = heap_begin;
  bitmap_meta_->bitmap_size_ = bitmap_size;

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

// Clean up any resources associated with the bitmap.
SharedSpaceBitmap::~SharedSpaceBitmap() {}

}//namespace accounting
}//namespace gc
}//namespace art
