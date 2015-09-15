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

#include "heap_bitmap.h"

#include "gc/space/space.h"

namespace art {
namespace gc {
namespace accounting {


#if ART_GC_SERVICE

SharedHeapBitmap::SharedHeapBitmap(Heap* heap,
    GCSrvceSharedHeapBitmap* header_addr) {
  if(header_addr == NULL) {
    header_addr =
        reinterpret_cast<GCSrvceSharedHeapBitmap*>(calloc(1,
            SERVICE_ALLOC_ALIGN_BYTE(GCSrvceSharedHeapBitmap)));
  }
  header_ = header_addr;
  header_->heap_ = heap;
  header_->index_ = 0;
  memset(header_->bitmaps_, 0, (8 * sizeof(BaseBitmap*)));

}

void SharedHeapBitmap::AddContinuousSpaceBitmap(accounting::BaseBitmap* bitmap) {
  DCHECK(bitmap != NULL);
  // Check for interval overlap.
  BaseBitmap* _temp = NULL;
  for(int i = 0; i < header_->index_; i ++) {
    _temp = header_->bitmaps_[i];
    CHECK(!(
        bitmap->HeapBegin() < _temp->HeapLimit() &&
        bitmap->HeapLimit() > _temp->HeapBegin()))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap "
        << _temp->Dump();
  }
  header_->bitmaps_[header_->index_++] = _temp;
}

void SharedHeapBitmap::Walk(BaseBitmap::Callback* callback, void* arg) {
  BaseBitmap* _temp = NULL;
  for(int i = 0; i < header_->index_; i ++) {
    _temp = header_->bitmaps_[i];
    _temp->Walk(callback, arg);
  }
}


void SharedHeapBitmap::ReplaceBitmap(BaseBitmap* old_bitmap,
    BaseBitmap* new_bitmap) {
  BaseBitmap* _temp = NULL;
  for(int i = 0; i < header_->index_; i ++) {
    _temp = header_->bitmaps_[i];
    if(_temp == old_bitmap) {
      header_->bitmaps_[i] = new_bitmap;
      return;
    }
  }
  LOG(FATAL) << "bitmap " << static_cast<const void*>(old_bitmap) << " not found";
}


void HeapBitmap::ReplaceBitmap(BaseBitmap* old_bitmap, BaseBitmap* new_bitmap) {
  for (auto& bitmap : continuous_space_bitmaps_) {
    if (bitmap == old_bitmap) {
      bitmap = new_bitmap;
      return;
    }
  }
  LOG(FATAL) << "bitmap " << static_cast<const void*>(old_bitmap) << " not found";
}

void HeapBitmap::ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set) {
  for (auto& space_set : discontinuous_space_sets_) {
    if (space_set == old_set) {
      space_set = new_set;
      return;
    }
  }
  LOG(FATAL) << "object set " << static_cast<const void*>(old_set) << " not found";
}

void HeapBitmap::AddContinuousSpaceBitmap(accounting::BaseBitmap* bitmap) {
  DCHECK(bitmap != NULL);

  // Check for interval overlap.
  for (const auto& cur_bitmap : continuous_space_bitmaps_) {
    CHECK(!(
        bitmap->HeapBegin() < cur_bitmap->HeapLimit() &&
        bitmap->HeapLimit() > cur_bitmap->HeapBegin()))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap " << cur_bitmap->Dump();
  }
  continuous_space_bitmaps_.push_back(bitmap);
}

void HeapBitmap::AddDiscontinuousObjectSet(SpaceSetMap* set) {
  DCHECK(set != NULL);
  discontinuous_space_sets_.push_back(set);
}

void HeapBitmap::Walk(BaseBitmap::Callback* callback, void* arg) {
  for (const auto& bitmap : continuous_space_bitmaps_) {
    bitmap->Walk(callback, arg);
  }

  DCHECK(!discontinuous_space_sets_.empty());
  for (const auto& space_set : discontinuous_space_sets_) {
    space_set->Walk(callback, arg);
  }
}


#else

void HeapBitmap::ReplaceBitmap(SpaceBitmap* old_bitmap, SpaceBitmap* new_bitmap) {
  for (auto& bitmap : continuous_space_bitmaps_) {
    if (bitmap == old_bitmap) {
      bitmap = new_bitmap;
      return;
    }
  }
  LOG(FATAL) << "bitmap " << static_cast<const void*>(old_bitmap) << " not found";
}

void HeapBitmap::ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set) {
  for (auto& space_set : discontinuous_space_sets_) {
    if (space_set == old_set) {
      space_set = new_set;
      return;
    }
  }
  LOG(FATAL) << "object set " << static_cast<const void*>(old_set) << " not found";
}

void HeapBitmap::AddContinuousSpaceBitmap(accounting::SpaceBitmap* bitmap) {
  DCHECK(bitmap != NULL);

  // Check for interval overlap.
  for (const auto& cur_bitmap : continuous_space_bitmaps_) {
    CHECK(!(
        bitmap->HeapBegin() < cur_bitmap->HeapLimit() &&
        bitmap->HeapLimit() > cur_bitmap->HeapBegin()))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap " << cur_bitmap->Dump();
  }
  continuous_space_bitmaps_.push_back(bitmap);
}

void HeapBitmap::AddDiscontinuousObjectSet(SpaceSetMap* set) {
  DCHECK(set != NULL);
  discontinuous_space_sets_.push_back(set);
}

void HeapBitmap::Walk(SpaceBitmap::Callback* callback, void* arg) {
  for (const auto& bitmap : continuous_space_bitmaps_) {
    bitmap->Walk(callback, arg);
  }

  DCHECK(!discontinuous_space_sets_.empty());
  for (const auto& space_set : discontinuous_space_sets_) {
    space_set->Walk(callback, arg);
  }
}
#endif
}  // namespace accounting
}  // namespace gc
}  // namespace art
