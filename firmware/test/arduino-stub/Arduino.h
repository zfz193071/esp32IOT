#pragma once

#define HIGH 1
#define LOW 0
#define OUTPUT 2
#define INPUT 3
#define INPUT_PULLUP 4

void digitalWrite(int gpio, int level);
void pinMode(int gpio, int mode);
int digitalRead(int gpio);
