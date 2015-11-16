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


#define SERVER_SWEEP_CALC_OFFSET(x, y) (x > y) ? (x - y) : (y - x)

namespace art {
namespace gc {
namespace collector {



IPCServerMarkerSweep::IPCServerMarkerSweep(gc::gcservice::GCServiceClientRecord* client_record) :
    client_rec_(client_record),
    heap_meta_(&(client_rec_->sharable_space_->heap_meta_)),
    offset_(SERVER_SWEEP_CALC_OFFSET(client_rec_->pair_mapps_->first->mem_maps_[0].begin_,
        client_rec_->pair_mapps_->second->mem_maps_[0].begin_)),
        curr_collector_ptr_(NULL),
        current_mark_bitmap_(NULL),
        mark_stack_(NULL) {


  spaces_[KGCSpaceServerZygoteInd_].client_base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[0].begin_);
  spaces_[KGCSpaceServerZygoteInd_].client_end_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[0].begin_) +
      client_rec_->pair_mapps_->first->mem_maps_[0].size_;
  spaces_[KGCSpaceServerZygoteInd_].base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->second->mem_maps_[0].begin_);
  spaces_[KGCSpaceServerZygoteInd_].base_offset_ =
      spaces_[KGCSpaceServerZygoteInd_].base_ + offset_;

  spaces_[KGCSpaceServerAllocInd_].client_base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[1].begin_);
  spaces_[KGCSpaceServerAllocInd_].client_end_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[1].begin_) +
      client_rec_->pair_mapps_->first->mem_maps_[1].size_;
  spaces_[KGCSpaceServerAllocInd_].base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->second->mem_maps_[1].begin_);
  spaces_[KGCSpaceServerAllocInd_].base_offset_ =
      spaces_[KGCSpaceServerAllocInd_].base_ + offset_;

  spaces_[KGCSpaceServerImageInd_].client_base_ =
      heap_meta_->image_space_begin_;
  spaces_[KGCSpaceServerImageInd_].client_end_ =
      heap_meta_->image_space_end_;
  spaces_[KGCSpaceServerImageInd_].base_ =
      spaces_[KGCSpaceServerImageInd_].client_base_;
  spaces_[KGCSpaceServerImageInd_].base_offset_ = 0;


  //set the sharable space to be shared
  android_atomic_acquire_store(2, &(client_rec_->sharable_space_->register_gc_));

  LOG(ERROR) << "Initialized the IPC_SERVER_SWEEP with Offset:" << offset_;
}


void IPCServerMarkerSweep::MarkReachableObjects(space::GCSrvSharableCollectorData* collector_addr) {
  Thread* _self = Thread::Current();
  LOG(ERROR) << " ++++ IPCServerMarkerSweep::MarkReachableObjects: "
      << _self->GetTid() << "; address " << reinterpret_cast<void*>(collector_addr);

  if(mark_stack_ == NULL)
    mark_stack_ = GetMappedMarkStack(client_rec_->pair_mapps_,
        KGCSpaceServerMarkStackInd_,
      &(client_rec_->sharable_space_->mark_stack_data_));
  if(current_mark_bitmap_ == NULL) {
    current_mark_bitmap_ = GetMappedBitmap(client_rec_->pair_mapps_,
        KGCSpaceServerMarkBitmapInd_,
          (curr_collector_ptr_->current_mark_bitmap_));
  }

  mark_stack_->DumpDataEntries(true);


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

}
}
}
