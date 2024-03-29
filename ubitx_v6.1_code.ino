/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
#include "ubitx.h"
#include "nano_gui.h"

/*
    The Arduino works by executing the code in a function called setup() and then it
    repeatedly keeps calling loop() forever. All the initialization code is kept in setup()
    and code to continuously sense the tuning knob, the function button, transmit / receive,
    etc is all in the loop() function. If you wish to study the code top down, then scroll
    to the bottom of this file and read your way up.

    The Arduino, unlike C/C++ on a regular computer with gigabytes of RAM, has very little memory.
    We have to be very careful with variables that are declared inside the functions as they are
    created in a memory region called the stack. The stack has just a few bytes of space on the Arduino
    if you declare large strings inside functions, they can easily exceed the capacity of the stack
    and corrupt your programs.

    We circumvent this by declaring a couple of global buffers as 'kitchen counters' where we can
    slice and dice our strings. These strings are mostly used to control the display or handle
    the input and output from the USB port.
*/
char gbuffB[30];
char gbuffC[30];

uint8_t vfoActive = VFO_A;

uint32_t vfoA = 7150000L;
uint32_t vfoB = 14200000L;
uint32_t sideTone = 800;
uint32_t usbCarrier;

uint32_t frequency;
uint32_t ritRxFrequency;
uint32_t ritTxFrequency;  // frequency is the current frequency on the dial
uint32_t firstIF = 45005000L;
int32_t calibration = 0;

bool ritOn = false;
bool isUsbVfoA = false;
bool isUsbVfoB = true;
bool cwMode = false; // if cwMode is on, the rx frequency is tuned down by sidetone hz instead of being zerobeat

/* these are variables that control the keyer behavior */
uint16_t cwSpeed = 100;  // dot period in milliseconds
uint16_t cwDelayTime = 60;
bool iambicKey = true;
uint8_t keyerControl = IAMBICB;

/*
   Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
*/
bool txCAT = false;           // turned on if the transmitting due to a CAT command
bool inTx = false;            // it is set to 1 if in transmit mode (whatever the reason : cw, ptt or cat)
bool splitOn = false;         // working split, uses VFO B as the transmit frequency
bool isUSB = false;           // upper sideband was selected, this is reset to the default for the
                              // frequency when it crosses the frequency border of 10 MHz
bool menuOn = false;          // set to 1 when the menu is being displayed, if a menu item sets it to zero, the menu is exited
uint32_t cwTimeout = 0;       // milliseconds to go before the cw transmit line is released and the radio goes back to rx mode


/*
   Below are the basic functions that control the uBitx. Understanding the functions before
   you start hacking around
*/

/*
   our custom delay. during any delay, the raduino should still be processing a few times
*/
void activeDelay(uint16_t delayBy) {

  uint32_t timeStart = millis();

  while (millis() - timeStart <= (uint32_t)delayBy) {
    delay(10);

    // background work
    checkCAT();
  }
}


/* save the state and frequency of the vfos */
void saveVFOs() {

  if (vfoActive == VFO_A)
    EEPROM.put(VFO_A, frequency);
  else
    EEPROM.put(VFO_A, vfoA);

  if (isUsbVfoA)
    EEPROM.put(VFO_A_MODE, VFO_MODE_USB);
  else
    EEPROM.put(VFO_A_MODE, VFO_MODE_LSB);

  if (vfoActive == VFO_B)
    EEPROM.put(VFO_B, frequency);
  else
    EEPROM.put(VFO_B, vfoB);

  if (isUsbVfoB)
    EEPROM.put(VFO_B_MODE, VFO_MODE_USB);
  else
    EEPROM.put(VFO_B_MODE, VFO_MODE_LSB);
}


/*
   Select the properly tx harmonic filters
   The four harmonic filters use only three relays
   the four LPFs cover 30-21 Mhz, 18 - 14 Mhz, 7-10 MHz and 3.5 to 5 Mhz
   Briefly, it works like this,
   - When KT1 is OFF, the 'off' position routes the PA output through the 30 MHz LPF
   - When KT1 is ON, it routes the PA output to KT2. Which is why you will see that
     the KT1 is on for the three other cases.
   - When the KT1 is ON and KT2 is off, the off position of KT2 routes the PA output
     to 18 MHz LPF (That also works for 14 Mhz)
   - When KT1 is On, KT2 is On, it routes the PA output to KT3
   - KT3, when switched on selects the 7-10 Mhz filter
   - KT3 when switched off selects the 3.5-5 Mhz filter
   See the circuit to understand this
*/
void setTXFilters(uint32_t freq) {

  if (freq > 21000000L) {
    // the default filter is with 35 MHz cut-off
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq >= 14000000L) {
    // thrown the KT1 relay on, the 30 MHz LPF is bypassed and the 14-18 MHz LPF is allowed to go through
    digitalWrite(TX_LPF_A, 1);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq > 7000000L) {
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 1);
    digitalWrite(TX_LPF_C, 0);
  } else {
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 1);
  }
}


/*
   This is the most frequently called function that configures the
   radio to a particular frequency and sideband

   The carrier oscillator of the detector/modulator is permanently fixed at
   uppper sideband. The sideband selection is done by placing the second oscillator
   either 12 Mhz below or above the 45 Mhz signal thereby inverting the sidebands
   through mixing of the second local oscillator.
*/
void setFrequency(uint32_t f) {
  //setTXFilters(f);  // <<<--- commented out here, enabled in startTx() instead

  // moved the setTXFilters call from here to startTX because:
  // * concerned about extra cycles being used during tuning because
  //   tuning happens extremely frequently
  // * concerned about longevity of Nano storage due to
  //   frequent write calls.  It's assumed that startTX is
  //   called much less frequently than setFrequency is

  // alternative(?) to reduce the intermod spur
  if (isUSB) {

    if (cwMode)
      si5351bxSetFreq(2, firstIF  + f + sideTone);
    else
      si5351bxSetFreq(2, firstIF  + f);

    si5351bxSetFreq(1, firstIF + usbCarrier);

  } else {

    if (cwMode)
      si5351bxSetFreq(2, firstIF  + f + sideTone);
    else
      si5351bxSetFreq(2, firstIF + f);

    si5351bxSetFreq(1, firstIF - usbCarrier);
  }

  frequency = f;
}

/*
   startTx is called by the PTT, cw keyer and CAT protocol to
   put the uBitx in tx mode. It takes care of rit settings, sideband settings and setting up TX filtering

   Note: In cw mode, this doesn't key the radio, only puts it in tx mode
   CW offset is calculated as lower than the operating frequency when in LSB mode, and vice versa in USB mode

   The transmit filter relays are powered up only during the tx so they dont
   draw any current during rx.
*/
void startTx(uint8_t txMode) {

  digitalWrite(TX_RX, 1);

  inTx = true;

  if (ritOn) {
    // save the current as the rx frequency
    ritRxFrequency = frequency;
    setTXFilters(ritTxFrequency);  // <<<--- moved from setFrequency
    setFrequency(ritTxFrequency);
  } else {
    if (splitOn == true) {
      if (vfoActive == VFO_B) {
        vfoActive = VFO_A;
        isUSB = isUsbVfoA;
        frequency = vfoA;
      }
      else if (vfoActive == VFO_A) {
        vfoActive = VFO_B;
        frequency = vfoB;
        isUSB = isUsbVfoB;
      }
    }

    setTXFilters(frequency);  // <<<--- moved from setFrequency
    setFrequency(frequency);
  }

  if (txMode == TX_CW) {
    digitalWrite(TX_RX, 0);

    // turn off the second local oscillator and the bfo
    si5351bxSetFreq(0, 0);
    si5351bxSetFreq(1, 0);

    // shift the first oscillator to the tx frequency directly
    // the key up and key down will toggle the carrier unbalancing
    // the exact cw frequency is the tuned frequency + sidetone
    if (isUSB)
      si5351bxSetFreq(2, frequency + sideTone);
    else
      si5351bxSetFreq(2, frequency - sideTone);

    delay(20);

    digitalWrite(TX_RX, 1);
  }

  drawTx();
}


/* turn off TX mode */
void stopTx() {

  inTx = false;

  digitalWrite(TX_RX, 0);           // turn off the tx

  si5351bxSetFreq(0, usbCarrier);  // set back the carrier oscillator, cw tx switches it off

  if (ritOn)
    setFrequency(ritRxFrequency);
  else {
    if (splitOn == true) {
      // vfo Change
      if (vfoActive == VFO_B) {
        vfoActive = VFO_A;
        frequency = vfoA;
        isUSB = isUsbVfoA;
      }
      else if (vfoActive == VFO_A) {
        vfoActive = VFO_B;
        frequency = vfoB;
        isUSB = isUsbVfoB;
      }
    }

    setFrequency(frequency);
  }

  drawTx();
}


/*
   ritEnable is called with a frequency parameter that determines
   what the tx frequency will be
*/
void ritEnable(uint32_t f) {
  ritOn = true;
  // save the non-rit frequency back into the VFO memory
  // as RIT is a temporary shift, this is not saved to EEPROM
  ritTxFrequency = f;
}


/* this is called by the RIT menu routine */
void ritDisable() {
  if (ritOn) {
    ritOn = false;
    setFrequency(ritTxFrequency);
    // updateDisplay();  // <<<---
    displayVFO(vfoActive);
  }
}

/*
   Basic User Interface Routines. These check the front panel for any activity
*/

/*
   The PTT is checked only if we are not already in a cw transmit session
   If the PTT is pressed, we shift to the ritbase if the rit was on
   flip the T/R line to T and update the display to denote transmission
*/
void checkPTT() {
  // we don't check for ptt when transmitting cw
  if (cwTimeout > 0)
    return;

  if (digitalRead(PTT) == 0 && inTx == false) {
    startTx(TX_SSB);

    activeDelay(50); // debounce the PTT
  }

  if (digitalRead(PTT) == 1 && inTx == true)
    stopTx();
}


/* check if the encoder button was pressed */
void checkButton() {
  // only if the button is pressed
  if (!encoderButtonDown())
    return;

  activeDelay(50);

  if (!encoderButtonDown()) // debounce
    return;

  // disengage any CAT work
  // doingCAT = false;

  uint16_t downTime = 0;

  while (encoderButtonDown()) {
    activeDelay(10);

    downTime++;

    if (downTime > 300) {
      doSetupMenu();
      return;
    }
  }

  activeDelay(100);

  doCommands();

  // wait for the button to go up again
  while (encoderButtonDown())
    activeDelay(10);

  activeDelay(50); // debounce
}


/* */
void switchVFO(uint8_t vfoSelect) {  // was int vfoSelect

  if (vfoSelect == VFO_A) {
    if (vfoActive == VFO_B) {
      vfoB = frequency;
      isUsbVfoB = isUSB;

      EEPROM.put(VFO_B, frequency);

      if (isUsbVfoB)
        EEPROM.put(VFO_B_MODE, VFO_MODE_USB);
      else
        EEPROM.put(VFO_B_MODE, VFO_MODE_LSB);
    }

    vfoActive = VFO_A;

    frequency = vfoA;
    isUSB = isUsbVfoA;
  } else {
    if (vfoActive == VFO_A) {
      vfoA = frequency;
      isUsbVfoA = isUSB;

      EEPROM.put(VFO_A, frequency);

      if (isUsbVfoA)
        EEPROM.put(VFO_A_MODE, VFO_MODE_USB);
      else
        EEPROM.put(VFO_A_MODE, VFO_MODE_LSB);
    }

    vfoActive = VFO_B;

    frequency = vfoB;
    isUSB = isUsbVfoB;
  }

  setFrequency(frequency);
  redrawVFOs();
  saveVFOs();
}


/*
   The tuning jumps by 50 Hz on each step when you tune slowly
   As you spin the encoder faster, the jump size also increases
   This way, you can quickly move to another band by just spinning the
   tuning knob (if that makes you happy)
*/
void doTuning() {

  int16_t s;
  static uint32_t prevFrequency;
  static uint32_t nextFrequencyUpdate = 0;

  uint32_t now = millis();

  // update the vfo display if it's time
  if (now >= nextFrequencyUpdate && prevFrequency != frequency) {
    displayVFO(vfoActive);
    nextFrequencyUpdate = now + 250; // was 500;  // <<<--- want to speed up display a little
    prevFrequency = frequency;
  }

  s = encoderRead();

  // encoder is at 0, nothing to see here, move along!
  if (!s)
    return;

  // doingCAT = false; // go back to manual mode if you were doing CAT (disabled from factory)
  prevFrequency = frequency;

  // the following add a number multiplied by a
  // *positive* number to *increase* the frequency value
  // when the encoder spins *up* the dial
  // ---
  if (s > 10)
    frequency += 200l * s;  // forward fastest
  else if (s > 5)
    frequency += 100l * s;  // forward faster
  else if (s > 0)
    frequency += 50l * s;   // forward normal
  // ---
  // the following add a number multiplied by a
  // *negative* number to *reduce* the frequency value
  // when the encoder spins *down* the dial
  // ---
  else if (s < -10)
    frequency += 200l * s;  // backward fastest
  else if (s < -5)
    frequency += 100l * s;  // backward faster
  else if (s < 0)
    frequency += 50l * s;   // backward normal

  // set USB or LSB depending on the frequency
  if (prevFrequency < 10000000l && frequency > 10000000l)
    isUSB = true;

  if (prevFrequency > 10000000l && frequency < 10000000l)
    isUSB = false;

  setFrequency(frequency);
}


/*
   RIT only steps back and forth by 100 hz at a time
*/
void doRITTuning() {

  int16_t knob = encoderRead();
  uint32_t oldFreq = frequency;

  if (knob < 0)
    frequency -= 100l;
  else if (knob > 0)
    frequency += 100;

  if (oldFreq != frequency) {
    setFrequency(frequency);

    displayVFO(vfoActive);
  }
}


/*
   settings are read from EEPROM. The first time around, the values may not be
   present or out of range, in this case, some sane defaults are copied into the
   variables
*/
void initSettings() {

  uint8_t value;

  // read the settings from the eeprom and restore them
  // if the readings are off, then set defaults
  EEPROM.get(MASTER_CAL, calibration);
  EEPROM.get(USB_CAL, usbCarrier);
  EEPROM.get(VFO_A, vfoA);
  EEPROM.get(VFO_B, vfoB);
  EEPROM.get(CW_SIDETONE, sideTone);
  EEPROM.get(CW_SPEED, cwSpeed);
  EEPROM.get(CW_DELAYTIME, cwDelayTime);

  if (usbCarrier > 11060000l || usbCarrier < 11048000l)
    usbCarrier = 11052000l;
  if (vfoA > 35000000l || 3500000l > vfoA)
    vfoA = 7150000l;
  if (vfoB > 35000000l || 3500000l > vfoB)
    vfoB = 14150000l;
  if (sideTone < 100 || 2000 < sideTone)
    sideTone = 800;
  if (cwSpeed < 10 || 1000 < cwSpeed)
    cwSpeed = 100;
  if (cwDelayTime < 10 || cwDelayTime > 100)
    cwDelayTime = 50;


  // the VFO modes are read in as either 2 (USB) or 3(LSB), 0, the default
  // is taken as 'uninitialized'
  EEPROM.get(VFO_A_MODE, value);

  switch (value) {
    case VFO_MODE_USB:
      isUsbVfoA = true;
      break;

    case VFO_MODE_LSB:
      isUsbVfoA = false;
      break;

    default:
      if (vfoA > 10000000l)
        isUsbVfoA = true;
      else
        isUsbVfoA = false;
  }

  EEPROM.get(VFO_B_MODE, value);

  switch (value) {
    case VFO_MODE_USB:
      isUsbVfoB = true;
      break;

    case VFO_MODE_LSB:
      isUsbVfoB = false;
      break;

    default:
      if (vfoA > 10000000l)
        isUsbVfoB = true;
      else
        isUsbVfoB = false;
  }

  // set the current mode
  isUSB = isUsbVfoA;

  // The keyer type splits into two variables
  EEPROM.get(CW_KEY_TYPE, value);

  if (value == 0)
    iambicKey = false;
  else if (value == 1) {
    iambicKey = true;
    keyerControl &= ~IAMBICB;
  }
  else if (value == 2) {
    iambicKey = true;
    keyerControl |= IAMBICB;
  }
}


/* set up pin modes */
void initPins() {

  analogReference(DEFAULT);

  // set up encoder and encoder button pins
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(FBUTTON, INPUT_PULLUP);

  pinMode(PTT, INPUT_PULLUP);

  pinMode(PIN_CW_TONE, OUTPUT);
  digitalWrite(PIN_CW_TONE, 0);

  pinMode(TX_RX, OUTPUT);
  digitalWrite(TX_RX, 0);

  pinMode(TX_LPF_A, OUTPUT);
  pinMode(TX_LPF_B, OUTPUT);
  pinMode(TX_LPF_C, OUTPUT);
  digitalWrite(TX_LPF_A, 0);
  digitalWrite(TX_LPF_B, 0);
  digitalWrite(TX_LPF_C, 0);

  pinMode(CW_KEY, OUTPUT);
  digitalWrite(CW_KEY, 0);
}


/*
  'native' setup routine - part of a typical Arduino sketch

  sets up serial output, inits display, settings, pins, oscillators and more
*/
void setup() {

  Serial.begin(38400);
  Serial.flush();

  displayInit();
  initSettings();
  initPins();
  initOscillators();

  frequency = vfoA;
  setFrequency(vfoA);

  encoderSetup();

  // do essential calibrations / setup when
  // encoder button is down during power-on
  // (almost looks like factory calibration trigger)
  if (encoderButtonDown()) {

    doTouchCalibration();

    isUSB = true;
    setFrequency(10000000l);

    setupFreq();

    isUSB = false;
    setFrequency(7100000l);

    setupBFO();
  }

  // draw home screen, drawing full
  // background and refreshing both vfos
  guiUpdate(true, true);
}


/*
   'native' main loop - part of a typical Arduino sketch

   checks for keydown, ptt, function button and tuning
*/
void loop() {

  if (cwMode)
    cwKeyer();
  else if (!txCAT)
    checkPTT();

  checkButton();

  // tune only when not tranmsitting
  if (!inTx) {
    if (ritOn)
      doRITTuning();
    else
      doTuning();

    checkTouch();
  }

  checkCAT();
}
