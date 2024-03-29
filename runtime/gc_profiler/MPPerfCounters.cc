/*
 * MPPerfCounters.cc
 *
 *  Created on: Sep 9, 2014
 *      Author: hussein
 */

#include <string>
#include <vector>
#include <map>
#include <stdint.h>
#include "cutils/perflib.h"
#include "os.h"
#include "runtime.h"
#include "thread.h"
#include "gc_profiler/MProfilerTypes.h"
#include "gc_profiler/MPPerfCounters.h"
#include "gc_profiler/MProfiler.h"


namespace art {
namespace mprofiler {


void PerfEventLogger::addStartMarkEvent(GCMMP_BREAK_DOWN_ENUM evt, uint64_t val) {
	(eventMarkers[evt]).startMarker = val;
}

uint64_t PerfEventLogger::addEndMarkEvent(GCMMP_BREAK_DOWN_ENUM evt, uint64_t val){
	uint64_t _diff = 0;
	eventMarkers[evt].finalMarker = val;
	if(eventMarkers[evt].startMarker > 0) {
		_diff = val - eventMarkers[evt].startMarker;
		eventMarkers[evt].startMarker = 0;
	}
	eventMarkers[evt].finalMarker = 0;
	return _diff;
}

PerfEventLogger::PerfEventLogger(void) {
	for(int i = GCMMP_GC_BRK_NONE; i < GCMMP_GC_BRK_MAXIMUM; i++){
		GCMMP_BREAK_DOWN_ENUM valIter = static_cast<GCMMP_BREAK_DOWN_ENUM>(i);
		eventMarkers[valIter].type = valIter;
		eventMarkers[valIter].startMarker = 0;
		eventMarkers[valIter].finalMarker = 0;
		eventAccMarkers[valIter] = 0;
	}
}

void PerfEventLogger::dumpMarks(void) {
	for(int i = GCMMP_GC_BRK_NONE; i < GCMMP_GC_BRK_MAXIMUM; i++){
		GCMMP_BREAK_DOWN_ENUM valIter = static_cast<GCMMP_BREAK_DOWN_ENUM>(i);
    if(eventAccMarkers[valIter] > 0) {
    	LOG(ERROR) << "markedEvents: " << valIter << ", " << eventAccMarkers[valIter];
    }
	}
}

void PerfEventLogger::getGCMarks(uint64_t* contVal) {
	for(int i = GCMMP_GC_BRK_SUSPENSION; i < GCMMP_GC_BRK_MAXIMUM; i++){
		GCMMP_BREAK_DOWN_ENUM valIter = static_cast<GCMMP_BREAK_DOWN_ENUM>(i);
    if(eventAccMarkers[valIter] > 0) {
    	*contVal += eventAccMarkers[valIter];
    	//LOG(ERROR) << "markedEvents: " << valIter << ", " << eventAccMarkers[valIter];
    }
	}
}


void MPPerfCounter::getGCDataDistributions(uint64_t* totalVal,
		uint64_t* gcMutVal, uint64_t* gcDaemonVal) {
	*totalVal += data;
	*gcMutVal += gcAcc;
}




void PerfEventLogger::addEvents(uint64_t tag, uint64_t data) {
	events.push_back(EventReading(tag, data));
}

void MPPerfCounter::addStartEvent(GCMMP_BREAK_DOWN_ENUM evt){
	readPerfData();
	evtLogger.addStartMarkEvent(evt, data);
}

void MPPerfCounter::dumpMarks(void) {
	evtLogger.dumpMarks();
}

void MPPerfCounter::getGCMarks(uint64_t* val) {
	evtLogger.getGCMarks(val);
}

void MPPerfCounter::addEndEvent(GCMMP_BREAK_DOWN_ENUM evt){
	readPerfData();
	uint64_t _diff = evtLogger.addEndMarkEvent(evt, data);
	gcAcc += _diff;
	evtLogger.eventAccMarkers[evt] += _diff;
}

uint64_t MPPerfCounter::addEndEventNOSpecial(GCMMP_BREAK_DOWN_ENUM evt){
	readPerfData();
	uint64_t _diff = evtLogger.addEndMarkEvent(evt, data);
	noSpectialAcc += _diff;
	evtLogger.eventAccMarkers[evt] += _diff;
	return _diff;
}


MPPerfCounter::MPPerfCounter(void) :
		event_name_("CYCLES") {

}

MPPerfCounter::MPPerfCounter(const char* event_name)  {
	event_name_ = event_name;
	gcAcc = 0;
	data = 0;
	noSpectialAcc = 0;
}

MPPerfCounter* MPPerfCounter::Create(const char* event_name){
	return new MPPerfCounter(event_name);
}


void MPPerfCounter::readPerfData(void) {
	int _locRet = 0;
	_locRet = get_perf_counter(hwCounter, &data);
	if (_locRet < 0) {
		LOG(ERROR) << "Error reading event for tid: " << hwCounter->pid;
	}
}


void MPPerfCounter::storeReading(uint64_t tag) {
	readPerfData();
	evtLogger.addEvents(tag, data);
}

bool MPPerfCounter::ClosePerfLib(void) {
	int _locRet = 0;
	if(hwCounter != NULL) {

		int _locRet = close_perf_counter(hwCounter);

		if(_locRet < 0) {
			LOG(FATAL) << "could not close perflib for tid: " << hwCounter->pid;
		} else {
			GCMMP_VLOG(INFO) << "MPPerfCounters: closing for pids: " << hwCounter->pid;
		}
	}
	return _locRet == 0;
}
/*
 * Open perflib and process ID
 */
bool MPPerfCounter::OpenPerfLib(pid_t pid) {
	int _locRet = 0;
	//art::Thread* self = art::Thread::Current();
	hwCounter =
			(PerfLibCounterT*) calloc(1, sizeof(PerfLibCounterT));
	GCMMP_VLOG(INFO) << "MPPerfCounters: openPerfLib for event:" << event_name_;
	hwCounter->event_name = NULL;
	hwCounter->event_name =
			(char*) calloc(1, sizeof(char) * MPPerfCounter::kGCPerfCountersNameSize);
	strcpy(hwCounter->event_name, event_name_);

	_locRet = create_perf_counter(hwCounter);

	if (_locRet < 0) {
		LOG(FATAL) << "could not create perflib for tid: " << pid;
		return false;
	}
	hwCounter->pid = pid;
	_locRet = open_perf_counter(hwCounter);
	if (_locRet < 0) {
		LOG(FATAL) << "could not open perflib for tid: " << pid;
		return false;
	}
	set_exclude_idle(hwCounter, MPPerfCounter::kGCPerfCountersExcIdle);

	GCMMP_VLOG(INFO) << "MPPerfCounters: Finished creating the performance counters for tid:" << pid;
	return true;
}
}// namespace mprofiler
}// namespace art
