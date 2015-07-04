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
#include "gc_profiler/MProfilerHeap.h"
#include "locks.h"
#include "os.h"
#include "class_linker.h"
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

const char * VMProfiler::benchmarks[] = {
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


const char * VMProfiler::gcMMPRootPath[] = {
		"/sdcard/gcperf/", "/data/anr/"
};

const GCMMPProfilingEntry VMProfiler::profilTypes[] = {
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
				 0x0B,
				 0,
				 "FREQ_MONITOR", "CPU Frequency Monitoring",
				 "FREQ_MONITOR_PROF.log",
				 NULL,
				 &createVMProfiler<CPUFreqProfiler>
		},//FREQ_MONITOR
		{
				 0x0D,
				 GCMMP_FLAGS_CREATE_DAEMON,
				 "MMU", "MMU over a given period of time",
				 "PERF_MMU_REF.log",
				 NULL,
				 &createVMProfiler<MMUProfiler>
		},//MMU
		{
				 0x12,
				 GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON,
				 "GCCPI", "Measure CPI for GC daemon",
				 "CPI_GC.log",
				 NULL,
				 &createVMProfiler<GCDaemonCPIProfiler>
		},//GCCPI
		{
				 0x02,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "ObjectSizesProfiler", "Object Histogram Profiler",
				 "GCP_HISTOGRAM.log",
				 NULL,
				 &createVMProfiler<ObjectSizesProfiler>
		},//Objects Histograms
		{
				 0x03,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "ThreadAllocatorProfiler", "Thread Allocator Profiler",
				 "GCP_ALLOC_THREADS.log",
				 NULL,
				 &createVMProfiler<ThreadAllocProfiler>
		},//Thread Allocator
		{
				 0x04,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "CohortProfiler", "Cohort Profiler",
				 "GCP_COHORT.log",
				 NULL,
				 &createVMProfiler<CohortProfiler>
		},//Cohort Profiler
		{
				 0x05,
				 GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_ALLOC_WINDOWS,
				 "ClassProfiler", "Class Profiler",
				 "GCP_CLASS.log",
				 NULL,
				 &createVMProfiler<ClassProfiler>
		},//Class Profiler
};//profilTypes

uint64_t GCPauseThreadManager::startCPUTime = 0;
uint64_t GCPauseThreadManager::startRealTime = 0;
int VMProfiler::kGCMMPLogAllocWindow = GCP_WINDOW_RANGE_LOG;
int VMProfiler::kGCMMPLogAllocWindowDump = GCP_WINDOW_RANGE_LOG;


VMProfiler* GCMMPThreadProf::mProfiler = NULL;
AtomicInteger VMProfiler::GCPTotalAllocBytes;
AtomicInteger GCCohortManager::kGCPLastCohortIndex;
int GCHistogramDataManager::kGCMMPHeaderSize = sizeof(GCPExtraObjHeader);

const int VMProfiler::kGCMMPDumpEndMarker = -99999999;

inline uint64_t GCPauseThreadManager::GetRelevantRealTime(void)  {
	return NanoTime() - GCPauseThreadManager::startRealTime;
}

inline uint64_t GCPauseThreadManager::GetRelevantCPUTime(void)  {
	return ProcessTimeNS() - GCPauseThreadManager::startCPUTime;
}

inline void GCPauseThreadManager::MarkStartTimeEvent(GCMMP_BREAK_DOWN_ENUM evType) {
	if(!busy_) {
		curr_marker_->startMarker = GCPauseThreadManager::GetRelevantRealTime();
		curr_marker_->type = evType;
		busy_ = true;
		count_opens_++;
	}
}

inline void GCPauseThreadManager::MarkEndTimeEvent(GCMMP_BREAK_DOWN_ENUM evType) {
	if(busy_) {
		if(curr_marker_->type != evType)
			return;
		curr_marker_->finalMarker = GCPauseThreadManager::GetRelevantRealTime();
		IncrementIndices();
		count_opens_--;
	}
}

void GCPauseThreadManager::DumpProfData(void* args) {
	VMProfiler* mProfiler = reinterpret_cast<VMProfiler*>(args);

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
	file->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker, sizeof(int));
}



void GCMMPThreadProf::readPerfCounter(int32_t val) {
	if(GetPerfRecord() == NULL)
		return;
	if(state == GCMMP_TH_RUNNING) {
		GetPerfRecord()->storeReading(val);
	}

}


void GCMMPThreadProf::readPerfCounter(int32_t val, uint64_t* totalVals,
		uint64_t* gcMutVals, uint64_t* gcDaemonVals) {
	if(GetPerfRecord() == NULL)
		return;
	if(state == GCMMP_TH_RUNNING) {
		GetPerfRecord()->storeReading(val);
	}
	if(isGCThread()) {
		*gcDaemonVals += GetPerfRecord()->data;
		*totalVals += GetPerfRecord()->data;
	} else {
		GetPerfRecord()->getGCDataDistributions(totalVals, gcMutVals, gcDaemonVals);
	}

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
	vmProfiler->setPauseManager(this);
	vmProfiler->setHistogramManager(this);
	state = GCMMP_TH_RUNNING;

	lifeTime_.startMarker = GCMMPThreadProf::mProfiler->GetRelevantRealTime();
	lifeTime_.finalMarker = 0;
	GCMMP_VLOG(INFO) << "VMProfiler : ThreadProf is initialized";
}

void MMUProfiler::setPauseManager(GCMMPThreadProf* thProf){
	for(int _iter = GCMMP_GC_BRK_NONE; _iter < GCMMP_GC_BRK_MAXIMUM; _iter++) {
		memset((void*) &thProf->timeBrks[_iter], 0, sizeof(GCMMP_ProfileActivity));
	}
	GCPauseThreadManager::startCPUTime = start_cpu_time_ns_;
	GCPauseThreadManager::startRealTime = start_time_ns_;
	thProf->pauseManager = new GCPauseThreadManager();
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
	lifeTime_.startMarker = GCPauseThreadManager::GetRelevantRealTime();
	lifeTime_.finalMarker = 0;
	GCMMP_VLOG(INFO) << "MProfiler : ThreadProf is initialized";
}

GCMMPThreadProf::~GCMMPThreadProf() {

}


bool GCMMPThreadProf::StopTimeProfiling(void) {
	if(state == GCMMP_TH_RUNNING) {
		state = GCMMP_TH_STOPPED;
		lifeTime_.finalMarker = GCPauseThreadManager::GetRelevantRealTime();
		return true;
	}
	return false;
}

void GCMMPThreadProf::Destroy(MProfiler* mProfiler) {

}

void MProfiler::RemoveThreadProfile(GCMMPThreadProf* thProfRec) {
	if(IsProfilingRunning()) {
		if(!thProfRec->StopTimeProfiling()) {
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

MPPerfCounter* GCDaemonCPIProfiler::createHWCounter(Thread* thread) {
	MPPerfCounter* _perfCounter = NULL;
	if(thread->GetProfRec() != NULL) { //create hw for instructions
		//GCMMP_VLOG(INFO) <<
		LOG(ERROR) << "GCDaemonCPIProfiler: creating hwCount: INSTRUCTIONS";
		_perfCounter = MPPerfCounter::Create("INSTRUCTIONS");
	} else {
		//GCMMP_VLOG(INFO) <<
		LOG(ERROR) << "GCDaemonCPIProfiler: creating hwCount: Cycles";
		_perfCounter = MPPerfCounter::Create("CYCLES");
	}
	_perfCounter->OpenPerfLib(thread->GetTid());

	return _perfCounter;
}


//MPPerfCounter* VMProfiler::createHWCounter(Thread* thread) {
//	GCMMP_VLOG(INFO) << "VMProfiler: createHWCounter";
//	return NULL;
//}

inline bool VMProfiler::IsProfilerThread(Thread* th) const {
  return (prof_thread_ != NULL && prof_thread_->GetTid() == th->GetTid());
}


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


inline void VMProfiler::updateHeapAllocStatus(void) {
	gc::Heap* heap_ = Runtime::Current()->GetHeap();


	int32_t _allocBytes = GCPTotalAllocBytes.load();

	heapStatus.index = 1.0 * (_allocBytes >> kGCMMPLogAllocWindowDump);
	heapStatus.timeInNsec = GetRelevantRealTime();
	heapStatus.allocatedBytes = _allocBytes;
	heapStatus.currAllocBytes = heap_->GetBytesAllocated();
	heapStatus.concurrentStartBytes = heap_->GetConcStartBytes();
	heapStatus.currFootPrint = heap_->GetMaxAllowedFootPrint();
	heapStatus.softLimit = 0;//heap_->GetMaxMemory();
	heapStatus.heapTargetUtilization = heap_->GetTargetHeapUtilization();

	heapStatus.gcCounts = 1.0 * getGCEventsCounts();


}


void VMProfiler::notifyAllocation(size_t allocSpace, size_t objSize,
		mirror::Object* obj) {
	if(!verifyThreadNotification())
		return;

	size_t initValue = (size_t)GCPTotalAllocBytes.load();
	GCPTotalAllocBytes.fetch_add(objSize);
	if(!IsAllocWindowsSet())
		return;


	bool _newWindow = (initValue >> kGCMMPLogAllocWindow) != ((objSize + initValue)  >> kGCMMPLogAllocWindow);
	//double _newIndex =  1.0 * ((initValue + allocSpace) >> kGCMMPLogAllocWindow);
	if(_newWindow) {

		GCMMP_VLOG(INFO) << "VMProfiler: allocation Window: " <<
				GCPTotalAllocBytes.load();

		{
			Thread* self = Thread::Current();
	    MutexLock mu(self, *prof_thread_mutex_);
	    GCMMP_HANDLE_FINE_PRECISE_ALLOC(allocSpace, objSize, obj);
	    receivedSignal_ = true;

	    if(hasProfDaemon()) {
	    	prof_thread_cond_->Broadcast(self);
	    }
	    // Wake anyone who may have been waiting for the GC to complete.
	    GCMMP_VLOG(INFO) << "VMProfiler: Sent the signal for allocation:" << self->GetTid() ;
		}
	} else {
		Thread* self = Thread::Current();
    MutexLock mu(self, *prof_thread_mutex_);
    GCMMP_HANDLE_FINE_PRECISE_ALLOC(allocSpace, objSize, obj);
	}
}

void VMProfiler::notifyAllocation(size_t objSize, size_t allocSize) {
	GCPTotalAllocBytes.fetch_add(allocSize);
	if(!IsAllocWindowsSet())
		return;

	GCP_DECLARE_ADD_ALLOC(objSize, allocSize);

	int32_t initValue = GCPTotalAllocBytes.load();
	double _newIndex =  1.0 * ((initValue + allocSize) >> kGCMMPLogAllocWindow);



	if((_newIndex) != (getAllocIndex())) {

		GCMMP_VLOG(INFO) << "VMProfiler: allocation Window: " << GCPTotalAllocBytes.load();


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

inline void PerfCounterProfiler::addHWStartEvent(GCMMP_BREAK_DOWN_ENUM evt) {
	Thread* self = Thread::Current();
	GCMMPThreadProf* _profRec =  self->GetProfRec();
	if(_profRec != NULL && _profRec->state == GCMMP_TH_RUNNING) {
		if(evt != GCMMP_GC_BRK_NONE)
			_profRec->perf_record_->addStartEvent(evt);
	}
}


inline void GCDaemonCPIProfiler::addHWStartEvent(GCMMP_BREAK_DOWN_ENUM evt) {
	Thread* self = Thread::Current();
	GCMMPThreadProf* _profRec =  self->GetProfRec();
	if(_profRec != NULL && _profRec->state == GCMMP_TH_RUNNING) {
		if(evt == GCMMP_GC_BRK_NONE) {
			pid_t _pid = _profRec->GetTid();
		  for (const auto& profRec : threadProfList_) {
		    if(profRec->GetTid() == _pid)
		    	profRec->perf_record_->addStartEvent(evt);
		  }
		}
	}
}

inline void PerfCounterProfiler::addHWEndEvent(GCMMP_BREAK_DOWN_ENUM evt) {
	Thread* self = Thread::Current();
	GCMMPThreadProf* _profRec =  self->GetProfRec();
	if(_profRec != NULL && _profRec->state == GCMMP_TH_RUNNING) {
		if(evt != GCMMP_GC_BRK_NONE)
			_profRec->perf_record_->addEndEvent(evt);
	}
}

inline void GCDaemonCPIProfiler::addHWEndEvent(GCMMP_BREAK_DOWN_ENUM evt) {
	Thread* self = Thread::Current();
	GCMMPThreadProf* _profRec =  self->GetProfRec();
	if(_profRec != NULL && _profRec->state == GCMMP_TH_RUNNING) {
		if(evt == GCMMP_GC_BRK_NONE) {
			pid_t _pid = _profRec->GetTid();
			int _index = 0;
		  for (const auto& profRec : threadProfList_) {
		    if(profRec->GetTid() == _pid) {
		    	_index++;
		    	//LOG(ERROR) << " getEventName:" << profRec->perf_record_->event_name_;
//		    	profRec->perf_record_->addEndEventNOSpecial(evt);
		    	if(_index == 2) {
		    		accData.currInstructions =
		    				profRec->perf_record_->addEndEventNOSpecial(evt);
		    		accData.instructions = profRec->perf_record_->data;
		    	} else {
		    		accData.currCycles =
		    				profRec->perf_record_->addEndEventNOSpecial(evt);
		    		accData.cycles = profRec->perf_record_->data;

		    	}


		    	if(_index == 2) {
		    		if(accData.currInstructions == 0) {
		    			return;
		    		}
		    		GCMMPCPIDataDumped dataDumped;

		    		dataDumped.index = ((GCPTotalAllocBytes.load()) >> kGCMMPLogAllocWindowDump)  * 1.0;
		    		dataDumped.currCycles = accData.currCycles;
		    		dataDumped.currInstructions = accData.currInstructions;
		    		dataDumped.currCPI =
		    				(dataDumped.currCycles * 1.0) / dataDumped.currInstructions;
		    		dataDumped.averageCPI =
		    				(accData.cycles * 1.0) / accData.instructions;

		    		dumpCPIStats(&dataDumped);
		    		break;
		    	}
		    }
		  }

		}
	}
}


bool MMUProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "MMUProfiler -- dettaching thread pid: " << thProf->GetTid();
		thProf->StopTimeProfiling();
	}
	return true;
}

bool PerfCounterProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "VMProfiler -- dettaching thread pid: " << thProf->GetTid();
		if(thProf->GetPerfRecord() != NULL) {
			int32_t currBytes_ = GCPTotalAllocBytes.load();
			thProf->readPerfCounter(currBytes_);
			thProf->GetPerfRecord()->ClosePerfLib();
			//thProf->resetPerfRecord();
		}
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}


bool GCDaemonCPIProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "VMProfiler -- dettaching thread pid: " << thProf->GetTid();
		if(thProf->GetPerfRecord() != NULL) {
			pid_t _id = thProf->GetTid();
			int32_t currBytes_ = GCPTotalAllocBytes.load();
		  for (const auto& profRec : threadProfList_) {
		    if(profRec->GetTid() == _id) {
		    	profRec->readPerfCounter(currBytes_);
		    	profRec->perf_record_->ClosePerfLib();
		    	profRec->state = GCMMP_TH_STOPPED;
		    }
		  }
		}
	}
	return true;
}


CPUFreqProfiler::CPUFreqProfiler(GCMMP_Options* argOptions, void* entry):
		VMProfiler(argOptions, entry){

	LOG(ERROR) << "VMProfiler : CPUFreqProfiler";
}



VMProfiler::VMProfiler(GCMMP_Options* argOptions, void* entry) :
				index_(argOptions->mprofile_type_),
				enabled_((argOptions->mprofile_type_ != MProfiler::kGCMMPDisableMProfile) || (argOptions->gcp_type_ != MProfiler::kGCMMPDisableMProfile)),
				gcDaemonAffinity_(argOptions->mprofile_gc_affinity_),
				prof_thread_(NULL),
				main_thread_(NULL),
				gc_daemon_(NULL),
				running_(false),
				receivedSignal_(false),
				start_heap_bytes_(0) {
	if(IsProfilingEnabled()) {
		int _loop = 0;
		bool _found = false;
		if(argOptions->gcp_type_ != MProfiler::kGCMMPDisableMProfile) {
			for(_loop = GCMMP_ARRAY_SIZE(VMProfiler::profilTypes) - 1;
					_loop >= 0; _loop--) {
				if(VMProfiler::profilTypes[_loop].id_ == argOptions->gcp_type_) {
					_found = true;
					break;
				}
			}
		} else {
			for(size_t _loopI = 0; _loopI < GCMMP_ARRAY_SIZE(VMProfiler::profilTypes); _loopI++) {
				if(VMProfiler::profilTypes[_loopI].id_ == index_) {
					_found = true;
					break;
				}
			}
		}
		if(_found) {
			setProfDaemon(false);
			const GCMMPProfilingEntry* profEntry = &VMProfiler::profilTypes[_loop];
			resetHeapAllocStatus();
			flags_ = profEntry->flags_;
			dump_file_name_ = profEntry->logFile_;
			perfName_ = profEntry->name_;
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

void VMProfiler::dumpHeapConfigurations(GC_MMPHeapConf* heapConf) {

  bool successWrite = dump_file_->WriteFully(heapConf, sizeof(GC_MMPHeapConf));
  if(!successWrite) {
  	LOG(ERROR) << "VMProfiler : dumpHeapConfigurations error writing heap header";
  } else {
  	LOG(ERROR) << "VMProfiler: heap header.. start: " << heapConf->startSize
  			<< "growthLimit= "<< heapConf->growthLimit <<", uptime= "
				<< heapConf->elapsedUPTimeNS;
  }
}

void VMProfiler::InitCommonData() {
	OpenDumpFile();

	GCPTotalAllocBytes = 0;
	start_heap_bytes_ = getRelevantAllocBytes();
	start_cpu_time_ns_ = ProcessTimeNS();
	start_time_ns_ = NanoTime();
	LOG(ERROR) <<  "Uptime is " << uptime_nanos() << "; start_time_ns_ is: " <<
			start_time_ns_;
	GC_MMPHeapConf heapConf;
	setHeapHeaderConf(&heapConf);
	dumpHeapConfigurations(&heapConf);

	if(isMarkHWEvents()){
		initMarkerManager();
	} else {
		markerManager = NULL;
		LOG(ERROR) <<  "no need to initialize event manager ";
	}
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

void GCDaemonCPIProfiler::attachSingleThread(Thread* thread) {
	GCMMP_VLOG(INFO) << "VMProfiler: Attaching thread: " << thread->GetTid();
	GCMMPThreadProf* threadProf = thread->GetProfRec();
	if(threadProf != NULL) {
		if(threadProf->state == GCMMP_TH_RUNNING) {
			GCMMP_VLOG(INFO) << "VMProfiler: The Thread was already attached " <<
					thread->GetTid() ;
			return;
		}
	}
	if(IsProfilerThread(thread)) {
		if(!IsAttachProfDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping profDaemon attached " <<
					thread->GetTid() ;
			return;
		}
	}

	std::string thread_name;
	thread->GetThreadName(thread_name);
	GCMMPThProfileTag _tag = GCMMP_THREAD_GCDAEMON;
	if(thread_name.compare("GCDaemon") == 0) { //that's the GCDaemon
		setGcDaemon(thread);

		setThreadAffinity(thread, false);
		if(!IsAttachGCDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCDaemon threadProf for " <<
					thread->GetTid() << thread_name;
			return;
		}
		LOG(ERROR) << "vmprofiler: Attaching GCDaemon: " << thread->GetTid();
	} else {
//		if(thread_name.compare("HeapTrimmerDaemon") == 0) {
//			setGcTrimmer(thread);
//			setThreadAffinity(thread, false);
//			if(!IsAttachGCDaemon()) {
//				GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCTrimmer threadProf for " << thread->GetTid() << thread_name;
//				return;
//			}
//			LOG(ERROR) << "vmprofiler: Attaching TimerDaemon: " << thread->GetTid();
//			_tag = GCMMP_THREAD_GCTRIM;
//		} else {
			return;
		}
	//}

	GCMMP_VLOG(INFO) << "VMProfiler: Initializing threadProf for " <<
			thread->GetTid() << thread_name;
	threadProf = new GCMMPThreadProf(this, thread);
	threadProf->setThreadTag(_tag);
	threadProfList_.push_back(threadProf);
	thread->SetProfRec(threadProf);
	threadProf = new GCMMPThreadProf(this, thread);
	threadProf->setThreadTag(_tag);
	threadProfList_.push_back(threadProf);
}


void VMProfiler::attachSingleThread(Thread* thread) {
	GCMMP_VLOG(INFO) << "VMProfiler: Attaching thread: " << thread->GetTid();
	GCMMPThreadProf* threadProf = thread->GetProfRec();
	if(threadProf != NULL) {
		if(threadProf->state == GCMMP_TH_RUNNING) {
			GCMMP_VLOG(INFO) << "VMProfiler: The Thread was already attached " <<
					thread->GetTid() ;
			return;
		}
	}
	if(IsProfilerThread(thread)) {
		if(!IsAttachProfDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping profDaemon attached " <<
					thread->GetTid() ;
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
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCDaemon threadProf for " <<
					thread->GetTid() << thread_name;
			return;
		}
		LOG(ERROR) << "vmprofiler: Attaching GCDaemon: " << thread->GetTid();
		_tag = GCMMP_THREAD_GCDAEMON;
	} else {
		if(thread_name.compare("HeapTrimmerDaemon") == 0) {
			setGcTrimmer(thread);
			setThreadAffinity(thread, false);
			if(!IsAttachGCDaemon()) {
				GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCTrimmer threadProf for " <<
						thread->GetTid() << thread_name;
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

	GCMMP_VLOG(INFO) << "VMProfiler: Initializing threadProf for " <<
			thread->GetTid() << thread_name;
	threadProf = new GCMMPThreadProf(this, thread);
	threadProf->setThreadTag(_tag);
	threadProfList_.push_back(threadProf);
	thread->SetProfRec(threadProf);
}


inline void VMProfiler::updateHeapPerfStatus(uint64_t totalVals,
		uint64_t gcMutVals, uint64_t gcDaemonVals) {
	heapStatus.totalMetric = totalVals;
	heapStatus.gcDaemonUsage = (gcDaemonVals * 100.0) /totalVals;
	heapStatus.gcMutUsage = (gcMutVals * 100.0) /totalVals;
}

void PerfCounterProfiler::getPerfData() {
	int32_t currBytes_ = GCPTotalAllocBytes.load();
	uint64_t _totalVals = 0;
	uint64_t _gcMutVals = 0;
	uint64_t _gcDaemonVals = 0;
	//gc::Heap* heap_ = Runtime::Current()->GetHeap();

	//LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " << heap_->GetBytesAllocated() << ", concBytes: " <<heap_->GetConcStartBytes() << ", footPrint: " << heap_->GetMaxAllowedFootPrint();
	for (const auto& threadProf : threadProfList_) {
		threadProf->readPerfCounter(currBytes_, &_totalVals, &_gcMutVals,
				&_gcDaemonVals);
	}
	updateHeapPerfStatus(_totalVals, _gcMutVals, _gcDaemonVals);
}




inline void VMProfiler::addEventMarker(GCMMP_ACTIVITY_ENUM evtMark) {
	Thread* self = Thread::Current();
	EventMarker* _address = NULL;
	{
		MutexLock mu(self, *evt_manager_lock_);
		_address = markerManager->markers + markerManager->currIndex;
		markerManager->currIndex++;
	}
	if(_address != NULL) {
		_address->evType = evtMark;
		_address->currHSize = GCPTotalAllocBytes.load();
		_address->currTime = GetRelevantRealTime();
	}
	if(markerManager->currIndex > kGCMMPMaxEventsCounts) {
			 LOG(ERROR) << "Index of events exceeds the maximum allowed";
	}
}

void VMProfiler::initMarkerManager(void) {
	LOG(ERROR) << "CPUFreqProfiler: Initializing the eventsManager";

	evt_manager_lock_ = new Mutex("Event manager lock");
	Thread* self = Thread::Current();
	{
	  MutexLock mu(self, *evt_manager_lock_);
		if(!isMarkHWEvents()) {
			markerManager = NULL;
			LOG(ERROR) <<  "no need to initialize event manager ";
			return;
		}
	  size_t capacity =
				RoundUp(sizeof(EventMarker) * kGCMMPMaxEventsCounts, kPageSize);
		markerManager = (EventMarkerManager*) calloc(1, sizeof(EventMarkerManager));
	  UniquePtr<MemMap> mem_map(MemMap::MapAnonymous("EventsTimeLine", NULL,
	  		capacity, PROT_READ | PROT_WRITE));
	  markerManager->currIndex = 0;
	  if (mem_map.get() == NULL) {
	    LOG(ERROR) << "CPUFreqProfiler: Failed to allocate pages for alloc space (EventsTimeLine) of size "
	        << PrettySize(capacity);
	    return;
	  } else {
	    LOG(ERROR) << "CPUFreqProfiler: succeeded to allocate pages for alloc space (EventsTimeLine) of size "
	        << PrettySize(capacity);
	  }
	  markerManager->markers = (EventMarker*)(mem_map->Begin());

	  //mem_map.release();
	}
}


void PerfCounterProfiler::dumpProfData(bool lastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
  LOG(ERROR) <<  "PerfCounterProfiler: start dumping data";

  if(lastDump) {
  	bool successWrite =
  			dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
  					sizeof(int));
  	if(successWrite) {
  		dumpEventMarks();
  	} else {
  		LOG(ERROR) << "PerfCounterProfiler:: could not dump the event marker after heap stats";
  	}
    dump_file_->Close();
  }

}


void GCDaemonCPIProfiler::dumpProfData(bool lastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);

	if(lastDump) {
		LOG(ERROR) <<  "GCDaemonCPIProfiler: start dumping data";
	  bool _success =
	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
	  			sizeof(int));
	  if(_success)
	  	LOG(ERROR) << "<<<< Succeeded dump to file" ;
	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
		dump_file_->Close();
		LOG(ERROR) <<  "GCDaemonCPIProfiler: done dumping data";
	}

}


void CPUFreqProfiler::dumpProfData(bool lastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
  LOG(ERROR) <<  "CPUFreqProfiler: start dumping data";
	dumpEventMarks();
	if(lastDump) {
		dump_file_->Close();
	}
	LOG(ERROR) <<  "CPUFreqProfiler: done dumping data";
}

inline void CPUFreqProfiler::AddEventMarker(GCMMP_ACTIVITY_ENUM evt){
	addEventMarker(evt);
}

inline void CPUFreqProfiler::DumpEventMarks(void){
	dumpEventMarks();
}

inline void PerfCounterProfiler::AddEventMarker(GCMMP_ACTIVITY_ENUM evt){
	addEventMarker(evt);
}

inline void PerfCounterProfiler::DumpEventMarks(void){
	dumpEventMarks();
}

void VMProfiler::dumpEventMarks(void) {
	Thread* self = Thread::Current();
	MutexLock mu(self, *evt_manager_lock_);

	size_t dataLength =  sizeof(EventMarker) * markerManager->currIndex;

  bool successWrite = dump_file_->WriteFully(markerManager->markers, dataLength);
  if(successWrite) {
  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker, sizeof(int));
  	LOG(ERROR) << "<<<< Succeeded dump to file" ;
  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
  }

	LOG(ERROR) << "<<<< total written: " << dataLength <<
			", Sizeof(EventMarker):"<< sizeof(EventMarker)
			<< ".. sizeof(uint64_t):" << sizeof(uint64_t) << ".. sizeof(int32_t):"
			<< sizeof(int32_t) << ".. sizeof (evtYpe:):" << sizeof(GCMMP_ACTIVITY_ENUM);

	for(int _indIter = 0; _indIter < markerManager->currIndex; _indIter++) {
		EventMarker* marker = markerManager->markers + _indIter;
		if(marker == NULL)
			return;
		LOG(ERROR) << "---->>>>> data written:<<["<<_indIter<<"] evtYpe:" <<
				marker->evType << "; currHSize:" << marker->currHSize << "; time:"
				<< marker->currTime;

	}
}




void PerfCounterProfiler::logPerfData() {
	int32_t currBytes_ = GCPTotalAllocBytes.load();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes() << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();
	uint64_t _sumData = 0;
	uint64_t _sumGc = 0;
	uint64_t _data = 0;
	for (const auto& threadProf : threadProfList_) {
		_data = threadProf->getDataPerfCounter();
		if(threadProf->getThreadTag() > GCMMP_THREAD_MAIN) {
			_sumGc += _data;
			LOG(ERROR) << "logging specific gcThreadProf: " <<
					threadProf->getThreadTag() << ", tid:" << threadProf->GetTid();
		}
		if(false)
			LOG(ERROR) << "logging thid: "<< threadProf->GetTid() << ", "<< _data;
		//threadProf->GetPerfRecord()->dumpMarks();
		threadProf->GetPerfRecord()->getGCMarks(&_sumGc);
		_sumData += _data;
	}
	LOG(ERROR) << "currBytes: " << currBytes_ <<", sumData= "<< _sumData <<
			", sumGc=" << _sumGc <<", ration="<< ((_sumGc*100.0)/_sumData);
}

bool CPUFreqProfiler::periodicDaemonExec(void){
	return true;
}

bool GCDaemonCPIProfiler::periodicDaemonExec(void){
	return true;
}

inline void PerfCounterProfiler::dumpHeapStats(void) {
	bool successWrite = dump_file_->WriteFully(&heapStatus, sizeof(GCMMPHeapStatus));
	if(successWrite) {

	} else {
		LOG(ERROR) << "could not dump heap stats";
	}
}


inline void GCDaemonCPIProfiler::dumpCPIStats(GCMMPCPIDataDumped* dataD) {
	bool successWrite = dump_file_->WriteFully(dataD, sizeof(GCMMPCPIDataDumped));
	if(successWrite) {

	} else {
		LOG(ERROR) << "could not dump heap stats";
	}
}

bool PerfCounterProfiler::periodicDaemonExec(void) {
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
    dumpHeapStats();
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
    //updateHeapAllocStatus();
    //getPerfData();
    receivedSignal_ = false;

    if(getRecivedShutDown()) {
    	LOG(ERROR) << "received shutdown tid: " <<  self->GetTid();
    	//logPerfData();
    }
  	return getRecivedShutDown();
  } else {
  	return false;
  }
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

GCDaemonCPIProfiler::GCDaemonCPIProfiler(GCMMP_Options* argOptions, void* entry) :
	VMProfiler(argOptions, entry) {
	if(initCounters(perfName_) != 0) {
		LOG(ERROR) << "GCDaemonCPIProfiler : init counters returned error";
	} else {
		memset(&accData, 0, sizeof(GCMMPCPIData));
		LOG(ERROR) << "GCDaemonCPIProfiler : Initializer";
	}
}

PerfCounterProfiler::PerfCounterProfiler(GCMMP_Options* argOptions,
		void* entry) :
		VMProfiler(argOptions, entry) {
	//GCMMPProfilingEntry* _entry = (GCMMPProfilingEntry*) entry;
	if(initCounters(perfName_) != 0) {
		LOG(ERROR) << "PerfCounterProfiler : init counters returned error";
	} else {
		hwEvent_ = perfName_;
		LOG(ERROR) <<
				"PerfCounterProfiler : init counters returned valid..evtName=" << hwEvent_;

	}
	LOG(ERROR) << "PerfCounterProfiler : Initializer";
}

int VMProfiler::initCounters(const char* evtName){
	init_perflib_counters();
	return 0;
}


MMUProfiler::MMUProfiler(GCMMP_Options* argOptions, void* entry):
		VMProfiler(argOptions, entry){

	LOG(ERROR) << "VMProfiler : MMUProfiler";
}

VMProfiler* VMProfiler::CreateVMprofiler(GCMMP_Options* opts) {
	VMProfiler* profiler = NULL;
	if(opts->gcp_type_ != MProfiler::kGCMMPDisableMProfile) {
		for(int _loop = GCMMP_ARRAY_SIZE(VMProfiler::profilTypes) - 1;
				_loop >= 0; _loop--) {
			if(VMProfiler::profilTypes[_loop].id_ == opts->gcp_type_) {
				const GCMMPProfilingEntry* profEntry = &VMProfiler::profilTypes[_loop];
				profiler = profEntry->creator_(opts, (void*) profEntry);
				break;
			}
		}
	} else {
		for(size_t _loop = 0; _loop < GCMMP_ARRAY_SIZE(VMProfiler::profilTypes); _loop++) {
			if(VMProfiler::profilTypes[_loop].id_ == opts->mprofile_type_) {
				const GCMMPProfilingEntry* profEntry = &VMProfiler::profilTypes[_loop];
				profiler = profEntry->creator_(opts, (void*) profEntry);
				break;
			}
		}
	}
	return profiler;
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
//	if(IsProfilingEnabled()) {
//		size_t _loop = 0;
//		for(_loop = 0; _loop < GCMMP_ARRAY_SIZE(MProfiler::profilTypes); _loop++) {
//			if(MProfiler::profilTypes[_loop].id_ == index_)
//				break; //found
//		}
//		if(_loop >= GCMMP_ARRAY_SIZE(MProfiler::profilTypes)) {
//			LOG(ERROR) << "MProfiler : Performance type is not supported";
//		}
//		const GCMMPProfilingEntry* profEntry = &MProfiler::profilTypes[_loop];
//		flags_ = profEntry->flags_;
//		dump_file_name_ = profEntry->logFile_;
//		GCMMP_VLOG(INFO) << "MProfiler Profiling is Enabled";
//		prof_thread_mutex_ = new Mutex("MProfile Thread lock");
//		prof_thread_cond_.reset(new ConditionVariable("MProfile Thread condition variable",
//																									*prof_thread_mutex_));
//		vmProfile = profEntry->creator_(argOptions, (void*)profEntry);
//		GCMMP_VLOG(INFO) << "MProfiler Created";
//
//	} else {
//		flags_ = 0;
//		dump_file_name_ = NULL;
//		GCMMP_VLOG(INFO) << "MProfiler Profiling is Disabled";
//	}

}


void MProfiler::DumpCurrentOutpu(void) {
	//ScopedThreadStateChange tsc(Thread::Current(), kWaitingForSignalCatcherOutput);
}

static void GCMMPKillThreadProf(GCMMPThreadProf* profRec, void* arg) {
	VMProfiler* vmProfiler = reinterpret_cast<VMProfiler*>(arg);
	if(vmProfiler != NULL) {
		if(vmProfiler->dettachThread(profRec)) {

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
	//GCMMPThreadProf::mProfiler = this;
	start_heap_bytes_ = GetRelevantAllocBytes();
	cpu_time_ns_ = ProcessTimeNS();
	start_time_ns_ = uptime_nanos();

	GCPauseThreadManager::startCPUTime = cpu_time_ns_;
	GCPauseThreadManager::startRealTime = start_time_ns_;

	GCMMP_VLOG(INFO) << "MProfiler startCPU NS is : " << cpu_time_ns_ <<
			", statTime: " << start_time_ns_;
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
			LOG(ERROR) << "GCMMP: Error in setting thread affinity tid:" <<
					th->GetTid() << ", cpuid: " <<  _cpu_id;
		} else {
			if(complementary) {
				GCMMP_VLOG(INFO) << "GCMMP: Complementary";
			}
			GCMMP_VLOG(INFO) << "GCMMP: Succeeded in setting assignments tid:" <<
					th->GetTid() << ", cpuid: " <<  _cpu_id;
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

      GCMMP_VLOG(INFO) << "MProfiler: Assigning profID to profDaemon " <<
      		self->GetTid();
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


inline void VMProfiler::setReceivedShutDown(bool val){
	receivedShutdown_ = val;
}

inline bool VMProfiler::getRecivedShutDown(void) {
	return receivedShutdown_;
}

void VMProfiler::setHeapHeaderConf(GC_MMPHeapConf* heapConf) {
	heapConf->elapsedUPTimeNS = (uptime_nanos()*1.0);
	gc::Heap* _heap = Runtime::Current()->GetHeap();
	heapConf->growthLimit = _heap->GetMaxMemory();
	heapConf->startSize   = _heap->GetHeapCapacity();
}

void VMProfiler::ShutdownProfiling(void) {
//	LOG(ERROR) << "ShutDownProfiling:" << Thread::Current()->GetTid();

	 if(IsProfilingRunning()) {
		 LOG(ERROR) << "VMProfiler: shutting down " << Thread::Current()->GetTid() ;
			end_heap_bytes_ = getRelevantAllocBytes();
			end_cpu_time_ns_ = GetRelevantCPUTime();
			end_time_ns_ = NanoTime();


			dumpProfData(true);


			//Runtime* runtime = Runtime::Current();

			Thread* self = Thread::Current();
			{
				//ThreadList* thread_list = Runtime::Current()->GetThreadList();
				MutexLock mu(self, *Locks::thread_list_lock_);
				ForEach(GCMMPKillThreadProf, this);
				//thread_list->ForEach(GCMMPResetThreadField, this);
			}

			setIsProfilingRunning(false);

			if(hasProfDaemon()) {
				Runtime* runtime = Runtime::Current();
				runtime->DetachCurrentThread();
				setProfDaemon(false);
			}
	 }
}

static void GCMMPDumpMMUThreadProf(GCMMPThreadProf* profRec, void* arg) {
	VMProfiler* vmProfiler = reinterpret_cast<VMProfiler*>(arg);
	if(vmProfiler != NULL) {

		 GCPauseThreadManager* mgr = profRec->getPauseMgr();
		 if(!mgr->HasData())
			 return;
		 art::File* f = vmProfiler->GetDumpFile();
		 int _pid = profRec->GetTid();
		 int _type = profRec->getThreadTag();
		 f->WriteFully(&_pid, sizeof(int));
		 f->WriteFully(&_type, sizeof(int));
		 if(profRec->GetEndTime() == 0) {
			 profRec->GetliveTimeInfo()->finalMarker =
					 vmProfiler->end_time_ns_ - vmProfiler->start_time_ns_;
		 }
		 f->WriteFully(profRec->GetliveTimeInfo(), sizeof(GCMMP_ProfileActivity));

		 GCMMP_VLOG(INFO) << "MProfiler_out: " << profRec->GetTid() << ">>>>>>>>>>>";

		 mgr->DumpProfData(vmProfiler);
		 GCMMP_VLOG(INFO) << "MPr_out: " << profRec->GetTid() << "<<<<<<<<<<<<<<";


	}
}


void MMUProfiler::dumpProfData(bool isLastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);

  LOG(ERROR) <<  "dumping for MMU";
  GCMMP_VLOG(INFO) << " Dumping the commin information ";
  bool successWrite = false;
//  dump_file_->WriteFully(&start_heap_bytes_, sizeof(size_t));
//  if(successWrite) {
//  	successWrite = dump_file_->WriteFully(&start_heap_bytes_, sizeof(size_t));
//  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
//  }
//  if(successWrite) {
//  	successWrite = dump_file_->WriteFully(&start_cpu_time_ns_, sizeof(uint64_t));
//  }

  GCMMP_VLOG(INFO) << " Dumping the MMU information ";

  successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));


  if(successWrite) {
  	successWrite = dump_file_->WriteFully(&end_time_ns_, sizeof(uint64_t));
  }
  LOG(ERROR) <<  "Uptime is " << uptime_nanos() << "; endTime is: " << end_time_ns_;
  if(successWrite) {
  	ForEach(GCMMPDumpMMUThreadProf, this);
  }

  if(successWrite) {
  	successWrite =
  			dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
  					sizeof(uint64_t));
  }

	if(isLastDump) {
		dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker, sizeof(int));
		dump_file_->Close();
	}
	GCMMP_VLOG(INFO) << " ManagerCPUTime: " <<
			GCPauseThreadManager::GetRelevantCPUTime();
	GCMMP_VLOG(INFO) << " ManagerRealTime: " <<
			GCPauseThreadManager::GetRelevantRealTime();
	uint64_t cuuT = ProcessTimeNS();
	GCMMP_VLOG(INFO) << "StartCPUTime =  "<< start_cpu_time_ns_ <<
			", cuuCPUT: "<< cuuT;
	cuuT = uptime_nanos();
	GCMMP_VLOG(INFO) << "StartTime =  "<< start_time_ns_ << ", cuuT: "<< cuuT;

	GCMMP_VLOG(INFO) << " startBytes = " << start_heap_bytes_ <<
			", cuuBytes = " << getRelevantAllocBytes();

};

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
  	successWrite =
  			dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
  					sizeof(uint64_t));
  }

	if(isLastDump) {
		dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
				sizeof(int));
		dump_file_->Close();
	}
	GCMMP_VLOG(INFO) << " ManagerCPUTime: " <<
			GCPauseThreadManager::GetRelevantCPUTime();
	GCMMP_VLOG(INFO) << " ManagerRealTime: " <<
			GCPauseThreadManager::GetRelevantRealTime();
	uint64_t cuuT = ProcessTimeNS();
	GCMMP_VLOG(INFO) << "StartCPUTime =  "<< cpu_time_ns_ << ", cuuCPUT: "<< cuuT;
	cuuT = uptime_nanos();
	GCMMP_VLOG(INFO) << "StartTime =  "<< start_time_ns_ << ", cuuT: "<< cuuT;

	GCMMP_VLOG(INFO) << " startBytes = " << start_heap_bytes_ <<
			", cuuBytes = " << GetRelevantAllocBytes();
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


inline bool VMProfiler::hasProfDaemon()  {
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


void VMProfiler::MProfileSignalCatcher(int signalVal) {
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetMProfiler()->ProcessSignalCatcher(signalVal);
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
		return threadProf->StopTimeProfiling();
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

void VMProfiler::ForEach(void (*callback)(GCMMPThreadProf*, void*),
		void* context) {
  for (const auto& profRec : threadProfList_) {
    callback(profRec, context);
  }
}

void MProfiler::ForEach(void (*callback)(GCMMPThreadProf*, void*),
		void* context) {
  for (const auto& profRec : threadProflist_) {
    callback(profRec, context);
  }
}

void MProfiler::OpenDumpFile() {
//	for (size_t i = 0; i < GCMMP_ARRAY_SIZE(gcMMPRootPath); i++) {
//		char str[256];
//		strcpy(str, gcMMPRootPath[i]);
//		strcat(str, dump_file_name_);
//
//
//		int fd = open(str, O_RDWR | O_APPEND | O_CREAT, 0777);
//	  if (fd == -1) {
//	    PLOG(ERROR) << "Unable to open MProfile Output file '" << str << "'";
//	    continue;
//	  }
//    GCMMP_VLOG(INFO) << "opened  Successsfully MProfile Output file '" << str << "'";
//    dump_file_ = new File(fd, std::string(dump_file_name_));
//    return;
//	}
}



void VMProfiler::GCMMProfPerfCounters(const char* name) {
	if(IsProfilingEnabled()) {
		for (size_t i = 0; i < GCMMP_ARRAY_SIZE(benchmarks); i++) {
			if (strcmp(name, benchmarks[i]) == 0) {
				GCMMP_VLOG(INFO) << "MProfiler found a target VM " << name << " " <<
						GCMMP_ARRAY_SIZE(benchmarks);
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
inline bool VMProfiler::IsMProfRunning() {
	VMProfiler* mP = Runtime::Current()->GetMProfiler();
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingRunning();
	return false;
}

/*
 * Return true only when the MProfiler is Running for HW events
 */
inline bool VMProfiler::IsMProfHWRunning() {
	VMProfiler* mP = Runtime::Current()->GetMProfiler();
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingHWEvent();
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




//void VMProfiler::MProfNotifyFree(size_t objSize, size_t allocSize) {
//	if(VMProfiler::IsMProfRunning()) {
//		Runtime::Current()->GetMProfiler()->notifyFreeing(objSize, allocSize);
//	}
//}


void VMProfiler::MProfNotifyFree(size_t allocSpace, mirror::Object* obj) {
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetMProfiler()->notifyFreeing(allocSpace, obj);
	}
}

void VMProfiler::MProfNotifyAlloc(size_t allocatedSpace,
		size_t objSize, mirror::Object* obj) {
	GCP_RESET_OBJ_PROFILER_HEADER(allocatedSpace,obj);
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetMProfiler()->notifyAllocation(allocatedSpace, objSize, obj);
	}
}


///*
// * Attach a thread from the MProfiler
// */
//void VMProfiler::MProfNotifyAlloc(size_t objSize, size_t allocSize) {
//	if(VMProfiler::IsMProfRunning()) {
//		Runtime::Current()->GetMProfiler()->notifyAllocation(objSize, allocSize);
//	}
//}
//


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
			LOG(ERROR) << "GCMMP: Error in setting thread affinity tid:" <<
					th->GetTid() << ", cpuid: " <<  _cpu_id;
		} else {
			if(complementary) {
				GCMMP_VLOG(INFO) << "GCMMP: Complementary";
			}
			GCMMP_VLOG(INFO) << "GCMMP: Succeeded in setting assignments tid:" <<
					th->GetTid() << ", cpuid: " <<  _cpu_id;
		}
	}
}
/*
 * Detach a thread from the MProfiler
 */
void VMProfiler::MProfDetachThread(art::Thread* th) {
	if(VMProfiler::IsMProfRunning()) {
		GCMMP_VLOG(INFO) << "VMProfiler: Detaching thid: " << th->GetTid();
		if(Runtime::Current()->GetMProfiler()->dettachThread(th->GetProfRec())) {
			th->SetProfRec(NULL);
			GCMMP_VLOG(INFO) << "MProfiler: Detaching thread from List " << th->GetTid();
		}
	}
}

inline void VMProfiler::MarkWaitTimeEvent(GCMMPThreadProf* profRec,
		GCMMP_BREAK_DOWN_ENUM evType) {
	profRec->getPauseMgr()->MarkStartTimeEvent(evType);
}

inline void VMProfiler::MarkEndWaitTimeEvent(GCMMPThreadProf* profRec,
		GCMMP_BREAK_DOWN_ENUM evType) {
	profRec->getPauseMgr()->MarkEndTimeEvent(evType);
}


void VMProfiler::MProfMarkStartConcGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addHWStartEvent(GCMMP_GC_BRK_NONE);
		Runtime::Current()->GetMProfiler()->addEventMarker(GCMMP_GC_DAEMON);
	}
}

void VMProfiler::MProfMarkEndConcGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addHWEndEvent(GCMMP_GC_BRK_NONE);
	}
}


void VMProfiler::MProfMarkStartTrimHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addHWStartEvent(GCMMP_GC_BRK_NONE);
		Runtime::Current()->GetMProfiler()->addEventMarker(GCMMP_GC_TRIM);
	}
}

void VMProfiler::MProfMarkEndTrimHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addHWEndEvent(GCMMP_GC_BRK_NONE);
	}
}


void VMProfiler::MProfMarkStartAllocGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addEventMarker(GCMMP_GC_MALLOC);
		Runtime::Current()->GetMProfiler()->addHWStartEvent(GCMMP_GC_BRK_GC_HAT);
	}
}
void VMProfiler::MProfMarkEndAllocGCHWEvent(void){
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addHWEndEvent(GCMMP_GC_BRK_GC_HAT);
	}
}

void VMProfiler::MProfMarkStartExplGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addEventMarker(GCMMP_GC_EXPLICIT);
		Runtime::Current()->GetMProfiler()->addHWStartEvent(GCMMP_GC_BRK_GC_EXPL);
	}
}

void VMProfiler::MProfMarkEndExplGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetMProfiler()->addHWEndEvent(GCMMP_GC_BRK_GC_EXPL);
	}
}


/*
 * Detach a thread from the MProfiler
 */
void VMProfiler::MProfMarkWaitTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkWaitTimeEvent(thProf, GCMMP_GC_BRK_WAIT_CONC);
	}
}
/*
 * Detach a thread from the MProfiler
 */
void VMProfiler::MProfMarkEndWaitTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_WAIT_CONC);
	}
}

void VMProfiler::MProfMarkGCHatTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}

void VMProfiler::MProfMarkEndGCHatTimeEvent(art::Thread* th){
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}


void VMProfiler::MProfMarkGCExplTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void VMProfiler::MProfMarkEndGCExplTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void VMProfiler::MProfMarkStartSafePointEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_SAFEPOINT);
	}
}

void VMProfiler::MProfMarkEndSafePointEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_SAFEPOINT);
	}
}

void VMProfiler::MProfMarkSuspendTimeEvent(art::Thread* th, art::ThreadState thState){
	if(VMProfiler::IsMProfilingTimeEvent()) {
		if(thState == kSuspended) {
			GCMMPThreadProf* thProf = th->GetProfRec();
			if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
				Runtime::Current()->GetMProfiler()->MarkWaitTimeEvent(thProf,
						GCMMP_GC_BRK_SUSPENSION);
				return;
			}
		}
	}
	return;
}

void VMProfiler::MProfMarkEndSuspendTimeEvent(art::Thread* th, art::ThreadState thState){
	if(VMProfiler::IsMProfilingTimeEvent()) {
		if(thState == kSuspended) {
			GCMMPThreadProf* thProf = th->GetProfRec();
			if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
				Runtime::Current()->GetMProfiler()->MarkEndWaitTimeEvent(thProf,
						GCMMP_GC_BRK_SUSPENSION);
			}
		}
	}
}


/*
 * Return true only when the MProfiler is Running
 */
inline bool VMProfiler::IsMProfilingTimeEvent() {
	VMProfiler* mP = Runtime::Current()->GetMProfiler();
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
/********************************* Object demographics profiling ****************/

ObjectSizesProfiler::ObjectSizesProfiler(GCMMP_Options* argOptions, void* entry) :
	VMProfiler(argOptions, entry) {
	srand (time(NULL));
	initHistogram();
//	memset((void*)&testLogic, 0, sizeof(GCPObjectHeaderTest));
//	testLogic.takeTest = 1;

	LOG(ERROR) << "ObjectSizesProfiler : ObjectSizesProfiler";
//	if(initCounters(perfName_) != 0) {
//		LOG(ERROR) << "ObjectSizesProfiler : init counters returned error";
//	} else {
//
//		LOG(ERROR) << "ObjectSizesProfiler : ObjectSizesProfiler";
//	}
}

void ObjectSizesProfiler::setHistogramManager(GCMMPThreadProf* thProf) {
	thProf->histogramManager = new GCHistogramManager();
}

MPPerfCounter* ObjectSizesProfiler::createHWCounter(Thread* thread) {
	GCMMP_VLOG(INFO) << "ObjectSizesProfiler: creating hwCount";
	return NULL;
}


void ObjectSizesProfiler::initHistogram(void) {
	GCCohortManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	hitogramsData = new GCHistogramManager();
//	lastLiveGuard = 0;
//
//	totalHistogramSize = GCP_MAX_HISTOGRAM_SIZE * sizeof(GCPHistogramRecord);
//	memset((void*)(&globalRecord), 0, sizeof(GCPHistogramRecord));
//	memset((void*)(&lastLiveRecord), 0, sizeof(GCPHistogramRecord));
//
//	globalRecord.pcntLive = 100.0;
//	globalRecord.pcntTotal = 100.0;
//
//	lastLiveRecord.pcntLive = 100.0;
//	lastLiveRecord.pcntTotal = 100.0;
//
//	memset((void*)histogramTable, 0, totalHistogramSize);
//	memset((void*)lastLiveTable, 0, totalHistogramSize);
//
//
//	for(size_t i = 0; i < GCMMP_ARRAY_SIZE(histogramTable); i++){
//		histogramTable[i].index = (i+1) * 1.0;
//		lastLiveTable[i].index = (i+1) * 1.0;
//	}
//
//	lastCohortIndex = 0;
}


//inline void  ObjectSizesProfiler::gcpAddDataToHist(GCPHistogramRecord* rec){
//	rec->cntLive++;
//	rec->cntTotal++;
//}


inline void ObjectSizesProfiler::gcpAddObject(size_t objSize, size_t allocSize) {
	//get cohorts
	if(allocSize == objSize) {
		LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
	}
}





inline void ObjectSizesProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj, GCMMPThreadProf* thProf) {

}


inline void ObjectSizesProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	getObjHistograms()->addObject(allocatedMemory, objSize, obj);
//	int32_t readVal = lastLiveGuard;

//	while(UNLIKELY(android_atomic_cas(0, 2, &lastLiveGuard) != 0)) {
//		readVal = lastLiveGuard;
//	}
//
//
//	gcpAddDataToHist(&lastLiveTable[histIndex]);
//	gcpAddDataToHist(&lastLiveRecord);

//	do {
//		readVal = 2;
//	} while (UNLIKELY(android_atomic_cas(readVal, 0, &lastLiveGuard) != 0));

//	if(false && globalRecord.cntTotal > 10000) {
//		if(testLogic.takeTest == 1) {
//			testLogic.takeTest = 2;
//			testLogic.obj = obj;
//			testLogic.headerReplica.objSize = objSize;
//			LOG(ERROR) << " ##### testRecord: obj: " << obj << " with size: " << testLogic.headerReplica.objSize;
//		}
//	}
}


//inline void ObjectSizesProfiler::gcpRemoveObject(size_t objSize, size_t allocSize) {
//	size_t histIndex = 32 - CLZ(objSize) - 1;
//	histogramTable[histIndex].cntLive--;
//	globalRecord.cntLive--;
//	lastLiveRecord.cntLive--;
//	if(false && allocSize == objSize) {
//			LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
//	}
//}

//inline void CohortProfiler::gcpRemoveObject(size_t allocatedMemory,
//		mirror::Object* obj) {
//
//	byte* address = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(obj) +
//			allocatedMemory - sizeof(GCPObjectExtraHeader));
//	GCPObjectExtraHeader* extraHeader = reinterpret_cast<GCPObjectExtraHeader*>(address);
//	if(extraHeader->objSize == 0) {
//		LOG(ERROR) << "skipping object with size 0";
//		return;
//	}
//	size_t histIndex = (32 - CLZ(extraHeader->objSize)) - 1;
//	histogramTable[histIndex].cntLive--;
//	globalRecord.cntLive--;
////	if(false && allocSize == objSize) {
////			LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
////	}
//}

inline void ObjectSizesProfiler::gcpRemoveObject(size_t allocatedMemory,
		mirror::Object* obj) {
	GCHistogramManager::GCPRemoveObj(allocatedMemory, obj);
//	//LOG(ERROR) << "ObjectSizesProfiler::remove--> " << allocatedMemory;
//	objHistograms->(allocatedMemory, objSize, obj);
//
//	byte* address = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(obj) +
//			allocatedMemory - sizeof(GCPObjectExtraHeader));
//	GCPObjectExtraHeader* extraHeader = reinterpret_cast<GCPObjectExtraHeader*>(address);
//	if(extraHeader->objSize == 0) {
//		//LOG(ERROR) << "skipping object with size 0";
//		return;
//	}
//	size_t histIndex = (32 - CLZ(extraHeader->objSize)) - 1;
//	if(false && testLogic.takeTest == 2) {
//		if(testLogic.obj == obj) {
//			testLogic.takeTest = 3;
//			LOG(ERROR) << " ##### testRecord: removeobj: " << obj << " with size: " << extraHeader->objSize << ", vs captured: " << testLogic.headerReplica.objSize;
//		}
//	}
//
//	if(histogramTable[histIndex].cntLive >= 1.0 ) {
//		histogramTable[histIndex].cntLive--;
//		globalRecord.cntLive--;
//	}
//
//	//int32_t readVal = 0;
//
//
//	if(lastLiveGuard != 1) {
//		if(lastLiveTable[histIndex].cntLive >= 1.0) {
//			lastLiveTable[histIndex].cntLive--;
//			if(lastLiveRecord.cntLive >= 1.0)
//				lastLiveRecord.cntLive--;
//		}
//	}
//
////	while(UNLIKELY(android_atomic_cas(readVal, 2, &lastLiveGuard) != 0)) {
////		readVal = 0;
////	}
//
//
//
////	do {
////		readVal = 2;
////	} while (UNLIKELY(android_atomic_cas(readVal, 0, &lastLiveGuard) != 0));
//
////	if(false && allocSize == objSize) {
////			LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
////	}
}

inline void ObjectSizesProfiler::dumpHeapStats(void) {
	bool successWrite = dump_file_->WriteFully(&heapStatus, sizeof(GCMMPHeapStatus));
	if(successWrite) {

	} else {
		LOG(ERROR) << "could not dump heap stats";
	}
}

inline void ObjectSizesProfiler::notifyFreeing(size_t allocatedSpace, mirror::Object* obj){
	gcpRemoveObject(allocatedSpace, obj);
}

//inline void ObjectSizesProfiler::notifyFreeing(size_t objSize, size_t allocSize) {
//	GCP_DECLARE_REMOVE_ALLOC(objSize, allocSize);
//}

//inline void CohortProfiler::notifyFreeing(size_t objSize, size_t allocSize) {
//	GCP_DECLARE_REMOVE_ALLOC(objSize, allocSize);
//}

void ObjectSizesProfiler::logPerfData() {



	int32_t currBytes_ = GCPTotalAllocBytes.load();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes() << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();

	GCHistogramManager* _histManager = getObjHistograms();

	for(int i = 0; i < GCHistogramDataManager::kGCMMPMaxHistogramEntries; i++){
		LOG(ERROR) << "index: " << _histManager->histogramTable[i].index << " :: cntLive=" <<
				_histManager->histogramTable[i].cntLive << "; cntTotal="<<
				_histManager->histogramTable[i].cntTotal<<"; pcntLive=" << _histManager->histogramTable[i].pcntLive <<
				"; pcntTotal="<< _histManager->histogramTable[i].pcntTotal;
	}
}

bool ObjectSizesProfiler::waitForProfileSignal(void) {
	Thread* self = Thread::Current();
  // Check if GC is running holding gc_complete_lock_.
  MutexLock mu(self, *prof_thread_mutex_);
  ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
  {
  	prof_thread_cond_->Wait(self);
  }
  return receivedSignal_;
}

bool ObjectSizesProfiler::periodicDaemonExec(void) {
	Thread* self = Thread::Current();
  if(waitForProfileSignal()) { //we recived Signal to Shutdown
    GCMMP_VLOG(INFO) << "ObjectSizesProfiler: signal Received " << self->GetTid() ;
    //LOG(ERROR) << "periodic daemon recieved signals tid: " <<  self->GetTid();

    {
    	MutexLock mu(self, *prof_thread_mutex_);
    	receivedSignal_ = false;
    }
 //
 //

    updateHeapAllocStatus();

    if(getRecivedShutDown()) {
    	LOG(ERROR) << "received shutdown tid: " <<  self->GetTid();

    } else {
    	dumpProfData(false);
    }
#if GCP_COLLECT_FOR_PROFILE
    	gc::Heap* heap_ = Runtime::Current()->GetHeap();
    	heap_->CollectGarbageForProfile(true);
#endif
  	return getRecivedShutDown();
  } else {
  	return false;
  }
}

bool ObjectSizesProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "ObjectSizesProfiler -- dettaching thread pid: " << thProf->GetTid();
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}

void ObjectSizesProfiler::gcpFinalizeHistUpdates(void) {
	GCCohortManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	//we are relaxed we do not need to lookup for the whole records
	getObjHistograms()->gcpCheckForResetHist();
}


//inline void ObjectSizesProfiler::gcpResetLastLive(GCPHistogramRecord* globalRec,
//		GCPHistogramRecord* array) {
//
//	int32_t _allocBytes = GCPTotalAllocBytes.load();
//
//	size_t newCohortIndex  = (_allocBytes >> GCP_COHORT_LOG);
//
//	if(lastCohortIndex == newCohortIndex)
//		return;
//
//	lastCohortIndex = newCohortIndex;
////	int32_t readVal = 0;
//
////	while(UNLIKELY(android_atomic_cas(readVal, 1, &lastLiveGuard) != 0)) {
////		readVal = 0;
////	}
//
//
//	for(size_t i = 0; i < GCP_MAX_HISTOGRAM_SIZE; i++){
////		if(array[i].cntTotal < 1.0)
////			continue;
//		array[i].cntLive = 0.0;
//		array[i].cntTotal = 0.0;
//		array[i].pcntLive = 0.0;
//		array[i].pcntTotal = 0.0;
//	}
//	globalRec->cntLive = 0.0;
//	globalRec->cntTotal = 0.0;
//
////	do {
////		readVal = 0;
////	} while (UNLIKELY(android_atomic_cas(1, readVal, &lastLiveGuard) != 0));
//}

void ObjectSizesProfiler::gcpUpdateGlobalHistogram(void) {
	//we do not need to aggregate since we have only one histogram
	getObjHistograms()->gcpCalculateEntries(getObjHistograms()->histogramTable,
			&getObjHistograms()->histRecord);
	getObjHistograms()->gcpCalculateAtomicEntries(getObjHistograms()->lastWindowHistTable,
			&getObjHistograms()->histAtomicRecord);
}

//inline void ObjectSizesProfiler::gcpAggregateGlobalRecs(GCPHistogramRecord* globalRec,
//		GCPHistogramRecord* array, bool force) {
//
////	int32_t readVal = lastLiveGuard;
////
////	if(force) {
////		while(readVal != 0) {
////			readVal = 0;
////			if (LIKELY(android_atomic_cas(0, 1, &lastLiveGuard) == 0))
////				break;
////		}
////	}
//	if(globalRec->cntLive < 1.0 || globalRec->cntTotal < 1.0)
//		return;
//	for(size_t i = 0; i < GCP_MAX_HISTOGRAM_SIZE; i++) {
//		if(array[i].cntTotal < 1.0)
//			continue;
//		array[i].pcntLive = (array[i].cntLive * 100.0) / globalRec->cntLive;
//		array[i].pcntTotal = (array[i].cntTotal * 100.0) / globalRec->cntTotal;
//	}
//
////	if(force) {
////		do {
////			readVal = lastLiveGuard;
////		} while (UNLIKELY(android_atomic_cas(readVal, 0, &lastLiveGuard) != 0));
////	}
//}





void ObjectSizesProfiler::dumpProfData(bool isLastDump){
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);


  //gcpAggregateGlobalRecs(&globalRecord, histogramTable, false);

 // bool forceAtomicity = true;
  //if(isLastDump) forceAtomicity = false;
  //gcpAggregateGlobalRecs(&lastLiveRecord, lastLiveTable, forceAtomicity);
//  //get the percentage of each histogram entry
//	for(size_t i = 0; i < GCMMP_ARRAY_SIZE(histogramTable); i++){
//		if(histogramTable[i].cntTotal < 1.0)
//			continue;
//		histogramTable[i].pcntLive = (histogramTable[i].cntLive * 100.0) / globalRecord.cntLive;
//		histogramTable[i].pcntTotal = (histogramTable[i].cntTotal * 100.0) / globalRecord.cntTotal;
//	}

	//dump the heap stats
	dumpHeapStats();
	//dump the global entry

	bool _success = true;
	_success =
  	dump_file_->WriteFully(&getObjHistograms()->histRecord,
  			sizeof(GCPHistogramRec));

 if(_success) {
		//dump the histogram entries
	 gcpUpdateGlobalHistogram();

	 _success &= getObjHistograms()->gcpDumpHistTable(dump_file_);
	 _success &= getObjHistograms()->gcpDumpHistAtomicTable(dump_file_);
//	 _success =
//	   	dump_file_->WriteFully(histogramTable, totalHistogramSize);
//
//	 _success &=
//	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
//	 	  			sizeof(int));

//	 _success =
//	   	dump_file_->WriteFully(lastLiveTable, totalHistogramSize);

//	 _success &=
//	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
//	 	  			sizeof(int));



 }

 if(isLastDump) {
	 _success &=
	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
	 	  			sizeof(int));
	 //dump the summary one more time
	 _success &= getObjHistograms()->gcpDumpHistTable(dump_file_);
//	 _success &= objHistograms->gcpDumpHistAtomicTable(dump_file_);
//	 _success &=
//	   	dump_file_->WriteFully(histogramTable, totalHistogramSize);
//	 _success &=
//	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
//	 	  			sizeof(int));
	 	  if(_success) {
	 	  	LOG(ERROR) << "<<<< Succeeded dump to file" ;
	 	  }
	 	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
	 		dump_file_->Close();
	 		LOG(ERROR) <<  "ObjectSizesProfiler: done dumping data";
	 		logPerfData();
 } else {

		 gcpFinalizeHistUpdates();
//		 gcpFinalizeHistUpdates();
		// gcpResetLastLive(&lastLiveRecord, lastLiveTable);


 }



 if(!_success) {
	 LOG(ERROR) <<  "ObjectSizesProfiler: XXXX Error dumping data";
 }
}
/*
 * Return true only when the MProfiler is Running
 */
size_t ObjectSizesProfiler::GCPAddMProfilingExtraBytes(size_t allocBytes) {
	return allocBytes + GCPGetExtraProfileBytes();
//	VMProfiler* mP = Runtime::Current()->GetMProfiler();
//	if(mP != NULL && mP->IsProfilingEnabled()) {
//		return ((ObjectSizesProfiler*) mP)->getExtraProfileBytes() + allocBytes;
//	}
//	return allocBytes;
}

/*
 * Return true only when the MProfiler is Running
 */
size_t ObjectSizesProfiler::GCPRemoveMProfilingExtraBytes(size_t allocBytes) {
	return allocBytes - GCPGetExtraProfileBytes();
//	VMProfiler* mP = Runtime::Current()->GetMProfiler();
//	if(mP != NULL && mP->IsProfilingEnabled()) {
//		return allocBytes - ((ObjectSizesProfiler*) mP)->getExtraProfileBytes();
//	}
//	return allocBytes;
}

void ObjectSizesProfiler::GCPInitObjectProfileHeader(size_t allocatedMemory,
		mirror::Object* obj) {
	GCPExtraObjHeader* extraHeader =
			GCHistogramDataManager::GCPGetObjProfHeader(allocatedMemory, obj);
	extraHeader->histRecP = NULL;
	extraHeader->objSize = 0;
}



/********************* GCHistogramDataManager profiling ****************/



GCHistogramDataManager::GCHistogramDataManager(void) : type_(GCMMP_HIST_CHILD) {
	generateNewSecret();
	lastCohortIndex = GCCohortManager::kGCPLastCohortIndex.load();
	initHistograms();
}

GCHistogramDataManager::GCHistogramDataManager(GCMMP_HISTOGRAM_MGR_TYPE hisMGR) :
		type_(hisMGR) {
	lastCohortIndex = GCCohortManager::kGCPLastCohortIndex.load();
	initHistograms();
}


/********************* GCClassTableManager profiling ****************/


GCClassTableManager::GCClassTableManager(void) : GCHistogramDataManager() {

}

GCClassTableManager::GCClassTableManager(GCMMP_HISTOGRAM_MGR_TYPE hisMGR) :
		GCHistogramDataManager(hisMGR) {

}


inline void GCClassTableManager::addObjectClassPair(mirror::Class* klass,
		mirror::Object* obj) {
	size_t klassHash = (size_t)klass->IdentityHashCode();
	auto end = classTable_.end();
	GCPHistogramRec* _histRec = NULL;
  for (auto it = classTable_.lower_bound(klassHash);
  		it != end && it->first == klassHash; ++it) {
  	_histRec = &it->second;
  	_histRec->cntLive++;
  }

  if(_histRec == NULL) {

  }

}




inline void GCClassTableManager::addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	GCPExtraObjHeader* extraHeader = GCPGetObjProfHeader(allocatedMemory, obj);
	extraHeader->objSize = objSize;
	extraHeader->histRecP = this;
	size_t histIndex = (32 - CLZ(objSize)) - 1;

	if(histIndex ==0)
		return;
//	 mirror::Class* _klass = obj->GetClass();
//	 if(_klass == NULL) {
//		 LOG(ERROR) << "XXXXXXXXX OBJECT CLASS IS NULL";
//	 }
//	std::make_pair(hash, klass)
//	classTable_


//	int32_t _readCohortIndex = (GCCohortManager::kGCPLastCohortIndex.load());
//
//	if(lastCohortIndex != _readCohortIndex) {
//		lastCohortIndex = _readCohortIndex;
//		histAtomicRecord.cntLive.store(1);
//		histAtomicRecord.cntTotal.store(1);
//		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
//			lastWindowHistTable[i].cntTotal  = 0.0;
//			lastWindowHistTable[i].cntLive  = 0.0;
//		}
//		lastWindowHistTable[histIndex].cntTotal.store(1);
//		lastWindowHistTable[histIndex].cntLive.store(1);
//	} else {
//		gcpAddDataToHist(&histogramTable[histIndex]);
//		gcpAddDataToHist(&histRecord);
//
//		histAtomicRecord.cntLive++;
//		histAtomicRecord.cntTotal++;
//		lastWindowHistTable[histIndex].cntTotal++;
//		lastWindowHistTable[histIndex].cntLive++;
//	}
}

/********************* GCHistogramManager profiling ****************/
void GCHistogramManager::initHistograms(void){
	totalHistogramSize = kGCMMPMaxHistogramEntries * sizeof(GCPHistogramRec);
	lastWindowHistSize = kGCMMPMaxHistogramEntries * sizeof(GCPHistogramRecAtomic);

	gcpResetHistogramData();
	gcpResetAtomicData();

}



GCHistogramManager::GCHistogramManager(void) : GCHistogramDataManager() {

}

GCHistogramManager::GCHistogramManager(GCMMP_HISTOGRAM_MGR_TYPE hisMGR) :
		GCHistogramDataManager(hisMGR) {

}


inline void  GCHistogramManager::gcpAddDataToHist(GCPHistogramRec* rec) {
	rec->cntLive++;
	rec->cntTotal++;
}

inline bool GCHistogramManager::gcpRemoveAtomicDataFromHist(GCPHistogramRecAtomic* rec) {
	bool modified = false;
	if(rec->cntLive.load() > 0) {
		rec->cntLive--;
		modified = true;
	}
  return modified;
}

bool GCHistogramManager::gcpRemoveDataFromHist(GCPHistogramRec* rec) {
	bool modified = false;
//	if(true)
//		return false;
	if (rec->cntLive >= 1.0) {
		rec->cntLive--;
		modified = true;
	}
  return modified;
}


inline void GCHistogramManager::addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	GCPExtraObjHeader* extraHeader = GCPGetObjProfHeader(allocatedMemory, obj);
	extraHeader->objSize = objSize;
	extraHeader->histRecP = this;
	size_t histIndex = (32 - CLZ(objSize)) - 1;

	int32_t _readCohortIndex = (GCCohortManager::kGCPLastCohortIndex.load());

	if(lastCohortIndex != _readCohortIndex) {
		lastCohortIndex = _readCohortIndex;
		histAtomicRecord.cntLive.store(1);
		histAtomicRecord.cntTotal.store(1);
		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
			lastWindowHistTable[i].cntTotal  = 0.0;
			lastWindowHistTable[i].cntLive  = 0.0;
		}
		lastWindowHistTable[histIndex].cntTotal.store(1);
		lastWindowHistTable[histIndex].cntLive.store(1);
	} else {
		gcpAddDataToHist(&histogramTable[histIndex]);
		gcpAddDataToHist(&histRecord);

		histAtomicRecord.cntLive++;
		histAtomicRecord.cntTotal++;
		lastWindowHistTable[histIndex].cntTotal++;
		lastWindowHistTable[histIndex].cntLive++;
	}
}


void GCHistogramManager::gcpRemoveObject(size_t histIndex) {
//	LOG(ERROR) << "passing+++histIndex << " <<histIndex;
	bool removedFlag = gcpRemoveDataFromHist(&histogramTable[histIndex]);

	if(removedFlag) {
		gcpRemoveDataFromHist(&histRecord);
	}

	//todo: this does not make sense
//	LOG(ERROR) << "Done+++histIndex a " << histIndex;
	if(type_ != GCMMP_HIST_ROOT) {
		if(lastCohortIndex != GCCohortManager::kGCPLastCohortIndex.load()){
			//we cannot remove since there was no allocation done
			return;
		}
	}
	removedFlag = gcpRemoveAtomicDataFromHist(&lastWindowHistTable[histIndex]);
	if(removedFlag) {
		gcpRemoveAtomicDataFromHist(&histAtomicRecord);
	}
//	LOG(ERROR) << "Done+++histIndex b " << histIndex;
}


void GCHistogramManager::GCPRemoveObj(size_t allocatedMemory,
		mirror::Object* obj) {

	GCPExtraObjHeader* extraHeader = GCPGetObjProfHeader(allocatedMemory, obj);

	GCHistogramManager* threadHistRec = extraHeader->histRecP;

	if(threadHistRec == NULL || extraHeader->objSize == 0)
		return;

	size_t histIndex = (32 - CLZ(extraHeader->objSize)) - 1;
	threadHistRec->gcpRemoveObject(histIndex);
}

inline void GCHistogramManager::gcpAggregateHistograms(GCPHistogramRec* hisTable,
		GCPHistogramRec* globalRec) {
	if(false && histRecord.cntTotal <= 0.0)
		return;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
		hisTable[i].index 				= histogramTable[i].index;
		hisTable[i].cntLive 			+= histogramTable[i].cntLive;
		hisTable[i].cntTotal 		  += histogramTable[i].cntTotal;
	}
	globalRec->cntLive 	+= histRecord.cntLive;
	globalRec->cntTotal += histRecord.cntTotal;
}

inline void GCHistogramManager::gcpAggAtomicHistograms(GCPHistogramRecAtomic* hisTable,
		GCPHistogramRecAtomic* globalRec) {
//	int32_t total = ;
//	if(false && total == 0)
//		return;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
		hisTable[i].index 				= lastWindowHistTable[i].index;
		hisTable[i].cntLive.fetch_add(lastWindowHistTable[i].cntLive.load());
		hisTable[i].cntTotal.fetch_add(lastWindowHistTable[i].cntTotal.load());
	}
	globalRec->cntTotal.fetch_add(histAtomicRecord.cntTotal.load());
	globalRec->cntLive.fetch_add(histAtomicRecord.cntLive.load());
}


inline void GCHistogramManager::gcpCalculateEntries(GCPHistogramRec* hisTable,
		GCPHistogramRec* globalRec) {
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		if(hisTable[i].cntTotal < 1.0)
			continue;
		hisTable[i].pcntLive = (hisTable[i].cntLive * 100.0) / globalRec->cntLive;
		hisTable[i].pcntTotal = (hisTable[i].cntTotal * 100.0) / globalRec->cntTotal;
	}
}

inline void GCHistogramManager::gcpCalculateAtomicEntries(GCPHistogramRecAtomic* hisTable,
		GCPHistogramRecAtomic* globalRec) {
	int32_t cntLive = globalRec->cntLive.load();
	int32_t cntTotal = globalRec->cntTotal.load();
	int32_t entryTotal = 0;
	if(cntTotal == 0)
		return;
	if(cntLive == 0) {
		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
			entryTotal = hisTable[i].cntTotal.load();
			hisTable[i].pcntLive = 0.0;
//			if(entryTotal < 1)
//				continue;
			hisTable[i].pcntTotal = (entryTotal * 100.0) / cntTotal;
		}
	} else  {
		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
			entryTotal = hisTable[i].cntTotal.load();
//			if(entryTotal < 1)
//				continue;
			hisTable[i].pcntLive = (hisTable[i].cntLive.load() * 100.0) / cntLive;
			hisTable[i].pcntTotal = (entryTotal * 100.0) / cntTotal;
		}
	}

}



bool GCHistogramManager::gcpDumpHistTable(art::File* dump_file) {
	 bool _success =
	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	 _success &=
			 dump_file->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
	 	  			sizeof(int));
	 return _success;
}


bool GCHistogramManager::gcpCheckForResetHist(void) {
	if(lastCohortIndex != GCCohortManager::kGCPLastCohortIndex.load()){
		//reset percentages in the atomic fields
		histAtomicRecord.pcntLive = 0.0;
		histAtomicRecord.pcntTotal = 0.0;
		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
			lastWindowHistTable[i].pcntLive = 0.0;
			lastWindowHistTable[i].pcntTotal = 0.0;
		}
		return true;
	}
	return false;
}

bool GCHistogramManager::gcpCheckForCompleteResetHist(void) {
	int32_t _loadedIndex = GCCohortManager::kGCPLastCohortIndex.load();
	if(lastCohortIndex != _loadedIndex) {
		setLastCohortIndex(_loadedIndex);
		//reset percentages in the atomic fields
		histAtomicRecord.pcntLive = 0.0;
		histAtomicRecord.pcntTotal = 0.0;
		histAtomicRecord.cntLive.store(0);
		histAtomicRecord.cntTotal.store(0);
		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
			lastWindowHistTable[i].pcntLive = 0.0;
			lastWindowHistTable[i].pcntTotal = 0.0;
			lastWindowHistTable[i].cntLive.store(0);
			lastWindowHistTable[i].cntTotal.store(0);
		}
		return true;
	}
	return false;
}


bool GCHistogramManager::gcpDumpHistAtomicTable(art::File* dump_file) {
	GCPHistogramRec dummyRec;
	bool _success = true;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
		 _success &=
		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
	 _success &=
			 dump_file->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
	 	  			sizeof(int));
	 return _success;
}


inline bool GCHistogramManager::gcpDumpHistAtomicRec(art::File* dump_file) {
	GCPHistogramRec dummyRec;
	GCPCopyRecords(&dummyRec, &histAtomicRecord);
	return dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
}

inline bool GCHistogramManager::gcpDumpHistRec(art::File* dump_file) {
	return dump_file->WriteFully(&histRecord, sizeof(GCPHistogramRec));
}

/********************************* Thread Alloc Profiler ****************/

void ThreadAllocProfiler::initHistogram(void) {
	GCCohortManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	hitogramsData = new GCHistogramManager(GCMMP_HIST_ROOT);
}

bool ThreadAllocProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "ThreadAllocProfiler -- dettaching thread pid: " << thProf->GetTid();
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}

void ThreadAllocProfiler::setHistogramManager(GCMMPThreadProf* thProf) {
	thProf->histogramManager = new GCHistogramManager();
	thProf->histogramManager->histRecord.index = thProf->GetTid();
	thProf->histogramManager->histAtomicRecord.index = thProf->GetTid();
}

bool ThreadAllocProfiler::periodicDaemonExec(void) {
	Thread* self = Thread::Current();
  if(waitForProfileSignal()) { //we recived Signal to Shutdown
    GCMMP_VLOG(INFO) << "ThreadAllocProfiler: signal Received " << self->GetTid() ;
    //LOG(ERROR) << "periodic daemon recieved signals tid: " <<  self->GetTid();

    {
    	MutexLock mu(self, *prof_thread_mutex_);
    	receivedSignal_ = false;
    }
 //
 //
#if GCP_COLLECT_FOR_PROFILE
    	gc::Heap* heap_ = Runtime::Current()->GetHeap();
    	heap_->CollectGarbageForProfile(false);
#endif
    updateHeapAllocStatus();

    if(getRecivedShutDown()) {
    	LOG(ERROR) << "received shutdown tid: " <<  self->GetTid();

    } else {
    	dumpProfData(false);
    }

  	return getRecivedShutDown();
  } else {
  	return false;
  }
}

//gcpAggregateHistograms(GCPHistogramRec* hisTable,
//		GCPHistogramRec* globalRec)

void ThreadAllocProfiler::logPerfData() {

	int32_t currBytes_ = GCPTotalAllocBytes.load();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes() << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();

	GCPHistogramRec* _tempRec = NULL;
	for (const auto& threadProf : threadProfList_) {
		GCHistogramManager* _histMgr = threadProf->histogramManager;
		if(_histMgr != NULL) {
			_tempRec = &_histMgr->histRecord;
			LOG(ERROR) << "index: " << _tempRec->index << " :: cntLive=" <<
					_tempRec->cntLive << "; cntTotal="<< _tempRec->cntTotal <<
					"; pcntLive=" << _tempRec->pcntLive <<
					"; pcntTotal="<< _tempRec->pcntTotal;
		}
	}
}


void ThreadAllocProfiler::gcpUpdateGlobalHistogram(void) {
	//set a new secret to make sure all the manager are not ousiders:
	GCHistogramManager* _histManager = getThreadHistograms();
	int _newSecret = _histManager->generateNewSecret();
	for (const auto& threadProf : threadProfList_) {
		GCHistogramManager* _histMgr = threadProf->histogramManager;
		if(_histMgr != NULL) {
			_histMgr->setFriendISecret(_newSecret);
			_histMgr->gcpAggregateHistograms(_histManager->histogramTable,
					&_histManager->histRecord);
			_histMgr->gcpAggAtomicHistograms(_histManager->lastWindowHistTable,
					&_histManager->histAtomicRecord);
		}
	}

	int32_t _cntAtomicTotal = _histManager->histAtomicRecord.cntTotal.load();
	int32_t _cntAtomicLive = _histManager->histAtomicRecord.cntLive.load();
	for (const auto& threadProf : threadProfList_) {
		GCHistogramManager* _histMgr = threadProf->histogramManager;
		if(_histMgr != NULL) {
			_histMgr->histAtomicRecord.pcntLive = 0.0;
			_histMgr->histAtomicRecord.pcntTotal = 0.0;

			if(!_histManager->gcpIsManagerFriend(_histMgr)) {
				LOG(ERROR) << "^^^^^^ Found  sneaky Histogram manager ^^^^^";
				_histMgr->histRecord.pcntLive = 0.0;
				_histMgr->histRecord.pcntTotal = 0.0;
			} else {
				_histMgr->histRecord.pcntLive =
						(_histMgr->histRecord.cntLive * 100.0) / _histManager->histRecord.cntLive;
				_histMgr->histRecord.pcntTotal =
						(_histMgr->histRecord.cntTotal * 100.0) / _histManager->histRecord.cntTotal;

				_histMgr->histAtomicRecord.pcntLive = _cntAtomicLive == 0 ? 0.0 :
						(_histMgr->histAtomicRecord.cntLive.load() * 100.0) / _cntAtomicLive;
				_histMgr->histAtomicRecord.pcntTotal = _cntAtomicTotal == 0 ? 0.0 :
						(_histMgr->histAtomicRecord.cntTotal.load() * 100.0) / _cntAtomicTotal;
			}
		}
	}

//	objHistograms->gcpCalculateEntries(objHistograms->histogramTable,
//			&objHistograms->histRecord);
//	objHistograms->gcpCalculateAtomicEntries(objHistograms->lastWindowHistTable,
//			&objHistograms->histAtomicRecord);
}

void ThreadAllocProfiler::gcpFinalizeHistUpdates(void) {
	GCCohortManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	GCHistogramManager* _histManager = getThreadHistograms();
	bool shouldUpdate = _histManager->gcpCheckForCompleteResetHist();
	if(shouldUpdate) {
		int32_t _cohortIndex =  GCCohortManager::kGCPLastCohortIndex.load();
		for (const auto& threadProf : threadProfList_) {
			GCHistogramManager* _histMgr = threadProf->histogramManager;
			if(_histMgr != NULL) {
				_histMgr->histAtomicRecord.cntLive.store(0);
				_histMgr->histAtomicRecord.cntTotal.store(0);
				_histMgr->histAtomicRecord.pcntLive = 0.0;
				_histMgr->histAtomicRecord.pcntTotal = 0.0;
				_histMgr->histAtomicRecord.index = threadProf->GetTid();
				_histMgr->setLastCohortIndex(_cohortIndex);
			}
		}
	}
	_histManager->gcpResetHistogramData();
	_histManager->gcpResetAtomicData();
}


inline void ThreadAllocProfiler::dumpHeapStats(void) {
	bool successWrite = dump_file_->WriteFully(&heapStatus, sizeof(GCMMPHeapStatus));
	if(successWrite) {

	} else {
		LOG(ERROR) << "could not dump heap stats";
	}
}


bool ThreadAllocProfiler::verifyThreadNotification() {
	bool _shouldApply = true;
	Thread* thread = Thread::Current();
	GCMMPThreadProf* threadProf = thread->GetProfRec();
	if(threadProf == NULL)
		_shouldApply = false;
	else {
		if(threadProf->state != GCMMP_TH_RUNNING) {
			GCMMP_VLOG(INFO) <<
					"VMProfiler: Allocation is not tracked because the thread is not profiled "
					<< thread->GetTid() ;
			_shouldApply = false;
		}
	}
	return _shouldApply;
}


bool ThreadAllocProfiler::dumpGlobalThreadsStats(void) {
	GCHistogramManager* _histMgr = NULL;
	bool _success = true;
	int _count = 0;
	for (const auto& threadProf : threadProfList_) {
		_histMgr = threadProf->histogramManager;
		if(_histMgr == NULL)
			continue;
		_success &= _histMgr->gcpDumpHistRec(dump_file_);
		if(!_success)
			return false;
		_count++;
	}

	if(_count > 0) {
		_success &= dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
				 	  			sizeof(int));
	}
	return _success;
}


bool ThreadAllocProfiler::dumpGlobalThreadsAtomicStats(void) {
	GCHistogramManager* _histMgr = NULL;
	bool _success = true;
	int _count = 0;
	for (const auto& threadProf : threadProfList_) {
		_histMgr = threadProf->histogramManager;
		if(_histMgr == NULL)
			continue;
		_success &= _histMgr->gcpDumpHistAtomicRec(dump_file_);
		if(!_success)
			return false;
		_count++;
	}
	if(_count > 0) {
		_success &= dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
				 	  			sizeof(int));
	}
	return _success;
}


void ThreadAllocProfiler::dumpProfData(bool isLastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
	//dump the heap stats
	dumpHeapStats();
	//dump the global entry
	gcpUpdateGlobalHistogram();
		//dump the global stats

//	GCPHistogramRec dummyRec;
//	dummyRec.index = 1;
//	dummyRec.cntLive = 1;
//	dummyRec.cntTotal = 1;
//	dummyRec.pcntLive = 1;
//	dummyRec.pcntTotal = 1;
	bool _success =
	  	dump_file_->WriteFully(&getThreadHistograms()->histRecord,
	  			sizeof(GCPHistogramRec));
	if(_success) {
		_success &= dumpGlobalThreadsStats();
		_success &= dumpGlobalThreadsAtomicStats();
	}

  if(isLastDump && _success) {
	  _success &=
	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
	 	  			sizeof(int));
	  //dump the summary at the end one more time
	  _success &= dumpGlobalThreadsStats();
	 	if(_success) {
	 		LOG(ERROR) << "<<<< Succeeded dump to file" ;
	 	}
	 	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
	 	dump_file_->Close();
	 	LOG(ERROR) <<  "ObjectSizesProfiler: done dumping data";
	 	logPerfData();
  } else {
		 gcpFinalizeHistUpdates();
//		 gcpFinalizeHistUpdates();
		// gcpResetLastLive(&lastLiveRecord, lastLiveTable);
 }
 if(!_success) {
	 LOG(ERROR) <<  "ObjectSizesProfiler: XXXX Error dumping data";
 }
}





void ThreadAllocProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize){
	return;
}

inline void ThreadAllocProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	GCMMPThreadProf* thProf = Thread::Current()->GetProfRec();
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
		if(thProf->histogramManager != NULL)
			thProf->histogramManager->addObject(allocatedMemory, objSize, obj);
	}
}


/******************** GCCohortManager ***********************/

GCCohortManager::GCCohortManager(AtomicInteger* allocRec) {
	//get the correct cohort index;
	cohortIndex_ = 0;
	allocRec_ = allocRec;
	initCohortsTable();
}


void GCCohortManager::addCohortRow(void) {
	currCoRowP = (GCPCohortsRow*) calloc(1, getCoRowSZ());
	currCoRowP->index_ = 0;
	cohortsTable.cohortRows_.push_back(currCoRowP);
}

void GCCohortManager::addCohortRecord(void) {
	if(currCoRowP->index_ == GCP_MAX_COHORT_ROW_CAP) {
		//we passed the capacity and we need a new row
		addCohortRow();
	}
	int _index = (currCohortP == NULL) ?
			 VMProfiler::GCPGetCalcCohortIndex() : currCohortP->index_+1;
	currCohortP = &currCoRowP->cohorts[currCoRowP->index_];
	currCoRowP->index_++;

	currCohortP->index_ = _index;
}



void GCCohortManager::initCohortsTable(void) {
	cohRowSZ_ = kGCMMPMaxRowCap * sizeof(GCPCohortRecordData);
	cohArrSZ_ = kGCMMPMaxTableCap * sizeof(GCPCohortsRow*);

	cohortsTable.index = 0;
	currCohortP = NULL;

	addCohortRow();
	addCohortRecord();
}


inline void GCCohortManager::addObjectToCohRecord(size_t objSize) {

	size_t sizeObjLeft = objSize;
	size_t cohSpaceLeft = 0;
	size_t iterFitSize = 0;
	bool _firstCohort = true;
	while(sizeObjLeft != 0) {
		cohSpaceLeft = getSpaceLeftCohort(currCohortP);
		if(cohSpaceLeft == 0) {
			addCohortRecord();
			continue;
		}
		iterFitSize = std::min(sizeObjLeft, cohSpaceLeft);
		updateCohRecObjBytes(currCohortP, iterFitSize);
		sizeObjLeft -= iterFitSize;
		if(_firstCohort) {
			updateCohRecObjCnts(currCohortP);
			_firstCohort = false;
		}
	}
}

void GCCohortManager::addObjCohorts(size_t allocatedMemory,
				size_t objSize, mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
			GCHistogramManager::GCPGetObjProfHeader(allocatedMemory, obj);
	addObjectToCohRecord(objSize);
	_profHeader->objSize = objSize;
	//we need to calculate the correct bytes without the allocated memory
	_profHeader->objBD = allocRec_->load()-objSize;
}


void GCCohortManager::gcpRemoveObject(size_t allocSpace, mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
			GCHistogramManager::GCPGetObjProfHeader(allocSpace, obj);
	if(_profHeader->objSize == 0) {
		//the object was not registered
		LOG(ERROR) << "---------Found none registered object";
		return;
	}

	size_t _startRow, _startIndex, _endRow, _endIndex = 0;
	getCoAddrFromBytes(&_startRow, &_startIndex, &_endRow, &_endIndex,
			_profHeader->objBD, _profHeader->objSize);

	GCPCohortRecordData* _firstRecP = NULL;
	GCPCohortRecordData* _LastRecP = NULL;
	size_t _rowIter  = _startRow;
	size_t _colIter  = _startIndex;

	if(_startIndex >= (size_t) kGCMMPMaxRowCap || _endIndex >= (size_t)kGCMMPMaxRowCap)
		LOG(ERROR) << "startRow=" << _startRow<< "; startInd=" << _startIndex << "; endRow=" << _endRow << "; endIndex=" << _endIndex;
	_firstRecP = getCoRecFromIndices(_startRow, _startIndex);
	if(_firstRecP == NULL) {
		LOG(ERROR) << "NULL:::BD="<<_profHeader->objBD<<"; currentBytes="<< allocRec_->load()<<"; capacit=" << cohortsTable.cohortRows_.size() <<",startRow=" << _startRow<< "; startInd=" << _startIndex << "; endRow=" << _endRow << "; endIndex=" << _endIndex;
		return;
	}

	//for performance we need only to handle last and first cohort;
	if(_startRow == _endRow && _startIndex == _endIndex) {
		//easy case: the object resides in 1 cohort;
		updateDelCohRecObj(_firstRecP, _profHeader->objSize);
		updateDelCohRecObjCnts(_firstRecP);
	} else {
		//first precisely calculate the cohort boundaries
		_LastRecP = getCoRecFromIndices(_endRow, _endIndex);
		updateDelCohRecObj(_LastRecP,
				(_profHeader->objBD + _profHeader->objSize) % kGCMMPCohorSize);
		updateDelCohRecObj(_firstRecP,
				(kGCMMPCohorSize - (_profHeader->objBD % kGCMMPCohorSize)));
		updateDelCohRecObjCnts(_firstRecP);


		while(true) {
			incColIndex(&_rowIter, &_colIter);
			if(_colIter == _endIndex && _endRow == _rowIter)
				break;
			if(_rowIter >= cohortsTable.cohortRows_.size() || _colIter >= (size_t)kGCMMPMaxRowCap){
				LOG(ERROR) << "2--NULL:::BD="<<_profHeader->objBD<<"; currentBytes="<< allocRec_->load()<<"; capacit=" << cohortsTable.cohortRows_.size() <<",startRow=" << _startRow<< "; startInd=" << _startIndex << "; endRow=" << _endRow << "; endIndex=" << _endIndex;
				return;
			}
			_LastRecP =  getCoRecFromIndices(_rowIter, _colIter);
			updateDelCohRecObj(_LastRecP, kGCMMPCohorSize);
		}
	}

}



GCPCohortRecordData* GCCohortManager::getCoRecFromObj(size_t allocSpace,
		mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
			GCHistogramManager::GCPGetObjProfHeader(allocSpace, obj);
	if(_profHeader->objSize == 0) //the object was not registered
		return NULL;
	size_t _cohIndex = (_profHeader->objBD >> GCP_COHORT_LOG);
	size_t _rowIndex = _cohIndex /  kGCMMPMaxRowCap;
	GCPCohortsRow* _row = cohortsTable.cohortRows_[_rowIndex];
	GCPCohortRecordData* _cohRec = &_row->cohorts[_cohIndex%_rowIndex];

	return _cohRec;

}

void GCCohortManager::gcpDumpCohortData(art::File* dumpFile) {
	bool _print   = false;
	bool _success = true;
	//GCPCohortRecordData* _recP = NULL;
	size_t _rowBytes = 0;
	//LOG(ERROR) << "dumpRows----";
	int _index = 0;
	for (const auto& _rowIterP : cohortsTable.cohortRows_) {
		_rowBytes = (_rowIterP->index_) * sizeof(GCPCohortRecordData);
		//LOG(ERROR) << _index << "::dump Row: " << _rowIterP->index_;
		if(_rowIterP->index_ > kGCMMPMaxRowCap) {

			LOG(ERROR) << "Index out of boundary : " << _rowIterP->index_;
		}
		_index++;
		if(_rowBytes == 0)
			break;
		_print = true;
		_success &= dumpFile->WriteFully(_rowIterP->cohorts, _rowBytes);

	}

	if(_print) {
		_success &= dumpFile->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
				 	  			sizeof(int));
	}
}


/********************************* Cohort profiling ****************/

void CohortProfiler::initHistogram(void) {

}

void CohortProfiler::setHistogramManager(GCMMPThreadProf* thProf) {
	thProf->histogramManager = NULL;
}


bool CohortProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "CohortProfiler -- dettaching thread pid: " <<
				thProf->GetTid();
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}


inline void CohortProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	cohMgr->addObjCohorts(allocatedMemory, objSize, obj);
}

void CohortProfiler::dumpProfData(bool isLastDump) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
	//dump the heap stats
	dumpHeapStats();
	//dump the global entry

	cohMgr->gcpDumpCohortData(dump_file_);

	//gcpUpdateGlobalHistogram();
		//dump the global stats

  if(isLastDump) {
	  bool _success = false;
  	_success &=
	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
	 	  			sizeof(int));
	 	if(_success) {
	 		LOG(ERROR) << "<<<< Succeeded dump to file" ;
	 	}
	 	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
	 	dump_file_->Close();
	 	LOG(ERROR) <<  "CohortProfiler: done dumping data";
	 	logPerfData();
  }
}


inline void CohortProfiler::gcpRemoveObject(size_t allocatedMemory,
		mirror::Object* obj) {
	Thread* self = Thread::Current();
	MutexLock mu(self, *prof_thread_mutex_);
	cohMgr->gcpRemoveObject(allocatedMemory, obj);
	//GCHistogramManager::GCPRemoveObj(allocatedMemory, obj);
}

void CohortProfiler::logPerfData() {

	int32_t currBytes_ = GCPTotalAllocBytes.load();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes() << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();

	//GCPCohortRecordData* _recP = NULL;
	size_t _rowBytes = 0;
	int _rIndex = 0;
	for (const auto& _rowIterP : cohMgr->cohortsTable.cohortRows_) {
		_rowBytes = (_rowIterP->index_) * sizeof(GCPCohortRecordData);
		if(_rowBytes == 0)
			break;
		LOG(ERROR) << "++ROW: " << _rIndex;
		int _indIter = 0;
		while(_indIter < _rowIterP->index_) {
			GCPCohortRecordData* _recData = &_rowIterP->cohorts[_indIter];
			LOG(ERROR) << "Cohort: "<< _recData->index_ << ", liveSize: " <<
					_recData->liveSize << ", totalSize: " << _recData->totalSize <<
					", objsCnt: " << _recData->objLiveCnt << ", totalObjs: " <<
					_recData->objTotalCnt;
			_indIter++;
		}
		_rIndex++;
	}
}
/************************ Class Loader *********************/


inline void ClassProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	getClassHistograms()->addObject(allocatedMemory, objSize, obj);
}
void ClassProfiler::initHistogram(void) {
	hitogramsData = new GCClassTableManager();
}

//class_Linker
void ClassProfiler::dumpAllClasses(void) {
	//std::ostringstream os;
 // os << "Dumping the Classes::::\n";
	ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
	Runtime::Current()->GetClassLinker()->GCPDumpAllClasses(7, LOG(ERROR));

	LOG(ERROR) << "++++++++++++++++ Counting for each class++++++++++++++++++++";
  std::vector<mirror::Class*> classes;

  Runtime::Current()->GetClassLinker()->GCPGetAllClasses(classes);
  std::vector<uint64_t> countVector;
  for(size_t _indexIter = 0; _indexIter < classes.size(); _indexIter++) {
  	countVector.push_back(0);
  }

  Runtime::Current()->GetHeap()->CountInstances(classes, false, &countVector[0]);
  int _index = 0;
  for (const auto& _rowCountIter : countVector) {
  	LOG(ERROR) << "count: " << _index++ << ": " << _rowCountIter;
  }
}

void ClassProfiler::logPerfData() {
	dumpAllClasses();
}

void ClassProfiler::dumpProfData(bool isLastDump) {
	if(isLastDump) {
		LOG(ERROR) << "ClassProfiler: dumpProfData with lastDump is true";
		logPerfData();
		dump_file_->Close();
		LOG(ERROR) << "ClassProfiler: Terminating dumpProfData with lastDump is true";
	} else {
		LOG(ERROR) << "ClassProfiler: dumpProfData with lastDump is flase";
	}
}

//
//void CohortProfiler::addCohortRecord(void) {
//	if(currCohortRow == NULL) {
//		addCohortRow();
//	}
//	if(currCohortRow->index >= GCP_MAX_COHORT_ROW_SIZE) {
//		addCohortRow();
//	}
//	currCohortRec = &(currCohortRow->cohortArr[currCohortRow->index]);
//	currCohortRow->index++;
//	memset((void*)(currCohortRec), 0, sizeof(GCPCohortRecord));
//	currCohortRec->index = 1.0 * cohortIndex;
//	memset((void*)currCohortRec->histogramTable, 0, totalHistogramSize);
//	for(size_t i = 0; i < GCMMP_ARRAY_SIZE(currCohortRec->histogramTable); i++) {
//		currCohortRec->histogramTable[i].index = (i+1) * 1.0;
//	}
//	cohortIndex++;
//}
//
//void CohortProfiler::addCohortRow(void) {
//	currCohortRow = (GCPCohortsRow*) calloc(1, cohortRowSize);
//	currCohortRow->index = 0;
//	memset((void*)(currCohortRow->cohortArr), 0, cohortRowSize);
//	cohortsTable.cohortRows[cohortsTable.index++] = currCohortRow;
//}
//
//
//
//void CohortProfiler::initCohortsTable(void) {
//	cohortArrayletSize = GCP_MAX_COHORT_ARRAYLET_SIZE * sizeof(GCPCohortsRow*);
//	cohortRowSize = GCP_MAX_COHORT_ROW_SIZE * sizeof(GCPCohortsRow);
//	memset((void*)(cohortsTable.cohortRows), 0, cohortArrayletSize);
//	cohortIndex = 0;
//	cohortsTable.index = 0;
//	currCohortRow = NULL;
//	currCohortRec = NULL;
//
//	addCohortRecord();
//
//	totalHistogramSize = GCP_MAX_HISTOGRAM_SIZE * sizeof(GCPHistogramRecord);
//
//	globalRecord.pcntLive  = 100.0;
//	globalRecord.pcntTotal = 100.0;
//	memset((void*)histogramTable, 0, totalHistogramSize);
//
//	for(size_t i = 0; i < GCMMP_ARRAY_SIZE(histogramTable); i++){
//		histogramTable[i].index = (i+1) * 1.0;
//	}
//}
//
//inline void CohortProfiler::addObjectToCohortRecord(GCPCohortRecord* rec,
//		size_t objSize, size_t fitSize, bool shouldCnt) {
//	//get histograms
//	size_t histIndex = 32 - CLZ(objSize) - 1;
//
//	if(shouldCnt) {
//		gcpAddDataToHist(&rec->cohortObjStats);
//		gcpAddDataToHist(&(rec->histogramTable[histIndex]));
//
//		gcpAddDataToHist(&globalRecord);
//	}
//	rec->cohortVolumeStats.cntLive  += fitSize;
//	rec->cohortVolumeStats.cntTotal += fitSize;
//}


//inline void CohortProfiler::gcpRemoveObject(size_t objSize, size_t allocSize){
//	size_t histIndex = 32 - CLZ(objSize) - 1;
//	if(histIndex == 0)
//		return;
//	if(allocSize == objSize) {
//		LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
//	}
//}
//
//
//inline void CohortProfiler::gcpAddObject(size_t allocatedMemory,
//		size_t objSize, mirror::Object* obj){
//	size_t histIndex = 32 - CLZ(objSize) - 1;
//	byte* address = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(obj) +
//			allocatedMemory - sizeof(GCPObjectExtraHeader));
//	GCPObjectExtraHeader* extraHeader = reinterpret_cast<GCPObjectExtraHeader*>(address);
//	extraHeader->objSize = objSize;
//	gcpAddDataToHist(&histogramTable[histIndex]);
//	gcpAddDataToHist(&globalRecord);
////	if(false && allocSize == objSize) {
////			LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
////
//}
//
//inline void CohortProfiler::gcpAddObject(size_t objSize, size_t allocSize) {
//	//get cohorts
//	bool firstLoop = true;
//	size_t sizeObjLeft = objSize;
//	if(allocSize == objSize) {
//		LOG(ERROR) << "<<<< weird: both sizes are equal: " << allocSize;
//	}
//	size_t cohortSpaceLeft = 0;
//	size_t fitSize = 0;
//	while(sizeObjLeft != 0) {
//		cohortSpaceLeft =
//				GCP_COHORT_SIZE - currCohortRec->cohortVolumeStats.cntTotal;
//		if(cohortSpaceLeft != 0) {
//			fitSize = std::min(sizeObjLeft, cohortSpaceLeft);
//			sizeObjLeft -= fitSize;
//			addObjectToCohortRecord(currCohortRec, objSize, fitSize, firstLoop);
//			firstLoop &= false;
//		}
//		if(sizeObjLeft != 0) {
//			addCohortRecord();
//		}
//	}
//}

//
//inline void CohortProfiler::dumpCohortGeneralStats(void) {
//	LOG(ERROR) << "<<<< currentCohortIndex: " << cohortIndex ;
//}
//
//void CohortProfiler::dumpProfData(bool isLastDump){
//  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
//
//  if(isLastDump || true) {
//  	dumpCohortGeneralStats();
//  }
//
////  //get the percentage of each histogram entry
////	for(size_t i = 0; i < GCMMP_ARRAY_SIZE(histogramTable); i++){
////		if(histogramTable[i].cntTotal < 1.0)
////			continue;
////		histogramTable[i].pcntLive = (histogramTable[i].cntLive * 100.0) / globalRecord.cntLive;
////		histogramTable[i].pcntTotal = (histogramTable[i].cntTotal * 100.0) / globalRecord.cntTotal;
////	}
////
////	//dump the heap stats
////	dumpHeapStats();
////	//dump the global entry
////
////	bool _success = true;
////	_success =
////  	dump_file_->WriteFully(&globalRecord,
////  			sizeof(GCPHistogramRecord));
////
//// if(_success) {
////		//dump the histogram entries
////	 _success =
////	   	dump_file_->WriteFully(histogramTable, totalHistogramSize);
////	 _success &=
////	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
////	 	  			sizeof(int));
//// }
////
//// if(isLastDump) {
////	 _success &=
////	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
////	 	  			sizeof(int));
////	 //dump the summary one more time
////	 _success &=
////	   	dump_file_->WriteFully(histogramTable, totalHistogramSize);
////	 _success &=
////	 	  	dump_file_->WriteFully(&mprofiler::VMProfiler::kGCMMPDumpEndMarker,
////	 	  			sizeof(int));
////	 	  if(_success) {
////	 	  	LOG(ERROR) << "<<<< Succeeded dump to file" ;
////	 	  }
////	 	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
////	 		dump_file_->Close();
////	 		LOG(ERROR) <<  "ObjectSizesProfiler: done dumping data";
////	 		logPerfData();
//// }
////
////
////
//// if(!_success) {
////	 LOG(ERROR) <<  "ObjectSizesProfiler: XXXX Error dumping data";
//// }
//}
//void CohortProfiler::dumpHeapStats(void) {
//
//}
//
//bool CohortProfiler::dettachThread(GCMMPThreadProf* thProf) {
//	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
//		GCMMP_VLOG(INFO) << "CohortProfiler -- dettaching thread pid: " << thProf->GetTid();
//		thProf->state = GCMMP_TH_STOPPED;
//	}
//	return true;
//}
//
//
//
//MPPerfCounter* CohortProfiler::createHWCounter(Thread* thread) {
//	GCMMP_VLOG(INFO) << "CohortProfiler: empry creating hwCount";
//	return NULL;
//}
//
//
//bool CohortProfiler::periodicDaemonExec(void) {
//	Thread* self = Thread::Current();
//  if(waitForProfileSignal()) { //we recived Signal to Shutdown
//    GCMMP_VLOG(INFO) << "CohortProfiler: signal Received " << self->GetTid() ;
//    //LOG(ERROR) << "periodic daemon recieved signals tid: " <<  self->GetTid();
//
//    {
//    	MutexLock mu(self, *prof_thread_mutex_);
//    	receivedSignal_ = false;
//    }
// //
// //
//#if GCP_COLLECT_FOR_PROFILE
//    	gc::Heap* heap_ = Runtime::Current()->GetHeap();
//    	heap_->CollectGarbageForProfile(false);
//#endif
//    updateHeapAllocStatus();
//
//    if(getRecivedShutDown()) {
//    	LOG(ERROR) << "received shutdown tid: " <<  self->GetTid();
//
//    } else {
//    	dumpProfData(false);
//    }
//
//  	return getRecivedShutDown();
//  } else {
//  	return false;
//  }
//}

//inline void CohortProfiler::notifyFreeing(size_t objSize, size_t allocSize) {
//	GCP_DECLARE_REMOVE_ALLOC(objSize, allocSize);
//}

}// namespace mprofiler
}// namespace art

void dvmGCMMProfPerfCounters(const char* vmName){
	art::mprofiler::VMProfiler* mProfiler =
			art::Runtime::Current()->GetMProfiler();
	if(mProfiler != NULL) {
		mProfiler->GCMMProfPerfCounters(vmName);
	}

}
