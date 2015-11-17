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
  byte* base_;
  byte* base_offset_;
  byte* client_base_;
  byte* client_end_;
} __attribute__((aligned(8))) GCSrverCollectorSpace;


class IPCServerMarkerSweep {
 public:
  static const int KGCSpaceCount = 3;
  static const int KGCSpaceServerImageInd_    = 0;
  static const int KGCSpaceServerZygoteInd_   = 1;
  static const int KGCSpaceServerAllocInd_    = 2;


  static const int KGCSpaceServerMarkStackInd_    = 2;
  static const int KGCSpaceServerMarkBitmapInd_   = 3;

  gcservice::GCServiceClientRecord* const client_rec_;
  space::GCSrvSharableHeapData* const heap_meta_;
  const uint32_t offset_;

  GCSrverCollectorSpace spaces_[KGCSpaceCount];
  space::GCSrvSharableCollectorData* curr_collector_ptr_;

  accounting::GCSrvceBitmap mark_bitmap_;
  accounting::SharedServerSpaceBitmap* current_mark_bitmap_;
  accounting::ATOMIC_OBJ_STACK_T* mark_stack_;

  IPCServerMarkerSweep(gcservice::GCServiceClientRecord* client_record);

  mirror::Object* MapObjectAddress(mirror::Object* obj);
  bool ClientSpaceContains(mirror::Object* obj, GCSrverCollectorSpace* server_space);

  void MarkRemoteObject(space::GCSrvSharableCollectorData* collector_meta);
  void FindDefaultMarkBitmap(void);


  accounting::SharedServerSpaceBitmap* GetMappedBitmap(
      android::MappedPairProcessFD* pair_memory,
      int entry_ind, accounting::GCSrvceBitmap* bitmap_meta_addr);

  accounting::ATOMIC_OBJ_STACK_T* GetMappedMarkStack(
      android::MappedPairProcessFD* pair_memory,
      int entr_ind,
      StructuredObjectStackData* stack_meta_address);


  void ProcessMarckStack(void);
  void ScanObjectVisit(mirror::Object* obj, uint32_t calculated_offset);
  //void ExternalScanObjectVisit(mirror::Object* obj, void* calculated_offset);
  void MarkReachableObjects(space::GCSrvSharableCollectorData* collector_addr);



};//class IPCServerMarkerSweep

}
}
}
#endif /* ART_RUNTIME_GC_COLLECTOR_IPC_SERVER_SWEEP_H_ */
