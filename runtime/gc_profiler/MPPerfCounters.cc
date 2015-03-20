/*
 * MPPerfCounters.cc
 *
 *  Created on: Sep 9, 2014
 *      Author: hussein
 */

#include <string>
#include <vector>
#include <map>
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
		eventMarkers[i].type = i;
		eventMarkers[i].startMarker = 0;
		eventMarkers[i].finalMarker = 0;
		eventAccMarkers[i] = 0;
	}
}

void PerfEventLogger::addEvents(int32_t tag, uint64_t data) {
	events.push_back(EventReading(tag, data));
}

void MPPerfCounter::addStartEvent(GCPauseThreadMarker evt){
	readPerfData();
	evtLogger.addStartMarkEvent(evt, data);
}

void MPPerfCounter::addEndEvent(GCPauseThreadMarker evt){
	readPerfData();
	evtLogger.eventAccMarkers[evt] += evtLogger.addEndMarkEvent(evt, data);
}

MPPerfCounter::MPPerfCounter(void) :
		event_name_("CYCLES") {

}

MPPerfCounter::MPPerfCounter(const char* event_name)  {
	event_name_ = event_name;
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


void MPPerfCounter::storeReading(int32_t tag) {
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
