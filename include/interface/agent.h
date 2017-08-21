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

enum tag_laik_agent_errno{
    LAIK_AGENT_ERRNO_SUCCESS = 0,
    LAIK_AGENT_ERRNO_INIT_FAIL = -100,
    LAIK_AGENT_ERRNO_UNKNOWN_FAIL = -110
};

enum tag_laik_ext_agent_t{
    LAIK_AGENT_STATIC = 0,
    LAIK_AGENT_DYNAMIC = 1,

    LAIK_AGENT_NONE = 255
};

enum tag_laik_ext_agent_cap{
    LAIK_AGENT_GET_FAIL = 1,
    LAIK_AGENT_GET_SPARE = 2,
    LAIK_AGENT_RESET_NODE = 4,
    LAIK_AGENT_SIMULATOR = 8
    
};

typedef enum tag_laik_agent_errno laik_ext_errno;
typedef struct tag_laik_ext_agent laik_agent;
typedef enum tag_laik_ext_agent_t laik_agent_t;
typedef enum tag_laik_ext_agent_cap laik_agent_cap;

typedef laik_ext_errno (*laik_agent_init) (int, char**);
typedef void (*laik_agent_detach) (laik_agent*);
typedef void (*laik_agent_reset) (laik_agent*);
typedef laik_agent_cap (*laik_agent_getcap) ();

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
