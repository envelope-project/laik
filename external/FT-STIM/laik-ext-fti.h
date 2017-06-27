/*
 * @Author: D. Yang 
 * @Date: 2017-06-27 08:10:53 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-06-27 12:25:33
 * 
 * MIT License
 *
 * Copyright (c) 2017 TU Muenchen, LRR, D. Yang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#ifndef LAIK_EXT_FTI_H

#define FTI_FILE_VERSION "FTI SCRIPT VER 1.0"
#define MAX_STEP_BUFFER_SIZE 1024

typedef struct _LaikExtMsg LaikExtMsg;

typedef int (*get_iter) ();
typedef int (*LAIK_EXT_FAIL) (LaikExtMsg* list);


typedef enum ctrl_type_tag{
    FTI_ITER = 0,
    FTI_TIME = 1
}ctrl_type;

/*
 * If num_nodes > 0 && num_components == 0, 
 * assume node failure. 
 */
typedef struct tag_step{
    ctrl_type type;
    int tic_or_iter;
    int* nodes;
    int num_nodes;
    int* components;
    int num_components;
}step_t;

typedef struct fti_file_tag{
    char* version;
    int max_tics;
    int atomic_time;

    int num_steps;
    step_t** steps;

    get_iter iter;
}fti_file;

typedef struct ProtobufCMessage_tag{
    const void* a;
    unsigned b;
    void* c;
}ProtobufCMessage;

struct  _LaikExtMsg
{
  ProtobufCMessage base;
  /*
   *	required int32 num_failing_nodes = 1;
   */
  size_t n_failing_nodes;
  char **failing_nodes;
  /*
   *	required int32 num_spare_nodes = 3;
   */
  size_t n_spare_nodes;
  char **spare_nodes;
};

int readFile(FILE* fp, fti_file* fti);
int writeFile(FILE* fp, const fti_file* fti);
int cleanup_fti(fti_file* f);
void simulate(fti_file* f, LAIK_EXT_FAIL notify);

#endif
