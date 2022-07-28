/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include "ubitx.h"


/* Normal encoder state */
uint8_t previousEncoderState = 0;
int8_t encoderCount = 0;

/* Momentum encoder state */
int16_t encoderCountPeriodic = 0;
int8_t momentum[3] = {0};
static const uint8_t callbackPeriodMS = 200;
// static const uint8_t MOMENTUM_MULTIPLIER = 1;


/*
    returns a two-bit number such that each bit reflects the current
    value of each of the two phases of the encoder
*/
uint8_t encoderState()
{
  return (digitalRead(ENC_A) ? 1 : 0 + digitalRead(ENC_B) ? 2 : 0);
}

/*
   SmittyHalibut's encoder handling, using interrupts. Should be quicker, smoother handling.
   The Interrupt Service Routine for Pin Change Interrupts on A0-A5.
*/
ISR(PCINT1_vect)
{
  uint8_t currentEncoderState = encoderState();

  if (previousEncoderState == currentEncoderState)  // unnecessary ISR
    return;

  // these transitions point to the encoder being rotated counter-clockwise
  if ((previousEncoderState == 0 && currentEncoderState == 2) ||
      (previousEncoderState == 2 && currentEncoderState == 3) ||
      (previousEncoderState == 3 && currentEncoderState == 1) ||
      (previousEncoderState == 1 && currentEncoderState == 0))
  {
    encoderCount -= 1;
    encoderCountPeriodic -= 1;
  }
  // these transitions point to the encoder being rotated clockwise
  else if ((previousEncoderState == 0 && currentEncoderState == 1) ||
           (previousEncoderState == 1 && currentEncoderState == 3) ||
           (previousEncoderState == 3 && currentEncoderState == 2) ||
           (previousEncoderState == 2 && currentEncoderState == 0))
  {
    encoderCount += 1;
    encoderCountPeriodic += 1;
  }

  previousEncoderState = currentEncoderState;  // record state for next pulse interpretation
}


/*
   Setup the encoder interrupts and global variables.
*/
void pciSetup(uint8_t pin) {
  *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
  PCIFR |= bit (digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
  PCICR |= bit (digitalPinToPCICRbit(pin)); // enable interrupt for the group
}


/* set up encoder */
void encoderSetup() {

  encoderCount = 0;
  previousEncoderState = encoderState();

  // setup Pin Change Interrupts for the encoder inputs
  pciSetup(ENC_A);
  pciSetup(ENC_B);

  // Set up timer interrupt for momentum
  TCCR1A = 0; // "normal" mode
  TCCR1B = 3; // clock divider of 64
  TCNT1  = 0; // start counting at 0
  OCR1A  = uint32_t(F_CPU) * callbackPeriodMS / 1000 / 64; // set target number
  TIMSK1 |= (1 << OCIE1A); // enable interrupt
}


/* timer compare interrupt service routine */
ISR(TIMER1_COMPA_vect) {
  momentum[2] = momentum[1];
  momentum[1] = momentum[0];
  momentum[0] = encoderCountPeriodic;
  encoderCountPeriodic = 0;
}


/* */
int8_t minMomentumMag() {

  int8_t minMag = 127;

  for (uint8_t i = 0; i < sizeof(momentum) / sizeof(momentum[0]); ++i) {
    int8_t mag = abs(momentum[i]);

    if (mag < minMag)
      minMag = mag;
  }

  return minMag;
}


/*
  returns the number of ticks in a short interval, +ve in clockwise, -ve in counter-clockwise
*/
int16_t encoderRead(void) {

  if (encoderCount != 0) {  // if (0 != enc_count) {
    int16_t ret = encoderCount;
    int8_t s = (encoderCount < 0) ? -1 : 1;
    int8_t momentumMag = minMomentumMag();

    if (momentumMag >= 20)
      ret += s * 40;
    else if (momentumMag >= 5)
      ret += s * (20 + momentumMag) / (20 - momentumMag);

    encoderCount = 0;
    return ret;
  }

  return 0;
}
