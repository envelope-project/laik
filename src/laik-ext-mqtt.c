

#include "../include/laik/ext.h"
#include "../include/laik/space.h"
#include "../include/laik/core.h"
#include "../include/laik-internal.h"

#include "../external/MQTT/laik_intf.h"
#include "../include/laik-ext-mqtt.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

static LaikExtMsg* ext_message = NULL;

int mqtt_get_message(LaikExtMsg* list)
{
  if(ext_message)
    return 0;
  
  //Save stuff, RACES!!!
  for(int i = 0; i < list->n_failing_nodes; i++)
  {
    printf("I got a message: %s\n", list->failing_nodes[i]);
  }
  
  ext_message = list; 
  return 0;
}

int mqtt_cleanup_()
{
    //Todo
    return -1;
}

void mqtt_init_(Laik_Instance* inst)
{
    //Register with MQTT
    //For now all parameters hardcoded, replace by config file/Enviroment variables later.
    //init_ext_com(&mqtt_get_message, &mqtt_cleanup_,
    //  "127.0.0.1", 1883, 60, NULL, NULL);
        
	//Todo
}

void mqtt_finalize_(Laik_Instance* inst)
{
	//Todo
	
	//Do I have to close anything?
}

//Called by application to signal that the partitioning can be changed now
void mqtt_allowRepartitioning_(Laik_Group* current_group, Laik_Data** data, int num_data)
{  
    //TODO: Fetch msg
    LaikExtMsg* msg = ext_message;
    
    LaikExtMsg dbgmsg;
    dbgmsg.n_failing_nodes = 1;
    dbgmsg.failing_nodes = NULL;
    msg = &dbgmsg;
    
    //Check if msg is usefull
    if(msg == NULL)
      return;
    if(msg->n_failing_nodes == 0)
      return;


    //Extract info from msg
    int killMyself = false;
    //TODO: Make more general, maybe pass a callback to decide how the strings should look
    /*
    for(int i = 0; i < nodes->n_failing_nodes; i++)
    {
      printf("Msg: %s\n", nodes->failing_nodes[i]);
      //if(strcmp(laik_mylocation(current_group->inst), nodes->failing_nodes[i]))
      if(strcmp("n0", nodes->failing_nodes[i]) && current_group->myid == 0)
      {
        killMyself = true;
        break;
      }
      if(strcmp("n1", nodes->failing_nodes[i]) && current_group->myid == 1)
      {
        killMyself = true;
        break;
      }
      if(strcmp("n2", nodes->failing_nodes[i]) && current_group->myid == 2)
      {
        killMyself = true;
        break;
      }
      if(strcmp("n3", nodes->failing_nodes[i]) && current_group->myid == 3)
      {
        killMyself = true;
        break;
      }
    }
    */
    //DEBUG: Just switch off nodes with number 1
    if(current_group->myid == 1)
      killMyself = true;

    //Exchange information on which nodes kill themselves
    int* failing;
    failing = malloc(current_group->size * sizeof(int));
    current_group->inst->backend->gatherInts(killMyself, failing);
      
    //Compute how many nodes are failing
    int num_failing = 0;
    for(int i = 0; i < current_group->size; i++)
      if(failing[i])
        num_failing++;
        
    //Redistribute data between the nodes
    //Currently only redistributes on block partitioning
    //LAIK_PT_All: should not need this as data is already everywhere
    //LAIK_PT_Master: Can master fail? If yes choose new one... <- DO
    //TODO: Other types?
    for(int i = 0; i < num_data; i++)
    {
      if(data[i]->activePartitioning->type == LAIK_PT_Block)
      {
        //A crude copy of the active partitioning, TODO: make more robust
        Laik_Partitioning* old = data[i]->activePartitioning;
        Laik_Partitioning* p;
        p = (Laik_Partitioning*) malloc(sizeof(Laik_Partitioning));

        //LOL, only for debug
        p->id = 42;
        p->name = strdup("partng-0     ");
        sprintf(p->name, "partng-%d", p->id);

        p->space = data[i]->activePartitioning->space;
        p->next = data[i]->activePartitioning->space->first_partitioning;
        p->space->first_partitioning = p;

        p->access = data[i]->activePartitioning->access;
        p->type = data[i]->activePartitioning->type;
        p->group = laik_world(data[i]->activePartitioning->space->inst);
        p->pdim = data[i]->activePartitioning->pdim;

        p->getIdxW = data[i]->activePartitioning->getIdxW;
        p->idxUserData = data[i]->activePartitioning->idxUserData;
        p->getTaskW = data[i]->activePartitioning->getTaskW;
        p->taskUserData = data[i]->activePartitioning->taskUserData;

        p->base = data[i]->activePartitioning->base;
        p->haloWidth = data[i]->activePartitioning->haloWidth;

        p->bordersValid = false;
        p->borders = false;
        
        laik_set_partitioning_internal(data[i], p, failing);
        
        uint64_t rcount;
        double* res;
        laik_map_def1(data[2], (void**) &res, &rcount);
        for(uint64_t i = 0; i < rcount; i++)
          laik_log(2, "res bef %i: %f\n", i, res[i]);
        
        old->bordersValid = false;
        laik_set_partitioning_internal(data[i], old, failing);
        
        laik_map_def1(data[2], (void**) &res, &rcount);
        for(uint64_t i = 0; i < rcount; i++)
          laik_log(2, "res aft %i: %f\n", i, res[i]);
      }
    }
    
    //Data redistributed, Change laiks group/inst numbering
    int id = current_group->myid;
    int new_id = 0;
    if(!failing[id])
      for(int i = 0; i < id; i++)
        if(!failing[i]) new_id++;
    

    int old_size = current_group->size;
    current_group->size -= num_failing;
    current_group->inst->size -= num_failing;
    
    current_group->myid = new_id;
    current_group->inst->myid = new_id;
    
    //Perform backup specific changes.
    current_group->inst->backend->switchOffNodes(failing, id);
    
    //Update Mappings, now another task is responsible
    //TODO: check if more things need updating? Mapping is only shortly active
    //as one repartitions in the new group right away
    for(int i = 0; i < num_data; i++)
    {
      data[i]->activeMapping->task = new_id;
      data[i]->activeMapping->baseIdx = 
        data[i]->activePartitioning->borders[id].from;
    }
    
    //Group has changed, update partitionings to reflect that.
    //TODO: Again only changes on LAIK_PT_BLOCK type so far
    //This should also be possible on other types, as we are not using anything
    //new, however it deadlocks, same problem as the deadlock in spmv2 perhaps?
    //Now:
    //Don't repartition. instead just rewrite borders
    
    for(int i = 0; i < num_data; i++)
    {
      Laik_Slice* new_borders = malloc(current_group->size * sizeof(Laik_Slice));
      //Copy all borders which are still necessary
      int j_ = 0;
      for(int j = 0; j < old_size; j++)
      {
        if(!failing[j])
        {
          new_borders[j_] = data[i]->activePartitioning->borders[j];
          j_++;
        }
      }
      free(data[i]->activePartitioning->borders);
      data[i]->activePartitioning->borders = new_borders;
      data[i]->activePartitioning->bordersValid = false;
    }
    /*
    for(unsigned i = 0; i < current_group->size; i++)
    {
      laik_log(2, "part %i from %i to %i\n", i,s
        data[i]->activePartitioning->borders[i].from.i[0],
        data[i]->activePartitioning->borders[i].to.i[0]
      );
        
    }*/
    
    //Cleanup
    free(failing);
}

static Laik_RepartitionControl laik_repartitioningcontrol_mqtt =
{
    mqtt_init_,
    mqtt_finalize_,
    mqtt_allowRepartitioning_
};

//Todo
Laik_RepartitionControl* init_ext_mqtt(Laik_Instance* inst)
{
  laik_repartitioningcontrol_mqtt.init(inst);
  return &laik_repartitioningcontrol_mqtt;
}
