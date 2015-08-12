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

namespace art {
namespace mprofiler {

typedef enum {
  GCSERVICE_STARTING = 0,
  GCSERVICE_RUNNING = 1,
  GCSERVICE_STOPPED = 2
}GCMMP_GCSERVICE_STATUS;

class GCServiceDaemon {
private:
  GCMMP_GCSERVICE_STATUS daemonStatus_;
  pthread_t pthread_;
  Thread* daemonThread_;
public:
  static GCServiceDaemon* GCServiceD_;
  android::SharedProcessMutex* global_lock_;
  void shutdownGCService(void);


  GCServiceDaemon(VMProfiler*);
  static void LaunchGCService(void* arg);
  static void* RunDaemon(void* arg);

  static void ShutdownGCService(void);
  static bool IsGCServiceRunning(void);
  static bool IsGCServiceStopped(void);
};//GCServiceDaemon
}//namespace mprofiler
}//namespace art



#endif /* ART_RUNTIME_GC_PROFILER_GCSERVICE_H_ */
