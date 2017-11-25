/*
 * @Author: D. Yang 
 * @Date: 2017-06-27 08:10:53 
 * @Last Modified by: D. Yang
 * @Last Modified time: 2017-06-27 17:25:39
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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "laik-ext-fti.h"

static void
fwriteln(
    const char* buf,
    FILE* fp
){
    fputs(buf, fp);
    fputc('\n', fp);
}
static void 
fputiln(
    int n,
    FILE* fp
){
    fprintf(fp, "%d\n", n);
}

static int
decode_step(
    const char* restrict opcode,
    step_t* restrict step
){
    int i;
    int ret;
    int bytes, n_bytes;
    assert(opcode);
    assert(step);

    ret = sscanf(opcode, "%d %d %d %d%n",
            (int*) &(step->type), /* FIXME: This is clearly wrong, enum != int */
            &(step->tic_or_iter), 
            &(step->num_nodes),
            &(step->num_components), 
            &bytes
    );

    if (ret != 4){
        fprintf(stderr, "Error Decoding Opcode.");
        return 1;
    }

    //TODO: Currently only node failure. 
    if(step->num_nodes != 0){
        step->nodes = (int*) malloc (sizeof(int)*step->num_nodes);
        for(i=0; i<step->num_nodes; i++){
            ret = sscanf(opcode+bytes, "%d%n", 
                    &(step->nodes[i]), 
                    &n_bytes);
            bytes += n_bytes;
            if(ret != 1){
                fprintf(stderr, "Error Decoding Opcode. ");
                return 1;
            }
        }
    }
    /*
    if(step->num_components != 0){
        step->components = (int*) malloc 
                (sizeof(int)*step->num_components);
        for(i=0; i<step->num_components; i++){
            ret = sscanf(opcode+bytes, "%d%n", 
                    &(step->components[i]), 
                    &n_bytes);
            bytes += n_bytes;
            if(ret != 1){
                fprintf(stderr, "Error Decoding Opcode. ");
                return 1;
            }
        }
    }
    */
    return 0;
}

void
performStep(
    const step_t* step, 
    LAIK_EXT_FAIL notify
){
    LaikExtMsg msg;
    char buf[64];
    int i;
    assert(step);

    msg.n_spare_nodes = 0;
    msg.n_failing_nodes = step->num_nodes;
    msg.failing_nodes = (char**) 
        malloc (sizeof(char*) * msg.n_failing_nodes);


    //build the message
    for(i=0;i<msg.n_failing_nodes;i++)
    {
        memset(buf, 0x0, 64);
        sprintf(buf, "%d", step->nodes[i]);
        msg.failing_nodes[i] = (char*) 
            malloc (strlen(buf+1)*sizeof(char));
        memcpy(&msg.failing_nodes[i], buf, strlen(buf+1));
    }
    
    notify(&msg);

    //clean up
    for(i=0; i<msg.n_failing_nodes; i++)
    {
        free(msg.failing_nodes[i]);
    }

}

int
readFile(
    FILE* fp, 
    fti_file* fti
)
{
    char buffer[MAX_STEP_BUFFER_SIZE];
    char* ret;
    int rv;
    int i;

    assert(fp);
    assert(fti);

    memset(buffer, 0xCC, MAX_STEP_BUFFER_SIZE);
    ret = fgets(buffer, sizeof(buffer), fp);
    if(!ret){
        fprintf(stderr, "Error Reading File.\n");
        return 1;
    }
    strtok(buffer, "\n");
    rv = strcmp(buffer, FTI_FILE_VERSION);

    if(rv!=0)
    {
        fprintf(stderr, "Wrong File version.\n");
        return 1;
    }

    ret = fgets(buffer, sizeof(buffer), fp);
    if(!ret){
        fprintf(stderr, "Error Reading File.\n");
        return 1;
    }
    sscanf(buffer, "%d", &(fti->max_tics));
    if(fti->max_tics<=0){
        fprintf(stderr, "Unplausible number of maximum tics");
        return 1;
    }
    
    ret = fgets(buffer, sizeof(buffer), fp);
    if(!ret){
        fprintf(stderr, "Error Reading File.\n");
        return 1;
    }
    sscanf(buffer, "%d", &(fti->atomic_time));
    if(fti->atomic_time<=0){
        fprintf(stderr, "Unplausible length of atomic time");
        return 1;
    }

    ret = fgets(buffer, sizeof(buffer), fp);
    if(!ret){
        fprintf(stderr, "Error Reading File.\n");
        return 1;
    }
    sscanf(buffer, "%d", &(fti->num_steps));
    if(fti->num_steps<=0){
        fprintf(stderr, "Unplausible number of steps");
        return 1;
    }
    
    fti->steps = (step_t**) calloc (fti->num_steps, sizeof(step_t*));
    for(i = 0; i<fti->num_steps; i++){
        step_t* single_step = (step_t*) calloc (1, sizeof(step_t));
        // Read
        ret = fgets(buffer, MAX_STEP_BUFFER_SIZE, fp);
        if(!ret){
            fprintf(stderr, "Error Reading File.\n");
            return 1;
        }

        // Decode 
        rv = decode_step(buffer, single_step);
        fti->steps[i] = single_step;
        if(rv){
            fprintf(stderr, "Error Decode Steps.");
            return 1;
        }
    }

    return 0;
}


int
writeFile(
    FILE* fp, 
    const fti_file* fti
){
    int i;
    char buffer[MAX_STEP_BUFFER_SIZE];
    int n_bytes = 0;
    int nn_bytes = 0;
    
    assert(fp);
    assert(fti);

    fwriteln(fti->version, fp);
    fputiln(fti->max_tics, fp);
    fputiln(fti->atomic_time, fp);
    fputiln(fti->num_steps, fp);


    for(i=0; i<fti->num_steps; i++){
        sprintf(buffer, "%d %d %d %d %n",
            fti->steps[i]->type,
            fti->steps[i]->tic_or_iter,
            fti->steps[i]->num_nodes,
            0,
            &n_bytes);

        for(int j=0; j<fti->steps[i]->num_nodes; j++)
        {
            sprintf(buffer + n_bytes, "%d %n", 
                fti->steps[i]->nodes[i], &nn_bytes);
            n_bytes += nn_bytes;
        }
    }

    return 0;
}

int
cleanup_fti(
    fti_file* f
){
    int i,j;
    for(i=0; i<f->num_steps; i++){
        free(f->steps[i]->nodes);
        free(f->steps[i]->components);
        free(f->steps[i]);
    }
    free(f->steps);
    return 0;
}

void 
simulate(
    fti_file* f, 
    LAIK_EXT_FAIL notify
){
    int i=0;
    assert(f);

    while(i<f->max_tics){
        switch(f->steps[i]->type){
            case FTI_ITER:
            {
                if(f->steps[i]->tic_or_iter != f->iter())
                {
                    break;
                }
                performStep(f->steps[i], notify);
                break;
            }
            case FTI_TIME:
            {
                if(f->steps[i]->tic_or_iter > i*f->atomic_time)
                {
                    break;
                }
                performStep(f->steps[i], notify);
                break;
            }
            default:
            {
                break;
            }
        }

        ++i;
        sleep(f->atomic_time);
    }
}
