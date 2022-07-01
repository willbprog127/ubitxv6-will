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
    and mess up your programs.

    We circumvent this by declaring a couple of global buffers as kitchen counters where we can
    slice and dice our strings. These strings are mostly used to control the display or handle
    the input and output from the USB port. We must keep a count of the bytes used while reading
    the serial port as we can easily run out of buffer space. This is done in the serial_in_count variable.
*/
char gbuffB[30];
char gbuffC[30];

bool ritOn = false;
byte vfoActive = VFO_A;

unsigned long vfoA = 7150000L;
unsigned long vfoB = 14200000L;
unsigned long sideTone = 800;
unsigned long usbCarrier;

bool isUsbVfoA = false;
bool isUsbVfoB = true;

unsigned long frequency;
unsigned long ritRxFrequency;
unsigned long ritTxFrequency;  // frequency is the current frequency on the dial
unsigned long firstIF = 45005000L;

bool cwMode = false; // if cwMode is flipped on, the rx frequency is tuned down by sidetone hz instead of being zerobeat

/* these are variables that control the keyer behaviour */
int cwSpeed = 100;  // this is actually the dot period in milliseconds
int32_t calibration = 0;
int cwDelayTime = 60;
bool Iambic_Key = true;

unsigned char keyerControl = IAMBICB;
// during CAT commands, we will freeze the display until CAT is disengaged
// bool doingCAT = false;


/*
   Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
*/
bool txCAT = false;           // turned on if the transmitting due to a CAT command
bool inTx = false;            // it is set to 1 if in transmit mode (whatever the reason : cw, ptt or cat)
bool splitOn = false;         // working split, uses VFO B as the transmit frequency
bool isUSB = false;           // upper sideband was selected, this is reset to the default for the
                              // frequency when it crosses the frequency border of 10 MHz
bool menuOn = false;          // set to 1 when the menu is being displayed, if a menu item sets it to zero, the menu is exited
unsigned long cwTimeout = 0;  // milliseconds to go before the cw transmit line is released and the radio goes back to rx mode


/*
   Below are the basic functions that control the uBitx. Understanding the functions before
   you start hacking around
*/

/*
   Our own delay. During any delay, the raduino should still be processing a few times.
*/
void active_delay(int delay_by) {
  unsigned long timeStart = millis();

  while (millis() - timeStart <= (unsigned long)delay_by) {
    delay(10);

    // background work
    checkCAT();
  }
}


/* */
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
void setTXFilters(unsigned long freq) {

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
void setFrequency(unsigned long f)
{
  //setTXFilters(f);  // <<<--- commented out here, enabled in startTx() instead

  // moved the setTXFilters call from here to startTX because:
  // * concerned about extra cycles being used during tuning because
  //   tuning happens extremely frequently
  // * concerned about longevity of Nano storage due to
  //   frequent write calls.  It's assumed that startTX is
  //   called much less frequently than setFrequency is

  /*
    if (isUSB) {
      si5351bx_setfreq(2, firstIF  + f);
      si5351bx_setfreq(1, firstIF + usbCarrier);
    }
    else{
      si5351bx_setfreq(2, firstIF + f);
      si5351bx_setfreq(1, firstIF - usbCarrier);
    }
  */
  // alternative to reduce the intermod spur
  if (isUSB) {
    if (cwMode)
      si5351bx_setfreq(2, firstIF  + f + sideTone);
    else
      si5351bx_setfreq(2, firstIF  + f);

    si5351bx_setfreq(1, firstIF + usbCarrier);
  } else {
    if (cwMode)
      si5351bx_setfreq(2, firstIF  + f + sideTone);
    else
      si5351bx_setfreq(2, firstIF + f);

    si5351bx_setfreq(1, firstIF - usbCarrier);
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
void startTx(byte txMode) {
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
    si5351bx_setfreq(0, 0);
    si5351bx_setfreq(1, 0);

    // shift the first oscillator to the tx frequency directly
    // the key up and key down will toggle the carrier unbalancing
    // the exact cw frequency is the tuned frequency + sidetone
    if (isUSB)
      si5351bx_setfreq(2, frequency + sideTone);
    else
      si5351bx_setfreq(2, frequency - sideTone);

    delay(20);
    digitalWrite(TX_RX, 1);
  }

  drawTx();
}


/* turn off TX mode */
void stopTx() {
  inTx = false;

  digitalWrite(TX_RX, 0);           // turn off the tx
  si5351bx_setfreq(0, usbCarrier);  // set back the carrier oscillator anyway, cw tx switches it off

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
void ritEnable(unsigned long f) {
  ritOn = true;
  //save the non-rit frequency back into the VFO memory
  //as RIT is a temporary shift, this is not saved to EEPROM
  ritTxFrequency = f;
}


/* this is called by the RIT menu routine */
void ritDisable() {
  if (ritOn) {
    ritOn = false;
    setFrequency(ritTxFrequency);
    updateDisplay();
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

    active_delay(50); // debounce the PTT
  }

  if (digitalRead(PTT) == 1 && inTx == true)
    stopTx();
}


/* check if the encoder button was pressed */
void checkButton() {
  // only if the button is pressed
  if (!btnDown())
    return;

  active_delay(50);

  if (!btnDown()) // debounce
    return;

  // disengage any CAT work
  // doingCAT = false;

  int downTime = 0;

  while (btnDown()) {
    active_delay(10);

    downTime++;

    if (downTime > 300) {
      doSetup2();
      return;
    }
  }

  active_delay(100);

  doCommands();

  // wait for the button to go up again
  while (btnDown())
    active_delay(10);

  active_delay(50); // debounce
}


/* */
void switchVFO(int vfoSelect) {
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
    //      printLine2("Selected VFO A  ");
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
    //      printLine2("Selected VFO B  ");
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
   tuning knob
*/
void doTuning() {
  int s;
  static unsigned long prev_freq;
  static unsigned long nextFrequencyUpdate = 0;

  unsigned long now = millis();

  if (now >= nextFrequencyUpdate && prev_freq != frequency) {
    updateDisplay();
    nextFrequencyUpdate = now + 500;
    prev_freq = frequency;
  }

  s = enc_read();

  if (!s)
    return;

  // doingCAT = false; // go back to manual mode if you were doing CAT
  prev_freq = frequency;

  if (s > 10)
    frequency += 200l * s;
  else if (s > 5)
    frequency += 100l * s;
  else if (s > 0)
    frequency += 50l * s;
  else if (s < -10)
    frequency += 200l * s;
  else if (s < -5)
    frequency += 100l * s;
  else if (s  < 0)
    frequency += 50l * s;

  if (prev_freq < 10000000l && frequency > 10000000l)
    isUSB = true;

  if (prev_freq > 10000000l && frequency < 10000000l)
    isUSB = false;

  setFrequency(frequency);
}


/*
   RIT only steps back and forth by 100 hz at a time
*/
void doRIT() {
  // unsigned long newFreq;

  int knob = enc_read();
  unsigned long old_freq = frequency;

  if (knob < 0)
    frequency -= 100l;
  else if (knob > 0)
    frequency += 100;

  if (old_freq != frequency) {
    setFrequency(frequency);
    updateDisplay();
  }
}


/*
   The settings are read from EEPROM. The first time around, the values may not be
   present or out of range, in this case, some intelligent defaults are copied into the
   variables.
*/
void initSettings() {
  byte x;

  // read the settings from the eeprom and restore them
  // if the readings are off, then set defaults
  EEPROM.get(MASTER_CAL, calibration);
  EEPROM.get(USB_CAL, usbCarrier);
  EEPROM.get(VFO_A, vfoA);
  EEPROM.get(VFO_B, vfoB);
  EEPROM.get(CW_SIDETONE, sideTone);
  EEPROM.get(CW_SPEED, cwSpeed);
  EEPROM.get(CW_DELAYTIME, cwDelayTime);

  // the screen calibration parameters : int slope_x=104, slope_y=137, offset_x=28, offset_y=29;

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


  // The VFO modes are read in as either 2 (USB) or 3(LSB), 0, the default
  // is taken as 'uninitialized'
  EEPROM.get(VFO_A_MODE, x);

  switch (x) {
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

  EEPROM.get(VFO_B_MODE, x);

  switch (x) {
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
  EEPROM.get(CW_KEY_TYPE, x);

  if (x == 0)
    Iambic_Key = false;
  else if (x == 1) {
    Iambic_Key = true;
    keyerControl &= ~IAMBICB;
  }
  else if (x == 2) {
    Iambic_Key = true;
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

  // configure the function button to use the external pull-up
  //  pinMode(FBUTTON, INPUT);
  //  digitalWrite(FBUTTON, HIGH);

  pinMode(PTT, INPUT_PULLUP);
  //  pinMode(ANALOG_KEYER, INPUT_PULLUP);

  pinMode(CW_TONE, OUTPUT);
  digitalWrite(CW_TONE, 0);

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
  'native' setup routine - required by Arduino

  sets up serial output, inits display, settings, pins, oscillators and more
*/
void setup()
{
  Serial.begin(38400);
  Serial.flush();

  displayInit();
  initSettings();
  initPins();
  initOscillators();
  frequency = vfoA;
  setFrequency(vfoA);
  enc_setup();

  if (btnDown()) {
    doTouchCalibration();
    isUSB = true;
    setFrequency(10000000l);
    setupFreq();
    isUSB = false;
    setFrequency(7100000l);
    setupBFO();
  }

  guiUpdate();
  displayRawText("v6.1", 270, 210, DISPLAY_LIGHTGREY, DISPLAY_NAVY);
}


/*
   'native' main loop - required by Arduino

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
      doRIT();
    else
      doTuning();

    checkTouch();
  }

  checkCAT();
}
