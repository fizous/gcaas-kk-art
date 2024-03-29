/*
 * MProfiler.h
 *
 *  Created on: Aug 27, 2014
 *      Author: hussein
 */

#ifndef MPROFILER_H_
#define MPROFILER_H_

#include <string>
#include <list>
#include "locks.h"
#include "base/mutex.h"
#include "safe_map.h"
#include "mem_map.h"
#include "base/unix_file/fd_file.h"
#include "os.h"
#include "base/logging.h"
#include "thread_state.h"
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MProfilerHeap.h"
#include "gc/accounting/gc_allocator.h"
#include "cutils/system_clock.h"
#include "utils.h"
#include "offsets.h"

/**********************************************************************
 * 											Macros Definitions
 **********************************************************************/

#if ART_GC_PROFILER_VERBOSE
  #define GC_V_ENABLED  true
#else
  #define GC_V_ENABLED  false
#endif

/* log information. used to monitor the flow of the profiler.*/
#define GCMMP_VLOG(severity) if (GC_V_ENABLED) ::art::LogMessage(__FILE__, __LINE__, severity, -1).stream()


#define GCMMP_ARRAY_SIZE(array) (sizeof((array))/sizeof((array[0])))




#define GCP_CALC_HIST_INDEX(_coh_index, value)  \
    uint32_t _leadZeros = 0;                    \
    uint32_t _highBits = High32Bits(value);     \
    _leadZeros += CLZ(_highBits);               \
    if(_highBits == 0) {                        \
      uint32_t _lowBits = Low32Bits(value);     \
      _leadZeros += CLZ(_lowBits);              \
    }                                           \
    _coh_index = (64 - _leadZeros);





#if (ART_USE_GC_PROFILER || ART_USE_GC_PROFILER_REF_DIST)
  #define GCP_ART_MARK_WAIT_EVENTS          1 /* turn on Pause time Events ConcurrentGC */
  #define GCP_DISABLE_CONC_COLLECT					1 /* turn off ConcurrentGC */
  #define GCP_DISABLE_EXPL_COLLECT					1 /* turn off explicit GC */
  #define GCP_COLLECT_FOR_PROFILE					  1 /* collect on each allocation window */
  #define GCMMP_HANDLE_FINE_PRECISE_ALLOC(x,y,z)                \
	  (gcpAddObject(x,y,z))
  #define GCMMP_HANDLE_FINE_PRECISE_FREE(allocSpace, objSize, isZygote)   \
	  art::mprofiler::VMProfiler::MProfNotifyFree(allocSpace, objSize, isZygote)
  #define GCP_ADD_EXTRA_BYTES(actualSize, extendedSize)	        \
	  (extendedSize = art::mprofiler::ObjectSizesProfiler::GCPAddMProfilingExtraBytes(actualSize))
  #define GCP_REMOVE_EXTRA_BYTES(actualSize, modifiedSize)      \
	  (modifiedSize = art::mprofiler::ObjectSizesProfiler::GCPRemoveMProfilingExtraBytes(actualSize))
  #define GCP_RESET_OBJ_PROFILER_HEADER(x,y)                    \
	  (ObjectSizesProfiler::GCPInitObjectProfileHeader(x,y))
  #define GCMMP_NOTIFY_ALLOCATION(allocatedSpace, objSize, obj) \
	  art::mprofiler::VMProfiler::MProfNotifyAlloc(allocatedSpace, objSize, obj)
  #define GCP_PROFILE_OBJ_CLASS(klass, obj)                     \
	  art::mprofiler::VMProfiler::MProfObjClass(klass, obj)
#else // NOT (ART_USE_GC_PROFILER || ART_USE_GC_PROFILER_REF_DIST)
  #define GCP_DISABLE_CONC_COLLECT					0 /* turn off ConcurrentGC */
  #define GCP_DISABLE_EXPL_COLLECT					0 /* turn off explicit GC */
  #define GCP_COLLECT_FOR_PROFILE					  0 /* collect on each allocation window */
  #define GCMMP_HANDLE_FINE_PRECISE_ALLOC(x,y,z)           ((void) 0)
  #define GCP_RESET_OBJ_PROFILER_HEADER(x,y)							 ((void) 0)
  #define GCP_PROFILE_OBJ_CLASS(klass, obj)                ((void) 0)
  #if (ART_USE_GC_DEFAULT_PROFILER)
    #define GCP_ART_MARK_WAIT_EVENTS          1 /* turn on Pause time Events ConcurrentGC */
    #define GCMMP_HANDLE_FINE_PRECISE_FREE(allocSpace, objSize, isZygote)   \
		  art::mprofiler::VMProfiler::MProfNotifyFree(allocSpace, objSize, isZygote)
    #define GCMMP_NOTIFY_ALLOCATION(allocatedSpace, objSize, obj) \
	    art::mprofiler::VMProfiler::MProfNotifyAlloc(allocatedSpace, objSize, obj)
   #define GCP_REMOVE_EXTRA_BYTES(actualSize, modifiedSize) \
      modifiedSize = actualSize
  #define GCP_ADD_EXTRA_BYTES(actualSize, extendedSize)           \
    (extendedSize = actualSize)
  #else // NOT (ART_USE_GC_DEFAULT_PROFILER)
    #define GCP_ART_MARK_WAIT_EVENTS          0 /* turn off Pause time Events ConcurrentGC */
    #define GCP_ADD_EXTRA_BYTES(actualSize, extendedSize) ((void) 0)
    #define GCP_REMOVE_EXTRA_BYTES(actualSize, modifiedSize) ((void) 0)
    #define GCMMP_HANDLE_FINE_PRECISE_FREE(allocSpace, objSize, isZygote)   \
		  ((void) 0)
    #define GCMMP_NOTIFY_ALLOCATION(allocatedSpace, objSize, obj) \
		  ((void) 0)
  #endif//ART_USE_GC_DEFAULT_PROFILER
#endif//ART_USE_GC_PROFILER





#if (GCP_ART_MARK_WAIT_EVENTS)
 #define GCP_MARK_START_CONC_GC_HW_EVENT mprofiler::VMProfiler::MProfMarkStartConcGCHWEvent()
 #define GCP_MARK_END_CONC_GC_HW_EVENT mprofiler::VMProfiler::MProfMarkEndConcGCHWEvent()
 #define GCP_MARK_START_ALLOC_GC_HW_EVENT mprofiler::VMProfiler::MProfMarkStartAllocGCHWEvent()
 #define GCP_MARK_END_ALLOC_GC_HW_EVENT mprofiler::VMProfiler::MProfMarkEndAllocGCHWEvent()
 #define GCP_MARK_START_EXPL_GC_HW_EVENT mprofiler::VMProfiler::MProfMarkStartExplGCHWEvent()
 #define GCP_MARK_END_EXPL_GC_HW_EVENT mprofiler::VMProfiler::MProfMarkEndExplGCHWEvent()
 #define GCP_MARK_START_TRIM_HW_EVENT mprofiler::VMProfiler::MProfMarkStartTrimHWEvent()
 #define GCP_MARK_END_TRIM_HW_EVENT mprofiler::VMProfiler::MProfMarkEndTrimHWEvent()


 #define GCP_MARK_START_WAIT_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkWaitTimeEvent(TH)
 #define GCP_MARK_END_WAIT_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkEndWaitTimeEvent(TH)
 #define GCP_MARK_START_EXPL_GC_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkGCExplTimeEvent(TH)
 #define GCP_MARK_END_EXPL_GC_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkEndGCExplTimeEvent(TH)
 #define GCP_MARK_START_GC_HAT_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkGCHatTimeEvent(TH)
 #define GCP_MARK_END_GC_HAT_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkEndGCHatTimeEvent(TH)
 #define GCP_MARK_START_SAFE_POINT_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkStartSafePointEvent(TH)
 #define GCP_MARK_END_SAFE_POINT_TIME_EVENT(TH) mprofiler::VMProfiler::MProfMarkEndSafePointEvent(TH)
 #define GCP_MARK_START_SUSPEND_TIME_EVENT(TH, ST) mprofiler::VMProfiler::MProfMarkSuspendTimeEvent(TH, ST)
 #define GCP_MARK_END_SUSPEND_TIME_EVENT(TH, ST) mprofiler::VMProfiler::MProfMarkEndSuspendTimeEvent(TH, ST)

 #define GCP_MARK_PRE_COLLECTION  mprofiler::VMProfiler::MProfMarkPreCollection()
 #define GCP_MARK_POST_COLLECTION  mprofiler::VMProfiler::MProfMarkPostCollection()
#else//GCP_ART_MARK_WAIT_EVENTS
 #define GCP_MARK_START_CONC_GC_HW_EVENT ((void) 0)
 #define GCP_MARK_END_CONC_GC_HW_EVENT ((void) 0)
 #define GCP_MARK_START_ALLOC_GC_HW_EVENT ((void) 0)
 #define GCP_MARK_END_ALLOC_GC_HW_EVENT ((void) 0)
 #define GCP_MARK_START_EXPL_GC_HW_EVENT ((void) 0)
 #define GCP_MARK_END_EXPL_GC_HW_EVENT ((void) 0)
 #define GCP_MARK_START_TRIM_HW_EVENT ((void) 0)
 #define GCP_MARK_END_TRIM_HW_EVENT ((void) 0)
 #define GCP_MARK_START_WAIT_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_END_WAIT_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_START_GC_HAT_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_END_GC_HAT_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_START_EXPL_GC_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_END_EXPL_GC_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_START_SAFE_POINT_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_END_SAFE_POINT_TIME_EVENT(TH) ((void) 0)
 #define GCP_MARK_START_SUSPEND_TIME_EVENT(TH, ST) ((void) 0)
 #define GCP_MARK_END_SUSPEND_TIME_EVENT(TH, ST) ((void) 0)

 #define GCP_MARK_PRE_COLLECTION ((void) 0)
 #define GCP_MARK_POST_COLLECTION ((void) 0)
#endif//GCP_ART_MARK_WAIT_EVENTS


#define GCP_OFF_CONCURRENT_GC()			(GCP_DISABLE_CONC_COLLECT)
#define GCP_OFF_EXPLICIT_GC()			  (GCP_DISABLE_EXPL_COLLECT)



/*
 * Checks if the VM is one of the profiled Benchmarks.
 *
 * Note: found that the VM name was set multiple times. I have no idea
 */
void dvmGCMMProfPerfCounters(const char*);


namespace art {
class ConditionVariable;
class Mutex;
class Thread;
class MembersOffset;
class Thread;

namespace mirror{
class Object;
}

namespace mprofiler {
class VMProfiler;
class ObjectSizesProfiler;

typedef void (*GCMMPDumpCurrentUsage)(bool);
typedef VMProfiler* (*VMProfilerConstructor) (GCMMP_Options*, void*);
/* types of Profiling defined here */
typedef struct PACKED(4) GCMMPProfilingEntry_S {
	int 						id_;					/* id of the profiling */
	unsigned int 		flags_;				/* the flag vector used to specify the functionality*/
	const char			*name_;	     	/* event name */
	const char			*desc_;	     	/* event description */
	const char			*logFile_;	  /* log file name */
	GCMMPDumpCurrentUsage dumpMethod;
	VMProfilerConstructor creator_;
}GCMMPProfilingEntry;


/* class to keep track of integrals (area under curve for the heap volume)*/
class GCMMPHeapIntegral {
  uint64_t lastHeapSize_; /* the heap from which we start measuring */
	uint64_t lastTime_;		 /* the point we last check the integration */
	double accIntegral_;  /* accumulative integral */
	double gcCounts_;
	double gcCPULoad_;
	double gcCPUIdleLoad_;

	size_t frag_histogram_[32];
	size_t maximim_frag_length_;
public:
	GCMMPHeapIntegral(void): lastHeapSize_(0), lastTime_(0),	accIntegral_(0),
			gcCounts_(0), gcCPULoad_(0), gcCPUIdleLoad_(0) {
	  resetFragHistogram();
	}

	void gcpPreCollectionMark(SafeGCPHistogramRec* allocationRec);

	void gcpPostCollectionMark(SafeGCPHistogramRec* allocationRec);

	void gcpUpdateHeapStatus(GCMMPHeapStatus* heapStatus);

	void gcpDumpMaxContigAlloc(uint64_t alloc_bytes);

	void gcpInsertFragSeg(size_t seg_length) {
	  if(seg_length >= 8) {
	    int index = 28 - CLZ(seg_length);
	    frag_histogram_[index]++;
	    maximim_frag_length_ = std::max(maximim_frag_length_, seg_length);
	  }
	}

	void resetFragHistogram(void) {
	  maximim_frag_length_ = 0;
	  memset((void*)frag_histogram_, 0, 32 * sizeof(size_t));
	}

	void dumpFragHistogram(void) {
	  LOG(ERROR) << "_________ dumping fragents______(max=" << maximim_frag_length_<<")";
	  for(int _iter = 0; _iter < 32; _iter++) {
	    if(frag_histogram_[_iter] > 0) {
	      LOG(ERROR) << "frag["<<_iter<<"]: " << frag_histogram_[_iter];
	    }
	  }
	}
};//GCMMPHeapIntegral



template <typename T>
art::mprofiler::VMProfiler* createVMProfiler(GCMMP_Options* opts, void* entry)
{
	return new T(opts, entry);
}

//typedef SafeMap<uint32_t, MEM_MAP*, std::less<MEM_MAP*>,
//    gc::accounting::GCAllocator<std::pair<uint32_t, MEM_MAP*> > > ArchiveMemMapsT;

class VMProfiler {

protected:
	//Index of the profiler type we are running
	const int index_;

  /* file used to dump the profiling data */
  const char * 	dump_file_name_;
  art::File* 		dump_file_;

  const char			*perfName_;

  void InitializeProfiler(void);

  void DumpProfData(bool);

  /*
   * Guards access to the state of the profiler daemon,
   * associated conditional variable is used to signal when a GC completes
   */
  Mutex* prof_thread_mutex_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> prof_thread_cond_ GUARDED_BY(prof_thread_mutex_);
  pthread_t pthread_ GUARDED_BY(prof_thread_mutex_);

public:
  static constexpr int kGCMMPDumpSignal 			= SIGUSR2;
  static constexpr int kGCMMPDefaultCohortLog = GCP_DEFAULT_COHORT_LOG;
  static const int kGCMMPDefaultAffinity 			= -1;
//	static const unsigned int kGCMMPEnableProfiling = 0;
  static const int kGCMMPDisableMProfile = 999;
  static const int kGCMMPDefaultGrowMethod = 0;
	static const int kGCMMPDumpEndMarker;
  static const int kGCMMPMaxEventsCounts = 1024;
	// List of profiled benchmarks in our system
	static const char * benchmarks[];
  // combines markAllocWindows, createProfDaemon, hasProfThread,
	/*
	 * list of the directory paths to save the logs
	 */
	static const char * gcMMPRootPath[];

	/*
	 * Predefined List of types
	 */
	static const GCMMPProfilingEntry profilTypes[];



	//static AtomicInteger GCPTotalAllocBytes;

	//static GCPHistogramRecAtomic allocatedBytesData;

	static SafeGCPHistogramRec allocatedBytesData_;

	static int kGCMMPLogAllocWindow;
  static int kGCMMPAllocWindow;
	static int kGCMMPLogAllocWindowDump;
  static int kGCMMPAllocWindowDump;

	static bool system_server_created_;

	std::vector<std::string> app_list_;

	const bool enabled_;
	// System thread used as main (thread id = 1).

	int gcDaemonAffinity_;

	Thread* prof_thread_;

	Thread* main_thread_;
  // System thread used as GC Daemon.
	Thread* gc_daemon_;
  // System thread used as GC Timmer.
	Thread* gc_trimmer_;
  // flags of the Profiler we are running
  unsigned int flags_;
	volatile bool has_profDaemon_;
	volatile bool receivedShutdown_;
	volatile bool running_;
	volatile bool receivedSignal_ GUARDED_BY(prof_thread_mutex_);

  size_t 		start_heap_bytes_;
  size_t 		end_heap_bytes_;

  uint64_t 	start_time_ns_;
  uint64_t 	end_time_ns_;
  uint64_t 	start_cpu_time_ns_;
  uint64_t 	end_cpu_time_ns_;

  Mutex* evt_manager_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  EventMarkerManager* markerManager;
//
//  typedef SafeMap<uint32_t, MEM_MAP*, std::less<MEM_MAP*>,
//      gc::accounting::GCAllocator<std::pair<uint32_t, MEM_MAP*> > > ArchiveMemMapsT;
  SafeMap<uint32_t, MEM_MAP*> map_archives_ /*GUARDED_BY(*evt_manager_lock_)*/;

  GCMMPHeapStatus heapStatus;
  GCMMPHeapIntegral heapIntegral_;

  uint64_t GetRelevantCPUTime(void) const {
  	return ProcessTimeNS() - start_cpu_time_ns_;
  }

  uint64_t GetRelevantRealTime(void) const {
  	return NanoTime() - start_time_ns_;
  }


  std::vector<GCMMPThreadProf*> threadProfList_;
  std::vector<Thread*> delayedProfThread_;

  art::File* GetDumpFile(void) const {
  	return dump_file_;
  }

  void attachThreads(void);
#if 0
  void notifyAllocation(size_t,size_t);
#endif
  void notifyAllocation(size_t, size_t, mirror::Object*);
//  virtual void notifyFreeing(size_t, size_t){}
  void notifyFree(size_t);
  void createProfDaemon();

  VMProfiler(GCMMP_Options*, void*);
  void createAppList(GCMMP_Options*);
	virtual ~VMProfiler(){}

	static void* runDaemon(void* arg);

  bool IsProfilingEnabled() const {
    return enabled_;
  }

  bool IsCreateProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_CREATE_DAEMON);
  }

  bool IsProfilingRunning() {
    return running_;
  }

  void setIsProfilingRunning(bool val) {
    running_ = val;
  }

  bool IsProfilerThread(Thread* th) const;

  bool IsAttachProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_ATTACH_PROF_DAEMON);
  }

  bool IsSetAffinityThread() const {
    return IsProfilingEnabled() && (gcDaemonAffinity_ != kGCMMPDefaultAffinity) ;
  }

  bool IsAttachGCDaemon () {
    return (flags_ & GCMMP_FLAGS_ATTACH_GCDAEMON);
  }

  bool IsAllocWindowsSet() {
  	return (flags_ & GCMMP_FLAGS_MARK_ALLOC_WINDOWS);
  }

  bool IsMutationsWindowsSet() {
  	return (flags_ & GCMMP_FLAGS_MARK_MUTATIONS_WINDOWS);
  }

  bool IsProfilingTimeEvent() {
    return IsProfilingRunning() && isMarkTimeEvents();
  }

  bool IsProfilingHWEvent() {
    return IsProfilingRunning() && isMarkHWEvents();
  }

	void setMainThread(Thread* thread) {
		main_thread_ = thread;
	}

	void setGcDaemon(Thread* thread) {
		gc_daemon_ = thread;
	}

	void setGcTrimmer(Thread* thread) {
		gc_trimmer_ = thread;
	}

	void accountFreeing(size_t objSize);

	void accountAllocating(size_t objSize);

	void accountAllocating(size_t objSize, uint64_t* before_val,
	                       uint64_t* after_val);


  void OpenDumpFile(void);
  void InitCommonData(void);
  virtual bool periodicDaemonExec(void) = 0;

  static void dvmGCMMProfPerfCountersVative(const char*);

  void ShutdownProfiling(void);
  void startProfiling(void);
  void ProcessSignalCatcher(int);
  int initCounters(const char*);

  void setProfDaemon(bool);
  void setReceivedShutDown(bool);
  bool getRecivedShutDown(void);
  bool hasProfDaemon(void);

	void updateHeapAllocStatus(void);
	int GCPGetCalculateStartBytes(void);
	int GCPGetCalculateMAXFootPrint(void);


	void updateHeapPerfStatus(uint64_t, uint64_t, uint64_t);

	void PreForkPreparation(void);
	void GCMMProfPerfCounters(const char*);


  void MarkWaitTimeEvent(GCMMPThreadProf*, GCMMP_BREAK_DOWN_ENUM);
  void MarkEndWaitTimeEvent(GCMMPThreadProf*, GCMMP_BREAK_DOWN_ENUM);

  void ForEach(void (*callback)(GCMMPThreadProf*, void*), void* context);
  void setHeapHeaderConf(GC_MMPHeapConf*);
  void dumpHeapConfigurations(GC_MMPHeapConf*);

  size_t getRelevantAllocBytes(void);
  void setThreadAffinity(art::Thread* th, bool complementary);

  static bool MProfRefDistance(const mirror::Object*, uint32_t,
  		const mirror::Object*);
	static bool IsMProfRunning();
	static bool IsMProfilingTimeEvent();
	static bool IsMProfHWRunning();
	static void MProfAttachThread(art::Thread*);
	static void MProfAttachThreadPostRenaming(art::Thread*);
//	static void MProfNotifyAlloc(size_t,size_t);
//  static void MProfNotifyAlloc(size_t, mirror::Object*);
	static void MProfNotifyAlloc(size_t, size_t, mirror::Object*);
	static void MProfObjClass(mirror::Class*, mirror::Object*);
	static void MProfNotifyFree(size_t, mirror::Object*, bool);
//	static void MProfNotifyFree(size_t,size_t);
	static void MProfileSignalCatcher(int);
	static void MProfDetachThread(art::Thread*);

	static void MProfMarkStartTrimHWEvent(void);
	static void MProfMarkEndTrimHWEvent(void);
	static void MProfMarkStartConcGCHWEvent(void);
	static void MProfMarkEndConcGCHWEvent(void);
  static void MProfMarkStartAllocGCHWEvent(void);
  static void MProfMarkEndAllocGCHWEvent(void);
  static void MProfMarkStartExplGCHWEvent(void);
  static void MProfMarkEndExplGCHWEvent(void);
  static void MProfMarkPreCollection(void);
  static void MProfMarkPostCollection(void);

  static void MProfMarkWaitTimeEvent(art::Thread*);
  static void MProfMarkEndWaitTimeEvent(art::Thread*);
  static void MProfMarkGCHatTimeEvent(art::Thread*);
  static void MProfMarkEndGCHatTimeEvent(art::Thread*);
  static void MProfMarkGCExplTimeEvent(art::Thread*);
  static void MProfMarkEndGCExplTimeEvent(art::Thread*);
  static void MProfMarkStartSafePointEvent(art::Thread*);
  static void MProfMarkEndSafePointEvent(art::Thread*);
  static void MProfMarkSuspendTimeEvent(art::Thread*, art::ThreadState);
  static void MProfMarkEndSuspendTimeEvent(art::Thread*, art::ThreadState);


  static VMProfiler* CreateVMprofiler(GCMMP_Options*);

  static uint64_t GCPCalcCohortIndex(void) {
  	return (allocatedBytesData_.get_total_count() >> GCHistogramDataManager::kGCMMPCohortLog);
  }

  virtual int32_t getGCEventsCounts(void) {
  	if(markerManager != NULL) return markerManager->curr_index_;
  	return 0;
  }

  virtual void initEventBulk(void);
  virtual void initMarkerManager(bool firstCall);

  virtual void notifyFreeing(size_t objSize, mirror::Object*) {
  	accountFreeing(objSize);
  }

  virtual void attachSingleThread(Thread* t);
  void attachSingleThreadPostRenaming(Thread* thread);
  virtual MPPerfCounter* createHWCounter(Thread*){return NULL;}

  virtual void setPauseManager(GCMMPThreadProf* thProf) {
  	thProf->pauseManager = NULL;
  }

  virtual void setHistogramManager(GCMMPThreadProf* thProf) {
  	thProf->histogramManager_ = NULL;
  }

  virtual void setThHistogramManager(GCMMPThreadProf* thProf, Thread*) {
  	setHistogramManager(thProf);
  }

  virtual bool verifyThreadNotification() {
  	return true;
  }


  static bool GCPDumpEndMarker(art::File* dumpFile){
  	return dumpFile->WriteFully(&kGCMMPDumpEndMarker,
  	                            static_cast<int64_t>(sizeof(int)));
  }

	virtual void addHWStartEvent(GCMMP_BREAK_DOWN_ENUM){};
	virtual void addHWEndEvent(GCMMP_BREAK_DOWN_ENUM) {};

  virtual bool createHWEvents(void) {return false;}
  virtual bool isMarkTimeEvents(void) {return false;}
  virtual bool isMarkHWEvents(void){return false;}
  virtual bool dettachThread(GCMMPThreadProf*){return true;}

  virtual void addEventMarker(GCMMP_ACTIVITY_ENUM);
  virtual void dumpEventMarks(void);
  virtual bool dumpEventArchive(EventMarkerArchive* event_archive);


//  virtual void AddEventMarker(GCMMP_ACTIVITY_ENUM){}
//  virtual void DumpEventMarks(void){}

  virtual void gcpAddObject(size_t, size_t){}
  virtual void gcpAddObject(size_t, size_t,
  		mirror::Object*){}
  virtual void gcpProfObjKlass(mirror::Class*, mirror::Object*){/*LOG(ERROR) << "VMProfiler::set class";*/}
  //virtual void gcpRemoveObject(size_t objSize, size_t allocSize){if(objSize == 0 || allocSize ==0) return;}
  virtual void gcpRemoveObject(size_t, mirror::Object*){}

  virtual void dumpProfData(bool) {}

  virtual double getAllocIndex() {
		return heapStatus.index;
	}
	virtual void resetHeapAllocStatus() {
		memset((void*)&heapStatus, 0, static_cast<int64_t>(sizeof(GCMMPHeapStatus)));
	}

	virtual void gcpPostMarkCollection();
};




class MMUProfiler : public VMProfiler {
public:
	MMUProfiler(GCMMP_Options* opts, void* entry);
	~MMUProfiler(){};


	MPPerfCounter* createHWCounter(Thread*);
	bool isMarkTimeEvents(void) {return true;}
	bool isMarkHWEvents(void) {return false;}
	bool periodicDaemonExec(void);
	bool dettachThread(GCMMPThreadProf*);

	void setPauseManager(GCMMPThreadProf*);

	void dumpProfData(bool);

	void initMarkerManager(bool) {
		markerManager = NULL;
	}
};

class CPUFreqProfiler : public VMProfiler {
public:
	bool periodicDaemonExec(void);
	CPUFreqProfiler(GCMMP_Options* opts, void* entry);
	~CPUFreqProfiler(){};

  void dumpProfData(bool);

  void AddEventMarker(GCMMP_ACTIVITY_ENUM);
  void DumpEventMarks(void);
  bool isMarkHWEvents(void) {return true;}


};


class PerfCounterProfiler : public VMProfiler {

public:
	const char			*hwEvent_;

	PerfCounterProfiler(GCMMP_Options* opts, void* entry);
	~PerfCounterProfiler(){};

	MPPerfCounter* createHWCounter(Thread*);

	bool createHWEvents(void) {return true;}
	bool isMarkTimeEvents(void) {return false;}
	bool isMarkHWEvents(void) {return true;}
	bool periodicDaemonExec(void);
	void readPerfData(void);
	bool dettachThread(GCMMPThreadProf*);
	void getPerfData(void);
	void logPerfData(void);


	void addHWStartEvent(GCMMP_BREAK_DOWN_ENUM evt);
	void addHWEndEvent(GCMMP_BREAK_DOWN_ENUM evt);


  void dumpProfData(bool);
  void dumpHeapStats(void);

  virtual void AddEventMarker(GCMMP_ACTIVITY_ENUM);
  virtual void DumpEventMarks(void);

};


class GCDaemonCPIProfiler : public VMProfiler {


public:
	GCMMPCPIData accData;

	GCDaemonCPIProfiler(GCMMP_Options* opts, void* entry);
	~GCDaemonCPIProfiler(){};
	bool createHWEvents(void) {return true;}
	bool isMarkHWEvents(void) {return true;}
	void attachSingleThread(Thread*);
	MPPerfCounter* createHWCounter(Thread*);
	void addHWStartEvent(GCMMP_BREAK_DOWN_ENUM);
	void addHWEndEvent(GCMMP_BREAK_DOWN_ENUM);
	bool dettachThread(GCMMPThreadProf*);
	void dumpCPIStats(GCMMPCPIDataDumped*);
	bool periodicDaemonExec(void);
	void dumpProfData(bool);
};//GCDaemonCPIProfiler

/* Application profilier for heap */

//typedef struct PACKED(4) GCPHistogramRecord_S {
//	double   index;
//	double cntLive;
//	double cntTotal;
//	double pcntLive;
//	double pcntTotal;
//} GCPHistogramRecord;
//
//
//typedef struct PACKED(4) GCPObjectExtraHeader_S {
//	size_t objSize;
//	size_t threadID;
//}GCPObjectExtraHeader;
//
//
//typedef struct PACKED(4) GCPObjectHeaderTest_S {
//	mirror::Object* obj;
//	GCPObjectExtraHeader headerReplica;
//	int takeTest;
//}GCPObjectHeaderTest;



class FragGCProfiler : public VMProfiler {
 public:
  GCHistogramDataManager* hitogramsData_;

   FragGCProfiler(GCMMP_Options* opts, void* entry);
   ~FragGCProfiler(){};
   void initHistDataManager(void);

   bool isMarkHWEvents(void) {return false;}

   bool periodicDaemonExec(void);
   void gcpLogPerfData(void);
   void resetFragHandlers();
   //overrides
   void dumpProfData(bool isLastDump);
   void gcpPostMarkCollection(void);

   void gcpAddFragSegment(size_t used_bytes, size_t freeLength) {
     hitogramsData_->addObjectFast(used_bytes, freeLength);
   }
   void attachSingleThread(Thread*);
   void initMarkerManager(bool) {
      markerManager = NULL;
   }
   GCHistogramObjSizesManager* getFragHistograms(void) {
      return (GCHistogramObjSizesManager*) hitogramsData_;
    }
};//FragGCProfiler

class ObjectSizesProfiler : public VMProfiler {
public:
	GCHistogramDataManager* hitogramsData_;

	GCHistogramObjSizesManager* getObjHistograms(void) {
		return (GCHistogramObjSizesManager*) hitogramsData_;
	}


	ObjectSizesProfiler(GCMMP_Options* argOptions, void* entry,
			bool initHistogrms);

	ObjectSizesProfiler(GCMMP_Options* opts, void* entry);

	~ObjectSizesProfiler(){};

	void initializeProfilerData(bool);
	static size_t GCPAddMProfilingExtraBytes(size_t);
	static size_t GCPRemoveMProfilingExtraBytes(size_t);
	static void GCPInitObjectProfileHeader(size_t allocatedMemory,
			mirror::Object* obj);

	static int GCPGetExtraProfileBytes(void) {
		return GCHistogramDataManager::kGCMMPHeaderSize;
	}

	virtual void initHistDataManager(void);
  //void gcpProfObjKlass(mirror::Class*, mirror::Object*){ LOG(ERROR) << "ObjectSizesProfiler::set class";}

	bool isMarkTimeEvents(void) {return false;}
	bool isMarkHWEvents(void) {return false;}



	bool periodicDaemonExec(void);
	void dumpProfData(bool);
  void dumpHeapStats(void);
  virtual void gcpLogPerfData(void);


//  void setHistogramManager(GCMMPThreadProf*);

	MPPerfCounter* createHWCounter(Thread*);

	bool dettachThread(GCMMPThreadProf*);

  void gcpAddObject(size_t objSize, size_t allocSize);
  void gcpAddObject(size_t allocatedMemory, size_t objSize, mirror::Object* obj);
  void gcpAddObject(size_t allocatedMemory,
  		size_t objSize, mirror::Object* obj, GCMMPThreadProf* thProf);

//  void notifyFreeing(size_t, size_t);
  void notifyFreeing(size_t, mirror::Object* obj);
//  void gcpRemoveObject(size_t objSize, size_t allocSize);
  void gcpRemoveObject(size_t sizeOffset, mirror::Object* obj);
  virtual bool waitForProfileSignal(void);


//  void gcpAddDataToHist(GCPHistogramRecord*);
  virtual void gcpUpdateGlobalHistogram(void);
//  void gcpAggregateGlobalRecs(GCPHistogramRecord*, GCPHistogramRecord*, bool);
//  void gcpResetLastLive(GCPHistogramRecord*, GCPHistogramRecord*);
  virtual void gcpFinalizeHistUpdates(void);

  void gcpResetObjectHeader(mirror::Object* obj);


};


class ThreadAllocProfiler : public ObjectSizesProfiler {

//	bool dumpGlobalThreadsStats(void);
//	bool dumpGlobalThreadsAtomicStats(void);
public:

	ThreadAllocProfiler(GCMMP_Options* opts, void* entry) :
		ObjectSizesProfiler(opts, entry, false) {
		initHistDataManager();
		LOG(ERROR) << "ThreadAllocProfiler : Constructor of ThreadAllocProfiler";
	}

	GCPThreadAllocManager* getThreadHistManager(void) {
		return (GCPThreadAllocManager*)hitogramsData_;
	}

  void setHistogramManager(GCMMPThreadProf*);
  void setThHistogramManager(GCMMPThreadProf*, Thread*);
  void initHistDataManager(void);
//  bool periodicDaemonExec(void);
  bool dettachThread(GCMMPThreadProf*);
  //void dumpProfData(bool);
  //void dumpHeapStats(void);

  void gcpUpdateGlobalHistogram(void);
  void gcpFinalizeHistUpdates(void);

  void gcpLogPerfData(void);

  void gcpAddObject(size_t objSize, size_t allocSize);
  void gcpAddObject(size_t allocatedMemory,
  		size_t objSize, mirror::Object* obj);

  bool verifyThreadNotification(void);

  void setThreadProfName(GCMMPThreadProf*, Thread*);
};

class CohortProfiler : public ObjectSizesProfiler {
public:
	GCCohortManager* getCohortManager(void) {
		return (GCCohortManager*)hitogramsData_;
	}

	CohortProfiler(GCMMP_Options* opts, void* entry) :
		ObjectSizesProfiler(opts, entry, false) {
		initializeCohortProfilerData(true);
	}

	CohortProfiler(GCMMP_Options* opts, void* entry, bool initHists) :
		ObjectSizesProfiler(opts, entry, false) {
		initializeCohortProfilerData(initHists);
	}

	void initializeCohortProfilerData(bool shouldInitHists) {
		GCCohortManager::kGCPLastCohortIndex.store(GCPCalcCohortIndex());
		if(shouldInitHists)
			initHistDataManager();
		LOG(ERROR) << "CohortProfiler : Constructor of CohortProfiler";
	}

  void initHistDataManager(void);
  void setHistogramManager(GCMMPThreadProf*);
  void gcpRemoveObject(size_t sizeOffset, mirror::Object* obj);
//  bool periodicDaemonExec(void);
  void gcpAddObject(size_t allocatedMemory, size_t objSize,
  		mirror::Object* obj);
  void gcpAddObject(size_t, size_t){}
  bool dettachThread(GCMMPThreadProf*);

  //void dumpProfData(bool);
  void gcpLogPerfData(void);
};


/**************** Ref Distance Profiler********************/
class RefDistanceProfiler : public CohortProfiler {
public:

	RefDistanceProfiler(GCMMP_Options* opts, void* entry) :
		CohortProfiler(opts, entry, false) {
		initHistDataManager();
		LOG(ERROR) << "CohortProfiler : Constructor of CohortProfiler";
	}

	void gcpProfilerDistance(const mirror::Object* dst,
			uint32_t member_offset, const mirror::Object* new_value);

	GCRefDistanceManager* getDistanceProfManager(void) {
		return (GCRefDistanceManager*) hitogramsData_;
	}
	void initHistDataManager(void);
	void gcpFinalizeHistUpdates(void);
};

/**************** ClassProfiler Profiler********************/
class ClassProfiler : public ObjectSizesProfiler {
public:

	GCClassTableManager* getClassHistograms(void) {
		return (GCClassTableManager*) hitogramsData_;
	}

	ClassProfiler(GCMMP_Options*, void*);

	void initHistDataManager(void);

  void gcpAddObject(size_t allocatedMemory,
  		size_t objSize, mirror::Object* obj);
  void gcpProfObjKlass(mirror::Class*, mirror::Object*);
  void gcpAddObject(size_t, size_t){}
//  void gcpRemoveObject(size_t, mirror::Object*);
  void dumpAllClasses(void);
  //void dumpProfData(bool);
  void gcpLogPerfData(void);
  //void gcpUpdateGlobalHistogram(void);
  void gcpFinalizeHistUpdates(void);
};


//typedef struct PACKED(4) GCPCohortRecord_S {
//	/* cohort index */
//	double   index;
//	/* tracking objects allocation */
//	GCPHistogramRecord cohortObjStats;
//	/* tracking volume statistics */
//	GCPHistogramRecord cohortVolumeStats;
//	/* object size histogram */
//	GCPHistogramRecord histogramTable[32];
//} GCPCohortRecord;
//
//typedef struct PACKED(4) GCPCohortsRow_S {
//	int index;
//	GCPCohortRecord cohortArr[GCP_MAX_COHORT_ROW_SIZE];
//} GCPCohortsRow;
//
//typedef struct PACKED(4) GCPCohortsTable_S {
//	int index;
//	GCPCohortsRow* cohortRows[GCP_MAX_COHORT_ARRAYLET_SIZE];
//} GCPCohortsTable;
//
//
//class CohortProfiler : public ObjectSizesProfiler {
//public:
//
//	~CohortProfiler(){};
//	void initHistogram(void);
//
//	int cohortIndex;
//	size_t cohortArrayletSize;
//	size_t cohortRowSize;
//	GCPCohortsTable cohortsTable;
//
//	GCPCohortsRow*    currCohortRow;
//	GCPCohortRecord*  currCohortRec;
//
//	void addCohortRecord(void);
//	void addCohortRow(void);
//	void initCohortsTable(void);
//
//	int getExtraProfileBytes(void) {return 8;}
////  void gcpAddObject(size_t objSize, size_t allocSize);
////  void gcpAddObject(size_t allocatedMemory, size_t objSize, mirror::Object* obj);
////  void gcpRemoveObject(size_t objSize, size_t allocSize);
////  void gcpRemoveObject(size_t sizeOffset, mirror::Object*);
//  void addObjectToCohortRecord(GCPCohortRecord*, size_t, size_t, bool);
//
//	bool periodicDaemonExec(void);
//	void dumpProfData(bool);
//  void dumpHeapStats(void);
//  void logPerfData(void);
//	MPPerfCounter* createHWCounter(Thread*);
//
//	bool dettachThread(GCMMPThreadProf*);
////  void notifyFreeing(size_t, size_t);
////  void notifyFreeing(size_t, mirror::Object*);
//  inline void dumpCohortGeneralStats(void);
//
//	CohortProfiler(GCMMP_Options* opts, void* entry) :
//		ObjectSizesProfiler(opts, entry) {
//		initCohortsTable();
//	}
//};


#if 0
class MProfiler {
private:

	//Index of the profiler type we are running
	const int index_;
  // System thread used as main (thread id = 1).
	Thread* main_thread_;
  // System thread used as GC Daemon.
	Thread* gc_daemon_;
  // System thread used as GC Timmer.
	Thread* gc_trimmer_;
  // flags of the Profiler we are running
  // combines markAllocWindows, createProfDaemon, hasProfThread,
  unsigned int flags_;

  /* file used to dump the profiling data */
  const char * dump_file_name_;
  art::File* dump_file_;

  /*
   * Guards access to the state of the profiler daemon,
   * associated conditional variable is used to signal when a GC completes
   */
  Mutex* prof_thread_mutex_ DEFAULT_MUTEX_ACQUIRED_AFTER;
  UniquePtr<ConditionVariable> prof_thread_cond_ GUARDED_BY(prof_thread_mutex_);
  pthread_t pthread_ GUARDED_BY(prof_thread_mutex_);
	/* array of thread records used to keep the data per thread */
  // The actual list of all threads.
  std::list<GCMMPThreadProf*> threadProflist_;
  // System thread used for the profiling (profileDaemon).
	Thread* prof_thread_;
  void CreateProfilerDaemon(void);

  void InitializeProfiler(void);

  void DumpProfData(bool);

	int gcDaemonAffinity_;

  const bool enabled_;

  volatile bool running_;

  static void* Run(void* arg);


  void OpenDumpFile(void);

  bool IsCreateProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_CREATE_DAEMON);
  }

  bool IsAttachProfDaemon() const {
    return (flags_ & GCMMP_FLAGS_ATTACH_PROF_DAEMON);
  }

  bool IsProfilingRunning() {
    return vmProfile->IsProfilingRunning();
  }

  bool IsProfilingTimeEvent() {
    return vmProfile->IsProfilingRunning() && vmProfile->isMarkTimeEvents();
  }

  bool IsAttachGCDaemon () {
    return (flags_ & GCMMP_FLAGS_ATTACH_GCDAEMON);
  }

  bool hasProfDaemon_;

  volatile bool receivedSignal_ GUARDED_BY(prof_thread_mutex_);

  void ShutdownProfiling(void);

  void SetMProfileFlags(void);

  void AttachThreads(void);

  bool ProfiledThreadsContain(Thread*);
  void RemoveThreadProfile(GCMMPThreadProf*);

public:
	static constexpr int kGCMMPDumpSignal = SIGUSR2;
//	static const unsigned int kGCMMPEnableProfiling = 0;
  static const int kGCMMPDisableMProfile = 999;
  static const int kGCMMPDefaultGrowMethod = 0;
  static const int kGCMMPMAXThreadCount = 64;
  static const int kGCMMPDefaultAffinity = -1;
  static const int kGCMMPDumpEndMarker;



	MProfiler(GCMMP_Options*);

	~MProfiler();

	//bool VerifyProcessName();

	void ProcessSignal(int);

  bool IsProfilingEnabled() const {
    return enabled_;
  }

  bool SetAffinityThread() const {
    return IsProfilingEnabled() && (gcDaemonAffinity_ != kGCMMPDefaultAffinity) ;
  }

  art::File* GetDumpFile(void) const {
  	return dump_file_;
  }


  void AttachThread(Thread*);
  bool DettachThread(GCMMPThreadProf*);


  void SetThreadAffinity(Thread*,bool);

  GCMMPDumpCurrentUsage dumpCurrUsage;
  void DumpCurrentOutpu(void);


  void ForEach(void (*callback)(GCMMPThreadProf*, void*), void* context);
  bool MainProfDaemonExec(void);

  void ProcessSignalCatcher(int);

  /* represents the time captured when we started the profiling */
  uint64_t 	start_time_ns_;
  uint64_t 	end_time_ns_;
  uint64_t 	cpu_time_ns_;
  uint64_t 	end_cpu_time_ns_;
  size_t 		start_heap_bytes_;
  size_t 		end_heap_bytes_;

  uint64_t GetRelevantCPUTime(void) const {
  	return ProcessTimeNS() - cpu_time_ns_;
  }

  uint64_t GetRelevantRealTime(void) const {
  	return uptime_nanos() - start_time_ns_;
  }

  size_t GetRelevantAllocBytes(void);

  int GetMainID(void);

  int GetGCDaemonID(void);

  friend class GCMMPThreadProf;


  VMProfiler* vmProfile;
}; //class MProfiler
#endif //romove mprofiler class

}  // namespace mprofiler
}  // namespace gc

#endif /* MPROFILER_H_ */
