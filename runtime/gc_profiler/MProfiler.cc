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
#include "cutils/system_clock.h"
#include "gc/heap.h"
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MProfiler.h"
#include "locks.h"
#include "os.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "thread_list.h"
#include "thread_state.h"
#include "thread.h"
//#include "scoped_thread_state_change.h"


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

const GCMMPProfilingEntry MProfiler::profilTypes[] = {
		{
				 0x0D,
				 GCMMP_FLAGS_CREATE_DAEMON,
				 "MMU", "MMU over a given period of time", "PERF_MMU_REF.log", NULL
		}//MMU
};//profilTypes

uint64_t GCPauseThreadManager::startCPUTime = 0;
uint64_t GCPauseThreadManager::startRealTime = 0;


uint64_t GCPauseThreadManager::GetRelevantRealTime(void)  {
	return 0;
	//return uptime_nanos() - startRealTime;
}

uint64_t GCPauseThreadManager::GetRelevantCPUTime(void)  {
	return 0;
	//return ProcessTimeNS() - startCPUTime;
}

void GCPauseThreadManager::MarkStartTimeEvent(GCMMP_BREAK_DOWN_ENUM evType) {
	if(!busy_) {
		curr_marker_->startMarker = GCPauseThreadManager::GetRelevantCPUTime();
		curr_marker_->type = evType;
		busy_ = true;
		count_opens_++;
	}
}

void GCPauseThreadManager::MarkEndTimeEvent(GCMMP_BREAK_DOWN_ENUM evType) {
	if(busy_){
		if(curr_marker_->type != evType)
			return;
		curr_marker_->finalMarker = GCPauseThreadManager::GetRelevantCPUTime();
		IncrementIndices();
		count_opens_--;
	}
}


void GCPauseThreadManager::DumpProfData(void) {
	int totalC = 0;
	if(curr_bucket_ind_ < 0)
		return;
	LOG(INFO) << "parenthesis: " << count_opens_;
	for(int bucketInd = 0; bucketInd <= curr_bucket_ind_; bucketInd++){
		int limit_ = (bucketInd == curr_bucket_ind_) ? curr_entry_:kGCMMPMaxEventEntries;
		for(int entryInd = 0; entryInd < limit_; entryInd++){
			LOG(INFO) << "pMgr " << totalC++ << ": " << pauseEvents[bucketInd][entryInd].type << ", " << pauseEvents[bucketInd][entryInd].startMarker << ", " << pauseEvents[bucketInd][entryInd].finalMarker;
		}
	}
}

GCMMPThreadProf::GCMMPThreadProf(MProfiler* mProfiler, Thread* thread)
	: pid(thread->GetTid()),
	  suspendedGC(false),
	  pauseManager(NULL),
	  state(GCMMP_TH_STARTING) {

	LOG(INFO) << "MPRofiler: Initializing arrayBreaks for " << thread->GetTid();
	for(int _iter = GCMMP_GC_BRK_SUSPENSION; _iter < GCMMP_GC_BRK_MAXIMUM; _iter++) {
		memset((void*) &timeBrks[_iter], 0, sizeof(GCMMP_ProfileActivity));
	}
	LOG(INFO) << "MPRofiler: Done Initializing arrayBreaks for " << thread->GetTid();
	pauseManager = new GCPauseThreadManager();
	state = GCMMP_TH_RUNNING;
	LOG(INFO) << "MProfiler : ThreadProf is initialized";
}

GCMMPThreadProf::~GCMMPThreadProf() {

}



bool GCMMPThreadProf::StopProfiling(void) {
	if(state == GCMMP_TH_RUNNING) {
		state = GCMMP_TH_STOPPED;
		return true;
	}
	return false;
}

void GCMMPThreadProf::Destroy(MProfiler* mProfiler) {

}

void MProfiler::RemoveThreadProfile(GCMMPThreadProf* thProfRec) {
	if(IsProfilingRunning()) {
			if(!thProfRec->StopProfiling()) {
				LOG(ERROR) << "MProfiler : ThreadProf is initialized";
			}
			threadProflist_.remove(thProfRec);
			thProfRec->Destroy(this);
			delete thProfRec;
	}
}

// Member functions definitions including constructor
MProfiler::MProfiler(GCMMP_Options* argOptions)
		: index_(argOptions->mprofile_type_),
		main_thread_(NULL),
		gc_daemon_(NULL),
		prof_thread_(NULL),
		enabled_((argOptions->mprofile_type_ != MProfiler::kGCMMPDisableMProfile)),
		running_(false),
		receivedSignal_(false)
{
	if(IsProfilingEnabled()) {
		size_t _loop = 0;
		for(_loop = 0; _loop < GCMMP_ARRAY_SIZE(MProfiler::profilTypes); _loop++) {
			if(MProfiler::profilTypes[_loop].id_ == index_)
				break; //found
		}
		if(_loop >= GCMMP_ARRAY_SIZE(MProfiler::profilTypes)) {
			LOG(ERROR) << "MProfiler : Performance type is not supported";
		}
		const GCMMPProfilingEntry* profEntry = &MProfiler::profilTypes[_loop];
		flags_ = profEntry->flags_;
		dump_file_name_ = profEntry->logFile_;
		LOG(INFO) << "MProfiler Profiling is Enabled";
		prof_thread_mutex_ = new Mutex("MProfile Thread lock");
		prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
																									*prof_thread_mutex_));

	} else {
		flags_ = 0;
		dump_file_name_ = NULL;
		LOG(INFO) << "MProfiler Profiling is Disabled";
	}
	LOG(INFO) << "MProfiler Created";
}


void MProfiler::DumpCurrentOutpu(void) {
	//ScopedThreadStateChange tsc(Thread::Current(), kWaitingForSignalCatcherOutput);
}

static void GCMMPKillThreadProf(GCMMPThreadProf* profRec, void* arg) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);
	if(mProfiler != NULL) {
		if(mProfiler->DettachThread(profRec)) {

		}
	}
}

static void GCMMPResetThreadField(Thread* t, void* arg) {
	t->SetProfRec(NULL);
}


void MProfiler::ShutdownProfiling(void) {

	if(IsProfilingRunning()){

		DumpProfData();

		Runtime* runtime = Runtime::Current();
		running_ = false;

		LOG(INFO) << "Starting Detaching all the thread Profiling";
		ForEach(GCMMPKillThreadProf, this);

		Thread* self = Thread::Current();
		{
			ThreadList* thread_list = Runtime::Current()->GetThreadList();
			MutexLock mu(self, *Locks::thread_list_lock_);
			thread_list->ForEach(GCMMPResetThreadField, this);
		}
		LOG(INFO) << "Done Detaching all the thread Profiling";
		LOG(INFO) << "Shutting Down";
		if(hasProfDaemon_) { //the PRof Daemon has to be the one doing the shutdown
			MutexLock mu(self, *prof_thread_mutex_);
			prof_thread_cond_->Broadcast(self);
			runtime->DetachCurrentThread();
		}
	}
}

void MProfiler::InitializeProfiler() {
	if(!IsProfilingEnabled())
		return;
	if(IsProfilingRunning()) {
		LOG(INFO) << "MProfiler: was already running";
		return;
	}
	cpu_time_ns_ = ProcessTimeNS();
	start_time_ns_ = uptime_nanos();

	GCPauseThreadManager::startCPUTime = cpu_time_ns_;
	GCPauseThreadManager::startRealTime = start_time_ns_;

	LOG(INFO) << "MProfiler startCPU NS is : " << cpu_time_ns_ << ", statTime: " << start_time_ns_;
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

void MProfiler::SetMProfileFlags(void) {
	OpenDumpFile();
	AttachThreads();
	running_ = true;
//	size_t capacity = MProfiler::kGCMMPMAXThreadCount * sizeof(GCMMPThreadProf);
//  UniquePtr<GCMMPThreadProf> mem_threads_allocated(MemMap::MapAnonymous(
//  		"thredProfileRegion", NULL, capacity, PROT_READ | PROT_WRITE));

}

MProfiler::~MProfiler() {
	if(prof_thread_mutex_ != NULL)
		delete prof_thread_mutex_;
  CHECK_PTHREAD_CALL(pthread_kill, (pthread_, SIGQUIT), "MProfiler shutdown");
  CHECK_PTHREAD_CALL(pthread_join, (pthread_, NULL), "MProfiler shutdown");
}

bool MProfiler::MainProfDaemonExec(){
	Thread* self = Thread::Current();
  // Check if GC is running holding gc_complete_lock_.
  MutexLock mu(self, *prof_thread_mutex_);
  LOG(INFO) << "MProfiler: Profiler Daemon Is going to Wait";
  ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
  {
  	prof_thread_cond_->Wait(self);
  }
  if(receivedSignal_) { //we recived Signal to Shutdown
    LOG(INFO) << "MProfiler: signal Received " << self->GetTid() ;
  	return true;
  } else {
  	return false;
  }
}

void* MProfiler::Run(void* arg) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);


  Runtime* runtime = Runtime::Current();

  mProfiler->hasProfDaemon_ =
  		runtime->AttachCurrentThread("MProfile Daemon", true,
  				runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler());

  CHECK(mProfiler->hasProfDaemon_);

  if(!mProfiler->hasProfDaemon_)
  	return NULL;

  mProfiler->flags_ |= GCMMP_FLAGS_HAS_DAEMON;
  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {

    MutexLock mu(self, *mProfiler->prof_thread_mutex_);
    if(!mProfiler->running_) {

      LOG(INFO) << "MProfiler: Assigning profID to profDaemon " << self->GetTid();
    	mProfiler->prof_thread_ = self;
    	mProfiler->SetMProfileFlags();
    } else {
    	 LOG(INFO) << "MProfiler: Profiler was already created";
    }

    mProfiler->prof_thread_cond_->Broadcast(self);
  }


  LOG(INFO) << "MProfiler: Profiler Daemon Created and Leaving";


  while(true) {
    // Check if GC is running holding gc_complete_lock_.
    if(mProfiler->MainProfDaemonExec())
    	break;
  }

  mProfiler->ShutdownProfiling();

  return NULL;

}


static void GCMMPDumpThreadProf(GCMMPThreadProf* profRec, void* arg) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);
	if(mProfiler != NULL) {
		 LOG(INFO) << "MProfiler_out: " << profRec->GetTid() << ">>>>>>>>>>>";
		 profRec->getPauseMgr()->DumpProfData();
		 LOG(INFO) << "MPr_out: " << profRec->GetTid() << "<<<<<<<<<<<<<<";
	}
}

void MProfiler::DumpProfData(void) {
	uint64_t cuuT = ProcessTimeNS();
	LOG(INFO) << "StartCPUTime =  "<< cpu_time_ns_ << ", cuuCPUT: "<< cuuT;
	cuuT = uptime_nanos();
	LOG(INFO) << "StartTime =  "<< start_time_ns_ << ", cuuT: "<< cuuT;
	ForEach(GCMMPDumpThreadProf, this);
}


void MProfiler::ProcessSignalCatcher(int signalVal) {
	if(signalVal == kGCMMPDumpSignal) {
		Thread* self = Thread::Current();
    MutexLock mu(self, *prof_thread_mutex_);
    receivedSignal_ = true;

    if(!hasProfDaemon_) {
    	ShutdownProfiling();
    }

    // Wake anyone who may have been waiting for the GC to complete.
    prof_thread_cond_->Broadcast(self);

    LOG(INFO) << "MProfiler: Sent the signal " << self->GetTid() ;
	}
}


void MProfiler::MProfileSignalCatcher(int signalVal) {
	if(MProfiler::IsMProfRunning()) {
		Runtime::Current()->mprofiler_->ProcessSignalCatcher(signalVal);
	}
}

void MProfiler::CreateProfilerDaemon(void) {
  // Create a raw pthread; its start routine will attach to the runtime.
	Thread* self = Thread::Current();
	MutexLock mu(self, *prof_thread_mutex_);

  CHECK_PTHREAD_CALL(pthread_create, (&pthread_, NULL, &Run, this),
  		"MProfiler Daemon thread");

  while (prof_thread_ == NULL) {
  	prof_thread_cond_->Wait(self);
  }
  prof_thread_cond_->Broadcast(self);

  LOG(INFO) << "MProfiler: Caller is leaving now";

}

static void GCMMPAttachThread(Thread* t, void* arg) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);
	if(mProfiler != NULL) {
		mProfiler->AttachThread(t);
	}
}




bool MProfiler::ProfiledThreadsContain(Thread* thread) {
	pid_t tId = thread->GetTid();
	for (const auto& threadProf : threadProflist_) {
    if (threadProf->GetTid() == tId) {
      return true;
    }
	}
	return false;
}

/*
 * Attach the thread to the set of the profiled threads
 * We assume that checks already done before we call this
 */
void MProfiler::AttachThread(Thread* thread) {
	LOG(INFO) << "MProfiler: Attaching thread Late " << thread->GetTid();
	GCMMPThreadProf* threadProf = thread->GetProfRec();
	if(threadProf != NULL) {
		if(threadProf->state == GCMMP_TH_RUNNING) {
			LOG(INFO) << "MPRofiler: The Thread was already attached " << thread->GetTid() ;
			return;
		}
	}
	if(thread->GetTid() == prof_thread_->GetTid()){
		if(!IsAttachProfDaemon()) {
			LOG(INFO) << "MProfiler: Skipping profDaemon attached " << thread->GetTid() ;
			return;
		}
	}
	std::string thread_name;
	thread->GetThreadName(thread_name);
	if(thread_name.compare("GCDaemon") == 0) { //that's the GCDaemon
		gc_daemon_ = thread;
		if(!IsAttachGCDaemon()) {
			LOG(INFO) << "MProfiler: Skipping GCDaemon threadProf for " << thread->GetTid() << thread_name;
			return;
		}
	} else if(thread_name.compare("main") == 0) { //that's the main thread
		main_thread_ = thread;
	}
	LOG(INFO) << "MProfiler: Initializing threadProf for " << thread->GetTid() << thread_name;
	threadProf = new GCMMPThreadProf(this, thread);
	threadProflist_.push_back(threadProf);
	thread->SetProfRec(threadProf);
}

bool MProfiler::DettachThread(GCMMPThreadProf* threadProf) {
	if(threadProf != NULL) {
//		LOG(INFO) << "MProfiler: Detaching thread from List " << threadProf->GetTid();
		return threadProf->StopProfiling();
	}
	return false;
}

void MProfiler::AttachThreads() {
//
//	 thread_list->SuspendAll();
//	 thread_list()->ForEach(GCMMPAttachThread, this);
//
//	 thread_list->ResumeAll();

	Thread* self = Thread::Current();
	LOG(INFO) << "MProfiler: Attaching All threads " << self->GetTid();
	ThreadList* thread_list = Runtime::Current()->GetThreadList();
	MutexLock mu(self, *Locks::thread_list_lock_);
	thread_list->ForEach(GCMMPAttachThread, this);
	LOG(INFO) << "MProfiler: Done Attaching All threads ";

}

void MProfiler::ForEach(void (*callback)(GCMMPThreadProf*, void*), void* context) {
  for (const auto& profRec : threadProflist_) {
    callback(profRec, context);
  }
}

void MProfiler::OpenDumpFile() {
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
		LOG(INFO) << "MProfiler did not find a target VM for " << name << " " << GCMMP_ARRAY_SIZE(benchmarks);
	}
}

void MProfiler::PreForkPreparation() {
	dvmGCMMPSetName = dvmGCMMProfPerfCounters;
}

/*
 * Attach a thread from the MProfiler
 */
void MProfiler::MProfAttachThread(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		Runtime::Current()->mprofiler_->AttachThread(th);
	}
}

/*
 * Detach a thread from the MProfiler
 */
void MProfiler::MProfDetachThread(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		if(Runtime::Current()->mprofiler_->DettachThread(th->GetProfRec())) {
			th->SetProfRec(NULL);
			LOG(INFO) << "MProfiler: Detaching thread from List " << th->GetTid();
		}
	}
}

void MProfiler::MarkWaitTimeEvent(GCMMPThreadProf* profRec,
		GCMMP_BREAK_DOWN_ENUM evType) {
	profRec->getPauseMgr()->MarkStartTimeEvent(evType);
}

void MProfiler::MarkEndWaitTimeEvent(GCMMPThreadProf* profRec,
		GCMMP_BREAK_DOWN_ENUM evType) {
	profRec->getPauseMgr()->MarkEndTimeEvent(evType);
}

/*
 * Detach a thread from the MProfiler
 */
void MProfiler::MProfMarkWaitTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf, GCMMP_GC_BRK_WAIT_CONC);
	}
}
/*
 * Detach a thread from the MProfiler
 */
void MProfiler::MProfMarkEndWaitTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_WAIT_CONC);
	}
}

void MProfiler::MProfMarkGCHatTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}

void MProfiler::MProfMarkEndGCHatTimeEvent(art::Thread* th){
	if(MProfiler::IsMProfRunning()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}


void MProfiler::MProfMarkGCExplTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void MProfiler::MProfMarkEndGCExplTimeEvent(art::Thread* th){
	if(MProfiler::IsMProfRunning()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void MProfiler::MProfMarkSuspendTimeEvent(art::Thread* th, art::ThreadState thState){
	if(MProfiler::IsMProfRunning()) {
		if(thState == kSuspended) {
			GCMMPThreadProf* thProf = th->GetProfRec();
			if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
				Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf,
						GCMMP_GC_BRK_SUSPENSION);
				return;
			}
		}
	}
	return;
}

void MProfiler::MProfMarkEndSuspendTimeEvent(art::Thread* th, art::ThreadState thState){
	if(MProfiler::IsMProfRunning()) {
		if(thState == kSuspended) {
			GCMMPThreadProf* thProf = th->GetProfRec();
			if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
				Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
						GCMMP_GC_BRK_SUSPENSION);
			}
		}
	}
}


/*
 * Return true only when the MProfiler is Running
 */
bool MProfiler::IsMProfRunning() {
	MProfiler* mP = Runtime::Current()->mprofiler_;
	if(mP != NULL)
		return mP->IsProfilingRunning();
	return false;
}


}// namespace mprofiler
}// namespace art

void dvmGCMMProfPerfCounters(const char* vmName){
	art::mprofiler::MProfiler* mProfiler =
			art::Runtime::Current()->GetMProfiler();
	if(mProfiler != NULL) {
		mProfiler->GCMMProfPerfCounters(vmName);
	}

}
