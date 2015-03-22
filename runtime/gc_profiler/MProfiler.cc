/*
 * MPprofiler.cc
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#include <string>
#include <pthread.h>
#include <fcntl.h>

#include "locks.h"
#include "base/unix_file/fd_file.h"
#include "cutils/sched_policy.h"
#include "cutils/process_name.h"
#include "cutils/system_clock.h"
#include "gc/heap.h"
#include "gc_profiler/MPPerfCounters.h"
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
#include "utils.h"
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

const char * VMProfiler::gcMMPRootPath[] = {
		"/sdcard/gcperf/", "/data/anr/"
};

const GCMMPProfilingEntry MProfiler::profilTypes[] = {
		{
				 0x00,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "CYCLES", "Perf Counter of CPU over a given period of time",
				 "PERF_CPU_USAGE.log",
				 NULL,
				 &createVMProfiler<PerfCounterProfiler>
		},//Cycles
		{
				 0x01,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "INSTRUCTIONS", "Perf Counter of Instructions over a given period of time",
				 "PERF_INSTRUCTIONS.log",
				 NULL,
				 &createVMProfiler<PerfCounterProfiler>
		},//Instructions
		{
				 0x02,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "L1I_ACCESS", "Perf Counter of L1I_ACCESS over a given period of time",
				 "PERF_IL1_ACCESS.log",
				 NULL,
				 &createVMProfiler<PerfCounterProfiler>
		},//L1I_ACCESS
		{
				 0x03,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "L1I_MISS", "Perf Counter of L1I_MISS over a given period of time",
				 "PERF_IL1_MISS.log",
				 NULL,
				 &createVMProfiler<PerfCounterProfiler>
		},//L1I_MISS
		{
				 0x04,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "L1D_ACCESS", "Perf Counter of L1D_ACCESS over a given period of time",
				 "PERF_DL1_ACCESS.log",
				 NULL,
				 &createVMProfiler<PerfCounterProfiler>
		},//L1D_ACCESS
		{
				 0x05,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "L1D_MISS", "Perf Counter of L1D_MISS over a given period of time",
				 "PERF_DL1_MISS.log",
				 NULL,
				 &createVMProfiler<PerfCounterProfiler>
		},//L1D_MISS
		{
				 0x0D,
				 GCMMP_FLAGS_CREATE_DAEMON,
				 "MMU", "MMU over a given period of time",
				 "PERF_MMU_REF.log",
				 NULL,
				 &createVMProfiler<MMUProfiler>
		}//MMU

};//profilTypes

uint64_t GCPauseThreadManager::startCPUTime = 0;
uint64_t GCPauseThreadManager::startRealTime = 0;
VMProfiler* GCMMPThreadProf::mProfiler = NULL;


const int MProfiler::kGCMMPDumpEndMarker = -99999999;

uint64_t GCPauseThreadManager::GetRelevantRealTime(void)  {
	return uptime_nanos() - GCPauseThreadManager::startRealTime;
}

uint64_t GCPauseThreadManager::GetRelevantCPUTime(void)  {
	return ProcessTimeNS() - GCPauseThreadManager::startCPUTime;
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
	if(busy_) {
		if(curr_marker_->type != evType)
			return;
		curr_marker_->finalMarker = GCPauseThreadManager::GetRelevantCPUTime();
		IncrementIndices();
		count_opens_--;
	}
}

void GCPauseThreadManager::DumpProfData(void* args) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(args);

	art::File* file = mProfiler->GetDumpFile();
	int totalC = 0;
	if(curr_bucket_ind_ < 0)
		return;
	GCMMP_VLOG(INFO) << "parenthesis: " << count_opens_;
	for(int bucketInd = 0; bucketInd <= curr_bucket_ind_; bucketInd++) {
		int limit_ = (bucketInd == curr_bucket_ind_) ? curr_entry_:kGCMMPMaxEventEntries;
		if(limit_ > 0) {
			//file->WriteFully(pauseEvents[bucketInd], limit_ * sizeof(GCPauseThreadMarker));
			for(int entryInd = 0; entryInd < limit_; entryInd++) {
				file->WriteFully(&pauseEvents[bucketInd][entryInd], sizeof(GCMMP_ProfileActivity));
				GCMMP_VLOG(INFO) << "pMgr " << totalC++ << ": " << pauseEvents[bucketInd][entryInd].type << ", " << pauseEvents[bucketInd][entryInd].startMarker << ", " << pauseEvents[bucketInd][entryInd].finalMarker;
			}
		}
	}
	file->WriteFully(&mprofiler::MProfiler::kGCMMPDumpEndMarker, sizeof(int));
}

int GCMMPThreadProf::GetThreadType(void) {
	if(GCMMPThreadProf::mProfiler->GetMainID() == GetTid()) {
		return 1;
	}
	if(GCMMPThreadProf::mProfiler->GetGCDaemonID() == GetTid()) {
		return 2;
	}
	return 0;
}

void GCMMPThreadProf::readPerfCounter(int32_t val) {
	if(GetPerfRecord() == NULL)
		return;
	if(state == GCMMP_TH_RUNNING) {
		GetPerfRecord()->storeReading(val);
	}

}


void GCMMPThreadProf::readPerfCounter(int32_t val, uint64_t* totalVals, uint64_t* gcVals) {
	if(GetPerfRecord() == NULL)
		return;
	if(state == GCMMP_TH_RUNNING) {
		GetPerfRecord()->storeReading(val);
		GetPerfRecord()->getGCMarks(gcVals);
	}
	*totalVals +=  GetPerfRecord()->data;
}

uint64_t GCMMPThreadProf::getDataPerfCounter(void) {
	if(GetPerfRecord() == NULL)
		return 0;
	return GetPerfRecord()->data;
}


GCMMPThreadProf::GCMMPThreadProf(VMProfiler* vmProfiler, Thread* thread)
	: pid(thread->GetTid()),
	  suspendedGC(false),
	  pauseManager(NULL),
	  state(GCMMP_TH_STARTING) {

	GCMMP_VLOG(INFO) << "VMProfiler: Initializing arrayBreaks for " << thread->GetTid();
//	for(int _iter = GCMMP_GC_BRK_SUSPENSION; _iter < GCMMP_GC_BRK_MAXIMUM; _iter++) {
//		memset((void*) &timeBrks[_iter], 0, sizeof(GCMMP_ProfileActivity));
//	}
	GCMMP_VLOG(INFO) << "VMProfiler: Done Initializing arrayBreaks for " << thread->GetTid();
//	pauseManager = new GCPauseThreadManager();
	setThreadTag(GCMMP_THREAD_DEFAULT);
	perf_record_ = vmProfiler->createHWCounter(thread);
	state = GCMMP_TH_RUNNING;

	lifeTime_.startMarker = GCMMPThreadProf::mProfiler->GetRelevantCPUTime();
	lifeTime_.finalMarker = 0;
	GCMMP_VLOG(INFO) << "VMProfiler : ThreadProf is initialized";
}

GCMMPThreadProf::GCMMPThreadProf(MProfiler* mProfiler, Thread* thread)
	: pid(thread->GetTid()),
	  suspendedGC(false),
	  pauseManager(NULL),
	  state(GCMMP_TH_STARTING) {

	GCMMP_VLOG(INFO) << "MPRofiler: Initializing arrayBreaks for " << thread->GetTid();
	for(int _iter = GCMMP_GC_BRK_SUSPENSION; _iter < GCMMP_GC_BRK_MAXIMUM; _iter++) {
		memset((void*) &timeBrks[_iter], 0, sizeof(GCMMP_ProfileActivity));
	}
	GCMMP_VLOG(INFO) << "MPRofiler: Done Initializing arrayBreaks for " << thread->GetTid();
	pauseManager = new GCPauseThreadManager();
	perf_record_ = MPPerfCounter::Create("CYCLES");
	state = GCMMP_TH_RUNNING;
	lifeTime_.startMarker = GCMMPThreadProf::mProfiler->GetRelevantCPUTime();
	lifeTime_.finalMarker = 0;
	GCMMP_VLOG(INFO) << "MProfiler : ThreadProf is initialized";
}

GCMMPThreadProf::~GCMMPThreadProf() {

}


bool GCMMPThreadProf::StopProfiling(void) {
	if(state == GCMMP_TH_RUNNING) {
		state = GCMMP_TH_STOPPED;
		lifeTime_.finalMarker = GCMMPThreadProf::mProfiler->GetRelevantCPUTime();
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


MPPerfCounter* MMUProfiler::createHWCounter(Thread* thread) {
	GCMMP_VLOG(INFO) << "MMUProfiling: creating hwCount";
	return NULL;
}

MPPerfCounter* PerfCounterProfiler::createHWCounter(Thread* thread) {
	GCMMP_VLOG(INFO) << "PerfCounterProfiler: creating hwCount: " << hwEvent_;
	MPPerfCounter* _perfCounter = MPPerfCounter::Create(hwEvent_);

	_perfCounter->OpenPerfLib(thread->GetTid());

	return _perfCounter;
}


//MPPerfCounter* VMProfiler::createHWCounter(Thread* thread) {
//	GCMMP_VLOG(INFO) << "VMProfiler: createHWCounter";
//	return NULL;
//}

void VMProfiler::startProfiling(void) {
	if(!IsProfilingEnabled())
		return;
	if(IsProfilingRunning()) {
		GCMMP_VLOG(INFO) << "VMProfiler: was already running";
		return;
	}

	if(IsCreateProfDaemon()) { //create daemon thread
		createProfDaemon();
	} else { //init without daemon thread
		InitCommonData();
	}
}


void VMProfiler::updateHeapAllocStatus(void) {
	gc::Heap* heap_ = Runtime::Current()->GetHeap();


	int32_t _allocBytes = total_alloc_bytes_.load();

	heapStatus.index = 1.0 * (_allocBytes >> kGCMMPLogAllocWindow);
	heapStatus.timeInNsec = GetRelevantCPUTime();
	heapStatus.allocatedBytes = _allocBytes;
	heapStatus.currAllocBytes = heap_->GetBytesAllocated();
	heapStatus.concurrentStartBytes = heap_->GetConcStartBytes();
	heapStatus.currFootPrint = heap_->GetMaxAllowedFootPrint();


}

void VMProfiler::notifyAllocation(size_t allocSize) {
	if(!IsAllocWindowsSet())
		return;
	int32_t initValue = total_alloc_bytes_.load();
	double _newIndex =  1.0 * ((initValue + allocSize) >> kGCMMPLogAllocWindow);

	total_alloc_bytes_.fetch_add(allocSize);

	if((_newIndex) != (getAllocIndex())) {

		GCMMP_VLOG(INFO) << "VMProfiler: allocation Window: " << total_alloc_bytes_.load();


		{
			Thread* self = Thread::Current();
	    MutexLock mu(self, *prof_thread_mutex_);
	    receivedSignal_ = true;

	    if(hasProfDaemon()) {
	    	prof_thread_cond_->Broadcast(self);
	    }

	    // Wake anyone who may have been waiting for the GC to complete.


	    GCMMP_VLOG(INFO) << "VMProfiler: Sent the signal for allocation:" << self->GetTid() ;
		}

	}
}

void PerfCounterProfiler::addHWStartEvent(GCMMP_BREAK_DOWN_ENUM evt){
	Thread* self = Thread::Current();
	GCMMPThreadProf* _profRec =  self->GetProfRec();
	if(_profRec != NULL && _profRec->state == GCMMP_TH_RUNNING) {
		_profRec->perf_record_->addStartEvent(evt);
	}
}
void PerfCounterProfiler::addHWEndEvent(GCMMP_BREAK_DOWN_ENUM evt){
	Thread* self = Thread::Current();
	GCMMPThreadProf* _profRec =  self->GetProfRec();
	if(_profRec != NULL && _profRec->state == GCMMP_TH_RUNNING) {
		_profRec->perf_record_->addEndEvent(evt);
	}
}

bool MMUProfiler::dettachThread(GCMMPThreadProf* thProf){
	return true;
}

bool PerfCounterProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "VMProfiler -- dettaching thread pid: " << thProf->GetTid();
		if(thProf->GetPerfRecord() != NULL) {
			int32_t currBytes_ = total_alloc_bytes_.load();
			thProf->readPerfCounter(currBytes_);
			thProf->GetPerfRecord()->ClosePerfLib();
			//thProf->resetPerfRecord();
		}
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}


VMProfiler::VMProfiler(GCMMP_Options* argOptions,
		void* entry) :
				index_(argOptions->mprofile_type_),
				enabled_((argOptions->mprofile_type_ != MProfiler::kGCMMPDisableMProfile)),
				gcDaemonAffinity_(argOptions->mprofile_gc_affinity_),
				prof_thread_(NULL),
				main_thread_(NULL),
				gc_daemon_(NULL),
				running_(false),
				receivedSignal_(false),
				start_heap_bytes_(0) {
	if(IsProfilingEnabled()) {
		size_t _loop = 0;
		bool _found = false;
		for(_loop = 0; _loop < GCMMP_ARRAY_SIZE(MProfiler::profilTypes); _loop++) {
			if(MProfiler::profilTypes[_loop].id_ == index_) {
				_found = true;
				break;
			}
		}
		if(_found) {
			const GCMMPProfilingEntry* profEntry = &MProfiler::profilTypes[_loop];
			resetHeapAllocStatus();
			flags_ = profEntry->flags_;
			dump_file_name_ = profEntry->logFile_;
			prof_thread_mutex_ = new Mutex("MProfile Thread lock");
			prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
																										*prof_thread_mutex_));
			setReceivedShutDown(false);
		} else {
			LOG(ERROR) << "VMprofile index is not supported";
		}
	}
	LOG(ERROR) << "VMProfiler : VMProfiler";
}

void VMProfiler::InitCommonData() {
	OpenDumpFile();

	start_heap_bytes_ = getRelevantAllocBytes();
	cpu_time_ns_ = ProcessTimeNS();
	start_time_ns_ = uptime_nanos();

	attachThreads();

	setIsProfilingRunning(true);
}


void VMProfiler::OpenDumpFile() {
	for (size_t i = 0; i < GCMMP_ARRAY_SIZE(gcMMPRootPath); i++) {
		char str[256];
		strcpy(str, gcMMPRootPath[i]);
		strcat(str, dump_file_name_);


		int fd = open(str, O_RDWR | O_APPEND | O_CREAT, 0777);
	  if (fd == -1) {
	    PLOG(ERROR) << "Unable to open MProfile Output file '" << str << "'";
	    continue;
	  }
    GCMMP_VLOG(INFO) << "opened  Successsfully MProfile Output file '" << str << "'";
    dump_file_ = new File(fd, std::string(dump_file_name_));
    return;
	}
}

static void GCMMPVMAttachThread(Thread* t, void* arg) {
	VMProfiler* vmProfiler = reinterpret_cast<VMProfiler*>(arg);
	if(vmProfiler != NULL) {
		vmProfiler->attachSingleThread(t);
	}
}


void VMProfiler::attachSingleThread(Thread* thread) {
	LOG(ERROR) << "VMProfiler: Attaching thread: " << thread->GetTid();
	GCMMPThreadProf* threadProf = thread->GetProfRec();
	if(threadProf != NULL) {
		if(threadProf->state == GCMMP_TH_RUNNING) {
			GCMMP_VLOG(INFO) << "VMProfiler: The Thread was already attached " << thread->GetTid() ;
			return;
		}
	}
	if(thread->GetTid() == prof_thread_->GetTid()) {
		if(!IsAttachProfDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping profDaemon attached " << thread->GetTid() ;
			return;
		}
	}

	std::string thread_name;
	thread->GetThreadName(thread_name);
	GCMMPThProfileTag _tag = GCMMP_THREAD_DEFAULT;
	if(thread_name.compare("GCDaemon") == 0) { //that's the GCDaemon
		setGcDaemon(thread);

		setThreadAffinity(thread, false);
		if(!IsAttachGCDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCDaemon threadProf for " << thread->GetTid() << thread_name;
			return;
		}
		LOG(ERROR) << "vmprofiler: Attaching GCDaemon: " << thread->GetTid();
		_tag = GCMMP_THREAD_GCDAEMON;
	} else {
		if(thread_name.compare("HeapTrimmerDaemon") == 0) {
			setGcTrimmer(thread);
			setThreadAffinity(thread, false);
			if(!IsAttachGCDaemon()) {
				GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCTrimmer threadProf for " << thread->GetTid() << thread_name;
				return;
			}
			LOG(ERROR) << "vmprofiler: Attaching TimerDaemon: " << thread->GetTid();
			_tag = GCMMP_THREAD_GCTRIM;
		} else if(thread_name.compare("main") == 0) { //that's the main thread
			setMainThread(thread);
			_tag = GCMMP_THREAD_MAIN;
			setThreadAffinity(thread, true);
		}

	}

	GCMMP_VLOG(INFO) << "VMProfiler: Initializing threadProf for " << thread->GetTid() << thread_name;
	threadProf = new GCMMPThreadProf(this, thread);
	threadProf->setThreadTag(_tag);
	threadProfList_.push_back(threadProf);
	thread->SetProfRec(threadProf);
}


void VMProfiler::updateHeapPerfStatus(uint64_t totalVals, uint64_t gcVals) {
	heapStatus.totalMetric = totalVals;
	heapStatus.gcDaemonUsage = (gcVals * 1.0) /totalVals;
	heapStatus.gcMutUsage = 0.0;
}

void PerfCounterProfiler::getPerfData() {
	int32_t currBytes_ = total_alloc_bytes_.load();
	uint64_t _totalVals = 0;
	uint64_t _gcVals = 0;
	//gc::Heap* heap_ = Runtime::Current()->GetHeap();

	//LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " << heap_->GetBytesAllocated() << ", concBytes: " <<heap_->GetConcStartBytes() << ", footPrint: " << heap_->GetMaxAllowedFootPrint();
	for (const auto& threadProf : threadProfList_) {
		threadProf->readPerfCounter(currBytes_, &_totalVals, &_gcVals);
	}
	updateHeapPerfStatus(_totalVals, _gcVals);
}

void PerfCounterProfiler::logPerfData() {
	int32_t currBytes_ = total_alloc_bytes_.load();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " << heap_->GetBytesAllocated() << ", concBytes: " <<heap_->GetConcStartBytes() << ", footPrint: " << heap_->GetMaxAllowedFootPrint();
	uint64_t _sumData = 0;
	uint64_t _sumGc = 0;
	uint64_t _data = 0;
	for (const auto& threadProf : threadProfList_) {
		_data = threadProf->getDataPerfCounter();
		if(threadProf->getThreadTag() > GCMMP_THREAD_MAIN) {
			_sumGc += _data;
			LOG(ERROR) << "logging specific gcThreadProf: " << threadProf->getThreadTag() << ", tid:" << threadProf->GetTid();
		}
		LOG(ERROR) << "logging thid: "<< threadProf->GetTid() << ", "<< _data;
		//threadProf->GetPerfRecord()->dumpMarks();
		threadProf->GetPerfRecord()->getGCMarks(&_sumGc);
		_sumData += _data;
	}
	LOG(ERROR) << "currBytes: " << currBytes_ <<", sumData= "<< _sumData << ", sumGc=" << _sumGc <<", ration="<< ((_sumGc*100.0)/_sumData);
}



bool PerfCounterProfiler::periodicDaemonExec(void){
	Thread* self = Thread::Current();
  // Check if GC is running holding gc_complete_lock_.
  MutexLock mu(self, *prof_thread_mutex_);
  ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
  {
  	prof_thread_cond_->Wait(self);
  }
  if(receivedSignal_) { //we recived Signal to Shutdown
    GCMMP_VLOG(INFO) << "VMProfiler: signal Received " << self->GetTid() ;
    //LOG(ERROR) << "periodic daemon recieved signals tid: " <<  self->GetTid();
    updateHeapAllocStatus();
    getPerfData();
    receivedSignal_ = false;

    if(getRecivedShutDown()) {
    	LOG(ERROR) << "received shutdown tid: " <<  self->GetTid();
    	logPerfData();
    }
  	return getRecivedShutDown();
  } else {
  	return false;
  }
}

bool MMUProfiler::periodicDaemonExec(void){
	return true;
}

void* VMProfiler::runDaemon(void* arg) {
	VMProfiler* mProfiler = reinterpret_cast<VMProfiler*>(arg);


  Runtime* runtime = Runtime::Current();

  mProfiler->setProfDaemon(
  		runtime->AttachCurrentThread("VMProfile", true,
  				runtime->GetSystemThreadGroup(),
      !runtime->IsCompiler()));

  CHECK(mProfiler->hasProfDaemon());

  if(!mProfiler->hasProfDaemon())
  	return NULL;

  LOG(ERROR) << "starting the profiler daemon";

  mProfiler->flags_ |= GCMMP_FLAGS_HAS_DAEMON;
  Thread* self = Thread::Current();
  DCHECK_NE(self->GetState(), kRunnable);
  {

    MutexLock mu(self, *mProfiler->prof_thread_mutex_);
    if(!mProfiler->IsProfilingRunning()) {

    	LOG(ERROR) << "VMProfiler: Assigning profID to profDaemon " << self->GetTid();
    	mProfiler->prof_thread_ = self;
    	mProfiler->InitCommonData();
    } else {
    	 GCMMP_VLOG(INFO) << "VMProfiler: Profiler was already created";
    }

    mProfiler->prof_thread_cond_->Broadcast(self);
  }


  GCMMP_VLOG(INFO) << "VMProfiler: Profiler Daemon Created and Leaving";


  while(!mProfiler->getRecivedShutDown()) {
    // Check if GC is running holding gc_complete_lock_.
    mProfiler->periodicDaemonExec();

  }
  LOG(ERROR) << "the daemon exiting the loop";
  //const char* old_cause = self->StartAssertNoThreadSuspension("Handling SIGQUIT");
  //ThreadState old_state =
  //self->SetStateUnsafe(kRunnable);
  mProfiler->ShutdownProfiling();

  return NULL;
}

void VMProfiler::attachThreads(){
	Thread* self = Thread::Current();
	GCMMP_VLOG(INFO) << "VMProfiler: Attaching All threads " << self->GetTid();
	ThreadList* thread_list = Runtime::Current()->GetThreadList();
	MutexLock mu(self, *Locks::thread_list_lock_);
	thread_list->ForEach(GCMMPVMAttachThread, this);
	GCMMP_VLOG(INFO) << "VMProfiler: Done Attaching All threads ";
}

size_t VMProfiler::getRelevantAllocBytes(void) {
	return Runtime::Current()->GetHeap()->GetBytesAllocatedEver() - start_heap_bytes_;
}





void VMProfiler::createProfDaemon(){
	//if(IsCreateProfDaemon()) { //create daemon
	  // Create a raw pthread; its start routine will attach to the runtime.
		Thread* self = Thread::Current();
		MutexLock mu(self, *prof_thread_mutex_);
		GCMMP_VLOG(INFO) << "VMProfiler: Creating VMProfiler Daemon";
	  CHECK_PTHREAD_CALL(pthread_create, (&pthread_, NULL, &runDaemon, this),
	  		"VMProfiler Daemon thread");

	  while (prof_thread_ == NULL) {
	  	prof_thread_cond_->Wait(self);
	  }
	  prof_thread_cond_->Broadcast(self);

	  GCMMP_VLOG(INFO) << "VMProfiler: Caller is leaving now";

//	}
}

PerfCounterProfiler::PerfCounterProfiler(GCMMP_Options* argOptions,
		void* entry): VMProfiler(argOptions, entry){
	GCMMPProfilingEntry* _entry = (GCMMPProfilingEntry*) entry;
	if(initCounters(_entry->name_) != 0) {
		LOG(ERROR) << "PerfCounterProfiler : init counters returned error";
	} else {
		hwEvent_ = _entry->name_;
		LOG(ERROR) << "PerfCounterProfiler : init counters returned valid..evtName=" << hwEvent_;

	}
	LOG(ERROR) << "PerfCounterProfiler : Initializer";
}

int PerfCounterProfiler::initCounters(const char* evtName){
	init_perflib_counters();
	return 0;
}


MMUProfiler::MMUProfiler(GCMMP_Options* argOptions,
		void* entry): VMProfiler(argOptions, entry){

	LOG(ERROR) << "VMProfiler : MMUProfiler";
}


// Member functions definitions including constructor
MProfiler::MProfiler(GCMMP_Options* argOptions)
		: index_(argOptions->mprofile_type_),
		main_thread_(NULL),
		gc_daemon_(NULL),
		prof_thread_(NULL),
		gcDaemonAffinity_(argOptions->mprofile_gc_affinity_),
		enabled_((argOptions->mprofile_type_ != MProfiler::kGCMMPDisableMProfile)),
		running_(false),
		receivedSignal_(false),
		start_heap_bytes_(0)
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
		GCMMP_VLOG(INFO) << "MProfiler Profiling is Enabled";
		prof_thread_mutex_ = new Mutex("MProfile Thread lock");
		prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
																									*prof_thread_mutex_));
		vmProfile = profEntry->creator_(argOptions, (void*)profEntry);
		GCMMP_VLOG(INFO) << "MProfiler Created";

	} else {
		flags_ = 0;
		dump_file_name_ = NULL;
		GCMMP_VLOG(INFO) << "MProfiler Profiling is Disabled";
	}

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
		end_heap_bytes_ = GetRelevantAllocBytes();
		end_cpu_time_ns_ = GetRelevantCPUTime();
		end_time_ns_ = GetRelevantRealTime();
		running_ = false;

		GCMMP_VLOG(INFO) << "Starting Detaching all the thread Profiling";
		ForEach(GCMMPKillThreadProf, this);

		DumpProfData(true);

		Runtime* runtime = Runtime::Current();


		Thread* self = Thread::Current();
		{
			ThreadList* thread_list = Runtime::Current()->GetThreadList();
			MutexLock mu(self, *Locks::thread_list_lock_);
			thread_list->ForEach(GCMMPResetThreadField, this);
		}
		GCMMP_VLOG(INFO) << "Done Detaching all the thread Profiling";
		GCMMP_VLOG(INFO) << "Shutting Down";
		if(hasProfDaemon_) { //the Prof Daemon has to be the one doing the shutdown
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
		GCMMP_VLOG(INFO) << "MProfiler: was already running";
		return;
	}
	GCMMPThreadProf::mProfiler = this;
	start_heap_bytes_ = GetRelevantAllocBytes();
	cpu_time_ns_ = ProcessTimeNS();
	start_time_ns_ = uptime_nanos();

	GCPauseThreadManager::startCPUTime = cpu_time_ns_;
	GCPauseThreadManager::startRealTime = start_time_ns_;

	GCMMP_VLOG(INFO) << "MProfiler startCPU NS is : " << cpu_time_ns_ << ", statTime: " << start_time_ns_;
	if(IsCreateProfDaemon()){
		CreateProfilerDaemon();
	} else {
		GCMMP_VLOG(INFO) << "MProfiler: No Daemon Creation";
		Thread* self = Thread::Current();
		MutexLock mu(self, *prof_thread_mutex_);
		if(!running_) {
			SetMProfileFlags();
		} else {
			GCMMP_VLOG(INFO) << "MProfiler: was already running";
		}
		prof_thread_cond_->Broadcast(self);
	}

	GCMMP_VLOG(INFO) << "MProfiler Is Initialized";
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
  GCMMP_VLOG(INFO) << "MProfiler: Profiler Daemon Is going to Wait";
  ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
  {
  	prof_thread_cond_->Wait(self);
  }
  if(receivedSignal_) { //we recived Signal to Shutdown
    GCMMP_VLOG(INFO) << "MProfiler: signal Received " << self->GetTid() ;
  	return true;
  } else {
  	return false;
  }
}

void VMProfiler::setThreadAffinity(art::Thread* th, bool complementary) {
	if(IsSetAffinityThread()) {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		uint32_t _cpuCount = (uint32_t) sysconf(_SC_NPROCESSORS_CONF);
		uint32_t _cpu_id =  (uint32_t) gcDaemonAffinity_;
		if(complementary) {
			for(uint32_t _ind = 0; _ind < _cpuCount; _ind++) {
				if(_ind != _cpu_id)
					CPU_SET(_ind, &mask);
			}
		} else {
			CPU_SET(_cpu_id, &mask);
		}
		if(sched_setaffinity(th->GetTid(),
												sizeof(mask), &mask) != 0) {
			if(complementary) {
				GCMMP_VLOG(INFO) << "GCMMP: Complementary";
			}
			LOG(ERROR) << "GCMMP: Error in setting thread affinity tid:" << th->GetTid() << ", cpuid: " <<  _cpu_id;
		} else {
			if(complementary) {
				GCMMP_VLOG(INFO) << "GCMMP: Complementary";
			}
			GCMMP_VLOG(INFO) << "GCMMP: Succeeded in setting assignments tid:" << th->GetTid() << ", cpuid: " <<  _cpu_id;
		}
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

      GCMMP_VLOG(INFO) << "MProfiler: Assigning profID to profDaemon " << self->GetTid();
    	mProfiler->prof_thread_ = self;
    	mProfiler->SetMProfileFlags();
    } else {
    	 GCMMP_VLOG(INFO) << "MProfiler: Profiler was already created";
    }

    mProfiler->prof_thread_cond_->Broadcast(self);
  }


  GCMMP_VLOG(INFO) << "MProfiler: Profiler Daemon Created and Leaving";


  while(true) {
    // Check if GC is running holding gc_complete_lock_.
    if(mProfiler->MainProfDaemonExec())
    	break;
  }
  //const char* old_cause = self->StartAssertNoThreadSuspension("Handling SIGQUIT");
  //ThreadState old_state =
  //self->SetStateUnsafe(kRunnable);
  mProfiler->ShutdownProfiling();

  return NULL;

}


void VMProfiler::setReceivedShutDown(bool val){
	receivedShutdown_ = val;
}

bool VMProfiler::getRecivedShutDown(void) {
	return receivedShutdown_;
}
void VMProfiler::ShutdownProfiling(void) {
//	LOG(ERROR) << "ShutDownProfiling:" << Thread::Current()->GetTid();
	 LOG(ERROR) << "VMProfiler: shutting down " << Thread::Current()->GetTid() ;
	 if(hasProfDaemon()) {
		 Runtime* runtime = Runtime::Current();
		 runtime->DetachCurrentThread();
		 setProfDaemon(false);
	 }
}


static void GCMMPDumpMMUThreadProf(GCMMPThreadProf* profRec, void* arg) {
	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);
	if(mProfiler != NULL) {

		 GCPauseThreadManager* mgr = profRec->getPauseMgr();
		 if(!mgr->HasData())
			 return;
		 art::File* f = mProfiler->GetDumpFile();
		 int _pid = profRec->GetTid();
		 int _type = profRec->GetThreadType();
		 f->WriteFully(&_pid, sizeof(int));
		 f->WriteFully(&_type, sizeof(int));
		 f->WriteFully(profRec->GetliveTimeInfo(), sizeof(GCMMP_ProfileActivity));

		 GCMMP_VLOG(INFO) << "MProfiler_out: " << profRec->GetTid() << ">>>>>>>>>>>";

		 mgr->DumpProfData(mProfiler);
		 GCMMP_VLOG(INFO) << "MPr_out: " << profRec->GetTid() << "<<<<<<<<<<<<<<";


	}
}

void MProfiler::DumpProfData(bool isLastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
  GCMMP_VLOG(INFO) << " Dumping the commin information ";
  bool successWrite = dump_file_->WriteFully(&start_heap_bytes_, sizeof(size_t));
  if(successWrite) {
  	successWrite = dump_file_->WriteFully(&start_heap_bytes_, sizeof(size_t));
  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
  }
  if(successWrite) {
  	successWrite = dump_file_->WriteFully(&cpu_time_ns_, sizeof(uint64_t));
  }

  GCMMP_VLOG(INFO) << " Dumping the MMU information ";

  if(successWrite) {
  	successWrite = dump_file_->WriteFully(&cpu_time_ns_, sizeof(uint64_t));
  }
  if(successWrite) {
  	successWrite = dump_file_->WriteFully(&end_cpu_time_ns_, sizeof(uint64_t));
  }

  if(successWrite) {
  	ForEach(GCMMPDumpMMUThreadProf, this);
  }

  if(successWrite) {
  	successWrite = dump_file_->WriteFully(&mprofiler::MProfiler::kGCMMPDumpEndMarker, sizeof(uint64_t));
  }

	if(isLastDump) {
		dump_file_->WriteFully(&mprofiler::MProfiler::kGCMMPDumpEndMarker, sizeof(int));
		dump_file_->Close();
	}
	GCMMP_VLOG(INFO) << " ManagerCPUTime: " << GCPauseThreadManager::GetRelevantCPUTime();
	GCMMP_VLOG(INFO) << " ManagerRealTime: " << GCPauseThreadManager::GetRelevantRealTime();
	uint64_t cuuT = ProcessTimeNS();
	GCMMP_VLOG(INFO) << "StartCPUTime =  "<< cpu_time_ns_ << ", cuuCPUT: "<< cuuT;
	cuuT = uptime_nanos();
	GCMMP_VLOG(INFO) << "StartTime =  "<< start_time_ns_ << ", cuuT: "<< cuuT;

	GCMMP_VLOG(INFO) << " startBytes = " << start_heap_bytes_ << ", cuuBytes = " << GetRelevantAllocBytes();
}


void VMProfiler::ProcessSignalCatcher(int signalVal) {
	if(signalVal == kGCMMPDumpSignal) {
		Thread* self = Thread::Current();
    MutexLock mu(self, *prof_thread_mutex_);
    receivedSignal_ = true;
    setReceivedShutDown(true);


    if(hasProfDaemon()){
    	LOG(ERROR) << "processSignalCatcher found that there is profDaemon";
    } else {
    	LOG(ERROR) << "processSignalCatcher shutting Down";
    	ShutdownProfiling();
    }

    // Wake anyone who may have been waiting for the GC to complete.
    prof_thread_cond_->Broadcast(self);

    GCMMP_VLOG(INFO) << "VMProfiler: Sent the signal " << self->GetTid() ;
	}
}


void VMProfiler::setProfDaemon(bool val)  {
	has_profDaemon_ = val;
}


bool VMProfiler::hasProfDaemon()  {
  return has_profDaemon_;
}

void MProfiler::ProcessSignalCatcher(int signalVal) {
	vmProfile->ProcessSignalCatcher(signalVal);
//	if(signalVal == kGCMMPDumpSignal) {
//		Thread* self = Thread::Current();
//    MutexLock mu(self, *prof_thread_mutex_);
//    receivedSignal_ = true;
//
//    if(!hasProfDaemon_) {
//    	ShutdownProfiling();
//    }
//
//    // Wake anyone who may have been waiting for the GC to complete.
//    prof_thread_cond_->Broadcast(self);
//
//    GCMMP_VLOG(INFO) << "MProfiler: Sent the signal " << self->GetTid() ;
//	}
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

  GCMMP_VLOG(INFO) << "MProfiler: Caller is leaving now";

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
	GCMMP_VLOG(INFO) << "MProfiler: Attaching thread:" << thread->GetTid();
	vmProfile->attachSingleThread(thread);

//	GCMMP_VLOG(INFO) << "MProfiler: Attaching thread Late " << thread->GetTid();
//	GCMMPThreadProf* threadProf = thread->GetProfRec();
//	if(threadProf != NULL) {
//		if(threadProf->state == GCMMP_TH_RUNNING) {
//			GCMMP_VLOG(INFO) << "MPRofiler: The Thread was already attached " << thread->GetTid() ;
//			return;
//		}
//	}
//	if(thread->GetTid() == prof_thread_->GetTid()) {
//		if(!IsAttachProfDaemon()) {
//			GCMMP_VLOG(INFO) << "MProfiler: Skipping profDaemon attached " << thread->GetTid() ;
//			return;
//		}
//	}
//
//	std::string thread_name;
//	thread->GetThreadName(thread_name);
//
//
//
//
//	if(thread_name.compare("GCDaemon") == 0) { //that's the GCDaemon
//		gc_daemon_ = thread;
//		SetThreadAffinity(thread, false);
//		if(!IsAttachGCDaemon()) {
//			GCMMP_VLOG(INFO) << "MProfiler: Skipping GCDaemon threadProf for " << thread->GetTid() << thread_name;
//			return;
//		}
//	} else {
//		if(thread_name.compare("HeapTrimmerDaemon") == 0) {
//			gc_trimmer_ = thread;
//			SetThreadAffinity(thread, false);
//			if(!IsAttachGCDaemon()) {
//				GCMMP_VLOG(INFO) << "MProfiler: Skipping GCTrimmer threadProf for " << thread->GetTid() << thread_name;
//				return;
//			}
//		} else if(thread_name.compare("main") == 0) { //that's the main thread
//				main_thread_ = thread;
//		}
//		SetThreadAffinity(thread, true);
//	}
//
//	GCMMP_VLOG(INFO) << "MProfiler: Initializing threadProf for " << thread->GetTid() << thread_name;
//	threadProf = new GCMMPThreadProf(this, thread);
//	threadProflist_.push_back(threadProf);
//	thread->SetProfRec(threadProf);
}

bool MProfiler::DettachThread(GCMMPThreadProf* threadProf) {
	if(threadProf != NULL) {
//		GCMMP_VLOG(INFO) << "MProfiler: Detaching thread from List " << threadProf->GetTid();
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
	GCMMP_VLOG(INFO) << "MProfiler: Attaching All threads " << self->GetTid();
	ThreadList* thread_list = Runtime::Current()->GetThreadList();
	MutexLock mu(self, *Locks::thread_list_lock_);
	thread_list->ForEach(GCMMPAttachThread, this);
	GCMMP_VLOG(INFO) << "MProfiler: Done Attaching All threads ";

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
    GCMMP_VLOG(INFO) << "opened  Successsfully MProfile Output file '" << str << "'";
    dump_file_ = new File(fd, std::string(dump_file_name_));
    return;
	}
}



void VMProfiler::GCMMProfPerfCounters(const char* name) {
	if(IsProfilingEnabled()) {
		for (size_t i = 0; i < GCMMP_ARRAY_SIZE(benchmarks); i++) {
			if (strcmp(name, benchmarks[i]) == 0) {
				GCMMP_VLOG(INFO) << "MProfiler found a target VM " << name << " " << GCMMP_ARRAY_SIZE(benchmarks);
				GCMMPThreadProf::mProfiler = this;
				startProfiling();
				//InitializeProfiler();
				return;
			}
		}
		//GCMMP_VLOG(INFO) << "MProfiler did not find a target VM for " << name << " " << GCMMP_ARRAY_SIZE(benchmarks);
	}
}

void VMProfiler::PreForkPreparation() {
	dvmGCMMPSetName = dvmGCMMProfPerfCounters;
}

/*
 * Return true only when the MProfiler is Running
 */
bool VMProfiler::IsMProfRunning() {
	VMProfiler* mP = Runtime::Current()->GetMProfiler();
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingRunning();
	return false;
}


/*
 * Attach a thread from the MProfiler
 */
void VMProfiler::MProfAttachThread(art::Thread* th) {
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetMProfiler()->attachSingleThread(th);
	}
}


/*
 * Attach a thread from the MProfiler
 */
void VMProfiler::MProfNotifyAlloc(size_t allocSize) {
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetMProfiler()->notifyAllocation(allocSize);
	}
}

void MProfiler::SetThreadAffinity(art::Thread* th, bool complementary) {
	if(SetAffinityThread()) {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		uint32_t _cpuCount = (uint32_t) sysconf(_SC_NPROCESSORS_CONF);
		uint32_t _cpu_id =  (uint32_t) gcDaemonAffinity_;
		if(complementary) {
			for(uint32_t _ind = 0; _ind < _cpuCount; _ind++) {
				if(_ind != _cpu_id)
					CPU_SET(_ind, &mask);
			}
		} else {
			CPU_SET(_cpu_id, &mask);
		}
		if(sched_setaffinity(th->GetTid(),
												sizeof(mask), &mask) != 0) {
			if(complementary) {
				GCMMP_VLOG(INFO) << "GCMMP: Complementary";
			}
			LOG(ERROR) << "GCMMP: Error in setting thread affinity tid:" << th->GetTid() << ", cpuid: " <<  _cpu_id;
		} else {
			if(complementary) {
				GCMMP_VLOG(INFO) << "GCMMP: Complementary";
			}
			GCMMP_VLOG(INFO) << "GCMMP: Succeeded in setting assignments tid:" << th->GetTid() << ", cpuid: " <<  _cpu_id;
		}
	}
}
/*
 * Detach a thread from the MProfiler
 */
void MProfiler::MProfDetachThread(art::Thread* th) {
	if(MProfiler::IsMProfRunning()) {
		GCMMP_VLOG(INFO) << "VMProfiler: Detaching thid: " << th->GetTid();
		if(Runtime::Current()->mprofiler_->vmProfile->dettachThread(th->GetProfRec())) {
			th->SetProfRec(NULL);
			GCMMP_VLOG(INFO) << "MProfiler: Detaching thread from List " << th->GetTid();
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


void MProfiler::MProfMarkStartAllocGCHWEvent(void) {
	if(MProfiler::IsMProfRunning()) {
		Runtime::Current()->mprofiler_->vmProfile->addHWStartEvent(GCMMP_GC_BRK_GC_HAT);
	}
}
void MProfiler::MProfMarkEndAllocGCHWEvent(void){
	if(MProfiler::IsMProfRunning()) {
		Runtime::Current()->mprofiler_->vmProfile->addHWEndEvent(GCMMP_GC_BRK_GC_HAT);
	}
}

void MProfiler::MProfMarkStartExplGCHWEvent(void) {
	if(MProfiler::IsMProfRunning()) {
		Runtime::Current()->mprofiler_->vmProfile->addHWStartEvent(GCMMP_GC_BRK_GC_EXPL);
	}
}

void MProfiler::MProfMarkEndExplGCHWEvent(void) {
	if(MProfiler::IsMProfRunning()) {
		Runtime::Current()->mprofiler_->vmProfile->addHWEndEvent(GCMMP_GC_BRK_GC_EXPL);
	}
}


/*
 * Detach a thread from the MProfiler
 */
void MProfiler::MProfMarkWaitTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf, GCMMP_GC_BRK_WAIT_CONC);
	}
}
/*
 * Detach a thread from the MProfiler
 */
void MProfiler::MProfMarkEndWaitTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_WAIT_CONC);
	}
}

void MProfiler::MProfMarkGCHatTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}

void MProfiler::MProfMarkEndGCHatTimeEvent(art::Thread* th){
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}


void MProfiler::MProfMarkGCExplTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void MProfiler::MProfMarkEndGCExplTimeEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void MProfiler::MProfMarkStartSafePointEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_SAFEPOINT);
	}
}

void MProfiler::MProfMarkEndSafePointEvent(art::Thread* th) {
	if(MProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->mprofiler_->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_SAFEPOINT);
	}
}

void MProfiler::MProfMarkSuspendTimeEvent(art::Thread* th, art::ThreadState thState){
	if(MProfiler::IsMProfilingTimeEvent()) {
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
	if(MProfiler::IsMProfilingTimeEvent()) {
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
bool MProfiler::IsMProfilingTimeEvent() {
	MProfiler* mP = Runtime::Current()->mprofiler_;
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingTimeEvent();
	return false;
}

std::size_t MProfiler::GetRelevantAllocBytes(void)  {
	return Runtime::Current()->GetHeap()->GetBytesAllocatedEver() - start_heap_bytes_;
}

int MProfiler::GetMainID(void)  {
	return main_thread_->GetTid();
}

int MProfiler::GetGCDaemonID(void)  {
	if(gc_daemon_ != NULL) {
		return gc_daemon_->GetTid();
	}
	return 0;
}

}// namespace mprofiler
}// namespace art

void dvmGCMMProfPerfCounters(const char* vmName){
	art::mprofiler::VMProfiler* mProfiler =
			art::Runtime::Current()->GetMProfiler();
	if(mProfiler != NULL) {
		mProfiler->GCMMProfPerfCounters(vmName);
	}

}
