/*
 * shared_space_bitmap.h
 *
 *  Created on: Aug 18, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_ACCOUNTING_SHARED_SPACE_BITMAP_H_
#define ART_RUNTIME_GC_ACCOUNTING_SHARED_SPACE_BITMAP_H_

#include "globals.h"
#include <stdint.h>



#include "gc/accounting/space_bitmap.h"

namespace art {

namespace gc {
namespace accounting {

typedef struct SharedSpaceBitmapMeta_S {
  // This bitmap itself, word sized for efficiency in scanning.
  word* const bitmap_begin_;

  // Size of this bitmap.
  size_t bitmap_size_;

  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  const uintptr_t heap_begin_;
} SharedSpaceBitmapMeta;


class SharedSpaceBitmap {
public:

  // Initialize a shared space bitmap using the provided mem_map as the live
  // bits. It does not Take ownership of mem map. The address range covered
  // starts at heap_begin and is of size equal to heap_capacity.
  // Objects are kAlignement-aligned.
  static SharedSpaceBitmap* CreateFromLocalSpaceBitmap(SpaceBitmap*);

  ~SharedSpaceBitmap();

  void Clear();

  // Starting address of our internal storage.
  word* Begin() {
    return bitmap_meta_->bitmap_begin_;
  }

  // Size of our internal storage
  size_t Size() const {
    return bitmap_meta_->bitmap_size_;
  }

  // Size of our internal storage
  size_t Size(size_t newSize) const {
    bitmap_meta_->bitmap_size_ = newSize;
    return bitmap_meta_->bitmap_size_;
  }


  // Size in bytes of the memory that the bitmaps spans.
  size_t HeapSize() const {
    return SpaceBitmap::IndexToOffset(Size() / kWordSize);
  }

  uintptr_t HeapBegin() const {
    return bitmap_meta_->heap_begin_;
  }

  // The maximum address which the bitmap can span. (HeapBegin() <= object < HeapLimit()).
  uintptr_t HeapLimit() const {
    return HeapBegin() + static_cast<uintptr_t>(HeapSize());
  }

  // Set the max address which can covered by the bitmap.
  void SetHeapLimit(uintptr_t new_end);


private:
  SharedSpaceBitmap(SharedSpaceBitmapMeta* meta,
                    word* bitmap_begin,
                    byte* heap_begin,
                    size_t heap_capacity);

  SharedSpaceBitmapMeta* bitmap_meta_;
}; //SharedSpaceBitmap

}//namespace accounting
}//namespace gc

}//namespace art

#endif /* ART_RUNTIME_GC_ACCOUNTING_SHARED_SPACE_BITMAP_H_ */
