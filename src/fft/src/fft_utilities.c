/*
 * Copyright (c) 2011 Joseph Gaeddert
 * Copyright (c) 2011 Virginia Polytechnic Institute & State University
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
// fft_common.c
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "liquid.internal.h"

// determine best FFT method based on size
liquid_fft_method liquid_fft_estimate_method(unsigned int _nfft)
{
    // TODO : determine best method based on size, for now return DFT
    return LIQUID_FFT_METHOD_DFT;
}

// is input radix-2?
int fft_is_radix2(unsigned int _n)
{
    // check to see if _n is radix 2
    unsigned int i;
    unsigned int d=0;
    unsigned int m=0;
    unsigned int t=_n;
    for (i=0; i<8*sizeof(unsigned int); i++) {
        d += (t & 1);           // count bits, radix-2 if d==1
        if (!m && (t&1)) m = i; // count lagging zeros, effectively log2(n)
        t >>= 1;
    }

    return (d == 1) ? 1 : 0;
}


// reverse _n-bit index _i
unsigned int fft_reverse_index(unsigned int _i, unsigned int _n)
{
    unsigned int j=0, k;
    for (k=0; k<_n; k++) {
        j <<= 1;
        j |= ( _i & 1 );
        _i >>= 1;
    }

    return j;
}


