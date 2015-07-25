/*
 * MPPerfCounters.h
 *
 *  Created on: Sep 9, 2014
 *      Author: hussein
 */

#ifndef MPPERFCOUNTERS_H_
#define MPPERFCOUNTERS_H_

#include "base/histogram.h"
#include "base/macros.h"
#include "base/mutex.h"

#include <string>
#include <vector>
#include <map>

#include <stdint.h>
#include "cutils/perflib.h"


namespace art {
namespace mprofiler {

typedef struct GCMMP_Options_s {
	std::string app_list_path_;
	int mprofile_type_;
	int mprofile_grow_method_;
	int mprofile_gc_affinity_;
	int gcp_type_;
	int cohort_log_;
	int alloc_window_log_;
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
	GCMMP_FLAGS_ATTACH_GCDAEMON = 16,
	GCMMP_FLAGS_MARK_MUTATIONS_WINDOWS = 32 //should we mark the mutations chunks
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


typedef struct GC_MMPHeapConf_S {
	uint32_t startSize;
	uint32_t growthLimit;
	double elapsedUPTimeNS;
} GC_MMPHeapConf;

typedef struct GCMMPHeapStatus_S {
	double 			index;
	double			timeInNsec;
	size_t 			allocatedBytes;
	size_t 			currAllocBytes;
	size_t			currFootPrint;
	size_t 			heapIdealFree;
	size_t 			concurrentStartBytes;
	size_t			softLimit;
	double			heapIntegral;
	double			gcCounts;
	double			gcCPULoad;
	double			gcCPUIdleLoad;
	double 			heapTargetUtilization;
	double 			gcDaemonUsage;
	double			gcMutUsage;
	uint64_t 	  totalMetric;
} GCMMPHeapStatus;

typedef struct GCMMPCPIData_S {
	pid_t 			threadID;
	uint64_t		currCycles;
	uint64_t 		currInstructions;
	uint64_t		cycles;
	uint64_t 		instructions;
} GCMMPCPIData;


typedef struct GCMMPCPIDataDumped_S {
	double 			index;
	uint64_t		currCycles;
	uint64_t 		currInstructions;
	double		  currCPI;
	double 		  averageCPI;
} GCMMPCPIDataDumped;


class PerfEventLogger {
 public:
  // Splits are nanosecond times and split names.
  typedef std::pair<int32_t, uint64_t> EventReading;
  typedef std::vector<EventReading> EventReadings;

  EventReadings events;
  GCPauseThreadMarker eventMarkers[GCMMP_GC_BRK_MAXIMUM];
  uint64_t eventAccMarkers[GCMMP_GC_BRK_MAXIMUM];

  void addEvents(int32_t, uint64_t);
  void addStartMarkEvent(GCMMP_BREAK_DOWN_ENUM, uint64_t);
  uint64_t addEndMarkEvent(GCMMP_BREAK_DOWN_ENUM, uint64_t);
  void dumpMarks(void);
  void getGCMarks(uint64_t*);

  explicit PerfEventLogger(void);


};

class MPPerfCounter {
public:
	static const bool kGCPerfCountersExcIdle = true;
	static const int 	kGCPerfCountersNameSize = 16;
	PerfLibCounterT*  hwCounter;
	const char* event_name_;
	PerfEventLogger evtLogger;
	uint64_t data;
	uint64_t gcAcc;
	uint64_t noSpectialAcc;

	void getGCDataDistributions(uint64_t*, uint64_t*, uint64_t*);
	MPPerfCounter(const char*);
	MPPerfCounter(void);
	/*
	 * Initiliazes performance library counters
	 */
	int InitPerflib(void) {
		return init_perflib_counters();
	}

	/*
	 * terminates performance library counters
	 */
	int TerminatePerflib(void) {
		return terminate_perflib_counters();
	}

	/*
	 * Open perflib and process ID
	 */
	bool OpenPerfLib(pid_t);
	bool ClosePerfLib(void);
	void readPerfData(void);

  static MPPerfCounter* Create(const char* event_name);

  void storeReading(int32_t);

  void addStartEvent(GCMMP_BREAK_DOWN_ENUM);
  void addEndEvent(GCMMP_BREAK_DOWN_ENUM);
  uint64_t addEndEventNOSpecial(GCMMP_BREAK_DOWN_ENUM);
  void dumpMarks(void);
  void getGCMarks(uint64_t*);
};

} //namespace mprofiler
} //namespace art

#endif /* MPPERFCOUNTERS_H_ */
