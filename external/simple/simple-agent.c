/**
 * File: /Users/dai/play/laik/external/static/static-agent.c
 * Created Date: Thu Jul 20 2017
 * Author: Dai Yang
 * -----
 * Last Modified: Sat Jul 22 2017
 * Modified By: Dai Yang
 * -----
 * Copyright (c) 2017 IfL LRR/I10, TU MUENCHEN
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 */

#include "simple-agent.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

static int aIter;
static int fail_iter;
static int fail_task;
static int isInited = 0;

void agent_detach (
    laik_agent* this
){
    (void) this;
}

int agent_clear_alarm (
    laik_agent* agent
){
    (void) agent;
    fail_iter = INT_MAX;
    return 0;
}

void agent_set_iter(
    laik_agent* this,
    const int iter
){
    assert(isInited);
    (void)this;
    aIter = iter;
}

void agent_set_phase(
    laik_agent* this, 
    const int num_phase,
    const char* name_phase, 
    const void* pData
){
    assert(isInited);
    (void) this;
    (void) num_phase;
    (void) name_phase;
    (void) pData;
}

void agent_get_failed(
    laik_agent* this, 
    int* n_failed,
    char* l_failed
){
    assert(isInited);
    (void)this;
    if(aIter<fail_iter){
        return;
    }
    sprintf(l_failed, "%d", fail_task);
    *n_failed = 1;

}

int agent_peek_failed(
    laik_agent* this
){
    assert(isInited);
    (void) this;
    if(aIter<fail_iter){
        return 0;
    }
    return 1;
}


laik_ext_errno agent_init(
    int argc, 
    char** argv
){
    assert(argc == 2);
    assert(argv);
    assert(argv[0]);
    assert(argv[1]);

    fail_iter = atoi(argv[0]);
    fail_task = atoi(argv[1]);

    isInited = 1;
    return LAIK_AGENT_ERRNO_SUCCESS;
}