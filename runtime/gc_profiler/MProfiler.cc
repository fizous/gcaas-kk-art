/*
 * MPprofiler.cc
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#include <string>
#include <pthread.h>
#include <fcntl.h>
#include <vector>
#include <utility>


#include "locks.h"
#include "base/mutex.h"
#include "base/unix_file/fd_file.h"
#include "cutils/sched_policy.h"
#include "cutils/process_name.h"
#include "cutils/system_clock.h"
#include "gc/heap.h"
#include "gc/space/dlmalloc_space.h"
#include "gc_profiler/MPPerfCounters.h"
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MProfiler.h"
#include "gc_profiler/MProfilerHeap.h"
#include "locks.h"
#include "os.h"
#include "class_linker.h"
#include "intern_table.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "thread_list.h"
#include "thread_state.h"
#include "thread.h"
#include "utils.h"
//#include "scoped_thread_state_change.h"


namespace art {
class MemmberOffset;

namespace mirror {
class Object;
}

namespace mprofiler {

const char * VMProfiler::benchmarks[] = {
		"com.aurorasoftworks.quadrant.ui.professional",
		"purdue.specjvm98",
		"purdue.dacapo",
		"com.antutu.ABenchMark",
		"com.android.cm3",
		"purdue.gcbench",
		"com.pandora.android",
		"com.android.browser",
		"com.rovio.angrybirds",
		"bbc.mobile.news.ww"
		//"com.android.systemui"  //we can add this to the profiled targets
		//"com.android.launcher" // the problem with this service is its lack of permissions to access Sdcard
};


const char * VMProfiler::gcMMPRootPath[] = {
		"/storage/sdcard1/Android/gcperf/", "/sdcard/gcperf/", "/data/anr/"
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
        0x13,
        GCMMP_FLAGS_NONE,
        "FRAG", "Measure Fragmentation mspace",
        "GPC_FRAG.log",
        NULL,
        &createVMProfiler<FragGCProfiler>
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
		{
				0x06,
				GCMMP_FLAGS_CREATE_DAEMON | GCMMP_FLAGS_ATTACH_GCDAEMON | GCMMP_FLAGS_MARK_MUTATIONS_WINDOWS,
				"RefDistanceProfiler", "Ref Distance Profiler",
				"GCP_REF_DISTANCES.log",
				NULL,
				&createVMProfiler<RefDistanceProfiler>
		},//RefDistance Profiler
};//VMProfiler::profilTypes

uint64_t GCPauseThreadManager::startCPUTime = 0;
uint64_t GCPauseThreadManager::startRealTime = 0;
int VMProfiler::kGCMMPLogAllocWindow = GCP_WINDOW_RANGE_LOG;
int VMProfiler::kGCMMPAllocWindow = (1 << GCP_WINDOW_RANGE_LOG);
int VMProfiler::kGCMMPLogAllocWindowDump = GCP_WINDOW_RANGE_LOG;
int VMProfiler::kGCMMPAllocWindowDump = (1 << GCP_WINDOW_RANGE_LOG);
uint64_t GCRefDistanceManager::kGCMMPMutationWindowSize = GCP_MUTATIONS_WINDOW_SIZE;
VMProfiler* GCMMPThreadProf::vmProfiler = NULL;


SafeGCPHistogramRec VMProfiler::allocatedBytesData_;
//GCPHistogramRecAtomic VMProfiler::allocatedBytesData;

bool VMProfiler::system_server_created_ = false;


const int VMProfiler::kGCMMPDumpEndMarker = -99999999;

inline uint64_t GCPauseThreadManager::GetRelevantRealTime(void)  {
	return NanoTime() - GCPauseThreadManager::startRealTime;
}

inline uint64_t GCPauseThreadManager::GetRelevantCPUTime(void)  {
	return ProcessTimeNS() - GCPauseThreadManager::startCPUTime;
}

inline void GCPauseThreadManager::MarkStartTimeEvent(GCMMP_BREAK_DOWN_ENUM evType) {
	//if(busy_ == 0) {
    UpdateCurrentEntry();
		curr_marker_->startMarker = GCPauseThreadManager::GetRelevantRealTime();
		curr_marker_->type = evType;
		busy_++;
		count_opens_++;

//		GCMMP_VLOG(INFO) << "openning: [" << curr_bucket_ind_ << ","
//		    << curr_entry_ << "] " << curr_marker_->type << ", paramType = "
//		    << evType << ", busy = " << busy_ << ", count_opens_ = "
//		    << count_opens_ << ", threadId = " << Thread::Current()->GetTid();

	//}
}

inline void GCPauseThreadManager::MarkEndTimeEvent(GCMMP_BREAK_DOWN_ENUM evType) {
	if(busy_ > 0) {


//    GCMMP_VLOG(INFO) << "closing: " << curr_marker_->type << ", paramType = " << evType
//        << ", busy = " << busy_ << ", count_opens_ = " << count_opens_ <<
//        ", threadId = " << Thread::Current()->GetTid();
    GCPauseThreadMarker* markerTemp = RetrieveLastOpened(evType);

		if(markerTemp == NULL) {
//		    GCMMP_VLOG(INFO) << "YYYY Did not work";
//	      GCMMP_VLOG(INFO) << "XXXXXXXXXXXXXXXXX ERROR TYPE IS NOT MATCHING XXXXX curr_marker_->type :"
//	          << curr_marker_->type << ", paramType = " << evType
//	          << ", busy = " << busy_ << ", count_opens_ = " << count_opens_ <<
//	          ", threadId = " << Thread::Current()->GetTid();
		    return;
		  }

	  markerTemp->finalMarker = GCPauseThreadManager::GetRelevantRealTime();
		IncrementIndices();
		count_opens_--;


	}


}

void GCPauseThreadManager::DumpProfData(void* args) {
	VMProfiler* mProfiler = reinterpret_cast<VMProfiler*>(args);

	art::File* file = mProfiler->GetDumpFile();
//	int totalC = 0;
	if(!HasData())
		return;
	//LOG(ERROR) << "parenthesis: " << count_opens_;
	uint64_t lastFinalTime = 0;
	for(int bucketInd = 0; bucketInd <= curr_bucket_ind_; bucketInd++) {
		int limit_ = (bucketInd == curr_bucket_ind_) ? curr_entry_ : kGCMMPMaxEventEntries - 1;
		if(limit_ >= 0) {
		  GCPauseThreadMarker*  _evt_marker = NULL;
			//file->WriteFully(pauseEvents[bucketInd], limit_ * sizeof(GCPauseThreadMarker));
			for(int entryInd = 0; entryInd <= limit_; entryInd++) {
			  _evt_marker = &pauseEvents[bucketInd][entryInd];
			  if(lastFinalTime > _evt_marker->finalMarker) {
			    continue;
			  }
			  if(_evt_marker->finalMarker == 0)
			    _evt_marker->finalMarker = GCPauseThreadManager::GetRelevantRealTime();
			  lastFinalTime = _evt_marker->finalMarker;
				file->WriteFully(_evt_marker, static_cast<int64_t>(sizeof(GCMMP_ProfileActivity)));
//				LOG(ERROR) << "pMgr " << totalC++ << ": " << _evt_marker->type
//				    << ", " << _evt_marker->startMarker << ", "
//				    << _evt_marker->finalMarker;
			}
		}
	}
	VMProfiler::GCPDumpEndMarker(file);
}



void GCMMPThreadProf::readPerfCounter(uint64_t val) {
	if(GetPerfRecord() == NULL)
		return;
	if(state == GCMMP_TH_RUNNING) {
		GetPerfRecord()->storeReading(val);
	}

}


void GCMMPThreadProf::readPerfCounter(uint64_t val, uint64_t* totalVals,
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

//	GCMMP_VLOG(INFO) << "VMProfiler: Initializing arrayBreaks for " << thread->GetTid();
	//	for(int _iter = GCMMP_GC_BRK_SUSPENSION; _iter < GCMMP_GC_BRK_MAXIMUM; _iter++) {
	//		memset((void*) &timeBrks[_iter], 0, sizeof(GCMMP_ProfileActivity));
	//	}
//	GCMMP_VLOG(INFO) << "VMProfiler: Done Initializing arrayBreaks for " << thread->GetTid();
	//	pauseManager = new GCPauseThreadManager();
	setThreadTag(GCMMP_THREAD_DEFAULT);
	perf_record_ = vmProfiler->createHWCounter(thread);
//	if(perf_record_ == NULL) {
//	  std::string thread_name;
//	  thread->GetThreadName(thread_name);
//	  LOG(ERROR) << "performance record was NULL for thread..." << thread_name;
//	}
	vmProfiler->setPauseManager(this);
	vmProfiler->setThHistogramManager(this, thread);


	lifeTime_.startMarker = GCMMPThreadProf::vmProfiler->GetRelevantRealTime();
	lifeTime_.finalMarker = 0;

	state = GCMMP_TH_RUNNING;
	GCMMP_VLOG(INFO) << "VMProfiler : ThreadProf is initialized";
}

void MMUProfiler::setPauseManager(GCMMPThreadProf* thProf){
//  GCMMP_VLOG(INFO) << "setPauseManager: Sizeof GCPauseThreadMarker is " << sizeof(GCPauseThreadMarker);
//	for(int _iter = GCMMP_GC_BRK_NONE; _iter < GCMMP_GC_BRK_MAXIMUM; _iter++) {
//		memset((void*) &thProf->timeBrks[_iter], 0, static_cast<int64_t>(sizeof(GCMMP_ProfileActivity)));
//	}
	GCPauseThreadManager::startCPUTime = start_cpu_time_ns_;
	GCPauseThreadManager::startRealTime = start_time_ns_;
	thProf->pauseManager = new GCPauseThreadManager();
}

#if 0
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
#endif

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

void GCMMPThreadProf::Destroy(VMProfiler* mProfiler) {

}




MPPerfCounter* MMUProfiler::createHWCounter(Thread* thread) {
	//GCMMP_VLOG(INFO) << "MMUProfiling: creating hwCount";
	return NULL;
}

MPPerfCounter* PerfCounterProfiler::createHWCounter(Thread* thread) {
	//GCMMP_VLOG(INFO) << "PerfCounterProfiler: creating hwCount: " << hwEvent_;
	MPPerfCounter* _perfCounter = MPPerfCounter::Create(hwEvent_);
//  std::string thread_name;
//  thread->GetThreadName(thread_name);
//  LOG(ERROR) << "performance record creating for thread..." << thread_name;

	//bool _result =
	    _perfCounter->OpenPerfLib(thread->GetTid());

//	if(!_result) {
//	  LOG(ERROR) << "Coud not open PerfLib: " << thread->GetTid();
//    std::string thread_name;
//    thread->GetThreadName(thread_name);
//    LOG(ERROR) << "performance record was NULL for thread..." << thread_name;
//	}


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



inline int VMProfiler::GCPGetCalculateStartBytes(void) {
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	int _diffLiveConc = heap_->GetConcStartBytes(true) - heap_->GetBytesAllocated();
	if(_diffLiveConc < 0)
	  return 0;
	return _diffLiveConc;
}

inline int VMProfiler::GCPGetCalculateMAXFootPrint(void) {
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	int _diffLiveConc = heap_->GetMaxAllowedFootPrint() - heap_->GetBytesAllocated();
  if(_diffLiveConc < 0)
    return 0;
	return _diffLiveConc;
}



inline void VMProfiler::updateHeapAllocStatus(void) {
	gc::Heap* heap_ = Runtime::Current()->GetHeap();


	uint64_t _allocBytes = 0;
	uint64_t _curr_alloc_bytes = 0;


	allocatedBytesData_.read_counts(Thread::Current(), &_allocBytes, &_curr_alloc_bytes);


	heapStatus.index = (_allocBytes  / kGCMMPAllocWindowDump);
	heapStatus.timeInNsec = GetRelevantRealTime();
	heapStatus.allocatedBytes = _allocBytes;
#if (ART_GC_SERVICE)
//	double ration = 1.0;
//	uint64_t _loaded_heap = static_cast<uint64_t>(heap_->GetBytesAllocated());
//	uint64_t _delta_heap_bytes = 0;
//	if(_curr_alloc_bytes < _loaded_heap) {
//	  _delta_heap_bytes = _loaded_heap - _curr_alloc_bytes;
//	  ration = 1 - ((_delta_heap_bytes * 1.0) / _curr_alloc_bytes);
//	}
//	heapStatus.currAllocBytes = _curr_alloc_bytes;//;(size_t) heap_->GetBytesAllocated();
//	//uint64_t _delta_conc_bytes = 0;
//	int _loaded_conc_heap = GCPGetCalculateStartBytes();
//	if(_loaded_conc_heap > 0) {
//	  heapStatus.concurrentStartBytes = heapStatus.currAllocBytes - (_loaded_conc_heap * ration);
//	} else {
//	  heapStatus.concurrentStartBytes = heapStatus.currAllocBytes;
//	}
//	heapStatus.currFootPrint = heapStatus.currAllocBytes + (GCPGetCalculateMAXFootPrint() * ration);


  heapStatus.currAllocBytes = _curr_alloc_bytes;//heap_->GetBytesAllocated();
  heapStatus.concurrentStartBytes =
      heapStatus.currAllocBytes + GCPGetCalculateStartBytes();//heap_->GetConcStartBytes();
  heapStatus.currFootPrint =
      //static_cast<uint32_t>(
          heapStatus.currAllocBytes + GCPGetCalculateMAXFootPrint();//);

//  LOG(ERROR) << "totalAlloc: "<< _allocBytes << ", currHeapBytes: " <<
//      heap_->GetBytesAllocated() << ", currBytes: " << _curr_alloc_bytes <<
//      ", concHeapBytes: " << heap_->GetConcStartBytes() << ", concBytes: " <<
//      heapStatus.concurrentStartBytes << ", footHeapPrint: " <<
//      GCPGetCalculateMAXFootPrint() << ", footPrint: " << heapStatus.currFootPrint;

#else
  heapStatus.currAllocBytes = _curr_alloc_bytes;//heap_->GetBytesAllocated();
  heapStatus.concurrentStartBytes =
      heapStatus.currAllocBytes + GCPGetCalculateStartBytes();//heap_->GetConcStartBytes();
  heapStatus.currFootPrint =
      //static_cast<uint32_t>(
          heapStatus.currAllocBytes + GCPGetCalculateMAXFootPrint();//);
#endif

  heapStatus.softLimit = 0;//heap_->GetMaxMemory();
  heapStatus.heapTargetUtilization = heap_->GetTargetHeapUtilization();
  heapIntegral_.gcpUpdateHeapStatus(&heapStatus);
	//uint32_t _allocBytes = static_cast<uint32_t>(allocatedBytesData.cntTotal.load());
//	LOG(ERROR) << "index: " << heapStatus.index
//	    << "; alloc_bytes:" <<  _allocBytes
//	    << "; currc_bytes:" <<  _curr_alloc_bytes
//	    << "; bytes allocated: " << heap_->GetBytesAllocated()
//	    << "; space bytes allocated: " << heap_->GetAllocSpace()->GetBytesAllocated();

}


void VMProfiler::notifyAllocation(size_t allocSpace, size_t objSize,
		mirror::Object* obj) {
	if(!verifyThreadNotification())
		return;

//  double _allocBytes = 0.0;
//  double _curr_alloc_bytes_ = 0.0;




	uint64_t initValue = 0.0;
	uint64_t updatedValue = 0.0;

//	(size_t)allocatedBytesData.cntTotal.load();

	accountAllocating(objSize, &initValue, &updatedValue);


	bool _newWindow =
	    (initValue >> kGCMMPLogAllocWindow) < (updatedValue >> kGCMMPLogAllocWindow);
	//	if(!IsAllocWindowsSet())
	//		return;


	//bool _newWindow = (initValue >> kGCMMPLogAllocWindow) != ((objSize + initValue)  >> kGCMMPLogAllocWindow);
	//double _newIndex =  1.0 * ((initValue + allocSpace) >> kGCMMPLogAllocWindow);
	if(_newWindow && IsAllocWindowsSet()) {

//		GCMMP_VLOG(INFO) << "VMProfiler: allocation Window: " <<
//		    updatedValue;

		{
			Thread* self = Thread::Current();
			MutexLock mu(self, *prof_thread_mutex_);
			GCMMP_HANDLE_FINE_PRECISE_ALLOC(allocSpace, objSize, obj);
			receivedSignal_ = true;

			if(hasProfDaemon()) {
				prof_thread_cond_->Broadcast(self);
			}
			// Wake anyone who may have been waiting for the GC to complete.
//			GCMMP_VLOG(INFO) << "VMProfiler: Sent the signal for allocation:" <<
//					self->GetTid() ;
		}
	} else {
		Thread* self = Thread::Current();
		MutexLock mu(self, *prof_thread_mutex_);
		GCMMP_HANDLE_FINE_PRECISE_ALLOC(allocSpace, objSize, obj);
	}
}

#if 0
void VMProfiler::notifyAllocation(size_t objSize, size_t allocSize) {
	allocatedBytesData.cntTotal.fetch_add(allocSize);
	if(!IsAllocWindowsSet())
		return;

	GCP_DECLARE_ADD_ALLOC(objSize, allocSize);

	int32_t initValue = allocatedBytesData.cntTotal.load();
	double _newIndex =  1.0 * ((initValue + allocSize) >> kGCMMPLogAllocWindow);



	if((_newIndex) != (getAllocIndex())) {

		GCMMP_VLOG(INFO) << "VMProfiler: allocation Window: " << allocatedBytesData.cntTotal.load();


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
#endif

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
  if(evt == GCMMP_GC_BRK_NONE) {
    uint64_t _cycles_data = 0;
    uint64_t _instr_data = 0;

    for (const auto& profRec : threadProfList_) {
      profRec->perf_record_->readPerfData();
      if(profRec->isEvent("CYCLES")) {
        _cycles_data += profRec->perf_record_->data;
      } else {
        _instr_data += profRec->perf_record_->data;
      }
    }

    GCMMPCPIDataDumped dataDumped;
    uint64_t _total_bytes = 0;
    uint64_t _curr_bytes = 0;
    Thread* self = Thread::Current();
    allocatedBytesData_.read_counts(self, &_total_bytes, &_curr_bytes);
    dataDumped.index = _total_bytes >> kGCMMPLogAllocWindowDump;


    dataDumped.currCycles = _cycles_data - accData.cycles;
    dataDumped.currInstructions = _instr_data - accData.instructions;



    accData.cycles = _cycles_data;
    accData.instructions = _instr_data;

//    dataDumped.currCycles = accData.currCycles;
//    dataDumped.currInstructions = accData.currInstructions;
    dataDumped.currCPI = 0;
    if(dataDumped.currInstructions != 0) {
      dataDumped.currCPI =
          (dataDumped.currCycles * 1.0) / dataDumped.currInstructions;
    }
    accData.currCycles = dataDumped.currCPI;
    accData.currInstructions = dataDumped.currInstructions;
    dataDumped.averageCPI =
        (accData.cycles * 1.0) / accData.instructions;
    dumpCPIStats(&dataDumped);
  }

  if(false) {
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
            //          profRec->perf_record_->addEndEventNOSpecial(evt);
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

              uint64_t _total_bytes = 0;
              uint64_t _curr_bytes = 0;

              allocatedBytesData_.read_counts(self, &_total_bytes, &_curr_bytes);

              dataDumped.index = _total_bytes >> kGCMMPLogAllocWindowDump;
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

}


bool MMUProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		//GCMMP_VLOG(INFO) << "MMUProfiler -- dettaching thread pid: " << thProf->GetTid();
		thProf->StopTimeProfiling();
	}
	return true;
}

bool PerfCounterProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		//GCMMP_VLOG(INFO) << "VMProfiler -- dettaching thread pid: " << thProf->GetTid();
		if(thProf->GetPerfRecord() != NULL) {
		  uint64_t _currBytes = allocatedBytesData_.get_total_count();
			thProf->readPerfCounter(_currBytes);
			thProf->GetPerfRecord()->ClosePerfLib();
			//thProf->resetPerfRecord();
		}
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}


bool GCDaemonCPIProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		//GCMMP_VLOG(INFO) << "VMProfiler -- dettaching thread pid: " << thProf->GetTid();
		if(thProf->GetPerfRecord() != NULL) {
			pid_t _id = thProf->GetTid();
			uint64_t _currBytes = allocatedBytesData_.get_total_count();
			for (const auto& profRec : threadProfList_) {
				if(profRec->GetTid() == _id) {
					profRec->readPerfCounter(_currBytes);
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



void VMProfiler::createAppList(GCMMP_Options* argOptions) {
	if(argOptions->app_list_path_ == NULL)
		return;

	std::string _file_lines;
	if (!ReadFileToString(argOptions->app_list_path_, &_file_lines)) {
		LOG(ERROR) << "(couldn't read " << argOptions->app_list_path_ << ")\n";
		return;
	}
	Split(_file_lines, '\n', app_list_);
	//  LOG(ERROR) << "---dump list of packages---";
	//  for (size_t i = 0; i < app_list_.size(); ++i) {
	//  	LOG(ERROR) << app_list_[i];
	//  }
	//  LOG(ERROR) << "<<<<<<dump list of packages>>>>>>>";
}


void VMProfiler::dvmGCMMProfPerfCountersVative(const char* vmName){
  art::mprofiler::VMProfiler* mProfiler =
      art::Runtime::Current()->GetVMProfiler();
  if(mProfiler != NULL) {
    mProfiler->GCMMProfPerfCounters(vmName);
  }

}

VMProfiler::VMProfiler(GCMMP_Options* argOptions, void* entry) :
						index_(argOptions->mprofile_type_),
						enabled_((argOptions->mprofile_type_ != VMProfiler::kGCMMPDisableMProfile) || (argOptions->gcp_type_ != VMProfiler::kGCMMPDisableMProfile)),
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
		if(argOptions->gcp_type_ != VMProfiler::kGCMMPDisableMProfile) {
			for(_loop = GCMMP_ARRAY_SIZE(VMProfiler::profilTypes) - 1;
					_loop >= 0; _loop--) {
				if(VMProfiler::profilTypes[_loop].id_ == argOptions->gcp_type_) {
					_found = true;
					break;
				}
			}
		} else {
			for(_loop = 0; _loop < (int) GCMMP_ARRAY_SIZE(VMProfiler::profilTypes);
					_loop++) {
				if(VMProfiler::profilTypes[_loop].id_ == index_) {
					_found = true;
					break;
				}
			}
		}
		if(_found) {
			createAppList(argOptions);
			GCHistogramDataManager::kGCMMPCohortLog = argOptions->cohort_log_;
			GCHistogramDataManager::GCPUpdateCohortSize();
			VMProfiler::kGCMMPLogAllocWindow = argOptions->alloc_window_log_;
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

	bool successWrite = dump_file_->WriteFully(heapConf, static_cast<int64_t>(sizeof(GC_MMPHeapConf)));
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

	//	GCPTotalAllocBytes = 0;


	allocatedBytesData_.reset(Thread::Current());



	start_heap_bytes_ = getRelevantAllocBytes();
	start_cpu_time_ns_ = ProcessTimeNS();
	start_time_ns_ = NanoTime();

	LOG(ERROR) <<  "Uptime is " << uptime_nanos() << "; start_time_ns_ is: " <<
			start_time_ns_;
	GC_MMPHeapConf heapConf;
	setHeapHeaderConf(&heapConf);
	dumpHeapConfigurations(&heapConf);

	if(isMarkHWEvents()){
		initMarkerManager(true);
	} else {
		markerManager = NULL;
//		LOG(ERROR) <<  "no need to initialize event manager ";
	}
	attachThreads();

	setIsProfilingRunning(true);

	LOG(ERROR) <<  "Done initCommonData";
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


static void GCMMPVMGetMainThread(Thread* t, void* arg) {
  Thread** threadParam = reinterpret_cast<Thread**>(arg);

  if(*threadParam == NULL) {
    *threadParam = t;
    return;
  }
  if((*threadParam)->GetTid() > t->GetTid()) {
      *threadParam = t;
  }
}

static bool isGCRelated(std::string& thread_name) {
  return (thread_name.compare("GCDaemon") == 0
      || thread_name.compare("IPC-MS-Daem") == 0
      || thread_name.find("Thread pool worker") == 0);
}

void GCDaemonCPIProfiler::attachSingleThread(Thread* thread) {
	GCMMP_VLOG(INFO) << "VMProfiler: Attaching thread: " << thread->GetTid();

  if(thread->IsStillStarting()) {
    std::string thread_name;
    thread->GetThreadName(thread_name);
    if(!isGCRelated(thread_name)) {
      GCMMP_VLOG(INFO) << "VMProfiler: going to delay thread --> " <<
          thread->GetTid() << " thread name is.. " << thread_name;
      delayedProfThread_.push_back(thread);
      return;
    }
  }


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
	if(isGCRelated(thread_name)) { //that's the GCDaemon
		setGcDaemon(thread);

		setThreadAffinity(thread, false);
		if(!IsAttachGCDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCDaemon threadProf for " <<
					thread->GetTid() << thread_name;
			return;
		}
		LOG(ERROR) << "vmprofiler: Attaching GCDaemon: " << thread->GetTid() << ", thread_name";
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

void VMProfiler::attachSingleThreadPostRenaming(Thread* thread) {
  Thread* self = Thread::Current();
  GCMMP_VLOG(INFO) << "-----VMProfiler: Attaching attachSingleThreadPostRenaming "
      << self->GetTid();
//  LOG(ERROR) << "-----VMProfiler: Attaching attachSingleThreadPostRenaming "
//      << self->GetTid();
 // ThreadList* thread_list = Runtime::Current()->GetThreadList();
  MutexLock mu(self, *Locks::thread_list_lock_);
  std::vector<Thread*>::iterator iter = delayedProfThread_.begin();
  while( iter != delayedProfThread_.end()) {
    if((*iter)->GetTid() == thread->GetTid()) {
      GCMMP_VLOG(INFO) << "VMProfiler: found delayed thread: " << thread->GetTid();
      delayedProfThread_.erase(iter);
      attachSingleThread(thread);
      break;
    }
    ++iter;
  }
}



void VMProfiler::attachSingleThread(Thread* thread) {
	GCMMP_VLOG(INFO) << "VMProfiler: Attaching thread: " << thread->GetTid();
	//LOG(ERROR) << "VMProfiler: attachSingleThread thread: " << thread->GetTid();
	if(thread->IsStillStarting()) {
    std::string thread_name;
    thread->GetThreadName(thread_name);
    if(!isGCRelated(thread_name)) {
//      LOG(ERROR) << "VMProfiler: delaying Attaching thread: --> " <<
//          thread->GetTid() << " thread name is.. " << thread_name;
      GCMMP_VLOG(INFO) << "VMProfiler: going to delay thread --> " <<
          thread->GetTid();
      delayedProfThread_.push_back(thread);
      return;
    }
	}


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
//	LOG(ERROR) << "vmprofiler: .......... Attaching tid: " << thread->GetTid()
//	    << ", thread_name: " << thread_name;
	GCMMPThProfileTag _tag = GCMMP_THREAD_DEFAULT;
	if(isGCRelated(thread_name)) { //that's the GCDaemon
		setGcDaemon(thread);

		setThreadAffinity(thread, false);
		if(!IsAttachGCDaemon()) {
			GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCDaemon threadProf for " <<
					thread->GetTid() << ", name: " <<thread_name;
			return;
		}
//		LOG(ERROR) << "vmprofiler: Attaching GCDaemon: " << thread->GetTid() <<
//		    ", thread_name: " << thread_name;
		_tag = GCMMP_THREAD_GCDAEMON;
	} else {
		if(thread_name.compare("HeapTrimmerDaemon") == 0) {
			setGcTrimmer(thread);
			setThreadAffinity(thread, false);
			if(!IsAttachGCDaemon()) {
				GCMMP_VLOG(INFO) << "VMProfiler: Skipping GCTrimmer threadProf for " <<
						thread->GetTid() << ", name: " << thread_name;
				return;
			}
//			LOG(ERROR) << "vmprofiler: Attaching TimerDaemon: " << thread->GetTid();
			_tag = GCMMP_THREAD_GCTRIM;
		} else if(thread_name.compare("main") == 0 || thread == main_thread_) { //that's the main thread
//		  LOG(ERROR) << " attachSingleThread:: ASSIGNING MAIN THREAD: " <<
//		      thread->GetTid() << ", name:" << thread_name;
			setMainThread(thread);
			_tag = GCMMP_THREAD_MAIN;
			setThreadAffinity(thread, true);
		}

	}
//  LOG(ERROR) << "VMProfiler: Initializing threadProf for " <<
//      thread->GetTid() << ", name: " << thread_name;
	GCMMP_VLOG(INFO) << "VMProfiler: Initializing threadProf for " <<
			thread->GetTid() << ", name: " << thread_name;
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
	//int32_t currBytes_ = GCPTotalAllocBytes.load();
  uint64_t _currBytes = allocatedBytesData_.get_total_count();
	uint64_t _totalVals = 0;
	uint64_t _gcMutVals = 0;
	uint64_t _gcDaemonVals = 0;
	//gc::Heap* heap_ = Runtime::Current()->GetHeap();

	//LOG(ERROR) << "Alloc: "<< currBytes_ << ", currBytes: " << heap_->GetBytesAllocated() << ", concBytes: " <<heap_->GetConcStartBytes() << ", footPrint: " << heap_->GetMaxAllowedFootPrint();
	for (const auto& threadProf : threadProfList_) {
		threadProf->readPerfCounter(_currBytes, &_totalVals, &_gcMutVals,
				&_gcDaemonVals);
	}
	updateHeapPerfStatus(_totalVals, _gcMutVals, _gcDaemonVals);
}




inline void VMProfiler::addEventMarker(GCMMP_ACTIVITY_ENUM evtMark) {
	Thread* self = Thread::Current();
	EventMarker* _address = NULL;
	{
		MutexLock mu(self, *evt_manager_lock_);
		if(markerManager->curr_index_ >= kGCMMPMaxEventsCounts) {
//		  LOG(ERROR) << "Thread::Current() = " << self->GetTid() <<
//		      "+++++Index of events exceeds the maximum allowed...markerManager->curr_index_: " << markerManager->curr_index_;
		  initEventBulk();
		}
		_address = &markerManager->markers_[markerManager->curr_index_];
		android_atomic_add(1, &(markerManager->curr_index_));
	  if(_address != NULL) {
	    _address->evType = evtMark;
	    _address->currHSize = allocatedBytesData_.get_total_count();
	    _address->currTime = GetRelevantRealTime();
	  }

	}

}



inline void VMProfiler::initEventBulk(void) {
//  LOG(ERROR) << "Init Event Bulk";

  size_t capacity =
      RoundUp(static_cast<size_t>(sizeof(EventMarker) * kGCMMPMaxEventsCounts), kPageSize);


  if(markerManager->markers_ != NULL) {
//    LOG(ERROR) << "<<<<Start With archiving>>>";

    EventMarkerArchive** headListP = &markerManager->events_archive_;

    while(*headListP != NULL) {
      headListP = &((*headListP)->next_event_bulk_);
    }


    *headListP =
        reinterpret_cast<EventMarkerArchive*>(calloc(1, static_cast<int64_t>(sizeof(EventMarkerArchive))));

    (*headListP)->next_event_bulk_ = NULL;
    (*headListP)->markers_ = markerManager->markers_;

    markerManager->archive_cnt_++;

//    LOG(ERROR) << "<<<<Done With archiving>>> " <<
//        reinterpret_cast<void*>(*headListP) << ", archiving: " <<
//        (reinterpret_cast<void*>((*headListP)->markers_));
  }
  std::string mapName("EventsTimeLine");

  MEM_MAP* mem_map = MEM_MAP::MapAnonymous(mapName.c_str(), NULL,
      capacity, PROT_READ | PROT_WRITE);

  if (mem_map == NULL) {
    LOG(FATAL) << "CPUFreqProfiler: Failed to allocate pages for alloc space (EventsTimeLine) of size "
        << PrettySize(capacity);
    return;
  } else {
//    LOG(ERROR) << "CPUFreqProfiler: succeeded to allocate pages for alloc space (EventsTimeLine) of size "
//        << PrettySize(capacity) << ", and address is : " <<
//        (reinterpret_cast<void*>(mem_map->Begin()));
  }

  map_archives_.Put(markerManager->archive_cnt_, mem_map);

  markerManager->markers_ = reinterpret_cast<EventMarker*>(mem_map->Begin());
  android_atomic_acquire_store(0, &markerManager->curr_index_);

//  LOG(ERROR) << "leaving VMProfiler::initEventBulk " <<
//      (reinterpret_cast<void*>(mem_map->Begin()));

}


void VMProfiler::initMarkerManager(bool firstCall) {
	LOG(ERROR) << "CPUFreqProfiler: Initializing the eventsManager";

	if(firstCall)
	  evt_manager_lock_ = new Mutex("Event manager lock");
	Thread* self = Thread::Current();
	{
		MutexLock mu(self, *evt_manager_lock_);
		if(!isMarkHWEvents()) {
			markerManager = NULL;
			LOG(ERROR) <<  "no need to initialize event manager ";
			return;
		}
		if(firstCall) {
      markerManager = (EventMarkerManager*) calloc(1, static_cast<int64_t>(sizeof(EventMarkerManager)));
      markerManager->events_archive_ = NULL;
      markerManager->markers_ = NULL;
      markerManager->curr_index_ = 0;
      markerManager->archive_cnt_ = 0;
		}

		initEventBulk();


//		size_t capacity =
//				RoundUp(sizeof(EventMarker) * kGCMMPMaxEventsCounts, kPageSize);
//
//		UniquePtr<MEM_MAP> mem_map(MEM_MAP::MapAnonymous("EventsTimeLine", NULL,
//				capacity, PROT_READ | PROT_WRITE));
//		markerManager->curr_index_ = 0;
//		if (mem_map.get() == NULL) {
//			LOG(ERROR) << "CPUFreqProfiler: Failed to allocate pages for alloc space (EventsTimeLine) of size "
//					<< PrettySize(capacity);
//			return;
//		} else {
//			LOG(ERROR) << "CPUFreqProfiler: succeeded to allocate pages for alloc space (EventsTimeLine) of size "
//					<< PrettySize(capacity);
//		}
//		markerManager->markers = (EventMarker*)(mem_map->Begin());

		//mem_map.release();
	}
}


void PerfCounterProfiler::dumpProfData(bool lastDump) {
	ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
	LOG(ERROR) <<  "PerfCounterProfiler: start dumping data";

	if(lastDump) {
		bool successWrite = GCPDumpEndMarker(dump_file_);
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
		bool _success = GCPDumpEndMarker(dump_file_);
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


bool VMProfiler::dumpEventArchive(EventMarkerArchive* event_archive) {

  size_t dataLength =
      sizeof(EventMarker) * kGCMMPMaxEventsCounts;
  bool successWrite = dump_file_->WriteFully(event_archive->markers_, dataLength);

  return successWrite;
}

void VMProfiler::dumpEventMarks(void) {
	Thread* self = Thread::Current();
	MutexLock mu(self, *evt_manager_lock_);
	bool successWrite = true;
	EventMarkerArchive* _event_archive_iter = markerManager->events_archive_;
	while(successWrite && _event_archive_iter != NULL) {
	  successWrite = (successWrite && dumpEventArchive(_event_archive_iter));
	  _event_archive_iter = _event_archive_iter->next_event_bulk_;
	}
	if(successWrite) {
	  size_t dataLength =  sizeof(EventMarker) * markerManager->curr_index_;
	  if(dataLength > 0) {
	    successWrite = dump_file_->WriteFully(markerManager->markers_, dataLength);
	    if(successWrite) {
	      GCPDumpEndMarker(dump_file_);
	    }
	  }
	}




	if(false) {
	  LOG(ERROR) << "<<<< total written: " << (sizeof(EventMarker) * markerManager->curr_index_) <<
			", Sizeof(EventMarker):"<< sizeof(EventMarker)
			<< ".. sizeof(uint64_t):" << sizeof(uint64_t) << ".. sizeof(int32_t):"
			<< sizeof(int32_t) << ".. sizeof (evtYpe:):" << sizeof(GCMMP_ACTIVITY_ENUM);

    for(int _indIter = 0; _indIter < markerManager->curr_index_; _indIter++) {
      EventMarker* marker = markerManager->markers_ + _indIter;
      if(marker == NULL)
        return;
      LOG(ERROR) << "---->>>>> data written:<<["<<_indIter<<"] evtYpe:" <<
          marker->evType << "; currHSize:" << marker->currHSize << "; time:"
          << marker->currTime;

    }
	}
}




void PerfCounterProfiler::logPerfData() {
	//uint64_t _currBytes = allocatedBytesData_.get_total_count();

  uint64_t _allocBytes = 0;
  uint64_t _curr_alloc_bytes = 0;


  allocatedBytesData_.read_counts(Thread::Current(), &_allocBytes,
                                  &_curr_alloc_bytes);



	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "totalAlloc: "<< _allocBytes << ", currHeapBytes: " <<
			heap_->GetBytesAllocated() << ", currBytes: " << _curr_alloc_bytes <<
			", concHeapBytes: " << heap_->GetConcStartBytes() << ", concBytes: " <<
			(_curr_alloc_bytes - GCPGetCalculateStartBytes()) << ", footPrint: " <<
			GCPGetCalculateMAXFootPrint();
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
	LOG(ERROR) << "currBytes: " << _allocBytes <<", sumData= "<< _sumData <<
			", sumGc=" << _sumGc <<", ration="<< ((_sumGc*100.0)/_sumData);
}

bool CPUFreqProfiler::periodicDaemonExec(void){
	return true;
}

bool GCDaemonCPIProfiler::periodicDaemonExec(void){
	return true;
}

inline void PerfCounterProfiler::dumpHeapStats(void) {
	bool successWrite = dump_file_->WriteFully(&heapStatus, static_cast<int64_t>(sizeof(GCMMPHeapStatus)));
	if(successWrite) {

	} else {
		LOG(ERROR) << "could not dump heap stats";
	}
}


inline void GCDaemonCPIProfiler::dumpCPIStats(GCMMPCPIDataDumped* dataD) {
	bool successWrite = dump_file_->WriteFully(dataD, static_cast<int64_t>(sizeof(GCMMPCPIDataDumped)));
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
//		GCMMP_VLOG(INFO) << "VMProfiler: signal Received " << self->GetTid() ;
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



bool MMUProfiler::periodicDaemonExec(void) {
	Thread* self = Thread::Current();
	// Check if GC is running holding gc_complete_lock_.
	MutexLock mu(self, *prof_thread_mutex_);
	ScopedThreadStateChange tsc(self, kWaitingInMainGCMMPCatcherLoop);
	{
		prof_thread_cond_->Wait(self);
	}
	if(receivedSignal_) { //we recived Signal to Shutdown
//		GCMMP_VLOG(INFO) << "VMProfiler: signal Received " << self->GetTid() ;
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

			LOG(ERROR) << "VMProfiler: Assigning profID to profDaemon " <<
					self->GetTid();
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
	Thread* _main_thread = NULL;
	thread_list->SuspendAll();
	{
    MutexLock mu(self, *Locks::thread_list_lock_);
    thread_list->ForEach(GCMMPVMAttachThread, this);
    if(main_thread_ == NULL) {
      thread_list->ForEach(GCMMPVMGetMainThread, &_main_thread);
      if(_main_thread != NULL) {
        LOG(ERROR) << " ASSIGNING MAIN THREAD: " << _main_thread->GetTid();
        setMainThread(_main_thread);
        GCMMPThreadProf* _prof_main = _main_thread->GetProfRec();
        if(_prof_main != NULL) {
          _prof_main->setThreadTag(GCMMP_THREAD_MAIN);
          LOG(ERROR) << " Setting ProfilerTag ..... ASSIGNING MAIN THREAD: " <<
              GCMMP_THREAD_MAIN<< ", id = " <<_main_thread->GetTid();
        }
      }
    }
	}
	thread_list->ResumeAll();
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
		memset(&accData, 0, static_cast<int64_t>(sizeof(GCMMPCPIData)));
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
	if(opts->gcp_type_ != VMProfiler::kGCMMPDisableMProfile) {
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





static void GCMMPKillThreadProf(GCMMPThreadProf* profRec, void* arg) {
	VMProfiler* vmProfiler = reinterpret_cast<VMProfiler*>(arg);
	if(vmProfiler != NULL) {
		if(vmProfiler->dettachThread(profRec)) {

		}
	}
}

//static void GCMMPResetThreadField(Thread* t, void* arg) {
//	t->SetProfRec(NULL);
//}



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
		f->WriteFully(&_pid, static_cast<int64_t>(sizeof(int)));
		f->WriteFully(&_type, static_cast<int64_t>(sizeof(int)));
		if(profRec->GetEndTime() == 0) {
			profRec->GetliveTimeInfo()->finalMarker =
					vmProfiler->end_time_ns_ - vmProfiler->start_time_ns_;
		}
		f->WriteFully(profRec->GetliveTimeInfo(), static_cast<int64_t>(sizeof(GCMMP_ProfileActivity)));

		GCMMP_VLOG(INFO) << "MProfiler_out: " << profRec->GetTid() << ">>>>>>>>>>>";

		mgr->DumpProfData(vmProfiler);
		//LOG(ERROR) << "id:" << _pid << ", type" << _type << ">>>>>>>>>>>>>>>";
		GCMMP_VLOG(INFO) << "MPr_out: " << profRec->GetTid();


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

	successWrite = dump_file_->WriteFully(&start_time_ns_, static_cast<int64_t>(sizeof(uint64_t)));


	if(successWrite) {
		successWrite = dump_file_->WriteFully(&end_time_ns_, static_cast<int64_t>(sizeof(uint64_t)));
	}
	LOG(ERROR) <<  "Uptime is " << uptime_nanos() << "; endTime is: " << end_time_ns_;
	if(successWrite) {
		ForEach(GCMMPDumpMMUThreadProf, this);
	}

	if(successWrite) {
		successWrite =
				GCPDumpEndMarker(dump_file_);
	}

	if(isLastDump) {
		GCPDumpEndMarker(dump_file_);
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




void VMProfiler::MProfileSignalCatcher(int signalVal) {
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetVMProfiler()->ProcessSignalCatcher(signalVal);
	}
}



//static void GCMMPAttachThread(Thread* t, void* arg) {
//	MProfiler* mProfiler = reinterpret_cast<MProfiler*>(arg);
//	if(mProfiler != NULL) {
//		mProfiler->AttachThread(t);
//	}
//}




void VMProfiler::ForEach(void (*callback)(GCMMPThreadProf*, void*),
		void* context) {
	for (const auto& profRec : threadProfList_) {
		callback(profRec, context);
	}
}



void VMProfiler::GCMMProfPerfCounters(const char* name) {
	if(IsProfilingEnabled()) {

		//		for (size_t i = 0; i < GCMMP_ARRAY_SIZE(benchmarks); i++) {
		//			if (strcmp(name, benchmarks[i]) == 0) {
		//				LOG(ERROR)<< "MVProfiler found a target VM " << name << " " <<
		//						i;
		//				LOG(ERROR) << "size of app list is " << app_list_.size();
		//				if(app_list_.size() > 0) {
		//					if (strcmp(name, app_list_[i].c_str()) != 0) {
		//						LOG(ERROR) << "we could not match" << app_list_.size();
		//					} else {
		//						LOG(ERROR) << "we could  match" << app_list_.size();
		//					}
		//				}
		//				GCMMPThreadProf::mProfiler = this;
		//				startProfiling();
		//				//InitializeProfiler();
		//				return;
		//			}
		//		}

		for (size_t i = 0; i < app_list_.size(); i++) {
			if (strcmp(name, app_list_[i].c_str()) == 0) {
				LOG(ERROR) << "========= MProfiler found a target VM: " << name << ", " <<
						app_list_.size();
				GCMMPThreadProf::vmProfiler = this;
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
	VMProfiler* mP = Runtime::Current()->GetVMProfiler();
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingRunning();
	return false;
}


/*
 * Return true only when the MProfiler is Running
 */
bool VMProfiler::MProfRefDistance(const mirror::Object* dst,
		uint32_t member_offset, const mirror::Object* new_value) {
	VMProfiler* mP = Runtime::Current()->GetVMProfiler();
	if(mP != NULL && mP->IsProfilingRunning()) {
		RefDistanceProfiler* refProfiler = (RefDistanceProfiler*) mP;
		refProfiler->gcpProfilerDistance(dst, member_offset, new_value);
		return true;
	}
	return false;
}
/*
 * Return true only when the MProfiler is Running for HW events
 */
inline bool VMProfiler::IsMProfHWRunning() {
	VMProfiler* mP = Runtime::Current()->GetVMProfiler();
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingHWEvent();
	return false;
}

/*
 * Attach a thread from the MProfiler
 */
void VMProfiler::MProfAttachThread(art::Thread* th) {
  th->SetProfRec(NULL);
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetVMProfiler()->attachSingleThread(th);
	}
}

/*
 * Attach a thread from the MProfiler after setting the thread name
 */
void VMProfiler::MProfAttachThreadPostRenaming(art::Thread* th) {
  if(VMProfiler::IsMProfRunning()) {
    Runtime::Current()->GetVMProfiler()->attachSingleThreadPostRenaming(th);
  }
}




//void VMProfiler::MProfNotifyFree(size_t objSize, size_t allocSize) {
//	if(VMProfiler::IsMProfRunning()) {
//		Runtime::Current()->GetMProfiler()->notifyFreeing(objSize, allocSize);
//	}
//}


void VMProfiler::MProfNotifyFree(size_t allocSpace, mirror::Object* obj, bool isZygote) {
	if(VMProfiler::IsMProfRunning() && !isZygote) {
		VMProfiler* _vmProfiler = Runtime::Current()->GetVMProfiler();
		_vmProfiler->notifyFreeing(allocSpace, obj);
	}
}


void VMProfiler::MProfObjClass(mirror::Class* klass, mirror::Object* obj) {
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetVMProfiler()->gcpProfObjKlass(klass, obj);
	}
}

void VMProfiler::MProfNotifyAlloc(size_t allocatedSpace,
		size_t objSize, mirror::Object* obj) {
	GCP_RESET_OBJ_PROFILER_HEADER(allocatedSpace,obj);
	if(VMProfiler::IsMProfRunning()) {
		Runtime::Current()->GetVMProfiler()->notifyAllocation(allocatedSpace,
				objSize, obj);
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

/*
 * Detach a thread from the MProfiler
 */
void VMProfiler::MProfDetachThread(art::Thread* th) {
	if(VMProfiler::IsMProfRunning()) {
		GCMMP_VLOG(INFO) << "VMProfiler: Detaching thid: " << th->GetTid();
		if(Runtime::Current()->GetVMProfiler()->dettachThread(th->GetProfRec())) {
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
		Runtime::Current()->GetVMProfiler()->addHWStartEvent(GCMMP_GC_BRK_NONE);
		Runtime::Current()->GetVMProfiler()->addEventMarker(GCMMP_GC_DAEMON);
	}
}

void VMProfiler::MProfMarkEndConcGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addHWEndEvent(GCMMP_GC_BRK_NONE);
	}
}


void VMProfiler::MProfMarkStartTrimHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addHWStartEvent(GCMMP_GC_BRK_NONE);
		Runtime::Current()->GetVMProfiler()->addEventMarker(GCMMP_GC_TRIM);
	}
}

void VMProfiler::MProfMarkEndTrimHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addHWEndEvent(GCMMP_GC_BRK_NONE);
	}
}


inline void GCMMPHeapIntegral::gcpPreCollectionMark(SafeGCPHistogramRec* allocationRec){
  uint64_t total_alloc_bytes = 0;
  uint64_t curr_alloc_bytes = 0;

  allocationRec->read_counts(Thread::Current(), &total_alloc_bytes, &curr_alloc_bytes);

  uint64_t deltaAllocBytes = total_alloc_bytes - lastTime_;


  //size_t _currBytes =  (size_t)allocationRec->cntLive.load();
  uint64_t _maxHeapP = curr_alloc_bytes;
  uint64_t _minHeapP = lastHeapSize_;

  if(lastHeapSize_ > curr_alloc_bytes) {
    _minHeapP = curr_alloc_bytes;
    _maxHeapP = lastHeapSize_;
  }

  double _extraSpace = 0.5 * (_maxHeapP - _minHeapP);

  _extraSpace += deltaAllocBytes * _minHeapP;
  accIntegral_ += _extraSpace;
  gcCounts_++;
}




inline void GCMMPHeapIntegral::gcpDumpMaxContigAlloc(uint64_t alloc_bytes) {
  Runtime::Current()->GetHeap()->GetMaxContigAlloc(this);
  LOG(ERROR) << "currBytes: " << alloc_bytes
      << ", max_contig: " << maximim_frag_length_;
}

inline void GCMMPHeapIntegral::gcpPostCollectionMark(SafeGCPHistogramRec* allocationRec) {
  allocationRec->read_counts(Thread::Current(), &lastTime_, &lastHeapSize_);
//  resetFragHistogram();
//  gcpDumpMaxContigAlloc(allocationRec->get_total_count());
//  dumpFragHistogram();
}

inline void GCMMPHeapIntegral::gcpUpdateHeapStatus(GCMMPHeapStatus* heapStatus) {
  heapStatus->heapIntegral = accIntegral_;
  heapStatus->gcCounts = gcCounts_;

}


inline void VMProfiler::accountFreeing(size_t objSize) {
  allocatedBytesData_.dec_counts(Thread::Current(), objSize);
//    GCPHistRecData::GCPDecAtomicRecData(objSize, &allocatedBytesData);
}

inline void VMProfiler::accountAllocating(size_t objSize) {
  allocatedBytesData_.inc_counts(Thread::Current(),objSize);
}

inline void VMProfiler::accountAllocating(size_t objSize, uint64_t* before_val,
                                          uint64_t* after_val) {
  allocatedBytesData_.inc_counts(Thread::Current(),objSize, before_val, after_val);
}



void VMProfiler::MProfMarkPreCollection(void) {
	if(VMProfiler::IsMProfRunning()) {
		VMProfiler* _vmProfiler = Runtime::Current()->GetVMProfiler();
		_vmProfiler->heapIntegral_.gcpPreCollectionMark(&_vmProfiler->allocatedBytesData_);
	}
}

void VMProfiler::gcpPostMarkCollection(void) {
  heapIntegral_.gcpPostCollectionMark(&allocatedBytesData_);
}

void FragGCProfiler::attachSingleThread(Thread* thread) {}


void FragGCProfiler::gcpLogPerfData() {

  uint64_t _currBytes = allocatedBytesData_.get_total_count();
  gc::Heap* heap_ = Runtime::Current()->GetHeap();
  LOG(ERROR) << "Alloc: "<< _currBytes << ", currBytes: " <<
      heap_->GetBytesAllocated() << ", concBytes: " <<
      heap_->GetConcStartBytes(true) << ", footPrint: " <<
      heap_->GetMaxAllowedFootPrint();

  GCHistogramObjSizesManager* _histManager = getFragHistograms();
  if(_histManager != NULL)
    _histManager->logManagedData();
}

bool FragGCProfiler::periodicDaemonExec(void){
  return true;
}

void FragGCProfiler::initHistDataManager(void) {
  LOG(ERROR) << "ObjectSizesProfiler::initHistDataManager";
  hitogramsData_ = new GCHistogramFragmentsManager();
}

FragGCProfiler::FragGCProfiler(GCMMP_Options* argOptions, void* entry):
            VMProfiler(argOptions, entry) {
  initHistDataManager();

}


void FragGCProfiler::resetFragHandlers(void) {
  if(hitogramsData_ == NULL)
    return;

  GCHistogramFragmentsManager* _manager =
      reinterpret_cast<GCHistogramFragmentsManager*>(hitogramsData_);
  _manager->gcpFinalizeProfileCycle();
}



void FragGCProfiler::dumpProfData(bool isLastDump) {
  bool _success = true;
  updateHeapAllocStatus();
  heapIntegral_.gcpPostCollectionMark(&allocatedBytesData_);
  Runtime::Current()->GetHeap()->GetMaxContigAlloc(this);
  //we do not need to aggregate since we have only one histogram
  hitogramsData_->calculatePercentiles();
  hitogramsData_->calculateAtomicPercentiles();
  _success = dump_file_->WriteFully(&heapStatus,
                                static_cast<int64_t>(sizeof(GCMMPHeapStatus)));
  _success &= hitogramsData_->gcpDumpManagedData(dump_file_ ,true);
  if(isLastDump) {
    _success &= GCPDumpEndMarker(dump_file_);
    _success &= hitogramsData_->gcpDumpSummaryManagedData(dump_file_);
    if(!_success) {
      LOG(ERROR) << "Error dumping data: ObjectSizesProfiler::dumpProfData";
    }
    dump_file_->Close();
    std::ostringstream outputStream;
    hitogramsData_->gcpDumpCSVData(outputStream);
    LOG(ERROR) << outputStream.str();
    gcpLogPerfData();
    LOG(ERROR) << "Done dumping data: ObjectSizesProfiler::dumpProfData";
  } else {
    resetFragHandlers();
  }

}


void FragGCProfiler::gcpPostMarkCollection(void) {
  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
  updateHeapAllocStatus();
  heapIntegral_.gcpPostCollectionMark(&allocatedBytesData_);

  Runtime::Current()->GetHeap()->GetMaxContigAlloc(this);

  //we do not need to aggregate since we have only one histogram
  hitogramsData_->calculatePercentiles();
  hitogramsData_->calculateAtomicPercentiles();

  dumpProfData(false);
}




void VMProfiler::MProfMarkPostCollection(void) {
	if(VMProfiler::IsMProfRunning()) {
		VMProfiler* _vmProfiler = Runtime::Current()->GetVMProfiler();
		_vmProfiler->gcpPostMarkCollection();
	}
}

void VMProfiler::MProfMarkStartAllocGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addEventMarker(GCMMP_GC_MALLOC);
		Runtime::Current()->GetVMProfiler()->addHWStartEvent(GCMMP_GC_BRK_GC_HAT);
	}
}
void VMProfiler::MProfMarkEndAllocGCHWEvent(void){
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addHWEndEvent(GCMMP_GC_BRK_GC_HAT);
	}
}

void VMProfiler::MProfMarkStartExplGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addEventMarker(GCMMP_GC_EXPLICIT);
		Runtime::Current()->GetVMProfiler()->addHWStartEvent(GCMMP_GC_BRK_GC_EXPL);
	}
}

void VMProfiler::MProfMarkEndExplGCHWEvent(void) {
	if(VMProfiler::IsMProfHWRunning()) {
		Runtime::Current()->GetVMProfiler()->addHWEndEvent(GCMMP_GC_BRK_GC_EXPL);
	}
}


/*
 * Detach a thread from the MProfiler
 */
void VMProfiler::MProfMarkWaitTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkWaitTimeEvent(thProf, GCMMP_GC_BRK_WAIT_CONC);
	}
}
/*
 * Detach a thread from the MProfiler
 */
void VMProfiler::MProfMarkEndWaitTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_WAIT_CONC);
	}
}

void VMProfiler::MProfMarkGCHatTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}

void VMProfiler::MProfMarkEndGCHatTimeEvent(art::Thread* th){
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_HAT);
	}
}


void VMProfiler::MProfMarkGCExplTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void VMProfiler::MProfMarkEndGCExplTimeEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_GC_EXPL);
	}
}

void VMProfiler::MProfMarkStartSafePointEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkWaitTimeEvent(thProf,
					GCMMP_GC_BRK_SAFEPOINT);
	}
}

void VMProfiler::MProfMarkEndSafePointEvent(art::Thread* th) {
	if(VMProfiler::IsMProfilingTimeEvent()) {
		GCMMPThreadProf* thProf = th->GetProfRec();
		if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING)
			Runtime::Current()->GetVMProfiler()->MarkEndWaitTimeEvent(thProf,
					GCMMP_GC_BRK_SAFEPOINT);
	}
}

void VMProfiler::MProfMarkSuspendTimeEvent(art::Thread* th, art::ThreadState thState){
	if(VMProfiler::IsMProfilingTimeEvent()) {
		if(thState == kSuspended) {
			GCMMPThreadProf* thProf = th->GetProfRec();
			if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
				Runtime::Current()->GetVMProfiler()->MarkWaitTimeEvent(thProf,
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
				Runtime::Current()->GetVMProfiler()->MarkEndWaitTimeEvent(thProf,
						GCMMP_GC_BRK_SUSPENSION);
			}
		}
	}
}


/*
 * Return true only when the MProfiler is Running
 */
inline bool VMProfiler::IsMProfilingTimeEvent() {
	VMProfiler* mP = Runtime::Current()->GetVMProfiler();
	if(mP != NULL && mP->IsProfilingEnabled())
		return mP->IsProfilingTimeEvent();
	return false;
}



/********************************* Object demographics profiling ****************/

void ObjectSizesProfiler::initializeProfilerData(bool initHistData){
	srand (time(NULL));
	GCHistogramDataManager::gcpTotalMutationsCount_.reset(Thread::Current());
//	GCHistogramDataManager::GCPTotalMutationsCount.store(0);
	if(initHistData)
		initHistDataManager();
	LOG(ERROR) << "ObjectSizesProfiler : initializeProfilerData";
}

ObjectSizesProfiler::ObjectSizesProfiler(GCMMP_Options* argOptions, void* entry,
		bool initHistogrms) :
			VMProfiler(argOptions, entry) {
	initializeProfilerData(initHistogrms);
	LOG(ERROR) << "ObjectSizesProfiler : ObjectSizesProfiler-B";
}

ObjectSizesProfiler::ObjectSizesProfiler(GCMMP_Options* argOptions, void* entry) :
			VMProfiler(argOptions, entry) {
	initializeProfilerData(true);
	LOG(ERROR) << "ObjectSizesProfiler : ObjectSizesProfiler-A";
}


//void ObjectSizesProfiler::setHistogramManager(GCMMPThreadProf* thProf) {
//	thProf->histogramManager_ = NULL;
//}

MPPerfCounter* ObjectSizesProfiler::createHWCounter(Thread* thread) {
	GCMMP_VLOG(INFO) << "ObjectSizesProfiler: creating hwCount";
	return NULL;
}


void ObjectSizesProfiler::initHistDataManager(void) {
	LOG(ERROR) << "ObjectSizesProfiler::initHistDataManager";
	hitogramsData_ = new GCHistogramObjSizesManager();
	//	GCHistogramDataManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
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
	hitogramsData_->addObject(allocatedMemory, objSize, obj);
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

	if(hitogramsData_ == NULL)
		return;
	accountFreeing(hitogramsData_->removeObject(allocatedMemory, obj));


	//	GCHistogramObjSizesManager::GCPRemoveObj(allocatedMemory, obj);
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
	bool successWrite = dump_file_->WriteFully(&heapStatus,
	                                           static_cast<int64_t>(sizeof(GCMMPHeapStatus)));
	uint64_t _totalMutations = 0;
	uint64_t _currMutations = 0;

	GCHistogramDataManager::gcpTotalMutationsCount_.read_counts(Thread::Current(),
	                                                             &_totalMutations, &_currMutations);
//	    GCHistogramDataManager::GCPTotalMutationsCount.load();
	successWrite &= dump_file_->WriteFully(&_totalMutations,
	                                       static_cast<int64_t>(sizeof(uint64_t)));
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




void ObjectSizesProfiler::gcpLogPerfData() {

  uint64_t _currBytes = allocatedBytesData_.get_total_count();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< _currBytes << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes(true) << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();

	GCHistogramObjSizesManager* _histManager = getObjHistograms();
	if(_histManager != NULL)
		_histManager->logManagedData();
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
		heap_->CollectGarbageForProfile(false);
		//LOG(ERROR) << "finished calling GCCollection";
#endif
		return getRecivedShutDown();
	} else {
		return false;
	}
}

bool ObjectSizesProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		GCMMP_VLOG(INFO) << "ObjectSizesProfiler -- dettaching thread pid: " <<
				thProf->GetTid();
		thProf->state = GCMMP_TH_STOPPED;
	}
	return true;
}



void ObjectSizesProfiler::gcpFinalizeHistUpdates(void) {
	GCHistogramObjSizesManager* _manager = getObjHistograms();
	if(_manager == NULL)
		return;
	_manager->gcpFinalizeProfileCycle();

	//GCHistogramDataManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	//we are relaxed we do not need to lookup for the whole records
	//getObjHistograms()->gcpCheckForResetHist();
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
	hitogramsData_->calculatePercentiles();
	hitogramsData_->calculateAtomicPercentiles();
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


void ObjectSizesProfiler::dumpProfData(bool isLastDump) {
	ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
	gcpUpdateGlobalHistogram();
	bool _success = true;
	if(isLastDump) {
		_success &= GCPDumpEndMarker(dump_file_);
		//dump the summary one more time
		_success &= hitogramsData_->gcpDumpSummaryManagedData(dump_file_);
		if(!_success) {
			LOG(ERROR) << "Error dumping data: ObjectSizesProfiler::dumpProfData";
		}
		dump_file_->Close();
		std::ostringstream outputStream;
		hitogramsData_->gcpDumpCSVData(outputStream);
		LOG(ERROR) << outputStream.str();
		gcpLogPerfData();
		LOG(ERROR) << "Done dumping data: ObjectSizesProfiler::dumpProfData";
	} else {
		dumpHeapStats();
		_success &= hitogramsData_->gcpDumpManagedData(dump_file_ ,true);
		//	gcpLogPerfData();
		gcpFinalizeHistUpdates();
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



/********************************* Thread Alloc Profiler ****************/


void ThreadAllocProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize){
	return;
}

inline void ThreadAllocProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	GCMMPThreadProf* thProf = Thread::Current()->GetProfRec();
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) {
		if(thProf->histogramManager_ != NULL)
			getThreadHistManager()->addObjectForThread(allocatedMemory, objSize,
					obj, thProf);
	}
}



void ThreadAllocProfiler::initHistDataManager(void) {
	hitogramsData_ = new GCPThreadAllocManager();
	LOG(ERROR) << "ThreadAllocProfiler : initHistDataManager";
	//GCPSetLastManagedCohort(GCPGetCalcCohortIndex());
}



void ThreadAllocProfiler::setHistogramManager(GCMMPThreadProf* thProf) {
	GCPThreadAllocManager* _manager = getThreadHistManager();
	if(_manager == NULL)
		return;
	_manager->setThreadManager(thProf);

}

void ThreadAllocProfiler::setThHistogramManager(GCMMPThreadProf* thProf,
		Thread* thread) {
	//LOG(ERROR) << "setThHistogramManager: " << thread->GetTid() << ", " << GetThreadName(thread->GetTid());
	setHistogramManager(thProf);
	//	if(thProf->histogramManager_ == NULL)
	//		return;
	//  std::string name;
	//  thread->GetThreadName(name);
	//	LOG(ERROR) << "setThHistogramManager: calling set reference name:" << thread->GetTid() << ", " << name;
	//	GCPPairHistogramRecords* _threadProfRec =
	//			(GCPPairHistogramRecords*) thProf->histogramManager_;
	//	_threadProfRec->setRefreneceNameFromThread(thread);
}
//bool ThreadAllocProfiler::periodicDaemonExec(void) {
//	Thread* self = Thread::Current();
//  if(waitForProfileSignal()) { //we recived Signal to Shutdown
//    GCMMP_VLOG(INFO) << "ThreadAllocProfiler: signal Received " << self->GetTid() ;
//    //LOG(ERROR) << "periodic daemon recieved signals tid: " <<  self->GetTid();
//    {
//    	MutexLock mu(self, *prof_thread_mutex_);
//    	receivedSignal_ = false;
//    }
// //
// //
//#if GCP_COLLECT_FOR_PROFILE
//    	gc::Heap* heap_ = Runtime::Current()->GetHeap();
//    	heap_->CollectGarbageForProfile(true);
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

//gcpAggregateHistograms(GCPHistogramRec* hisTable,
//		GCPHistogramRec* globalRec)


void ThreadAllocProfiler::gcpLogPerfData() {

  uint64_t _currBytes = allocatedBytesData_.get_total_count();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< _currBytes << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes(true) << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();

	GCPThreadAllocManager* _dataMgr = getThreadHistManager();

	if(_dataMgr != NULL) {
		_dataMgr->logManagedData();
	}

}


void ThreadAllocProfiler::gcpUpdateGlobalHistogram(void) {
	GCPThreadAllocManager* _manager = getThreadHistManager();
	if(_manager == NULL)
		return;
	_manager->calculatePercentiles();
	_manager->calculateAtomicPercentiles();

	//
	//
	//	//set a new secret to make sure all the manager are not ousiders:
	//	GCHistogramObjSizesManager* _histManager = getThreadHistograms();
	//	int _newSecret = _histManager->generateNewSecret();
	//	for (const auto& threadProf : threadProfList_) {
	//		GCHistogramObjSizesManager* _histMgr = threadProf->histogramManager_;
	//		if(_histMgr != NULL) {
	//			_histMgr->setFriendISecret(_newSecret);
	//			_histMgr->gcpAggregateHistograms(_histManager->histogramTable,
	//					&_histManager->histRecord);
	//			_histMgr->gcpAggAtomicHistograms(_histManager->lastWindowHistTable,
	//					&_histManager->histAtomicRecord);
	//		}
	//	}
	//
	//	int32_t _cntAtomicTotal = _histManager->histAtomicRecord.cntTotal.load();
	//	int32_t _cntAtomicLive = _histManager->histAtomicRecord.cntLive.load();
	//	for (const auto& threadProf : threadProfList_) {
	//		GCHistogramObjSizesManager* _histMgr = threadProf->histogramManager_;
	//		if(_histMgr != NULL) {
	//			_histMgr->histAtomicRecord.pcntLive = 0.0;
	//			_histMgr->histAtomicRecord.pcntTotal = 0.0;
	//
	//			if(!_histManager->gcpIsManagerFriend(_histMgr)) {
	//				LOG(ERROR) << "^^^^^^ Found  sneaky Histogram manager ^^^^^";
	//				_histMgr->histRecord.pcntLive = 0.0;
	//				_histMgr->histRecord.pcntTotal = 0.0;
	//			} else {
	//				_histMgr->histRecord.pcntLive =
	//						(_histMgr->histRecord.cntLive * 100.0) / _histManager->histRecord.cntLive;
	//				_histMgr->histRecord.pcntTotal =
	//						(_histMgr->histRecord.cntTotal * 100.0) / _histManager->histRecord.cntTotal;
	//
	//				_histMgr->histAtomicRecord.pcntLive = _cntAtomicLive == 0 ? 0.0 :
	//						(_histMgr->histAtomicRecord.cntLive.load() * 100.0) / _cntAtomicLive;
	//				_histMgr->histAtomicRecord.pcntTotal = _cntAtomicTotal == 0 ? 0.0 :
	//						(_histMgr->histAtomicRecord.cntTotal.load() * 100.0) / _cntAtomicTotal;
	//			}
	//		}
	//	}

	//	objHistograms->gcpCalculateEntries(objHistograms->histogramTable,
	//			&objHistograms->histRecord);
	//	objHistograms->gcpCalculateAtomicEntries(objHistograms->lastWindowHistTable,
	//			&objHistograms->histAtomicRecord);
}

void ThreadAllocProfiler::gcpFinalizeHistUpdates(void) {
	GCPThreadAllocManager* _manager = getThreadHistManager();
	if(_manager == NULL)
		return;
	_manager->gcpFinalizeProfileCycle();

	//GCHistogramDataManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	//we are relaxed we do not need to lookup for the whole records
	//getObjHistograms()->gcpCheckForResetHist();
}

//void ThreadAllocProfiler::gcpFinalizeHistUpdates(void) {
//	//GCHistogramDataManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
//	GCHistogramObjSizesManager* _histManager = getThreadHistograms();
//	_histManager->GCPSetLastManagedCohort(GCPCalcCohortIndex());
//	bool shouldUpdate = _histManager->gcpCheckForCompleteResetHist();
//	if(shouldUpdate) {
//		//int32_t _cohortIndex =  GCHistogramDataManager::kGCPLastCohortIndex.load();
//		for (const auto& threadProf : threadProfList_) {
//			GCHistogramObjSizesManager* _histMgr = threadProf->histogramManager_;
//			if(_histMgr != NULL) {
//				_histMgr->histAtomicRecord.cntLive.store(0);
//				_histMgr->histAtomicRecord.cntTotal.store(0);
//				_histMgr->histAtomicRecord.pcntLive = 0.0;
//				_histMgr->histAtomicRecord.pcntTotal = 0.0;
//				_histMgr->histAtomicRecord.index = threadProf->GetTid();
//				//_histMgr->setLastCohortIndex(_cohortIndex);
//			}
//		}
//	}
//	_histManager->gcpResetHistogramData();
//	_histManager->gcpResetAtomicData();
//}


//inline void ThreadAllocProfiler::dumpHeapStats(void) {
//	bool successWrite = dump_file_->WriteFully(&heapStatus, sizeof(GCMMPHeapStatus));
//	if(successWrite) {
//
//	} else {
//		LOG(ERROR) << "could not dump heap stats";
//	}
//}


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

bool ThreadAllocProfiler::dettachThread(GCMMPThreadProf* thProf) {
	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
		/*GCMMP_VLOG(INFO)*/ //LOG(ERROR) << "ThreadAllocProfiler -- dettaching thread pid: " << thProf->GetTid();
		thProf->state = GCMMP_TH_STOPPED;

		GCPThreadAllocManager* _manager = getThreadHistManager();
		if(_manager != NULL)
			return _manager->dettachThreadFromManager(thProf);
	}
	return true;
}

//bool ThreadAllocProfiler::dettachThread(GCMMPThreadProf* thProf) {
//	if(thProf != NULL && thProf->state == GCMMP_TH_RUNNING) { //still running
//		GCMMP_VLOG(INFO) << "ThreadAllocProfiler -- dettaching thread pid: " << thProf->GetTid();
//		thProf->state = GCMMP_TH_STOPPED;
//	}
//	return true;
//}

//bool ThreadAllocProfiler::dumpGlobalThreadsStats(void) {
//	GCHistogramObjSizesManager* _histMgr = NULL;
//	bool _success = true;
//	int _count = 0;
//	for (const auto& threadProf : threadProfList_) {
//		_histMgr = threadProf->histogramManager_;
//		if(_histMgr == NULL)
//			continue;
//		_success &= _histMgr->gcpDumpHistRec(dump_file_);
//		if(!_success)
//			return false;
//		_count++;
//	}
//
//	if(_count > 0) {
//		_success &= GCPDumpEndMarker(dump_file_);
//	}
//	return _success;
//}
//
//
//bool ThreadAllocProfiler::dumpGlobalThreadsAtomicStats(void) {
//	GCHistogramObjSizesManager* _histMgr = NULL;
//	bool _success = true;
//	int _count = 0;
//	for (const auto& threadProf : threadProfList_) {
//		_histMgr = threadProf->histogramManager_;
//		if(_histMgr == NULL)
//			continue;
//		_success &= _histMgr->gcpDumpHistAtomicTable(dump_file_);
//		if(!_success)
//			return false;
//		_count++;
//	}
//	if(_count > 0) {
//		_success &= GCPDumpEndMarker(dump_file_);
//	}
//	return _success;
//}


//void ThreadAllocProfiler::dumpProfData(bool isLastDump) {
//  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
//	//dump the heap stats
//	dumpHeapStats();
//	//dump the global entry
//	gcpUpdateGlobalHistogram();
//		//dump the global stats
//
////	GCPHistogramRec dummyRec;
////	dummyRec.index = 1;
////	dummyRec.cntLive = 1;
////	dummyRec.cntTotal = 1;
////	dummyRec.pcntLive = 1;
////	dummyRec.pcntTotal = 1;
//	bool _success =
//	  	dump_file_->WriteFully(&getThreadHistograms()->histRecord,
//	  			sizeof(GCPHistogramRec));
//	if(_success) {
//		_success &= dumpGlobalThreadsStats();
//		_success &= dumpGlobalThreadsAtomicStats();
//	}
//
//  if(isLastDump && _success) {
//	  _success &=
//	  		GCPDumpEndMarker(dump_file_);
//	  //dump the summary at the end one more time
//	  _success &= dumpGlobalThreadsStats();
//	 	if(_success) {
//	 		LOG(ERROR) << "<<<< Succeeded dump to file" ;
//	 	}
//	 	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
//	 	dump_file_->Close();
//	 	LOG(ERROR) <<  "ObjectSizesProfiler: done dumping data";
//	 	logPerfData();
//  } else {
//		 gcpFinalizeHistUpdates();
////		 gcpFinalizeHistUpdates();
//		// gcpResetLastLive(&lastLiveRecord, lastLiveTable);
// }
// if(!_success) {
//	 LOG(ERROR) <<  "ObjectSizesProfiler: XXXX Error dumping data";
// }
//}


/******************* ref distance profiler ******************/


void RefDistanceProfiler::gcpProfilerDistance(const mirror::Object* dst,
		uint32_t member_offset, const mirror::Object* new_value) {
	GCRefDistanceManager* _manager = getDistanceProfManager();
	if(_manager == NULL)
		return;
	uint64_t currMutationCnt = 0;
	uint64_t totalMutationCnt = 0;
	Thread* self = Thread::Current();
			GCHistogramDataManager::gcpTotalMutationsCount_.read_counts(self,&totalMutationCnt,
			                                                            &currMutationCnt);
	GCHistogramDataManager::GCPIncMutations(self);
	_manager->profileDistance(dst, member_offset, new_value);
	if(IsMutationsWindowsSet() && (totalMutationCnt > 0) &&
	    (totalMutationCnt % GCRefDistanceManager::kGCMMPMutationWindowSize == 0)) {

		{
			MutexLock mu(self, *prof_thread_mutex_);
			receivedSignal_ = true;
			if(hasProfDaemon()) {
				prof_thread_cond_->Broadcast(self);
			}
			// Wake anyone who may have been waiting for the GC to complete.
			GCMMP_VLOG(INFO) << "VMProfiler: Sent the signal for gcpProfilerDistance:" <<
					self->GetTid() ;
		}
	}
}

void RefDistanceProfiler::initHistDataManager(void) {
	hitogramsData_ = new GCRefDistanceManager(&allocatedBytesData_);
	LOG(ERROR) << "RefDistanceProfiler::initHistDataManager";
}


void RefDistanceProfiler::gcpFinalizeHistUpdates(void) {
	GCRefDistanceManager* _manager = getDistanceProfManager();
	if(_manager == NULL)
		return;
	_manager->gcpFinalizeProfileCycle();

	//GCHistogramDataManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	//we are relaxed we do not need to lookup for the whole records
	//getObjHistograms()->gcpCheckForResetHist();
}

/********************************* Cohort profiling ****************/

void CohortProfiler::initHistDataManager(void) {
	hitogramsData_ = new GCCohortManager(&allocatedBytesData_);
}

void CohortProfiler::setHistogramManager(GCMMPThreadProf* thProf) {
	thProf->histogramManager_ = NULL;
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
	getCohortManager()->addObject(allocatedMemory, objSize, obj);
}

//void CohortProfiler::dumpProfData(bool isLastDump) {
//  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
//	//dump the heap stats
//	dumpHeapStats();
//	//dump the global entry
//
//	hitogramsData_->gcpDumpManagedData(dump_file_, false);
//
//	//gcpUpdateGlobalHistogram();
//		//dump the global stats
//
//  if(isLastDump) {
//	  bool _success = false;
//  	_success &= GCPDumpEndMarker(dump_file_);
//	 	if(_success) {
//	 		LOG(ERROR) << "<<<< Succeeded dump to file" ;
//	 	}
//	 	  	//successWrite = dump_file_->WriteFully(&start_time_ns_, sizeof(uint64_t));
//	 	dump_file_->Close();
//	 	LOG(ERROR) <<  "CohortProfiler: done dumping data";
//	 	gcpLogPerfData();
//  }
//}


inline void CohortProfiler::gcpRemoveObject(size_t allocatedMemory,
		mirror::Object* obj) {
  Thread* self = Thread::Current();
  MutexLock mu(self, *prof_thread_mutex_);
  uint64_t _size = getCohortManager()->removeObject(allocatedMemory, obj);
  if (_size > 0)
    accountFreeing(static_cast<size_t>(_size));

	//GCHistogramManager::GCPRemoveObj(allocatedMemory, obj);
}




void CohortProfiler::gcpLogPerfData() {
#if 0
	int32_t _currBytes = allocatedBytesData.cntTotal.load();
	gc::Heap* heap_ = Runtime::Current()->GetHeap();
	LOG(ERROR) << "Alloc: "<< _currBytes << ", currBytes: " <<
			heap_->GetBytesAllocated() << ", concBytes: " <<
			heap_->GetConcStartBytes(true) << ", footPrint: " <<
			heap_->GetMaxAllowedFootPrint();
#endif
	GCCohortManager* _coManager = getCohortManager();
	if(_coManager == NULL)
		return;
	_coManager->logManagedData();
}
/************************ Class Loader *********************/

ClassProfiler::ClassProfiler(GCMMP_Options* opts, void* entry) :
			ObjectSizesProfiler(opts, entry, false) {
	initHistDataManager();
	LOG(ERROR) << "ClassProfiler : Constructor of ClassProfiler";
}


//void ClassProfiler::gcpRemoveObject(size_t allocSpace, mirror::Object* obj) {
//	GCPExtraObjHeader* _profHeader =
//				GCHistogramObjSizesManager::GCPGetObjProfHeader(allocSpace, obj);
//	if(_profHeader->objSize == 0) {
//		//the object was not registered
//		LOG(ERROR) << "---------Found none registered object";
//		return;
//	}
//
//
//	GCPHistRecData* _dataRec = _profHeader->dataRec;
//	if(_dataRec == NULL)
//		return;
//
//	GCClassTableManager* _mngr = getClassHistograms();
//	if(_mngr != NULL) {
//		_mngr->
//		_mngr->
////		_dataRec->gcpDecRecData();
////
////		_mngr->histData_->gcpDecRecData(_profHeader->objSize);
////		if(_dataRec->gcpDecAtomicRecData()) {
////			//update the global counter as well
////			_mngr->histData_->gcpDecAtomicRecData();
////		}
//	}
//}

inline void ClassProfiler::gcpAddObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	//LOG(ERROR) << " Adding object in classProfiler";
	getClassHistograms()->addObject(allocatedMemory, objSize, obj);
}

void ClassProfiler::gcpProfObjKlass(mirror::Class* klass, mirror::Object* obj) {
	GCClassTableManager* classManager = getClassHistograms();
	if(classManager != NULL) {
		GCPHistRecData* _rec = classManager->addObjectClassPair(klass, obj);
		if(_rec == NULL)
			LOG(ERROR) << "ClassProfiler::gcpProfObjKlass NULL record";
		//		if(_rec == NULL) {
		//			LOG(ERROR) << "Could not add the new record";
		//		} else {
		//			LOG(ERROR) << "new record was added";
		//		}
		//		size_t objSpace =  Runtime::Current()->GetHeap()->GCPGetObjectAllocatedSpace(obj);
		//		if(objSpace == 0) {
		//			LOG(ERROR) << "Objectsize rturned 0: ";
		//		}
		//		GCPExtraObjHeader* _profHeader =
		//					GCHistogramObjSizesManager::GCPGetObjProfHeader(objSpace, obj);
		//		classManager->gcpIncAtomicPairRecData(_profHeader->objSize, _rec);
		//		classManager->gcpIncPairRecData(_profHeader->objSize, _rec);
		//
		//		_profHeader->dataRec = _rec;

	}
}

void ClassProfiler::gcpFinalizeHistUpdates(void) {
	GCClassTableManager* _manager = getClassHistograms();
	if(_manager == NULL)
		return;
	_manager->gcpFinalizeProfileCycle();
}

void ClassProfiler::initHistDataManager(void) {
	LOG(ERROR) << "Initializing ClassProfiler::initHistDataManager";
	hitogramsData_ = new GCClassTableManager();
}

//class_Linker
void ClassProfiler::dumpAllClasses(void) {
	//std::ostringstream os;
	// os << "Dumping the Classes::::\n";
	ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
	if(false)
		Runtime::Current()->GetClassLinker()->GCPDumpAllClasses(7, LOG(ERROR));

	LOG(ERROR) << "++++++++++++++++ Counting for each class++++++++++++++++++++";
	GCClassTableManager* tablManager = getClassHistograms();
	if(tablManager == NULL) {
		LOG(ERROR) << "+++table manager is NULL";
		return;
	} else {
		LOG(ERROR) << "+++table manager is not NULL";
		tablManager->logManagedData();
	}
	//Runtime::Current()->GetInternTable()->


	//	if(tablManager->classTable_ != NULL) {
	//		LOG(ERROR) << "+++table class table is not NULL";
	//		LOG(ERROR) << "+++table class table size is" << tablManager->classTable_.size();
	//	} else {
	//		LOG(ERROR) << "+++sssstable class table is null";
	//	}
	//
	//	int _countMine = 0;
	//	for (const std::pair<size_t, GCPHistogramRec*>& it : tablManager->classTable_) {
	//		LOG(ERROR) << "-- " << _countMine++;
	//		if(it.second == NULL) {
	//			LOG(ERROR) << "--------NULL";
	//		}
	//		//GCPHistogramRec* _recI =  it.second;
	//		//LOG(ERROR) << "-- " <<_countMine++<<"  :: "<< it.first << ", count=" << _recI->cntLive;
	//	}

	LOG(ERROR) << "++++++++++++++++ Done Counting for each class++++++++++++++++++++";// << _countMine;

	//  std::vector<mirror::Class*> classes;
	//
	//  Runtime::Current()->GetClassLinker()->GCPGetAllClasses(classes);
	//  std::vector<uint64_t> countVector;
	//  for(size_t _indexIter = 0; _indexIter < classes.size(); _indexIter++) {
	//  	countVector.push_back(0);
	//  }
	//
	//  Runtime::Current()->GetHeap()->CountInstances(classes, false, &countVector[0]);
	//  int _index = 0;
	//  for (const auto& _rowCountIter : countVector) {
	//  	LOG(ERROR) << "count: " << _index++ << ": " << _rowCountIter;
	//  }
}

void ClassProfiler::gcpLogPerfData() {

	GCClassTableManager* tablManager = getClassHistograms();
	if(tablManager == NULL) {
		LOG(ERROR) << "+++table manager is NULL";
		return;
	} else {
		LOG(ERROR) << "+++table manager is not NULL";
		tablManager->logManagedData();
	}

	if(false)
		dumpAllClasses();
}




//void ClassProfiler::dumpProfData(bool isLastDump) {
//  ScopedThreadStateChange tsc(Thread::Current(), kWaitingForGCMMPCatcherOutput);
//	GCClassTableManager* tablManager = getClassHistograms();
//	if(tablManager != NULL) {
//		dumpHeapStats();
////		tablManager->calculatePercentiles();
////		tablManager->calculateAtomicPercentiles();
//		tablManager->gcpDumpManagedData(dump_file_, true);
//		// dump class data from last allocation window
//		if(isLastDump) {
//			GCPDumpEndMarker(dump_file_);
//			//dump data summary
//			tablManager->gcpDumpSummaryManagedData(dump_file_);
//			dump_file_->Close();
//			gcpLogPerfData();
//			LOG(ERROR) << "ClassProfiler: Terminating dumpProfData with lastDump is true";
//		}
//		gcpFinalizeHistUpdates();
//
//	}
//}

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

#if 0

void MProfiler::DumpCurrentOutpu(void) {
	//ScopedThreadStateChange tsc(Thread::Current(), kWaitingForSignalCatcherOutput);
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
				VMProfiler::GCPDumpEndMarker(dump_file_);
	}

	if(isLastDump) {
		VMProfiler::GCPDumpEndMarker(dump_file_);
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
// Member functions definitions including constructor
MProfiler::MProfiler(GCMMP_Options* argOptions)
: index_(argOptions->mprofile_type_),
	main_thread_(NULL),
	gc_daemon_(NULL),
	prof_thread_(NULL),
	gcDaemonAffinity_(argOptions->mprofile_gc_affinity_),
	enabled_((argOptions->mprofile_type_ != VMProfiler::kGCMMPDisableMProfile)),
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

#endif // removing MProfiler class
}// namespace mprofiler
}// namespace art

void dvmGCMMProfPerfCounters(const char* vmName){
//	art::mprofiler::VMProfiler* mProfiler =
//			art::Runtime::Current()->GetVMProfiler();
//	if(mProfiler != NULL) {
//		mProfiler->GCMMProfPerfCounters(vmName);
//	}

}
