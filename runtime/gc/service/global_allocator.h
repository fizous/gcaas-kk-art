/*
 * global_allocator.h
 *
 *  Created on: Sep 13, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_
#define ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_

#include <set>
#include <vector>
#include "safe_map.h"
#include "ipcfs/ipcfs.h"
#include "mem_map.h"
#include "globals.h"
#include "utils.h"
#include "runtime.h"
#include "thread_pool.h"
#include "gc/space/space.h"

#if (ART_GC_SERVICE || true)

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
  GCSERVICE_STATUS_SYS_SERVER_CREATED = 16,
  GCSERVICE_STATUS_SHUTTING_DOWN  = 32,
  GCSERVICE_STATUS_STOPPED  = 64
} GC_SERVICE_STATUS;


typedef enum {
  GC_SERVICE_TASK_NOP = 0,
  GC_SERVICE_TASK_REG = 1,
  GC_SERVICE_TASK_CONC = 2,
  GC_SERVICE_TASK_EXPLICIT = 4,
  GC_SERVICE_TASK_TRIM = 8,
  GC_SERVICE_TASK_GC_ALLOC = 16,
  GC_SERVICE_TASK_STATS = 32,
  GC_SERVICE_TASK_MAX_LIMIT = 64
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


typedef enum {
  GC_SERVICE_SHARE_SPACES_ALLOC = 1,
  GC_SERVICE_SHARE_SPACES_ZYGOTE = 2,
  GC_SERVICE_SHARE_SPACES_HEAP_BITMAPS = 4
} GC_SERVICE_SHARE_SPACES;

typedef enum {
  GC_SERVICE_HANDLE_TRIM_DISALLOWED = 0,
  GC_SERVICE_HANDLE_TRIM_ALLOWED = 1
} GC_SERVICE_HANDLE_TRIM;

typedef enum {
  GC_SERVICE_HANDLE_SYS_SERVER_DISALLOWED = 0,
  GC_SERVICE_HANDLE_SYS_SERVER_ALLOWED = 1
} GC_SERVICE_HANDLE_SYS_SERVER;

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

typedef struct GCSrvcPhysicalState_S {
  long mem_total;
  long mem_free;
}  __attribute__((aligned(4))) GCSrvcPhysicalState;

typedef struct GCServiceHeader_S {
  SynchronizedLockHead lock_;
  volatile int counter_;
  volatile int status_;
  volatile int zygote_creation_busy_;
  InterProcessMutex* mu_;
  InterProcessConditionVariable* cond_;
  GCSrvcPhysicalState global_state_;
  pid_t service_pid_;
//  GC_SERVICE_STATUS service_status_;
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

typedef struct GCSrvc_Options_S {
  std::string gcservc_conf_path_;
  std::string gcservc_apps_list_path_;
  double fgd_growth_mutiplier_;
  /* control strategies regarding trimming */
  int trim_conf_;
  /* control strategies to share zygote space */
  int share_zygote_space_;
  /* control strategies to fwd GC allocatoion to GCDaemon */
  int fwd_gc_alloc_;
  /* page capacity of the global allocator*/
  int page_capacity_;
  /* should we handle system server by the GCService? */
  int handle_system_server_;
} GCSrvc_Options;

class GCServiceGlobalAllocator {
 public:
  //static const int kGCServiceFWDAllocationGC = GC_SERVICE_HANDLE_ALLOC_DAEMON;
  //static const int KGCServiceShareZygoteSpace = 3;
  static GCServiceGlobalAllocator* CreateServiceAllocator(void);
  static space::GCSrvSharableDlMallocSpace* GCSrvcAllocateSharableSpace(int* index_p);
  static bool ShouldForkService(void);
  static void BlockOnGCProcessCreation(pid_t);
  static void BlockOnGCZygoteCreation(void);
  static bool ShouldNotifyForZygoteForkRelease(void);
  void UpdateForkService(pid_t);
  void BlockOnGCProcessCreation(void);
  static int GetTrimConfig(void);
  bool isTrimHandlingEnabled(void) const {
    return (srvc_options_.trim_conf_ == GC_SERVICE_HANDLE_TRIM_ALLOWED);
  }
  static GCServiceHeader* GetServiceHeader(void);
  static GCSrvcClientHandShake* GetServiceHandShaker(void);
  static void GCSrvcNotifySystemServer();
  static bool GCSrvcIsSharingSpacesEnabled();
  static bool GCSrvcOption(const std::string&, GCSrvc_Options*);
  static bool ShouldRegisterApp(const char* se_name_c_str);
  void InitGCSrvcOptions(GCSrvc_Options* opts_addr);

  //static int GCPAllowSharedMemMaps;

  GCSrvc_Options srvc_options_;

  GCSrvcClientHandShake* handShake_;
  static GCServiceGlobalAllocator* allocator_instant_;



  bool fwdGCAllocation() const {
    return (srvc_options_.fwd_gc_alloc_ == GC_SERVICE_HANDLE_ALLOC_DAEMON);
  }

  bool shareZygoteSpace() const {
    return ((srvc_options_.share_zygote_space_ & GC_SERVICE_SHARE_SPACES_ZYGOTE) > 0);
  }

  bool shareHeapBitmapsSpace() const {
    return ((srvc_options_.share_zygote_space_ & GC_SERVICE_SHARE_SPACES_HEAP_BITMAPS) > 0);
  }

 private:
  //static const int   kGCServicePageCapacity = 64;

  GCSrvcGlobalRegionHeader* region_header_;
  std::vector<std::string> app_list_;


  // constructor
  GCServiceGlobalAllocator(/*int pages*/);
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

  void SignalCollector(GC_SERVICE_TASK req_type);
  int WaitForRequest(void);
  void WaitForGCTask(void);
  void ExecuteTrim(void);
  void ExecuteGC(GC_SERVICE_TASK);
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
  volatile int trims_count_;

  collector::IPCServerMarkerSweep* ipc_msweep_;


};//class ServerCollector





class GCSrvceAgent {
 public:
  GCSrvceAgent(android::MappedPairProcessFD*);
  ServerCollector* collector_;
  GCServiceClientRecord binding_;
  volatile int process_id_;
 private:

};//class GCSrvceAgent


class GCSrvcMemInfoOOM {
 public:

  static long total_ram_;
  static long free_ram_[];

  const int oom_adj_;
  const char * oom_label_;

  int parse_status_;
  long aggregate_memory_;
  std::vector<GCSrvceAgent*> agents_list_;

  GCSrvcMemInfoOOM(int, const char *);
  int parseString(char* line);
  void resetMemInfo(void);

  static int readTotalMemory(char* line);
  static int readFreeMemory(char* line);
  static int parseMemInfo(const char* file_path);

  static int parseOOMHeaderString(char* line, char* label, long* mem_size);
  static int parseOOMRecString(char* line, long* mem_size, int* pid);


  static GCSrvcMemInfoOOM mem_info_oom_list_[];
};//GCSrvcMemInfoOOM



typedef SafeMap<volatile int32_t, GCSrvceAgent*> ClientAgentsMap;

class GCServiceDaemon {
  Thread*   thread_;
  pthread_t pthread_;

  Mutex* shutdown_mu_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(shutdown_mu_);
  int processed_index_;
  int mem_info_fd_;

  // The last time a heap trim occurred.
  uint64_t last_global_update_time_ns_;

  //std::vector<android::MappedPairProcessFD*> registered_apps_;

  GCServiceDaemon(GCServiceProcess*);
  static void* RunDaemon(void*);
  void mainLoop(void);
  void initShutDownSignals(void);

public:
//  static GCServiceDaemon* gcdaemon_inst_;
  //GCServiceProcess* process_;
  //std::vector<GCSrvceAgent*> client_agents_;
  ClientAgentsMap agents_map_;


  UniquePtr<ThreadPool> thread_pool_;
  static GCServiceDaemon* CreateServiceDaemon(GCServiceProcess*);
  bool waitShutDownSignals(void);
  GCSrvceAgent* GetAgentByPid(int pid);
  void UpdateGlobalState(void);
  void UpdateGlobalProcessStates(void);


  void SetMemInfoDumpFile();

  static const char * meminfo_args_[];

};//class GCServiceDaemon





class GCServiceProcess {
public:
  static GCServiceProcess* InitGCServiceProcess(GCServiceHeader*, GCSrvcClientHandShake*, int);
  static void LaunchGCServiceProcess(void);
  GCServiceHeader* service_meta_;
  GCSrvcClientHandShake* handShake_;
  GCServiceDaemon* daemon_;
  static GCServiceProcess* process_;
  byte* import_address_;
  int enable_trimming_;
  android::FileMapperService* fileMapperSvc_;
private:


  bool initSvcFD(void);
  GCServiceProcess(GCServiceHeader*, GCSrvcClientHandShake*,
                   int enable_trim);
  void SetGCDaemon(void);






  Thread*   thread_;
  bool srvcReady_;
};//class GCServiceProcess


}//namespace gcservice
}//namespace gc
}//namespace art

#endif //ART_GC_SERVICE

#endif /* ART_RUNTIME_GC_SERVICE_GLOBAL_ALLOCATOR_H_ */
