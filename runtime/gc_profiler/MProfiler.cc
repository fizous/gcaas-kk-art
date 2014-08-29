/*
 * MPprofiler.cc
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#include <string>
#include <pthread.h>
#include <fcntl.h>


#include "base/unix_file/fd_file.h"
#include "cutils/sched_policy.h"
#include "cutils/process_name.h"
#include <cutils/trace.h>
#include "gc/heap.h"
#include "gc_profiler/MProfiler.h"
#include "locks.h"
#include "os.h"
#include "runtime.h"
#include "thread_list.h"
#include "thread.h"



namespace art {
namespace mprofiler {

const char * MProfiler::benchmarks[] = {
			"com.aurorasoftworks.quadrant.ui.professional",
			"purdue.specjvm98",
			"purdue.dacapo",
			"com.antutu.ABenchMark",
			"com.android.cm3",
			"purdue.gcbench",
			"com.pandora.android"
			//"com.android.systemui"  //we can add this to the profiled targets
			//"com.android.launcher" // the problem with this service is its lack of permissions to access Sdcard
};

const char * MProfiler::gcMMPRootPath[] = {
		"/sdcard/gcperf/", "/data/anr/"
};

// Member functions definitions including constructor
MProfiler::MProfiler(GCMMP_Options* argOptions)
		: index_(argOptions->mprofile_type_),
		main_thread_(NULL),
		gc_daemon_(NULL),
		flags_(0),
 		dump_file_name_("PERF.log"),
		thread_recs_(NULL),
		prof_thread_(NULL),
		enabled_((argOptions->mprofile_type_ != MProfiler::kGCMMPDisableMProfile)),
		running_(false)
{
	;
	if(IsProfilingEnabled()) {
		LOG(INFO) << "MProfiler Profiling is Enabled";
		prof_thread_mutex_ = new Mutex("MProfile Thread lock");
		prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
																									*prof_thread_mutex_));

	} else {
		LOG(INFO) << "MProfiler Profiling is Disabled";
	}
	LOG(INFO) << "MProfiler Created";
}

void MProfiler::InitializeProfiler(){
	if(!IsProfilingEnabled())
		return;
	if(IsProfilingRunning())
		return;

	if(IsCreateProfDaemon()){
		CreateProfilerDaemon();
	} else {
		LOG(INFO) << "MProfiler: No Daemon Creation";
		Thread* self = Thread::Current();
		MutexLock mu(self, *prof_thread_mutex_);
		if(!running_) {
			SetMProfileFlags();
		} else {
			LOG(INFO) << "MProfiler: was already running";
		}
		prof_thread_cond_->Broadcast(self);
	}

	LOG(INFO) << "MProfiler Is Initialized";
}

void MProfiler::SetMProfileFlags(void){
	running_ = true;
	AttachThreads();
	OpenDumpFile();
}

MProfiler::~MProfiler() {
	if(prof_thread_mutex_ != NULL)
		delete prof_thread_mutex_;

}

void* MProfiler::Run(void* arg) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);


  Runtime* runtime = Runtime::Current();

  bool hasProfDaemon =
  		runtime->AttachCurrentThread("MProfile Daemon", true, runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler());
  CHECK(hasProfDaemon);

  if(!hasProfDaemon)
  	return NULL;

  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {
    MutexLock mu(self, *mProfiler->prof_thread_mutex_);
    if(!mProfiler->running_) {
    	mProfiler->prof_thread_ = self;
    	mProfiler->SetMProfileFlags();
    } else {
    	 LOG(INFO) << "MPRofiler: Profiler was already created";
    }

    mProfiler->prof_thread_cond_->Broadcast(self);
  }


  LOG(INFO) << "MPRofiler: Profiler Daemon Created";
  return NULL;

}

void MProfiler::CreateProfilerDaemon(void){
  // Create a raw pthread; its start routine will attach to the runtime.
	Thread* self = Thread::Current();
	MutexLock mu(self, *prof_thread_mutex_);

  CHECK_PTHREAD_CALL(pthread_create, (&pthread_, NULL, &Run, this), "MProfiler Daemon thread");

  while (prof_thread_ == NULL) {
  	prof_thread_cond_->Wait(self);
  }
  prof_thread_cond_->Broadcast(self);

  LOG(INFO) << "MPRofiler: Caller is leaving now";

}

static void GCMMPAttachThread(Thread* t, void* arg){
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);
	LOG(INFO) << "MPRofiler: Attaching thread: " << t->GetTid();
	if(mProfiler->IsProfilingEnabled()){
		LOG(INFO) << "MPRofiler: Attaching thread: " << t->GetTid();
	} else {
		LOG(INFO) << "MPRofiler: Attaching thread: " << t->GetTid();
	}
}

void MProfiler::AttachThread(Thread* thread){
	if(IsProfilingRunning()) {
		LOG(INFO) << "MPRofiler: Attaching thread Late " << self->GetTid() ;
	}
}

void MProfiler::AttachThreads(){
//
//	 thread_list->SuspendAll();
//	 thread_list()->ForEach(GCMMPAttachThread, this);
//
//	 thread_list->ResumeAll();

	Thread* self = Thread::Current();
	LOG(INFO) << "MPRofiler: Attaching All threads " << self->GetTid();
	ThreadList* thread_list = Runtime::Current()->GetThreadList();
	MutexLock mu(self, *Locks::thread_list_lock_);
	thread_list->ForEach(GCMMPAttachThread, this);
	LOG(INFO) << "MPRofiler: Done Attaching All threads ";

}

void MProfiler::OpenDumpFile(){
	for (size_t i = 0; i < GCMMP_ARRAY_SIZE(gcMMPRootPath); i++) {
		char str[256];
		strcpy(str, gcMMPRootPath[i]);
		strcat(str, dump_file_name_);


		int fd = open(str, O_RDWR | O_APPEND | O_CREAT, 0777);
	  if (fd == -1) {
	    PLOG(ERROR) << "Unable to open MProfile Output file '" << str << "'";
	    continue;
	  }
    PLOG(INFO) << "opened  Successsfully MProfile Output file '" << str << "'";
    dump_file_ = new File(fd, std::string(dump_file_name_));
    return;
	}
}



void MProfiler::GCMMProfPerfCounters(const char* name) {
	if(IsProfilingEnabled()){
		for (size_t i = 0; i < GCMMP_ARRAY_SIZE(benchmarks); i++) {
			if (strcmp(name, benchmarks[i]) == 0) {
				LOG(INFO) << "MProfiler found a target VM " << name << " " << GCMMP_ARRAY_SIZE(benchmarks);
				InitializeProfiler();
				return;
			}
		}
	}
	LOG(INFO) << "MProfiler did not find a target VM for " << name << " " << GCMMP_ARRAY_SIZE(benchmarks);
}

void MProfiler::PreForkPreparation() {
	dvmGCMMPSetName = dvmGCMMProfPerfCounters;
}
}// namespace mprofiler
}// namespace art

void dvmGCMMProfPerfCounters(const char* vmName){
	art::mprofiler::MProfiler* mProfiler =
			art::Runtime::Current()->GetMProfiler();
	mProfiler->GCMMProfPerfCounters(vmName);
}
