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
  static bool RequestUpdateStats(void);
  static bool RequestConcGC(void);
  static bool RemoveGCSrvcActiveRequest(gc::gcservice::GC_SERVICE_TASK task);
  static bool RequestExplicitGC(void);
  bool ShouldPushNewTrimRequest(gc::gcservice::GC_SERVICE_TASK task);
  bool ShouldPushNewRequest(gc::gcservice::GC_SERVICE_TASK task);
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

  }
  void setExplRequestTime(uint64_t timestamp, uint64_t heapsize) {
    sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_time_ns_ = timestamp;
    sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_ = heapsize;
  }

  void updateDeltaConcReq(uint64_t timestamp, uint64_t heapsize,
                          uint64_t* time_latency, uint64_t* heap_latency) {
    if(heapsize < sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_) {
//      LOG(ERROR) << "DANGER:::: concurrent ..current=" << heapsize
//          << ", marked="
//          << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_
//          << ", curr_time=" << timestamp << ", marked_time = " << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_;
      *heap_latency = 0;
    } else {
      *heap_latency = heapsize -
          sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_;
    }
    *time_latency = (timestamp -
        sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_time_ns_) ;

  }


  void updateDeltaExplReq(uint64_t timestamp, uint64_t heapsize,
                          uint64_t* time_latency, uint64_t* heap_latency) {

    if(heapsize < sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_) {
//      LOG(ERROR) << "DANGER:::: explicit ..current=" << heapsize
//          << ", marked="
//          << sharable_space_->sharable_space_data_->meminfo_rec_.conc_req_heap_size_
//          << ", curr_time=" << timestamp
//          << ", marked_time = "
//          << sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_;
      *heap_latency = 0;
    } else {
      *heap_latency = heapsize -
          sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_heap_size_;
    }

    *time_latency = (timestamp -
        sharable_space_->sharable_space_data_->meminfo_rec_.expl_req_time_ns_);
  }

  gc::space::AgentMemInfo* GetMemInfoRec(void) {
    return &sharable_space_->sharable_space_data_->meminfo_rec_;
  }



  void updateProcessState(void);
 private:
  GCServiceClient(gc::space::SharableDlMallocSpace*, int, int);
  int index_;
  int enable_trimming_;
  int last_process_state_;
  gc::space::SharableDlMallocSpace* sharable_space_;
  Mutex* gcservice_client_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  //gc::collector::IPCMarkSweep* collector_;
  gc::collector::IPCHeap* ipcHeap_;

  std::vector<gc::gcservice::GCServiceReq*> active_requests_;





};//GCServiceClient

}//namespace gcservice
}//namespace art


#endif //ART_GC_SERVICE

#endif /* ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_ */
