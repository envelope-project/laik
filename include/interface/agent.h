/**
 * File: /Users/dai/play/laik/include/interface/agent.h
 * Created Date: Sat Jul 22 2017
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


#define MAX_UID_LENGTH 64
#define MAX_FAILED_BUFFER 32

/* -------------- NUMBERS and CODES -------------- */

// LAIK Agent Error Numbers
enum tag_laik_agent_errno{
    LAIK_AGENT_ERRNO_SUCCESS = 0,
    LAIK_AGENT_ERRNO_INIT_FAIL = -100,
    LAIK_AGENT_ERRNO_UNKNOWN_FAIL = -110
};

// LAIK Agent Type
enum tag_laik_ext_agent_t{
    LAIK_AGENT_DEFAULT = 0,
    LAIK_AGENT_FT = 1,
    LAIK_AGENT_PROFILING = 2,

    LAIK_AGENT_UNKNOWN = 255
};

/* -------------- TYPES -------------- */
typedef struct { char uid[MAX_UID_LENGTH]; } node_uid_t;
typedef enum tag_laik_agent_errno Laik_Ext_Errno;
typedef struct tag_laik_ext_agent Laik_Agent;
typedef struct tag_laik_ext_ft_agent Laik_Ft_Agent;
typedef struct tag_laik_ext_profile_agent Laik_Profiling_Agent;
typedef enum tag_laik_ext_agent_t Laik_Agent_Type;

/* -------------- FUNCTION PROTOTYPES -------------- */

// Initialization of an agent
typedef Laik_Agent* (*laik_agent_init) (int, char**);

// Close and finalize an agent
typedef void (*laik_agent_detach) (void);

// reset an agent
typedef void (*laik_agent_reset) (void);

/** 
 * @brief  Prototype for get failed nodes.
 * @note   Assuming LAIK will prepare the buffer for uids. 
            This name must be unique and consequently used
            across entire application. 
 * @retval 
 */
typedef void (*laik_agent_get_failed) (int*, node_uid_t**);

/** 
 * @brief  Get Number of failed nodes.
 * @note   
 * @retval 
 */
typedef int (*laik_agent_peek_failed) (void);

/** 
 * @brief  Prototpye for getting spare nodes. 
 * @note   
 * @retval 
 */
typedef void (*laik_agent_get_spare) (int*, node_uid_t*);

/** 
 * @brief  Prototype for peek spare node
 * @note   
 * @retval 
 */
typedef int (*laik_agent_peek_spare) (void);

/** 
 * @brief  Prototype for informing the agent that the failure
            Information is consumed. 
 * @note   
 * @retval 
 */
typedef int (*laik_agent_clear) (void);


/** 
 * @brief Set current program iteration.
 * @note  Simulator only 
 * @retval 
 */
typedef void (*laik_agent_set_iter) (const int);

/** 
 * @brief  set current program phase
 * @note   Simulator only
 * @retval 
 */
typedef void (*laik_agent_set_phase) (const int, const char*, const void*);

/** 
 * @brief  Shut down a given node. 
 * @note   
 * @retval 
 */
typedef void (*laik_agent_shut_node) (int uuid);

/* -------------- DATASTRUCTURES -------------- */

struct tag_laik_ext_agent{
    int id;
    char* name; 

    int isAlive;
    int isInitialized;

    Laik_Agent_Type type;

    /* standard agent functionalities */
    laik_agent_detach detach;
    laik_agent_reset reset;
};


struct tag_laik_ext_ft_agent{
    Laik_Agent base;

    /* fault tolerant agent */
    laik_agent_get_failed getfail;
    laik_agent_peek_failed peekfail;


    // Optional, not yet used.
    laik_agent_clear clearalarm;

    /* extended agent functionalities */
    laik_agent_get_spare getspare;
    laik_agent_peek_spare peekspare;


    // Node Control Feedbacks
    laik_agent_shut_node freenode;

    // testing only
    laik_agent_set_iter setiter;
};

struct tag_laik_ext_profile_agent{
    Laik_Agent base;

    // Some Profiling Interface
};