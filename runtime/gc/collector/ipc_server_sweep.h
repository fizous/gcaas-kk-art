/*
 * ipc_server_sweep.h
 *
 *  Created on: Nov 2, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_
#define ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_


#include "ipcfs/ipcfs.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "gc/service/global_allocator.h"
#include "gc/space/space.h"
#include "gc/heap.h"

namespace art {
namespace gc {
namespace collector {


typedef struct GCSrverCollectorSpace_S {
  // Immune range, every object inside the immune range is assumed to be marked.
  uintptr_t base_;
  uintptr_t base_offset_;
  uintptr_t client_base_;
  uintptr_t client_end_;
} __attribute__((aligned(8))) GCSrverCollectorSpace;

class IPCServerMarkerSweep {
 public:
  static const int KGCSpaceCount = 3;
  static const int KGCSpaceServerImageInd_  = 0;
  static const int KGCSpaceServerZygoteInd_ = 1;
  static const int KGCSpaceServerAllocInd_  = 2;

  gc::gcservice::GCServiceClientRecord* const client_rec_;
  space::GCSrvSharableHeapData* const heap_meta_;
  const uintptr_t offset_;

  GCSrverCollectorSpace spaces_[KGCSpaceCount];
  space::GCSrvSharableCollectorData* curr_collector_ptr_;

  accounting::GCSrvceBitmap mark_bitmap_;
  accounting::SharedSpaceBitmap* current_mark_bitmap_;
  accounting::ATOMIC_OBJ_STACK_T* mark_stack_;

  IPCServerMarkerSweep(gc::gcservice::GCServiceClientRecord* client_record);

  mirror::Object* MapObjectAddress(mirror::Object* obj);
  bool ClientSpaceContains(mirror::Object* obj, GCSrverCollectorSpace* server_space);

  void MarkRemoteObject(space::GCSrvSharableCollectorData* collector_meta);
  void FindDefaultMarkBitmap(void);


  accounting::SharedSpaceBitmap*  GetMappedBitmap(android::MappedPairProcessFD* pair_memory,
      int entr_ind,
      accounting::GCSrvceBitmap* bitmap_meta_addr);

  accounting::ATOMIC_OBJ_STACK_T*  GetMappedMarkStack(android::MappedPairProcessFD* pair_memory,
      int entr_ind,
      StructuredObjectStackData* stack_meta_address);
};//class IPCServerMarkerSweep

}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_ */
