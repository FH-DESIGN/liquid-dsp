/*
 * Copyright (c) 2007, 2009 Joseph Gaeddert
 * Copyright (c) 2007, 2009 Virginia Polytechnic Institute & State University
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
//
//

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "liquid.internal.h"

#if HAVE_FFTW3_H
#   include <fftw3.h>
#endif

#define DEBUG_OFDMOQAMFRAME64SYNC             1
#define DEBUG_OFDMOQAMFRAME64SYNC_PRINT       0
#define DEBUG_OFDMOQAMFRAME64SYNC_FILENAME    "ofdmoqamframe64sync_internal_debug.m"
#define DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN  (1024)

// auto-correlation integration length
#define OFDMOQAMFRAME64SYNC_AUTOCORR_LEN      (96)

#if DEBUG_OFDMOQAMFRAME64SYNC
void ofdmoqamframe64sync_debug_print(ofdmoqamframe64sync _q);
#endif

struct ofdmoqamframe64sync_s {
    unsigned int num_subcarriers;   // 64
    unsigned int m;
    float beta;

    // filterbank objects
    ofdmoqam analyzer;

    // constants
    float zeta;         // scaling factor

    // PLCP
    float complex * S0; // short sequence
    float complex * S1; // long sequence

    // pilot sequence
    msequence ms_pilot;

    // signal detection | automatic gain control
    agc sigdet;

    // auto-correlators
    autocorr_cccf autocorr0;        // auto-correlation object [0]
    autocorr_cccf autocorr1;        // auto-correlation object [1]
    unsigned int autocorr_length;   // auto-correlation length
    unsigned int autocorr_delay0;   // delay [0]
    unsigned int autocorr_delay1;   // delay [1]
    float complex rxx0;
    float complex rxx1;
    float complex rxx_max0;
    float complex rxx_max1;

#if DEBUG_OFDMOQAMFRAME64SYNC
    cfwindow debug_x;
    cfwindow debug_rxx0;
    cfwindow debug_rxx1;
    cfwindow debug_rxy;
    cfwindow debug_framesyms;
#endif
};

ofdmoqamframe64sync ofdmoqamframe64sync_create(ofdmoqamframe64sync_callback _callback,
                                       void * _userdata)
{
    ofdmoqamframe64sync q = (ofdmoqamframe64sync) malloc(sizeof(struct ofdmoqamframe64sync_s));
    q->num_subcarriers = 64;
    q->m = 2;
    q->beta = 0.7f;

    q->zeta = 1.0f;
    
    // create analyzer
    q->analyzer = ofdmoqam_create(q->num_subcarriers,
                                  q->m,
                                  q->beta,
                                  0.0f,  // dt
                                  OFDMOQAM_ANALYZER,
                                  0);    // gradient

 
    // allocate memory for PLCP arrays
    q->S0 = (float complex*) malloc((q->num_subcarriers)*sizeof(float complex));
    q->S1 = (float complex*) malloc((q->num_subcarriers)*sizeof(float complex));
    ofdmoqamframe64_init_S0(q->S0);
    ofdmoqamframe64_init_S1(q->S1);

#if DEBUG_OFDMOQAMFRAME64SYNC
    q->debug_x   = cfwindow_create(DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN);
    q->debug_rxx0 = cfwindow_create(DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN);
    q->debug_rxx1 = cfwindow_create(DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN);
    q->debug_rxy = cfwindow_create(DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN);
    q->debug_framesyms = cfwindow_create(DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN);
#endif

    // set pilot sequence
    q->ms_pilot = msequence_create(8);

    // create agc | signal detection object
    q->sigdet = agc_create(1.0f, 0.01f);

    // create auto-correlator objects
    q->autocorr_length = q->num_subcarriers;
    q->autocorr_delay0 = q->num_subcarriers;
    q->autocorr_delay1 = q->num_subcarriers / 2;
    q->autocorr0 = autocorr_cccf_create(q->autocorr_length, q->autocorr_delay0);
    q->autocorr1 = autocorr_cccf_create(q->autocorr_length, q->autocorr_delay1);

    ofdmoqamframe64sync_reset(q);

    return q;
}

void ofdmoqamframe64sync_destroy(ofdmoqamframe64sync _q)
{
#if DEBUG_OFDMOQAMFRAME64SYNC
    ofdmoqamframe64sync_debug_print(_q);
    cfwindow_destroy(_q->debug_x);
    cfwindow_destroy(_q->debug_rxx0);
    cfwindow_destroy(_q->debug_rxx1);
    cfwindow_destroy(_q->debug_rxy);
    cfwindow_destroy(_q->debug_framesyms);
#endif

    // free analyzer object memory
    ofdmoqam_destroy(_q->analyzer);

    // clean up PLCP arrays
    free(_q->S0);
    free(_q->S1);

    // free pilot msequence object memory
    msequence_destroy(_q->ms_pilot);

    // free agc | signal detection object memory
    agc_destroy(_q->sigdet);

    // free auto-correlator memory objects

    // free main object memory
    free(_q);
}

void ofdmoqamframe64sync_print(ofdmoqamframe64sync _q)
{
    printf("ofdmoqamframe64sync:\n");
    printf("    num subcarriers     :   %u\n", _q->num_subcarriers);
}

void ofdmoqamframe64sync_reset(ofdmoqamframe64sync _q)
{
    // reset pilot sequence generator
    msequence_reset(_q->ms_pilot);

    // reset auto-correlators
    autocorr_cccf_clear(_q->autocorr0);
    autocorr_cccf_clear(_q->autocorr1);
}

void ofdmoqamframe64sync_execute(ofdmoqamframe64sync _q,
                                 float complex * _x,
                                 unsigned int _n)
{
    unsigned int i;
    for (i=0; i<_n; i++) {
        autocorr_cccf_push(_q->autocorr0, _x[i]);
        autocorr_cccf_execute(_q->autocorr0, &_q->rxx0);

        autocorr_cccf_push(_q->autocorr1, _x[i]);
        autocorr_cccf_execute(_q->autocorr1, &_q->rxx1);

#if DEBUG_OFDMOQAMFRAME64SYNC
        cfwindow_push(_q->debug_rxx0, _q->rxx0);
        cfwindow_push(_q->debug_rxx1, _q->rxx1);
#endif
    }
}

//
// internal
//

#if DEBUG_OFDMOQAMFRAME64SYNC
void ofdmoqamframe64sync_debug_print(ofdmoqamframe64sync _q)
{
    FILE * fid = fopen(DEBUG_OFDMOQAMFRAME64SYNC_FILENAME,"w");
    if (!fid) {
        printf("error: ofdmoqamframe64_debug_print(), could not open file for writing\n");
        return;
    }
    fprintf(fid,"%% %s : auto-generated file\n", DEBUG_OFDMOQAMFRAME64SYNC_FILENAME);
    fprintf(fid,"close all;\n");
    fprintf(fid,"clear all;\n");
    fprintf(fid,"n = %u;\n", DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN);
    unsigned int i;
    float complex * rc;

    /*
    fprintf(fid,"nu_hat = %12.4e;\n", _q->nu_hat0 + _q->nu_hat1);

    // gain vectors
    for (i=0; i<64; i++) {
        fprintf(fid,"G(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(_q->G[i]), cimagf(_q->G[i]));
        fprintf(fid,"G0(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(_q->G0[i]), cimagf(_q->G0[i]));
        fprintf(fid,"G1(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(_q->G1[i]), cimagf(_q->G1[i]));
    }
    */
 
    fprintf(fid,"x = zeros(1,n);\n");
    cfwindow_read(_q->debug_x, &rc);
    for (i=0; i<DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN; i++)
        fprintf(fid,"x(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(rc[i]), cimagf(rc[i]));
    fprintf(fid,"figure;\n");
    fprintf(fid,"plot(0:(n-1),real(x),0:(n-1),imag(x));\n");
    fprintf(fid,"xlabel('sample index');\n");
    fprintf(fid,"ylabel('received signal, x');\n");

    fprintf(fid,"rxx0 = zeros(1,n);\n");
    cfwindow_read(_q->debug_rxx0, &rc);
    for (i=0; i<DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN; i++)
        fprintf(fid,"rxx0(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(rc[i]), cimagf(rc[i]));
    fprintf(fid,"rxx1 = zeros(1,n);\n");
    cfwindow_read(_q->debug_rxx1, &rc);
    for (i=0; i<DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN; i++)
        fprintf(fid,"rxx1(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(rc[i]), cimagf(rc[i]));
    fprintf(fid,"figure;\n");
    fprintf(fid,"plot(0:(n-1),abs(rxx0),0:(n-1),abs(rxx1));\n");
    fprintf(fid,"xlabel('sample index');\n");
    fprintf(fid,"ylabel('|r_{xx}|');\n");

    fprintf(fid,"rxy = zeros(1,n);\n");
    cfwindow_read(_q->debug_rxy, &rc);
    for (i=0; i<DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN; i++)
        fprintf(fid,"rxy(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(rc[i]), cimagf(rc[i]));
    fprintf(fid,"figure;\n");
    fprintf(fid,"plot(0:(n-1),abs(rxy));\n");
    fprintf(fid,"xlabel('sample index');\n");
    fprintf(fid,"ylabel('|r_{xy}|');\n");

    /*
    // plot gain vectors
    fprintf(fid,"f = [-32:31];\n");
    fprintf(fid,"figure;\n");
    fprintf(fid,"subplot(2,1,1);\n");
    fprintf(fid,"    plot(f,fftshift(abs(G0)),f,fftshift(abs(G1)),f,fftshift(abs(G)));\n");
    fprintf(fid,"    ylabel('gain');\n");
    fprintf(fid,"subplot(2,1,2);\n");
    fprintf(fid,"    plot(f,unwrap(fftshift(arg(G0))),...\n");
    fprintf(fid,"         f,unwrap(fftshift(arg(G1))),...\n");
    fprintf(fid,"         f,unwrap(fftshift(arg(G))));\n");
    fprintf(fid,"    ylabel('phase');\n");
    */

    // frame symbols
    fprintf(fid,"framesyms = zeros(1,n);\n");
    cfwindow_read(_q->debug_framesyms, &rc);
    for (i=0; i<DEBUG_OFDMOQAMFRAME64SYNC_BUFFER_LEN; i++)
        fprintf(fid,"framesyms(%4u) = %12.4e + j*%12.4e;\n", i+1, crealf(rc[i]), cimagf(rc[i]));
    fprintf(fid,"figure;\n");
    fprintf(fid,"plot(real(framesyms),imag(framesyms),'x','MarkerSize',1);\n");
    fprintf(fid,"axis square;\n");
    fprintf(fid,"axis([-1.5 1.5 -1.5 1.5]);\n");
    fprintf(fid,"xlabel('in-phase');\n");
    fprintf(fid,"ylabel('quadrature phase');\n");
    fprintf(fid,"title('Frame Symbols');\n");

    fclose(fid);
    printf("ofdmoqamframe64sync/debug: results written to %s\n", DEBUG_OFDMOQAMFRAME64SYNC_FILENAME);
}
#endif

