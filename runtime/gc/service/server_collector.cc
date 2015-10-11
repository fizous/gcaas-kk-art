

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
    heap_data_(meta_alloc),
    run_mu_("ServerLock"),
    run_cond_("ServerLock::cond_", run_mu_),
    thread_(NULL),
    status_(0) {

  SharedFutexData* _futexAddress = &heap_data_->phase_lock_.futex_head_;
  SharedConditionVarData* _condAddress = &heap_data_->phase_lock_.cond_var_;
  SharedFutexData* _conc_futexAddress = &heap_data_->conc_lock_.futex_head_;
  SharedConditionVarData* _conc_condAddress = &heap_data_->conc_lock_.cond_var_;

  if(true) {
    phase_mu_ = new InterProcessMutex(_futexAddress, "GCServiceD Mutex");
    phase_cond_ =  new InterProcessConditionVariable(*phase_mu_,
        "GCServiceD CondVar", _condAddress);

    conc_req_cond_mu_ = new InterProcessMutex(_conc_futexAddress, "GCConc Mutex");
    conc_req_cond_ = new InterProcessConditionVariable(*conc_req_cond_mu_,
        "GCConc CondVar", _conc_condAddress);

    CHECK_PTHREAD_CALL(pthread_create,
        (&pthread_, NULL,
        &RunCollectorDaemon, this),
        "Server-Collector");


    Thread* self = Thread::Current();
    MutexLock mu(self, run_mu_);
    LOG(ERROR) << "------------------ServerCollector--->going to wait for initialization of Daemon";
    while (thread_ == NULL) {
      run_cond_.Wait(self);
    }
  }
}


void ServerCollector::SignalCollector(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::SignalCollector..." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    MutexLock mu(self, run_mu_);
    if(thread_ != NULL) {
      status_ = status_ + 1;
      LOG(ERROR) << "ServerCollector::SignalCollector ---- Thread was not null:" << self->GetTid() << "; status=" << status_;
      run_cond_.Broadcast(self);
    } else {
      LOG(ERROR) << "ServerCollector::SignalCollector ---- Thread was  null:" << self->GetTid();
    }
//    if(status_ == 0) {
//      status_ = 1;
//    }
//
  }

  LOG(ERROR) << "ServerCollector::SignalCollector...LEaving: " << self->GetTid();
}

void ServerCollector::WaitForRequest(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::WaitForRequest.." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    MutexLock mu(self, run_mu_);
    while(status_ <= 0) {
      run_cond_.Wait(self);
    }
    status_ = 0;
    LOG(ERROR) << "leaving ServerCollector:: leaving WaitForRequest; status=" << status_;
    run_cond_.Broadcast(self);
  }


}

void ServerCollector::ExecuteGC(void) {
  Thread* self = Thread::Current();
  LOG(ERROR) << "ServerCollector::ExecuteGC.." << self->GetTid();
  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    while(heap_data_->is_gc_running_ == 1 && heap_data_->conc_flag_ == 1) {
      LOG(ERROR) << "__________ServerCollector::ExecuteGC: going to wait for running flags";
      conc_req_cond_->Wait(self);
    }
    LOG(ERROR) << "__________ServerCollector::ExecuteGC: left wait for running flags";
    heap_data_->conc_flag_ = 1;
    heap_data_->is_gc_complete_ = 0;
    heap_data_->is_gc_running_ = 0;
    conc_req_cond_->Broadcast(self);
    LOG(ERROR) << "ServerCollector::ExecuteGC.. " << self->GetTid() <<
              ", setting conc flag to " << heap_data_->conc_flag_;
  }


  if(false) {
    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *phase_mu_);
      heap_data_->gc_phase_ = space::IPC_GC_PHASE_INIT;
      LOG(ERROR) << "ServerCollector::ExecuteGC..setting phase to init: " << self->GetTid();
      phase_cond_->Broadcast(self);
    }
  }
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
  {
    IPMutexLock interProcMu(self, *conc_req_cond_mu_);
    while(heap_data_->is_gc_complete_ != 1) {
      conc_req_cond_->Wait(self);
      LOG(ERROR) << "ServerCollector::WaitForGCTask.. " << self->GetTid() <<
          ", setting conc flag to " << heap_data_->conc_flag_;
    }
    LOG(ERROR) << "ServerCollector::WaitForGCTask.. " << self->GetTid() <<
        ", leaving while flag " << heap_data_->conc_flag_;
    heap_data_->is_gc_complete_ = 0;
    heap_data_->conc_flag_ = 0;
//    heap_data_->conc_flag_ = heap_data_->conc_flag_ - 1;
    conc_req_cond_->Broadcast(self);
  }

  if(false) {

    ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
    {
      IPMutexLock interProcMu(self, *phase_mu_);
      heap_data_->gc_phase_ = space::IPC_GC_PHASE_FINISH;
      LOG(ERROR) << "ServerCollector::WaitForGCTask..setting phase to IPC_GC_PHASE_FINISH: " << self->GetTid();
      phase_cond_->Wait(self);
    }
  }

//  ScopedThreadStateChange tsc(self, kWaitingForGCProcess);
//  {
//    MutexLock mu(self, run_mu_);
//    status_ = 0;
//    LOG(ERROR) << "ServerCollector::WaitForGCTask.. leaving" << self->GetTid();
//  }
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
  CHECK(runtime->AttachCurrentThread("serverCollector", true, NULL, false));

  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    MutexLock mu(self, _server->run_mu_);
    _server->thread_ = self;
    _server->run_cond_.Broadcast(self);
  }

//  bool _createThread =  runtime->AttachCurrentThread("serverCollector", true,
//      runtime->GetSystemThreadGroup(),
//      !runtime->IsCompiler());
//
//  if(!_createThread) {
//    LOG(ERROR) << "-------- could not attach internal GC service Daemon ---------";
//    return NULL;
//  }
  LOG(ERROR) << "ServerCollector::RunCollectorDaemon after broadcast" ;

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
