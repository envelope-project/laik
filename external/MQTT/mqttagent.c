
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

#include "laik.h"
#include "mqttclient.h"

static char ip[64]; 
static int port; 
static char username[64];
static char password[64];
static char topic[128];
static int isInited;

static int num_failed;
static node_uid_t** failed_nodes;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t* mutex = &m;

static void free_backbuffer(
    void
){
    pthread_mutex_lock(mutex);
    for(int i=0; i<num_failed; i++) free(failed_nodes[i]);
    num_failed = 0;
    memset(failed_nodes, 0x0, MAX_FAILED_BUFFER*sizeof(node_uid_t*));
    pthread_mutex_unlock(mutex);
}

void mqtt_detach(
    void
){
    stop_mosquitto();
    num_failed = 0;
    free_backbuffer();
    free(failed_nodes);
}

void mqtt_reset(
    void
){
    // Stop Mosquitto look and wait it to finish
    stop_mosquitto();
    sleep(20);

    // Clean the backbuffer for failed nodes
    free_backbuffer();
    
    //restart mosquitto
    start_mosquitto(ip, port, username, password, topic);
}

void onMessage(
    int msglen, 
    const char* msg
){
    node_uid_t* node;
    if(msglen>0){
        node = (node_uid_t*) malloc (sizeof(node_uid_t));
        strncpy(node->uid, msg, MAX_UID_LENGTH);
        assert(num_failed < MAX_FAILED_BUFFER);
        pthread_mutex_lock(mutex);
        failed_nodes[num_failed] = node; 
        num_failed++;
        pthread_mutex_unlock(mutex);
    }
}

void mqtt_getfailed(
    int* n_failed,
    node_uid_t** l_failed
){
    pthread_mutex_lock(mutex);
    for(int i=0; i<num_failed; i++){
       memcpy(l_failed[i], failed_nodes[i], MAX_UID_LENGTH);
    }
    *n_failed = num_failed;
    free_backbuffer();
    pthread_mutex_unlock(mutex);
}

int mqtt_peekfailed(
    void
){
    return num_failed;
}

Laik_Agent* agent_init(
    int argc, 
    char** argv
){
    
    if(argc != 3 || argc != 5){
        //laik_log(2, "Invalid Argument number.\n");
        exit(-1);
    }

    strncpy(ip, argv[0], 64);
    port = atoi(argv[1]);
    strncpy(topic, argv[2], 128);
    strncpy(username, argv[3], 64);
    strncpy(password, argv[4], 64);

    Laik_Ft_Agent* me = (Laik_Ft_Agent*) calloc (1, sizeof(Laik_Ft_Agent));
    assert(me);

    Laik_Agent* myBase = &(me->base);

    myBase->name = "MQTT Fault Tolerant Agent";
    myBase->id = 0x02;
    myBase->isAlive = 1;
    myBase->isInitialized = 1;
    myBase->type = LAIK_AGENT_FT;

    myBase->detach = mqtt_detach;
    myBase->reset = mqtt_reset;

    me->getfail = mqtt_getfailed; 
    me->peekfail = mqtt_peekfailed;

    num_failed = 0;
    failed_nodes = (node_uid_t**) 
            calloc (MAX_FAILED_BUFFER, sizeof(node_uid_t*));

    register_callback(onMessage);
    start_mosquitto(ip, port, username, password, topic);
    
    isInited = 1;
    return (Laik_Agent*) me;
}
