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
#include "gc/collector/ipc_server_sweep.h"
#include "gc/collector/ipc_server_sweep-inl.h"
#include "base/bounded_fifo.h"
#include "mirror/object-inl.h"
#include "gc/collector/mark_sweep.h"
#include "mirror/class.h"
#include "mirror/object_array.h"
#include "mirror/art_field.h"

#define SERVER_SWEEP_CALC_OFFSET(x, y) (x > y) ? (x - y) : (y - x)

using ::art::mirror::Class;
using ::art::mirror::Object;

namespace art {
namespace gc {
namespace collector {



class ServerMarkObjectVisitor {
 public:
  explicit ServerMarkObjectVisitor(IPCServerMarkerSweep* const server_mark_sweep)
          ALWAYS_INLINE : mark_sweep_(server_mark_sweep) {}

  // TODO: Fixme when anotatalysis works with visitors.
  void operator()(const Object* /* obj */, const Object* ref, MemberOffset& /* offset */,
                  bool /* is_static */) const ALWAYS_INLINE
      NO_THREAD_SAFETY_ANALYSIS {
//    if (kCheckLocks) {
//      Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
//      Locks::heap_bitmap_lock_->AssertExclusiveHeld(Thread::Current());
//    }
    mark_sweep_->MarkObject(ref);
  }

 private:
  IPCServerMarkerSweep* const mark_sweep_;
};


void IPCServerMarkerSweep::ResetStats(void) {
  android_atomic_acquire_store(0, &(array_count_));
  android_atomic_acquire_store(0, &(class_count_));
  android_atomic_acquire_store(0, &(other_count_));
}

IPCServerMarkerSweep::IPCServerMarkerSweep(
    gc::gcservice::GCServiceClientRecord* client_record) :
        client_rec_(client_record),
        heap_meta_(&(client_rec_->sharable_space_->heap_meta_)),
        offset_(SERVER_SWEEP_CALC_OFFSET(client_rec_->pair_mapps_->first->mem_maps_[0].begin_,
        client_rec_->pair_mapps_->second->mem_maps_[0].begin_)),
        curr_collector_ptr_(NULL),
        current_mark_bitmap_(NULL),
        mark_stack_(NULL),
        java_lang_Class_client_(client_record->java_lang_Class_cached_),
        current_immune_begin_(NULL),
        current_immune_end_(NULL) {


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


  //set the sharable space to be shared
  android_atomic_acquire_store(2, &(client_rec_->sharable_space_->register_gc_));

  LOG(ERROR) << "Initialized the IPC_SERVER_SWEEP with Offset:" << offset_ <<
      ", java_lang_class = " << reinterpret_cast<void*>(java_lang_Class_client_);
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    LOG(ERROR) << StringPrintf("...space[%d]  --> client-start=%p, client-end=%p", i,
        spaces_[i].client_base_, spaces_[i].client_end_);
  }
  for(int i = KGCSpaceServerImageInd_; i <= KGCSpaceServerAllocInd_; i++) {
    LOG(ERROR) << StringPrintf("...space[%d]  --> server-start=%p, server-end=%p", i,
        spaces_[i].base_, spaces_[i].base_end_);
  }
}


void IPCServerMarkerSweep::MarkReachableObjects(space::GCSrvSharableCollectorData* collector_addr) {
  Thread* _self = Thread::Current();
  LOG(ERROR) << " ++++ IPCServerMarkerSweep::MarkReachableObjects: "
      << _self->GetTid() << "; address " <<
      reinterpret_cast<void*>(collector_addr);

  InitMarkingPhase(collector_addr);

  ProcessMarckStack();


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

  if(current_mark_bitmap_ == NULL) {
    current_mark_bitmap_ =
        new accounting::SharedServerSpaceBitmap(bitmap_meta_addr,
            mapped_server_address, offset_);
  }
  return current_mark_bitmap_;
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

static void ExternalScanObjectVisit(mirror::Object* obj,
    void* args) {
  IPCServerMarkerSweep* param =
      reinterpret_cast<IPCServerMarkerSweep*>(args);
  //uint32_t calc_offset = (param->offset_ / sizeof(Object*));
//  uint32_t* calc_offset = reinterpret_cast<uint32_t*>(calculated_offset);



  param->ServerScanObject(obj, param->offset_);
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
  uint32_t calculated_offset = offset_ / (sizeof(Object*));
  LOG(ERROR) << "Calculated offset..." <<  calculated_offset;

  mark_stack_->OperateOnStack(ExternalScanObjectVisit,
      this);
  LOG(ERROR) << "+++++++++++++++++++++++ array_count = " << array_count_ <<
      ", class_count = " << class_count_ << ", class_count = " << other_count_;
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


void IPCServerMarkerSweep::InitMarkingPhase(space::GCSrvSharableCollectorData* collector_addr) {
  curr_collector_ptr_ = collector_addr;

  if(mark_stack_ == NULL)
    mark_stack_ = GetMappedMarkStack(client_rec_->pair_mapps_,
        KGCSpaceServerMarkStackInd_,
      &(client_rec_->sharable_space_->mark_stack_data_));
  if(current_mark_bitmap_ == NULL) {
    current_mark_bitmap_ = GetMappedBitmap(client_rec_->pair_mapps_,
        KGCSpaceServerMarkBitmapInd_,
          (curr_collector_ptr_->current_mark_bitmap_));
  }

  ResetStats();

  current_immune_begin_ =
      const_cast<mirror::Object*>(
          MapReferenceToServer<mirror::Object>(curr_collector_ptr_->immune_begin_));
  current_immune_end_ =
      const_cast<mirror::Object*>(
          MapReferenceToServer<mirror::Object>(curr_collector_ptr_->immune_end_));
}

}
}
}
