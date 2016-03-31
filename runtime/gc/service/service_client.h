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
#include "utils.h"

namespace art {
namespace gc {
namespace service {


class GCServiceClient {

 public:
  static GCServiceClient* service_client_;

  static void InitClient(const char* se_name_c_str, int trim_config);
  static void FinalizeInitClient();
  static bool RequestUpdateStats(void);
  static bool RequestConcGC(void);
  static bool RemoveGCSrvcActiveRequest(GC_SERVICE_TASK task);
  static bool RequestExplicitGC(void);
  bool ShouldPushNewGCRequest(GC_SERVICE_TASK task);
  static bool RequestAllocateGC(void) ;
  static bool RequestWaitForConcurrentGC(collector::GcType* type);
  static bool RequestInternalGC(collector::GcType gc_type, GcCause gc_cause,
      bool clear_soft_references, collector::GcType* gctype);
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
    return (enable_trimming_ == service::GC_SERVICE_HANDLE_TRIM_ALLOWED);
  }

  void setMemInfoMarkStamp(uint64_t timestamp, uint64_t heapsize,
                           GC_SERVICE_TASK task) {
    int _index = 32 - (CLZ(GC_SERVICE_TASK)) - 2;
    space::AgnetMemInfoTimeStamp* _stamp_rec =
        &(sharable_space_->sharable_space_data_->meminfo_rec_.time_stamps_[_index]);
    _stamp_rec->req_heap_size_ = heapsize;
    _stamp_rec->req_time_ns_ = timestamp;
  }

  void updateDeltaReqLatency(uint64_t timestamp, uint64_t heapsize,
                          uint64_t* time_latency, uint64_t* heap_latency,
                          GC_SERVICE_TASK task) {
    int _index = 32 - CLZ(GC_SERVICE_TASK) - 2;
    space::AgnetMemInfoTimeStamp* _stamp_rec =
        &(sharable_space_->sharable_space_data_->meminfo_rec_.time_stamps_[_index]);
    if(heapsize < _stamp_rec->req_heap_size_) {
      *heap_latency = 0;
    } else {
      *heap_latency = heapsize - _stamp_rec->req_heap_size_;
    }
    *time_latency = (timestamp - _stamp_rec->req_time_ns_) ;
  }

//  void setConcRequestTime(uint64_t timestamp, uint64_t heapsize) {
//    sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_ = timestamp;
//    sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_ = heapsize;
//
//  }
//  void setExplRequestTime(uint64_t timestamp, uint64_t heapsize) {
//    sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_time_ns_ = timestamp;
//    sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_ = heapsize;
//  }
//  void setAllocRequestTime(uint64_t timestamp, uint64_t heapsize) {
//    sharable_space_->sharable_space_data_->meminfo_rec_.alloc_req_time_ns_ = timestamp;
//    sharable_space_->sharable_space_data_->meminfo_rec_.alloc_req_heap_size_ = heapsize;
//  }
//
//  void updateDeltaConcReq(uint64_t timestamp, uint64_t heapsize,
//                          uint64_t* time_latency, uint64_t* heap_latency) {
//    if(heapsize < sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_) {
////      LOG(ERROR) << "DANGER:::: concurrent ..current=" << heapsize
////          << ", marked="
////          << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_
////          << ", curr_time=" << timestamp << ", marked_time = " << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_;
//      *heap_latency = 0;
//    } else {
//      *heap_latency = heapsize -
//          sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_;
//    }
//    *time_latency = (timestamp -
//        sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_) ;
//
//  }
//
//  void updateDeltaAllocReq(uint64_t timestamp, uint64_t heapsize,
//                          uint64_t* time_latency, uint64_t* heap_latency) {
//    if(heapsize < sharable_space_->sharable_space_data_->meminfo_rec_.alloc_req_heap_size_) {
////      LOG(ERROR) << "DANGER:::: concurrent ..current=" << heapsize
////          << ", marked="
////          << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_
////          << ", curr_time=" << timestamp << ", marked_time = " << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_;
//      *heap_latency = 0;
//    } else {
//      *heap_latency = heapsize -
//          sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_;
//    }
//    *time_latency = (timestamp -
//        sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_) ;
//
//  }
//
//
//  void updateDeltaExplReq(uint64_t timestamp, uint64_t heapsize,
//                          uint64_t* time_latency, uint64_t* heap_latency) {
//
//    if(heapsize < sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_) {
////      LOG(ERROR) << "DANGER:::: explicit ..current=" << heapsize
////          << ", marked="
////          << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_
////          << ", curr_time=" << timestamp
////          << ", marked_time = "
////          << sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_;
//      *heap_latency = 0;
//    } else {
//      *heap_latency = heapsize -
//          sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_;
//    }
//
//    *time_latency = (timestamp -
//        sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_time_ns_);
//  }

  space::AgentMemInfo* GetMemInfoRec(void) {
    return &sharable_space_->sharable_space_data_->meminfo_rec_;
  }



  void updateProcessState(void);
 private:
  GCServiceClient(space::SharableDlMallocSpace*, int, int);
  int index_;
  int enable_trimming_;
  int last_process_state_;
  int last_process_oom_;
  space::SharableDlMallocSpace* sharable_space_;
  Mutex* gcservice_client_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  //gc::collector::IPCMarkSweep* collector_;
  collector::IPCHeap* ipcHeap_;

  std::vector<service::GCServiceReq*> active_requests_;





};//GCServiceClient
}//namespace service
}//namespace gc
}//namespace art


#endif //ART_GC_SERVICE

#endif /* ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_ */
