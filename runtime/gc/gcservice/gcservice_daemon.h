/*
 * gcservice_daemon.h
 *
 *  Created on: Aug 30, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_GCSERVICE_DAEMON_H_
#define ART_RUNTIME_GC_GCSERVICE_GCSERVICE_DAEMON_H_

#include "thread.h"
#include "gc/gcservice/common.h"
#include "gc/gcservice/gcservice.h"

namespace art {

namespace gcservice {

class GCService;

class GCServiceDaemon {
  Thread*   thread_;
  pthread_t pthread_;

  Mutex* shutdown_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(shutdown_mu_);

  GCServiceDaemon(GCServiceProcess*);
  static void* RunDaemon(void*);
  void mainLoop(void);
  void initShutDownSignals(void);
public:
  GCServiceProcess* process_;
  static GCServiceDaemon* CreateServiceDaemon(GCServiceProcess*);
};//class GCServiceDaemon



class GCServiceProcess {
public:
  static void InitGCServiceProcess(GCService*);
  GCService* service_;
  GCServiceDaemon* daemon_;
private:


  bool initSvcFD(void);
  GCServiceProcess(GCService*);
  static GCServiceProcess* process_;

  android::FileMapperService* fileMapperSvc_;


  Thread*   thread_;
  bool srvcReady_;
};//class GCServiceProcess

}//namespace gcservice
}//namespace art
#endif /* ART_RUNTIME_GC_GCSERVICE_GCSERVICE_DAEMON_H_ */
