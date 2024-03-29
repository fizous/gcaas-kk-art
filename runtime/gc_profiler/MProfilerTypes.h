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
#include <list>
#include <string>
#include <vector>
#include <utility>
#include "cutils/system_clock.h"
#include "gc_profiler/MPPerfCounters.h"

#include "gc/accounting/gc_allocator.h"


/* window size of the dumping daya */
#define GCP_WINDOW_RANGE_LOG							16
#define GCP_MUTATIONS_WINDOW_SIZE					5000

/* number of buckets to create the histogram */
//#define GCP_MAX_COHORT_ARRAYLET_CAP				128
#define GCP_MAX_COHORT_ROW_CAP						64
#define GCP_DEFAULT_COHORT_LOG						19
#define GCP_DEAFULT_COHORT_SIZE						524288 // 512k

#define GCP_MAX_HISTOGRAM_SIZE						64
#define GCMMP_GCPAUSE_ARRAY_SIZE					64

namespace art {

//namespace gc {
//
//namespace accounting {
// class GCAllocator;
//}
//
//}
class Thread;

namespace mprofiler {

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
	uint64_t currHSize;
	/* event type */
	GCMMP_ACTIVITY_ENUM evType;
  /* padding*/
  uint32_t padding_;
} EventMarker;


typedef struct EventMarkerArchive_S {
  EventMarker  *markers_;
  struct EventMarkerArchive_S *next_event_bulk_;
} EventMarkerArchive;


/*
 * Container of all the events in the profiler
 */
typedef struct EventMarkerManager_S {
	/* current index of the event being triggered */
	volatile int32_t curr_index_;
	/* pointer to the area of the memory holding all the events */
	EventMarker* markers_; //
	EventMarkerArchive* events_archive_;
	uint32_t archive_cnt_;
} EventMarkerManager;


typedef struct GCPHistogramRecAtomic_S {
	uint64_t   		index;
	AtomicInteger cntLive;
	AtomicInteger cntTotal;
	double pcntLive;
	double pcntTotal;
} GCPHistogramRecAtomic;



//typedef struct GCPHistogramRecAtomicWrapper_S {
//  GCPHistogramRecAtomic atomic_support_;
//  uint64_t cntLive_over_flow_;
//  uint64_t cntTotal_over_flow_;
//} GCPHistogramRecAtomicWrapper;
//
//
//
//class GCPHistRecDataWrapper {
// public:
//  GCPHistogramRecAtomicWrapper recorded_data_;
//  uint64_t get_cnt_total() {
//    return recorded_data_.cntTotal_over_flow_;
//  }
//
//  uint64_t inc_cnt_total(int value) {
//    return recorded_data_.cntTotal_over_flow_ += fetch_add ;
//  }
//};

typedef struct PACKED(4) GCPHistogramRec_S {
	uint64_t   index;
	double cntLive;
	double cntTotal;
	double pcntLive;
	double pcntTotal;
} GCPHistogramRec;


typedef struct PACKED(4) GCPCohortRecordData_S {
	uint64_t index_;
	double objLiveCnt;
	double objTotalCnt;
	double liveSize;
	double totalSize;
} GCPCohortRecordData;



typedef struct PACKED(4) GCPSafeRecData_S {
  int64_t index_;
  uint64_t cntLive_;
  uint64_t cntTotal_;
} GCPSafeRecData;


class SafeGCPHistogramRec {
 public:
  GCPSafeRecData dataRec_;
  Mutex* safe_lock_ DEFAULT_MUTEX_ACQUIRED_AFTER;

  SafeGCPHistogramRec() {
    safe_lock_ = new Mutex("SafeGCPHistogramRec lock");
    memset((void*)&dataRec_, 0, static_cast<int64_t>(sizeof(GCPSafeRecData)));
  }


  void setIndex(int64_t ind) {
    dataRec_.index_ = ind;
  }
  void resetLiveData(Thread* th){
    MutexLock mu(th, *safe_lock_);
    dataRec_.cntLive_ = 0;
  }

  void reset(Thread* th) {
    MutexLock mu(th, *safe_lock_);
    memset((void*)&dataRec_, 0, static_cast<int64_t>(sizeof(GCPSafeRecData)));
  }

  void inc_counts(Thread* th, size_t val) {
    MutexLock mu(th, *safe_lock_);
    dataRec_.cntLive_ += val;
    dataRec_.cntTotal_ += val;
  }

  void inc_counts(Thread* th, size_t val, uint64_t* before_val, uint64_t* after_val) {
    MutexLock mu(th, *safe_lock_);
    *before_val = dataRec_.cntTotal_;
    uint64_t cast_val = static_cast<uint64_t>(val);
    dataRec_.cntLive_ += cast_val;
    dataRec_.cntTotal_ += cast_val;
    *after_val = dataRec_.cntTotal_;
  }

  void dec_counts(Thread* th, size_t val) {
    MutexLock mu(th, *safe_lock_);
    if(static_cast<uint64_t>(val) > (dataRec_.cntLive_)) {
      dataRec_.cntLive_ = 0;
      return;
    }
    dataRec_.cntLive_ -= static_cast<uint64_t>(val);
  }

  void read_counts(Thread* th, uint64_t* total_cnt, uint64_t* live_cnt) {
    MutexLock mu(th, *safe_lock_);
    *total_cnt = dataRec_.cntTotal_;
    *live_cnt = dataRec_.cntLive_;
  }

  uint64_t get_total_count() {
    return dataRec_.cntTotal_;
  }
};





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

	static bool GCPDumpHistRecord(art::File* file, GCPHistogramRec* rec);


	void initDataRecords(uint64_t kIndex) {
		memset((void*)&dataRec_, 0, static_cast<int64_t>(sizeof(GCPHistogramRec)));
		memset((void*)&atomicDataRec_, 0, static_cast<int64_t>(sizeof(GCPHistogramRecAtomic)));
		dataRec_.index = kIndex;
		atomicDataRec_.index = kIndex;
	}



	GCPHistRecData(uint64_t kIndex){
		initDataRecords(kIndex);
	}

	GCPHistRecData(void) { initDataRecords(0);}


	GCPHistogramRec* gcpGetDataRecP(void) {
		return &dataRec_;
	}

	GCPHistogramRecAtomic* gcpGetAtomicDataRecP(void) {
		return &atomicDataRec_;
	}

	bool gcpDumpHistRec(art::File*);

	bool gcpDumpAtomicHistRec(art::File*);



	void gcpUpdateRecPercentile(GCPHistogramRec* rootRec) {
		dataRec_.pcntLive = (dataRec_.cntLive * 100.0) / rootRec->cntLive;
		dataRec_.pcntTotal = (dataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void gcpUnsafeUpdateRecPercentile(GCPHistogramRec* rootRec) {
		dataRec_.pcntLive = (rootRec->cntLive < 1) ? 0.0 :
				(dataRec_.cntLive * 100.0) / rootRec->cntLive;
		dataRec_.pcntTotal = (rootRec->cntTotal < 1) ? 0.0 :
				(dataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void gcpUpdateAtomicRecPercentile(GCPHistogramRecAtomic* rootRec) {
		atomicDataRec_.pcntLive = rootRec->cntLive == 0 ? 0.0 :
				(atomicDataRec_.cntLive * 100.0) / rootRec->cntLive;
		atomicDataRec_.pcntTotal = rootRec->cntTotal == 0 ? 0.0 :
				(atomicDataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void gcpSafeUpdateAtomicRecPercentile(GCPHistogramRecAtomic* rootRec) {
		atomicDataRec_.pcntLive = (atomicDataRec_.cntLive * 100.0) / rootRec->cntLive;
		atomicDataRec_.pcntTotal = (atomicDataRec_.cntTotal * 100.0) / rootRec->cntTotal;
	}

	void GCPZerofyHistAtomicRecData(GCPHistogramRecAtomic* rec) {
		uint64_t _index = rec->index;
	  memset((void*)(rec), 0, static_cast<int64_t>(sizeof(GCPHistogramRecAtomic)));
	  rec->index = _index;
	  rec->cntLive.store(0);
	  rec->cntTotal.store(0);
	}

	void gcpZerofyHistAtomicRecData(void) {
		uint64_t _index = atomicDataRec_.index;
	  memset((void*)(&atomicDataRec_), 0, static_cast<int64_t>(sizeof(GCPHistogramRecAtomic)));
	  atomicDataRec_.index = _index;
	  atomicDataRec_.cntLive.store(0);
	  atomicDataRec_.cntTotal.store(0);
	}

  void gcpZerofyHistRecData(void) {
    dataRec_.cntLive = 0;
    dataRec_.pcntLive = 0;
  }

	void gcpDecRecData(size_t space){
		dataRec_.cntLive -= space;
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

	void gcpIncRecData(size_t space){
		dataRec_.cntLive += space;
		dataRec_.cntTotal += space;
	}

	void gcpIncRecData(void){
		dataRec_.cntLive++;
		dataRec_.cntTotal++;
	}


	static bool GCPDecAtomicRecData(size_t space, GCPHistogramRecAtomic* rec) {
		if(rec->cntLive > (int32_t)space) {
			rec->cntLive.fetch_sub(space);
			return true;
		}
		return false;
	}


	bool gcpDecAtomicRecData(size_t space){
		return GCPDecAtomicRecData(space, &atomicDataRec_);
	}

	static void GCPIncAtomicRecData(GCPHistogramRecAtomic* rec) {
		rec->cntLive++;
		rec->cntTotal++;
	}

	void gcpIncAtomicRecData(void){
		GCPIncAtomicRecData(&atomicDataRec_);
	}

	static void GCPIncAtomicRecData(size_t space, GCPHistogramRecAtomic* rec) {
		rec->cntLive.fetch_add(space);
		rec->cntTotal.fetch_add(space);
	}

	void gcpIncAtomicRecData(size_t space){
		GCPIncAtomicRecData(space, &atomicDataRec_);
	}

};

class GCPPairHistogramRecords : public GCPHistRecData {
private:
	mirror::Class* klzz_;
	std::string referenceName_;
	char * volatile referenceStringName_;
public:
	GCPHistRecData countData_;
	GCPHistRecData sizeData_;

	GCPPairHistogramRecords(void) : klzz_(NULL),
		referenceStringName_(NULL),
		countData_(0), sizeData_(0) {

	}

	GCPPairHistogramRecords(uint64_t id) : klzz_(NULL),
		referenceStringName_(NULL),
		countData_(id), sizeData_(id) {
	}

	GCPPairHistogramRecords(uint64_t id, mirror::Class* klass) :
		klzz_(klass), referenceName_(PrettyClass(klass)),
		referenceStringName_(NULL),
		countData_(id), sizeData_(id) {
	}


	void gcpPairSetRecordIndices(uint64_t kIndex) {
		countData_.initDataRecords(kIndex);
		sizeData_.initDataRecords(kIndex);
	}

	void gcpPairIncRecData(uint64_t space){
		countData_.gcpIncRecData();
		sizeData_.gcpIncRecData(space);
	}

	void gcpPairIncAtomicRecData(size_t space){
		countData_.gcpIncAtomicRecData();
		sizeData_.gcpIncAtomicRecData(space);
	}

	bool gcpPairDecRecData(uint64_t space){
		if(countData_.dataRec_.cntLive > 0) {
			countData_.gcpDecRecData();
			sizeData_.gcpDecRecData(space);
			return true;
		}
		return false;
	}

	bool gcpPairDecAtomicRecData(size_t space) {
		if(sizeData_.atomicDataRec_.cntLive.load() > (int32_t)space) {
			if(countData_.atomicDataRec_.cntLive.load() > 0) {
				if(sizeData_.gcpDecAtomicRecData(space)) {
					countData_.gcpDecAtomicRecData();
					return true;
				}
			}
		}
		return false;
	}

	void gcpPairUpdatePercentiles(GCPPairHistogramRecords* globalRec) {

		countData_.gcpUnsafeUpdateRecPercentile(globalRec->countData_.gcpGetDataRecP());
		sizeData_.gcpUnsafeUpdateRecPercentile(globalRec->sizeData_.gcpGetDataRecP());
	}

	void gcpPairUpdateAtomicPercentiles(GCPPairHistogramRecords* globalRec) {
		countData_.gcpUpdateAtomicRecPercentile(globalRec->countData_.gcpGetAtomicDataRecP());
		sizeData_.gcpUpdateAtomicRecPercentile(globalRec->sizeData_.gcpGetAtomicDataRecP());
	}

	void gcpZerofyPairHistAtomicRecData(void) {
		countData_.gcpZerofyHistAtomicRecData();
		sizeData_.gcpZerofyHistAtomicRecData();
	}

  void gcpZerofyPairHistRecData(void) {
    countData_.gcpZerofyHistRecData();
    sizeData_.gcpZerofyHistRecData();
  }

	mirror::Class* getClassP(){return klzz_;}
	std::string& getRefrenecePrettyName(){return referenceName_;}
	void getReferenceStringName(char** charP) volatile {*charP = referenceStringName_;}
	void setRefreneceNameFromThread(pid_t);
};

//typedef std::multimap<size_t, mprofiler::GCPHistogramRec*> HistogramTable_S;
//typedef std::multimap<size_t, size_t> HistogramTableTest_S;


typedef struct PACKED(4) GCPExtraObjHeader_S {
  uint64_t objSize;
	union {
		GCHistogramDataManager* histRecP;
		GCPHistRecData* dataRec;
		uint64_t objBD;
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
	static size_t kGCMMPCohortSize;

	//static AtomicInteger GCPTotalMutationsCount;
  static SafeGCPHistogramRec  gcpTotalMutationsCount_;

	uint64_t GCPGetLastManagedCohort() {
		return static_cast<uint64_t>(kGCPLastCohortIndex.load());
	}

	void GCPSetLastManagedCohort(int32_t newIndex) {
		kGCPLastCohortIndex.store(newIndex);
	}

	GCMMP_HISTOGRAM_MGR_TYPE type_;
	GCPHistRecData*				histData_;

	GCPHistogramRec				histRecord_;
	GCPHistogramRecAtomic histAtomicRecord;
	//secretNumber used to check if this manager is included;
	int iSecret;

	GCHistogramDataManager* parentManager_;


	GCHistogramDataManager(void);
	GCHistogramDataManager(bool, GCHistogramDataManager*);
	GCHistogramDataManager(GCMMP_HISTOGRAM_MGR_TYPE);
	GCHistogramDataManager(bool);

	void initManager(GCHistogramDataManager*, bool);

	virtual ~GCHistogramDataManager() {}

	static size_t AddMProfilingExtraBytes(size_t);
	static size_t removeMProfilingExtraBytes(size_t);
	static void GCPInitObjectProfileHeader(size_t allocatedMemory,
			mirror::Object* obj);

	static int GetExtraProfileBytes(void) {
		return GCHistogramDataManager::kGCMMPHeaderSize;
	}

	static void GCPIncMutations(Thread* thread) {
	    gcpTotalMutationsCount_.inc_counts(thread, 1);
//		GCPTotalMutationsCount++;
	}

  static GCPExtraObjHeader* GCPGetObjProfHeader(size_t allocatedMemory,
  		mirror::Object* obj) {
  	byte* address = reinterpret_cast<byte*>(reinterpret_cast<byte*>(obj) +
  			allocatedMemory - sizeof(GCPExtraObjHeader));
  	GCPExtraObjHeader* extraHeader =
  			reinterpret_cast<GCPExtraObjHeader*>(address);
  	return extraHeader;
  }

  static void GCPUpdateCohortSize() {
  	kGCMMPCohortSize = (size_t)  (1 << kGCMMPCohortLog);
  }
  virtual void initHistograms(void) {}

  virtual void addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) = 0;
  virtual void addObjectFast(size_t,size_t);


  virtual uint64_t removeObject(size_t, mirror::Object*) {return 0;}

  virtual void logManagedData(void) {}
  virtual bool gcpDumpHistRec(art::File*);

  virtual bool gcpDumpManagedData(art::File*, bool) {return true;}
  virtual bool gcpDumpSummaryManagedData(art::File*) {return true;}

  virtual void gcpZeorfyAllAtomicRecords(void) {}

  virtual void gcpSetRecordIndices(uint64_t ind) {
  	histData_->atomicDataRec_.index = ind;
  	histData_->dataRec_.index = ind;
  }

  virtual void gcpSetPairRecordIndices(uint64_t ind) {
  	GCPPairHistogramRecords* _rec =
  			(GCPPairHistogramRecords*) histData_;
  	_rec->gcpPairSetRecordIndices(ind);
  }

  virtual void gcpNoAggAddDataToHist(GCPHistRecData* dataRec) {
  	dataRec->gcpIncAtomicRecData();
  	dataRec->gcpIncRecData();
		histData_->gcpIncAtomicRecData();
		histData_->gcpIncRecData();
  }

  virtual void gcpNoAggAddSingleDataToHist(GCPHistRecData* dataRec) {
  	dataRec->gcpIncAtomicRecData();
  	dataRec->gcpIncRecData();
  }

  virtual void gcpNoAggAddSingleDataToPairHist(size_t space,
  		GCPHistRecData* dataRec) {
  	GCPPairHistogramRecords* _pairRec = (GCPPairHistogramRecords*) dataRec;
  	_pairRec->gcpPairIncAtomicRecData(space);
  	_pairRec->gcpPairIncRecData(space);
  }

  virtual void gcpAggRemoveDataFromHist(GCPHistRecData* dataRec) {
  	dataRec->gcpDecRecData();
  	bool _remFlag = dataRec->gcpDecAtomicRecData();
  	GCHistogramDataManager* _managerIter = this;
  	while(_managerIter != NULL) {
  		_managerIter->histData_->gcpDecRecData();
  		if(_remFlag)
  			_managerIter->histData_->gcpDecAtomicRecData();
  		_managerIter = _managerIter->parentManager_;
  	}
  }

  virtual void gcpNoAggRemoveDataFromHist(GCPHistRecData* dataRec) {
  	dataRec->gcpDecRecData();
  	histData_->gcpDecRecData();
  	bool _remFlag = dataRec->gcpDecAtomicRecData();
		if(_remFlag)
			histData_->gcpDecAtomicRecData();
  }

  virtual void gcpAggAddDataToHist(GCPHistRecData* dataRec) {
  	dataRec->gcpIncAtomicRecData();
  	dataRec->gcpIncRecData();
  	//upate the global records till the root
  	GCHistogramDataManager* _managerIter = this;
  	while(_managerIter != NULL) {
  		_managerIter->histData_->gcpIncAtomicRecData();
  		_managerIter->histData_->gcpIncRecData();
  		_managerIter = _managerIter->parentManager_;
  	}
  }
  virtual void gcpIncPairRecData(size_t space, GCPHistRecData* rec) {
  	GCPPairHistogramRecords* _globalRec =
  			(GCPPairHistogramRecords*) histData_;
  	GCPPairHistogramRecords* _localRec =
  			(GCPPairHistogramRecords*) rec;
  	_localRec->gcpPairIncRecData(space);
  	_globalRec->gcpPairIncRecData(space);
  }

  virtual void gcpAggIncPairRecData(size_t space, GCPHistRecData* rec) {
//  	GCPPairHistogramRecords* _localRec =
//  			(GCPPairHistogramRecords*) rec;
  	gcpIncPairRecData(space, rec);
  	//upate the global records till the root
  	GCHistogramDataManager* _managerIter = parentManager_;
  	while(_managerIter != NULL) {
    	GCPPairHistogramRecords* _globalRec =
    			(GCPPairHistogramRecords*) _managerIter->histData_;
    	_globalRec->gcpPairIncRecData(space);
  		_managerIter = _managerIter->parentManager_;
  	}
  }

  virtual void gcpIncAtomicPairRecData(size_t space, GCPHistRecData* rec) {
  	GCPPairHistogramRecords* _globalRec =
  			(GCPPairHistogramRecords*) histData_;
  	GCPPairHistogramRecords* _localRec =
  			(GCPPairHistogramRecords*) rec;
  	_localRec->gcpPairIncAtomicRecData(space);
  	_globalRec->gcpPairIncAtomicRecData(space);
  }

  virtual void gcpAggIncAtomicPairRecData(size_t space, GCPHistRecData* rec) {
  	gcpIncAtomicPairRecData(space, rec);
  	//upate the global records till the root
  	GCHistogramDataManager* _managerIter = parentManager_;
  	while(_managerIter != NULL) {
    	GCPPairHistogramRecords* _globalRec =
    			(GCPPairHistogramRecords*) _managerIter->histData_;
    	_globalRec->gcpPairIncAtomicRecData(space);
  		_managerIter = _managerIter->parentManager_;
  	}
  }


  virtual bool gcpAggDecPairRecData(size_t space, GCPHistRecData* rec) {
  	bool _remFlag = gcpDecPairRecData(space, rec);
  	if(_remFlag) {
    	GCHistogramDataManager* _managerIter = parentManager_;
    	while(_managerIter != NULL) {
    		GCPPairHistogramRecords* pRec =
    				(GCPPairHistogramRecords*)parentManager_->histData_;
    		if(!pRec->gcpPairDecRecData(space))
    			break;
    		_managerIter = _managerIter->parentManager_;
    	}
  	}
  	return _remFlag;
  }

  virtual bool gcpDecPairRecData(size_t space, GCPHistRecData* rec) {
  	GCPPairHistogramRecords* _globalRec =
  			(GCPPairHistogramRecords*) histData_;
  	GCPPairHistogramRecords* _localRec =
  			(GCPPairHistogramRecords*) rec;
  	if(_localRec->gcpPairDecRecData(space)) {
  		_globalRec->gcpPairDecRecData(space);
  		return true;
  	}
  	return false;

  }

  virtual bool gcpDecAtomicPairRecData(size_t space, GCPHistRecData* rec) {
  	GCPPairHistogramRecords* _globalRec =
  			(GCPPairHistogramRecords*) histData_;
  	GCPPairHistogramRecords* _localRec =
  			(GCPPairHistogramRecords*) rec;
  	if(_localRec->gcpPairDecAtomicRecData(space)) {
  		_globalRec->gcpPairDecAtomicRecData(space);
  		return true;
  	}
  	return false;
  }

  virtual bool gcpAggDecAtomicPairRecDat(size_t space, GCPHistRecData* dataRec) {
  	bool _remFlag = gcpDecAtomicPairRecData(space, dataRec);
  	//bool _remFlag = dataRec->gcpDecAtomicPairRecData();
  	GCHistogramDataManager* _managerIter = parentManager_;
  	while(_managerIter != NULL) {
  		GCPPairHistogramRecords* pRec =
  				(GCPPairHistogramRecords*)parentManager_->histData_;
  		if(!pRec->gcpDecAtomicRecData())
  			break;
  		_managerIter = _managerIter->parentManager_;
  	}
  	return _remFlag;
  }

  virtual void calculateAtomicPercentiles(void) {}
  virtual void calculatePercentiles(void) {}
  virtual bool gcpDumpHistTable(art::File*, bool){return true;}
  virtual bool gcpDumpHistAtomicTable(art::File*){return true;}
	virtual void gcpFinalizeProfileCycle(void){}
//  void gcpRemoveDataToHist(GCPHistogramRec*);

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
  	memset((void*)(rec), 0, static_cast<int64_t>(sizeof(GCPHistogramRec)));
  	rec->pcntLive = 100.0;
  	rec->pcntTotal = 100.0;
  }

  void gcpResetHistogramAtomicRecData(GCPHistogramRecAtomic* rec) {
  	memset((void*)(rec), 0, static_cast<int64_t>(sizeof(GCPHistogramRecAtomic)));
  	rec->pcntLive = 100.0;
  	rec->pcntTotal = 100.0;
  }

  void gcpZerofyHistogramAtomicRecData(GCPHistogramRecAtomic* rec) {
  	uint64_t _index = rec->index;
  	memset((void*)(rec), 0, static_cast<int64_t>(sizeof(GCPHistogramRecAtomic)));
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

	void gcpLogDataRecord(std::ostream& os) {
		GCPHistogramRec* _dataRec = &histData_->dataRec_;
		os << "index: "<< StringPrintf("%llu",_dataRec->index) <<
				";cntLive: " << _dataRec->cntLive <<
				"; cntTotal: "<< _dataRec->cntTotal<< "; pcntLive: " <<
				_dataRec->pcntLive << "; pcntTotal: " << _dataRec->pcntTotal;
	}

	void gcpLogDataRecord(std::ostream& os, GCPHistogramRec* _dataRec) {
		os << "index: "<< StringPrintf("%llu",_dataRec->index) << "; cntLive: " << _dataRec->cntLive <<
				"; cntTotal: "<< _dataRec->cntTotal<< "; pcntLive: " <<
				_dataRec->pcntLive << "; pcntTotal: " << _dataRec->pcntTotal;
	}

	void gcpLogDataRecord(std::ostream& os, GCPCohortRecordData* _dataRec) {
		os << "index: "<< StringPrintf("%llu",_dataRec->index_) <<
				"; liveSize: " << _dataRec->liveSize <<
				"; totalSize: "<< _dataRec->totalSize <<
				"; liveObj: " << _dataRec->objLiveCnt <<
				"; totalObj: " << _dataRec->objTotalCnt;
	}

	virtual bool gcpDumpCSVGlobalDataSummary(std::ostringstream&) {return true;}
	virtual bool gcpDumpCSVCoreTables(std::ostringstream&) {return true;}

	virtual bool gcpDumpCSVData(std::ostringstream& outputStream) {
		bool _success = gcpDumpCSVGlobalDataSummary(outputStream);
		_success = gcpDumpCSVCoreTables(outputStream);
		return _success;
	}

};//GCHistogramDataManager


class GCCohortManager : public GCHistogramDataManager {
protected:
	size_t cohRowSZ_;
//	size_t cohArrSZ_;
//
	size_t getCoRowSZ(void) {
		return cohRowSZ_ + sizeof(int);
	}

	uint64_t calcObjBD(uint64_t objSize) {
		return allocRec_->get_total_count() - objSize;
	}

	uint64_t calcObjLifeTime(uint64_t objBD) {
		return allocRec_->get_total_count() - objBD;
	}

	size_t getSpaceLeftCohort(GCPCohortRecordData* rec) {
		return kGCMMPCohortSize - rec->totalSize;
	}

	int getCohortsCount(void);


	bool gcpDumpSizeHistAtomicLifeTable(art::File*,
			bool);
	bool gcpDumpCntHistAtomicLifeTable(art::File*,
			bool);
	bool gcpDumpSizeHistLifeTable(art::File*,
			bool);
	bool gcpDumpCntHistLifeTable(art::File*,
			bool);
public:
	static constexpr int kGCMMPMaxRowCap 		= GCP_MAX_COHORT_ROW_CAP;
	//static constexpr int kGCMMPMaxTableCap 	= GCP_MAX_COHORT_ARRAYLET_CAP;

	GCPCohortRecordData*	currCohortP;
	GCPCohortsRow*    		currCoRowP;
	GCPCohortsTable 			cohortsTable_;
	SafeGCPHistogramRec* 	allocRec_;

	GCPPairHistogramRecords lifeTimeHistograms_[kGCMMPMaxHistogramEntries];

	GCCohortManager(SafeGCPHistogramRec*);
	void initHistograms(void);

	void addObject(size_t, size_t, mirror::Object*);
	bool gcpDumpManagedData(art::File*, bool);
	void logManagedData(void);
	void addCohortRecord(void);
	void addCohortRow(void);

	void addObjectToCohRecord(size_t objSize);

  void gcpDumpCohortData(art::File*);
	GCPCohortRecordData* getCoRecFromObj(size_t allocSpace, mirror::Object* obj);
	uint64_t removeObject(size_t allocSpace, mirror::Object* obj);
	void gcpFinalizeProfileCycle(void);
	void gcpZeorfyAllAtomicRecords(void);
	uint64_t calcNewCohortIndex();
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
			uint64_t bd, uint64_t objSize) {
		*startIndex = static_cast<size_t>(bd >> kGCMMPCohortLog);
		*startRow = *startIndex /  kGCMMPMaxRowCap;
		*startIndex = (*startIndex) % kGCMMPMaxRowCap;

		*endIndex = static_cast<size_t>( (bd + objSize) >> kGCMMPCohortLog);
		*endRow = *endIndex /  kGCMMPMaxRowCap;
		*endIndex = (*endIndex) % kGCMMPMaxRowCap;
	}

	GCPCohortRecordData* getCoRecFromIndices(size_t row, size_t index) {
		if(row >= cohortsTable_.cohortRows_.size() || index >= (size_t)kGCMMPMaxRowCap)
			return NULL;
		GCPCohortsRow* _row = cohortsTable_.cohortRows_[row];
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


class GCPDistanceRecord {
public:
	double index_;
	AtomicInteger live_;
	AtomicInteger total_;

	void resetLiveData(){
		live_.store(0);
	}
};

typedef struct PACKED(4) GCPDistanceRecDisplay_S {
	double index_;
	uint64_t live_;
	uint64_t total_;
	uint64_t padding_;
}  GCPDistanceRecDisplay;

class GCRefDistanceManager : public GCCohortManager {
protected:
	void initDistanceArray(void);

	void copyToDisplayRecord(Thread* thread, GCPDistanceRecDisplay* dist,
	                         SafeGCPHistogramRec* src) {
		dist->index_ = static_cast<double>(src->dataRec_.index_);
		src->read_counts(thread, &dist->total_, &dist->live_);
		dist->padding_ = 0;
	}
	void copyArrayForDisplay(Thread* thread, SafeGCPHistogramRec* arrRefs) {
		for(int iter = 0; iter < kGCMMPMaxHistogramEntries; iter++) {
			copyToDisplayRecord(thread, &arrayDisplay_[iter], &arrRefs[iter]);
		}
	}

	void logRecDisplay(GCPDistanceRecDisplay* recDisplay) {
		LOG(ERROR) << StringPrintf("index:%f; live:%llu; total:%llu",
		                                       recDisplay->index_,
		                                       recDisplay->live_, recDisplay->total_);
	}

public:
	static uint64_t kGCMMPMutationWindowSize;
	SafeGCPHistogramRec posRefDist_[kGCMMPMaxHistogramEntries];
	SafeGCPHistogramRec negRefDist_[kGCMMPMaxHistogramEntries];


//	GCPDistanceRecord posRefDist_[kGCMMPMaxHistogramEntries];
//	GCPDistanceRecord negRefDist_[kGCMMPMaxHistogramEntries];

	GCPDistanceRecDisplay arrayDisplay_[kGCMMPMaxHistogramEntries];
//	GCPDistanceRecord selReferenceStats_;
//	GCPDistanceRecord mutationStats_;

	SafeGCPHistogramRec selReferenceStats_;
	SafeGCPHistogramRec mutationStats_;

	GCRefDistanceManager(SafeGCPHistogramRec*);

	void resetCurrentCounters();

	void profileDistance(const mirror::Object*,
			uint32_t, const mirror::Object*);

	void gcpFinalizeProfileCycle(void);

	void logManagedData(void);

	bool gcpDumpManagedData(art::File*, bool);
	bool gcpDumpHistTable(art::File*, bool);
};


class GCClassTableManager : public GCHistogramDataManager {
private:
	void printClassNames(void);
public:


	GCPHistRecData* addObjectClassPair(mirror::Class* klass,
				mirror::Object* obj);

	GCClassTableManager(void);
	GCClassTableManager(GCMMP_HISTOGRAM_MGR_TYPE);
	~GCClassTableManager(){};

	void initHistograms();

	void addObject(size_t, size_t, mirror::Object*);
	uint64_t removeObject(size_t, mirror::Object*);
	//std::unordered_map<size_t, GCPHistogramRec*> histogramMapTable;

//	SafeMap<size_t, mirror::Class*, std::less<size_t>,
//	gc::accounting::GCAllocator<std::pair<size_t,mirror::Class*>>> histogramMapTable;

	void logManagedData(void);

	bool gcpDumpManagedData(art::File*, bool);
	bool gcpDumpSummaryManagedData(art::File*);
	bool dumpClassCntHistograms(art::File* dumpFile,
			bool dumpGlobalRec);
	void gcpFinalizeProfileCycle(void);
	bool dumpClassSizeHistograms(art::File* dumpFile,
			bool dumpGlobalRec);
	bool dumpClassAtomicCntHistograms(art::File*);
	bool dumpClassAtomicSizeHistograms(art::File*);

	void calculatePercentiles(void);
	void calculateAtomicPercentiles(void);
	void gcpZeorfyAllAtomicRecords(void);

	bool gcpDumpCSVGlobalDataSummary(std::ostringstream&);
	bool gcpDumpCSVCoreTables(std::ostringstream&);
};


class GCHistogramObjSizesManager : public GCHistogramDataManager {
//	size_t totalHistogramSize;
//	size_t lastWindowHistSize;
public:
	GCPPairHistogramRecords sizeHistograms_[kGCMMPMaxHistogramEntries];
	GCHistogramObjSizesManager(void);
	GCHistogramObjSizesManager(bool, GCHistogramDataManager*);
	GCHistogramObjSizesManager(GCMMP_HISTOGRAM_MGR_TYPE);
	~GCHistogramObjSizesManager(){};



	//GCPHistRecData sizeHistograms[GCP_MAX_HISTOGRAM_SIZE];



//	GCPHistogramRec histogramTable[GCP_MAX_HISTOGRAM_SIZE];
//	GCPHistogramRecAtomic lastWindowHistTable[GCP_MAX_HISTOGRAM_SIZE];




	//static void GCPRemoveObj(size_t allocatedMemory, mirror::Object* obj);

	void initHistograms(void);
	void addObject(size_t, size_t, mirror::Object*);


//  bool gcpRemoveDataFromHist(GCPHistogramRec*);
//  bool gcpRemoveAtomicDataFromHist(GCPHistogramRecAtomic*);
	uint64_t removeObject(size_t, mirror::Object*);
	void gcpRemoveObjectFromIndex(size_t, size_t, bool);
	void gcpRemoveObjFromEntriesWIndex(size_t, size_t);

	void calculatePercentiles(void);
	void calculateAtomicPercentiles(void);



  //// methods for dumping and aggrgating /////////
//  void gcpAggAtomicHistograms(GCPHistogramRecAtomic* hisTable,
//  		GCPHistogramRecAtomic* globalRec);
//  void gcpAggregateHistograms(GCPHistogramRec* hisTable,
//  		GCPHistogramRec* globalRec);
//  void gcpCalculateEntries(GCPHistogramRec* hisTable,
//  		GCPHistogramRec* globalRec);
//  void gcpCalculateAtomicEntries(GCPHistogramRecAtomic* hisTable,
//  		GCPHistogramRecAtomic* globalRec);

//  bool gcpCheckForResetHist(void);
//  bool gcpCheckForCompleteResetHist(void);

  virtual bool gcpDumpHistTable(art::File*, bool);
  bool gcpDumpHistAtomicTable(art::File*);
  bool gcpDumpHistAtomicSpaceTable(art::File*);
	virtual bool gcpDumpHistSpaceTable(art::File*, bool);
	void logManagedData(void);
	bool gcpDumpCSVGlobalDataSummary(std::ostringstream&);
	bool gcpDumpCSVCoreTables(std::ostringstream&);

	bool gcpDumpManagedData(art::File*, bool);
  bool gcpDumpSummaryManagedData(art::File*);

  //bool gcpDumpHistAtomicRec(art::File*);

  void gcpFinalizeProfileCycle(void);
  void gcpZeorfyAllAtomicRecords(void);

//  void gcpResetHistogramData() {
//  	gcpResetHistogramRecData(&histRecord);
//  	memset((void*)histogramTable, 0, totalHistogramSize);
//  	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
//  		histogramTable[i].index 			= (i+1) * 1.0;
//  	}
//  }

//  void gcpResetAtomicData() {
//  	gcpResetHistogramAtomicRecData(&histAtomicRecord);
//  	memset((void*)lastWindowHistTable, 0, lastWindowHistSize);
//
//  	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
//  		lastWindowHistTable[i].index  = (i+1) * 1.0;
//  	}
//  }
};//GCHistogramObjSizesManager

//typedef std::list<GCMMPThreadProf*>& ThreadProfList_S;


class GCHistogramFragmentsManager: public GCHistogramObjSizesManager {
public :
  GCPPairHistogramRecords*       usedBytesData_;
  GCHistogramFragmentsManager(void);
  ~GCHistogramFragmentsManager(){}
  void gcpFinalizeProfileCycle(void);
  void gcpZeorfyAllRecords(void);
  void addObjectFast(size_t,size_t);
  bool gcpDumpHistTable(art::File*, bool);
  bool gcpDumpHistSpaceTable(art::File*, bool);
};

class GCPThreadAllocManager : public GCHistogramDataManager {
protected:
	bool gcpDumpThreadHistogramCSVData(std::ostringstream&);
public:
	// a global record holder for all histograms
	GCHistogramObjSizesManager* objSizesHistMgr_;

	GCPThreadAllocManager(void);


	void gcpFinalizeProfileCycle(void);
	/* overriden methods */
	void initHistograms();
	void addObject(size_t, size_t, mirror::Object*);
	uint64_t removeObject(size_t, mirror::Object*);
	void addObjectForThread(size_t, size_t, mirror::Object*, GCMMPThreadProf*);
	void setThreadManager(GCMMPThreadProf*);
	bool dettachThreadFromManager(GCMMPThreadProf*);
	void gcpZeorfyAllAtomicRecords(void);
	void calculatePercentiles(void);
	void calculateAtomicPercentiles(void);

	bool gcpDumpManagedData(art::File*, bool);
	bool gcpDumpSummaryManagedData(art::File*);
	bool gcpDumpHistTable(art::File*, bool);
	bool gcpDumpHistAtomicTable(art::File*);
	bool gcpDumpHistAtomicSpaceTable(art::File*);
	bool gcpDumpHistSpaceTable(art::File*, bool);
	void logManagedData(void);

	bool gcpDumpCSVGlobalDataSummary(std::ostringstream&);
	bool gcpDumpCSVCoreTables(std::ostringstream&);
	bool gcpDumpCSVData(std::ostringstream&);
};//GCPThreadAllocManager









class PACKED(4) GCPauseThreadManager {
	 GCPauseThreadMarker* curr_marker_;
	 GCPauseThreadMarker* pauseEvents[GCMMP_GCPAUSE_ARRAY_SIZE];
	 int curr_bucket_ind_;
	 int curr_entry_;
	 int ev_count_;
	 int busy_;
	 int count_opens_;
public:
	 static constexpr int kGCMMPMaxEventEntries = 64;
	 static constexpr int kGCMMPMaxBucketEntries = GCMMP_GCPAUSE_ARRAY_SIZE;
	 static uint64_t startRealTime;
	 static uint64_t startCPUTime;

	 static uint64_t GetRelevantCPUTime(void);

	 static uint64_t GetRelevantRealTime(void);

	 GCPauseThreadManager(void) :
	   curr_marker_(NULL), curr_bucket_ind_(-1), curr_entry_(-1), ev_count_(0), busy_(0), count_opens_(0) {
	   //UpdateCurrentEntry();
	 }

	 ~GCPauseThreadManager(void);

	 void InitPausesEntry(GCPauseThreadMarker** entryPointer) {

			*entryPointer =
					reinterpret_cast<GCPauseThreadMarker*>(calloc(kGCMMPMaxEventEntries,
					                                              static_cast<int64_t>(sizeof(GCPauseThreadMarker))));
		} //InitPausesEntry

	 void IncrementIndices(void) {
			ev_count_++;
			busy_--;
		//	GCMMP_VLOG(INFO) << "MPRofiler: Incremented Indices " << ev_count_ << ", " << curr_entry_ << ", " << curr_bucket_ind_;
	 } //IncrementIndices

	 void UpdateCurrentEntry() {

	   if(curr_marker_ == NULL) {
	     curr_entry_ = 0;
	     curr_bucket_ind_ = 0;
	     InitPausesEntry(&pauseEvents[curr_bucket_ind_]);
	   } else {
	     curr_entry_ = (curr_entry_ + 1) % kGCMMPMaxEventEntries;
	      if(curr_entry_ == 0) {
	        curr_bucket_ind_++;
	        if(curr_bucket_ind_ >= kGCMMPMaxBucketEntries) {
	          LOG(ERROR) << "MProfiler: Exceeded maximum count of entries ";
	        }
	    //    GCMMP_VLOG(INFO) << "MPRofiler: Initializing entry for the manager " << curr_bucket_ind_ << ", " << curr_entry_;
	        InitPausesEntry(&pauseEvents[curr_bucket_ind_]);
	      }
	   }
     curr_marker_ = &(pauseEvents[curr_bucket_ind_][curr_entry_]);
	 }

	 GCPauseThreadMarker* RetrieveLastOpened(GCMMP_BREAK_DOWN_ENUM evtType) {
	   int _currentBucketIn = curr_bucket_ind_;
	   int _currentEntry = curr_entry_;
	   while(_currentBucketIn >= 0) {
	     while(_currentEntry >= 0) {
	       GCPauseThreadMarker* _marker = &(pauseEvents[_currentBucketIn][_currentEntry]);
	       if(_marker->finalMarker == 0 && _marker->type == evtType) {
	         return _marker;
	       }
	       _currentEntry--;
	     }
	     _currentEntry = kGCMMPMaxEventEntries - 1;
	     _currentBucketIn--;
	   }
	   return NULL;
	 }

	 void MarkStartTimeEvent(GCMMP_BREAK_DOWN_ENUM);
	 void MarkEndTimeEvent(GCMMP_BREAK_DOWN_ENUM);
	 bool HasData(void) const {
		 return (ev_count_ > 0 || count_opens_ > 0);
	 }
	 void DumpProfData(void* args);

	 void calculateAtomicPercentiles(void);
	 void calculatePercentiles(void);


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
	GCHistogramDataManager* histogramManager_;
	/* markers used to set the temporary information to start an event */
	//GCMMP_ProfileActivity timeBrks[GCMMP_GC_BRK_MAXIMUM];
	static VMProfiler* vmProfiler;
	volatile GCMMPThreadProfState state;
	GCMMPThProfileTag tag_;
	GCMMPThreadProf(VMProfiler*, Thread*);
#if 0
	GCMMPThreadProf(MProfiler*, Thread*);
#endif
	~GCMMPThreadProf(void);

  void Destroy(VMProfiler*);

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
  void readPerfCounter(uint64_t);
  void readPerfCounter(uint64_t, uint64_t*, uint64_t*, uint64_t*);
  uint64_t getDataPerfCounter();


  bool isGCThread() {
  	return (tag_ >= GCMMP_THREAD_GCDAEMON);
  }

  bool isEvent(const char* source) const {
    return (strcmp(source, perf_record_->event_name_) == 0);
  }
};



} // namespace mprofiler
} // namespace art

#endif /* MPROFILERTYPES_H_ */
