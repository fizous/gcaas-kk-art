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
#include "safe_map.h"

#include "os.h"

#include <string>
#include <vector>
#include <utility>
#include "cutils/system_clock.h"
#include "gc_profiler/MPPerfCounters.h"

#include "gc/accounting/gc_allocator.h"


/* window size of the dumping daya */
#define GCP_WINDOW_RANGE_LOG							16

/* number of buckets to create the histogram */
#define GCP_MAX_COHORT_ARRAYLET_CAP				128
#define GCP_MAX_COHORT_ROW_CAP						64
#define GCP_COHORT_LOG										18
#define GCP_COHORT_SIZE										262144 // 256k

#define GCP_MAX_HISTOGRAM_SIZE						32
#define GCMMP_GCPAUSE_ARRAY_SIZE					32

namespace art {

//namespace gc {
//
//namespace accounting {
// class GCAllocator;
//}
//
//}
namespace mprofiler {

class MProfiler;
class VMProfiler;
class GCMMPThreadProf;
class MPPerfCounter;
class GCHistogramObjSizesManager;
class GCHistogramDataManager;
/*
 * enum of the events we are profiling per mutator. we can look for activities.
 * Make sure that GCMMP_GC_MAX_ACTIVITIES always at the bottom of the definition
 */
typedef enum {
	GCMMP_GC_MALLOC 	= 0,
	GCMMP_GC_EXPLICIT = 1,
	GCMMP_GC_DAEMON 	= 2,
	GCMMP_GC_TRIM			= 3,
	GCMMP_GC_GROW			= 4,
	GCMMP_FORCE_UTIL	= 5,
	GCMMP_FORCE_CONC	= 6,
	GCMMP_MINOR_COLLECTION = 7,
	GCMMP_MAJOR_COLLECTION = 8,
	GCMMP_CPU_FREQ_UPDATE = 9,
	GCMMP_GC_MAX_ACTIVITIES
} GCMMP_ACTIVITY_ENUM;

typedef enum {
	GCMMP_HIST_NONE  = 0,
	GCMMP_HIST_ROOT  = 1,
	GCMMP_HIST_CHILD = 2,
} GCMMP_HISTOGRAM_MGR_TYPE;

/*
 * struct that represents the status of the heap when an event is triggered
 */
typedef struct EventMarker_S {
	/* time of the event */
	uint64_t currTime;
	/* the heap size when the event was marked */
	int32_t currHSize;
	/* event type */
	GCMMP_ACTIVITY_ENUM evType;
} EventMarker;

/*
 * Container of all the events in the profiler
 */
typedef struct EventMarkerManager_S {
	/* current index of the event being triggered */
	int32_t currIndex;
	/* pointer to the area of the memory holding all the events */
	EventMarker* markers; //
} EventMarkerManager;


typedef struct GCPHistogramRecAtomic_S {
	AtomicInteger   index;
	AtomicInteger cntLive;
	AtomicInteger cntTotal;
	double pcntLive;
	double pcntTotal;
} GCPHistogramRecAtomic;



typedef struct PACKED(4) GCPHistogramRec_S {
	double   index;
	double cntLive;
	double cntTotal;
	double pcntLive;
	double pcntTotal;
} GCPHistogramRec;


typedef struct PACKED(4) GCPCohortRecordData_S {
	double index_;
	double objLiveCnt;
	double objTotalCnt;
	double liveSize;
	double totalSize;
} GCPCohortRecordData;

class GCPHistRecData {
public:
	GCPHistogramRec dataRec_;
	GCPHistogramRecAtomic atomicDataRec_;

	static void GCPCopyRecordsData(GCPHistogramRec* dest,
			GCPHistogramRecAtomic* src) {
  	dest->index = src->index;
  	dest->cntLive = src->cntLive.load();
  	dest->cntTotal = src->cntTotal.load();
  	dest->pcntLive = src->pcntLive;
  	dest->pcntTotal = src->pcntTotal;
  }

	void initDataRecords(size_t kIndex) {
		memset((void*)&dataRec_, 0, sizeof(GCPHistogramRec));
		memset((void*)&atomicDataRec_, 0, sizeof(GCPHistogramRecAtomic));
		dataRec_.index = kIndex;
		atomicDataRec_.index = kIndex;
	}



	GCPHistRecData(size_t kIndex){
		initDataRecords(kIndex);
	}

	GCPHistRecData(void) { initDataRecords(0);}

	static bool GCPDumpHistRecord(art::File* file, GCPHistogramRec* rec);

	GCPHistogramRec* gcpGetDataRecP(void) {
		return &dataRec_;
	}

	GCPHistogramRecAtomic* gcpGetAtomicDataRecP(void) {
		return &atomicDataRec_;
	}

	bool gcpDumpHistRec(art::File*);

	bool gcpDumpAtomicHistRec(art::File*);



	void gcpUpdateRecPercentile(GCPHistogramRec* rootRec){
		dataRec_.pcntLive = (dataRec_.cntLive * 100.0) / rootRec->cntLive;
		dataRec_.pcntTotal = (dataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void gcpUpdateAtomicRecPercentile(GCPHistogramRecAtomic* rootRec){
		atomicDataRec_.pcntLive = rootRec->cntLive == 0 ? 0.0 :
				(atomicDataRec_.cntLive * 100.0) / rootRec->cntLive;
		atomicDataRec_.pcntTotal = rootRec->cntTotal == 0 ? 0.0 :
				(atomicDataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void gcpSafeUpdateAtomicRecPercentile(GCPHistogramRecAtomic* rootRec){
		atomicDataRec_.pcntLive = (atomicDataRec_.cntLive * 100.0) / rootRec->cntLive;
		atomicDataRec_.pcntTotal = (atomicDataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void GCPZerofyHistAtomicRecData(GCPHistogramRecAtomic* rec) {
		double _index = rec->index;
	  memset((void*)(rec), 0, sizeof(GCPHistogramRecAtomic));
	  rec->index = _index;
	  rec->cntLive.store(0);
	  rec->cntTotal.store(0);
	}

	void gcpZerofyHistAtomicRecData(void) {
		double _index = atomicDataRec_.index;
	  memset((void*)(&atomicDataRec_), 0, sizeof(GCPHistogramRecAtomic));
	  atomicDataRec_.index = _index;
	  atomicDataRec_.cntLive.store(0);
	  atomicDataRec_.cntTotal.store(0);
	}

	void gcpDecRecData(void){
		dataRec_.cntLive--;
	}

	bool gcpDecAtomicRecData(void){
		if(atomicDataRec_.cntLive > 0) {
			atomicDataRec_.cntLive--;
			return true;
		}
		return false;
	}

	void gcpIncRecData(void){
		dataRec_.cntLive++;
		dataRec_.cntTotal++;
	}

	void gcpIncAtomicRecData(void){
		atomicDataRec_.cntLive++;
		atomicDataRec_.cntTotal++;
	}
};

typedef std::multimap<size_t, mprofiler::GCPHistogramRec*> HistogramTable_S;
typedef std::multimap<size_t, size_t> HistogramTableTest_S;


typedef struct PACKED(4) GCPExtraObjHeader_S {
	size_t objSize;
	union {
		GCHistogramDataManager* histRecP;
		GCPHistRecData* dataRec;
		size_t objBD;
	};
} GCPExtraObjHeader;

typedef struct PACKED(4) GCPCohortsRow_S {
	int index_;
	GCPCohortRecordData cohorts[GCP_MAX_COHORT_ROW_CAP];
} GCPCohortsRow;


typedef struct GCPCohortsTable_S {
	int index;
	std::vector<GCPCohortsRow*> cohortRows_;
} GCPCohortsTable;


class GCHistogramDataManager {
public:
	static constexpr int kGCMMPMaxHistogramEntries = GCP_MAX_HISTOGRAM_SIZE;
	static int 	kGCMMPHeaderSize;
	static AtomicInteger kGCPLastCohortIndex;
	static int kGCMMPCohortLog;

	int32_t GCPGetLastManagedCohort() {
		return kGCPLastCohortIndex.load();
	}

	void GCPSetLastManagedCohort(int32_t newIndex) {
		kGCPLastCohortIndex.store(newIndex);
	}

	GCMMP_HISTOGRAM_MGR_TYPE type_;
	GCPHistRecData*				histData_;

	GCPHistogramRec				histRecord;
	GCPHistogramRecAtomic histAtomicRecord;
	//secretNumber used to check if this manager is included;
	int iSecret;

	GCHistogramDataManager(void);
	GCHistogramDataManager(GCMMP_HISTOGRAM_MGR_TYPE);
	GCHistogramDataManager(bool);
	virtual ~GCHistogramDataManager(){}

	static size_t AddMProfilingExtraBytes(size_t);
	static size_t removeMProfilingExtraBytes(size_t);
	static void GCPInitObjectProfileHeader(size_t allocatedMemory,
			mirror::Object* obj);

	static int GetExtraProfileBytes(void) {
		return GCHistogramDataManager::kGCMMPHeaderSize;
	}

  static GCPExtraObjHeader* GCPGetObjProfHeader(size_t allocatedMemory,
  		mirror::Object* obj) {
  	byte* address = reinterpret_cast<byte*>(reinterpret_cast<uintptr_t>(obj) +
  			allocatedMemory - sizeof(GCPExtraObjHeader));
  	GCPExtraObjHeader* extraHeader =
  			reinterpret_cast<GCPExtraObjHeader*>(address);
  	return extraHeader;
  }

  virtual void initHistograms(void) {}

  virtual void addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) = 0;

  virtual bool gcpDumpHistRec(art::File*);

  virtual void gcpDumpManagedData(art::File*, bool){}

  virtual void gcpZeorfyAllAtomicRecords(void){}

  void gcpAddDataToHist(GCPHistogramRec*);
  void gcpRemoveDataToHist(GCPHistogramRec*);

	GCPHistogramRec* gcpGetDataRecP(void) {
		return histData_->gcpGetDataRecP();
	}

	GCPHistogramRecAtomic* gcpGetAtomicDataRecP(void) {
		return histData_->gcpGetAtomicDataRecP();
	}

//  void setLastCohortIndex(int32_t index) {
//  	lastCohortIndex = index;
//  }


  void gcpResetHistogramRecData(GCPHistogramRec* rec) {
  	memset((void*)(rec), 0, sizeof(GCPHistogramRec));
  	rec->pcntLive = 100.0;
  	rec->pcntTotal = 100.0;
  }

  void gcpResetHistogramAtomicRecData(GCPHistogramRecAtomic* rec) {
  	memset((void*)(rec), 0, sizeof(GCPHistogramRecAtomic));
  	rec->pcntLive = 100.0;
  	rec->pcntTotal = 100.0;
  }

  void gcpZerofyHistogramAtomicRecData(GCPHistogramRecAtomic* rec) {
  	double _index = rec->index;
  	memset((void*)(rec), 0, sizeof(GCPHistogramRecAtomic));
  	rec->index = _index;
  }

  void static GCPCopyRecords(GCPHistogramRec* dest,
  		GCPHistogramRecAtomic* src) {
  	dest->index = src->index;
  	dest->cntLive = src->cntLive.load();
  	dest->cntTotal = src->cntTotal.load();
  	dest->pcntLive = src->pcntLive;
  	dest->pcntTotal = src->pcntTotal;
  }


  int generateNewSecret() {
  	return (iSecret = rand() % 1000 + 1);
  }

  void setFriendISecret(int secret) {
  	iSecret = secret;
  }

  bool gcpIsManagerFriend(GCHistogramDataManager* instMGR) {
  	return iSecret == instMGR->iSecret;
  }


};//GCHistogramDataManager


class GCCohortManager : public GCHistogramDataManager {
	size_t cohRowSZ_;
	size_t cohArrSZ_;

	size_t getCoRowSZ(void) {
		return cohRowSZ_ + sizeof(int);
	}

public:
	static constexpr int kGCMMPMaxRowCap 		= GCP_MAX_COHORT_ROW_CAP;
	static constexpr int kGCMMPMaxTableCap 	= GCP_MAX_COHORT_ARRAYLET_CAP;
	static constexpr size_t kGCMMPCohortSize = (size_t) GCP_COHORT_SIZE;

	GCPCohortRecordData*	currCohortP;
	GCPCohortsRow*    		currCoRowP;
	GCPCohortsTable 			cohortsTable;
	AtomicInteger* 				allocRec_;

	GCCohortManager(AtomicInteger*);
	void initHistograms(void);

	void addObject(size_t, size_t, mirror::Object*);
	void gcpDumpManagedData(art::File*, bool);

	void addCohortRecord(void);
	void addCohortRow(void);

	void addObjectToCohRecord(size_t objSize);

  void gcpDumpCohortData(art::File*);
	GCPCohortRecordData* getCoRecFromObj(size_t allocSpace, mirror::Object* obj);
	void gcpRemoveObject(size_t allocSpace, mirror::Object* obj);

	size_t getSpaceLeftCohort(GCPCohortRecordData* rec) {
		return kGCMMPCohortSize - rec->totalSize;
	}

	void updateCohRecObj(GCPCohortRecordData* rec, size_t fit) {
		rec->liveSize  += fit;
		rec->totalSize += fit;
		rec->objLiveCnt++;
		rec->objTotalCnt++;
	}

	void updateCohRecObjBytes(GCPCohortRecordData* rec, size_t fit) {
		rec->liveSize  += fit;
		rec->totalSize += fit;
	}

	void updateCohRecObjCnts(GCPCohortRecordData* rec) {
		rec->objLiveCnt++;
		rec->objTotalCnt++;
	}

	void updateDelCohRecObjCnts(GCPCohortRecordData* rec) {
		rec->objLiveCnt--;
	}

	void updateDelCohRecObj(GCPCohortRecordData* rec, size_t fitSize) {
		rec->liveSize  -= fitSize;

	}

	void getCoAddrFromBytes(size_t* startRow,
			size_t* startIndex, size_t* endRow, size_t* endIndex,
			size_t bd, size_t objSize) {
		*startIndex = (bd >> kGCMMPCohortLog);
		*startRow = *startIndex /  kGCMMPMaxRowCap;
		*startIndex = (*startIndex) % kGCMMPMaxRowCap;

		*endIndex = ( (bd + objSize) >> kGCMMPCohortLog);
		*endRow = *endIndex /  kGCMMPMaxRowCap;
		*endIndex = (*endIndex) % kGCMMPMaxRowCap;
	}

	GCPCohortRecordData* getCoRecFromIndices(size_t row, size_t index) {
		if(row >= cohortsTable.cohortRows_.size() || index >= (size_t)kGCMMPMaxRowCap)
			return NULL;
		GCPCohortsRow* _row = cohortsTable.cohortRows_[row];
		return &_row->cohorts[index];
	}

	void incColIndex(size_t* row, size_t* index) {
		int _col = *index + 1;
		if(_col == kGCMMPMaxRowCap) {
			_col = 0;
			*row += 1;
		}
		*index = (size_t)_col;
	}

};


class GCClassTableManager : public GCHistogramDataManager {
public:
	GCClassTableManager(void);
	GCClassTableManager(GCMMP_HISTOGRAM_MGR_TYPE);
	~GCClassTableManager(){};

	HistogramTable_S classTable_;

	void initHistograms();
	//std::unordered_map<size_t, GCPHistogramRec*> histogramMapTable;

//	SafeMap<size_t, mirror::Class*, std::less<size_t>,
//	gc::accounting::GCAllocator<std::pair<size_t,mirror::Class*>>> histogramMapTable;

	void addObject(size_t, size_t, mirror::Object*);
	GCPHistRecData* addObjectClassPair(mirror::Class* klass,
			mirror::Object* obj);


	void logClassTable(void);
	void gcpDumpManagedData(art::File*, bool);
	void dumpClassHistograms(art::File* dumpFile,
			bool dumpGlobalRec);
	void dumpClassAtomicHistograms(art::File*);
	void calculatePercentiles(void);
	void calculateAtomicPercentiles(void);
	void gcpZeorfyAllAtomicRecords(void);
};


class GCHistogramObjSizesManager : public GCHistogramDataManager {
	size_t totalHistogramSize;
	size_t lastWindowHistSize;
public:
	GCPHistRecData sizeHistograms[GCP_MAX_HISTOGRAM_SIZE];



	GCPHistogramRec histogramTable[GCP_MAX_HISTOGRAM_SIZE];
	GCPHistogramRecAtomic lastWindowHistTable[GCP_MAX_HISTOGRAM_SIZE];


	GCHistogramObjSizesManager(void);
	GCHistogramObjSizesManager(GCMMP_HISTOGRAM_MGR_TYPE);
	~GCHistogramObjSizesManager(){};

	static void GCPRemoveObj(size_t allocatedMemory, mirror::Object* obj);

	void initHistograms(void);
	void addObject(size_t, size_t, mirror::Object*);

//  bool gcpRemoveDataFromHist(GCPHistogramRec*);
//  bool gcpRemoveAtomicDataFromHist(GCPHistogramRecAtomic*);
  void gcpRemoveObject(size_t);
	void calculatePercentiles(void);
	void calculateAtomicPercentiles(void);



  //// methods for dumping and aggrgating /////////
  void gcpAggAtomicHistograms(GCPHistogramRecAtomic* hisTable,
  		GCPHistogramRecAtomic* globalRec);
  void gcpAggregateHistograms(GCPHistogramRec* hisTable,
  		GCPHistogramRec* globalRec);
//  void gcpCalculateEntries(GCPHistogramRec* hisTable,
//  		GCPHistogramRec* globalRec);
//  void gcpCalculateAtomicEntries(GCPHistogramRecAtomic* hisTable,
//  		GCPHistogramRecAtomic* globalRec);

//  bool gcpCheckForResetHist(void);
//  bool gcpCheckForCompleteResetHist(void);

  bool gcpDumpHistTable(art::File*, bool);
  bool gcpDumpHistAtomicTable(art::File*);
  //bool gcpDumpHistAtomicRec(art::File*);



  void gcpResetHistogramData() {
  	gcpResetHistogramRecData(&histRecord);
  	memset((void*)histogramTable, 0, totalHistogramSize);
  	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
  		histogramTable[i].index 			= (i+1) * 1.0;
  	}
  }

  void gcpResetAtomicData() {
  	gcpResetHistogramAtomicRecData(&histAtomicRecord);
  	memset((void*)lastWindowHistTable, 0, lastWindowHistSize);

  	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
  		lastWindowHistTable[i].index  = (i+1) * 1.0;
  	}
  }
};//GCHistogramObjSizesManager



class GCPThreadAllocManager : public GCHistogramDataManager {
public:
	// a global record holder for all histograms
	GCHistogramObjSizesManager* objSizesHist_;
};

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



	volatile bool suspendedGC;

	GCMMP_ProfileActivity lifeTime_;
public:
	GCPauseThreadManager* pauseManager;
	GCHistogramObjSizesManager* histogramManager;
	/* markers used to set the temporary information to start an event */
	GCMMP_ProfileActivity timeBrks[GCMMP_GC_BRK_MAXIMUM];
	static VMProfiler* mProfiler;
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


  bool StopTimeProfiling(void);
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


  void resetPerfRecord(){
  	perf_record_ = NULL;
  }
  // The performance counter record.
  MPPerfCounter* perf_record_;

  MPPerfCounter* GetPerfRecord() {
    return perf_record_;
  }
  void readPerfCounter(int32_t);
  void readPerfCounter(int32_t, uint64_t*, uint64_t*, uint64_t*);
  uint64_t getDataPerfCounter();


  bool isGCThread() {
  	return (tag_ >= GCMMP_THREAD_GCDAEMON);
  }
};



} // namespace mprofiler
} // namespace art

#endif /* MPROFILERTYPES_H_ */
