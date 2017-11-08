/* This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
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
 * Distributed Markov chain example.
 */

#include <stdlib.h>
#include <stdio.h>

typedef struct _MGraph {
    int n;   // number of states
    int in;  // fan-in
    int* cm; // connectivity
    double* pm; // probabilities
} MGraph;

// Produce a graph with <n> nodes and some arbitrary connectivity
// with a fan-in <in>. The resulting graph will be stored in
// <cm>[i,c], which is a <n> * (<in> +1) matrix storing the incoming nodes
// of node i in row i, using columns 1 .. <in> (column 0 is set to i).
// <pm>[i,j] is initialized with the probability of the transition
// from node <cm>[i,j] to node i, with cm[i,0] the prob for staying.
void init(MGraph* mg, int fineGrained)
{
    int n = mg->n;
    int in = mg->in;
    int* cm = mg->cm;
    double* pm = mg->pm;

    // for normalization of probabilites
    double* sum = malloc(n * sizeof(double));
    for(int i=0; i < n; i++) sum[i] = 0.0;

    // some kind of ring structure
    for(int i=0; i < n; i++) {
        int step = 1;
        cm[i * (in + 1) + 0] = i; // stay in i
        pm[i * (in + 1) + 0] = 5;
        sum[i] += 5;
        for(int j = 1; j <= in; j++) {
            int fromNode = (i + step) % n;
            double prob = (double) ((j+i) % (5 * in)) + 1;
            sum[fromNode] += prob;
            cm[i * (in + 1) + j] = fromNode;
            pm[i * (in + 1) + j] = prob;
            step = 2 * step + j + fineGrained * (i % 37);
            while(step > n) step -= n;
        }
    }
    // normalization. never should do divide-by-0
    for(int i=0; i < n; i++) {
        for(int j = 0; j <= in; j++)
            pm[i * (in + 1) + j] /= sum[ cm[i * (in + 1) + j] ];
    }
}

void print(MGraph* mg)
{
    int n = mg->n;
    int in = mg->in;
    int* cm = mg->cm;
    double* pm = mg->pm;

    for(int i = 0; i < n; i++) {
        printf("State %2d: stay %.3f ", i, pm[i * (in + 1)]);
        for(int j = 1; j <= in; j++)
            printf("<=(%.3f)=%-2d  ",
                   pm[i * (in + 1) + j], cm[i * (in + 1) + j]);
        printf("\n");
    }
}

// iteratively calculate probability distribution
double* run(MGraph* mg,
            int miter, double* v1, double* v2)
{
    int n = mg->n;
    int in = mg->in;
    int* cm = mg->cm;
    double* pm = mg->pm;

    double* src = v1;
    double* dest = v2;
    for(int iter = 0; iter < miter; iter++) {

        // spread values according to probability distribution
        for(int i=0; i < n; i++) {
            int off = i * (in + 1);
            double v = src[i] * pm[off];
            for(int j = 1; j <= in; j++)
                v += src[cm[off + j]] * pm[off + j];
            dest[i] = v;
        }

        if (src == v1) {
            src = v2; dest = v1;
        } else {
            src = v1; dest = v2;
        }
    }

    double sum = 0.0;
    for(int i=0; i < n; i++) {
        sum += src[i];
    }
    printf("  result probs: p0 = %g, p1 = %g, p2 = %g, Sum: %f\n",
           src[0], src[1], src[2], sum);

    return src;
}

int main(int argc, char* argv[])
{
    int n = 1000000;
    int in = 10;
    int miter = 10;
    int doPrint = 0;
    int fineGrained = 0;

    int arg = 1;
    while((arg < argc) && (argv[arg][0] == '-')) {
        if (argv[arg][1] == 'f') fineGrained = 1;
        if (argv[arg][1] == 'p') doPrint = 1;
        if (argv[arg][1] == 'h') {
            printf("markov-ser [options] [<statecount> [<fan-in> [<iterations>]]]\n"
                   "\nOptions:\n"
                   " -f: use pseudo-random connectivity (much more slices)\n"
                   " -p: print connectivity\n"
                   " -h: this help text\n");
            exit(1);
        }
        arg++;
    }
    if (argc > arg) n = atoi(argv[arg]);
    if (argc > arg + 1) in = atoi(argv[arg + 1]);
    if (argc > arg + 2) miter = atoi(argv[arg + 2]);

    if (n == 0) n = 1000000;
    if (in == 0) in = 10;

    printf("Init Markov chain with %d states, max fan-in %d\n", n, in);
    printf("Run %d iterations each.\n", miter);

    MGraph mg;
    mg.n = n;
    mg.in = in;
    mg.cm = malloc(n * (in + 1) * sizeof(int));
    mg.pm = malloc(n * (in + 1) * sizeof(double));

    init(&mg, fineGrained);
    if (doPrint) print(&mg);

    double* v1 = malloc(n * sizeof(double));
    double* v2 = malloc(n * sizeof(double));
    for(int i = 0; i < n; i++) {
        v1[i] = 0.0;
        v2[i] = 0.0;
    }

    printf("Start with state 0 prob 1 ...\n");
    v1[0] = 1.0;
    run(&mg, miter, v1, v2);

    printf("Start with state 1 prob 1 ...\n");
    for(int i = 0; i < n; i++) v1[i] = 0.0;
    v1[1] = 1.0;
    run(&mg, miter, v1, v2);

    printf("Start with all probs equal ...\n");
    double p = 1.0 / n;
    for(int i = 0; i < n; i++) v1[i] = p;
    run(&mg, miter, v1, v2);

    return 0;
}
