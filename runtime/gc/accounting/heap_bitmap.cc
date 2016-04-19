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
#include "space_bitmap-inl.h"
namespace art {
namespace gc {
namespace accounting {


#if (ART_GC_SERVICE || true)

int SharedHeapBitmap::MaxHeapBitmapIndex;

BaseHeapBitmap* BaseHeapBitmap::CreateHeapBitmap(/*Heap* heap,*/ bool sharable) {
  if(!sharable) {
    return new HeapBitmap(/*heap*/);
  }
  return new SharedHeapBitmap(/*heap*/);
}

BaseHeapBitmap* BaseHeapBitmap::ReShareHeapBitmap(/*Heap* heap,*/
         SharedHeapBitmap* originalBMap, GCSrvceSharedHeapBitmap* header_addr) {
  memcpy(header_addr, originalBMap->header_,
      SERVICE_ALLOC_ALIGN_BYTE(GCSrvceSharedHeapBitmap));
  originalBMap->header_ = header_addr;
  return originalBMap;
  //return new SharedHeapBitmap(/*heap,*/ header_addr);
}


bool BaseHeapBitmap::TestNoLock(const mirror::Object* obj) {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    return bitmap->Test(obj);
  } else {
    LOG(FATAL) << "Test: object does not belong to any bitmap";
  }
  return false;
}
bool BaseHeapBitmap::Test(const mirror::Object* obj) {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    return bitmap->Test(obj);
  } else {
    LOG(FATAL) << "Test: object does not belong to any bitmap";
  }
  return false;
}


void BaseHeapBitmap::Clear(const mirror::Object* obj)  {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    bitmap->Clear(obj);
  } else {
    LOG(FATAL) << "The object could not be cleared as it does not belong to "
        "any bitmap";
  }
}


void BaseHeapBitmap::Set(const mirror::Object* obj)  {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    bitmap->Set(obj);
  } else {
    LOG(FATAL) << "The object could not be set the object as it does not belong to "
        "any bitmap";
  }
}



SharedHeapBitmap::SharedHeapBitmap(/*Heap* heap,*/
    GCSrvceSharedHeapBitmap* header_addr) : BaseHeapBitmap(/*heap*/) {
  if(header_addr == NULL) {
    header_addr =
        reinterpret_cast<GCSrvceSharedHeapBitmap*>(calloc(1,
            SERVICE_ALLOC_ALIGN_BYTE(GCSrvceSharedHeapBitmap)));
    header_ = header_addr;
//    *(const_cast<const Heap*>(header_->heap_)) = heap;
    header_->index_ = 0;
    for(int i = 0; i < HEAP_BITMAPS_ARR_CAPACITY; i++) {
      header_->bitmap_headers_[i] = NULL;
    }

//    GCSrvceSharedHeapBitmap _data_values = {heap, 0,
//        {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}};
//    memcpy(header_, &_data_values,
//        SERVICE_ALLOC_ALIGN_BYTE(GCSrvceSharedHeapBitmap));
  //  memset(header_->bitmaps_, 0, (8 * sizeof(BaseBitmap*)));
    LOG(ERROR) << "done creating heap bitmap";
  } else {
    header_ = header_addr;
    LOG(ERROR) << "done re-sharing heap bitmap with index = "
        << header_->index_ << ", rounded_size="
        << SERVICE_ALLOC_ALIGN_BYTE(GCSrvceSharedHeapBitmap)
        << ", sizeof_size = " << sizeof(GCSrvceSharedHeapBitmap);
  }


}



void SharedHeapBitmap::AddContinuousSpaceBitmap(accounting::SPACE_BITMAP* bitmap) {
  // Check for interval overlap.
  for (const auto& cur_bitmap : continuous_space_bitmaps_) {
    CHECK(!(
        (bitmap->HeapBegin() < cur_bitmap->HeapLimit())
          && (bitmap->HeapLimit() > cur_bitmap->HeapBegin())))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap "
        << cur_bitmap->Dump();
  }
  continuous_space_bitmaps_.push_back(bitmap);
  if(bitmap->IsStructuredBitmap()) {
//    LOG(ERROR) << "header is allocated? " << (header_ != NULL);
    if(header_ != NULL) {
//      LOG(ERROR) << "SharedHeapBitmap::AddContinuousSpaceBitmap: Index: " <<
//          header_->index_;
    } else {
//      LOG(FATAL) << "SharedHeapBitmap::AddContinuousSpaceBitmap..._header is not "
//          "allocated";
    }

    MaxHeapBitmapIndex = (MaxHeapBitmapIndex < header_->index_) ?
        header_->index_ : MaxHeapBitmapIndex;

    if(header_->index_ >= HEAP_BITMAPS_ARR_CAPACITY) {
      LOG(FATAL) << "AddContinuousSpaceBitmap ..exceeded Arr capacity.." <<
          header_->index_;
    }

    accounting::SharedSpaceBitmap* _shared_bitmap =
        (accounting::SharedSpaceBitmap*) bitmap;
    header_->bitmap_headers_[header_->index_++] = _shared_bitmap->bitmap_data_;
  }



//  LOG(ERROR) << "SharedHeapBitmap::AddContinuousSpaceBitmap: We passed the loop "
//      << header_->index_ << ", max bitmap index = " << MaxHeapBitmapIndex;

//  DCHECK(bitmap != NULL);
//  // Check for interval overlap.
//  SPACE_BITMAP* _temp = NULL;
//
//  for(int i = 0; i < header_->index_; i++) {
//    _temp = header_->bitmap_headers_[i];
//    CHECK(!(
//        (bitmap->HeapBegin() < _temp->HeapLimit())
//          && (bitmap->HeapLimit() > _temp->HeapBegin())))
//        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap "
//        << _temp->Dump();
//  }
//  MaxHeapBitmapIndex = (MaxHeapBitmapIndex < header_->index_) ?
//      header_->index_ : MaxHeapBitmapIndex;
//  if(header_->index_ >= HEAP_BITMAPS_ARR_CAPACITY) {
//    LOG(FATAL) << "AddContinuousSpaceBitmap ..exceeded Arr capacity.." <<
//        header_->index_;
//  }
//  header_->bitmap_headers_[header_->index_++] = bitmap->;


}

void SharedHeapBitmap::Walk(BaseBitmap::Callback* callback, void* arg) {
  for (const auto& bitmap : continuous_space_bitmaps_) {
    bitmap->Walk(callback, arg);
  }

//  SPACE_BITMAP* _temp = NULL;
//  for(int i = 0; i < header_->index_; i ++) {
//    _temp = header_->bitmaps_[i];
//    _temp->Walk(callback, arg);
//  }
}

void SharedHeapBitmap::FixDataEntries() {


  for(int i = 0; i < HEAP_BITMAPS_ARR_CAPACITY; i++) {
    header_->bitmap_headers_[i] = NULL;
  }
  header_->index_ = 0;
  for (auto& bitmap : continuous_space_bitmaps_) {
    if(!bitmap->IsStructuredBitmap())
      continue;
    SharedSpaceBitmap* beetmap = (SharedSpaceBitmap*)bitmap;
    header_->bitmap_headers_[header_->index_++] = beetmap->bitmap_data_;
  }


//  if(new_entry == old_entry)
//    return;
//  for(int i = 0; i < header_->index_; i ++) {
//    if(old_entry == header_->bitmap_headers_[i]) {
//      header_->bitmap_headers_[i] = new_entry;
//      break;
//    }
//  }
}


void SharedHeapBitmap::ReplaceBitmap(BaseBitmap* old_bitmap,
    BaseBitmap* new_bitmap) {
  for (auto& bitmap : continuous_space_bitmaps_) {
    if (bitmap == old_bitmap) {
//      SharedSpaceBitmap* casted_old_beetmap = (SharedSpaceBitmap*)old_bitmap;
//      SharedSpaceBitmap* casted_new_beetmap = (SharedSpaceBitmap*)new_bitmap;
      bitmap = new_bitmap;
      FixDataEntries();

      return;
    }
  }
  LOG(FATAL) << "HeapBitmap::ReplaceBitmap; bitmap "
             << static_cast<const void*>(old_bitmap) << " not found";

//
//  BaseBitmap* _temp = NULL;
//  for(int i = 0; i < header_->index_; i ++) {
//    _temp = header_->bitmaps_[i];
//    if(_temp == old_bitmap) {
//      header_->bitmaps_[i] = new_bitmap;
//      return;
//    }
//  }
//  LOG(FATAL)  << "SharedHeapBitmap::ReplaceBitmap...bitmap "
//              << static_cast<const void*>(old_bitmap) << " not found";
}


void HeapBitmap::ReplaceBitmap(BaseBitmap* old_bitmap, SPACE_BITMAP* new_bitmap) {
  for (auto& bitmap : continuous_space_bitmaps_) {
    if (bitmap == old_bitmap) {
      bitmap = new_bitmap;
      return;
    }
  }
  LOG(FATAL) << "HeapBitmap::ReplaceBitmap; bitmap "
             << static_cast<const void*>(old_bitmap) << " not found";
}

void HeapBitmap::ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set) {
  for (auto& space_set : discontinuous_space_sets_) {
    if (space_set == old_set) {
      space_set = new_set;
      return;
    }
  }
  LOG(FATAL)  << "object set " << static_cast<const void*>(old_set)
              << " not found";
}

void HeapBitmap::AddContinuousSpaceBitmap(accounting::SPACE_BITMAP* bitmap) {
  DCHECK(bitmap != NULL);

  // Check for interval overlap.
  for (const auto& cur_bitmap : continuous_space_bitmaps_) {
    CHECK(!(
        (bitmap->HeapBegin() < cur_bitmap->HeapLimit())
          && (bitmap->HeapLimit() > cur_bitmap->HeapBegin())))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap "
        << cur_bitmap->Dump();
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

bool HeapBitmap::Test(const mirror::Object* obj) {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    return bitmap->Test(obj);
  } else {
    return GetDiscontinuousSpaceObjectSet(obj) != NULL;
  }
}

void HeapBitmap::Clear(const mirror::Object* obj)  {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    bitmap->Clear(obj);
  } else {
    SpaceSetMap* set = GetDiscontinuousSpaceObjectSet(obj);
    DCHECK(set != NULL);
    set->Clear(obj);
  }
}

void HeapBitmap::Set(const mirror::Object* obj) {
  SPACE_BITMAP* bitmap = GetContinuousSpaceBitmap(obj);
  if (LIKELY(bitmap != NULL)) {
    bitmap->Set(obj);
  } else {
    SpaceSetMap* set = GetDiscontinuousSpaceObjectSet(obj);
    DCHECK(set != NULL);
    set->Set(obj);
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
  LOG(FATAL)  << "bitmap " << static_cast<const void*>(old_bitmap)
              << " not found";
}

void HeapBitmap::ReplaceObjectSet(SpaceSetMap* old_set, SpaceSetMap* new_set) {
  for (auto& space_set : discontinuous_space_sets_) {
    if (space_set == old_set) {
      space_set = new_set;
      return;
    }
  }
  LOG(FATAL)  << "object set " << static_cast<const void*>(old_set)
              << " not found";
}

void HeapBitmap::AddContinuousSpaceBitmap(accounting::SpaceBitmap* bitmap) {
  DCHECK(bitmap != NULL);

  // Check for interval overlap.
  for (const auto& cur_bitmap : continuous_space_bitmaps_) {
    CHECK(!(
        bitmap->HeapBegin() < cur_bitmap->HeapLimit() &&
        bitmap->HeapLimit() > cur_bitmap->HeapBegin()))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap "
        << cur_bitmap->Dump();
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
