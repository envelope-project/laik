

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

  printf("LAIK %d/%d - Started repartitioning\n", current_group->inst->myid, current_group->inst->size);

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
    //DEBUG: Just switch off nodes with number 1,2
    if(current_group->myid == 1)
      killMyself = true;
    if(current_group->myid == 2)
      killMyself = true;
    if(current_group->myid == 6)
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
    //LAIK_PT_Master: Can master fail? If yes choose new one...
    //TODO: Other types?
    for(int i = 0; i < num_data; i++)
    {
      if(data[i]->activePartitioning->type == LAIK_PT_Block)
      {
        laik_set_partitioning_internal(data[i], data[i]->activePartitioning, failing);
      }
    }
    
    //Data redistributed, Change laiks group/inst numbering
    int id = current_group->myid;
    int new_id = 0;
    if(!failing[id])
      for(int i = 0; i < id; i++)
        if(!failing[i]) new_id++;
        
    current_group->size -= num_failing;
    current_group->inst->size -= num_failing;
    
    current_group->myid = new_id;
    current_group->inst->myid = new_id;
    
    //Update Mappings, now another task is responsible
    //TODO: check if more things need updating? Mapping is only shortly active
    //as one repartitions in the new group right away
    for(int i = 0; i < num_data; i++)
    {
      data[i]->activeMapping->task = new_id;
    }
    
    //Group has changed, update partitionings to reflect that.
    //TODO: Again only changes on LAIK_PT_BLOCK type so far
    //This should also be possible on other types, as we are not using anything
    //new, however it deadlocks, same problem as the deadlock in spmv2 perhaps?
    for(int i = 0; i < num_data; i++)
    {
      if(data[i]->activePartitioning->type == LAIK_PT_Block)
      {
        data[i]->activePartitioning->bordersValid=false;
        data[i]->activePartitioning->borders = 0;
        laik_set_partitioning(data[i], data[i]->activePartitioning);
      }
    }
    
    //Perform backup specific changes.
    current_group->inst->backend->switchOffNodes(failing, id);
    
    //Cleanup
    free(failing);
}
    /*
    //TODO: Will I shut of?
    int killMyself = false;   

    //Gather all failing nodes
    //TODO: Somehow abstract?
    for(int i = 0; i < msg->n_failing_nodes; i++)
    {
      printf("Msg: %s\n", msg->failing_nodes[i]);
      //if(strcmp(laik_mylocation(p->group->inst), msg->failing_nodes[i]))
      if(strcmp("n0", msg->failing_nodes[i]) && p->group->myid == 0)
      {
        killMyself = true;
        break;
      }
      if(strcmp("n1", msg->failing_nodes[i]) && p->group->myid == 1)
      {
        killMyself = true;
        break;
      }
      if(strcmp("n2", msg->failing_nodes[i]) && p->group->myid == 2)
      {
        killMyself = true;
        break;
      }
      if(strcmp("n3", msg->failing_nodes[i]) && p->group->myid == 3)
      {
        killMyself = true;
        break;
      }
    }
    
    if(killMyself)
      printf("LAIK %d/%d - Underlying node is failing, starting repartitioning...\n", p->group->inst->myid, p->group->inst->size);
    
    MPI_Comm comm = ((MPIData*)p->group->inst->backend_data)->comm;
    
    int* failing = malloc(p->group->size * sizeof(int));
    
    MPI_Allgather(&killMyself, 1, MPI_INT, failing, 1, MPI_INT, comm);
    
    printf("Kill: %i", killMyself); 
    
    int num_failing = 0;
    for(int i = 0; i < p->group->size; i++)
      if(failing[i])
        num_failing++;
    
    //Clear partitioning
    
    Laik_Partitioning** backup_partitionings = malloc(num_data * sizeof(Laik_Partitioning*));
    */
    /*
    for(int i = 0; i < num_data; i++)
    {
      backup_partitionings[i] = data[i]->activePartitioning;
      
      laik_set_new_partitioning(data[i], LAIK_PT_All, LAIK_AB_WriteAll, NULL);
      printf("LAIK %i : After rep: %i\n", p->group->inst->myid, i);
    }
    */
    /*
    //Change group/inst numbering
    int id = p->group->myid;
    int new_id = 0;
    if(!failing[id])
    {
      for(int i = 0; i < id; i++)
      {
        if(!failing[i])
          new_id++;
      }
    */
      /*
      for(int i = new_id-1; i >=0; i--)
      {
        if(failing[i])
          new_id = i;
        else
          break;
      }
      */
    /* 
    }
    
    p->group->size -= num_failing;
    p->group->inst->size -= num_failing;
    
    p->group->myid = new_id;
    p->group->inst->myid = new_id;
    
    //Mapping for all data s?? Only task should be necessary, rest by set_partitioning
    for(int i = 0; i < num_data; i++)
    {
      data[i]->activeMapping->task = new_id;
    } 
    
    //Change MPI backend communicator
    MPI_Comm new_comm;
    MPI_Comm_split(comm, failing[id] ? MPI_UNDEFINED : 0, id, &new_comm);
    
    if(killMyself)
    {
      MPI_Finalize();
      exit(0);
    }
    
    ((MPIData*)p->group->inst->backend_data)->comm = new_comm;
    */
    /*
    printf("DEBUG after %i \n\n", new_id);
    //Restore partitions on changed group
    for(int i = 0; i < num_data; i++)
    {
      //laik_set_partitioning(data[i], backup_partitionings[i], NULL);
      laik_set_partitioning(data[i], data[i]->activePartitioning, NULL);
    }
    */
    /*
    //Cleanup
    ext_message = NULL;
    //free(backup_partitionings);
    //free(failing);
    
    if(killMyself)
      while(1); //Something better here ;)
  */

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
