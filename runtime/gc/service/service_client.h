/*
 * service_client.h
 *
 *  Created on: Sep 29, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_
#define ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_

#include "gc/space/dlmalloc_space.h"
#include "ipcfs/ipcfs.h"
#include "gc/collector/ipc_mark_sweep.h"
#include "gc/heap.h"
namespace art {

namespace gcservice {


class GCServiceClient {

 public:
  static GCServiceClient* service_client_;
  static void InitClient(const char* se_name_c_str);
  static void FinalizeInitClient();
  static bool RequestConcGC(void);
  static bool RequestExplicitGC(void);
  static bool RequestAllocateGC(void) ;
  static bool RequestWaitForConcurrentGC(gc::collector::GcType* type);
  static bool RequestInternalGC(gc::collector::GcType gc_type, gc::GcCause gc_cause,
      bool clear_soft_references, gc::collector::GcType* gctype);
  static bool SetNextGCType(gc::collector::GcType gc_type);
  static bool GetNextGCType(gc::collector::GcType* gc_type);
  static bool SetConcStartBytes(size_t conc_start);
  static bool GetConcStartBytes(size_t* conc_start);
  static void RequestHeapTrim(void);
  void FinalizeHeapAfterInit(void);
  void ConstructHeap(void);

  void FillMemMapData(android::FileMapperParameters* rec);

  void FillAshMemMapData(android::IPCAShmemMap* rec,
      AShmemMap* shmem_map);
 private:
  GCServiceClient(gc::space::SharableDlMallocSpace*, int);

  int index_;
  gc::space::SharableDlMallocSpace* sharable_space_;
  //gc::collector::IPCMarkSweep* collector_;
  gc::collector::IPCHeap* ipcHeap_;






};//GCServiceClient

}//namespace gcservice
}//namespace art

#endif /* ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_ */
