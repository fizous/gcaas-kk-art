/*
 * service_client.h
 *
 *  Created on: Sep 2, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_SERVICE_CLIENT_H_
#define ART_RUNTIME_GC_GCSERVICE_SERVICE_CLIENT_H_

#include "gc/gcservice/common.h"

namespace art {

namespace gcservice {


class GCServiceClient {

 public:
  static GCServiceClient* service_client_;
  static void InitClient();
  static void FinalizeInitClient();
  void FinalizeHeapAfterInit(void);
  void ConstructHeap(void);
 private:
  GCServiceClient(int);

  int index_;
  SharedHeapMetada* heap_meta_;
};//GCServiceClient

}//namespace gcservice
}//namespace art


#endif /* ART_RUNTIME_GC_GCSERVICE_SERVICE_CLIENT_H_ */
