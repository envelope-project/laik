/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017 Dai Yang, I10/TUM
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

#include "../include/laik-internal.h"

/** 
 * @brief  Test if a given function exists in dynamic library
 * @note   
 * @param  handle: Handle to the library.
 * @param  name: Name of the function
 * @retval NULL, if the function is not found;
           f_ptr, if the function is found.
 */
static laik_agent_init probfunc(
    void* handle,
    char* name
){
    assert(handle);
    assert(name);

    laik_agent_init result;

    /* Work around dlsym()'s broken type signature */
    *((void**) (&result)) = dlsym (handle, name);
    if(!result){
        dlclose(handle);
        laik_log(LAIK_LL_Error, "%s", dlerror());
        exit(1);
    }
    return result;
}

/** 
 * @brief  Map a given node uid to LAIK Task number
 * @note   FIXEME>>>> NEED TO BE IMPLEMENTED
 * @param  inst: the LAIK Instance
 * @param  uid: A given node uid. 
 * @retval the LAIK Task number
 */
static int laik_map_id(
    Laik_Instance* inst,
    node_uid_t* uid
){
    assert(inst);
    assert(uid);

    
    //FIXME: Do something plausible
    (void) inst;

    return atoi(uid->uid);
}


/** 
 * @brief  Load an external agent.
 * @note   
 * @param  inst: The LAIK instance
 * @param  path: Path of the agent
 * @param  isDynamic: type of the agent
 * @param  argc: arguments to pass to the agent
 * @param  argv: arguments to pass to the agent
 * @retval None
 */
void laik_ext_load_agent_from_file (
    Laik_Instance* instance,
    char* path,
    int argc, 
    char** argv
){
    assert(instance);
    assert(path);
    assert(argc == 0 || argv);

    void *handle;
    Laik_Agent* agent;
    laik_agent_init init;
    Laik_RepartitionControl* ctrl;
    
    if(instance->repart_ctrl == NULL){
        laik_ext_init(instance);
    }
    ctrl = instance->repart_ctrl;

    //ensure less than MAX_AGENT
    assert(ctrl->num_agents <= MAX_AGENTS);

    handle = dlopen(path, RTLD_LAZY);
    if(!handle){
        laik_log(LAIK_LL_Error, "%s", dlerror());
        exit(1);
    }
    init = probfunc(handle, "agent_init");
    assert(init);

    agent = init(argc, argv );
    //argv = {"localhost","1883",top_name };
    assert(agent);

    ctrl->agents[ctrl->num_agents] = agent;
    ctrl->num_agents++;
    ctrl->handles[ctrl->num_agents] = handle;
    laik_log(LAIK_LL_Debug, "laik_ext_load_agent done ! L:2 \n");
}

/** 
 * @brief  Load an external agent.
 * @note   
 * @param  inst: The LAIK instance
 * @param  name: Pointer of the agent
 * @param  argc: arguments to pass to the agent
 * @param  argv: arguments to pass to the agent
 * @retval None
 */
void laik_ext_loadagent (
    Laik_Instance* instance,
    laik_agent_init init,
    int argc, 
    char** argv
){
    assert(instance);
    assert(init);
    assert(argc == 0 || argv);

    Laik_Agent* agent;
    Laik_RepartitionControl* ctrl;

    if(instance->repart_ctrl == NULL){
        laik_ext_init(instance);
    }

    ctrl = instance->repart_ctrl;

    //ensure less than MAX_AGENT
    assert(ctrl->num_agents <= MAX_AGENTS);

    agent = init(argc, argv);

    assert(agent);

    ctrl->agents[ctrl->num_agents] = agent;
    ctrl->num_agents++;
    ctrl->handles[ctrl->num_agents] = NULL;
}

/** 
 * @brief  Clean up the agent system and close all agent. 
 * @note   
 * @param  instance: The LAIK instance
 * @retval None
 */
void laik_ext_cleanup(
    Laik_Instance* instance
){
    Laik_RepartitionControl* ctrl;
    assert(instance);
    ctrl = instance->repart_ctrl;
    assert(ctrl);

    for(int i=0; i<ctrl->num_agents; i++){
        //Laik_Agent* agent = ctrl->agents[i];
        //agent->detach();
        if(ctrl->handles[i]){
            dlclose(ctrl->handles[i]);
        }
    }
    ctrl->num_agents = 0;

    free(ctrl);
    instance->repart_ctrl = NULL;
}


/** 
 * @brief  Create a list of failed nodes
 * @note   
 * @param  instance: The LAIK Instance
 * @param  num_failed: Number of failed ranks
 * @param  failed_ranks: A field, large enough to hold the number
            of failed ranks.
 * @param  max_failed: The size of this field
 * @retval None
 */
void laik_get_failed (
    Laik_Instance* instance, 
    int* num_failed,
    node_uid_t* failed_ranks,
    int max_failed
){

    (void) max_failed;
    Laik_RepartitionControl* ctrl;
    int i, n, count;
    assert(instance);    
    ctrl = instance->repart_ctrl;
    assert(ctrl);
    

    count = 0;
    for(i=0; i < ctrl -> num_agents; i++){
        Laik_Agent* agent = ctrl->agents[i];

        // Only Require these Fault Tolerance Agents
        if(agent->type == LAIK_AGENT_FT)
        {
            Laik_Ft_Agent* fta = (Laik_Ft_Agent*) agent;
            // Check if there is something to do
            n = fta->peekfail();
            laik_log(LAIK_LL_Debug, "peek return %d L:2 \n" , n);
            if(!n){
                continue;
            }

            // Yes: (1) Get uids; (2) Map; (3) Create List
            // (1) Get a list of node_uids


            fta->getfail(num_failed, failed_ranks);

            //printf("fta-> getfail: %d \n", *num_failed);
            for(int k = 0 ; k < *num_failed; k++ )
			{
				laik_log(LAIK_LL_Debug, " Buf: %s   - % d \n",(failed_ranks + k)->uid, k);
				fflush(stdout);
			}

            // (2) Map the failed nodes to ranks
            //for(j=0;j<n;j++){
                // Prevent Segmentation Fault
                //assert(count < max_failed);

                // Map node uid to laik id
                //int id = laik_map_id(instance, &buffer[i]);
                
                // fill the whole list
                //*failed_ranks[count] = id;
             //   count++;
           // }
        }
    }


}

/** 
 * @brief  Initialize the LAIK Repartition Control Interface
 * @note   
 * @param  inst: The LAIK Instance
 * @retval None
 */
void laik_ext_init (
    Laik_Instance* inst
){
    assert(inst);

    Laik_RepartitionControl* ctrl;
    ctrl = (Laik_RepartitionControl*) calloc (1, sizeof(Laik_RepartitionControl));
    ctrl->num_agents = 0;
    inst->repart_ctrl = ctrl;

}
