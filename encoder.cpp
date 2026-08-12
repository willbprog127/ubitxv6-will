/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file

  ** Momentum functions removed. Reference Ashhar Farhan's original code to add it back **
*/

#include "ubitx.h"

/* file-level variables */

/* normal encoder state */
static uint8_t m_previousEncoderState = 0;
static int8_t m_encoderCount = 0;

/*
  returns a two-bit number such that each bit reflects the current
  value of each of the two phases of the encoder
*/
uint8_t encoderState()
{
  return (digitalRead(ENC_A) ? 1 : 0) + (digitalRead(ENC_B) ? 2 : 0);
}

/*
  SmittyHalibut's encoder handling, using interrupts. Should be quicker, smoother handling.
  The Interrupt Service Routine for Pin Change Interrupts on A0-A5.
*/
ISR (PCINT1_vect)
{
  uint8_t currentEncoderState = encoderState();

  if (m_previousEncoderState == currentEncoderState)  // unnecessary ISR
    return;

  // these transitions point to the encoder being rotated counter-clockwise
  if ((m_previousEncoderState == 0 && currentEncoderState == 2) ||
      (m_previousEncoderState == 2 && currentEncoderState == 3) ||
      (m_previousEncoderState == 3 && currentEncoderState == 1) ||
      (m_previousEncoderState == 1 && currentEncoderState == 0))
    m_encoderCount -= 1;

  // these transitions point to the encoder being rotated clockwise
  else if ((m_previousEncoderState == 0 && currentEncoderState == 1) ||
           (m_previousEncoderState == 1 && currentEncoderState == 3) ||
           (m_previousEncoderState == 3 && currentEncoderState == 2) ||
           (m_previousEncoderState == 2 && currentEncoderState == 0))
    m_encoderCount += 1;

  m_previousEncoderState = currentEncoderState;  // record state for next pulse interpretation
}

/*
  Set up the encoder interrupts and global variables.
*/
void pciSetup (uint8_t pin)
{
  *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
  PCIFR |= bit (digitalPinToPCICRbit(pin));  // clear any outstanding interrupt
  PCICR |= bit (digitalPinToPCICRbit(pin));  // enable interrupt for the group
}

/* set up encoder */
void encoderSetup ()
{
  m_encoderCount = 0;
  m_previousEncoderState = encoderState();

  // setup Pin Change Interrupts for the encoder inputs
  pciSetup(ENC_A);
  pciSetup(ENC_B);
}

/*
  returns the number of ticks in a short interval, +ve in clockwise, -ve in counter-clockwise
*/
int16_t encoderRead ()
{
  if (m_encoderCount != 0)
  {
    int16_t ret = m_encoderCount;

    m_encoderCount = 0;
    return ret;
  }

  return 0;
}
