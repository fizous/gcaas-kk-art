/*
 * MProfilerHeap.h
 *
 *  Created on: Jun 21, 2015
 *      Author: hussein
 */

#ifndef ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_
#define ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_

#define DVM_ALLOW_GCPROFILER			0

/******************************************************************************
 * 																Macro Definitions
 *
 *
 ******************************************************************************/
/** define the type of the collection **/
#define GCP_MAX_HISTOGRAM_SIZE						32 /* number of buckets to create the histogram */
#define GCP_DISABLE_CONC_COLLECT					1 /* turn off ConcurrentGC */
#define GCP_DISABLE_EXPL_COLLECT					1 /* turn off explicit GC */

#if DVM_ALLOW_GCPROFILER
#define GCP_DECLARE_ADD_ALLOC(x)			  (gcpAddObject(x))
#define GCP_DECLARE_REMOVE_ALLOC(x)			(gcpRemoveObject(x))
#else//if DVM_ALLOW_GCPROFILER

#define GCP_DECLARE_ADD_ALLOC(x)			((void) 0)
#define GCP_DECLARE_REMOVE_ALLOC(x)		((void) 0)


#endif//if DVM_ALLOW_GCPROFILER


#define GCP_OFF_CONCURRENT_GC()			(GCP_DISABLE_CONC_COLLECT & DVM_ALLOW_GCPROFILER)
#define GCP_OFF_EXPLICIT_GC()			(GCP_DISABLE_EXPL_COLLECT & DVM_ALLOW_GCPROFILER)

#endif /* ART_RUNTIME_GC_PROFILER_MPROFILERHEAP_H_ */
