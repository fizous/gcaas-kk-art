/*
 * ipc_server_sweep.cc
 *
 *  Created on: Nov 2, 2015
 *      Author: hussein
 */



#include <string>
#include <cutils/ashmem.h>
#include "globals.h"
#include "mem_map.h"
#include "ipcfs/ipcfs.h"
#include "sticky_mark_sweep.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "thread_list.h"
#include "mirror/object-inl.h"
#include "gc/service/service_client.h"
#include "gc/space/dlmalloc_space-inl.h"
#include "gc/space/space-inl.h"
#include "gc/space/space.h"
#include "gc/space/large_object_space.h"

#include "base/bounded_fifo.h"
#include "mirror/class.h"
#include "mirror/object_array.h"
#include "mirror/art_field.h"
#include "mirror/object-inl.h"
#include "gc/collector/mark_sweep.h"
#include "gc/collector/ipc_server_sweep.h"
#include "gc/collector/ipc_server_sweep-inl.h"

#define SERVER_SWEEP_CALC_OFFSET(x, y) (x > y) ? (x - y) : (y - x)

namespace art {
namespace gc {
namespace collector {




int IPCServerMarkerSweep::passed_bitmap_tests_ = 0;
int IPCServerMarkerSweep::pushed_back_to_stack_ = 0;
int IPCServerMarkerSweep::is_reference_class_cnt_ = 0;
class ServerMarkObjectVisitor {
 public:
  explicit ServerMarkObjectVisitor(IPCServerMarkerSweep* const server_mark_sweep)
          ALWAYS_INLINE : mark_sweep_(server_mark_sweep) {}

  // TODO: Fixme when anotatalysis works with visitors.
  void operator()(const mirror::Object* /* obj */, const mirror::Object* ref,
                  MemberOffset& /* offset */, bool /* is_static */) const ALWAYS_INLINE
      NO_THREAD_SAFETY_ANALYSIS {
//    if (kCheckLocks) {
//      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
//      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
//    }
    if(true)
      mark_sweep_->MarkObject(ref);
  }

 private:
  IPCServerMarkerSweep* const mark_sweep_;
};

class ServerMarkObjectVisitorRemoval {
 public:
  explicit ServerMarkObjectVisitorRemoval(IPCServerMarkerSweep* const server_mark_sweep)
          ALWAYS_INLINE : mark_sweep_(server_mark_sweep) {}

  // TODO: Fixme when anotatalysis works with visitors.
  void operator()(const mirror::Object* /* obj */, const mirror::Object* ref,
                  MemberOffset& /* offset */, bool /* is_static */) const ALWAYS_INLINE
      NO_THREAD_SAFETY_ANALYSIS {
//    if (kCheckLocks) {
//      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
//      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
//    }
    if(true)
      mark_sweep_->MarkObject(ref);
  }

 private:
  IPCServerMarkerSweep* const mark_sweep_;
};


void IPCServerMarkerSweep::ResetStats(void) {
  memset(&cashed_stats_client_, 0, sizeof(space::GCSrvceCashedStatsCounters));
}

IPCServerMarkerSweep::IPCServerMarkerSweep(
    gc::gcservice::GCServiceClientRecord* client_record) :
        client_rec_(client_record),
        heap_meta_(&(client_rec_->sharable_space_->heap_meta_)),
        offset_(static_cast<int32_t>(SERVER_SWEEP_CALC_OFFSET(
                            client_rec_->pair_mapps_->first->mem_maps_[0].begin_,
                                  client_rec_->pair_mapps_->second->mem_maps_[0].begin_))),
        curr_collector_ptr_(NULL),
        current_mark_bitmap_(NULL),
        mark_stack_(NULL),
  ref_referent_off_client_(heap_meta_->reference_offsets_.reference_referent_offset_),
  ref_queue_off_client_(heap_meta_->reference_offsets_.reference_queue_offset_),
  ref_queueNext_off_client_(heap_meta_->reference_offsets_.reference_queueNext_offset_),
  ref_pendingNext_off_client_(heap_meta_->reference_offsets_.reference_pendingNext_offset_),
  ref_reference_zombie_off_client_(heap_meta_->reference_offsets_.finalizer_reference_zombie_offset_) {

  spaces_[KGCSpaceServerZygoteInd_].client_base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[0].begin_);
  spaces_[KGCSpaceServerZygoteInd_].client_end_ =
      (spaces_[KGCSpaceServerZygoteInd_].client_base_ +
          client_rec_->pair_mapps_->first->mem_maps_[0].size_);
  spaces_[KGCSpaceServerZygoteInd_].base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->second->mem_maps_[0].begin_);
  spaces_[KGCSpaceServerZygoteInd_].base_end_ =
      (spaces_[KGCSpaceServerZygoteInd_].base_ +
          client_rec_->pair_mapps_->second->mem_maps_[0].size_);

  spaces_[KGCSpaceServerAllocInd_].client_base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[1].begin_);
  spaces_[KGCSpaceServerAllocInd_].client_end_ =
      (spaces_[KGCSpaceServerAllocInd_].client_base_ +
          client_rec_->pair_mapps_->first->mem_maps_[1].size_);
  spaces_[KGCSpaceServerAllocInd_].base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->second->mem_maps_[1].begin_);
  spaces_[KGCSpaceServerAllocInd_].base_end_ =
      (spaces_[KGCSpaceServerAllocInd_].base_ +
          client_rec_->pair_mapps_->second->mem_maps_[1].size_);

  spaces_[KGCSpaceServerImageInd_].client_base_ =
      heap_meta_->image_space_begin_;
  spaces_[KGCSpaceServerImageInd_].client_end_ =
      heap_meta_->image_space_end_;
  spaces_[KGCSpaceServerImageInd_].base_ = heap_meta_->image_space_begin_;
  spaces_[KGCSpaceServerImageInd_].base_end_ = heap_meta_->image_space_end_;



  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    marked_spaces_count_prof_[i] = 0;
  }
  //set the sharable space to be shared
  android_atomic_acquire_store(2, &(client_rec_->sharable_space_->register_gc_));

  memset(&cashed_references_client_, 0, sizeof(space::GCSrvceCashedReferences));

  //cashed_references_client_.java_lang_Class_ = client_record->java_lang_Class_cached_;
  LOG(ERROR) << "Initialized the IPC_SERVER_SWEEP with Offset:" << offset_ <<
      ", java_lang_class = ";//; << reinterpret_cast<void*>(cashed_references_client_.java_lang_Class_);
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    LOG(ERROR) << StringPrintf("...space[%d]  --> client-start=%p, client-end=%p", i,
        spaces_[i].client_base_, spaces_[i].client_end_);
  }
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    LOG(ERROR) << StringPrintf("...space[%d]  --> server-start=%p, server-end=%p", i,
        spaces_[i].base_, spaces_[i].base_end_);
  }
}


void IPCServerMarkerSweep::MarkReachableObjects(
                            space::GCSrvSharableCollectorData* collector_addr) {
  Thread* _self = Thread::Current();


  if(InitMarkingPhase(collector_addr)) {
    LOG(ERROR) << " ++++ IPCServerMarkerSweep::MarkReachableObjects: "
        << _self->GetTid() << "; address " <<
        reinterpret_cast<void*>(collector_addr);
    ProcessMarckStack();
  }


}

accounting::ATOMIC_OBJ_STACK_T*  IPCServerMarkerSweep::GetMappedMarkStack(
    android::MappedPairProcessFD* pair_memory,
    int entry_ind,
    StructuredObjectStackData* stack_meta_address) {

//  android::IPCAShmemMap* _client_address =
//      &(pair_memory->first->mem_maps_[entry_ind]);
  android::IPCAShmemMap* _server_address =
      &(pair_memory->second->mem_maps_[entry_ind]);

//
//  uintptr_t _stack_offset = _server_address->begin_ - _client_address->begin_;
  accounting::ATOMIC_OBJ_STACK_T* atomic_stack_dup =
      accounting::ATOMIC_OBJ_STACK_T::CreateServerAtomicStack(stack_meta_address,
          reinterpret_cast<byte*>(_server_address->begin_));

  return atomic_stack_dup;
}




bool IPCServerMarkerSweep::TestMappedBitmap(
                                    const mirror::Object* mapped_object) {
  if(mapped_object == NULL)
    return true;
  const byte* casted_param = reinterpret_cast<const byte*>(mapped_object);
//  if(casted_param < GetClientSpaceEnd(KGCSpaceServerImageInd_)) {
//    return ref_parm;
//  }
  int matching_index = -1;
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    if((casted_param <  GetServerSpaceEnd(i)) &&
        (casted_param >= GetServerSpaceBegin(i))) {
      matching_index = i;
    }
  }


  if(matching_index < 0) {
    LOG(FATAL) << "XXXX IPCServerMarkerSweep::TestMappedBitmap XXX " <<
                mapped_object;
    return false;
  }
  marked_spaces_count_prof_[matching_index] += 1;
  if(matching_index == KGCSpaceServerImageInd_)
    return true;

  accounting::SharedServerSpaceBitmap* _object_beetmap = current_mark_bitmap_;
  bool _resultHasAddress = _object_beetmap->HasAddress(mapped_object);
  bool _resultTestFlag = false;
  if(!_resultHasAddress) {
    for (const auto& beetmap : mark_bitmaps_) {
      _resultHasAddress = beetmap->HasAddress(mapped_object);
      if(_resultHasAddress) {
        _object_beetmap = beetmap;
        break;
      }
    }
  }
  if(_resultHasAddress) {
    _resultTestFlag = _object_beetmap->Test(mapped_object);
  }

  if(!(_resultHasAddress && _resultTestFlag)) {
    LOG(ERROR) << "failed = " << passed_bitmap_tests_ << ", data_struct: " <<
        _object_beetmap->bitmap_data_ <<
        ", Object does not belong to bitmap.." << mapped_object <<
        ", bitmap_begin = " << _object_beetmap->Begin() <<
        ", bitmap_size = " << _object_beetmap->Size() <<
        ", bitmap_heap_size = " << _object_beetmap->HeapSize() <<
        ", heap_begin = " << _object_beetmap->HeapBegin() <<
        ", kBitsPerWord = " << kBitsPerWord <<
        ", (test): " << _resultTestFlag << ", _resultHasAddress: " <<
        ", (HasAddress): " << _resultHasAddress;
    LOG(FATAL) << "[1]&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&";
    return false;
  }

  passed_bitmap_tests_ += 1;
  return true;
}

accounting::SharedServerSpaceBitmap* IPCServerMarkerSweep::GetMappedBitmap(
    android::MappedPairProcessFD* pair_memory,
    int entry_ind, accounting::GCSrvceBitmap* bitmap_meta_addr) {

  android::IPCAShmemMap* _server_address =
      &(pair_memory->second->mem_maps_[entry_ind]);
//  AShmemMap* _bitmap_mem_map = &(bitmap_meta_addr->mem_map_);
  byte* mapped_server_address = reinterpret_cast<byte*>(_server_address->begin_);


//  _bitmap_mem_map->mapped_begin_ =
//      reinterpret_cast<byte*>(_server_address->begin_);

//  android::IPCAShmemMap* _client_address =
//      &(pair_memory->first->mem_maps_[entry_ind]);
//  android::IPCAShmemMap* _server_address =
//      &(pair_memory->second->mem_maps_[entry_ind]);
//  AShmemMap* _bitmap_mem_map = &(bitmap_meta_addr->mem_map_);
//  _bitmap_mem_map->mapped_begin_ =
//      reinterpret_cast<byte*>(_server_address->begin_);

  accounting::SharedServerSpaceBitmap* _alloc_space_bitmap =
      new accounting::SharedServerSpaceBitmap(bitmap_meta_addr,
                                            mapped_server_address, offset_);

//  if(mark_bitmaps_.empty()) {
//
//    if(current_mark_bitmap_ == NULL)
//      current_mark_bitmap_ = _alloc_space_bitmap;
//
//    mark_bitmaps_.push_back(_alloc_space_bitmap);
//  }

  return _alloc_space_bitmap;
}

void IPCServerMarkerSweep::ServerScanObject(const mirror::Object* obj,
    uint32_t calculated_offset) {
  //obj = (obj + calculated_offset);


 // mirror::Object* mapped_obj = MapClientReference(obj);

  ServerMarkObjectVisitor visitor(this);
  ServerScanObjectVisit(obj, visitor);


//  CHECK(klass != NULL);
//  if (UNLIKELY(klass->IsArrayClass())) {
//    if (klass->IsObjectArrayClass()) {
//
//    }
//  } else if (UNLIKELY(klass == NULL)) {
//
////    VisitClassReferences(klass, obj, visitor);
//  } else {
//
//  }
}

bool IPCServerMarkerSweep::ServerScanObjectRemoval(const mirror::Object* obj) {
  ServerMarkObjectVisitorRemoval visitor(this);
  return ServerScanObjectVisitRemoval(obj, visitor);
}


static void ExternalScanObjectVisit(mirror::Object* obj,
    void* args) {
  IPCServerMarkerSweep* param =
      reinterpret_cast<IPCServerMarkerSweep*>(args);
  //uint32_t calc_offset = (param->offset_ / sizeof(Object*));
//  uint32_t* calc_offset = reinterpret_cast<uint32_t*>(calculated_offset);



  param->ServerScanObject(obj, param->offset_);
}

static bool ExternalScanObjectVisitRemoval(mirror::Object* obj,
    void* args) {
  IPCServerMarkerSweep* param =
      reinterpret_cast<IPCServerMarkerSweep*>(args);
  //uint32_t calc_offset = (param->offset_ / sizeof(Object*));
//  uint32_t* calc_offset = reinterpret_cast<uint32_t*>(calculated_offset);



  return param->ServerScanObjectRemoval(obj);
}



void IPCServerMarkerSweep::ProcessMarckStack() {
  LOG(ERROR) << "%%%%%%%%%%%%%%%%%%%%%%%";
  LOG(ERROR) << "IPCServerMarkerSweep::ProcessMarckStack....size:" << mark_stack_->Size();
  if(mark_stack_->IsEmpty()) {
    LOG(ERROR) << "+++++++++++++++++++++++";
    return;
  }
//  static const size_t kFifoSize = 4;
//  BoundedFifoPowerOfTwo<const Object*, kFifoSize> prefetch_fifo;
  if(false)
    mark_stack_->Sort();
  //mark_stack_->DumpDataEntries(true);
//  mark_stack_->VerifyDataEntries(true, reinterpret_cast<uintptr_t>(spaces_[KGCSpaceServerZygoteInd_].client_base_),
//      reinterpret_cast<uintptr_t>(spaces_[KGCSpaceServerZygoteInd_].client_end_),
//          reinterpret_cast<uintptr_t>(spaces_[KGCSpaceServerAllocInd_].client_base_),
//              reinterpret_cast<uintptr_t>(spaces_[KGCSpaceServerAllocInd_].client_end_));
  uint32_t calculated_offset = offset_ / (sizeof(mirror::Object*));
  LOG(ERROR) << "Calculated offset..." <<  calculated_offset;


  const mirror::Object* popped_oject = NULL;
  if(false) {
    for (;;) {
      if (mark_stack_->IsEmpty()) {
        break;
      }
      popped_oject = mark_stack_->PopBack();
      ServerScanObject(popped_oject, 0);
    }
  }

  if(false)
    mark_stack_->OperateOnStack(ExternalScanObjectVisit,this);

  if(true)
    ((accounting::ServerStructuredObjectStack*)mark_stack_)->OperateRemovalOnStack(ExternalScanObjectVisitRemoval, this);

  UpdateClientCachedReferences(&curr_collector_ptr_->cashed_references_,
      &cashed_references_client_);

  LOG(ERROR) << "+++++++++++++++++++++++ array_count = " <<
      cashed_stats_client_.array_count_ <<
      ", class_count = " << cashed_stats_client_.class_count_ <<
      ", other_count = " << cashed_stats_client_.other_count_ <<
      ", reference_count = " << cashed_stats_client_.reference_count_ <<
      ", success_bitmaps = " << passed_bitmap_tests_ <<
      "\n isRefClassCnt = " << is_reference_class_cnt_<<
      "\n pushed_back_to_stack = " << pushed_back_to_stack_ <<
      "\n marked_bitmap_cnt: " <<
      StringPrintf("[%d] : %d \n[%d] : %d\n[%d] : %d",
          KGCSpaceServerImageInd_, marked_spaces_count_prof_[KGCSpaceServerImageInd_],
          KGCSpaceServerZygoteInd_, marked_spaces_count_prof_[KGCSpaceServerZygoteInd_],
          KGCSpaceServerAllocInd_, marked_spaces_count_prof_[KGCSpaceServerAllocInd_]);



//  for (;;) {
//    const Object* obj = NULL;
//    if (kUseMarkStackPrefetch) {
//      while (!mark_stack_->IsEmpty() && prefetch_fifo.size() < kFifoSize) {
//        const Object* obj = (mark_stack_->PopBack() + calculated_offset);
//
//        DCHECK(obj != NULL);
//        __builtin_prefetch(obj);
//        prefetch_fifo.push_back(obj);
//      }
//      if (prefetch_fifo.empty()) {
//        break;
//      }
//      obj = prefetch_fifo.front();
//      prefetch_fifo.pop_front();
//    } else {
//      if (mark_stack_->IsEmpty()) {
//        break;
//      }
//      obj = (mark_stack_->PopBack() + calculated_offset);
//    }
//    DCHECK(obj != NULL);
//    ScanObjectVisit(obj, calculated_offset);
//  }
}


void IPCServerMarkerSweep::UpdateClientCachedReferences(
    space::GCSrvceCashedReferences* dest, space::GCSrvceCashedReferences* src) {
  dest->immune_begin_ = const_cast<mirror::Object*>(
      MapReferenceToClient<mirror::Object>(src->immune_begin_));
  dest->immune_end_ =
      const_cast<mirror::Object*>(
          MapReferenceToClient<mirror::Object>(src->immune_end_));

//  dest->soft_reference_list_ =
//      const_cast<mirror::Object*>(
//          MapReferenceToClientChecks<mirror::Object>(src->soft_reference_list_));
//  dest->weak_reference_list_ =
//      const_cast<mirror::Object*>(
//          MapReferenceToClientChecks<mirror::Object>(src->weak_reference_list_));
//  dest->finalizer_reference_list_ =
//      const_cast<mirror::Object*>(
//          MapReferenceToClientChecks<mirror::Object>(src->finalizer_reference_list_));
//  dest->phantom_reference_list_ =
//      const_cast<mirror::Object*>(
//          MapReferenceToClientChecks<mirror::Object>(src->phantom_reference_list_));
//  dest->cleared_reference_list_ =
//      const_cast<mirror::Object*>(
//          MapReferenceToClientChecks<mirror::Object>(src->cleared_reference_list_));


}

void IPCServerMarkerSweep::SetCachedReferencesPointers(
    space::GCSrvceCashedReferences* dest, space::GCSrvceCashedReferences* src) {
  dest->immune_begin_ = const_cast<mirror::Object*>(
          MapReferenceToServer<mirror::Object>(src->immune_begin_));
  dest->immune_end_ =
      const_cast<mirror::Object*>(
          MapReferenceToServer<mirror::Object>(src->immune_end_));

  dest->soft_reference_list_ =
      const_cast<mirror::Object*>(
          MapReferenceToServerChecks<mirror::Object>(src->soft_reference_list_));
  dest->weak_reference_list_ =
      const_cast<mirror::Object*>(
          MapReferenceToServerChecks<mirror::Object>(src->weak_reference_list_));
  dest->finalizer_reference_list_ =
      const_cast<mirror::Object*>(
          MapReferenceToServerChecks<mirror::Object>(src->finalizer_reference_list_));
  dest->phantom_reference_list_ =
      const_cast<mirror::Object*>(
          MapReferenceToServerChecks<mirror::Object>(src->phantom_reference_list_));
  dest->cleared_reference_list_ =
      const_cast<mirror::Object*>(
          MapReferenceToServerChecks<mirror::Object>(src->cleared_reference_list_));

  dest->java_lang_Class_ =
      const_cast<mirror::Class*>(
          MapReferenceToServerChecks<mirror::Class>(src->java_lang_Class_));


  LOG(ERROR) << "!!!!! JavaLangClass = " << dest->java_lang_Class_;

}

void IPCServerMarkerSweep::UpdateCurrentMarkBitmap(void) {
  mark_bitmaps_.clear();
  live_bitmaps_.clear();
  accounting::GCSrvceSharedHeapBitmap* live_heap_beetmap =
                                          &(heap_meta_->live_heap_bitmap_data_);
  for(int i = 0; i < live_heap_beetmap->index_; i++) {
    for (const auto& beetmap : all_bitmaps_) {
      if(beetmap->bitmap_data_ == live_heap_beetmap->bitmap_headers_[i]) {
        live_bitmaps_.push_back(beetmap);
        break;
      }
    }
  }

  accounting::GCSrvceSharedHeapBitmap* mark_heap_beetmap =
                                          &(heap_meta_->mark_heap_bitmap_data_);
  for(int i = 0; i < mark_heap_beetmap->index_; i++) {
    for (const auto& beetmap : all_bitmaps_) {
      if(beetmap->bitmap_data_ == mark_heap_beetmap->bitmap_headers_[i]) {
        mark_bitmaps_.push_back(beetmap);
        if(curr_collector_ptr_->current_mark_bitmap_ == beetmap->bitmap_data_) {
          current_mark_bitmap_ = beetmap;
        }
        break;
      }
    }
  }

  LOG(ERROR) << " ####### marks_size = " << mark_bitmaps_.size() <<
      " lives_size = " <<  live_bitmaps_.size() << " ####### ";
  for (const auto& beetmap : live_bitmaps_) {
    LOG(ERROR) << " live: " << beetmap->bitmap_data_ << ", " <<
        beetmap->bitmap_data_->name_;
  }
  for (const auto& beetmap : mark_bitmaps_) {
    LOG(ERROR) << " mark: " << beetmap->bitmap_data_ << ", " <<
        beetmap->bitmap_data_->name_;
  }
  LOG(ERROR) << " current_mark_bitmap_: " << current_mark_bitmap_->bitmap_data_ <<
      ", " << current_mark_bitmap_->bitmap_data_->name_;


}


bool IPCServerMarkerSweep::InitMarkingPhase(space::GCSrvSharableCollectorData* collector_addr) {
  if(mark_stack_ == NULL) {
    mark_stack_ = GetMappedMarkStack(client_rec_->pair_mapps_,
        KGCSpaceServerMarkStackInd_,
      &(client_rec_->sharable_space_->heap_meta_.mark_stack_data_));
  }

  if(all_bitmaps_.empty()) {
    accounting::SharedServerSpaceBitmap* _temp_beetmap = NULL;
    _temp_beetmap = GetMappedBitmap(client_rec_->pair_mapps_,
        KGCSpaceServerZygoteMarkBMInd_,
      &(heap_meta_->reshared_zygote_.mark_bitmap_));
    all_bitmaps_.push_back(_temp_beetmap);

    _temp_beetmap = GetMappedBitmap(client_rec_->pair_mapps_,
        KGCSpaceServerZygoteLiveBMInd_,
      &(heap_meta_->reshared_zygote_.live_bitmap_));
    all_bitmaps_.push_back(_temp_beetmap);

    _temp_beetmap = GetMappedBitmap(client_rec_->pair_mapps_,
        KGCSpaceServerMarkBitmapInd_, &(client_rec_->sharable_space_->mark_bitmap_));
    all_bitmaps_.push_back(_temp_beetmap);

    _temp_beetmap = GetMappedBitmap(client_rec_->pair_mapps_,
        KGCSpaceServerLiveBitmapInd_, &(client_rec_->sharable_space_->live_bitmap_));
    all_bitmaps_.push_back(_temp_beetmap);
  }


  if(mark_stack_->IsEmpty())
    return false;


  curr_collector_ptr_ = collector_addr;


  UpdateCurrentMarkBitmap();


//  if(all_bitmaps_.empty()) {
//    if(gc::gcservice::GCServiceGlobalAllocator::KGCServiceShareZygoteSpace > 1) {
//      accounting::SharedServerSpaceBitmap* _temp_mark_bitmap =
//          GetMappedBitmap(client_rec_->pair_mapps_,
//              KGCSpaceServerZygoteMarkBMInd_,
//            &(heap_meta_->reshared_zygote_.mark_bitmap_));
//      all_bitmaps_.push_back(_temp_mark_bitmap);
//
//
//      _temp_mark_bitmap =
//          GetMappedBitmap(client_rec_->pair_mapps_,
//              KGCSpaceServerZygoteLiveBMInd_,
//            &(heap_meta_->reshared_zygote_.live_bitmap_));
//      all_bitmaps_.push_back(_temp_mark_bitmap);
//    }
//  }
//
//  if(mark_bitmaps_.empty()) {
//    if(current_mark_bitmap_ == NULL) {
//      current_mark_bitmap_ = GetMappedBitmap(client_rec_->pair_mapps_,
//          KGCSpaceServerMarkBitmapInd_,
//            (curr_collector_ptr_->current_mark_bitmap_));
//      mark_bitmaps_.push_back(current_mark_bitmap_);
//    }
//    if(gc::gcservice::GCServiceGlobalAllocator::KGCServiceShareZygoteSpace > 1) {
//      accounting::SharedServerSpaceBitmap* _temp_mark_bitmap =
//          GetMappedBitmap(client_rec_->pair_mapps_,
//              KGCSpaceServerZygoteMarkBMInd_,
//            &(heap_meta_->reshared_zygote_.mark_bitmap_));
//      mark_bitmaps_.push_back(_temp_mark_bitmap);
//
//      _temp_mark_bitmap =
//          GetMappedBitmap(client_rec_->pair_mapps_,
//              KGCSpaceServerZygoteLiveBMInd_,
//            &(heap_meta_->reshared_zygote_.live_bitmap_));
//      live_bitmaps_.push_back(_temp_mark_bitmap);
//    }
//
//    LOG(ERROR) << "Pushed the mark_bitmaps in to the stack.." <<
//        mark_bitmaps_.size();
//
//
//  }





  ResetStats();

  LOG(ERROR) << "-------------------------RESTARTING-------------------------";

  SetCachedReferencesPointers(&cashed_references_client_,
      &curr_collector_ptr_->cashed_references_);




  LOG(ERROR) << "----------------------DONE RESTARTING-----------------------";
  return true;
}




}
}
}
