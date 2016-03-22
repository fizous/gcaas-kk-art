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
#include "gc/space/space.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "mem_map.h"

using ::art::gc::space::AgentMemInfoHistory;
using ::art::gc::space::AgentMemInfo;
using ::art::gc::space::GCSrvSharableDlMallocSpace;
namespace art {
namespace gc {
namespace gcservice {

GCServiceProcess* GCServiceProcess::process_ = NULL;

const char * GCServiceDaemon::meminfo_args_[] = {
    "--oom"
};


long GCSrvcMemInfoOOM::total_ram_ = 0;
long GCSrvcMemInfoOOM::free_ram_[] = {0, 0, 0, 0};

GCSrvcMemInfoOOM GCSrvcMemInfoOOM::mem_info_oom_list_[] = {
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


int GCSrvcMemInfoOOM::parseOOMRecString(char* line,
                                           long* mem_size, int* pid) {

//  int length= 0;
//  const char* res;
//  char  output[256];
  int result = sscanf(line, " %ld kB: %*s (pid %d",  mem_size, pid);

  if(result == 2)
    return 100;
  return 0;
}


int GCSrvcMemInfoOOM::parseOOMHeaderString(char* line, char* label, long* mem_size) {
  int result = sscanf(line, " %ld kB: %[^\n]s",  mem_size, label);

  if(result == 2)
    return 100;
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

//static bool GCSrvcMemInfoOOM_skip_file(char* line, int* stage_parsing) {
//  if(*stage_parsing == 0) {
//    char ooom[256];
//    int result = sscanf(line, "Total PSS by OOM %s:", ooom);
//    if(result > 0) {
//      *stage_parsing = 1;
//    }
//    return false;
//  }
//  return true;
//}

int GCSrvcMemInfoOOM::parseMemInfo(const char* file_path) {

  FILE *f;
  f = fopen(file_path, "r");
  if (!f)
    return 0;// errno;

  char line[256];
  //char _label[128];
  int _read_res = 0;
  int _stage = 0;
  int pid = 0;
  char _label[256];
  long _memory_size = 0;

  GCSrvcMemInfoOOM*  _meminfoP = NULL;
//  for(int i = 0; i< 13; i++){
//    GCSrvcMemInfoOOM*  _meminfoP = &(GCSrvcMemInfoOOM::mem_info_oom_list_[i]);
//    _meminfoP->resetMemInfo();
//  }


  int _curr_index = 0;
  _meminfoP = &(GCSrvcMemInfoOOM::mem_info_oom_list_[_curr_index]);

  while (fgets(line, 256, f)) {

    if(_stage ==1) {
      _read_res  = GCSrvcMemInfoOOM::parseOOMRecString(line,
                                               &_memory_size, &pid);

      if(_read_res == 100) {
        GCSrvceAgent* _agent = GCServiceProcess::process_->daemon_->GetAgentByPid(pid);
        if(_agent != NULL) {


          _agent->updateOOMLabel(_meminfoP->oom_adj_, _memory_size);

          AgentMemInfo* _meminfo_app_rec = &(_agent->meminfo_rec_);

          LOG(ERROR) << "---1-" << pid << ", "
              << _meminfo_app_rec->memory_size_ << " kB, "
              << _meminfo_app_rec->oom_label_ << "...." << line;
          _meminfoP->agents_list_.push_back(_agent);
        }

        continue;
      }
    }

    if(_stage  <= 1) {
      if(strlen(line) == 1) {
        _stage |= 2;
        continue;
      }


      _read_res  = GCSrvcMemInfoOOM::parseOOMHeaderString(line, _label, &_memory_size);
      if(_read_res == 100) {
        LOG(ERROR) << "orig: " << line << ", .. label:" << _label;
        while(_curr_index < 13) {
          _meminfoP = &(GCSrvcMemInfoOOM::mem_info_oom_list_[_curr_index]);
          _meminfoP->resetMemInfo();
          _meminfoP->agents_list_.clear();
          _curr_index++;
          if(strcmp(_meminfoP->oom_label_, _label) == 0){
            _meminfoP->aggregate_memory_ = _memory_size;
            LOG(ERROR) << "found label: " << _label << " at index " <<  _curr_index-1;
            break;
          }

        }
        //LOG(ERROR) << "-0-" << _meminfoP->oom_label_ << ", "<< _memory_size << " kB";
        _stage |= 1;
        continue;
      }


    }
    if(_stage == 3) {
      _read_res = GCSrvcMemInfoOOM::readTotalMemory(line);
      if(_read_res == 100) {
        _stage |= 4;
        LOG(ERROR) << "---2-" << line;
        continue;
      }
    }
    if(_stage == 7) {
      _read_res = GCSrvcMemInfoOOM::readFreeMemory(line);
      if(_read_res == 100) {
        _stage |= 8;
        LOG(ERROR) << "---2-" << line;
        break;
      }
    }
  }

  for(int i = 0; i< 13; i++){
    GCSrvcMemInfoOOM*  _meminfoP = &(GCSrvcMemInfoOOM::mem_info_oom_list_[i]);
    LOG(ERROR) << "-0-" << _meminfoP->oom_label_ << ", "<< _meminfoP->aggregate_memory_ << " kB";
  }

  fclose(f);


  return 1;

}





int GCSrvcMemInfoOOM::readTotalMemory(char* line) {
  int result = sscanf(line, " Total RAM: %ld kB", &total_ram_);
  if(result == 1) {
    return 100;
  }
  if(result == EOF)
    return EOF;
  return 0;
}

int GCSrvcMemInfoOOM::readFreeMemory(char* line) {
  int _index = 0;
  int result = sscanf(line, " Free RAM: %ld kB (%ld cached pss + %ld cached + %ld free)",
                      &free_ram_[_index], &free_ram_[_index+1], &free_ram_[_index+2],
                      &free_ram_[_index+3]);
  if(result == 4 ) {
    return 100;
  }
  if(result == EOF)
    return EOF;
  return 0;
}


GCServiceDaemon* GCServiceDaemon::CreateServiceDaemon(GCServiceProcess* process) {
  return new GCServiceDaemon(process);
}


GCSrvceAgent* GCServiceDaemon::GetAgentByPid(int pid) {
  auto result = agents_map_.find(pid);
  if (result == agents_map_.end()) {
    return NULL;
  } else {
    return result->second;
  }


//  for (auto& client : client_agents_) {
//     if(client->binding_.pair_mapps_->first->process_id_ == pid) {
//       return client;
//     }
//  }
//  return NULL;
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
     thread_(NULL), processed_index_(0), mem_info_fd_(-1), last_global_update_time_ns_(0) {
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
//static bool ReadStaticInt(JNIEnvExt* env, jclass clz, const char* name,
//                                                              int* out_value) {
//  CHECK(out_value != NULL);
//  jfieldID field = env->GetStaticFieldID(clz, name, "I");
//  if (field == NULL) {
//    env->ExceptionClear();
//    return false;
//  }
//  *out_value = env->GetStaticIntField(clz, field);
//  return true;
//}

void GCServiceDaemon::SetMemInfoDumpFile(void) {
  int fd = open("/data/anr/meminfo.data", O_RDWR | O_CREAT, 0777);
  if (fd == -1) {
    PLOG(ERROR) << "Unable to open stack trace file '" << "/data/anr/meminfo.data" << "'";
    return;
  }
  mem_info_fd_ = fd;


}


void GCServiceDaemon::UpdateGlobalProcessStates(void) {




//  Thread* self = Thread::Current();
//  JNIEnvExt* env = self->GetJniEnv();
//  // Just attempt to do this the first time.
//  jclass clz = env->FindClass("android/app/ActivityManager");
//  if (clz == NULL) {
//    LOG(WARNING) << "Activity manager class is null";
//    return;
//  }
//  ScopedLocalRef<jclass> activity_manager(env, clz);
//  std::vector<const char*> care_about_pauses;
//  care_about_pauses.push_back("PROCESS_STATE_TOP");
//  care_about_pauses.push_back("PROCESS_STATE_IMPORTANT_BACKGROUND");
//
//  // Process states which care about pause times.
//  std::set<int> process_state_cares_about_pause_time_;
//
//  // Attempt to read the constants and classify them as whether or not we care about pause times.
//  for (size_t i = 0; i < care_about_pauses.size(); ++i) {
//    int process_state = 0;
//    if (ReadStaticInt(env, activity_manager.get(), care_about_pauses[i], &process_state)) {
//      process_state_cares_about_pause_time_.insert(process_state);
//      LOG(ERROR)<< "XXXXXX Adding process state " << process_state
//                 << " to set of states which care about pause time";
//    }
//  }

//  int fd = open("/data/anr/meminfo.data", O_RDWR | O_CREAT, 0777);
//  if (fd == -1) {
//    PLOG(ERROR) << "Unable to open stack trace file '" << "/data/anr/meminfo.data" << "'";
//    return;
//  }


  uint64_t _curr_time = NanoTime();
  uint64_t _difference_time = _curr_time - last_global_update_time_ns_;
  if(_difference_time < 2000000000)
    return;
  LOG(ERROR)<< "--------------------------------------";
  last_global_update_time_ns_ = _curr_time;

  //std::string _meminfo_lines;

  SetMemInfoDumpFile();
  bool mem_info_result =
      GCServiceProcess::process_->fileMapperSvc_->UpdateMemInfo(mem_info_fd_,
                                                                "meminfo",
                                                                GCServiceDaemon::meminfo_args_,
                                                                1);
  close(mem_info_fd_);

  if(mem_info_result) {


    if(GCSrvcMemInfoOOM::parseMemInfo("/data/anr/meminfo.data")) {

      LOG(ERROR) << "total_ram=" << GCSrvcMemInfoOOM::total_ram_ << "\n" <<
          "free_rams: " << GCSrvcMemInfoOOM::free_ram_[0] <<
          "," << GCSrvcMemInfoOOM::free_ram_[1] <<
          "," << GCSrvcMemInfoOOM::free_ram_[2] <<
          "," << GCSrvcMemInfoOOM::free_ram_[3];
    }

    //if(fcntl(mem_info_fd_, F_GETFD) != -1 || errno != EBADF) {
//      if (!ReadFileToString("/data/anr/meminfo.data", &_meminfo_lines)) {
//        LOG(ERROR) << "(couldn't read dump of mem_info  \n";
//      } else {
//        LOG(ERROR) << "meminfo_dump------------------------\n" << _meminfo_lines;
//        std::vector<std::string> mem_info_dump;
//        Split(_meminfo_lines, '\n', mem_info_dump);
//      }
    //}
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
  LOG(ERROR)<< "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~";


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
  process_id_ = mappedPair->first->process_id_;
//  binding_.java_lang_Class_cached_ =
//      reinterpret_cast<mirror::Class*>(mappedPair->first->java_lang_Class_cached_);

  collector_ = ServerCollector::CreateServerCollector(&binding_);
  meminfo_rec_ = &(binding_.sharable_space_->meminfo_rec_);

  meminfo_rec_->histor_head_ = 0;
  meminfo_rec_->histor_tail_ = 0;
  meminfo_rec_->last_update_ns_ = 0;
  for(int i = 0; i < MEM_INFO_WINDOW_SIZE; i++) {
    AgentMemInfoHistory* hist_rec = &(meminfo_rec_->history_wins_[i]);
    hist_rec->heap_size_ = 0;
    hist_rec->memory_size_ = 0;
    hist_rec->oom_label_ = 0;
  }

}


void GCSrvceAgent::updateOOMLabel(int new_label, long memory_size) {
  meminfo_rec_->memory_size_ = memory_size;
  meminfo_rec_->oom_label_ =  new_label;
  AgentMemInfoHistory* hist_rec = &(meminfo_rec_->history_wins_[meminfo_rec_->histor_tail_]);
  meminfo_rec_->histor_tail_ = (meminfo_rec_->histor_tail_ + 1) % MEM_INFO_WINDOW_SIZE;


  if(meminfo_rec_->histor_tail_ == meminfo_rec_->histor_head_) {
    meminfo_rec_->histor_head_ = (meminfo_rec_->histor_head_ + 1) % MEM_INFO_WINDOW_SIZE;
  }

  hist_rec->oom_label_ = new_label;
  meminfo_rec_->last_update_ns_ = NanoTime();
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
