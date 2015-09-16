/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_INL_H_
#define ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_INL_H_

#include "heap_bitmap.h"

namespace art {
namespace gc {
namespace accounting {

#if rue || ART_GC_SERVICE
template <typename Visitor>
inline void BaseHeapBitmap::Visit(const Visitor& visitor) {
  BaseBitmap* _bitmap = NULL;
  for(int i = 0; i < GetContinuousSize(); i++) {
    _bitmap = GetContBitmapFromIndex(i);
    _bitmap->VisitMarkedRange(_bitmap->HeapBegin(), _bitmap->HeapLimit(), visitor);
  }
  SpaceSetMap* _set_map = NULL;
  for(int i = 0; i < GetDiscContinuousSize(); i++) {
    _set_map = GetDiscContBitmapFromIndex(i);
    _set_map->Visit(visitor);
  }

}

#else



template <typename Visitor>
inline void HeapBitmap::Visit(const Visitor& visitor) {
  for (const auto& bitmap : continuous_space_bitmaps_) {
    bitmap->VisitMarkedRange(bitmap->HeapBegin(), bitmap->HeapLimit(), visitor);
  }
  DCHECK(!discontinuous_space_sets_.empty());
  for (const auto& space_set : discontinuous_space_sets_) {
    space_set->Visit(visitor);
  }
}

#endif

}  // namespace accounting
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_INL_H_
