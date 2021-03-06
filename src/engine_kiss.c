/*

  Copyright 2013 Samsung R&D Institute Russia
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met: 

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer. 
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution. 

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  
 */

/*! @file engine_kiss.c
 *  @brief KissFFT wrapper functions implementation.
 *  @author Markovtsev Vadim <v.markovtsev@samsung.com>
 *  @version 1.0
 *
 *  @section Notes
 *  This code partially conforms to <a href="http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml">Google C++ Style Guide</a>.
 *
 *  @section Copyright
 *  Copyright 2013 Samsung R&D Institute Russia
 */

#include "src/engine_kiss.h"
#include <assert.h>
#include <math.h>
#include "src/kiss/kiss_fft.h"
#include "src/kiss/kiss_fftr.h"
#include "src/kiss/kiss_fftnd.h"

typedef struct {
  kiss_fftr_cfg cfg;
  float *input;
  float *output;
} KissDCTInternalData;

void init_kiss(void *engineInternalData UNUSED,
               FFTFSingleInstance *instance) {
  switch (instance->type) {
    case FFTF_TYPE_COMPLEX:
      if (instance->dimension == FFTF_DIMENSION_1D) {
        instance->internalData = kiss_fft_alloc(
            instance->length,
            instance->direction == FFTF_DIRECTION_BACKWARD,
            NULL, NULL);
      } else {
        instance->internalData = kiss_fftnd_alloc(
            instance->lengths, instance->dimension,
            instance->direction == FFTF_DIRECTION_BACKWARD,
            NULL, NULL);
      }
      break;
    case FFTF_TYPE_REAL:
      assert(instance->dimension == FFTF_DIMENSION_1D &&
             "Higher dimensions are not implemented for real inputs");
      instance->internalData = kiss_fftr_alloc(
          instance->length,
          instance->direction == FFTF_DIRECTION_BACKWARD,
          NULL, NULL);
      break;
    case FFTF_TYPE_DCT:
      assert(instance->dimension == FFTF_DIMENSION_1D &&
             "Higher dimensions are not implemented");
      KissDCTInternalData *dctd = malloc(sizeof(KissDCTInternalData));
      dctd->cfg = kiss_fftr_alloc(
          instance->length * 2,
          instance->direction == FFTF_DIRECTION_BACKWARD,
          NULL, NULL);
      dctd->input = fftf_malloc((instance->length * 2 + 2) * sizeof(float));
      dctd->output = fftf_malloc((instance->length * 2 + 2) * sizeof(float));
      instance->internalData = dctd;
      break;
  }
}

void calc_kiss(void *engineInternalData UNUSED,
               const FFTFSingleInstance *instance) {
  switch (instance->type) {
    case FFTF_TYPE_COMPLEX: {
      if (instance->dimension == FFTF_DIMENSION_1D) {
        kiss_fft_cfg cfg = (kiss_fft_cfg)instance->internalData;
        kiss_fft(cfg, (const kiss_fft_cpx *)instance->input,
                 (kiss_fft_cpx *)instance->output);
      } else {
        kiss_fftnd_cfg cfg = (kiss_fftnd_cfg)instance->internalData;
        kiss_fftnd(cfg, (const kiss_fft_cpx *)instance->input,
                   (kiss_fft_cpx *)instance->output);
      }
      break;
    }
    case FFTF_TYPE_REAL: {
      kiss_fftr_cfg cfg = (kiss_fftr_cfg)instance->internalData;
      if (instance->direction == FFTF_DIRECTION_FORWARD) {
        kiss_fftr(cfg, (const kiss_fft_scalar *)instance->input,
                  (kiss_fft_cpx *)instance->output);
      } else {
        kiss_fftri(cfg, (const kiss_fft_cpx *)instance->input,
                   (kiss_fft_scalar *)instance->output);
      }
      break;
    }
    case FFTF_TYPE_DCT: {
      KissDCTInternalData *dctd = (KissDCTInternalData *)instance->internalData;
      int length = instance->length;
      if (instance->direction == FFTF_DIRECTION_FORWARD) {
        memcpy(dctd->input, instance->input, length * sizeof(float));
        for (int i = 0; i < length; i++) {
          dctd->input[length + i] = instance->input[length - i - 1];
        }
        kiss_fftr(dctd->cfg, (const kiss_fft_scalar *)dctd->input,
                  (kiss_fft_cpx *)dctd->output);
        for (int i = 0; i < length; i++) {
          float yre = dctd->output[i * 2];
          float yim = dctd->output[i * 2 + 1];
          float tmp = 3.141592f * i / (2 * length);
          float wre = cosf(tmp);
          float wim = -sinf(tmp);
          instance->output[i] = wre * yre - yim * wim;
        }
      } else {
        for (int i = 0; i < length; i++) {
          float tmp = 3.141592f * i / (2 * length);
          float wre = cosf(tmp);
          float wim = sinf(tmp);
          float yre = wre * instance->input[i];
          float yim = wim * instance->input[i];
          dctd->input[i * 2] = yre;
          dctd->input[i * 2 + 1] = yim;
        }
        dctd->input[length] = dctd->input[length + 1] = 0;
        kiss_fftri(dctd->cfg, (const kiss_fft_cpx *)dctd->input,
                   (kiss_fft_scalar *)dctd->output);
        memcpy(instance->output, dctd->output, length * sizeof(float));
      }
      break;
    }
  }
}

void destroy_kiss(void *engineInternalData UNUSED,
                  FFTFSingleInstance *instance) {
  if (instance->type == FFTF_TYPE_DCT) {
    KissDCTInternalData *dctd = (KissDCTInternalData *)instance->internalData;
    free(dctd->input);
    free(dctd->output);
    kiss_fft_free(dctd->cfg);
    free(dctd);
    return;
  }
  if (instance->dimension == FFTF_DIMENSION_1D) {
    kiss_fft_free(instance->internalData);
  } else {
    kiss_fftnd_free((kiss_fftnd_cfg)instance->internalData);
  }
}

void *malloc_kiss(void *engineInternalData UNUSED, size_t size) {
  return KISS_FFT_MALLOC(size);
}

void free_kiss(void *engineInternalData UNUSED, void *ptr) {
  KISS_FFT_FREE(ptr);
}
