/*
 * gcservice_daemon.cc
 *
 *  Created on: Sep 28, 2015
 *      Author: hussein
 */
#include <string>
#include <cutils/ashmem.h>
#include "gc/service/global_allocator.h"
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "mem_map.h"


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
  LOG(ERROR) << "-------- Inside GCServiceDaemon::RunDaemon ---------";
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
    _processObj->service_meta_->status_ = GCSERVICE_STATUS_RUNNING;
    _processObj->service_meta_->cond_->Broadcast(self);
  }

  LOG(ERROR) << "GCServiceDaemon is entering the main loop: " <<
      _daemonObj->thread_->GetTid();

  while(_processObj->service_meta_->status_ == GCSERVICE_STATUS_RUNNING) {
    _daemonObj->mainLoop();
  }

  LOG(ERROR) << "GCServiceDaemon left the main loop: " <<
      _daemonObj->thread_->GetTid();

  return NULL;
}


GCServiceDaemon::GCServiceDaemon(GCServiceProcess* process) :
     thread_(NULL), processed_index_(0) {
  Thread* self = Thread::Current();
  {
    IPMutexLock interProcMu(self, *process->service_meta_->mu_);
    process->service_meta_->status_ = GCSERVICE_STATUS_STARTING;
//    registered_apps_.reset(accounting::ATOMIC_MAPPED_STACK_T::Create("registered_apps",
//        64, false));
    initShutDownSignals();
    LOG(ERROR) << "Thread_POOL----" << "resetting thread pool for gcservice daemon";
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
      reinterpret_cast<gc::space::GCSrvSharableDlMallocSpace*>(
          mappedPair->first->shared_space_addr_);
  collector_ = ServerCollector::CreateServerCollector(&binding_);
}


//----------
//----------------------------- GCServiceProcess ------------------------------

void GCServiceProcess::LaunchGCServiceProcess(void) {
  InitGCServiceProcess(
      GCServiceGlobalAllocator::GetServiceHeader(),
      GCServiceGlobalAllocator::GetServiceHandShaker());
}


GCServiceProcess* GCServiceProcess::InitGCServiceProcess(GCServiceHeader* meta,
    GCSrvcClientHandShake* handshake) {
  if(GCServiceProcess::process_ == NULL) {
    LOG(ERROR) << "initializing process";
    GCServiceProcess::process_ = new GCServiceProcess(meta, handshake);
    GCServiceProcess::process_->SetGCDaemon();

  }
  return GCServiceProcess::process_;
}

bool GCServiceProcess::initSvcFD(void) {
  bool returnRes = false;
  IPMutexLock interProcMu(thread_, *service_meta_->mu_);
  if(fileMapperSvc_ == NULL) {
    LOG(ERROR) << " creating fileMapperSvc_ for first time ";
    fileMapperSvc_ =
        android::FileMapperService::CreateFileMapperSvc();
    returnRes = android::FileMapperService::IsServiceReady();
  } else {
    LOG(ERROR) << " reconnecting ";
    returnRes = android::FileMapperService::Reconnect();
  }

  if(returnRes) {
    LOG(ERROR) << " the proc found the service initialized ";
    service_meta_->status_ = GCSERVICE_STATUS_SERVER_INITIALIZED;
  } else {
    LOG(ERROR) << " the proc found the service not initialized ";
  }
  service_meta_->cond_->Broadcast(thread_);
  return returnRes;
}


GCServiceProcess::GCServiceProcess(GCServiceHeader* meta,
                                  GCSrvcClientHandShake* handShakeMemory) :
    service_meta_(meta), handShake_(handShakeMemory), fileMapperSvc_(NULL),
    thread_(NULL), srvcReady_(false) {

  thread_ = Thread::Current();
  {
    LOG(ERROR) << " changing status of service to waiting for server ";
    IPMutexLock interProcMu(thread_, *service_meta_->mu_);
    service_meta_->status_ = GCSERVICE_STATUS_WAITINGSERVER;
    service_meta_->cond_->Broadcast(thread_);
  }
  srvcReady_ = initSvcFD();

  import_address_ = std::max(Runtime::Current()->GetHeap()->GetMaxAddress(),
      MemBaseMap::max_covered_address);

}

void GCServiceProcess::SetGCDaemon(void) {
  LOG(ERROR) << "Import Address ------ " << reinterpret_cast<void*>(import_address_);
  daemon_ = GCServiceDaemon::CreateServiceDaemon(this);

  LOG(ERROR) << "going to wait for the shutdown signals";
  while(true) {
    if(daemon_->waitShutDownSignals())
      break;
  }
  LOG(ERROR) << "GCService process shutdown";
}


}
}
}
