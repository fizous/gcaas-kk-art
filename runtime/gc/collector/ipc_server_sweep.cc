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


namespace art {
namespace gc {
namespace collector {

IPCServerMarkerSweep::IPCServerMarkerSweep(gc::gcservice::GCServiceClientRecord* client_record) :
    client_rec_(client_record),
    heap_meta_(&(client_rec_->sharable_space_->heap_meta_)),
    offset_(client_rec_->pair_mapps_->first->mem_maps_[0].begin_ -
        client_rec_->pair_mapps_->second->mem_maps_[0].begin_),
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
      reinterpret_cast<unsigned int>(spaces_[KGCSpaceServerZygoteInd_].base_) + offset_;

  spaces_[KGCSpaceServerAllocInd_].client_base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[1].begin_);
  spaces_[KGCSpaceServerAllocInd_].client_end_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->first->mem_maps_[1].begin_) +
      client_rec_->pair_mapps_->first->mem_maps_[1].size_;
  spaces_[KGCSpaceServerAllocInd_].base_ =
      reinterpret_cast<byte*>(client_rec_->pair_mapps_->second->mem_maps_[1].begin_);
  spaces_[KGCSpaceServerAllocInd_].base_offset_ =
      reinterpret_cast<unsigned int>(spaces_[KGCSpaceServerAllocInd_].base_) + offset_;

  spaces_[KGCSpaceServerImageInd_].client_base_ =
      heap_meta_->image_space_begin_;
  spaces_[KGCSpaceServerImageInd_].client_end_ =
      heap_meta_->image_space_end_;
  spaces_[KGCSpaceServerImageInd_].base_ =
      spaces_[KGCSpaceServerImageInd_].client_base_;
  spaces_[KGCSpaceServerImageInd_].base_offset_ = 0;
}

bool IPCServerMarkerSweep::ClientSpaceContains(mirror::Object* obj,
    GCSrverCollectorSpace* server_space) {
  byte* byte_ptr = reinterpret_cast<byte*>(obj);
  return (byte_ptr < server_space->client_end_) && (byte_ptr >= server_space->client_base_);
}

mirror::Object* IPCServerMarkerSweep::MapObjectAddress(mirror::Object* obj) {
  GCSrverCollectorSpace* _space_rec = NULL;
  for(int i = 0 ; i < KGCSpaceCount; i ++) {
    _space_rec = &spaces_[i];
    if(ClientSpaceContains(obj, _space_rec)) { // found the space
      return obj + _space_rec->base_offset_;
    }
  }
  LOG(ERROR) << "Could not map Object from Space "  << obj;
  return obj;
}





accounting::SharedSpaceBitmap* IPCServerMarkerSweep::GetMappedBitmap(android::MappedPairProcessFD* pair_memory,
    int entry_ind, accounting::GCSrvceBitmap* bitmap_meta_addr) {
//  android::IPCAShmemMap* _client_address =
//      &(pair_memory->first->mem_maps_[entry_ind]);
  android::IPCAShmemMap* _server_address =
      &(pair_memory->second->mem_maps_[entry_ind]);
  AShmemMap* _bitmap_mem_map = &(bitmap_meta_addr->mem_map_);
  _bitmap_mem_map->begin_ = reinterpret_cast<byte*>(_server_address->begin_);
  bitmap_meta_addr->heap_begin_ =
      reinterpret_cast<uintptr_t>(spaces_[KGCSpaceServerAllocInd_].base_);
  bitmap_meta_addr->bitmap_begin_ = reinterpret_cast<word*>(_bitmap_mem_map->begin_);


  return new accounting::SharedSpaceBitmap(bitmap_meta_addr);

}

void IPCServerMarkerSweep::FindDefaultMarkBitmap(void) {

  accounting::GCSrvceBitmap* _bitmap_space =
      curr_collector_ptr_->current_mark_bitmap_;

  memcpy(&mark_bitmap_,  _bitmap_space,
      SERVICE_ALLOC_ALIGN_BYTE(accounting::GCSrvceBitmap));

  current_mark_bitmap_ =
      GetMappedBitmap(client_rec_->pair_mapps_, 4, &mark_bitmap_);

}



accounting::ATOMIC_OBJ_STACK_T*  IPCServerMarkerSweep::GetMappedMarkStack(
    android::MappedPairProcessFD* pair_memory,
    int entry_ind,
    StructuredObjectStackData* stack_meta_address) {

  android::IPCAShmemMap* _client_address =
      &(pair_memory->first->mem_maps_[entry_ind]);
  android::IPCAShmemMap* _server_address =
      &(pair_memory->second->mem_maps_[entry_ind]);


  uintptr_t _stack_offset = _server_address->begin_ - _client_address->begin_;
  accounting::ATOMIC_OBJ_STACK_T* atomic_stack_dup =
      accounting::ATOMIC_OBJ_STACK_T::CreateAtomicStack(stack_meta_address/*,
          _stack_offset*/);

  return atomic_stack_dup;
}

void IPCServerMarkerSweep::MarkRemoteObject(space::GCSrvSharableCollectorData* collector_meta) {
  curr_collector_ptr_ = collector_meta;
  mark_stack_ = GetMappedMarkStack(client_rec_->pair_mapps_, 5,
      &(client_rec_->sharable_space_->mark_stack_data_));
  FindDefaultMarkBitmap();
}




}
}
}
