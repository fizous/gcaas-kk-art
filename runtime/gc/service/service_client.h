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

namespace art {

namespace gcservice {


class GCServiceClient {

 public:
  static GCServiceClient* service_client_;
  static void InitClient(const char* se_name_c_str);
  static void FinalizeInitClient();
  void FinalizeHeapAfterInit(void);
  void ConstructHeap(void);

  void FillMemMapData(android::FileMapperParameters* rec);

  void FillAshMemMapData(android::IPCAShmemMap* rec,
      AShmemMap* shmem_map);
 private:
  GCServiceClient(gc::space::SharableDlMallocSpace*, int);

  int index_;
  gc::space::SharableDlMallocSpace* sharable_space_;
};//GCServiceClient

}//namespace gcservice
}//namespace art

#endif /* ART_RUNTIME_GC_SERVICE_SERVICE_CLIENT_H_ */
