#include "laik-internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

// Profiling

static Laik_Instance* laik_profinst = 0;
extern char* __progname;

Laik_Profiling_Controller* laik_init_profiling(
    void
){
    Laik_Profiling_Controller* ctrl = (Laik_Profiling_Controller*) 
            calloc(1, sizeof(Laik_Profiling_Controller));

    return ctrl;
}

void laik_free_profiling(
    Laik_Instance* i
){
    free(i->profiling);
}

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

void laik_profile_user_stop(Laik_Instance* i)
{
    if (laik_profinst) {
        if (laik_profinst == i) {
            if (i->profiling->do_profiling) {
                if (i->profiling->user_timer_active) {
                    i->profiling->time_user = laik_wtime() -  i->profiling->timer_user;
                    i->profiling->timer_user = 0.0;
                    i->profiling->user_timer_active = 0;
                }
            }
        }
    }
}

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

double laik_get_total_time()
{
    if (!laik_profinst) return 0.0;

    return laik_profinst->profiling->time_total;
}

double laik_get_backend_time()
{
    if (!laik_profinst) return 0.0;

    return laik_profinst->profiling->time_backend;
}

void laik_writeout_profile()
{
    if (!laik_profinst) return;
    if (!laik_profinst->profiling->profile_file) return;
    //backend-id, phase, iteration, time_total, time_ackend, user_time
    fprintf( (FILE*)laik_profinst->profiling->profile_file,
             "%s, %d, %d, %f, %f, %f\n",
             laik_profinst->guid,
             laik_profinst->control->cur_phase, laik_profinst->control->cur_iteration,
             laik_profinst->profiling->time_total, laik_profinst->profiling->time_backend,
             laik_profinst->profiling->time_user
            );
}

void laik_close_profiling_file(Laik_Instance* i)
{
    if (i->profiling->profile_file != NULL) {
        fprintf((FILE*)i->profiling->profile_file, "======MEASUREMENT END AT: %lu======\n",
                (unsigned long) time(NULL));
        fclose(i->profiling->profile_file);
        i->profiling->profile_file = NULL;
    }
}

void laik_profile_printf(const char* msg, ...)
{
    if(laik_profinst->profiling->profile_file){
        va_list args;
        va_start(args, msg);
        vfprintf((FILE*)laik_profinst->profiling->profile_file, msg, args);
    }
}

