/*
 * MPPerfCounters.h
 *
 *  Created on: Sep 9, 2014
 *      Author: hussein
 */

#ifndef MPPERFCOUNTERS_H_
#define MPPERFCOUNTERS_H_

#include <linux/perf_event.h>
#include <linux/types.h>


typedef struct PerfLibS
{
	int     cpus;   			/* What cpu to monitor */
	u32     pid;        	/* what pid to monitor 0=self */
	char    *event_name; 	/* Name of event matching with whats on qwiki */
	void		*attrAddress; /* address of the profile attribute in the perfcounter struct */

	/*
	 * Perf uses file descriptors to read the performance counters.
   * This is the file descriptor for the counter.
	 */
	int             fileDescriptor;

} PerfLibCounterT;


int init_perflib_counters(void);


#endif /* MPPERFCOUNTERS_H_ */
