#include "laik-internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>

/**
 * Application controlled profiling
 *
 * Stop compute/LAIK times, and optionally write them
 * as one record (= one line) into a CSV file when requested.
 *
 * Currently, LAIK is implemented in such a way that we
 * expect most time to be spent within "laik_switchto".
 * This covers:
 * - triggering a partitioner run if not yet done for
 *   the partitioning we will switch to
 * - calculating the transition between old and new
 *   partitioning and data flow
 * - executing the transition with the help of a backend
 * If in the future, there are other relevant time spans
 * required by LAIK, instrumentation must be updated.
 *
 * We collect two LAIK-related time spans:
 * - how much time is spent in "laik_switchto"?
 *   Approximately, this should be the time spent in LAIK.
 *   Thus, it is called "LAIK total time"
 * - how much time is spent within the backend?
 *   This is part of LAIK total time, used for executing
 *   transitions and synchronizing the KV-store
 *
 * In addition, the application can specify time spans as
 * "user time", by surrounding code to measure with calls
 * laik_profile_user_start/laik_profile_user_stop.
 *
 * Functions are provided to get measured times.
 * In output-to-file mode, times are written out when
 * requested.
 *
 * API DISCUSSION:
 * - API suggests that we can profile per LAIK instance, but
 *   profiling can be active only for one instance?!
 * - ensure user time to be mutual exclusive to LAIK times
 * - how to make this usable for automatic load balancing?
 *   (connect absolute times (!) with a partitioning to modify)
 * - global user time instead of per-LAIK-instance user times
 * - control this from outside (environment variables)
 * - keep it usable also for production mode (too much
 *   instrumentation destroys measurements anyway, so
 *   better keep overhead low)
 * - enable automatic measurement output on iteration/phase change
 * - PAPI counters
 * - combine this with sampling to become more precise
 *   without higher overhead
*/

static Laik_Instance* laik_profinst = 0;
extern char* __progname;

// called by laik_init
Laik_Profiling_Controller* laik_init_profiling(void)
{
    Laik_Profiling_Controller* ctrl = (Laik_Profiling_Controller*) 
            calloc(1, sizeof(Laik_Profiling_Controller));

    return ctrl;
}

//Time Measurement Funcitonality
double laik_realtime(){
    struct timeval tv;
    gettimeofday(&tv, 0);

    return tv.tv_sec+1e-6*tv.tv_usec;
}

// NOTE: See Discussion
// https://stackoverflow.com/questions/6498972/faster-equivalent-of-gettimeofday
// The CLOCK_MONOTONIC_COARSE is way faster but delivers a significant lower precision 
double laik_fast_realtime(){
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &tv);
    return (double)tv.tv_sec+(double)1e-9*tv.tv_nsec;
}

double laik_cputime(){
    clock_t clk = clock();
    return (double)clk/CLOCKS_PER_SEC;
}

double laik_wtime()
{
  return laik_realtime();
}

// called by laik_finalize
// FIXME: should close any open file if output-to-file is enabled
void laik_free_profiling(Laik_Instance* i)
{
    free(i->profiling);
}

// start profiling measurement for given instance
// FIXME: why not globally enable/disable profiling?
void laik_enable_profiling(Laik_Instance* i)
{
    if (laik_profinst) {
        if (laik_profinst == i) return;
        laik_profinst->profiling->do_profiling = false;
    }
    laik_profinst = i;
    if (!i) return;

    i->profiling->do_profiling = true;
    i->profiling->time_backend = 0.0;
    i->profiling->time_total = 0.0;
    i->profiling->time_user = 0.0;
}

// reset measured time spans
void laik_reset_profiling(Laik_Instance* i)
{
    if (laik_profinst) {
        if (laik_profinst == i) {
            if (i->profiling->do_profiling) {
                i->profiling->do_profiling = true;
                i->profiling->time_backend = 0.0;
                i->profiling->time_total = 0.0;
                i->profiling->time_user = 0.0;
            }
        }
    }
}

// start user-time measurement
void laik_profile_user_start(Laik_Instance* i)
{
    if (laik_profinst) {
        if (laik_profinst == i) {
            if (i->profiling->do_profiling) {
                i->profiling->timer_user = laik_wtime();
                i->profiling->user_timer_active = 1;
            }
        }
    }
}

// stop user-time measurement
void laik_profile_user_stop(Laik_Instance* i)
{
    if (laik_profinst) {
        if (laik_profinst == i) {
            if (i->profiling->do_profiling) {
                if (i->profiling->user_timer_active) {
                    i->profiling->time_user = laik_wtime() -
                                              i->profiling->timer_user;
                    i->profiling->timer_user = 0.0;
                    i->profiling->user_timer_active = 0;
                }
            }
        }
    }
}

// enable output-to-file mode for use of laik_writeout_profile()
void laik_enable_profiling_file(Laik_Instance* i, const char* filename)
{
    if (laik_profinst) {
        if (laik_profinst == i) return;
        laik_profinst->profiling->do_profiling = false;
    }

    laik_profinst = i;
    if (!i) return;

    i->profiling->do_profiling = true;
    i->profiling->time_backend = 0.0;
    i->profiling->time_total = 0.0;
    snprintf(i->profiling->filename, MAX_FILENAME_LENGTH, "t%s.%s", i->guid, filename);
    i->profiling->profile_file = fopen(filename, "a+");
    if (i->profiling->profile_file == NULL) {
        laik_log(LAIK_LL_Error, "Unable to start file based profiling");
    }

    fprintf((FILE*)i->profiling->profile_file, "======MEASUREMENT START AT: %lu======\n", 
            (unsigned long) time(NULL));

    fprintf((FILE*)i->profiling->profile_file, "======Application %s======\n", 
            __progname);

}

// get LAIK total time for LAIK instance for which profiling is enabled
double laik_get_total_time()
{
    if (!laik_profinst) return 0.0;

    return laik_profinst->profiling->time_total;
}

// get LAIK backend time for LAIK instance for which profiling is enabled
double laik_get_backend_time()
{
    if (!laik_profinst) return 0.0;

    return laik_profinst->profiling->time_backend;
}

// for output-to-file mode, write out meassured times
// This is done for the LAIK instance which currently is enabled.
// FIXME: why not reset timers? We never want same time span to appear in
//        multiple lines of the output file?!
void laik_writeout_profile()
{
    if (!laik_profinst) return;
    if (!laik_profinst->profiling->profile_file) return;
    //backend-id, phase, iteration, time_total, time_ackend, user_time
    fprintf( (FILE*)laik_profinst->profiling->profile_file,
             "%s, %d, %d, %f, %f, %f\n",
             laik_profinst->guid,
             laik_profinst->control->cur_phase,
             laik_profinst->control->cur_iteration,
             laik_profinst->profiling->time_total,
             laik_profinst->profiling->time_backend,
             laik_profinst->profiling->time_user
            );
}

// disable output-to-file mode, eventually closing yet open file before
void laik_close_profiling_file(Laik_Instance* i)
{
    if (i->profiling->profile_file != NULL) {
        fprintf((FILE*)i->profiling->profile_file, "======MEASUREMENT END AT: %lu======\n",
                (unsigned long) time(NULL));
        fclose(i->profiling->profile_file);
        i->profiling->profile_file = NULL;
    }
}

// print arbitrary text to file in output-to-file mode
void laik_profile_printf(const char* msg, ...)
{
    if (laik_profinst->profiling->profile_file) {
        va_list args;
        va_start(args, msg);
        vfprintf((FILE*)laik_profinst->profiling->profile_file, msg, args);
    }
}

