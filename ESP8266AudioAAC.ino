#include <Arduino.h>
#include "AudioGeneratorAAC.h"


AudioGeneratorAAC *aac;

void setup()
{
aac = new AudioGeneratorAAC();
  
}


void loop()
{
  aac->loop();
}

