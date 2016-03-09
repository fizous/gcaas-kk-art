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
#include "thread_pool.h"
#include "gc/space/space.h"


#if ART_GC_SERVICE

#define GC_SERVICE_BUFFER_REQ_CAP   128


#define GCSERVICE_ALLOC_VLOG_ON 0
#define GCSERVICE_ALLOC_VLOG(severity)  if (GCSERVICE_ALLOC_VLOG_ON) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream()

namespace art {
namespace gc {

namespace collector {
 class IPCServerMarkerSweep;
}

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


typedef enum {
  GC_SERVICE_TASK_NOP = 0,
  GC_SERVICE_TASK_REG,
  GC_SERVICE_TASK_CONC,
  GC_SERVICE_TASK_EXPLICIT,
  GC_SERVICE_TASK_TRIM,
  GC_SERVICE_TASK_GC_ALLOC,
  GC_SERVICE_TASK_STATS,
  GC_SERVICE_TASK_MAX_LIMIT
} GC_SERVICE_TASK;

typedef enum {
  GC_SERVICE_REQ_NONE = 0,
  GC_SERVICE_REQ_NEW,
  GC_SERVICE_REQ_STARTED,
  GC_SERVICE_REQ_COMPLETE,
  GC_SERVICE_REQ__MAX_LIMIT
} GC_SERVICE_REQ_STATUS;


typedef enum {
  GC_SERVICE_HANDLE_ALLOC_NONE = 0,
  GC_SERVICE_HANDLE_ALLOC_MUT,
  GC_SERVICE_HANDLE_ALLOC_DAEMON
} GC_SERVICE_HANDLE_ALLOC_GC;


//typedef struct GCServiceClientHandShake_S {
//  SynchronizedLockHead lock_;
//  InterProcessMutex* mu_;
//  InterProcessConditionVariable* cond_;
//  android::FileMapperParameters process_mappers_[IPC_PROCESS_MAPPER_CAPACITY];
//
//  volatile int available_;
//  volatile int tail_;
//  volatile int queued_;
//  volatile int head_;
//} __attribute__((aligned(8))) GCServiceClientHandShake;

typedef struct GCServiceConcReq_S {
  volatile int pid_;
  volatile int req_type_;
  volatile int status_;
  volatile uintptr_t data_addr_;
} __attribute__((aligned(8))) GCServiceReq;

typedef struct GCServiceRequestsBuffer_S {
  SynchronizedLockHead lock_;
  GCServiceReq entries_[GC_SERVICE_BUFFER_REQ_CAP];
  android::FileMapperParameters process_mappers_[IPC_PROCESS_MAPPER_CAPACITY];

  volatile int head_;
  volatile int tail_;
  volatile int queued_;
  volatile int available_;


  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;

  volatile int mapper_head_;
  volatile int mapper_tail_;

} __attribute__((aligned(8))) GCServiceRequestsBuffer;


typedef struct GCServiceHeader_S {
  SynchronizedLockHead lock_;
  volatile int counter_;
  volatile int status_;
  volatile int zygote_creation_busy_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
  pid_t service_pid_;
  GC_SERVICE_STATUS service_status_;
} __attribute__((aligned(8))) GCServiceHeader;



typedef struct GCSrvcGlobalRegionHeader_S {
  // This bitmap itself, word sized for efficiency in scanning.
  AShmemMap ashmem_meta_;
  GCServiceRequestsBuffer gc_handshake_;
  GCServiceHeader service_header_;
  byte* current_addr_;
}  __attribute__((aligned(8))) GCSrvcGlobalRegionHeader;





class GCSrvcClientHandShake {
 public:
  static const int KGCRequestBufferCapacity = GC_SERVICE_BUFFER_REQ_CAP;
  static const int KProcessMapperCapacity   = IPC_PROCESS_MAPPER_CAPACITY;
  GCSrvcClientHandShake(GCServiceRequestsBuffer*);
  android::FileMapperParameters* GetMapperRecord(void* params);
  void ProcessQueuedMapper(android::MappedPairProcessFD* entry);
  void ReqConcCollection(void*);
  void ReqExplicitCollection(void*);
  void ReqRegistration(void*);
  void ReqHeapTrim(void);
  void ReqAllocationGC(void);
  void ListenToRequests(void*);
  void ProcessGCRequest(void* args);
  //GCServiceClientHandShake* mem_data_;
  GCServiceRequestsBuffer* gcservice_data_;
 private:
  void Init();
  void ResetProcessMap(android::FileMapperParameters*);
  void ResetRequestEntry(GCServiceReq* entry);

}; //class GCSrvcClientHandShake



class GCServiceGlobalAllocator {
 public:
  static const int kGCServiceFWDAllocationGC = GC_SERVICE_HANDLE_ALLOC_DAEMON;
  static const int KGCServiceShareZygoteSpace = 3;
  static GCServiceGlobalAllocator* CreateServiceAllocator(void);
  static space::GCSrvSharableDlMallocSpace* GCSrvcAllocateSharableSpace(int* index_p);
  static bool ShouldForkService(void);
  static void BlockOnGCProcessCreation(pid_t);
  static void BlockOnGCZygoteCreation(void);
  static bool ShouldNotifyForZygoteForkRelease(void);
  void UpdateForkService(pid_t);
  void BlockOnGCProcessCreation(void);
  static GCServiceHeader* GetServiceHeader(void);
  static GCSrvcClientHandShake* GetServiceHandShaker(void);
  static int GCPAllowSharedMemMaps;

  GCSrvcClientHandShake* handShake_;
  static GCServiceGlobalAllocator* allocator_instant_;
 private:
  static const int   kGCServicePageCapacity = 64;

  GCSrvcGlobalRegionHeader* region_header_;


  // constructor
  GCServiceGlobalAllocator(int pages);
  byte* AllocateSharableSpace(int* index_p);
  void initServiceHeader(void);
  void RaiseSemaphore();
  void ResetSemaphore();

  byte* allocate(size_t num_bytes) {
    byte* _addr = region_header_->current_addr_;
    size_t allocated_bytes = RoundUp(num_bytes, kObjectAlignment);
    region_header_->current_addr_ +=  allocated_bytes;
    return _addr;
  }
};//class ServiceAllocator


class GCServiceProcess;



typedef struct GCServiceClientRecord_S {
  gc::space::GCSrvSharableDlMallocSpace* sharable_space_;
  android::MappedPairProcessFD* pair_mapps_;
//  mirror::Class* java_lang_Class_cached_;
}  __attribute__((aligned(8))) GCServiceClientRecord;



class ServerCollector {
 public:
  ServerCollector(GCServiceClientRecord* client_rec,
      space::GCSrvSharableHeapData* meta_alloc);

  void Run(void);
  space::GCSrvSharableHeapData* heap_data_;
  Mutex run_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  ConditionVariable run_cond_ GUARDED_BY(run_mu_);
  Thread*   thread_ GUARDED_BY(run_mu_);
  pthread_t pthread_ GUARDED_BY(run_mu_);

  volatile int status_ GUARDED_BY(run_mu_);


  InterProcessMutex* phase_mu_;
  InterProcessConditionVariable* phase_cond_;

  InterProcessMutex* conc_req_cond_mu_;
  InterProcessConditionVariable* conc_req_cond_;


  InterProcessMutex* gc_complete_mu_;
  InterProcessConditionVariable* gc_complete_cond_;

  void SignalCollector(bool is_explicit = false);
  int WaitForRequest(void);
  void WaitForGCTask(void);
  void ExecuteGC(int);
  void UpdateCollectorAddress(Thread* self,
      space::GCSrvSharableCollectorData* address);
  void BlockOnCollectorAddress(Thread* self);
  void FinalizeGC(Thread* self);
  void ConcMarkPhaseGC(void);
  void WaitForConcMarkPhaseGC(void);
  void WaitForFinishPhaseGC(void);
  void PostFinishPhaseGC(void);

  static void* RunCollectorDaemon(void*);
  static ServerCollector* CreateServerCollector(void* args);

  ThreadPool* gc_workers_pool_;
  /*********** task queues ************/
  Mutex shake_hand_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  ConditionVariable shake_hand_cond_ GUARDED_BY(shake_hand_mu_);
  space::GCSrvSharableCollectorData* volatile curr_collector_addr_;


  volatile int cycles_count_;

  collector::IPCServerMarkerSweep* ipc_msweep_;


};//class ServerCollector


class GCSrvceAgent {
 public:
  GCSrvceAgent(android::MappedPairProcessFD*);
  ServerCollector* collector_;
  GCServiceClientRecord binding_;
 private:

};//class GCSrvceAgent


class GCServiceDaemon {
  Thread*   thread_;
  pthread_t pthread_;

  Mutex* shutdown_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(shutdown_mu_);
  int processed_index_;

  //std::vector<android::MappedPairProcessFD*> registered_apps_;

  GCServiceDaemon(GCServiceProcess*);
  static void* RunDaemon(void*);
  void mainLoop(void);
  void initShutDownSignals(void);

public:
  static GCServiceDaemon* gcdaemon_inst_;
  //GCServiceProcess* process_;
  std::vector<GCSrvceAgent*> client_agents_;
  UniquePtr<ThreadPool> thread_pool_;
  static GCServiceDaemon* CreateServiceDaemon(GCServiceProcess*);
  bool waitShutDownSignals(void);
  GCSrvceAgent* GetAgentByPid(int pid);
};//class GCServiceDaemon


class GCServiceProcess {
public:
  static GCServiceProcess* InitGCServiceProcess(GCServiceHeader*, GCSrvcClientHandShake*);
  static void LaunchGCServiceProcess(void);
  GCServiceHeader* service_meta_;
  GCSrvcClientHandShake* handShake_;
  GCServiceDaemon* daemon_;
  static GCServiceProcess* process_;
  byte* import_address_;

private:


  bool initSvcFD(void);
  GCServiceProcess(GCServiceHeader*, GCSrvcClientHandShake*);
  void SetGCDaemon(void);


  android::FileMapperService* fileMapperSvc_;


  Thread*   thread_;
  bool srvcReady_;
};//class GCServiceProcess


}//namespace gcservice
}//namespace gc
}//namespace art

#endif //ART_GC_SERVICE

#endif /* ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_ */
