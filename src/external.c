/**
 * File: /Users/dai/play/laik/src/laik-external.c
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

#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "laik.h"

laik_agent* laik_ext_loadagent (
    char* path, 
    int argc, 
    char** argv
){
    //not yet implemented;
    assert(0);
    return 0;
}

laik_agent* laik_ext_loadagent_static(
    laik_agent_init_static init,
    int argc, 
    char** argv
){
    assert(init);
    return (init(argc, argv));
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
    
    char ** failed = 0;
    int n_failed = 0;
    char ** spare = 0;
    int n_spare = 0;
    int i,j;
    
    for(i=0; i<ctrl->num_agents; i++){
        char** temp; 
        int n_temp;

        // TODO: GetSpare

        (void) n_spare;
        (void) spare;

        ctrl->agents[i]->getfail((ctrl->agents[i]), &n_temp, &temp);
        if(n_temp > 0){
            if(n_failed == 0){
                failed = (char**) malloc (n_temp*sizeof(char*));
            }else{
                failed = (char**) realloc (failed, (n_temp + n_failed)*(sizeof(char*)));
            }
            for (j=0; j<n_temp; j++){
                failed[j] = (char*) malloc (strlen(temp[i]) + 1);
                strcpy(failed[j], temp[i]);
            }
        }else {continue; }
    }
    
    /*
    * TODO:
    * (1) Schrink Group
    * (2) Repartition of all data within a group
    */
    assert(0);
}