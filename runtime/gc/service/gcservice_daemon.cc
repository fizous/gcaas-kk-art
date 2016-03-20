/*
 * gcservice_daemon.cc
 *
 *  Created on: Sep 28, 2015
 *      Author: hussein
 */
#include <string>
#include <cutils/ashmem.h>
#include "gc/service/global_allocator.h"
#include "gc/collector/ipc_server_sweep.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "mem_map.h"


using ::art::gc::space::GCSrvSharableDlMallocSpace;
namespace art {
namespace gc {
namespace gcservice {

GCServiceProcess* GCServiceProcess::process_ = NULL;

GCServiceDaemon* GCServiceDaemon::CreateServiceDaemon(GCServiceProcess* process) {
  return new GCServiceDaemon(process);
}


GCSrvceAgent* GCServiceDaemon::GetAgentByPid(int pid) {
  for (auto& client : client_agents_) {
     if(client->binding_.pair_mapps_->first->process_id_ == pid) {
       return client;
     }
  }
  return NULL;
}

void* GCServiceDaemon::RunDaemon(void* arg) {
  GCServiceDaemon* _daemonObj = reinterpret_cast<GCServiceDaemon*>(arg);
  GCServiceProcess* _processObj = GCServiceProcess::process_;
  IPC_MS_VLOG(INFO) << "-------- Inside GCServiceDaemon::RunDaemon ---------";
  Runtime* runtime = Runtime::Current();
  bool _createThread =  runtime->AttachCurrentThread("GCSvcDaemon", true,
      runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler());

  if(!_createThread) {
    LOG(ERROR) << "-------- could not attach internal GC service Daemon ---------";
    return NULL;
  }
  Thread* self = Thread::Current();


  DCHECK_NE(self->GetState(), kRunnable);
  {
    IPMutexLock interProcMu(self, *_processObj->service_meta_->mu_);
    _daemonObj->thread_ = self;
    _daemonObj->SetMemInfoDumpFile();
    _processObj->service_meta_->status_ = GCSERVICE_STATUS_RUNNING;
    _processObj->service_meta_->cond_->Broadcast(self);
  }

  IPC_MS_VLOG(INFO) << "GCServiceDaemon is entering the main loop: " <<
      _daemonObj->thread_->GetTid();

  while((_processObj->service_meta_->status_ & GCSERVICE_STATUS_RUNNING) > 0) {
    _daemonObj->UpdateGlobalState();
    if(true) {
      _daemonObj->UpdateGlobalProcessStates();
    }
    _daemonObj->mainLoop();
  }

  IPC_MS_VLOG(INFO) << "GCServiceDaemon left the main loop: " <<
      _daemonObj->thread_->GetTid();

  return NULL;
}


GCServiceDaemon::GCServiceDaemon(GCServiceProcess* process) :
     thread_(NULL), processed_index_(0), mem_info_fd_(-1) {
  Thread* self = Thread::Current();
  {
    IPMutexLock interProcMu(self, *process->service_meta_->mu_);
    process->service_meta_->status_ = GCSERVICE_STATUS_STARTING;
//    registered_apps_.reset(accounting::ATOMIC_MAPPED_STACK_T::Create("registered_apps",
//        64, false));
    initShutDownSignals();
    IPC_MS_VLOG(INFO) << "Thread_POOL----" << "resetting thread pool for gcservice daemon";
    thread_pool_.reset(new ThreadPool(4));
    process->service_meta_->cond_->Broadcast(self);
  }

  CHECK_PTHREAD_CALL(pthread_create,
      (&pthread_, NULL,
      &GCServiceDaemon::RunDaemon, this),
      "GCService Daemon thread");

}

void GCServiceDaemon::initShutDownSignals(void) {
  Thread* self = Thread::Current();
  shutdown_mu_ = new Mutex("gcService Shutdown");
  MutexLock mu(self, *shutdown_mu_);
  shutdown_cond_.reset(new ConditionVariable("gcService Shutdown condition variable",
      *shutdown_mu_));
}


bool GCServiceDaemon::waitShutDownSignals(void) {
  Thread* self = Thread::Current();
  MutexLock mu(self, *shutdown_mu_);
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    shutdown_cond_->Wait(self);
  }
  if(GCServiceProcess::process_->service_meta_->status_ == GCSERVICE_STATUS_STOPPED) {
    shutdown_cond_->Broadcast(self);
    return true;
  }
  shutdown_cond_->Broadcast(self);
  return false;
}

const char *GCServiceDaemon::mem_info_args_[] = {"--oom"};
//const char *GCServiceDaemon::mem_info_oom_labels_[] = {
//    "Native",
//    "System", "Persistent", "Foreground",
//    "Visible", "Perceptible",
//    "Heavy Weight", "Backup",
//    "A Services", "Home",
//    "Previous", "B Services", "Cached"
//};


long GCSrvcMemInfoOOM::total_ram_ = 0;
long GCSrvcMemInfoOOM::free_ram_[] = {0, 0, 0, 0};

const GCSrvcMemInfoOOM GCServiceDaemon::mem_info_oom_list_[] = {
    GCSrvcMemInfoOOM(-17, "Native"),
    GCSrvcMemInfoOOM(-16, "System"),
    GCSrvcMemInfoOOM(-12, "Persistent"),
    GCSrvcMemInfoOOM(0, "Foreground"),
    GCSrvcMemInfoOOM(1, "Visible"),
    GCSrvcMemInfoOOM(2, "Perceptible"),
    GCSrvcMemInfoOOM(3, "Backup"),
    GCSrvcMemInfoOOM(4, "Heavy Weight"),
    GCSrvcMemInfoOOM(5, "A Services"),
    GCSrvcMemInfoOOM(6, "Home"),
    GCSrvcMemInfoOOM(7, "Previous"),
    GCSrvcMemInfoOOM(8, "B Services"),
    GCSrvcMemInfoOOM(15, "Cached"),
};



GCSrvcMemInfoOOM::GCSrvcMemInfoOOM(int adj, const char * label) :
    oom_adj_(adj), oom_label_(label), parse_status_(0), aggregate_memory_(0) {

}

void GCSrvcMemInfoOOM::resetMemInfo() {
  parse_status_ = 0;
  aggregate_memory_ = 0;
}



int GCSrvcMemInfoOOM::parseOOMHeaderString(char* line, char* label,
                                           long* mem_size) {

  int result = sscanf(line, " %ld kB: %s",  mem_size, label);

  if(result == 2)
    return 1;
  return 0;
}

int GCSrvcMemInfoOOM::parseString(char* line) {
  if(parse_status_ == 0) {
    char _label[128];
    long _memory_size;

    int result = sscanf(line, " %ld kB: %s",  &_memory_size, _label);
    if(result == 2) {
      if(strcmp(_label, oom_label_) == 0) {
        parse_status_ = 1;
        aggregate_memory_ = _memory_size;
        LOG(ERROR) << "----- line header ----" << line;
        return 100;
      }
      return 1000;
    }
  }
  if(parse_status_ == 1) {
    int proc_id;
    long proc_mem;
    int result = sscanf(line, " %ld kB: %*s (pid %d%*s",  &proc_mem, &proc_id);
    if(result == 2) {
      LOG(ERROR) << "\t\t proc line : " << line;
      return 101;
    }

    parse_status_ = 2;
    return 1000;
  }
  return 0;
}


int GCSrvcMemInfoOOM::parseMemInfo(const char* file_path) {
  FILE *f;

  char line[256];
  f = fopen(file_path, "r");
  if (!f) {
    LOG(ERROR) << "GCSrvcMemInfoOOM::parseMemInfo...could not open file";
    return -1;// errno;
  }


  int _curr_index = 0;

  for(int i =0; i < 13; i++) {
    GCSrvcMemInfoOOM* mem_info_rec = const_cast<GCSrvcMemInfoOOM*>(&GCServiceDaemon::mem_info_oom_list_[i]);
    mem_info_rec->resetMemInfo();
  }

  while (fgets(line, 256, f)) {
    LOG(ERROR) << "+++ " << line;
    char _label[128];
    long _memory_size;
    if(GCSrvcMemInfoOOM::parseOOMHeaderString(line, _label, &_memory_size) == 1) {
      LOG(ERROR) << "---- " << _curr_index++ << ", "<< line;
    }
#if 0
    while(_curr_index < 13) {
      GCSrvcMemInfoOOM* mem_info_rec = const_cast<GCSrvcMemInfoOOM*>(&GCServiceDaemon::mem_info_oom_list_[_curr_index]);
      char _label[128];
      long _memory_size;
      if(mem_info_rec->parse_status_ == 2) {
        _curr_index++;
        break;
      }
      if(mem_info_rec->parse_status_ == 1) {
        int _res_line = mem_info_rec->parseString(line);
        if(_res_line == 101) {
          break;
        } else if (_res_line == 1000) {
          continue;
        }
      }
      if(GCSrvcMemInfoOOM::parseOOMHeaderString(line, _label, &_memory_size) == 1) {
        //new OOMInfo header;
        while(_curr_index < 13) {
          GCSrvcMemInfoOOM* mem_info_rec_r = const_cast<GCSrvcMemInfoOOM*>(&GCServiceDaemon::mem_info_oom_list_[_curr_index]);
          if(mem_info_rec_r->parse_status_ == 0) {
            int _res_header = mem_info_rec_r->parseString(line);
            if(_res_header == 100) {
              break;
            }
          }
          _curr_index++;
        }
      }
    }
    if(readTotalMemory(line) == 100)
      continue;
    if(readFreeMemory(line) == 100)
      break;
#endif
  }



  fclose(f);

  return 1;

}



int GCSrvcMemInfoOOM::readTotalMemory(char* line) {
  int result = sscanf(line, "Total RAM: %ld kB", &total_ram_);
  if(result == 1) {
    return 100;
  }
  if(result == EOF)
    return EOF;
  return 0;
}

int GCSrvcMemInfoOOM::readFreeMemory(char* line) {
  int _index = 0;
  int result = sscanf(line, "Free RAM: %ld kB (%ld cached pss + %ld cached + %ld free)",
                      &free_ram_[_index], &free_ram_[_index+1], &free_ram_[_index+2],
                      &free_ram_[_index+3]);
  if(result == 4 ) {
    return 100;
  }
  if(result == EOF)
    return EOF;
  return 0;
}




void GCServiceDaemon::UpdateGlobalState(void) {
  FILE *f;

  char line[256];
  f = fopen("/proc/meminfo", "r");
  if (!f) return;// errno;


  GCSrvcPhysicalState* _physical_state =
      &(GCServiceProcess::process_->service_meta_->global_state_);
  while (fgets(line, 256, f)) {
      sscanf(line, "MemTotal: %ld kB", &_physical_state->mem_total);
      sscanf(line, "MemFree: %ld kB", &_physical_state->mem_free);
  }

  fclose(f);

//  LOG(ERROR) << "--- GlobalPhysicalMemory..... totalMemory:"
//      << _physical_state->mem_total << ", freeMemory:" << _physical_state->mem_free;

}
static bool ReadStaticInt(JNIEnvExt* env, jclass clz, const char* name,
                                                              int* out_value) {
  CHECK(out_value != NULL);
  jfieldID field = env->GetStaticFieldID(clz, name, "I");
  if (field == NULL) {
    env->ExceptionClear();
    return false;
  }
  *out_value = env->GetStaticIntField(clz, field);
  return true;
}

void GCServiceDaemon::SetMemInfoDumpFile(void) {
  int fd = open("/data/anr/meminfo.data", O_RDWR | O_CREAT, 0777);
  if (fd == -1) {
    PLOG(ERROR) << "Unable to open stack trace file '" << "/data/anr/meminfo.data" << "'";
    return;
  }
  mem_info_fd_ = fd;


}


void GCServiceDaemon::UpdateGlobalProcessStates(void) {



  LOG(ERROR)<< "--------------------------------------";
  Thread* self = Thread::Current();
  JNIEnvExt* env = self->GetJniEnv();
  // Just attempt to do this the first time.
  jclass clz = env->FindClass("android/app/ActivityManager");
  if (clz == NULL) {
    LOG(WARNING) << "Activity manager class is null";
    return;
  }
  ScopedLocalRef<jclass> activity_manager(env, clz);
  std::vector<const char*> care_about_pauses;
  care_about_pauses.push_back("PROCESS_STATE_TOP");
  care_about_pauses.push_back("PROCESS_STATE_IMPORTANT_BACKGROUND");

  // Process states which care about pause times.
  std::set<int> process_state_cares_about_pause_time_;

  // Attempt to read the constants and classify them as whether or not we care about pause times.
  for (size_t i = 0; i < care_about_pauses.size(); ++i) {
    int process_state = 0;
    if (ReadStaticInt(env, activity_manager.get(), care_about_pauses[i], &process_state)) {
      process_state_cares_about_pause_time_.insert(process_state);
      LOG(ERROR)<< "XXXXXX Adding process state " << process_state
                 << " to set of states which care about pause time";
    }
  }

//  int fd = open("/data/anr/meminfo.data", O_RDWR | O_CREAT, 0777);
//  if (fd == -1) {
//    PLOG(ERROR) << "Unable to open stack trace file '" << "/data/anr/meminfo.data" << "'";
//    return;
//  }

  std::string _meminfo_lines;
  bool mem_info_result =
      GCServiceProcess::process_->fileMapperSvc_->UpdateMemInfo(mem_info_fd_,
                                                                "meminfo",
                                                                mem_info_args_,
                                                                1);

  if(mem_info_result) {
    if(fcntl(mem_info_fd_, F_GETFD) != -1 || errno != EBADF) {
      GCSrvcMemInfoOOM::parseMemInfo("/data/anr/meminfo.data");


//      if (!ReadFileToString("/data/anr/meminfo.data", &_meminfo_lines)) {
//        LOG(ERROR) << "(couldn't read dump of mem_info  \n";
//      } else {
//        LOG(ERROR) << "meminfo_dump------------------------\n" << _meminfo_lines;
//        std::vector<std::string> mem_info_dump;
//        Split(_meminfo_lines, '\n', mem_info_dump);
//      }
    }
  }


//  if(fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
//    close(fd);
//    LOG(ERROR) << " HHHHH synchronizing the fd HHHHH ";
//    if (!ReadFileToString("/data/anr/meminfo.data", &_meminfo_lines)) {
//         LOG(ERROR) << "(couldn't read dump of mem_info  \n";
//       } else {
//         LOG(ERROR) << "meminfo_dump------------------------\n" << _meminfo_lines;
//         std::vector<std::string> mem_info_dump;
//         Split(_meminfo_lines, '\n', mem_info_dump);
//       }
//
//  } else {
//    if (!ReadFileToString("/data/anr/meminfo.data",&_meminfo_lines)) {
//      LOG(ERROR) << "(couldn't read dump of mem_info  \n";
//    } else {
//      LOG(ERROR) << "meminfo_dump------------------------\n" << _meminfo_lines;
//      std::vector<std::string> mem_info_dump;
//      Split(_meminfo_lines, '\n', mem_info_dump);
//    }
//  }

//  if (!ReadFileToString(fd, &_meminfo_lines)) {
//    LOG(ERROR) << "(couldn't read dump of mem_info  \n";
//  } else {
//    LOG(ERROR) << "meminfo_dump------------------------\n" << _meminfo_lines;
//    std::vector<std::string> mem_info_dump;
//    Split(_meminfo_lines, '\n', mem_info_dump);
//  }
//
//  int close_result = close(fd);
//  if(close_result < 0) {
//    LOG(ERROR) << "Closing file......";
//  }
  LOG(ERROR)<< "--------------------------------------";


}


void GCServiceDaemon::mainLoop(void) {
  GCServiceProcess::process_->handShake_->ListenToRequests(this);
//  IPMutexLock interProcMu(thread_, *process_->handShake_->mem_data_->mu_);
//  ScopedThreadStateChange tsc(thread_, kWaitingForGCProcess);
//  {
//    LOG(ERROR) << "waiting for new Process ";
//    process_->handShake_->mem_data_->cond_->Wait(thread_);
//  }
//  LOG(ERROR) << "GCServiceDaemon::mainLoop ====  received signal";
//  if(process_->service_meta_->status_ == GCSERVICE_STATUS_RUNNING) {
//    LOG(ERROR) << "before calling ====  ProcessQueuedMapper";
//
//
//    android::FileMapperParameters* _f_mapper_params_a =
//        reinterpret_cast<android::FileMapperParameters*>(calloc(1,
//            sizeof(android::FileMapperParameters)));
//    android::FileMapperParameters* _f_mapper_params_b =
//        reinterpret_cast<android::FileMapperParameters*>(calloc(1,
//            sizeof(android::FileMapperParameters)));
//
//    android::MappedPairProcessFD* _newEntry =
//        new std::pair<android::FileMapperParameters*,
//        android::FileMapperParameters*>(_f_mapper_params_a, _f_mapper_params_b);
    //registered_apps_.push_back(_newEntry);
    //process_->handShake_->ProcessQueuedMapper(_newEntry);


//
//    client_agents_.push_back(GCSrvceAgent(_newEntry));
//    while(processed_index_ < process_->service_meta_->counter_) {
//      LOG(ERROR) << " processing index registration: " <<
//          processed_index_;
//      processed_index_++;
//    }
//  }

//  process_->handShake_->mem_data_->cond_->Broadcast(thread_);
}


GCSrvceAgent::GCSrvceAgent(android::MappedPairProcessFD* mappedPair) {
  binding_.pair_mapps_ = mappedPair;
  binding_.sharable_space_ =
      reinterpret_cast<GCSrvSharableDlMallocSpace*>(
          mappedPair->first->shared_space_addr_);
//  binding_.java_lang_Class_cached_ =
//      reinterpret_cast<mirror::Class*>(mappedPair->first->java_lang_Class_cached_);
  collector_ = ServerCollector::CreateServerCollector(&binding_);
}


//----------
//----------------------------- GCServiceProcess ------------------------------

void GCServiceProcess::LaunchGCServiceProcess(void) {

  int trim_param = GCServiceGlobalAllocator::allocator_instant_->GetTrimConfig();

  InitGCServiceProcess(
      GCServiceGlobalAllocator::GetServiceHeader(),
      GCServiceGlobalAllocator::GetServiceHandShaker(),
      trim_param);
}


GCServiceProcess* GCServiceProcess::InitGCServiceProcess(GCServiceHeader* meta,
    GCSrvcClientHandShake* handshake, int enable_trim){
  if(GCServiceProcess::process_ == NULL) {
    IPC_MS_VLOG(INFO) << "initializing process";
    GCServiceProcess::process_ = new GCServiceProcess(meta, handshake, enable_trim);
    GCServiceProcess::process_->SetGCDaemon();

  }
  return GCServiceProcess::process_;
}

bool GCServiceProcess::initSvcFD(void) {
  bool returnRes = false;
  IPMutexLock interProcMu(thread_, *service_meta_->mu_);
  if(fileMapperSvc_ == NULL) {
    IPC_MS_VLOG(INFO) << " creating fileMapperSvc_ for first time ";
    fileMapperSvc_ =
        android::FileMapperService::CreateFileMapperSvc();
    returnRes = android::FileMapperService::IsServiceReady();
  } else {
    IPC_MS_VLOG(INFO) << " reconnecting ";
    returnRes = android::FileMapperService::Reconnect();
  }

  if(returnRes) {
    IPC_MS_VLOG(INFO) << " the proc found the service initialized ";
    service_meta_->status_ = GCSERVICE_STATUS_SERVER_INITIALIZED;
  } else {
    IPC_MS_VLOG(INFO) << " the proc found the service not initialized ";
  }
  service_meta_->cond_->Broadcast(thread_);
  return returnRes;
}


GCServiceProcess::GCServiceProcess(GCServiceHeader* meta,
                                  GCSrvcClientHandShake* handShakeMemory,
                                  int enable_trim)  :
    service_meta_(meta),
    handShake_(handShakeMemory),
    enable_trimming_(enable_trim),
    fileMapperSvc_(NULL),
    thread_(NULL), srvcReady_(false){

  thread_ = Thread::Current();
  {
    IPC_MS_VLOG(INFO) << " changing status of service to waiting for server ";
    IPMutexLock interProcMu(thread_, *service_meta_->mu_);
    service_meta_->status_ = GCSERVICE_STATUS_WAITINGSERVER;
    service_meta_->cond_->Broadcast(thread_);
  }
  srvcReady_ = initSvcFD();

//  import_address_ = std::max(Runtime::Current()->GetHeap()->GetMaxAddress(),
//      MemBaseMap::max_covered_address);

}

void GCServiceProcess::SetGCDaemon(void) {
  IPC_MS_VLOG(INFO) << "Import Address ------ " << reinterpret_cast<void*>(import_address_);
  daemon_ = GCServiceDaemon::CreateServiceDaemon(this);

  IPC_MS_VLOG(INFO) << "going to wait for the shutdown signals";
  while(true) {
    if(daemon_->waitShutDownSignals())
      break;
  }
  IPC_MS_VLOG(INFO) << "GCService process shutdown";
}





}
}
}
