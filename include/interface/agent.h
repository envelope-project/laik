/*
 * @Author: D. Yang 
 * @Date: 2017-07-06 07:49:13 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-07-06 07:51:22
 * 
 * This interface proposal shall provide a implementation 
 * template for creating external control agents. 
 */

enum tag_laik_ext_agent_t{
    LAIK_AGENT_STATIC = 0,
    LAIK_AGENT_DYNAMIC = 1,
    LAIK_AGENT_SIMULATOR = 2,

    LAIK_AGENT_NONE = 255
};

enum tag_laik_ext_agent_cap{
    LAIK_AGENT_GET_FAIL = 1,
    LAIK_AGENT_GET_SPARE = 2,
    LAIK_AGENT_RESET_NODE = 4,
};

typedef struct tag_laik_ext_agent laik_agent;
typedef enum tag_laik_ext_agent_t laik_agent_t;
typedef enum tag_laik_ext_agent_cap laik_agent_cap;

typedef laik_agent* (*laik_agent_init_static) (int, char**);
typedef void (*laik_agent_detach) (laik_agent*);
typedef void (*laik_agent_reset) (laik_agent*);
typedef laik_agent_cap (*laik_agent_getcap) (laik_agent*);

typedef void (*laik_agent_get_failed) (laik_agent*, int*, char***);
typedef int (*laik_agent_peek_failed) (laik_agent*);
typedef void (*laik_agent_get_spare) (laik_agent*, int*, char***);
typedef int (*laik_agent_peek_spare) (laik_agent*);
typedef int (*laik_agent_clear) (laik_agent*);

typedef void (*laik_agent_set_iter) (laik_agent*, const int);
typedef void (*laik_agent_set_phase) (laik_agent*, const int, const char*, const void*);


struct tag_laik_ext_agent{
    int id;
    char* name; 

    laik_agent_cap capabilities;
    laik_agent_t type;

    /* standard agent functionalities */
    laik_agent_detach detach;
    laik_agent_reset reset;
    laik_agent_get_failed getfail;
    laik_agent_peek_failed peekfail;
    laik_agent_clear clearalarm;

    /* extended agent functionalities */
    laik_agent_get_spare getspare;
    laik_agent_peek_spare peekspare;
    
    /* simulator agent functionalities */
    laik_agent_set_iter setiter;
    laik_agent_set_phase setphase;
};
