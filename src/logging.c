/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017-2019 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <laik-internal.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// default log level
static int laik_loglevel = LAIK_LL_Error;
// file descriptor to write to instead of stderr
static FILE* laik_logfile = NULL;
// formatting choice:  // 0: none, 1: short, 2:long
static int laik_logprefix = 2;
// time of initialization, may be synced by backends
static struct timeval laik_log_init_time;
// active instance
static Laik_Instance* laik_loginst = 0;
// without instance, use a location as context (laik_log_init_loc)
static char* laik_log_mylocation = 0;
static int laik_logctr = 0;
// filter
static int laik_log_fromLID = -1;
static int laik_log_toLID = -1;

void laik_log_init_internal()
{
    static bool init_done = false;
    if (init_done) return;
    init_done = true;

    gettimeofday(&laik_log_init_time, NULL);

    char* str = getenv("LAIK_LOG");
    if (str) {
        if (*str == 'n') { laik_logprefix = 0; str++; }
        if (*str == 's') { laik_logprefix = 1; str++; }

        int l = atoi(str);
        if (l > 0)
            laik_loglevel = l;
        else {
            // exit with some help text
            fprintf(stderr, "Unknown LAIK_LOG syntax. Use\n\n"
                            "    LAIK_LOG=[option]level[:locID[-toID]]\n\n"
                            " option : logging option (characters, defaults to none)\n"
                            "            n - no line prefix\n"
                            "            s - use short prefix\n"
                            " level  : minimum logging level (digit, defaults to 0: no logging)\n"
                            " locID  : only log if process has given location ID (number, default: no filter)\n"
                            " toID   : allow logging for range of location IDs [locID;toID] (number)\n");
            exit(1);
        }
        char* p = index(str, ':');
        if (p) {
            p++;
            laik_log_fromLID = atoi(p);
            p = index(p, '-');
            if (p) {
                p++;
                laik_log_toLID = atoi(p);
            }
            else
                laik_log_toLID = laik_log_fromLID;
        }
    }

    str = getenv("LAIK_LOG_FILE");
    if(str){
        laik_logfile = freopen(str, "a+", stdout);
        if(!laik_logfile){
            laik_log(LAIK_LL_Error, "Cannot Initialize File for print output.\n");
        }
        stderr = laik_logfile;
        stdout = laik_logfile;
    }

}

// initialize logging for instance <instance>
// TODO: enable for multiple instances
void laik_log_init(Laik_Instance* i)
{
    assert(laik_loginst == 0);
    laik_loginst = i;

    // if early-log location set: free it
    if (laik_log_mylocation) {
        free(laik_log_mylocation);
        laik_log_mylocation = 0;
    }

    laik_log_init_internal();
}

// call this function at beginning of backend initilization,
// before a full backend instance is created, to get log output
void laik_log_init_loc(char* mylocation)
{
    laik_log_mylocation = strdup(mylocation);

    laik_log_init_internal();
}

// cleanup logging of instance <i>
void laik_log_cleanup(Laik_Instance* i)
{
    if (i) {
        if (laik_loginst != i) return;
    }

    laik_log_flush(0);

    if (laik_logfile)
        fclose(laik_logfile);
}

// reset start time for log output
void laik_log_set_time(struct timeval* t)
{
    laik_log_init_time = *t;
}

// to overwrite environment variable LAIK_LOG
void laik_set_loglevel(int l)
{
    laik_loglevel = l;
}

// check for log level: return true if given log level will be shown
bool laik_log_shown(int l)
{
    return (l >= laik_loglevel);
}

/* Log a message, similar to printf
 *
 * By default, a prefix is added which allows sorting to get stable output
 * from the arbitrarily interleaved output of multiple MPI tasks:
 *
 * == LAIK-<logctr>-T<task> <itermsgctr>.<line> <wtime>
 *
 * logctr : counter incremented at iteration/phase borders
 * task   : task rank in this LAIK instance
 * msgctr : log message counter, reset at each logctr change
 * line   : a line counter if a log message consists of multiple lines
 * wtime  : wall clock time since LAIK instance initialization
 *
 * To build the message step by step:
 * - start: laik_log_begin(<level>)
 * - optionally multiple times: laik_log_append(<msg>, ...)
 * - end with laik_log_flush(<msg>, ...)
 *
 * Or just use log(<level>, <msg>, ...) which internally uses above functions
*/

// buffered logging, not thread-safe

static int current_logLevel = LAIK_LL_None;
static char* current_logBuffer = 0;
static int current_logSize = 0;
static int current_logPos = 0;

bool laik_log_begin(int l)
{
    // if nothing should be logged, set level to none and return
    if (l < laik_loglevel) {
        current_logLevel = LAIK_LL_None;
        return false;
    }
    if ((laik_log_fromLID >= 0) && (laik_loginst != 0)) {
        assert(laik_log_toLID >= laik_log_fromLID);
        if ((laik_loginst->mylocationid < laik_log_fromLID) ||
            (laik_loginst->mylocationid > laik_log_toLID)) {
            current_logLevel = LAIK_LL_None;
            return false;
        }
    }
    current_logLevel = l;

    current_logPos = 0;
    if (current_logBuffer == 0) {
        // init: start with 1k buffer
        current_logBuffer = malloc(1024);
        assert(current_logBuffer); // cannot call laik_panic
        current_logSize = 1024;
    }
    return true;
}

static
void log_append(const char *format, va_list ap)
{
    if (current_logLevel == LAIK_LL_None) return;

    // to be able to do a 2nd pass over ap (if buffer is too small)
    va_list ap2;
    va_copy(ap2, ap);

    int left, len;
    left = current_logSize - current_logPos;
    assert(left > 0);
    len = vsnprintf(current_logBuffer + current_logPos, left,
                    format, ap);

    // does it fit into buffer? (len is without terminating zero byte)
    if (len >= left) {
        int size = 2 * current_logSize;
        if (size < len + 1) size = len + 1;
        current_logBuffer = realloc(current_logBuffer, size);
        current_logSize = size;
        // printf("Enlarging log buffer to %d bytes ...\n", size);

        // print again into enlarged buffer - must fit
        left = current_logSize - current_logPos;
        len = vsnprintf(current_logBuffer + current_logPos, left,
                        format, ap2);
        assert(len < left);
    }

    current_logPos += len;
}

void laik_log_append(const char* msg, ...)
{
    if (current_logLevel == LAIK_LL_None) return;

    va_list args;
    va_start(args, msg);
    log_append(msg, args);
    va_end(args);
}

// increment logging counter used in prefix
void laik_log_inc()
{
    laik_logctr++;
}

static
void log_flush()
{
    if (current_logLevel == LAIK_LL_None) return;
    if ((current_logPos == 0) || (current_logBuffer == 0)) return;

    const char* lstr = 0;
    switch(current_logLevel) {
    case LAIK_LL_Warning: lstr = "Warning"; break;
    case LAIK_LL_Error:   lstr = "ERROR"; break;
    case LAIK_LL_Panic:   lstr = "PANIC"; break;
    default: break;
    }

    // counters for stable output
    static int counter = 0;
    static int last_logctr = 0;
    int line_counter = 0;
    if (last_logctr != laik_logctr) {
        counter = 0;
        last_logctr = laik_logctr;
    }
    counter++;

#define LINE_LEN 100
    // enough for prefix plus one line of log message
    static char buf2[150 + LINE_LEN];
    int off1 = 0, off, off2;

    char* buf1 = current_logBuffer;

    int spaces = 0, last_break = 0;
    bool at_newline = true;

    struct timeval now;
    gettimeofday(&now, NULL);
    double wtime = (double)(now.tv_sec - laik_log_init_time.tv_sec) +
                   0.000001 * (now.tv_usec - laik_log_init_time.tv_usec);
    int wtime_min = (int) (wtime/60.0);
    double wtime_s = wtime - 60.0 * wtime_min;

    // append prefix at beginning of each line of msg
    while(buf1[off1]) {

        // prefix to allow sorting of log output
        // sorting makes chunks from output of each MPI task
        line_counter++;
        off2 = sprintf(buf2, "%s ", (line_counter == 1) ? "==" : "..");
        if (laik_loginst == 0) {
            off2 += sprintf(buf2+off2, "%-7s: ",
                            laik_log_mylocation ? laik_log_mylocation : "");

        }
        else {
            if (laik_logprefix == 1)
                off2 += sprintf(buf2+off2, "L%02d | ", laik_loginst->mylocationid);
            else if (laik_logprefix == 2)
                off2 += sprintf(buf2+off2,
                                "LAIK-%04d-L%02d %04d.%02d %2d:%06.3f | ",
                                laik_logctr, laik_loginst->mylocationid,
                                counter, line_counter,
                                wtime_min, wtime_s);
        }
        if (lstr)
            off2 += sprintf(buf2+off2, "%-7s: ",
                            (line_counter == 1) ? lstr : "");

        // line of message

        if (at_newline) {
            // get indent
            spaces = 0;
            while(buf1[off1] == ' ') { off1++; spaces++; }
        }

        // indent: add 4 spaces if this is continuation line
        off2 += sprintf(buf2+off2, "%*s",
                        at_newline ? spaces : spaces + 4, "");

        at_newline = false;
        off = off1;

        last_break = 0;
        while(buf1[off]) {
            if (buf1[off] == '\n') {
                at_newline = true;
                break;
            }
            if (buf1[off] == ' ') {
                // break line if too long?
                if (spaces + (off - off1) > LINE_LEN) {
                    if (last_break)
                        off = last_break; // go back
                    break;
                }
                last_break = off;
            }
            off++;
        }
        if (buf1[off]) buf1[off++] = 0;
        off2 += sprintf(buf2+off2, "%s\n", buf1 + off1);
        off1 = off;

        assert(off2 < 150 + LINE_LEN);

        // TODO: allow to go to debug file
        fprintf(stderr, "%s", buf2);
    }
    current_logPos = 0;

    // stop program on panic with failed assertion
    if (current_logLevel == LAIK_LL_Panic) assert(0);
}

void laik_log_flush(const char* msg, ...)
{
    if (current_logLevel == LAIK_LL_None) return;

    if (msg) {
        va_list args;
        va_start(args, msg);
        log_append(msg, args);
        va_end(args);
    }

    log_flush();
}

void laik_log(int l, const char* msg, ...)
{
    if (!laik_log_begin(l)) return;

    va_list args;
    va_start(args, msg);
    log_append(msg, args);
    va_end(args);

    log_flush();
}

// panic: terminate application
void laik_panic(const char* msg)
{
    laik_log(LAIK_LL_Panic, "%s", msg);
}
