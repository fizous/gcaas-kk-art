/*
 * common.h
 *
 *  Created on: Aug 30, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_GCSERVICE_COMMON_H_
#define ART_RUNTIME_GC_GCSERVICE_COMMON_H_

#include "thread.h"
#include "ipcfs/ipcfs.h"

/* log information. used to monitor the flow of the profiler.*/
#if ART_GC_PROFILER_VERBOSE
#define SRVC_ILOG  1
#else
#define SRVC_ILOG  0
#endif


#define GCSERV_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << " SvcI: "
#define GCSERV_PROC_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << " =SvcProc= "
#define GCSERV_ZYGOTE_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << "-Zygote- "
#define GCSERV_CLIENT_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << " == CLIENT== "
#define GCSERV_DAEM_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << " ==DAEM== "
#define GCSERV_ALLOC_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << "==SERV_ALLOC== "

#if ART_GC_PROFILER_SERVICE
  #define GCP_REGISTER_PROC_FOR_GCSERVICE(runtime)                      \
    runtime->GCPRegisterWithGCService()
  #define GCP_INIT_GC_SERVICE_HEADER                                    \
    GCMMP_VLOG(INFO) << "GCService: initializing service header";       \
    gcservice::GCService::InitService();                                               \
    GCMMP_VLOG(INFO) << "GCService: Done initiaqlizing service header"
  #define GCP_FORK_GCSERVICE                                            \
    GCPForkGCService();
  #define GCP_SIGNAL_SERVER_READY(runtime)                              \
    runtime->GCPSignalGCServerReady();
  #define GCP_SERVICE_EXPLICIT_FILTER(gcpType) GCService::FilterCollectionType(gcpType)
#else
  #define GCP_FORK_GCSERVICE                                    ((void) 0)
  #define GCP_REGISTER_PROC_FOR_GCSERVICE(runtime)              ((void) 0)
  #define GCP_INIT_GC_SERVICE_HEADER                            ((void) 0)
  #define GCP_SIGNAL_SERVER_READY                               ((void) 0)
  #define GCP_SIGNAL_SERVER_READY(runtime)                      ((void) 0)
  #define GCP_SERVICE_EXPLICIT_FILTER(gcpType)                  (gcpType)
#endif//ART_GC_PROFILER_SERVICE

namespace art {
namespace gcservice {

typedef enum {
  GCSERVICE_STATUS_NONE = 0,
  GCSERVICE_STATUS_WAITINGSERVER = 1,
  GCSERVICE_STATUS_SERVER_INITIALIZED = 2,
  GCSERVICE_STATUS_STARTING = 4,
  GCSERVICE_STATUS_RUNNING  = 8,
  GCSERVICE_STATUS_SHUTTING_DOWN  = 16,
  GCSERVICE_STATUS_STOPPED  = 32
} GC_SERVICE_STATUS;

typedef struct GCServiceMetaData_S {
  SynchronizedLockHead lock_header_;
  volatile int counter_;
  volatile int status_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
} GCServiceMetaData;

}//namespace gcservice
}//namespace art

#endif /* ART_RUNTIME_GC_GCSERVICE_COMMON_H_ */
