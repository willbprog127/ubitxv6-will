/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include "ubitx.h"

/*
  CW Keyer
  CW Key logic change with ron's code (ubitx_keyer.cpp)
  Ron's logic has been modified to work with the original uBITX by KD8CEC

  Original Comment ----------------------------------------------------------------------------
   The CW keyer handles either a straight key or an iambic / paddle key.
   They all use just one analog input line. This is how it works.
   The analog line has the internal pull-up resistor enabled.
   When a straight key is connected, it shorts the pull-up resistor, analog input is 0 volts
   When a paddle is connected, the dot and the dash are connected to the analog pin through
   a 10K and a 2.2K resistors. These produce a 4v and a 2v input to the analog pins.
   So, the readings are as follows :
   0v - straight key
   1-2.5 v - paddle dot
   2.5 to 4.5 v - paddle dash
   2.0 to 0.5 v - dot and dash pressed

   The keyer is written to transparently handle all these cases

   Generating CW
   The CW is cleanly generated by unbalancing the front-end mixer
   and putting the local oscillator directly at the CW transmit frequency.
   The sidetone, generated by the Arduino is injected into the volume control
*/

// in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs //  <<<--- what parameter???

/* Variables for Ron's new logic */
#define DIT_L 0x01 // DIT latch
#define DAH_L 0x02 // DAH latch
#define DIT_PROC 0x04 // DIT is being processed

enum KSTYPE {
  IDLE,
  CHK_DIT,
  CHK_DAH,
  KEYED_PREP,
  KEYED,
  INTER_ELEMENT
};

/* CW ADC Range */
const uint8_t cwAdcSTFrom = 0;
const uint8_t cwAdcSTTo = 50;
const uint8_t cwAdcBothFrom = 51;
const uint16_t cwAdcBothTo = 300;
const uint16_t cwAdcDotFrom = 301;
const uint16_t cwAdcDotTo = 600;
const uint16_t cwAdcDashFrom = 601;
const uint16_t cwAdcDashTo = 800;

uint8_t delayBeforeCWStartTime = 50;

static uint32_t ktimer;
uint8_t keyerState = IDLE;  // was unsigned char


/*
   Starts transmitting the carrier with the sidetone
   It assumes that we have called cwTxStart and not called cwTxStop
   each time it is called, the cwTimeOut is pushed further into the future
*/
void cwKeydown() {

  tone(PIN_CW_TONE, (int16_t)sideTone);
  digitalWrite(CW_KEY, 1);

  // Modified by KD8CEC, for CW Delay Time save to eeprom
  cwTimeout = millis() + cwDelayTime * 10;
}


/*
   Stops the cw carrier transmission along with the sidetone
   Pushes the cwTimeout further into the future
*/
void cwKeyUp() {

  noTone(PIN_CW_TONE);
  digitalWrite(CW_KEY, 0);

  // Modified by KD8CEC, for CW Delay Time save to eeprom
  cwTimeout = millis() + cwDelayTime * 10;
}


/*
    test to reduce the keying error. do not delete lines
    created by KD8CEC for compatible with new CW Logic
*/
uint8_t updatePaddleLatch(uint8_t isUpdateKeyState) {

  uint8_t tmpKeyerControl = 0;  // was unsigned char

  uint16_t paddle = analogRead(ANALOG_KEYER);  // <<<--- changed from int because docs
                                               //        say analogRead() maxes out at 1024 for nano
  // use the PTT as the key for tune up, quick QSOs
  if (digitalRead(PTT) == 0)
    tmpKeyerControl |= DIT_L;
  else if (paddle >= cwAdcDashFrom && paddle <= cwAdcDashTo)
    tmpKeyerControl |= DAH_L;
  else if (paddle >= cwAdcDotFrom && paddle <= cwAdcDotTo)
    tmpKeyerControl |= DIT_L;
  else if (paddle >= cwAdcBothFrom && paddle <= cwAdcBothTo)
    tmpKeyerControl |= (DAH_L | DIT_L) ;
  else
  {
    if (iambicKey)
      tmpKeyerControl = 0 ;
    else if (paddle <= cwAdcSTTo)  // <<<--- changed from paddle >= cwAdcSTFrom && because paddle is uint
      tmpKeyerControl = DIT_L ;
    else
      tmpKeyerControl = 0 ;
  }

  if (isUpdateKeyState == 1)
    keyerControl |= tmpKeyerControl;

  return tmpKeyerControl;
}


/*
   New keyer logic, by RON
   modified by KD8CEC
*/
void cwKeyer(void) {

  bool continueLoop = true;
  uint8_t tmpKeyControl = 0;  // was unsigned int

  if (iambicKey) {
    while (continueLoop) {
      switch (keyerState) {
        case IDLE:

          tmpKeyControl = updatePaddleLatch(0);

          if (tmpKeyControl == DAH_L || tmpKeyControl == DIT_L ||
              tmpKeyControl == (DAH_L | DIT_L) || (keyerControl & 0x03)) {
            updatePaddleLatch(1);
            keyerState = CHK_DIT;
          } else {
            if (0 < cwTimeout && cwTimeout < millis()) {
              cwTimeout = 0;
              stopTx();
            }

            continueLoop = false;
          }
          break;

        case CHK_DIT:
          if (keyerControl & DIT_L) {
            keyerControl |= DIT_PROC;
            ktimer = cwSpeed;
            keyerState = KEYED_PREP;
          } else {
            keyerState = CHK_DAH;
          }
          break;

        case CHK_DAH:
          if (keyerControl & DAH_L) {
            ktimer = cwSpeed * 3;
            keyerState = KEYED_PREP;
          } else {
            keyerState = IDLE;
          }
          break;

        case KEYED_PREP:
          // modified KD8CEC
          if (!inTx) {
            // DelayTime Option
            activeDelay(delayBeforeCWStartTime * 2);

            // keyDown = 0;
            cwTimeout = millis() + cwDelayTime * 10;  //+ CW_TIMEOUT;
            startTx(TX_CW);
          }

          ktimer += millis(); // set ktimer to interval end time
          keyerControl &= ~(DIT_L + DAH_L); // clear both paddle latch bits
          keyerState = KEYED; // next state

          cwKeydown();
          break;

        case KEYED:
          if (millis() > ktimer) { // are we at end of key down ?
            cwKeyUp();
            ktimer = millis() + cwSpeed; // inter-element time
            keyerState = INTER_ELEMENT; // next state
          } else if (keyerControl & IAMBICB) {
            updatePaddleLatch(1); // early paddle latch in Iambic B mode
          }
          break;

        case INTER_ELEMENT:
          // Insert time between dits/dahs
          updatePaddleLatch(1); // latch paddle state

          if (millis() > ktimer) { // are we at end of inter-space ?
            if (keyerControl & DIT_PROC) { // was it a dit or dah ?
              keyerControl &= ~(DIT_L + DIT_PROC); // clear two bits
              keyerState = CHK_DAH; // dit done, check for dah
            } else {
              keyerControl &= ~(DAH_L); // clear dah latch
              keyerState = IDLE; // go idle
            }
          }
          break;
      }

      checkCAT();
    } // end of while
  }
  else {
    while (true) {
      uint8_t state = updatePaddleLatch(0);

      if (state == DIT_L) {
        // if we are here, it is only because the key is pressed
        if (!inTx) {
          startTx(TX_CW);

          // DelayTime Option
          activeDelay(delayBeforeCWStartTime * 2);

          // keyDown = 0;
          cwTimeout = millis() + cwDelayTime * 10;  //+ CW_TIMEOUT;
        }

        cwKeydown();

        while (updatePaddleLatch(0) == DIT_L)
          activeDelay(1);

        cwKeyUp();
      } else {
        if (0 < cwTimeout && cwTimeout < millis()) {
          cwTimeout = 0;
          // keyDown = 0;
          stopTx();
        }

        return;  // Tx stop control by Main Loop
      }

      checkCAT();
    } // end of while
  }   // end of else
}
