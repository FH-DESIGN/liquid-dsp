/*
 * Copyright (c) 2007, 2008, 2009, 2010 Joseph Gaeddert
 * Copyright (c) 2007, 2008, 2009, 2010 Virginia Polytechnic
 *                                      Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// Test mixed-radix FFT algorithm
//

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include <getopt.h>
#include <complex.h>

#define DEBUG 0
#define DFT_FORWARD (-1)
#define DFT_REVERSE ( 1)

// print usage/help message
void usage()
{
    printf("fft_mixed_radix_test -- test mixed-radix DFTs, compare to slow DFT method\n");
    printf("options (default values in []):\n");
    printf("  u/h   : print usage/help\n");
    printf("  p     : stride (freq)\n");
    printf("  q     : stride (time)\n");
}

// super slow DFT, but functionally correct
void dft_run(unsigned int    _nfft,
             float complex * _x,
             float complex * _y,
             int             _dir,
             int             _flags);

int main(int argc, char*argv[]) {
    // transform size: p*q
    unsigned int p = 5;
    unsigned int q = 3;

    int dopt;
    while ((dopt = getopt(argc,argv,"uhp:q:")) != EOF) {
        switch (dopt) {
        case 'u':
        case 'h': usage();          return 0;
        case 'p': p = atoi(optarg); break;
        case 'q': q = atoi(optarg); break;
        default:
            exit(1);
        }
    }

    // transform size
    unsigned int n = p*q;

    // validate input
    if ( n == 0 ) {
        fprintf(stderr,"error: input transform size must be at least 2\n");
        exit(1);
    }

    unsigned int i;
    unsigned int k;

    // create and initialize data arrays
    float complex x[n];
    float complex y[n];
    float complex y_test[n];
    for (i=0; i<n; i++) {
        //x[i] = randnf() + _Complex_I*randnf();
        x[i] = (float)i + _Complex_I*(3 - (float)i);
    }

    // compute output for testing
    dft_run(n, x, y_test, DFT_FORWARD, 0);

    //
    // run Cooley-Tukey FFT
    //

    // compute twiddle factors (roots of unity)
    float complex twiddle[n];
    for (i=0; i<n; i++)
        twiddle[i] = cexpf(-_Complex_I*2*M_PI*(float)i / (float)n);

    // decimate in time
    for (i=0; i<q; i++) {
        for (k=0; k<p; k++)
            y[p*i+k] = x[k*q+i];
    }
#if DEBUG
    for (i=0; i<n; i++) {
        printf("  y[%3u] = %12.6f + j*%12.6f\n",
            i, crealf(y[i]), cimagf(y[i]));
    }
#endif

    // compute 'q' DFTs of size 'p' and multiply by twiddle factors
    printf("computing %u DFTs of size %u...\n", q, p);
    for (i=0; i<q; i++) {
#if DEBUG
        printf("  i=%3u/%3u\n", i, q);
#endif

        // for now, copy to temp buffer, compute FFT, and store result
        float complex t0[p];
        float complex t1[p];
        for (k=0; k<p; k++) t0[k] = y[p*i+k];
        dft_run(p, t0, t1, DFT_FORWARD, 0);
        for (k=0; k<p; k++) y[p*i+k] = t1[k];

#if DEBUG
        for (k=0; k<p; k++)
            printf("  %12.6f + j%12.6f > %12.6f + j%12.6f\n", crealf(t0[k]), cimagf(t0[k]), crealf(t1[k]), cimagf(t1[k]));
#endif
    }

    // compute 'p' DFTs of size 'q' and transpose
    printf("computing %u DFTs of size %u...\n", p, q);
    for (i=0; i<p; i++) {
#if DEBUG
        printf("  i=%3u/%3u\n", i, p);
#endif
        
        // for now, copy to temp buffer, compute FFT, and store result
        float complex t0[q];
        float complex t1[q];
        for (k=0; k<q; k++) t0[k] = y[p*k+i] * twiddle[i*k];
        dft_run(q, t0, t1, DFT_FORWARD, 0);
        for (k=0; k<q; k++) y[p*k+i] = t1[k];

#if DEBUG
        for (k=0; k<q; k++)
            printf("  %12.6f + j%12.6f > %12.6f + j%12.6f\n", crealf(t0[k]), cimagf(t0[k]), crealf(t1[k]), cimagf(t1[k]));
#endif
    }

    // 
    // print results
    //
    for (i=0; i<n; i++) {
        printf("  y[%3u] = %12.6f + j*%12.6f (expected %12.6f + j%12.6f)\n",
            i,
            crealf(y[i]),      cimagf(y[i]),
            crealf(y_test[i]), cimagf(y_test[i]));
    }

    // compute error
    float rmse = 0.0f;
    for (i=0; i<n; i++) {
        float e = y[i] - y_test[i];
        rmse += e*conjf(e);
    }
    rmse = sqrtf(rmse / (float)n);
    printf("RMS error : %12.4e (%s)\n", rmse, rmse < 1e-3 ? "pass" : "FAIL");

    return 0;
}

// super slow DFT, but functionally correct
void dft_run(unsigned int    _nfft,
             float complex * _x,
             float complex * _y,
             int             _dir,
             int             _flags)
{
    unsigned int i;
    unsigned int k;

    int d = (_dir == DFT_FORWARD) ? -1 : 1;

    for (i=0; i<_nfft; i++) {
        _y[i] = 0.0f;
        for (k=0; k<_nfft; k++) {
            float phi = 2*M_PI*d*i*k / (float)_nfft;
            _y[i] += _x[k] * cexpf(_Complex_I*phi);
        }
    }
}

