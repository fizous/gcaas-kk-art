/*
 * global_allocator.h
 *
 *  Created on: Sep 13, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_
#define ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_

#include "ipcfs/ipcfs.h"
#include "mem_map.h"
#include "globals.h"
#include "utils.h"
#include "runtime.h"
#include "gc/space/space.h"

namespace art {
namespace gc {
namespace gcservice{

typedef enum {
  GCSERVICE_STATUS_NONE = 0,
  GCSERVICE_STATUS_WAITINGSERVER = 1,
  GCSERVICE_STATUS_SERVER_INITIALIZED = 2,
  GCSERVICE_STATUS_STARTING = 4,
  GCSERVICE_STATUS_RUNNING  = 8,
  GCSERVICE_STATUS_SHUTTING_DOWN  = 16,
  GCSERVICE_STATUS_STOPPED  = 32
} GC_SERVICE_STATUS;




typedef struct GCServiceClientHandShake_S {
  SynchronizedLockHead lock_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
  android::FileMapperParameters process_mappers_[IPC_PROCESS_MAPPER_CAPACITY];

  volatile int available_;
  volatile int tail_;
  volatile int queued_;
  volatile int head_;
} __attribute__((aligned(8))) GCServiceClientHandShake;




typedef struct GCServiceHeader_S {
  SynchronizedLockHead lock_;
  volatile int counter_;
  volatile int status_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
  pid_t service_pid_;
  GC_SERVICE_STATUS service_status_;
} __attribute__((aligned(8))) GCServiceHeader;

typedef struct GCSrvcGlobalRegionHeader_S {
  // This bitmap itself, word sized for efficiency in scanning.
  AShmemMap ashmem_meta_;
  GCServiceClientHandShake gc_handshake_;
  byte* current_addr_;
  GCServiceHeader service_header_;
}  __attribute__((aligned(8))) GCSrvcGlobalRegionHeader;


class GCSrvcClientHandShake {
 public:
  static const int KProcessMapperCapacity = IPC_PROCESS_MAPPER_CAPACITY;
  GCSrvcClientHandShake(GCServiceClientHandShake*);
  android::FileMapperParameters* GetMapperRecord(int index, int* fdArr);
  void ProcessQueuedMapper(android::MappedPairProcessFD* entry);
  GCServiceClientHandShake* mem_data_;
 private:
  void Init();
  void ResetProcessMap(android::FileMapperParameters*);

}; //class GCSrvcClientHandShake



class GCServiceGlobalAllocator {
 public:
  static GCServiceGlobalAllocator* CreateServiceAllocator(void);
  static space::GCSrvSharableDlMallocSpace* GCSrvcAllocateSharableSpace(int* index_p);
  static bool ShouldForkService(void);
  static void BlockOnGCProcessCreation(pid_t);
  void UpdateForkService(pid_t);
  void BlockOnGCProcessCreation(void);
  static GCServiceHeader* GetServiceHeader(void);
  static GCSrvcClientHandShake* GetServiceHandShaker(void);
  static int GCPAllowSharedMemMaps;

  GCSrvcClientHandShake* handShake_;
  static GCServiceGlobalAllocator* allocator_instant_;
 private:
  static const int   kGCServicePageCapacity = 16;
  GCSrvcGlobalRegionHeader* region_header_;


  // constructor
  GCServiceGlobalAllocator(int pages);
  byte* AllocateSharableSpace(int* index_p);
  void initServiceHeader(void);

  byte* allocate(size_t num_bytes) {
    byte* _addr = region_header_->current_addr_;
    size_t allocated_bytes = RoundUp(num_bytes, kObjectAlignment);
    region_header_->current_addr_ +=  allocated_bytes;
    return _addr;
  }
};//class ServiceAllocator


class GCServiceProcess;

class GCServiceDaemon {
  Thread*   thread_;
  pthread_t pthread_;

  Mutex* shutdown_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(shutdown_mu_);
  int processed_index_;

  gc::accounting::StructuredMapPairProcStack registered_apps_;

  GCServiceDaemon(GCServiceProcess*);
  static void* RunDaemon(void*);
  void mainLoop(void);
  void initShutDownSignals(void);

public:
  GCServiceProcess* process_;
  static GCServiceDaemon* CreateServiceDaemon(GCServiceProcess*);
  bool waitShutDownSignals(void);
};//class GCServiceDaemon


class GCServiceProcess {
public:
  static GCServiceProcess* InitGCServiceProcess(GCServiceHeader*, GCSrvcClientHandShake*);
  static void LaunchGCServiceProcess(void);
  GCServiceHeader* service_meta_;
  GCSrvcClientHandShake* handShake_;
  GCServiceDaemon* daemon_;
private:


  bool initSvcFD(void);
  GCServiceProcess(GCServiceHeader*, GCSrvcClientHandShake*);
  static GCServiceProcess* process_;

  android::FileMapperService* fileMapperSvc_;


  Thread*   thread_;
  bool srvcReady_;
};//class GCServiceProcess
}//namespace gcservice
}//namespace gc
}//namespace art

#endif /* ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_ */
