/*
 * MProfilerTypes.cc
 *
 *  Created on: Jul 16, 2015
 *      Author: hussein
 */


#include "locks.h"
#include "base/mutex.h"
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
#include "intern_table.h"
#include "runtime.h"
#include "ScopedLocalRef.h"
#include "scoped_thread_state_change.h"
#include "thread_list.h"
#include "thread_state.h"
#include "thread.h"
#include "utils.h"

namespace art {
namespace mprofiler {


SafeGCPHistogramRec GCHistogramDataManager::gcpTotalMutationsCount_;
AtomicInteger GCHistogramDataManager::kGCPLastCohortIndex;
int GCHistogramDataManager::kGCMMPCohortLog = VMProfiler::kGCMMPDefaultCohortLog;
int GCHistogramDataManager::kGCMMPHeaderSize = sizeof(GCPExtraObjHeader);
size_t GCHistogramDataManager::kGCMMPCohortSize =
		(size_t) (1 << GCHistogramDataManager::kGCMMPCohortLog);

/**************************** GCPHistRecData *********************************/
bool GCPHistRecData::GCPDumpHistRecord(art::File* file, GCPHistogramRec* rec) {
	return file->WriteFully(rec, static_cast<int64_t>(sizeof(GCPHistogramRec)));
}

inline bool GCPHistRecData::gcpDumpHistRec(art::File* file) {
	return file->WriteFully(&dataRec_, static_cast<int64_t>(sizeof(GCPHistogramRec)));
}

inline bool GCPHistRecData::gcpDumpAtomicHistRec(art::File* file) {
	GCPHistogramRec _dummyRec;
	GCPCopyRecordsData(&_dummyRec, &atomicDataRec_);
	return file->WriteFully(&_dummyRec, static_cast<int64_t>(sizeof(GCPHistogramRec)));
}

inline void GCPPairHistogramRecords::setRefreneceNameFromThread(
		pid_t pid) {
	std::string _tempName (GetThreadName(pid));
	referenceStringName_ = new char [_tempName.length()+1];
	std::strcpy (referenceStringName_, _tempName.c_str());
//	LOG(ERROR) << "+++setting name for pid: "<< pid << ": " <<
//			referenceStringName_ <<
//			", sysName:" << GetThreadName(pid);
}

/********************* GCHistogramManager profiling ****************/
void GCHistogramObjSizesManager::initHistograms(void) {
//	totalHistogramSize = kGCMMPMaxHistogramEntries * sizeof(GCPHistogramRec);
//	lastWindowHistSize = kGCMMPMaxHistogramEntries * sizeof(GCPHistogramRecAtomic);

	/* initialize the global record */
	histData_ = new GCPPairHistogramRecords(0);
	/* initialize the histogram tables */
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		sizeHistograms_[i].gcpPairSetRecordIndices(static_cast<uint64_t>(i+1));
	}
//	gcpResetHistogramData();
//	gcpResetAtomicData();

}



GCHistogramObjSizesManager::GCHistogramObjSizesManager(void) :
		GCHistogramDataManager(false) {
	initHistograms();
}

GCHistogramObjSizesManager::GCHistogramObjSizesManager(GCMMP_HISTOGRAM_MGR_TYPE hisMGR) :
		GCHistogramDataManager(hisMGR) {

}

GCHistogramObjSizesManager::GCHistogramObjSizesManager(bool shouldInitHist,
		GCHistogramDataManager* parentManager) :
				GCHistogramDataManager(false, parentManager) {
	if(shouldInitHist)
		initHistograms();

}

void GCHistogramObjSizesManager::gcpZeorfyAllAtomicRecords(void) {
	((GCPPairHistogramRecords*)histData_)->gcpZerofyPairHistAtomicRecData();
	for (int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		sizeHistograms_[i].gcpZerofyPairHistAtomicRecData();
	}
}

void GCHistogramObjSizesManager::gcpFinalizeProfileCycle(void) {

  uint64_t _newCohortIndex = VMProfiler::GCPCalcCohortIndex();
  if(_newCohortIndex != GCPGetLastManagedCohort()) {
  	gcpZeorfyAllAtomicRecords();
  	GCPSetLastManagedCohort(_newCohortIndex);
  }
	//GCHistogramDataManager::kGCPLastCohortIndex.store(GCPGetCalcCohortIndex());
	//we are relaxed we do not need to lookup for the whole records
	//getObjHistograms()->gcpCheckForResetHist();
}

//inline bool GCHistogramObjSizesManager::gcpRemoveAtomicDataFromHist(GCPHistogramRecAtomic* rec) {
//	bool modified = false;
//	if(rec->cntLive.load() > 0) {
//		rec->cntLive--;
//		modified = true;
//	}
//  return modified;
//}

//bool GCHistogramObjSizesManager::gcpRemoveDataFromHist(GCPHistogramRec* rec) {
//	bool modified = false;
////	if(true)
////		return false;
//	if (rec->cntLive >= 1.0) {
//		rec->cntLive--;
//		modified = true;
//	}
//  return modified;
//}


inline void GCHistogramObjSizesManager::addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	GCPExtraObjHeader* _extraHeader =
			GCHistogramDataManager::GCPGetObjProfHeader(allocatedMemory, obj);
	_extraHeader->objSize = static_cast<uint64_t>(objSize);
	_extraHeader->histRecP = this;
	size_t histIndex = (32 - CLZ(objSize)) - 1;

//	int32_t _readCohortIndex = (GCHistogramDataManager::kGCPLastCohortIndex.load());

	gcpAggIncPairRecData(_extraHeader->objSize, &sizeHistograms_[histIndex]);
	gcpAggIncAtomicPairRecData(_extraHeader->objSize, &sizeHistograms_[histIndex]);

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

uint64_t GCHistogramObjSizesManager::removeObject(size_t allocSpace,
		mirror::Object* obj) {
	GCPExtraObjHeader* _extraHeader =
			GCHistogramDataManager::GCPGetObjProfHeader(allocSpace, obj);

	if(_extraHeader->objSize == 0)
		return 0;
	GCHistogramDataManager* _histManager = _extraHeader->histRecP;

	if(_histManager == NULL)
		return 0;

	size_t histIndex = (32 - CLZ(_extraHeader->objSize)) - 1;
	((GCHistogramObjSizesManager*)_histManager)->gcpRemoveObjectFromIndex(histIndex,
			_extraHeader->objSize, true);

	return _extraHeader->objSize;

}

void GCHistogramObjSizesManager::gcpRemoveObjFromEntriesWIndex(size_t histIndex,
		size_t objSpace) {
//	LOG(ERROR) << "passing+++histIndex << " <<histIndex;
	GCPPairHistogramRecords* _recData = &sizeHistograms_[histIndex];
	_recData->gcpPairDecRecData(objSpace);
	_recData->gcpPairDecAtomicRecData(objSpace);
}


void GCHistogramObjSizesManager::gcpRemoveObjectFromIndex(size_t histIndex,
		size_t objSize, bool isAgg) {
//	LOG(ERROR) << "passing+++histIndex << " <<histIndex;
	if(isAgg) {
		gcpAggDecPairRecData(objSize, &sizeHistograms_[histIndex]);
		gcpAggDecAtomicPairRecDat(objSize, &sizeHistograms_[histIndex]);
	} else {
		gcpDecPairRecData(objSize, &sizeHistograms_[histIndex]);
		gcpDecAtomicPairRecData(objSize, &sizeHistograms_[histIndex]);
	}

//	bool removedFlag = gcpRemoveDataFromHist(&histogramTable[histIndex]);
//
//	if(removedFlag) {
//		gcpRemoveDataFromHist(&histRecord);
//	}
//
//	//todo: this does not make sense
////	LOG(ERROR) << "Done+++histIndex a " << histIndex;
//	if(type_ != GCMMP_HIST_ROOT) {
//		if(lastCohortIndex != GCHistogramDataManager::kGCPLastCohortIndex.load()){
//			//we cannot remove since there was no allocation done
//			return;
//		}
//	}
//	removedFlag = gcpRemoveAtomicDataFromHist(&lastWindowHistTable[histIndex]);
//	if(removedFlag) {
//		gcpRemoveAtomicDataFromHist(&histAtomicRecord);
//	}
////	LOG(ERROR) << "Done+++histIndex b " << histIndex;
}


//void GCHistogramObjSizesManager::GCPRemoveObj(size_t allocatedMemory,
//		mirror::Object* obj) {
//
//	GCPExtraObjHeader* extraHeader =
//			GCHistogramDataManager::GCPGetObjProfHeader(allocatedMemory, obj);
//
//	if(extraHeader->objSize == 0)
//		return;
//	GCHistogramDataManager* _histManager = extraHeader->histRecP;
//
//	if(_histManager == NULL || extraHeader->objSize == 0)
//		return;
//
//	size_t histIndex = (32 - CLZ(extraHeader->objSize)) - 1;
//	((GCHistogramObjSizesManager*)_histManager)->gcpRemoveObject(histIndex);
//}

//inline void GCHistogramObjSizesManager::gcpAggregateHistograms(GCPHistogramRec* hisTable,
//		GCPHistogramRec* globalRec) {
//	if(false && histRecord.cntTotal <= 0.0)
//		return;
//	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
//		hisTable[i].index 				= histogramTable[i].index;
//		hisTable[i].cntLive 			+= histogramTable[i].cntLive;
//		hisTable[i].cntTotal 		  += histogramTable[i].cntTotal;
//	}
//	globalRec->cntLive 	+= histRecord.cntLive;
//	globalRec->cntTotal += histRecord.cntTotal;
//}


//inline void GCHistogramObjSizesManager::gcpAggAtomicHistograms(GCPHistogramRecAtomic* hisTable,
//		GCPHistogramRecAtomic* globalRec) {
////	int32_t total = ;
////	if(false && total == 0)
////		return;
//	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
//		hisTable[i].index 				= lastWindowHistTable[i].index;
//		hisTable[i].cntLive.fetch_add(lastWindowHistTable[i].cntLive.load());
//		hisTable[i].cntTotal.fetch_add(lastWindowHistTable[i].cntTotal.load());
//	}
//	globalRec->cntTotal.fetch_add(histAtomicRecord.cntTotal.load());
//	globalRec->cntLive.fetch_add(histAtomicRecord.cntLive.load());
//}


inline void GCHistogramObjSizesManager::calculatePercentiles(void) {
	GCPPairHistogramRecords* _globalRec = (GCPPairHistogramRecords*)histData_;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		sizeHistograms_[i].gcpPairUpdatePercentiles(_globalRec);
//		if(hisTable[i].cntTotal < 1.0)
//			continue;
//		hisTable[i].pcntLive = (hisTable[i].cntLive * 100.0) / globalRec->cntLive;
//		hisTable[i].pcntTotal = (hisTable[i].cntTotal * 100.0) / globalRec->cntTotal;
	}
}

inline void GCHistogramObjSizesManager::calculateAtomicPercentiles(void) {
	GCPPairHistogramRecords* _globalRec = (GCPPairHistogramRecords*)histData_;

	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		sizeHistograms_[i].gcpPairUpdateAtomicPercentiles(_globalRec);
	}

//	int32_t _cntLive = _globalRec->cntLive.load();
//	int32_t _cntTotal = _globalRec->cntTotal.load();
//	bool _safeFlag = true;
//	if(_cntLive == 0 || _cntTotal == 0) {
//		_safeFlag = false;
//	}
//	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
//		if(_safeFlag)
//			sizeHistograms_[i].gcpPairUpdateAtomicPercentiles(_globalRec);
//		else
//			sizeHistograms[i].gcpUpdateAtomicRecPercentile(_globalRec);
//	}

//
//	int32_t entryTotal = 0;
//	if(cntTotal == 0)
//		return;
//	if(cntLive == 0) {
//		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
//			entryTotal = hisTable[i].cntTotal.load();
//			hisTable[i].pcntLive = 0.0;
////			if(entryTotal < 1)
////				continue;
//			hisTable[i].pcntTotal = (entryTotal * 100.0) / cntTotal;
//		}
//	} else  {
//		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
//			entryTotal = hisTable[i].cntTotal.load();
////			if(entryTotal < 1)
////				continue;
//			hisTable[i].pcntLive = (hisTable[i].cntLive.load() * 100.0) / cntLive;
//			hisTable[i].pcntTotal = (entryTotal * 100.0) / cntTotal;
//		}
//	}

}

void GCHistogramObjSizesManager::logManagedData(void) {
	LOG(ERROR) << "TotalMutations: " << gcpTotalMutationsCount_.get_total_count();
	GCPPairHistogramRecords* _globalRec =
			(GCPPairHistogramRecords*) histData_;
	GCPPairHistogramRecords* _dataRec = NULL;
	LOG(ERROR) << "Dumping global Record:";
	gcpLogDataRecord(LOG(ERROR), &_globalRec->countData_.dataRec_);
	LOG(ERROR) << "Dumping Entries Record:";
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
		_dataRec = &sizeHistograms_[i];
		gcpLogDataRecord(LOG(ERROR), &_dataRec->countData_.dataRec_);
	}
}

bool GCHistogramObjSizesManager::gcpDumpSummaryManagedData(art::File* dump_file) {
	return gcpDumpHistTable(dump_file, false);
}

bool GCHistogramObjSizesManager::gcpDumpManagedData(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _success = gcpDumpHistTable(dump_file, dumpGlobalRec);
	_success &= gcpDumpHistAtomicTable(dump_file);
	_success &= gcpDumpHistSpaceTable(dump_file, dumpGlobalRec);
	_success &= gcpDumpHistAtomicSpaceTable(dump_file);
	return _success;
}

bool GCHistogramObjSizesManager::gcpDumpHistSpaceTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	if(dumpGlobalRec) {
		GCPPairHistogramRecords* _record = (GCPPairHistogramRecords*) histData_;
		_dataWritten = _record->sizeData_.gcpDumpHistRec(dump_file);
	}
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		_dataWritten = sizeHistograms_[i].sizeData_.gcpDumpHistRec(dump_file);
		if(!_dataWritten)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}

bool GCHistogramObjSizesManager::gcpDumpHistAtomicSpaceTable(
		art::File* dump_file) {
	bool _dataWritten = false;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		_dataWritten = sizeHistograms_[i].sizeData_.gcpDumpAtomicHistRec(dump_file);
		if(!_dataWritten)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	return _dataWritten;
}


bool GCHistogramObjSizesManager::gcpDumpCSVGlobalDataSummary(
		std::ostringstream& outputStream) {
	GCPPairHistogramRecords* pairData =
			(GCPPairHistogramRecords*) histData_;
	outputStream << "TotalAllocObjects:" <<
			StringPrintf("%.0f", pairData->countData_.dataRec_.cntTotal) <<
			"; TotalAllocSpace:" <<
			StringPrintf("%.0f", pairData->sizeData_.dataRec_.cntTotal) << "\n";
	return true;
}

bool GCHistogramObjSizesManager::gcpDumpCSVCoreTables(
		std::ostringstream& outputStream) {
	for(size_t i = 0; i < (size_t) kGCMMPMaxHistogramEntries; i++) {
		GCPHistRecData* _countDataP =
				&sizeHistograms_[i].countData_;
		GCPHistRecData* _sizeDataP =
				&sizeHistograms_[i].sizeData_;
		outputStream << "HistogramEntryIndex:"<<
				StringPrintf("%llu", _countDataP->dataRec_.index) <<
				"; HistogramEntryObjCnt:"<<
				StringPrintf("%.0f", _countDataP->dataRec_.cntTotal) <<
				"; HistogramEntryObjSize:"<<
				StringPrintf("%.0f", _sizeDataP->dataRec_.cntTotal) << "\n";
	}
	return true;
}

bool GCHistogramObjSizesManager::gcpDumpHistTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _success = false;
	if(dumpGlobalRec) {
		GCPPairHistogramRecords* _record = (GCPPairHistogramRecords*) histData_;
		_success = _record->countData_.gcpDumpHistRec(dump_file);
	}
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
		_success = sizeHistograms_[i].countData_.gcpDumpHistRec(dump_file);
		if(!_success)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_success)
		_success &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _success;
}


//bool GCHistogramObjSizesManager::gcpCheckForResetHist(void) {
//	if(lastCohortIndex != GCHistogramDataManager::kGCPLastCohortIndex.load()){
//		//reset percentages in the atomic fields
//		histAtomicRecord.pcntLive = 0.0;
//		histAtomicRecord.pcntTotal = 0.0;
//		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
//			lastWindowHistTable[i].pcntLive = 0.0;
//			lastWindowHistTable[i].pcntTotal = 0.0;
//		}
//		return true;
//	}
//	return false;
//}

//bool GCHistogramObjSizesManager::gcpCheckForCompleteResetHist(void) {
//	int32_t _loadedIndex = GCHistogramDataManager::kGCPLastCohortIndex.load();
//	if(lastCohortIndex != _loadedIndex) {
//		setLastCohortIndex(_loadedIndex);
//		//reset percentages in the atomic fields
//		histAtomicRecord.pcntLive = 0.0;
//		histAtomicRecord.pcntTotal = 0.0;
//		histAtomicRecord.cntLive.store(0);
//		histAtomicRecord.cntTotal.store(0);
//		for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
//			lastWindowHistTable[i].pcntLive = 0.0;
//			lastWindowHistTable[i].pcntTotal = 0.0;
//			lastWindowHistTable[i].cntLive.store(0);
//			lastWindowHistTable[i].cntTotal.store(0);
//		}
//		return true;
//	}
//	return false;
//}


bool GCHistogramObjSizesManager::gcpDumpHistAtomicTable(art::File* dump_file) {
//	GCPHistogramRec dummyRec;
	bool _success = false;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++){
		_success = sizeHistograms_[i].countData_.gcpDumpAtomicHistRec(dump_file);
		if(!_success)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
	if(_success)
		_success &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _success;
}


//inline bool GCHistogramObjSizesManager::gcpDumpHistAtomicRec(art::File* dump_file) {
//	GCPHistogramRec dummyRec;
//	GCPCopyRecords(&dummyRec, &histAtomicRecord);
//	return dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
//}



/*********************** Thread Alloc manager *****************/
GCPThreadAllocManager::GCPThreadAllocManager(void) :
		GCHistogramDataManager(false, NULL) {
	initHistograms();
	LOG(ERROR) << "GCPThreadAllocManager : GCPThreadAllocManager";
}

void GCPThreadAllocManager::initHistograms() {
	objSizesHistMgr_ = new GCHistogramObjSizesManager(true, NULL);
	histData_ = objSizesHistMgr_->histData_;
	LOG(ERROR) << "GCPThreadAllocManager : initHistograms";
}


void GCPThreadAllocManager::setThreadManager(GCMMPThreadProf* thProf) {
	thProf->histogramManager_ = new GCHistogramObjSizesManager(true,
			objSizesHistMgr_);
	thProf->histogramManager_->gcpSetPairRecordIndices(
			thProf->GetTid() & 0x00000000FFFFFFFF);
}

bool GCPThreadAllocManager::dettachThreadFromManager(GCMMPThreadProf* thProf) {
	GCPPairHistogramRecords* _threadProfRec =
			(GCPPairHistogramRecords*) thProf->histogramManager_->histData_;
	if(_threadProfRec == NULL) {
		LOG(ERROR) << "Found record NULL: " << thProf->GetTid();
		return true;
	}
	char* threadNameP = NULL;
	_threadProfRec->getReferenceStringName(&threadNameP);
	if(threadNameP == NULL)
		_threadProfRec->setRefreneceNameFromThread(thProf->GetTid());
	return true;
}


inline void GCPThreadAllocManager::addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
//	GCPExtraObjHeader* extraHeader =
//			GCHistogramDataManager::GCPGetObjProfHeader(allocatedMemory, obj);
//	extraHeader->objSize = objSize;
//	extraHeader->histRecP = this;
	size_t histIndex = (32 - CLZ(objSize)) - 1;
	objSizesHistMgr_->gcpNoAggAddSingleDataToPairHist(objSize, &objSizesHistMgr_->sizeHistograms_[histIndex]);
}

void GCPThreadAllocManager::addObjectForThread(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj, GCMMPThreadProf* thProf) {
	if(thProf->histogramManager_ != NULL) {
		thProf->histogramManager_->addObject(allocatedMemory, objSize, obj);
		//TODO: account for the global histograms:: or maybe not for now
		addObject(allocatedMemory, objSize, obj);
		//TODO: We should remove data from the histograms as well. But I will ignore it now
	}
}

uint64_t GCPThreadAllocManager::removeObject(size_t allocSpace, mirror::Object* obj) {
	GCPExtraObjHeader* _extraHeader =
			GCHistogramDataManager::GCPGetObjProfHeader(allocSpace, obj);

	if(_extraHeader->objSize == 0)
		return 0;
	GCHistogramDataManager* _histManager = _extraHeader->histRecP;

	if(_histManager == NULL)
		return 0;

	size_t histIndex = (32 - CLZ(_extraHeader->objSize)) - 1;
	((GCHistogramObjSizesManager*)_histManager)->gcpRemoveObjectFromIndex(histIndex,
			_extraHeader->objSize,	true);
	objSizesHistMgr_->gcpRemoveObjFromEntriesWIndex(histIndex,
			_extraHeader->objSize);
	return _extraHeader->objSize;
}

inline void GCPThreadAllocManager::calculatePercentiles(void) {
	GCPPairHistogramRecords* _globalRec =
			(GCPPairHistogramRecords*)objSizesHistMgr_->histData_;
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
		if(_histMgr == NULL)
			continue;
		GCPPairHistogramRecords* _recP =
				(GCPPairHistogramRecords*) _histMgr->histData_;
		_recP->gcpPairUpdatePercentiles(_globalRec);
	}
}

inline void GCPThreadAllocManager::calculateAtomicPercentiles(void) {
	GCPPairHistogramRecords* _globalRec = (GCPPairHistogramRecords*) histData_;
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
		if(_histMgr == NULL)
			continue;
		GCPPairHistogramRecords* _recP =
				(GCPPairHistogramRecords*) _histMgr->histData_;
		_recP->gcpPairUpdateAtomicPercentiles(_globalRec);
	}

//	GCPHistogramRecAtomic* _globalRec = histData_->gcpGetAtomicDataRecP();
//	int32_t _cntLive = _globalRec->cntLive.load();
//	int32_t _cntTotal = _globalRec->cntTotal.load();
//	bool _safeFlag = true;
//	if(_cntLive == 0 || _cntTotal == 0) {
//		_safeFlag = false;
//	}
//	for (const auto& threadProf :
//			Runtime::Current()->GetVMProfiler()->threadProfList_) {
//		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
//		if(_histMgr == NULL)
//			continue;
//		if(_safeFlag)
//			_histMgr->histData_->gcpSafeUpdateAtomicRecPercentile(_globalRec);
//		else
//			_histMgr->histData_->gcpUpdateAtomicRecPercentile(_globalRec);
//	}
}

bool GCPThreadAllocManager::gcpDumpHistTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	if(dumpGlobalRec) {
		GCPPairHistogramRecords* _record =
				(GCPPairHistogramRecords*) objSizesHistMgr_->histData_;
		_dataWritten = _record->countData_.gcpDumpHistRec(dump_file);
	} else {
		LOG(ERROR) << "We used to call GCPThreadAllocManager::gcpDumpHistTable";
	}
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
		if(_histMgr == NULL)
			continue;
		GCPPairHistogramRecords* _record =
				(GCPPairHistogramRecords*)_histMgr->histData_;
		_dataWritten = _record->countData_.gcpDumpHistRec(dump_file);
		if(!_dataWritten)
			break;
	}
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}


bool GCPThreadAllocManager::gcpDumpHistSpaceTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	if(dumpGlobalRec) {
		GCPPairHistogramRecords* _record = (GCPPairHistogramRecords*)
				objSizesHistMgr_->histData_;
		_dataWritten = _record->sizeData_.gcpDumpHistRec(dump_file);
	} else {
		LOG(ERROR) << "We used to call GCPThreadAllocManager::gcpDumpHistTable";
	}
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
		if(_histMgr == NULL)
			continue;
		GCPPairHistogramRecords* _record =
				(GCPPairHistogramRecords*)_histMgr->histData_;
		_dataWritten = _record->sizeData_.gcpDumpHistRec(dump_file);
		if(!_dataWritten)
			break;
	}
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}

void GCPThreadAllocManager::logManagedData(void) {
	LOG(ERROR) << "TotalMutations: " << gcpTotalMutationsCount_.get_total_count();
	if(false) {
		LOG(ERROR) << "<<Dumping Global Record>>>";
		GCPPairHistogramRecords* _record =
							(GCPPairHistogramRecords*) histData_;
		gcpLogDataRecord(LOG(ERROR), &_record->countData_.dataRec_);

		LOG(ERROR) << "<<Dumping Thread Records>>";
		int _indexIter = 0;
		for (const auto& threadProf : Runtime::Current()->GetVMProfiler()->threadProfList_) {
			GCHistogramDataManager* _histMgr =
					threadProf->histogramManager_;
			if(_histMgr != NULL) {
				LOG(ERROR) << "-- thread index: " << _indexIter++;
				GCHistogramObjSizesManager* _thrDataManager =
						(GCHistogramObjSizesManager*)_histMgr;
				if(_thrDataManager == NULL)
					continue;
				GCPPairHistogramRecords* _record =
						(GCPPairHistogramRecords*) _thrDataManager->histData_;
				gcpLogDataRecord(LOG(ERROR), &_record->countData_.dataRec_);
			}
		}
		LOG(ERROR) << "<<gcpDumpCSVData>>";
	}
}


bool GCPThreadAllocManager::gcpDumpCSVGlobalDataSummary(
		std::ostringstream& outputStream) {
	GCPPairHistogramRecords* pairData =
			(GCPPairHistogramRecords*) histData_;
	outputStream << "TotalAllocObjects:" <<
			StringPrintf("%.0f", pairData->countData_.dataRec_.cntTotal) <<
			"; TotalAllocSpace:" <<
			StringPrintf("%.0f", pairData->sizeData_.dataRec_.cntTotal) <<
			"; threadCount:" << StringPrintf("%zd",
					Runtime::Current()->GetVMProfiler()->threadProfList_.size()) << "\n";

	return objSizesHistMgr_->gcpDumpCSVCoreTables(outputStream);
}


bool GCPThreadAllocManager::gcpDumpCSVCoreTables(
		std::ostringstream& outputStream) {
	for (const auto& threadProf : Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr =
				threadProf->histogramManager_;
		if(_histMgr != NULL) {
			//LOG(ERROR) << "-- thread index: " << _indexIter++;
			GCHistogramObjSizesManager* _thrDataManager =
					(GCHistogramObjSizesManager*)_histMgr;
			if(_thrDataManager == NULL)
				continue;
			GCPPairHistogramRecords* _record =
					(GCPPairHistogramRecords*) _thrDataManager->histData_;
			char* threadNameP = NULL;

			_record->getReferenceStringName(&threadNameP);
			if(threadNameP == NULL) {
				_record->setRefreneceNameFromThread(threadProf->GetTid());
				_record->getReferenceStringName(&threadNameP);
				//LOG(ERROR) << "set in final stage";
			}
			outputStream << "ThreadAllocIndex:" <<
					StringPrintf("%llu", _record->countData_.dataRec_.index) <<
					"; ThreadAllocName:" << threadNameP <<
					"; ThreadTotalObjCnt:" << StringPrintf("%.0f",_record->countData_.dataRec_.cntTotal) <<
					"; ThreadTotalSpace:" << StringPrintf("%.0f",_record->sizeData_.dataRec_.cntTotal) << "\n";
//			LOG(ERROR) << "ThreadAllocName: " << threadNameP;
//			gcpLogDataRecord(LOG(ERROR), &_record->countData_.dataRec_);
		}
	}

	return true;
}

//bool GCPThreadAllocManager::gcpDumpTotalSummaryCSVData(
//		std::ostringstream& outputStream) {
//	return objSizesHistMgr_->gcpDumpCSVData(outputStream);
//}



bool GCPThreadAllocManager::gcpDumpThreadHistogramCSVData(
		std::ostringstream& outputStream) {
	GCPHistRecData* _countDataP = NULL;
	GCPHistRecData* _sizeDataP  = NULL;
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramObjSizesManager* _thrDataManager =
							(GCHistogramObjSizesManager*)threadProf->histogramManager_;
		GCPPairHistogramRecords* _threadMainRecord =
				(GCPPairHistogramRecords*) _thrDataManager->histData_;
		for(size_t i = 0; i < (size_t) kGCMMPMaxHistogramEntries; i++) {
			_countDataP = &_thrDataManager->sizeHistograms_[i].countData_;
			_sizeDataP = &_thrDataManager->sizeHistograms_[i].sizeData_;
			outputStream << "ThreadIndex:"<<
					StringPrintf("%llu", _threadMainRecord->countData_.dataRec_.index) <<
					"; histIndex:"  << StringPrintf("%llu", _countDataP->dataRec_.index) <<
					"; objectsCnt:" <<
					StringPrintf("%.0f", _countDataP->dataRec_.cntTotal) <<
					"; ObjectsSize:"<<
					StringPrintf("%.0f", _sizeDataP->dataRec_.cntTotal) <<"\n";
		}

	}

	return true;
}

bool GCPThreadAllocManager::gcpDumpCSVData(std::ostringstream& outputStream) {
	bool _success = gcpDumpCSVCoreTables(outputStream);
	_success &= gcpDumpCSVGlobalDataSummary(outputStream);
  _success &= gcpDumpThreadHistogramCSVData(outputStream);
	return _success;
}

bool GCPThreadAllocManager::gcpDumpHistAtomicSpaceTable(art::File* dump_file) {
	bool _success = false;
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
		if(_histMgr == NULL)
			continue;
		GCPPairHistogramRecords* _record =
				(GCPPairHistogramRecords*)_histMgr->histData_;
		_success = _record->sizeData_.gcpDumpAtomicHistRec(dump_file);
		if(!_success)
			break;
	}
	if(_success)
		_success &=
				VMProfiler::GCPDumpEndMarker(dump_file);
	return _success;
}

bool GCPThreadAllocManager::gcpDumpHistAtomicTable(art::File* dump_file) {
	bool _success = false;
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		GCHistogramDataManager* _histMgr = threadProf->histogramManager_;
		if(_histMgr == NULL)
			continue;
		GCPPairHistogramRecords* _record =
				(GCPPairHistogramRecords*)_histMgr->histData_;
		_success = _record->countData_.gcpDumpAtomicHistRec(dump_file);
		if(!_success)
			break;
	}
	if(_success)
		_success &=
				VMProfiler::GCPDumpEndMarker(dump_file);
	return _success;
}


void GCPThreadAllocManager::gcpZeorfyAllAtomicRecords() {
	for (const auto& threadProf :
			Runtime::Current()->GetVMProfiler()->threadProfList_) {
		if(threadProf->histogramManager_ != NULL) {
			threadProf->histogramManager_->gcpZeorfyAllAtomicRecords();
		}
	}
	objSizesHistMgr_->gcpZeorfyAllAtomicRecords();
//	histData_->gcpZerofyHistAtomicRecData();
}

void GCPThreadAllocManager::gcpFinalizeProfileCycle(){
	uint64_t _newCohortIndex = VMProfiler::GCPCalcCohortIndex();
	if(_newCohortIndex != GCPGetLastManagedCohort()) {
		gcpZeorfyAllAtomicRecords();
		GCPSetLastManagedCohort(_newCohortIndex);
	}
}

bool GCPThreadAllocManager::gcpDumpManagedData(art::File* dumpFile,
		bool dumpGlobalRec) {
	bool _success = gcpDumpHistTable(dumpFile, dumpGlobalRec);
	_success &= gcpDumpHistAtomicTable(dumpFile);
	_success = gcpDumpHistSpaceTable(dumpFile, dumpGlobalRec);
	_success &= gcpDumpHistAtomicSpaceTable(dumpFile);
//	_success &= dumpClassSizeHistograms(dumpFile, dumpGlobalRec);
//	_success &= dumpClassAtomicSizeHistograms(dumpFile);
	return _success;
}

bool GCPThreadAllocManager::gcpDumpSummaryManagedData(art::File* dumpFile) {
	return gcpDumpHistTable(dumpFile, false);
}





/******************** GCCohortManager ***********************/

GCCohortManager::GCCohortManager(SafeGCPHistogramRec* allocRec) :
		GCHistogramDataManager(false) {
	//get the correct cohort index;
	allocRec_ = allocRec;
	initHistograms();
}



void GCCohortManager::addCohortRow(void) {
	currCoRowP = (GCPCohortsRow*) calloc(1, getCoRowSZ());
	currCoRowP->index_ = 0;
	cohortsTable_.cohortRows_.push_back(currCoRowP);
}

void GCCohortManager::addCohortRecord(void) {
	if(currCoRowP->index_ == kGCMMPMaxRowCap) {
		//we passed the capacity and we need a new row
		addCohortRow();
	}
	uint64_t _index = calcNewCohortIndex();
	currCohortP = &currCoRowP->cohorts[currCoRowP->index_];
	currCoRowP->index_++;

	currCohortP->index_ = _index;
}



void GCCohortManager::initHistograms(void) {
	cohRowSZ_ = kGCMMPMaxRowCap   * sizeof(GCPCohortRecordData);
//	cohArrSZ_ = kGCMMPMaxTableCap * sizeof(GCPCohortsRow*);

	cohortsTable_.index = 0;
	currCohortP = NULL;

	addCohortRow();
	addCohortRecord();

	for(size_t i = 0; i < (size_t) kGCMMPMaxHistogramEntries; i++) {
		lifeTimeHistograms_[i].gcpPairSetRecordIndices((uint64_t)((i+1)));
	}
}

inline uint64_t GCCohortManager::calcNewCohortIndex() {
	if(currCohortP == NULL) {
		return VMProfiler::GCPCalcCohortIndex();
	}
	return (currCohortP->index_ + 1);
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

void GCCohortManager::addObject(size_t allocatedMemory, size_t objSize,
		mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
			GCHistogramObjSizesManager::GCPGetObjProfHeader(allocatedMemory, obj);
	addObjectToCohRecord(objSize);
	_profHeader->objSize = static_cast<uint64_t>(objSize);
	//we need to calculate the correct bytes without the allocated memory
	_profHeader->objBD = calcObjBD(_profHeader->objSize);
}


uint64_t GCCohortManager::removeObject(size_t allocSpace, mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
			GCHistogramObjSizesManager::GCPGetObjProfHeader(allocSpace, obj);
	if(_profHeader == NULL || _profHeader->objSize == 0) {
		//the object was not registered
		//GCMMP_VLOG(INFO)  << "---------Found none registered object";
		return 0;
	}
	uint64_t lifeTime = calcObjLifeTime(_profHeader->objBD);
	uint32_t histIndex = 0;

	GCP_CALC_HIST_INDEX(histIndex, lifeTime);


	//GCMMP_VLOG(INFO)  << "---histogram_index: " << histIndex << ", lifeTime: " << lifeTime;

	lifeTimeHistograms_[histIndex].gcpPairIncRecData(_profHeader->objSize);
	lifeTimeHistograms_[histIndex].gcpPairIncAtomicRecData(static_cast<size_t>(_profHeader->objSize));



	size_t _startRow, _startIndex, _endRow, _endIndex = 0;
	getCoAddrFromBytes(&_startRow, &_startIndex, &_endRow, &_endIndex,
			_profHeader->objBD, _profHeader->objSize);


//	if(true)
//	  return 0;
	GCPCohortRecordData* _firstRecP = NULL;
	GCPCohortRecordData* _LastRecP = NULL;
	size_t _rowIter  = _startRow;
	size_t _colIter  = _startIndex;

	if(_startIndex >= (size_t) kGCMMPMaxRowCap || _endIndex >= (size_t)kGCMMPMaxRowCap)
		LOG(ERROR) << "startRow=" << _startRow<< "; startInd=" << _startIndex << "; endRow=" << _endRow << "; endIndex=" << _endIndex;
	_firstRecP = getCoRecFromIndices(_startRow, _startIndex);
	if(_firstRecP == NULL) {
//		LOG(ERROR) << "NULL:::BD="<<_profHeader->objBD<<"; currentBytes="<< allocRec_->load()<<"; capacit=" << cohortsTable_.cohortRows_.size() <<",startRow=" << _startRow<< "; startInd=" << _startIndex << "; endRow=" << _endRow << "; endIndex=" << _endIndex;
		return 0;
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
		                   static_cast<size_t>((_profHeader->objBD + _profHeader->objSize) % kGCMMPCohortSize));
		updateDelCohRecObj(_firstRecP,
		                   static_cast<size_t>((kGCMMPCohortSize - (_profHeader->objBD % kGCMMPCohortSize))));
		updateDelCohRecObjCnts(_firstRecP);


		while(true) {
			incColIndex(&_rowIter, &_colIter);
			if(_colIter == _endIndex && _endRow == _rowIter)
				break;
			if(_rowIter >= cohortsTable_.cohortRows_.size() || _colIter >= (size_t)kGCMMPMaxRowCap){
//				LOG(ERROR) << "2--NULL:::BD="<<_profHeader->objBD<<"; currentBytes="<< allocRec_->load()<<"; capacit=" << cohortsTable_.cohortRows_.size() <<",startRow=" << _startRow<< "; startInd=" << _startIndex << "; endRow=" << _endRow << "; endIndex=" << _endIndex;
				return 0;
			}
			_LastRecP =  getCoRecFromIndices(_rowIter, _colIter);
			updateDelCohRecObj(_LastRecP, kGCMMPCohortSize);
		}
	}

	return _profHeader->objSize;

}



GCPCohortRecordData* GCCohortManager::getCoRecFromObj(size_t allocSpace,
		mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
			GCHistogramObjSizesManager::GCPGetObjProfHeader(allocSpace, obj);
	if(_profHeader->objSize == 0) //the object was not registered
		return NULL;
	uint64_t _cohIndex = _profHeader->objBD >> GCHistogramDataManager::kGCMMPCohortLog;
	size_t _rowIndex = static_cast<size_t>(_cohIndex /  kGCMMPMaxRowCap);
	GCPCohortsRow* _row = cohortsTable_.cohortRows_[_rowIndex];
	GCPCohortRecordData* _cohRec = &_row->cohorts[_cohIndex%_rowIndex];

	return _cohRec;

}

bool GCCohortManager::gcpDumpSizeHistAtomicLifeTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		_dataWritten = lifeTimeHistograms_[i].sizeData_.gcpDumpAtomicHistRec(dump_file);
		if(!_dataWritten)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}

bool GCCohortManager::gcpDumpCntHistAtomicLifeTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		_dataWritten = lifeTimeHistograms_[i].countData_.gcpDumpAtomicHistRec(dump_file);
		if(!_dataWritten)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}

bool GCCohortManager::gcpDumpSizeHistLifeTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		_dataWritten = lifeTimeHistograms_[i].sizeData_.gcpDumpHistRec(dump_file);
		if(!_dataWritten)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}

bool GCCohortManager::gcpDumpCntHistLifeTable(art::File* dump_file,
		bool dumpGlobalRec) {
	bool _dataWritten = false;
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		_dataWritten = lifeTimeHistograms_[i].countData_.gcpDumpHistRec(dump_file);
		if(!_dataWritten)
			break;
//		GCPCopyRecords(&dummyRec, &lastWindowHistTable[i]);
//		 _success &=
//		   	dump_file->WriteFully(&dummyRec, sizeof(GCPHistogramRec));
	}
//	 bool _success =
//	   	dump_file->WriteFully(histogramTable, totalHistogramSize);
	if(_dataWritten)
		_dataWritten &=
			 VMProfiler::GCPDumpEndMarker(dump_file);
	 return _dataWritten;
}

bool GCCohortManager::gcpDumpManagedData(art::File* dumpFile,
		bool dumpGlobalData){
	bool _print   = false;
	//GCPCohortRecordData* _recP = NULL;
	size_t _rowBytes = 0;
	//LOG(ERROR) << "dumpRows----";
	for (const auto& _rowIterP : cohortsTable_.cohortRows_) {
		_rowBytes = (_rowIterP->index_) * sizeof(GCPCohortRecordData);
		//LOG(ERROR) << _index << "::dump Row: " << _rowIterP->index_;
		if(_rowIterP->index_ > kGCMMPMaxRowCap) {
			LOG(ERROR) << "Index out of boundary : " << _rowIterP->index_;
		}
		if(_rowBytes == 0)
			break;
		_print = dumpFile->WriteFully(_rowIterP->cohorts, _rowBytes);
		if(!_print)
			break;
	}

	if(_print)
		_print &= VMProfiler::GCPDumpEndMarker(dumpFile);

	_print &= gcpDumpCntHistLifeTable(dumpFile, false);
	_print &= gcpDumpSizeHistLifeTable(dumpFile, false);
	//_print &= gcpDumpCntHistAtomicLifeTable(dumpFile, false);
	//_print &= gcpDumpSizeHistAtomicLifeTable(dumpFile, false);

	return _print;
}


void GCCohortManager::logManagedData(void) {
  if(VMProfiler::GCPCalcCohortIndex() == 0)
    return;

	size_t _rowBytes = 0;
	LOG(ERROR) << "Count of Cohort Rows: "<< cohortsTable_.cohortRows_.size() <<
			"; table index: " << cohortsTable_.index;
	LOG(ERROR) << "TotalMutations: " << gcpTotalMutationsCount_.get_total_count();
	int _rIndex = 0;
	for (const auto& _rowIterP : cohortsTable_.cohortRows_) {
		_rowBytes = (_rowIterP->index_) * sizeof(GCPCohortRecordData);
		if(_rowBytes == 0)
			break;
		LOG(ERROR) << "++ROW: " << _rIndex;
		int _indIter = 0;
		while(_indIter < _rowIterP->index_) {
			GCPCohortRecordData* _recData = &_rowIterP->cohorts[_indIter];
			gcpLogDataRecord(LOG(ERROR), _recData);
			_indIter++;
		}
		_rIndex++;
	}
}

void GCCohortManager::gcpZeorfyAllAtomicRecords(void) {
	for (int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		lifeTimeHistograms_[i].gcpZerofyPairHistAtomicRecData();
	}
}

void GCCohortManager::gcpFinalizeProfileCycle(void) {
  uint64_t _newCohortIndex = VMProfiler::GCPCalcCohortIndex();
  if(_newCohortIndex != GCPGetLastManagedCohort()) {
  	gcpZeorfyAllAtomicRecords();
  	GCPSetLastManagedCohort(_newCohortIndex);
  }
}


/*********************** GCRefDistanceManager **************************/

GCRefDistanceManager::GCRefDistanceManager(SafeGCPHistogramRec* safeTotalAccount) :
		GCCohortManager(safeTotalAccount) {
	initDistanceArray();
}

void GCRefDistanceManager::initDistanceArray(void) {
	//int64_t _index = 0;
	for(int64_t i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		//_index = i * 1.0;/*(uint64_t)((i) & (0x00000000FFFFFFFF));*/
		posRefDist_[i].setIndex(i);
		posRefDist_[i].setIndex(-i);
	}
	mutationStats_.setIndex(0);
	selReferenceStats_.setIndex(1);

}


void GCRefDistanceManager::resetCurrentCounters(void) {
  Thread* self_thread = Thread::Current();
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		posRefDist_[i].resetLiveData(self_thread);
		negRefDist_[i].resetLiveData(self_thread);
	}
	mutationStats_.resetLiveData(self_thread);
	selReferenceStats_.resetLiveData(self_thread);
}

void GCRefDistanceManager::gcpFinalizeProfileCycle(void) {
	resetCurrentCounters();
}




void GCRefDistanceManager::profileDistance(const mirror::Object* sourceObj,
		uint32_t member_offset, const mirror::Object* sinkObj) {
	//we only measure distance if both objects are registered in the profiler
	size_t allocatedSpace =
			Runtime::Current()->GetHeap()->GCPGetObjectAllocatedSpace(sourceObj);
	if(allocatedSpace == 0) {
		GCMMP_VLOG(INFO) << "Skipping Allocated space of source is zero";
		return;
	}

	mirror::Object* youngObj = const_cast<mirror::Object*>(sourceObj);

	GCPExtraObjHeader* _sourceProfHeader =
			GCHistogramObjSizesManager::GCPGetObjProfHeader(allocatedSpace, youngObj);
	if(_sourceProfHeader->objSize == 0) {
		//the object was not registered
		GCMMP_VLOG(INFO)  << "---------Found the source as none registered object";
		return;
	}
	mirror::Object* oldObj = const_cast<mirror::Object*>(sinkObj);
	allocatedSpace =
				Runtime::Current()->GetHeap()->GCPGetObjectAllocatedSpace(oldObj);
	if(allocatedSpace == 0) {
		GCMMP_VLOG(INFO) << "Skipping Allocated space of sink is zero";
		return;
	}
	GCPExtraObjHeader* _sinkProfHeader =
			GCHistogramObjSizesManager::GCPGetObjProfHeader(allocatedSpace, oldObj);
	if(_sinkProfHeader->objSize == 0) {
		//the object was not registered
		GCMMP_VLOG(INFO)  << "---------Found the sink as none registered object";
		return;
	}



	GCPExtraObjHeader* _oldProfHeader = _sinkProfHeader;
	GCPExtraObjHeader* _youngProfHeader = _sourceProfHeader;

	int directionCase = 1;
	size_t _refDistanceIndex = 0;
	uint64_t _refDistance = 0;


	if(sourceObj == sinkObj) {
		//special case when self reference
		directionCase = 0;
	} else {
		if(_sourceProfHeader->objBD < _sinkProfHeader->objBD) { //switch
			oldObj = const_cast<mirror::Object*>(sourceObj);
			youngObj = const_cast<mirror::Object*>(sinkObj);
			_oldProfHeader = _sourceProfHeader;
			_youngProfHeader = _sinkProfHeader;
			directionCase = -1;
		}
		size_t _startRowOld, _startIndexOld, _endRowOld, _endIndexOld = 0;
		size_t _startRowYoung, _startIndexYoung, _endRowYoung, _endIndexYoung = 0;
		getCoAddrFromBytes(&_startRowOld, &_startIndexOld, &_endRowOld,
				&_endIndexOld, _oldProfHeader->objBD, _oldProfHeader->objSize);
		getCoAddrFromBytes(&_startRowYoung, &_startIndexYoung, &_endRowYoung,
				&_endIndexYoung, _youngProfHeader->objBD, _youngProfHeader->objSize);
		GCPCohortRecordData* _oldCohortRecP =
				getCoRecFromIndices(_endRowOld, _endIndexOld);
		double _oldPopFactor = _oldCohortRecP->liveSize / _oldCohortRecP->totalSize;
		uint64_t _oldBoundary =
				(_oldProfHeader->objBD + _oldProfHeader->objSize) % kGCMMPCohortSize;
		uint64_t _youngBoundary =
				(_youngProfHeader->objBD) % kGCMMPCohortSize;
		if(_endRowOld == _startRowYoung && _endIndexOld == _startIndexYoung) {
			//special case when both objects in the same cohort
			_refDistance = (uint64_t) (_oldPopFactor * (_youngBoundary - _oldBoundary));
		} else {
			_refDistance += (uint64_t)((kGCMMPCohortSize - _oldBoundary) * _oldPopFactor);
			GCPCohortRecordData*_youngCohortRecP =
							getCoRecFromIndices(_startRowYoung, _startIndexYoung);
			double _youngPopFactor =
					_youngCohortRecP->liveSize / _youngCohortRecP->totalSize;
			_refDistance +=  (size_t)(_youngPopFactor * _youngBoundary);
			size_t _rowIter = _endRowOld, _colIter = _endIndexOld;
			GCPCohortRecordData* _cohorRecIter = NULL;
			while(true) {
				incColIndex(&_rowIter, &_colIter);
				if(_colIter == _startIndexYoung && _startRowYoung == _rowIter)
					break;
				_cohorRecIter =  getCoRecFromIndices(_rowIter, _colIter);
				_refDistance +=  (static_cast<uint64_t>(_cohorRecIter->liveSize));
			}
		}
	}


	Thread* _curr_thread = Thread::Current();

	if(directionCase == -1) {
		_refDistance += _oldProfHeader->objSize - member_offset;
	} else if (directionCase == 1) {
		_refDistance += member_offset + _oldProfHeader->objSize;
	} else {
		_refDistance += member_offset;
		selReferenceStats_.inc_counts(_curr_thread,1);
//		selReferenceStats_.total_++;
	}
	_refDistanceIndex = (32 - CLZ(_refDistance));
	if(directionCase == -1) {
		negRefDist_[_refDistanceIndex].inc_counts(_curr_thread,1);
//		negRefDist_[_refDistanceIndex].total_++;
	} else {
		posRefDist_[_refDistanceIndex].inc_counts(_curr_thread,1);
//		posRefDist_[_refDistanceIndex].total_++;
	}

	mutationStats_.inc_counts(_curr_thread,1);
//	mutationStats_.total_++;
}

bool GCRefDistanceManager::gcpDumpHistTable(art::File* dumpFile,
		bool dumpGlobalData) {
	bool _success   = false;
	Thread* _curr_thread = Thread::Current();
	if(dumpGlobalData) {
		GCMMP_VLOG(INFO) << "-----summary-----";
		GCPDistanceRecDisplay _recDisplayMuts;
		GCPDistanceRecDisplay _recDisplaySelfMuts;

		double _selfRefPercTotal = 0.0;
		double _selfRefPercLastWindow = 0.0;

		double _totalPercTotal = 0.0;
		double _totalPercLastWindow = 0.0;

		copyToDisplayRecord(_curr_thread, &_recDisplayMuts, &mutationStats_);
		copyToDisplayRecord(_curr_thread, &_recDisplaySelfMuts, &selReferenceStats_);

//		_recDisplayMuts.index_ = mutationStats_.total_.load() * 1.0;
//		_recDisplaySelfMuts.index_ = mutationStats_.total_.load() * 1.0;
		if(_recDisplayMuts.live_ != 0) {
			_selfRefPercLastWindow =
					(1.0 * _recDisplaySelfMuts.live_) / _recDisplayMuts.live_;
		}
		if(_recDisplayMuts.total_ != 0) {
			_selfRefPercTotal = (1.0 * _recDisplaySelfMuts.total_) / _recDisplayMuts.total_;
		}
		_totalPercTotal = 100.0 - _selfRefPercTotal;
		_totalPercLastWindow = 100.0 - _selfRefPercLastWindow;

		_success = dumpFile->WriteFully(&_recDisplayMuts, static_cast<int64_t>(sizeof(GCPDistanceRecDisplay)));
		_success &= dumpFile->WriteFully(&_totalPercLastWindow, static_cast<int64_t>(sizeof(double)));
		_success &= dumpFile->WriteFully(&_totalPercTotal, static_cast<int64_t>(sizeof(double)));

		if (GC_V_ENABLED)
			logRecDisplay(&_recDisplayMuts);
		GCMMP_VLOG(INFO) << "lastWindowPerc:" << StringPrintf("%f", _totalPercLastWindow)
				<< "; totalPerc:" << StringPrintf("%f", _totalPercTotal);


		_success &= dumpFile->WriteFully(&_recDisplaySelfMuts, static_cast<int64_t>(sizeof(GCPDistanceRecDisplay)));
		_success &= dumpFile->WriteFully(&_selfRefPercLastWindow, static_cast<int64_t>(sizeof(double)));
		_success &= dumpFile->WriteFully(&_selfRefPercTotal, static_cast<int64_t>(sizeof(double)));

		if (GC_V_ENABLED)
			logRecDisplay(&_recDisplaySelfMuts);
		GCMMP_VLOG(INFO) << "lastWindowPerc:" << StringPrintf("%f", _selfRefPercLastWindow)
				<< "; totalPerc:" << StringPrintf("%f", _selfRefPercTotal);
		if(_success)
			_success &= VMProfiler::GCPDumpEndMarker(dumpFile);

		if(!_success) {
			LOG(ERROR) << "error dumping global record in GCRefDistanceManager::gcpDumpHistTable";
		}
	}
	copyArrayForDisplay(_curr_thread, negRefDist_);
	_success = dumpFile->WriteFully(arrayDisplay_,
			kGCMMPMaxHistogramEntries * sizeof(GCPDistanceRecDisplay));
	copyArrayForDisplay(_curr_thread, posRefDist_);
	_success &= dumpFile->WriteFully(arrayDisplay_,
			kGCMMPMaxHistogramEntries * sizeof(GCPDistanceRecDisplay));
	if(_success)
		_success &= VMProfiler::GCPDumpEndMarker(dumpFile);
	return _success;
}

void GCRefDistanceManager::logManagedData(void) {
	LOG(ERROR) << "----dumping Positive----------";
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		logRecDisplay(&arrayDisplay_[i]);
	}
	LOG(ERROR) << "----dumping Negative----------";
	for(int i = 0; i < kGCMMPMaxHistogramEntries; i++) {
		LOG(ERROR) << i << ": " << StringPrintf("%llu",negRefDist_[i].get_total_count());
	}
}

bool GCRefDistanceManager::gcpDumpManagedData(art::File* dumpFile,
		bool dumpGlobalData) {
	LOG(ERROR) << "dumping reference distances";
	bool _success = gcpDumpHistTable(dumpFile, dumpGlobalData);
	return _success;
}

/********************* GCHistogramDataManager profiling ****************/


GCHistogramDataManager::GCHistogramDataManager(bool shouldInitHistograms) : type_(GCMMP_HIST_CHILD) {
	initManager(NULL, shouldInitHistograms);
}


GCHistogramDataManager::GCHistogramDataManager(void) : type_(GCMMP_HIST_CHILD) {
	initManager(NULL, true);
}

GCHistogramDataManager::GCHistogramDataManager(bool shouldInitHistograms,
		GCHistogramDataManager* parentManager) {
	initManager(parentManager, shouldInitHistograms);
}

GCHistogramDataManager::GCHistogramDataManager(GCMMP_HISTOGRAM_MGR_TYPE hisMGR) :
		type_(hisMGR) {
	initManager(NULL, true);
}


void GCHistogramDataManager::initManager(GCHistogramDataManager* pManager,
		bool shouldInitHist) {
	generateNewSecret();
	parentManager_ = pManager;
	GCPSetLastManagedCohort(static_cast<int32_t>(VMProfiler::GCPCalcCohortIndex()));
	if(shouldInitHist)
		initHistograms();
}

//inline void GCHistogramDataManager::gcpAddDataToHist(GCPHistogramRec* rec) {
//	rec->cntLive++;
//	rec->cntTotal++;
//}
//
//
//inline void GCHistogramDataManager::gcpRemoveDataToHist(GCPHistogramRec* rec) {
//	rec->cntLive--;
//}

inline bool GCHistogramDataManager::gcpDumpHistRec(art::File* dump_file) {
	return dump_file->WriteFully(gcpGetDataRecP(), static_cast<int64_t>(sizeof(GCPHistogramRec)));
}

/********************* GCClassTableManager profiling ****************/


GCClassTableManager::GCClassTableManager(void) :
		GCHistogramDataManager(false) {
	initHistograms();
	//LOG(ERROR) << "GCClassTableManager::GCClassTableManager";
}

GCClassTableManager::GCClassTableManager(GCMMP_HISTOGRAM_MGR_TYPE hisMGR) :
		GCHistogramDataManager(hisMGR) {
	initHistograms();
	//LOG(ERROR) << "GCClassTableManager::GCClassTableManager";
}

void GCClassTableManager::initHistograms(void) {
	/* no need for histData_ */
	histData_ = new GCPPairHistogramRecords(0);
	//histData_ = new GCPHistRecData(0);
	//LOG(ERROR) << "GCClassTableManager::initHistograms";
}

GCPHistRecData* GCClassTableManager::addObjectClassPair(mirror::Class* klass,
		mirror::Object* obj) {
	if(klass == NULL)
		return NULL;
	uint64_t klassHash = 0;
	{

		ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
		klassHash = Runtime::Current()->GetClassLinker()->gcpGetClassHash(klass);
		//LOG(ERROR) << "start Hash=" << klassHash;
		GCPHistRecData* _histRec =
				Runtime::Current()->GetInternTable()->GCPProfileObjKlass(klassHash,
						klass);

		if(_histRec != NULL) {
			size_t objSpace =
					Runtime::Current()->GetHeap()->GCPGetObjectAllocatedSpace(obj);
			if(objSpace == 0) {
				LOG(ERROR) << "Objectsize rturned 0: ";
			}
			GCPExtraObjHeader* _profHeader =
					GCHistogramDataManager::GCPGetObjProfHeader(objSpace, obj);
			gcpIncAtomicPairRecData(_profHeader->objSize, _histRec);
			gcpIncPairRecData(_profHeader->objSize, _histRec);

			_profHeader->dataRec = _histRec;
		} else {
			LOG(ERROR) << "GCClassTableManager::addObjectClassPair -- histRec is NULL ";
		}
		return _histRec;
	}
	return NULL;
}



//
//
//
////		if(_histRec == NULL) {
////			//LOG(ERROR) << "GCClassTableManager:: _histRec is NULL";
////		} else {
////			//LOG(ERROR) << "GCClassTableManager:: _histRec is not NULL";
////		}
////		_histRec->gcpIncRecData();
////		_histRec->gcpIncAtomicRecData();
//////		if(histData_ == NULL) {
//////			LOG(ERROR) << "GCClassTableManager:: histData_ is NULL";
//////		} else {
//////			LOG(ERROR) << "GCClassTableManager:: histData_ is not NULL";
//////		}
////		//update the global entry as well
////		histData_->gcpIncRecData();
////		histData_->gcpIncAtomicRecData();
//
//		//add data to global histogram
//
//		return _histRec;
////		for (auto it = histogramMapTable.find(klassHash), end = histogramMapTable.end(); it != end; ++it) {
////			LOG(ERROR) << "Found start Hash=" << klassHash;
////			return;
////		}
//		//_histRec = (GCPHistogramRec*) calloc(1, sizeof(GCPHistogramRec));
//
//
//		//histogramMapTable.emplace(klassHash, _histRec);
//		//classTable_.insert(std::make_pair(klassHash, klass));
//		//LOG(ERROR) << "Done Hash=" << klassHash;
//	}
//	return NULL;
//  //Thread* self = Thread::Current();
//  //MutexLock mu(self, classTable_lock_);
//
//	//if(directory_.empty()) {
//		//LOG(ERROR) << "Adding the phone book:" << directory_.size();
//		//size_t sizeT = directory_.size();
//		//directory_.insert(std::make_pair(sizeT,    klass));
//		//directory_.insert(std::pair<size_t, PhoneNum>(2,    PhoneNum("555-9999")));
//		//directory_.insert(std::pair<size_t, PhoneNum>(3,  PhoneNum("555-9678")));
//		//LOG(ERROR) << "Done Adding the phone book";
////	}
////
////
////
//////  for (auto it = classTable_.find(klassHash), end = classTable_.end(); it != end; ++it) {
//////  	_histRec = &it->second;
//////    break;
//////  }
////
//////	auto search = classTable_.find(klassHash);
//////	if(search != classTable_.end()) {
//////		_histRec = search->second;
//////	}
////  if(_histRec == NULL) {
////  	GCPHistogramRec _record;
////  	//klassHash = classTableTest_.size();
////  	_histRec = (GCPHistogramRec*) calloc(1, sizeof(GCPHistogramRec));
////
////  	//if(false)
////  	//	classTable_[klassHash] = &_record;
////  	classTableTest_.insert(std::make_pair(klassHash, klassHash));
////  	LOG(ERROR) << "Done Hash=" << klassHash;
////  }
////
////  gcpAddDataToHist(_histRec);
//}


inline uint64_t GCClassTableManager::removeObject(size_t allocSpace, mirror::Object* obj) {
	GCPExtraObjHeader* _profHeader =
				GCHistogramObjSizesManager::GCPGetObjProfHeader(allocSpace, obj);
	if(_profHeader->objSize == 0) {
		GCMMP_VLOG(INFO) << "--------- GCClassTableManager::removeObject: Found none registered object";
		return 0;
	}
	GCPHistRecData* _dataRec = _profHeader->dataRec;
	if(_dataRec == NULL)
		return 0;
	gcpDecPairRecData(_profHeader->objSize, _dataRec);
	gcpDecAtomicPairRecData(_profHeader->objSize, _dataRec);

	return _profHeader->objSize;
}


inline void GCClassTableManager::addObject(size_t allocatedMemory,
		size_t objSize, mirror::Object* obj) {
	GCPExtraObjHeader* extraHeader = GCPGetObjProfHeader(allocatedMemory, obj);
	extraHeader->objSize = objSize;
	extraHeader->dataRec = NULL;
//	size_t histIndex = (32 - CLZ(objSize)) - 1;
//	if(histIndex == 0)
//		return;
//	 mirror::Class* _klass = obj->GetClass();
//	 if(_klass == NULL) {
//		 LOG(ERROR) << "XXXXXXXXX OBJECT CLASS IS NULL";
//	 }
//	std::make_pair(hash, klass)
//	classTable_


//	int32_t _readCohortIndex = (GCHistogramDataManager::kGCPLastCohortIndex.load());
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

void GCClassTableManager::logManagedData(void){

	GCPPairHistogramRecords* _dataRec =
			(GCPPairHistogramRecords*) histData_;

	LOG(ERROR) << "GlobalRecord>>  Counts";
	gcpLogDataRecord(LOG(ERROR),
			_dataRec->countData_.gcpGetDataRecP());
	LOG(ERROR) << "GlobalRecord>>  Sizes";
	gcpLogDataRecord(LOG(ERROR),
			_dataRec->sizeData_.gcpGetDataRecP());

	LOG(ERROR) << "+++table class size is " <<
			Runtime::Current()->GetInternTable()->classTableProf_.size();

	LOG(ERROR) << "TotalMutations: " << gcpTotalMutationsCount_.get_total_count();
	if(true)
		return;
	double _cntLive = 0.0;
	double _cntTotal = 0.0;
	double _pcntLive = 0.0;
	double _pcnTotal = 0.0;
	double _pcntAtomicLive  = 0.0;
	double _pcntAtomicTotal = 0.0;

	double _spaceLive = 0.0;
	double _spaceTotal = 0.0;
	double _pcntSpaceLive = 0.0;
	double _pcnSpaceTotal = 0.0;

	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*)it.second;
		GCPHistRecData* _cntRecord = &_rec->countData_;
		GCPHistRecData* _spaceRecord = &_rec->sizeData_;



		_cntTotal += _cntRecord->dataRec_.cntTotal;
		_cntLive  += _cntRecord->dataRec_.cntLive;
		_pcntLive += _cntRecord->dataRec_.pcntLive;
		_pcnTotal += _cntRecord->dataRec_.pcntTotal;

		_spaceLive += _spaceRecord->dataRec_.cntTotal;
		_spaceTotal += _spaceRecord->dataRec_.cntLive;
		_pcntSpaceLive += _spaceRecord->dataRec_.pcntLive;
		_pcnSpaceTotal += _spaceRecord->dataRec_.pcntTotal;

		_pcntAtomicTotal += _cntRecord->atomicDataRec_.pcntTotal;
		_pcntAtomicLive  += _cntRecord->atomicDataRec_.pcntLive;

		if(false) {
				LOG(ERROR) << "hash-- " << it.first;
				//LOG(ERROR) <<;
				gcpLogDataRecord(LOG(ERROR) << "Count>> ", &_cntRecord->dataRec_);
				//LOG(ERROR) <<"dataSize:";
				gcpLogDataRecord(LOG(ERROR) << "Space>> ", &_spaceRecord->dataRec_);


				LOG(ERROR) << "atomic hash-- " << it.first <<"; cntLive= " <<
						_cntRecord->atomicDataRec_.cntLive << "; cntTotal: " <<
						_cntRecord->atomicDataRec_.cntTotal << "; pcntLive= " <<
						_cntRecord->atomicDataRec_.pcntLive << "; pcntTotal= " <<
						_cntRecord->atomicDataRec_.pcntTotal;
		}
	}

	LOG(ERROR) << "Aclaculated>>  cntLive: "
			<< _cntLive << "; cntTotal: " << _cntTotal << "pcntLive:" <<
			_pcntLive << "pcntTotal: " <<_pcnTotal <<
			"atomic pcntLive = " << _pcntAtomicLive << "; atomic pcntTotal = " <<
			_pcntAtomicTotal;

	LOG(ERROR) << "=============== PRinting Class Names ==============";
	printClassNames();
}
//

void GCClassTableManager::calculateAtomicPercentiles(void) {
	GCPPairHistogramRecords* _globalHolder =
			(GCPPairHistogramRecords*)histData_;
//	GCPHistogramRecAtomic* _globalCntAtomicRec =
//			&_globalHolder->countData_.atomicDataRec_;
//	GCPHistogramRecAtomic* _globalSizeAtomicRec =
//			&_globalHolder->sizeData_.atomicDataRec_;
//	bool _safeCounts = true;
//	bool _safeSpace = true;
//	if(_globalCntAtomicRec->cntLive == 0 || _globalCntAtomicRec->cntTotal == 0) {
//		_safeCounts = false;
//	}
//	if(_globalSizeAtomicRec->cntLive == 0 || _globalSizeAtomicRec->cntTotal == 0) {
//		_safeSpace = false;
//	}
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		_rec->gcpPairUpdateAtomicPercentiles(_globalHolder/*, _safeCounts, _safeSpace*/);
	}
}



void GCClassTableManager::calculatePercentiles(void) {
	GCPPairHistogramRecords* _globalHolder =
			(GCPPairHistogramRecords*)histData_;
	GCPPairHistogramRecords* _recData = NULL;
	//LOG(ERROR) << "GCClassTable::calculatePercentiles::";
	//LOG(ERROR) << "Counts record:";
	//gcpLogDataRecord(LOG(ERROR), _globalHolder->countData_.gcpGetDataRecP());
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		_recData = (GCPPairHistogramRecords*) it.second;
		_recData->gcpPairUpdatePercentiles(_globalHolder);
	}
}


bool GCClassTableManager::dumpClassCntHistograms(art::File* dumpFile,
		bool dumpGlobalRec) {
	if(dumpGlobalRec) {
		GCPPairHistogramRecords* _record = (GCPPairHistogramRecords*) histData_;
		_record->countData_.gcpDumpHistRec(dumpFile);
	}
	bool _dataWritten = false;
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		_dataWritten = _rec->countData_.gcpDumpHistRec(dumpFile);
		if(!_dataWritten)
			break;
	}
	if(_dataWritten) {
		return VMProfiler::GCPDumpEndMarker(dumpFile);
	}
	return false;
}


bool GCClassTableManager::dumpClassAtomicCntHistograms(art::File* dumpFile) {
	bool _dataWritten = false;
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		_dataWritten = _rec->countData_.gcpDumpAtomicHistRec(dumpFile);
		if(!_dataWritten)
			break;
	}
	if(_dataWritten) {
		return VMProfiler::GCPDumpEndMarker(dumpFile);
	}
	return false;
}

bool GCClassTableManager::dumpClassSizeHistograms(art::File* dumpFile,
		bool dumpGlobalRec) {
	if(dumpGlobalRec) {
		GCPPairHistogramRecords* _record = (GCPPairHistogramRecords*) histData_;
		_record->sizeData_.gcpDumpHistRec(dumpFile);
	}
	bool _dataWritten = false;
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		_dataWritten = _rec->sizeData_.gcpDumpHistRec(dumpFile);
		if(!_dataWritten)
			break;
	}
	if(_dataWritten) {
		return VMProfiler::GCPDumpEndMarker(dumpFile);
	}
	return false;
}

bool GCClassTableManager::dumpClassAtomicSizeHistograms(art::File* dumpFile) {
	bool _dataWritten = false;
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		_dataWritten = _rec->sizeData_.gcpDumpAtomicHistRec(dumpFile);
		if(!_dataWritten)
			break;
	}
	if(_dataWritten) {
		return VMProfiler::GCPDumpEndMarker(dumpFile);
	}
	return false;
}



bool GCClassTableManager::gcpDumpManagedData(art::File* dumpFile,
		bool dumpGlobalRec) {
	bool _success = dumpClassCntHistograms(dumpFile, dumpGlobalRec);
	_success &= dumpClassAtomicCntHistograms(dumpFile);
	_success &= dumpClassSizeHistograms(dumpFile, dumpGlobalRec);
	_success &= dumpClassAtomicSizeHistograms(dumpFile);
	return _success;
}




bool GCClassTableManager::gcpDumpSummaryManagedData(art::File* dumpFile) {
	return dumpClassCntHistograms(dumpFile, false);
}


void GCClassTableManager::gcpZeorfyAllAtomicRecords(void) {
	((GCPPairHistogramRecords*)histData_)->gcpZerofyPairHistAtomicRecData();
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		_rec->gcpZerofyPairHistAtomicRecData();
	}
}

void GCClassTableManager::gcpFinalizeProfileCycle(void) {

  uint64_t _newCohortIndex = VMProfiler::GCPCalcCohortIndex();
  if(_newCohortIndex != GCPGetLastManagedCohort()) {
  	gcpZeorfyAllAtomicRecords();
  	GCPSetLastManagedCohort(_newCohortIndex);
  }
}


void GCClassTableManager::printClassNames(void) {
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;

		mirror::Class* _klass = _rec->getClassP();
		if(_klass == NULL)
			LOG(ERROR) << "XXXX NULL Hash: " << it.first;
		else {
			gcpLogDataRecord(LOG(ERROR)<< "Name: " <<
					_rec->getRefrenecePrettyName(),
					_rec->countData_.gcpGetDataRecP());
		}
//		mirror::Class* _klass = Runtime::Current()->GetClassLinker()->
//		_rec->gcpZerofyPairHistAtomicRecData();
	}
}


bool GCClassTableManager::gcpDumpCSVGlobalDataSummary(
		std::ostringstream& outputStream) {
	GCPPairHistogramRecords* pairData =
			(GCPPairHistogramRecords*) histData_;
	outputStream << "TotalAllocObjects:" <<
			StringPrintf("%.0f", pairData->countData_.dataRec_.cntTotal) <<
			"; TotalAllocSpace:" <<
			StringPrintf("%.0f", pairData->sizeData_.dataRec_.cntTotal) <<
			"; LoadedClasses:" <<
			StringPrintf("%zd",
					Runtime::Current()->GetInternTable()->classTableProf_.size()) << "\n";
	return true;
}

bool GCClassTableManager::gcpDumpCSVCoreTables(std::ostringstream& outputStream) {
	for (const std::pair<uint64_t, mprofiler::GCPHistRecData*>& it :
			Runtime::Current()->GetInternTable()->classTableProf_) {
		mprofiler::GCPPairHistogramRecords* _rec =
				(GCPPairHistogramRecords*) it.second;
		GCPHistogramRec* countP = _rec->countData_.gcpGetDataRecP();
		GCPHistogramRec* sizeP = _rec->sizeData_.gcpGetDataRecP();
		outputStream << "clzIndex:" << StringPrintf("%llu", countP->index) <<
				"; TotalObjsCnt:" <<  	StringPrintf("%.0f", countP->cntTotal) <<
				"; TotalSpace:" <<  	StringPrintf("%.0f", sizeP->cntTotal) <<
				"; name:" << _rec->getRefrenecePrettyName() << "\n";
	}
	return true;
}


}//mprofiler namespace
}//namespace art
