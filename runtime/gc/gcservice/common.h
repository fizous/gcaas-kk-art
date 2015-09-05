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
#define GCSERV_IMMUNE_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << " #IMMUNE#: "
#define GCSERV_SPACE_ILOG if (SRVC_ILOG) ::art::LogMessage(__FILE__, __LINE__, INFO, -1).stream() << " #SPACE#: "

#if ART_GC_PROFILER_SERVICE
  #define GCP_REGISTER_PROC_FOR_GCSERVICE(runtime)                      \
    runtime->GCPRegisterWithGCService()
  #define GCP_INIT_GC_SERVICE_HEADER                                    \
    Runtime* _runtime = Runtime::Current();                             \
    if(gcservice::GCService::InitService()) {                           \
                                                                 \
    } else {                                                            \
      _runtime->GCPForkGCService();                                       \
      GCMMP_VLOG(INFO) << "GCService: initialized service header";        \
    }

  #define GCP_SIGNAL_SERVER_READY(runtime)                              \
    runtime->GCPSignalGCServerReady();
  #define GCP_SERVICE_EXPLICIT_FILTER(gcpType) gcservice::GCService::FilterCollectionType(gcpType)
  #define GCP_SERVICE_ZYGOTE_RETENTION(policy)  gcservice::GCService::GetZygoteRetentionPolicy(policy)
  #define GCP_SERVICE_SET_ZYGOTE_SPACE(space)                    gcservice::GCService::zygote_space_ = space;
  #define GCP_SERVICE_LOG_IMMUNED(addr)                            gcservice::GCService::LogImmunedObjectMutation(addr)
  #define GCP_SERVICE_LOG_SPACE_IMMUNED(space)                     if(space->IsZygoteSpace())       gcservice::GCService::LogImmunedSpaceAllocation()
  #define GCP_SERVICE_CREAQTE_ALLOC_SPACE(space) space->CreateZygoteSpaceWithSharedAcc("alloc space")
  #define GCP_SERVICE_FINALIZE_INIT   gcservice::GCService::GCPFinalizeHeapAfterInit()
#else
  #define GCP_REGISTER_PROC_FOR_GCSERVICE(runtime)              ((void) 0)
  #define GCP_INIT_GC_SERVICE_HEADER                            ((void) 0)
  #define GCP_SIGNAL_SERVER_READY                               ((void) 0)
  #define GCP_SIGNAL_SERVER_READY(runtime)                      ((void) 0)
  #define GCP_SERVICE_EXPLICIT_FILTER(gcpType)                  (gcpType)
  #define GCP_SERVICE_ZYGOTE_RETENTION(policy)                    policy
  #define GCP_SERVICE_SET_ZYGOTE_SPACE(space)                    ((void) 0)
  #define GCP_SERVICE_LOG_IMMUNED(addr)                           ((void)0)
  #define GCP_SERVICE_LOG_SPACE_IMMUNED(space)                     ((void)0)
  #define GCP_SERVICE_CREAQTE_ALLOC_SPACE(space) space->CreateZygoteSpace("alloc space")
  #define GCP_SERVICE_FINALIZE_INIT                     ((void)0)
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




typedef struct BitMapMemberMetaData_S {
  // Backing storage for bitmap.
  BaseMapMem* mem_map_;

  // This bitmap itself, word sized for efficiency in scanning.
  word* const bitmap_begin_;

  // Size of this bitmap.
  size_t bitmap_size_;

  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  const uintptr_t heap_begin_;
} BitMapMemberMetaData;

typedef struct SharedSpaceBitmapMeta_S {
  /* memory pointer to the bitmap data*/
  SharedMemMapMeta data_;
  // The base address of the heap, which corresponds to the word containing the first bit in the
  // bitmap.
  BitMapMemberMetaData bitmap_fields_;
} SharedSpaceBitmapMeta;

typedef struct SharedSpaceMeta_S {
  SharedMemMapMeta mem_meta_;
  byte* biased_begin_;
  byte* begin_;
  size_t offset_;
  /* data related to space bitmap */
  SharedSpaceBitmapMeta bitmap_meta_;
} SharedSpaceMeta;

typedef struct SharedCardTableMeta_S {
  SharedMemMapMeta mem_meta_;
  byte* biased_begin_;
  byte* begin_;
  size_t offset_;
} SharedCardTableMeta;

typedef struct SharedHeapMetada_S {
  SynchronizedLockHead lock_header_;
  pid_t pid_;
  /* data related to continuous space */
  SharedCardTableMeta card_table_meta_;

  SharedSpaceMeta alloc_space_meta_;

  gcservice::GC_SERVICE_STATUS vm_status_;
  /* used to synchronize on conc requests*/
  SynchronizedLockHead gc_conc_requests;
} SharedHeapMetada;

}//namespace art

#endif /* ART_RUNTIME_GC_GCSERVICE_COMMON_H_ */
