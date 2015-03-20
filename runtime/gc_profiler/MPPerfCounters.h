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


class PerfEventLogger {
 public:
  // Splits are nanosecond times and split names.
  typedef std::pair<int32_t, uint64_t> EventReading;
  typedef std::vector<EventReading> EventReadings;

  EventReadings events;
  GCPauseThreadMarker eventMarkers[GCMMP_GC_BRK_MAXIMUM];
  uint64_t eventAccMarkers[GCMMP_GC_BRK_MAXIMUM];
  void addEvents(int32_t, uint64_t);
  void addStartMarkEvent(GCPauseThreadMarker, uint64_t);
  uint64_t addEndMarkEvent(GCPauseThreadMarker, uint64_t);
  explicit PerfEventLogger(void);


};

class MPPerfCounter {
public:
	static const bool kGCPerfCountersExcIdle = true;
	static const int 	kGCPerfCountersNameSize = 16;
	PerfLibCounterT*  hwCounter;
	const char* event_name_;
	PerfEventLogger evtLogger;
	u64 data;


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
};

} //namespace mprofiler
} //namespace art

#endif /* MPPERFCOUNTERS_H_ */
