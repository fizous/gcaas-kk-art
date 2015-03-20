/*
 * MProfilerTypes.h
 *
 *  Created on: Aug 29, 2014
 *      Author: hussein
 */

#ifndef MPROFILERTYPES_H_
#define MPROFILERTYPES_H_

#include "base/histogram.h"
#include "base/macros.h"
#include "base/mutex.h"

#include <string>
#include <vector>
#include <map>

#include "cutils/system_clock.h"
#include "gc_profiler/MPPerfCounters.h"

#define GCMMP_GCPAUSE_ARRAY_SIZE					  32

namespace art {

namespace mprofiler {



typedef struct GCMMP_Options_s {
	int mprofile_type_;
	int mprofile_grow_method_;
	int mprofile_gc_affinity_;
} GCMMP_Options;

/**
 * enum used to define the concurrency of the virtual machine
 */
typedef enum {
	GC_GROW_METHOD_DEFAULT 	= 0,
	GC_GROW_METHOD_BG_GC 		= 1,
	GC_GROW_METHOD_WITH_PID = 2,
	GC_GROW_METHOD_WITH_HEUR= 4,
	GC_GROW_METHOD_FG_GC		= 8,
	GC_GROW_METHOD_EXPL_OFF	= 16,
	GC_GROW_METHOD_SUPER_BG_GC 		= 32
} GC_GROW_METHODS;



/*
 * enum of the events we are profiling per mutator. we can look for activities.
 * Make sure that GCMMP_GC_BRK_MAXIMUM always at the bottom of the definition
 */
typedef enum {
	GCMMP_GC_BRK_NONE = 0,
	GCMMP_GC_BRK_SUSPENSION = 1,
	GCMMP_GC_BRK_WAIT_CONC,
	GCMMP_GC_BRK_GC_EXPL,
	GCMMP_GC_BRK_GC_HAT,
	GCMMP_GC_BRK_HEAP_LOCK,
	GCMMP_GC_BRK_SAFEPOINT,
	GCMMP_GC_BRK_MAXIMUM
} GCMMP_BREAK_DOWN_ENUM;


/*
 * represents the status of the thread. We need to know whether a thread
 * is profiled, stopped, of still running from the profiler point of view.
 */
typedef enum GCMMPThreadProfStateT {
	GCMMP_TH_ALLOC = 0,
	GCMMP_TH_STARTING,
	GCMMP_TH_RUNNING,
	GCMMP_TH_STOPPED,
	GCMMP_TH_REMOVED
} GCMMPThreadProfState;

/*
 * Flags used to check the functionality of the mprofiler->flags_
 */
typedef enum GCMMPFlagsEnum_s {
	GCMMP_FLAGS_NONE = 0,
	GCMMP_FLAGS_CREATE_DAEMON = 1, //should we create a daemon profiler?
	GCMMP_FLAGS_HAS_DAEMON = 2,		 //does it possess a daemon thread
	GCMMP_FLAGS_ATTACH_PROF_DAEMON = 4, // should we attach the profile daemon
	GCMMP_FLAGS_MARK_ALLOC_WINDOWS = 8, //should we mark the allocation chunks
	GCMMP_FLAGS_ATTACH_GCDAEMON = 16
} GCMMPFlagsEnum;



/*
 * Flags used to check the functionality of the mprofiler->flags_
 */
typedef enum GCMMPThProfileTag_s {
	GCMMP_THREAD_DEFAULT = 0,
	GCMMP_THREAD_MAIN = 1,
	GCMMP_THREAD_GCDAEMON = 2, //should we create a daemon profiler?
	GCMMP_THREAD_GCTRIM = 3		 //does it possess a daemon thread
} GCMMPThProfileTag;

/*
 * Struct used to hold the temporary values when we enter the block of an
 * activity. When we exit we add the delta to the accumulated field.
 */
typedef struct PACKED(4) GCMMP_ProfileActivity_S {
	/* Used as a temp to hold the counter vaue when we enter the activity block */
	uint64_t startMarker;
	/* represents the total value of the activity */
	uint64_t finalMarker;
} GCMMP_ProfileActivity;

/*
 * Struct used to hold the temporary values when we enter the block of an
 * activity. When we exit we add the delta to the accumulated field.
 */
typedef struct PACKED(4) GCPauseThreadMarker_S {
	/* Used as a temp to hold the counter vaue when we enter the activity block */
	uint64_t startMarker;
	/* represents the total value of the activity */
	uint64_t finalMarker;
	/* the type of the pause */
	GCMMP_BREAK_DOWN_ENUM type;
} GCPauseThreadMarker;

class MProfiler;
class VMProfiler;
class GCMMPThreadProf;
class MPPerfCounter;


class PACKED(4) GCPauseThreadManager {
	 GCPauseThreadMarker* curr_marker_;
	 GCPauseThreadMarker* pauseEvents[GCMMP_GCPAUSE_ARRAY_SIZE];
	 int curr_bucket_ind_;
	 int curr_entry_;
	 int ev_count_;
	 bool busy_;
	 int count_opens_;
public:
	 static constexpr int kGCMMPMaxEventEntries = 32;
	 static constexpr int kGCMMPMaxBucketEntries = GCMMP_GCPAUSE_ARRAY_SIZE;
	 static uint64_t startRealTime;
	 static uint64_t startCPUTime;

	 static uint64_t GetRelevantCPUTime(void);

	 static uint64_t GetRelevantRealTime(void);

	 GCPauseThreadManager(void) :
		 curr_bucket_ind_(-1), curr_entry_(-1), ev_count_(-1), busy_(false), count_opens_(0) {
		 IncrementIndices();
	 }

	 ~GCPauseThreadManager(void);

	 void InitPausesEntry(GCPauseThreadMarker** entryPointer) {
			*entryPointer =
					reinterpret_cast<GCPauseThreadMarker*>(calloc(kGCMMPMaxEventEntries,
					sizeof(GCPauseThreadMarker)));
		} //InitPausesEntry

	 void IncrementIndices(void) {
			ev_count_++;
			curr_entry_ = (curr_entry_ + 1) % kGCMMPMaxEventEntries;
			if(curr_entry_ == 0) {
				curr_bucket_ind_++;
				if(curr_bucket_ind_ >= kGCMMPMaxBucketEntries) {
					LOG(ERROR) << "MProfiler: Exceeded maximum count of entries ";
				}
		//		GCMMP_VLOG(INFO) << "MPRofiler: Initializing entry for the manager " << curr_bucket_ind_ << ", " << curr_entry_;
				InitPausesEntry(&pauseEvents[curr_bucket_ind_]);
			}
			busy_ = false;
			curr_marker_ = &(pauseEvents[curr_bucket_ind_][curr_entry_]);
		//	GCMMP_VLOG(INFO) << "MPRofiler: Incremented Indices " << ev_count_ << ", " << curr_entry_ << ", " << curr_bucket_ind_;
	 } //IncrementIndices

	 void MarkStartTimeEvent(GCMMP_BREAK_DOWN_ENUM);
	 void MarkEndTimeEvent(GCMMP_BREAK_DOWN_ENUM);
	 bool HasData(void) const {
		 return (ev_count_ > 0);
	 }
	 void DumpProfData(void* args);
}; // Class GCPauseThreadManager


/*
 * Holds the profiling data per thread . We do not keep a pointer to the thread
 * because threads may terminate before we collect the information
 */
class GCMMPThreadProf {

	/* system ID of the thread monitored */
	const pid_t pid;

	/* markers used to set the temporary information to start an event */
	GCMMP_ProfileActivity timeBrks[GCMMP_GC_BRK_MAXIMUM];

	volatile bool suspendedGC;
	GCPauseThreadManager* pauseManager;
	GCMMP_ProfileActivity lifeTime_;
public:
	static MProfiler* mProfiler;
	volatile GCMMPThreadProfState state;
	GCMMPThProfileTag tag_;

	GCMMPThreadProf(MProfiler*, Thread*);
	GCMMPThreadProf(VMProfiler*, Thread*);
	~GCMMPThreadProf(void);

  void Destroy(MProfiler*);

  pid_t GetTid() const {
    return pid;
  }

  GCPauseThreadManager* getPauseMgr(void) const {
  	return pauseManager;
  }

  GCMMPThProfileTag getThreadTag(){
  	return tag_;
  }

  void setThreadTag(GCMMPThProfileTag tag){
  	tag_ = tag;
  }


  bool StopProfiling(void);
  void ForceDeadTime(void);

  uint64_t GetCreationTime(void) const {
  	return lifeTime_.startMarker;
  }

  uint64_t GetEndTime(void) const {
  	return lifeTime_.finalMarker;
  }

  GCMMP_ProfileActivity* GetliveTimeInfo(void)  {
  	return &lifeTime_;
  }

  int GetThreadType(void);

  void resetPerfRecord(){
  	perf_record_ = NULL;
  }
  // The performance counter record.
  MPPerfCounter* perf_record_;

  MPPerfCounter* GetPerfRecord() {
    return perf_record_;
  }
  void readPerfCounter(int32_t);
  uint64_t getDataPerfCounter();
};
} // namespace mprofiler
} // namespace art

#endif /* MPROFILERTYPES_H_ */
