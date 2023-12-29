#pragma once

// Userdata format for audio reactive FX
#include "fcn_declare.h"

/* use the following code to pass AudioReactive usermod variables to effect

  uint8_t  *binNum = (uint8_t*)&SEGENV.aux1, *maxVol = (uint8_t*)(&SEGENV.aux1+1); // just in case assignment
  bool      samplePeak = false;
  float     FFT_MajorPeak = 1.0;
  uint8_t  *fftResult = nullptr;
  float    *fftBin = nullptr;
  const const um_data_t *um_data;
  if (usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE)) {
    volumeSmth    = *(float*)   um_data->u_data[0];
    volumeRaw     = *(float*)   um_data->u_data[1];
    fftResult     =  (uint8_t*) um_data->u_data[2];
    samplePeak    = *(uint8_t*) um_data->u_data[3];
    FFT_MajorPeak = *(float*)   um_data->u_data[4];
    my_magnitude  = *(float*)   um_data->u_data[5];
    maxVol        =  (uint8_t*) um_data->u_data[6];  // requires UI element (SEGMENT.customX?), changes source element
    binNum        =  (uint8_t*) um_data->u_data[7];  // requires UI element (SEGMENT.customX?), changes source element
    fftBin        =  (float*)   um_data->u_data[8];
  } else {
    // add support for no audio data
    um_data = simulateSound(SEGMENT.soundSim);
  }
*/

// Common type definitions for um types
extern const um_types_t audioreactive_um_types[8]; // lives in util.cpp
