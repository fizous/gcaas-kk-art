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

#ifndef ART_RUNTIME_GC_ACCOUNTING_SPACE_BITMAP_INL_H_
#define ART_RUNTIME_GC_ACCOUNTING_SPACE_BITMAP_INL_H_

#include "base/logging.h"
#include "cutils/atomic-inline.h"
#include "utils.h"

namespace art {
namespace gc {
namespace accounting {

#if (ART_GC_SERVICE)
inline bool BaseBitmap::Modify(const mirror::Object* obj, bool do_set) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
  DCHECK_GE(addr, HeapBegin());
  const uintptr_t offset = addr - HeapBegin();
  const size_t index = OffsetToIndex(offset);
  const word mask = OffsetToMask(offset);
  DCHECK_LT(index, Size() / kWordSize) << " Size() = " << Size();
  word* address = &Begin()[index];
  word old_word = *address;
  if (do_set) {
    *address = old_word | mask;
  } else {
    *address = old_word & ~mask;
  }
  return (old_word & mask) != 0;
}

inline bool BaseBitmap::AtomicTestAndSet(const mirror::Object* obj) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
  DCHECK_GE(addr, HeapBegin());
  const uintptr_t offset = addr - HeapBegin();
  const size_t index = OffsetToIndex(offset);
  const word mask = OffsetToMask(offset);
  word* const address = &Begin()[index];
  DCHECK_LT(index, Size() / kWordSize) << " Size() = " << Size();
  word old_word;
  do {
    old_word = *address;
    // Fast path: The bit is already set.
    if ((old_word & mask) != 0) {
      return true;
    }
  } while (UNLIKELY(android_atomic_cas(old_word, old_word | mask, address) != 0));
  return false;
}


inline bool BaseBitmap::Test(const mirror::Object* obj) const {
  uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
  DCHECK(HasAddress(obj)) << obj;
  DCHECK(Begin() != NULL);
  DCHECK_GE(addr, HeapBegin());
  const uintptr_t offset = addr - HeapBegin();
  return (Begin()[OffsetToIndex(offset)] & OffsetToMask(offset)) != 0;
}


template <typename Visitor>
void BaseBitmap::VisitMarkedRange(uintptr_t visit_begin, uintptr_t visit_end,
                                   const Visitor& visitor) const {
  DCHECK_LT(visit_begin, visit_end);
  const size_t bit_index_start = (visit_begin - HeapBegin()) / kAlignment;
  const size_t bit_index_end = (visit_end - HeapBegin() - 1) / kAlignment;

  size_t word_start = bit_index_start / kBitsPerWord;
  size_t word_end = bit_index_end / kBitsPerWord;
  DCHECK_LT(word_end * kWordSize, Size());

  // Trim off left_bits of left bits.
  size_t edge_word = Begin()[word_start];

  // Handle bits on the left first as a special case
  size_t left_bits = bit_index_start & (kBitsPerWord - 1);
  if (left_bits != 0) {
    edge_word &= (1 << (kBitsPerWord - left_bits)) - 1;
  }

  // If word_start == word_end then handle this case at the same place we handle the right edge.
  if (edge_word != 0 && word_start < word_end) {
    uintptr_t ptr_base = IndexToOffset(word_start) + HeapBegin();
    do {
      const size_t shift = CLZ(edge_word);
      mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
      visitor(obj);
      edge_word ^= static_cast<size_t>(kWordHighBitMask) >> shift;
    } while (edge_word != 0);
  }
  word_start++;

  for (size_t i = word_start; i < word_end; i++) {
    size_t w = Begin()[i];
    if (w != 0) {
      uintptr_t ptr_base = IndexToOffset(i) + HeapBegin();
      do {
        const size_t shift = CLZ(w);
        mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
        visitor(obj);
        w ^= static_cast<size_t>(kWordHighBitMask) >> shift;
      } while (w != 0);
    }
  }

  // Handle the right edge, and also the left edge if both edges are on the same word.
  size_t right_bits = bit_index_end & (kBitsPerWord - 1);

  // If word_start == word_end then we need to use the word which we removed the left bits.
  if (word_start <= word_end) {
    edge_word = Begin()[word_end];
  }

  // Bits that we trim off the right.
  edge_word &= ~((static_cast<size_t>(kWordHighBitMask) >> right_bits) - 1);
  uintptr_t ptr_base = IndexToOffset(word_end) + HeapBegin();
  while (edge_word != 0) {
    const size_t shift = CLZ(edge_word);
    mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
    visitor(obj);
    edge_word ^= static_cast<size_t>(kWordHighBitMask) >> shift;
  }
}




#else
inline bool SpaceBitmap::AtomicTestAndSet(const mirror::Object* obj) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
  DCHECK_GE(addr, HeapBegin());
  const uintptr_t offset = addr - HeapBegin();
  const size_t index = OffsetToIndex(offset);
  const word mask = OffsetToMask(offset);
  word* const address = &Begin()[index];
  DCHECK_LT(index, Size() / kWordSize) << " Size() = " << Size();
  word old_word;
  do {
    old_word = *address;
    // Fast path: The bit is already set.
    if ((old_word & mask) != 0) {
      return true;
    }
  } while (UNLIKELY(android_atomic_cas(old_word, old_word | mask, address) != 0));
  return false;
}

inline bool SpaceBitmap::Test(const mirror::Object* obj) const {
  uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
  DCHECK(HasAddress(obj)) << obj;
  DCHECK(Begin() != NULL);
  DCHECK_GE(addr, HeapBegin());
  const uintptr_t offset = addr - HeapBegin();
  return (Begin()[OffsetToIndex(offset)] & OffsetToMask(offset)) != 0;
}

template <typename Visitor>
void SpaceBitmap::VisitMarkedRange(uintptr_t visit_begin, uintptr_t visit_end,
                                   const Visitor& visitor) const {
  DCHECK_LT(visit_begin, visit_end);
  const size_t bit_index_start = (visit_begin - HeapBegin()) / kAlignment;
  const size_t bit_index_end = (visit_end - HeapBegin() - 1) / kAlignment;

  size_t word_start = bit_index_start / kBitsPerWord;
  size_t word_end = bit_index_end / kBitsPerWord;
  DCHECK_LT(word_end * kWordSize, Size());

  // Trim off left_bits of left bits.
  size_t edge_word = Begin()[word_start];

  // Handle bits on the left first as a special case
  size_t left_bits = bit_index_start & (kBitsPerWord - 1);
  if (left_bits != 0) {
    edge_word &= (1 << (kBitsPerWord - left_bits)) - 1;
  }

  // If word_start == word_end then handle this case at the same place we handle the right edge.
  if (edge_word != 0 && word_start < word_end) {
    uintptr_t ptr_base = IndexToOffset(word_start) + HeapBegin();
    do {
      const size_t shift = CLZ(edge_word);
      mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
      visitor(obj);
      edge_word ^= static_cast<size_t>(kWordHighBitMask) >> shift;
    } while (edge_word != 0);
  }
  word_start++;

  for (size_t i = word_start; i < word_end; i++) {
    size_t w = Begin()[i];
    if (w != 0) {
      uintptr_t ptr_base = IndexToOffset(i) + HeapBegin();
      do {
        const size_t shift = CLZ(w);
        mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
        visitor(obj);
        w ^= static_cast<size_t>(kWordHighBitMask) >> shift;
      } while (w != 0);
    }
  }

  // Handle the right edge, and also the left edge if both edges are on the same word.
  size_t right_bits = bit_index_end & (kBitsPerWord - 1);

  // If word_start == word_end then we need to use the word which we removed the left bits.
  if (word_start <= word_end) {
    edge_word = Begin()[word_end];
  }

  // Bits that we trim off the right.
  edge_word &= ~((static_cast<size_t>(kWordHighBitMask) >> right_bits) - 1);
  uintptr_t ptr_base = IndexToOffset(word_end) + HeapBegin();
  while (edge_word != 0) {
    const size_t shift = CLZ(edge_word);
    mirror::Object* obj = reinterpret_cast<mirror::Object*>(ptr_base + shift * kAlignment);
    visitor(obj);
    edge_word ^= static_cast<size_t>(kWordHighBitMask) >> shift;
  }
}

inline bool SpaceBitmap::Modify(const mirror::Object* obj, bool do_set) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
  DCHECK_GE(addr, HeapBegin());
  const uintptr_t offset = addr - HeapBegin();
  const size_t index = OffsetToIndex(offset);
  const word mask = OffsetToMask(offset);
  DCHECK_LT(index, Size() / kWordSize) << " Size() = " << Size();
  word* address = &Begin()[index];
  word old_word = *address;
  if (do_set) {
    *address = old_word | mask;
  } else {
    *address = old_word & ~mask;
  }
  return (old_word & mask) != 0;
}
#endif

}  // namespace accounting
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ACCOUNTING_SPACE_BITMAP_INL_H_
