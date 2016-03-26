/*
 * service_client.h
 *
 *  Created on: Sep 29, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_
#define ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_


#if (ART_GC_SERVICE || true)

#include "gc/space/dlmalloc_space.h"
#include "ipcfs/ipcfs.h"
#include "gc/collector/ipc_mark_sweep.h"
#include "gc/heap.h"
#include "gc/service/global_allocator.h"
namespace art {

namespace gcservice {


class GCServiceClient {

 public:
  static GCServiceClient* service_client_;

  static void InitClient(const char* se_name_c_str, int trim_config);
  static void FinalizeInitClient();
  static bool RequestConcGC(void);
  static bool RequestExplicitGC(void);
  static bool RequestAllocateGC(void) ;
  static bool RequestWaitForConcurrentGC(gc::collector::GcType* type);
  static bool RequestInternalGC(gc::collector::GcType gc_type, gc::GcCause gc_cause,
      bool clear_soft_references, gc::collector::GcType* gctype);
//  static bool SetNextGCType(gc::collector::GcType gc_type);
//  static bool GetNextGCType(gc::collector::GcType* gc_type);
//  static bool SetConcStartBytes(size_t conc_start);
//  static bool GetConcStartBytes(size_t* conc_start);
  static void RequestHeapTrim(void);
  void FinalizeHeapAfterInit(void);
  void ConstructHeap(void);

  void FillMemMapData(android::FileMapperParameters* rec);

  void FillAshMemMapData(android::IPCAShmemMap* rec,
      AShmemMap* shmem_map);

  bool isTrimRequestsEnabled() const {
    return (enable_trimming_ == gc::gcservice::GC_SERVICE_HANDLE_TRIM_ALLOWED);
  }

  void setConcRequestTime(uint64_t timestamp, uint64_t heapsize) {
    sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_ = timestamp;
    sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_ = heapsize;

    LOG(ERROR) << "**TimeStamp conc_req: " << timestamp << ", " <<heapsize;
  }
  void setExplRequestTime(uint64_t timestamp, uint64_t heapsize) {
    sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_time_ns_ = timestamp;
    sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_ = heapsize;

    LOG(ERROR) << "++TimeStamp explicit: " << timestamp << ", " <<heapsize;
  }

  void updateDeltaConcReq(uint64_t timestamp, uint64_t heapsize) {
    uint64_t _delta_ts = (timestamp -
        sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_) / 1000;
    uint64_t _delta_heapsize = heapsize -
        sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_;

    LOG(ERROR) << "===End conc_req: " << timestamp << ", " << heapsize <<
       ", ration = " <<  ((_delta_heapsize * 100.0) / _delta_ts) ;
  }


  void updateDeltaExplReq(uint64_t timestamp, uint64_t heapsize) {
    uint64_t _delta_ts = (timestamp -
        sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_time_ns_) / 1000;
    uint64_t _delta_heapsize = heapsize -
        sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_;

    LOG(ERROR) << "===End expl_req: " << timestamp << ", " << heapsize <<
       ", ration = " <<  ((_delta_heapsize * 100.0) / _delta_ts) ;
  }

  gc::space::AgentMemInfo* GetMemInfoRec(void) {
    return &sharable_space_->sharable_space_data_->meminfo_rec_;
  }



 private:
  GCServiceClient(gc::space::SharableDlMallocSpace*, int, int);
  int index_;
  int enable_trimming_;
  gc::space::SharableDlMallocSpace* sharable_space_;
  //gc::collector::IPCMarkSweep* collector_;
  gc::collector::IPCHeap* ipcHeap_;






};//GCServiceClient

}//namespace gcservice
}//namespace art


#endif //ART_GC_SERVICE

#endif /* ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_ */
