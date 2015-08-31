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
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MProfilerHeap.h"
#include "cutils/system_clock.h"
#include "ipcfs/ipcfs.h"
#include "utils.h"
#include "offsets.h"

/* log information. used to monitor the flow of the profiler.*/
#if ART_GC_PROFILER_VERBOSE
#define SRVC_ILOG  1
#else
#define SRVC_ILOG  0
#endif




namespace art {
namespace gc {






typedef struct GCDaemonMetaData_S {
  SynchronizedLockHead lock_header_;
  volatile int counter_;
  volatile int status_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
} GCDaemonMetaData;


//typedef struct GCDaemonHeader_S {
//  GCDaemonMetaData* meta_data_;
//  InterProcessMutex* mu_;
//  InterProcessConditionVariable* cond_;
//} GCDaemonHeader;

class GCServiceDaemon {
public:
  static GCServiceDaemon* GCServiceD;

//  InterProcessMutex* service_mu_;
//  InterProcessConditionVariable* service_cond_;

  GCServiceDaemon(GCDaemonMetaData* service_header);

  bool isRunning(void);
  bool isNotRunning(void);
  bool isStopped(void);
  bool isShuttingDown(void);
  bool gcserviceMain(Thread*);
  bool createService(Thread*);
  bool initMemoryService(Thread*);
  bool createServiceNoBlock(Thread*);
  void shutdown(void);

//  static GCDaemonHeader* CreateServiceHeader(void);
  static void InitServiceMetaData(GCDaemonMetaData*);
  static void LaunchGCService(void* arg);
  static void* RunDaemon(void* arg);
  static void ShutdownGCService(void);
  static bool IsGCServiceRunning(void);
  static bool IsGCServiceStopped(void);
  static void GCPBlockForServiceReady(GCDaemonMetaData* dMeta);
  static void GCPSignalToLaunchServer(void);
  static void GCPRegisterWithGCService(void);
  static bool GCPMapFileDescriptor(int);

  /******************** setters and getters ************************/
  inline void _Status(GC_SERVICE_STATUS new_status) {
    service_meta_data_->status_ = new_status;
  }

  inline int _Counter() {
    return service_meta_data_->counter_;
  }

  inline int _Status() {
    return service_meta_data_->status_;
  }

  inline InterProcessMutex* _Mu() {
    return service_meta_data_->mu_;
  }

  inline InterProcessConditionVariable* _Cond() {
    return service_meta_data_->cond_;
  }


  /* it assumes that the lock on the service is acquired */
  void registerProcesss(void);
  void initSharedHeapHeader(void);


private:
  GCDaemonMetaData* service_meta_data_;
  Thread*   daemonThread_;
  pthread_t pthread_;

  Mutex* shutdown_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(shutdown_mu_);


  volatile int processed_index_;

#ifdef HAVE_ANDROID_OS
  android::FileMapperService* fileMapperSvc_;
#endif


  void initShutDownSisgnals(void);
};//GCServiceDaemon

}//namespace gc
}//namespace art



#endif /* ART_RUNTIME_GC_PROFILER_GCSERVICE_H_ */
