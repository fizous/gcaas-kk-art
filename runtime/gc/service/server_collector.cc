

#include <string>
#include <cutils/ashmem.h>
#include "scoped_thread_state_change.h"
#include "thread_state.h"
#include "thread.h"
#include "mem_map.h"
#include "gc/service/global_allocator.h"


namespace art {
namespace gc {
namespace gcservice {

ServerCollector::ServerCollector(space::GCSrvSharableHeapData* meta_alloc) :
    heap_data_(meta_alloc) {
//  Thread* self = Thread::Current();
  run_mu_ = new Mutex("gcService Shutdown");
  run_cond_.reset(new ConditionVariable("gcService Shutdown condition variable",
      *run_mu_));
  status_ = 0;

  SharedFutexData* _futexAddress = &heap_data_->phase_lock_.futex_head_;
  SharedConditionVarData* _condAddress = &heap_data_->phase_lock_.cond_var_;

  if(true) {
    phase_mu_ = new InterProcessMutex(_futexAddress, "GCServiceD Mutex");
    phase_cond_ =  new InterProcessConditionVariable(*phase_mu_,
        "GCServiceD CondVar", _condAddress);
    thread_ = NULL;

    CHECK_PTHREAD_CALL(pthread_create,
        (&pthread_, NULL,
        &RunCollectorDaemon, this),
        "Server-Collector");
  }
}


void ServerCollector::SignalCollector(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::SignalCollector..." << self->GetTid();
//  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
//  MutexLock mu(self, *run_mu_);
//  if(status_ == 0) {
//    status_ = 1;
//    LOG(ERROR) << "ServerCollector::Setting status to 1..." << self->GetTid();
//  }
//  run_cond_->Broadcast(self);
}

void ServerCollector::WaitForRequest(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::WaitForRequest.." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  MutexLock mu(self, *run_mu_);
  while(status_ == 0) {
    run_cond_->Wait(self);
  }
  LOG(ERROR) << "leaving ServerCollector:: leaving WaitForRequest";
}

void ServerCollector::ExecuteGC(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::ExecuteGC.." << self->GetTid();
//  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
//  IPMutexLock interProcMu(self, *phase_mu_);
//  if(heap_data_->gc_phase_ == space::IPC_GC_PHASE_NONE) {
//    heap_data_->gc_phase_ = space::IPC_GC_PHASE_INIT;
//    LOG(ERROR) << "ServerCollector::ExecuteGC..setting phase to init: " << self->GetTid();
//    phase_cond_->Broadcast(self);
//  } else {
//    LOG(ERROR) << "ServerCollector::ExecuteGC.. skipped setting phase to init: " << self->GetTid();
//  }
}



void ServerCollector::WaitForGCTask(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::WaitForGCTask.." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  MutexLock mu(self, *run_mu_);
  status_ = 0;
  LOG(ERROR) << "ServerCollector::WaitForGCTask.. leaving" << self->GetTid();
//  IPMutexLock interProcMu(self, *phase_mu_);
//  while(heap_data_->gc_phase_ != space::IPC_GC_PHASE_FINISH) {
//    LOG(ERROR) << "ServerCollector::WaitForGCTask..inside while loop." << self->GetTid();
//    phase_cond_->Wait(self);
//  }
//  LOG(ERROR) << "ServerCollector::WaitForGCTask..left the wait condition.." << self->GetTid();
//  if(heap_data_->gc_phase_ == space::IPC_GC_PHASE_FINISH) {
//    heap_data_->gc_phase_ = space::IPC_GC_PHASE_NONE;
//  }
//  phase_cond_->Broadcast(self);
}

void ServerCollector::Run(void) {
  LOG(ERROR) << "ServerCollector::Run";
  while(true) {
    WaitForRequest();
    ExecuteGC();
    WaitForGCTask();
  }
}

void* ServerCollector::RunCollectorDaemon(void* args) {
  ServerCollector* _server = reinterpret_cast<ServerCollector*>(args);

  Runtime* runtime = Runtime::Current();
  bool _createThread =  runtime->AttachCurrentThread("serverCollector", true,
      runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler());

  if(!_createThread) {
    LOG(ERROR) << "-------- could not attach internal GC service Daemon ---------";
    return NULL;
  }

  _server->thread_ = Thread::Current();
  _server->Run();

  return NULL;
}



ServerCollector* ServerCollector::CreateServerCollector(void* args) {
  space::GCSrvSharableHeapData* _meta_alloc =
      (space::GCSrvSharableHeapData*) args;
  return new ServerCollector(_meta_alloc);
}

}
}
}
