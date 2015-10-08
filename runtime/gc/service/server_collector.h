/*
 * server_collector.h
 *
 *  Created on: Oct 7, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SERVICE_SERVER_COLLECTOR_H_
#define ART_RUNTIME_GC_SERVICE_SERVER_COLLECTOR_H_


#include "scoped_thread_state_change.h"
#include "gc/collector/ipc_mark_sweep.h"
#include "gc/space/space.h"
#include "gc/service/global_allocator.h"

namespace art {
namespace gc {
namespace gcservice {


class ServerCollector {
 public:
  ServerCollector(space::GCSrvSharableHeapData* meta_alloc);

  void Run(void);
  space::GCSrvSharableHeapData* heap_data_;
  Mutex* run_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> run_cond_ GUARDED_BY(shutdown_mu_);


  volatile int status_;

  InterProcessMutex* phase_mu_;
  InterProcessConditionVariable* phase_cond_;


  Thread*   thread_;
  pthread_t pthread_;

  void SignalCollector(void);
  void WaitForRequest(void);
  void WaitForGCTask(void);
  void ExecuteGC(void);



  static void* RunCollectorDaemon(void*);
  static ServerCollector* CreateServerCollector(void* args);

};//class ServerCollector


}
}
}

#endif /* ART_RUNTIME_GC_SERVICE_SERVER_COLLECTOR_H_ */
