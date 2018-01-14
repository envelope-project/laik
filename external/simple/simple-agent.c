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

void sa_reset(
    void
){
    
}

void sa_detach (
    void
){

}

int sa_clear (
    void
){
    fail_iter = INT_MAX;
    return 0;
}

void sa_set_iter(
    const int iter
){
#ifdef __DEBUG__
    printf("Simple Agent: set_iter, iter = %d \n", iter);    
#endif
    aIter = iter;
}

void sa_getfailed(
    int* n_failed,
    node_uid_t** l_failed
){
    char* buf = (*l_failed[0]).uid;
#ifdef __DEBUG__
    printf("Simple Agent: Get Failed, fail_iter = %d, aIter = %d\n", fail_iter, aIter);    
#endif
    if(aIter<fail_iter){
        *n_failed = 0;
        return;
    }
#ifdef __DEBUG__
    printf("Simple Agent: True Failed, task: %d, iter = %d\n", fail_task, fail_iter);
#endif

    sprintf(buf, "%d", fail_task);
    *n_failed = 1;
}

int sa_peekfailed(
    void
){
    if(aIter<fail_iter){
        return 0;
    }
    return 1;
}

Laik_Agent* agent_init(
    int argc, 
    char** argv
){
    assert(argc == 2);
    assert(argv);
    assert(argv[0]);
    assert(argv[1]);

    fail_iter = atoi(argv[0]);
    fail_task = atoi(argv[1]);

    Laik_Ft_Agent* me = (Laik_Ft_Agent*)
            calloc (1, sizeof(Laik_Ft_Agent));
    assert(me);
    Laik_Agent* myBase = &(me->base);

    myBase->name = "Simple Agent";
    myBase->id = 0x01;
    myBase->isAlive = 1;
    myBase->isInitialized = 1;
    myBase->type = LAIK_AGENT_FT;

    myBase->detach = sa_detach;
    myBase->reset = sa_reset;

    me->getfail = sa_getfailed;
    me->peekfail = sa_peekfailed;
    me->setiter = sa_set_iter;
#ifdef __DEBUG__
    printf("Simple Agent: Init done, fail_iter = %d, fail_task = %d\n", fail_iter, fail_task);
#endif
    isInited = 1;
    return (Laik_Agent*) me;
}
