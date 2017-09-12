/**
 * File: /Users/dai/play/laik/src/laik-external.c
 * Created Date: Thu Jul 20 2017
 * Author: Dai Yang
 * -----
 * Last Modified: Mon Jul 24 2017
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

#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "laik.h"

static int laik_ext_num_loaded = 0;
static void** laik_ext_so_loaded = 0;
static int laik_ext_cur_id = 0;

static void* probfunc(
    void* handle,
    char* name
){
    void* func;
    assert(handle);
    assert(name);

    func = dlsym(handle, name);
    if(!handle){
        dlclose(handle);
        laik_log(LAIK_LL_Error, dlerror());
        exit(1);
    }

    return func;
}

laik_agent* laik_ext_loadagent (
    char* name,
    laik_agent_t type,
    char* path, 
    int argc, 
    char** argv
){
    void *handle;
    laik_agent* agent;
    laik_agent_init init;
    laik_ext_errno errno;
    laik_agent_getcap getcap;

    assert(name);
    assert(path);
    if(argc>0)  assert(argv);

    switch (type){
        case LAIK_AGENT_STATIC:
            laik_log(LAIK_LL_Error, 
                "This is for dynamic loaded libs, call laik_ext_loadagent_static() instead \n");
            exit(1);

        case LAIK_AGENT_DYNAMIC:
            handle = dlopen(path, RTLD_LAZY);
            if(!handle){
                laik_log(LAIK_LL_Error, dlerror());
                exit(1);
            }
            init = (laik_agent_init) probfunc(handle, "agent_init");
            errno = init(argc, argv);
            if(errno != LAIK_AGENT_ERRNO_SUCCESS){
                laik_log(LAIK_LL_Error, "Cannot initialize agent");
                exit(1);
            }
            agent = malloc (sizeof(laik_agent));

            agent->id = laik_ext_cur_id;
            agent->name = name;
            getcap = (laik_agent_getcap) probfunc(handle, "agent_get_cap");
            agent->capabilities = getcap();
            agent->type = LAIK_AGENT_DYNAMIC;

            agent->detach = (laik_agent_detach) probfunc(handle, "agent_detach");
            agent->getfail = (laik_agent_get_failed) probfunc (handle, "agent_get_failed");
            agent->peekfail = (laik_agent_peek_failed) probfunc(handle, "agent_peek_failed");
            agent->clearalarm = (laik_agent_clear) probfunc(handle, "agent_clear_alarm");

            if(agent->capabilities & LAIK_AGENT_GET_SPARE){
                agent->getspare = (laik_agent_get_spare) probfunc(handle, "agent_get_spare");
                agent->peekspare = (laik_agent_peek_spare) probfunc(handle, "agent_peek_spare");
            }

            if(agent->capabilities & LAIK_AGENT_SIMULATOR){
                agent->setiter = (laik_agent_set_iter) probfunc(handle, "agent_set_iter");
                agent->setphase = (laik_agent_set_phase) probfunc(handle, "agent_set_phase");
            }

            if(agent->capabilities & LAIK_AGENT_RESET_NODE){
                agent->reset = (laik_agent_reset) probfunc(handle, "agent_reset");
            }
            break;
            
        default:
            laik_log(LAIK_LL_Error, 
                "Unsupported Agent Type. \n");
            exit(1);
    }

    laik_ext_num_loaded++;
    if(laik_ext_so_loaded == 0){
        laik_ext_so_loaded = malloc(sizeof(void*));
    }else{
        laik_ext_so_loaded = realloc(laik_ext_so_loaded, 
                (laik_ext_num_loaded+1)*sizeof(void*) );
    }
    laik_ext_so_loaded[laik_ext_num_loaded] = handle;

    return agent;
}

laik_agent* laik_ext_loadagent_static(
    laik_agent_init init,
    int argc, 
    char** argv
){
    assert(0);
    return 0;
}

void laik_ext_cleanup(
    laik_agent* agent
){
    assert(agent);
    agent->detach(agent);
}

int laik_allow_repartition (
    Laik_Instance* instance, 
    Laik_RepartitionControl* ctrl,
    int num_groups, 
    Laik_Group** groups
){

    assert(instance);
    assert(ctrl);
    assert(groups);
    assert(ctrl->num_agents);
    assert(ctrl->agents);
    
    char buffer[256];
    int num_failed = 0;
    int total_failed = 0;
    int* failed_tasks;
    int i;
    char* token;
    
    

    //TODO: Match up Task number with some unique identifier
    for(i=0; i<ctrl->num_agents; i++){
        // If no node is failed, no need to continue
        if(ctrl->agents[i]->peekfail(ctrl->agents[i]) == 0) continue;

        // Get list of failed node
        ctrl->agents[i]->getfail((ctrl->agents[i]), &num_failed, buffer);
        if(num_failed > 0){
            total_failed += num_failed;
            if(failed_tasks == 0){
                failed_tasks = malloc (total_failed*sizeof(int));
            }else{
                failed_tasks = (int*) realloc (failed_tasks, total_failed*sizeof(int));
            }

            //tokenized buffer
            token = strtok(buffer, " ");            
            //fill the lisk with failed tasks;
            failed_tasks[total_failed-1] = atoi(token);

            //if there are more than 1 failed task, continue tokenizing.
            while(num_failed > 1){
                token = strtok(NULL, " ");
                failed_tasks[total_failed-num_failed] = atoi(token);
            }

            //reset and go to next agent
            num_failed = 0;
        }else {continue; }
    }
    
    /*
    * TODO:
    * (1) Schrink Group
    * (2) Repartition of all data within a group
    */
    assert(0);

    return 0;
}
