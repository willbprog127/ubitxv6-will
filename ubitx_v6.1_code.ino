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
  We have to **BE VERY CAREFUL** with variables that are declared inside the functions as they are
  created in a memory region called the stack. The stack has just a few bytes of space on the Arduino
  if you declare large strings inside functions, they can easily exceed the capacity of the stack
  and corrupt your programs.

  We circumvent this by declaring a couple of global buffers as 'kitchen counters' where we can
  slice and dice our strings. These strings are mostly used to control the display or handle
  the input and output from the USB port.
*/

/* global variables */
char g_buffB[30];
char g_buffC[30];

uint8_t g_vfoActive = VFO_A;

uint32_t g_vfoA = 7000000L;
uint32_t g_vfoB = 14000000L;
uint16_t g_sideTone = 600;
uint32_t g_usbCarrier;

uint32_t g_frequency;
uint32_t g_ritTxFrequency;  // frequency is the current frequency on the dial
int32_t g_calibration = 0;

bool g_ritOn = false;
bool g_cwMode = false;  // if g_cwMode is on, RX frequency is tuned down by sidetone Hz instead of being zerobeat

/* these are variables that control the keyer behavior */
uint16_t g_cwSpeed = 100;  // dot period in milliseconds
uint16_t g_cwDelayTime = 60;
bool g_iambicKey = true;
uint8_t g_keyerControl = IAMBICB;

/*
  Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
*/
bool g_txCAT = false;      // turned on if the transmitting due to a CAT command
bool g_inTx = false;       // it is set to 1 if in transmit mode (whatever the reason : CW, PTT or CAT)
bool g_splitOn = false;    // working split, uses VFO B as the transmit frequency
bool g_isUSB = false;      // upper sideband was selected, this is reset to the default for the
                           // frequency when it crosses the frequency border of 10 MHz
bool g_menuOn = false;     // set to 1 when the menu is being displayed, if a menu item sets it to zero, the menu is exited
uint32_t g_cwTimeout = 0;  // milliseconds to go before the CW transmit line is released and the radio goes back to RX mode

/* file-level variables */
static uint32_t m_ritRxFrequency;
static uint32_t m_firstIF = 45005000L;
static bool m_isUsbVfoA = false;
static bool m_isUsbVfoB = true;

/*
  Below are the basic functions that control the uBitX. Understand the functions before
  you start hacking around
*/

/*
  our custom delay. during any delay, the Raduino should still be processing a few times
*/
void activeDelay (uint16_t delayBy)
{
  uint32_t timeStart = millis();

  while (millis() - timeStart <= (uint32_t)delayBy)
  {
    delay(10);

    // background work
    checkCAT();
  }
}

/* save the state and frequency of the vfos */
void saveVFOs ()
{
  if (g_vfoActive == VFO_A)
    EEPROM.put(VFO_A, g_frequency);
  else
    EEPROM.put(VFO_A, g_vfoA);

  if (m_isUsbVfoA)
    EEPROM.put(VFO_A_MODE, VFO_MODE_USB);
  else
    EEPROM.put(VFO_A_MODE, VFO_MODE_LSB);

  if (g_vfoActive == VFO_B)
    EEPROM.put(VFO_B, g_frequency);
  else
    EEPROM.put(VFO_B, g_vfoB);

  if (m_isUsbVfoB)
    EEPROM.put(VFO_B_MODE, VFO_MODE_USB);
  else
    EEPROM.put(VFO_B_MODE, VFO_MODE_LSB);
}

/*
  select the proper TX harmonic filters
  filters are selected using relays KA1, KB1 and KC1
  see the circuit to understand this OR ELSE!!!
*/
void setTXFilters (uint32_t freq)
{
  if (freq > 21000000L)
  {
    // default LPF enabled with 35 MHz cut-off
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq >= 14000000L)
  {
    // LPF_A enabled - relay KA1
    digitalWrite(TX_LPF_A, 1);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq > 7000000L)
  {
    // LPF_B enabled - relay KB1
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 1);
    digitalWrite(TX_LPF_C, 0);
  }
  else
  {
    // LPF_C enabled - relay KC1
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 1);
  }
}

/*
  This is the most frequently called function that configures the
  radio to a particular frequency and sideband

  The carrier oscillator of the detector/modulator is permanently fixed at
  upper sideband. The sideband selection is done by placing the second oscillator
  either 12 MHz below or above the 45 MHz signal thereby inverting the sidebands
  through mixing of the second local oscillator.
*/
void setFrequency (uint32_t f)
{
  setTXFilters(f);

  // setup to reduce intermod spur
  if (g_isUSB)
  {
    if (g_cwMode)
      si5351bxSetFreq(2, m_firstIF + f + g_sideTone);
    else
      si5351bxSetFreq(2, m_firstIF + f);

    si5351bxSetFreq(1, m_firstIF + g_usbCarrier);
  }
  else
  {
    if (g_cwMode)
      si5351bxSetFreq(2, m_firstIF  + f + g_sideTone);
    else
      si5351bxSetFreq(2, m_firstIF + f);

    si5351bxSetFreq(1, m_firstIF - g_usbCarrier);
  }

  g_frequency = f;
}

/*
  startTx is called by the PTT, CW keyer and CAT protocol to
  put the uBitx in TX mode. It takes care of RIT settings, sideband settings
  and setting up TX filtering

  Note: In CW mode, this doesn't key the radio, only puts it in TX mode
  CW offset is calculated as lower than the operating frequency when in LSB
  mode, and vice versa in USB mode

  The transmit filter relays are powered up only during the TX so they dont
  draw any current during RX.
*/
void startTx (uint8_t txMode)
{
  digitalWrite(TX_RX, 1);

  g_inTx = true;

  if (g_ritOn)
  {
    // save the current as the RX frequency
    m_ritRxFrequency = g_frequency;
    setFrequency(g_ritTxFrequency);
  }
  else
  {
    if (g_splitOn)
    {
      if (g_vfoActive == VFO_B)
      {
        g_vfoActive = VFO_A;
        g_isUSB = m_isUsbVfoA;
        g_frequency = g_vfoA;
      }
      else if (g_vfoActive == VFO_A)
      {
        g_vfoActive = VFO_B;
        g_frequency = g_vfoB;
        g_isUSB = m_isUsbVfoB;
      }
    }

    setFrequency(g_frequency);
  }

  if (txMode == TX_CW)
  {
    digitalWrite(TX_RX, 0);

    // turn off the second local oscillator and the bfo
    si5351bxSetFreq(0, 0);
    si5351bxSetFreq(1, 0);

    // shift the first oscillator to the TX frequency directly
    // the key up and key down will toggle the carrier unbalancing
    // the exact CW frequency is the tuned frequency + sidetone
    if (g_isUSB)
      si5351bxSetFreq(2, g_frequency + g_sideTone);
    else
      si5351bxSetFreq(2, g_frequency - g_sideTone);

    delay(20);

    digitalWrite(TX_RX, 1);
  }

  drawTx();
}

/* turn off TX mode */
void stopTx()
{
  g_inTx = false;

  digitalWrite(TX_RX, 0);  // turn off the TX

  si5351bxSetFreq(0, g_usbCarrier);  // set back the carrier oscillator, CW TX switches it off

  if (g_ritOn)
    setFrequency(m_ritRxFrequency);
  else
  {
    if (g_splitOn)
    {
      // vfo Change
      if (g_vfoActive == VFO_B)
      {
        g_vfoActive = VFO_A;
        g_frequency = g_vfoA;
        g_isUSB = m_isUsbVfoA;
      }
      else if (g_vfoActive == VFO_A)
      {
        g_vfoActive = VFO_B;
        g_frequency = g_vfoB;
        g_isUSB = m_isUsbVfoB;
      }
    }

    setFrequency(g_frequency);
  }

  drawTx();
}

/*
  ritEnable is called with a frequency parameter that determines
  what the TX frequency will be
*/
void ritEnable (uint32_t f)
{
  g_ritOn = true;
  // save the non-RIT frequency back into the VFO memory
  // as RIT is a temporary shift, this is not saved to EEPROM
  g_ritTxFrequency = f;
}

/* this is called by the RIT menu routine */
void ritDisable ()
{
  if (g_ritOn)
  {
    g_ritOn = false;
    setFrequency(g_ritTxFrequency);
    // updateDisplay();  // <<<---
    displayVFO(g_vfoActive);
  }
}

/*
  basic user interface routines. these check the front panel for any activity
*/

/*
  The PTT is checked only if we are not already in a CW transmit session
  If the PTT is pressed, we shift to the RIT base if the RIT was on
  flip the T/R line to T and update the display to denote transmission
*/
void checkPTT ()
{
  // we don't check for PTT when transmitting CW
  if (g_cwTimeout > 0)
    return;

  if (digitalRead(PTT) == 0 && !g_inTx)
  {
    startTx(TX_SSB);

    activeDelay(50);  // debounce the PTT
  }

  if (digitalRead(PTT) == 1 && g_inTx)
    stopTx();
}

/* check if the encoder button was pressed */
void checkButton ()
{
  // only if the button is pressed
  if (!encoderButtonDown())
    return;

  activeDelay(50);

  if (!encoderButtonDown()) // debounce
    return;

  // disengage any CAT work (debug only)
  // doingCAT = false;

  uint16_t downTime = 0;

  // wait about five seconds before showing setup menu
  while (encoderButtonDown())
  {
    activeDelay(10);

    downTime++;

    if (downTime > 250) // was 300)
    {
      doSetupMenu();
      return;
    }
  }

  activeDelay(100);

  doCommands();

  // wait for the button to go up again
  while (encoderButtonDown())
    activeDelay(10);

  activeDelay(50);  // debounce
}


/* switch from one vfo to the other, making it active */
void switchVFO (uint8_t vfoSelect)
{
  if (vfoSelect == VFO_A)
  {
    if (g_vfoActive == VFO_B)
    {
      g_vfoB = g_frequency;
      m_isUsbVfoB = g_isUSB;

      EEPROM.put(VFO_B, g_frequency);

      if (m_isUsbVfoB)
        EEPROM.put(VFO_B_MODE, VFO_MODE_USB);
      else
        EEPROM.put(VFO_B_MODE, VFO_MODE_LSB);
    }

    g_vfoActive = VFO_A;

    g_frequency = g_vfoA;
    g_isUSB = m_isUsbVfoA;
  }
  else
  {
    if (g_vfoActive == VFO_A)
    {
      g_vfoA = g_frequency;
      m_isUsbVfoA = g_isUSB;

      EEPROM.put(VFO_A, g_frequency);

      if (m_isUsbVfoA)
        EEPROM.put(VFO_A_MODE, VFO_MODE_USB);
      else
        EEPROM.put(VFO_A_MODE, VFO_MODE_LSB);
    }

    g_vfoActive = VFO_B;

    g_frequency = g_vfoB;
    g_isUSB = m_isUsbVfoB;
  }

  setFrequency(g_frequency);
  redrawVFOs();
  saveVFOs();
}

/*
  The tuning jumps by 50 Hz on each step. No acceleration or momentum.
*/
void doTuning ()
{
  int16_t s;
  static uint32_t prevFrequency;
  static uint32_t nextFrequencyUpdate = 0;

  uint32_t now = millis();

  // update the vfo display if it's time
  if (now >= nextFrequencyUpdate && prevFrequency != g_frequency)
  {
    displayVFO(g_vfoActive);
    nextFrequencyUpdate = now + 250;  // was 500, changed to update frequency more frequently
    prevFrequency = g_frequency;
  }

  s = encoderRead();

  // encoder is at 0, nothing to see here, move along!
  if (!s)
    return;

  // doingCAT = false;  // go back to manual mode if you were doing CAT (debug only)
  prevFrequency = g_frequency;

  // add or subtract from frequency, depending on encoder value
  if (s > 0)
    g_frequency += 50l;
  else if (s < 0)
    g_frequency -= 50l;

  // set USB or LSB depending on the frequency
  if (prevFrequency < 10000000l && g_frequency >= 10000000l)
    g_isUSB = true;
  else if (prevFrequency >= 10000000l && g_frequency < 10000000l)
    g_isUSB = false;

  setFrequency(g_frequency);
}

/*
  RIT only steps back and forth by 100 Hz at a time
*/
void doRITTuning ()
{
  int16_t knob = encoderRead();
  uint32_t oldFreq = g_frequency;

  if (knob < 0)
    g_frequency -= 100l;
  else if (knob > 0)
    g_frequency += 100;

  if (oldFreq != g_frequency)
  {
    setFrequency(g_frequency);

    displayVFO(g_vfoActive);
  }
}

/*
  settings are read from EEPROM. The first time around, the values may not be
  present or out of range, in this case, some sane defaults are copied into the
  variables
*/
void initSettings ()
{
  uint8_t value;

  // read the settings from the eeprom and restore them
  // if the readings are off, then set defaults
  EEPROM.get(MASTER_CAL, g_calibration);
  EEPROM.get(USB_CAL, g_usbCarrier);
  EEPROM.get(VFO_A, g_vfoA);
  EEPROM.get(VFO_B, g_vfoB);
  EEPROM.get(CW_SIDETONE, g_sideTone);
  EEPROM.get(CW_SPEED, g_cwSpeed);
  EEPROM.get(CW_DELAYTIME, g_cwDelayTime);

  if (g_usbCarrier > 11060000l || g_usbCarrier < 11048000l)
    g_usbCarrier = 11052000l;
  if (g_vfoA > 35000000l || g_vfoA < 3500000l)  // set a VFO_A default of 7 MHz if out of range
    g_vfoA = 7000000l;
  if (g_vfoB > 35000000l || g_vfoB < 3500000l)  // set a VFO_B default of 14 MHz if out of range
    g_vfoB = 14000000l;
  if (g_sideTone < 100 || 2000 < g_sideTone)  // set sidetone default of 600 Hz if out of range
    g_sideTone = 600;
  if (g_cwSpeed < 10 || 1000 < g_cwSpeed)  // set CW dot speed default if out of range
    g_cwSpeed = 100;
  if (g_cwDelayTime < 10 || g_cwDelayTime > 100)  // set CW delay speed default if out of range
    g_cwDelayTime = 50;

  // the VFO modes are read in as either 2 (USB) or 3(LSB), 0, the default
  // is taken as 'uninitialized'
  EEPROM.get(VFO_A_MODE, value);

  switch (value)
  {
    case VFO_MODE_USB:
      m_isUsbVfoA = true;
      break;

    case VFO_MODE_LSB:
      m_isUsbVfoA = false;
      break;

    default:
      if (g_vfoA > 10000000l)
        m_isUsbVfoA = true;
      else
        m_isUsbVfoA = false;
  }

  EEPROM.get(VFO_B_MODE, value);

  switch (value)
  {
    case VFO_MODE_USB:
      m_isUsbVfoB = true;
      break;

    case VFO_MODE_LSB:
      m_isUsbVfoB = false;
      break;

    default:
      if (g_vfoA > 10000000l)
        m_isUsbVfoB = true;
      else
        m_isUsbVfoB = false;
  }

  // set the current mode
  g_isUSB = m_isUsbVfoA;

  // The keyer type splits into two variables
  EEPROM.get(CW_KEY_TYPE, value);

  if (value == 0)
    g_iambicKey = false;
  else if (value == 1)
  {
    g_iambicKey = true;
    g_keyerControl &= ~IAMBICB;
  }
  else if (value == 2)
  {
    g_iambicKey = true;
    g_keyerControl |= IAMBICB;
  }
}

/* set up Nano's pin modes */
void initPins ()
{
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
  standard setup routine - part of a typical Arduino sketch

  sets up serial output, inits display, settings, pins, oscillators and more
*/
void setup ()
{
  Serial.begin(38400);
  Serial.flush();

  displayInit();
  initSettings();
  initPins();
  initOscillators();

  g_frequency = g_vfoA;
  setFrequency(g_vfoA);

  encoderSetup();

  // do essential calibrations / setup when
  // encoder button is down during power-on
  // (almost looks like factory calibration trigger)
  if (encoderButtonDown())
  {
    doTouchCalibration();

    g_isUSB = true;
    setFrequency(10000000l);

    setupFreq();

    g_isUSB = false;
    setFrequency(7100000l);

    setupBFO();
  }

  // update screen, clearing both the screen and refreshing the vfos
  guiUpdate(true, true);
}

/*
  standard main loop - part of a typical Arduino sketch

  checks for keydown, PTT, function button and tuning
*/
void loop ()
{
  if (g_cwMode)
    cwKeyer();
  else if (!g_txCAT)
    checkPTT();

  checkButton();

  // tune only when not transmitting
  if (!g_inTx)
  {
    if (g_ritOn)
      doRITTuning();
    else
      doTuning();

    checkTouch();
  }

  checkCAT();
}
