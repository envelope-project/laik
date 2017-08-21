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

static int aIter;
static int fail_iter;
static int fail_task;
static int isInited = 0;

static char* failed_nodes[1];
static char* st_fail_node = "1";
static const int n_fail_node = 1;

static
void static_agent_detach (
    laik_agent* this
){
    return;
}
static 
void static_agent_setiter(
    laik_agent* this,
    const int iter
){
    assert(isInited);
    (void)this;
    aIter = iter;
}
static
void static_agent_setphase(
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

/* this message need to be freed by the user!!!! */
static
void static_agent_getfailed(
    laik_agent* this, 
    int* n_failed,
    char*** l_failed
){
    assert(isInited);
    (void)this;
    
    failed_nodes[0] = st_fail_node;
    
    if(aIter<fail_iter){
        return;
    }


    //FIXME: Mem Leak. 
    *l_failed = failed_nodes;
    *n_failed = n_fail_node;
}

static
int static_agent_peek(
    laik_agent* this
){
    assert(isInited);
    (void) this;
    if(aIter<fail_iter){
        return 0;
    }
    return n_fail_node;
}


laik_ext_errno static_agent_init(
    int argc, 
    char** argv
){
    assert(argc == 2);
    assert(argv);
    assert(argv[0]);

    fail_iter = atoi(argv[0]);

    isInited = 1;
    return LAIK_AGENT_ERRNO_SUCCESS;
}