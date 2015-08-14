/*
 * GCService.h
 *
 *  Created on: Aug 11, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_PROFILER_GCSERVICE_H_
#define ART_RUNTIME_GC_PROFILER_GCSERVICE_H_

#include <string>
#include <list>
#include "locks.h"
#include "base/mutex.h"
#include "base/unix_file/fd_file.h"
#include "os.h"
#include "base/logging.h"
#include "thread_state.h"
#include "cutils/SharedProcessMutex.h"
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MProfilerHeap.h"
#include "cutils/system_clock.h"
#include "utils.h"
#include "offsets.h"

/* log information. used to monitor the flow of the profiler.*/
#define GCSERV_VLOG(severity) if (ART_GC_PROFILER_VERBOSE) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream() << "==GC_SERVICE== "


namespace art {
namespace mprofiler {


typedef enum {
  GCSERVICE_STATUS_NONE = 0,
  GCSERVICE_STATUS_STARTING = 1,
  GCSERVICE_STATUS_RUNNING  = 2,
  GCSERVICE_STATUS_SHUTTING_DOWN  = 4,
  GCSERVICE_STATUS_STOPPED  = 8
} GC_SERVICE_STATUS;



typedef struct GCDaemonMetaData_S {
  SynchronizedLockHead lock_header_;
  volatile int counter_;
  volatile int status_;
} GCDaemonMetaData;


typedef struct GCDaemonHeader_S {
  GCDaemonMetaData* meta_data_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
} GCDaemonHeader;

class GCServiceDaemon {
public:
  static GCServiceDaemon* GCServiceD;

//  InterProcessMutex* service_mu_;
//  InterProcessConditionVariable* service_cond_;

  GCServiceDaemon(GCDaemonHeader* service_header);

  bool isRunning(void);
  bool isStopped(void);
  bool isShuttingDown(void);
  bool gcserviceMain(Thread*);
  void shutdown(void);

  static GCDaemonHeader* CreateServiceHeader(void);
  static void LaunchGCService(void* arg);
  static void* RunDaemon(void* arg);
  static void ShutdownGCService(void);
  static bool IsGCServiceRunning(void);
  static bool IsGCServiceStopped(void);
  static void GCPBlockForServiceReady(GCDaemonHeader* dHeader);
  static void GCPRegisterGCService(void);

  /******************** setters and getters ************************/
  inline void _Status(GC_SERVICE_STATUS new_status) {
    service_header_->meta_data_->status_ = new_status;
  }

  inline int _Counter() {
    return service_header_->meta_data_->counter_;
  }

  inline int _Status() {
    return service_header_->meta_data_->status_;
  }

  inline InterProcessMutex* _Mu() {
    return service_header_->mu_;
  }

  inline InterProcessConditionVariable* _Cond() {
    return service_header_->cond_;
  }



private:
  GCDaemonHeader* service_header_;
  Thread*   daemonThread_;
  pthread_t pthread_;

  Mutex* shutdown_mu DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(shutdown_mu);


  void initShutDownSignals(void);
};//GCServiceDaemon

}//namespace mprofiler
}//namespace art



#endif /* ART_RUNTIME_GC_PROFILER_GCSERVICE_H_ */
