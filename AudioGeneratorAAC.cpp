/*
  AudioGeneratorAAC
  Audio output generator using the Helix AAC decoder
  
  Copyright (C) 2017  Earle F. Philhower, III

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma GCC optimize ("O3")

#include "AudioGeneratorAAC.h"

// Isolate any globals these guys provide in their own namespace...
namespace AAC {
#include "libhelix-aac/assembly.h"

#include "libhelix-aac/aacdec.h"
#include "libhelix-aac/aacdec.c"
#include "libhelix-aac/bitstream.c" 
#include "libhelix-aac/buffers.c"
#include "libhelix-aac/noiseless.c"
#include "libhelix-aac/filefmt.c"
#include "libhelix-aac/dequant.c"
#include "libhelix-aac/decelmnt.c"
#include "libhelix-aac/stproc.c"
#include "libhelix-aac/pns.c"
#include "libhelix-aac/tns.c"
#include "libhelix-aac/dct4.c"
#include "libhelix-aac/imdct.c"
#include "libhelix-aac/fft.c"

#include "libhelix-aac/aactabs.c"
#include "libhelix-aac/trigtabs.c"

#include "libhelix-aac/huffman.c"
#include "libhelix-aac/hufftabs.c"
};



AudioGeneratorAAC::AudioGeneratorAAC()
{
  running = false;
  file = NULL;
  output = NULL;
  hAACDecoder = AAC::AACInitDecoder();
  // For sanity's sake...
  memset(buff, 0, sizeof(buff));
  memset(outSample, 0, sizeof(outSample));
  buffValid = 0;
  lastFrameEnd = 0;
  validSamples = 0;
  curSample = 0;
  lastRate = 0;
  lastChannels = 0;
}

AudioGeneratorAAC::~AudioGeneratorAAC()
{
  AAC::AACFreeDecoder(hAACDecoder);
}

bool AudioGeneratorAAC::stop()
{
  if (!running) return true;
  running = false;
  return file->close();
}

bool AudioGeneratorAAC::isRunning()
{
  return running;
}

bool AudioGeneratorAAC::FillBufferWithValidFrame()
{
  buff[0] = 0; // Destroy any existing sync word @ 0
  int nextSync;
  do {
    nextSync = AAC::AACFindSyncWord(buff + lastFrameEnd, buffValid - lastFrameEnd);
    if (nextSync >= 0) nextSync += lastFrameEnd;
    lastFrameEnd = 0;
    if (nextSync == -1) {
      if (buff[buffValid-1]==0xff) { // Could be 1st half of syncword, preserve it...
        buff[0] = 0xff;
        buffValid = file->read(buff+1, sizeof(buff)-1);
        if (buffValid==0) return false; // No data available, EOF
      } else { // Try a whole new buffer
        buffValid = file->read(buff, sizeof(buff));
        if (buffValid==0) return false; // No data available, EOF
      }
    }
  } while (nextSync == -1);

  // Move the frame to start at offset 0 in the buffer
  buffValid -= nextSync; // Throw out prior to nextSync
  memmove(buff, buff+nextSync, buffValid);

  // We have a sync word at 0 now, try and fill remainder of buffer
  buffValid += file->read(buff + buffValid, sizeof(buff) - buffValid);

  return true;
}

bool AudioGeneratorAAC::loop()
{
  if (!running) return true; // Nothing to do here!

  // If we've got data, try and pump it out...
  while (validSamples) {
    lastSample[0] = outSample[curSample*2];
    lastSample[1] = outSample[curSample*2 + 1];
    if (!output->ConsumeSample(lastSample)) return true; // Can't send, but no error detected
    validSamples--;
    curSample++;
  }

  // No samples available, need to decode a new frame
  if (FillBufferWithValidFrame()) {
    // buff[0] start of frame, decode it...
    unsigned char *inBuff = reinterpret_cast<unsigned char *>(buff);
    int bytesLeft = buffValid;
    int ret;
    if (ret = AAC::AACDecode(hAACDecoder, &inBuff, &bytesLeft, outSample)) {
      // Error, skip the frame...
      Serial.printf("AAC decode error %d\n", ret);
    } else {
      lastFrameEnd = buffValid - bytesLeft;
      AACFrameInfo fi;
      AAC::AACGetLastFrameInfo(hAACDecoder, &fi);
      if (fi.sampRateOut != lastRate) {
        output->SetRate(fi.sampRateOut);
        lastRate = fi.sampRateOut;
      }
      if (fi.nChans != lastChannels) {
        output->SetChannels(fi.nChans);
        lastChannels = fi.nChans;
      }
      curSample = 0;
      validSamples = fi.outputSamps / lastChannels;
    }
  } else {
    running = false; // No more data, we're done here...
  }

  return running;
}

bool AudioGeneratorAAC::begin(AudioFileSource *source, AudioOutput *output)
{
  if (!source) return false;
  file = source;
  if (!output) return false;
  this->output = output;
  if (!file->isOpen()) return false; // Error

  output->begin();
  
  // AAC always comes out at 16 bits
  output->SetBitsPerSample(16);
  
  running = true;
  
  return true;
}


