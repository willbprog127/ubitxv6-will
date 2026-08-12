/*
  This source file is under General Public License version 3.

  =============================================================
  ============ uBitX v6 firmware ==============================
  =============================================================

  * --- Note from Will Brokenbourgh regarding 2025-2026 changes ---
  *
  * - INTENT -
  * All code was originally written by Ashhar Farhan, KE7ER, KD8CEC and others before or around 2019. Code has been
  * reformatted and corrected for better readability, better functioning, fixed mistakes and changed many of the
  * integer types to smallest usable storage size to better fit in the Nano's memory. Original comments have also
  * been spell-corrected or changed for accuracy. Momentum functions were removed from encoder handling. A more
  * pleasant font and colour scheme is used (based on my tastes...feel free to hack in yours). Some adjustment
  * functions now can be exited by pressing the on-screen button again (sidetone, CW speed, etc) instead of having
  * to press the tuning knob. Allowable frequency range has been increased when entering a frequency manually.
  * Made many function arguments 'const', where appropriate.
  *
  * - CODE STYLE INFO -
  * + operators are surrounded by spaces
  * + brackets are on their own lines except for structs and multi-line array assignment
  * + strcmp tests are like this "if (strcmp(str1, str2) == 0)" instead of "if (!strcmp(str1, str2))"
  * + boolean tests are like this "if (!boolValue)" instead of "if (boolValue == false)", same with 'true'
  * + function definitions have a space before the signature list while function calls do not.
  * + file-level-only functions are marked as 'static'. File-level-only variables have 'm_' prefix
  * + global non-constantexpr variables have 'g_' prefix


  [ START OF ORIGINAL (edited) COMMENTS ]
  ----------------------------------------------------------------

  The uBitX v6 HF transceiver consists of a Raduino connected to an analog RF / AF board

  The Raduino is a small board that includes the Arduino Nano, a TFT display and
  an Si5351a frequency synthesizer. The Raduino is manufactured by HF Signals Electronics Pvt Ltd
  www.hfsignals.com

  To learn more about Arduino you may visit www.arduino.cc.

  Below are the libraries needed for building the Raduino

  The EEPROM library is used to store settings like the frequency memory, calibration data, etc.

  The main chip which generates up to three various frequencies in the Raduino is the Si5351a.
  To learn more about Si5351a you can download the datasheet from www.silabs.com although it
  is not a requirement to understand this code

  This uBitX sketch uses a built-in Si5351 library

  The Wire.h library is used to talk to the Si5351 and we also declare an instance of
  Si5351 object to control the clocks.

  Some information in these files may be left-overs from earlier versions of the uBitX line.  Please
  file an Issue on this project's github if you find anything inaccurate
*/

#ifndef _UBITX_H_
#define _UBITX_H_

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

/*
  - RADUINO PIN ASSIGNMENTS -
  On the top of the board, in line with the LCD connector is an 8-pin connector
  that is largely used with the encoder. It has a regulated 5v output and ground.

  This connector is marked 'CONTROLS' on the Raduino schematic.

  The pins are assigned as follows (left to right, display facing you):
     Pin 1 (Violet), A7, SPARE        - (no connection)
     Pin 2 (Blue),   A6, KEYER (DATA) - (no connection)
     Pin 3 (Green), +5v               - (no connection)
     Pin 4 (Yellow), GND              - to encoder
     Pin 5 (Orange), A3, PTT          - (no connection)
     Pin 6 (Red),    A2, F BUTTON     - to encoder
     Pin 7 (Brown),  A1, ENC B        - to encoder
     Pin 8 (Black),  A0, ENC A        - to encoder

  The pins at the bottom of the Raduino, which connect to the rest of the rig have
  three clock outputs and digital lines to control the rig.
  According to the Raduino schematic, the pin assignment is as follows:
  Pin   1     2     3     4     5     6     7     8     9     10    11    12    13    14    15    16    17    18
       T/R CW-TONE TX_A  TX_B TX_C CW-KEY  GND  CLK0   GND   GND   CLK0  GND   GND   CLK2  +12V  +12V  PTT  KEYER
*/

/*
   These are the Arduino Nano pin ID assignments
*/
constexpr uint8_t ENC_A = A0;          // Tuning encoder interface
constexpr uint8_t ENC_B = A1;          // Tuning encoder interface
constexpr uint8_t FBUTTON = A2;        // Tuning encoder interface
constexpr uint8_t PTT   = A3;          // Sense it for ssb and as a straight key for CW operation
constexpr uint8_t ANALOG_KEYER = A6;   // This is used as keyer. The analog port has 4.7K pull up resistor.
                                       // Details are in the circuit description on www.hfsignals.com

constexpr uint8_t TX_RX = 7;           // Pin from the Nano to the radio to switch to TX (HIGH; and RX(LOW)
constexpr uint8_t PIN_CW_TONE = 6;     // Generates a square wave sidetone while sending the CW.
constexpr uint8_t TX_LPF_A = 5;        // The 30 MHz LPF is permanently connected in the output of the PA...
constexpr uint8_t TX_LPF_B = 4;        //  ...Alternatively, either 3.5 MHz, 7 MHz or 14 Mhz LPFs are...
constexpr uint8_t TX_LPF_C = 3;        //  ...switched inline depending upon the TX frequency
constexpr uint8_t CW_KEY = 2;          //  Pin goes high during CW keydown to transmit the carrier.
                                       // The CW_KEY is needed in addition to the TX/RX key as the
                                       // key can be up within a TX period

/*
  These are the indices where these user changable settings are stored in the EEPROM
*/
constexpr uint8_t MASTER_CAL = 0;
constexpr uint8_t USB_CAL = 8;

/*
  these are ids of the vfos as well as their offset into the eeprom storage - DON'T change these values!
*/
constexpr uint8_t VFO_A = 16;
constexpr uint8_t VFO_B = 20;
constexpr uint8_t CW_SIDETONE = 24;
constexpr uint8_t CW_SPEED = 28;
constexpr uint8_t CW_DELAYTIME = 48;

/* the screen calibration parameters : int slopeX=104, slopeY=137, offsetX=28, offsetY=29; */
constexpr uint8_t SLOPE_X = 32;
constexpr uint8_t SLOPE_Y = 36;
constexpr uint8_t OFFSET_X = 40;
constexpr uint8_t OFFSET_Y = 44;

/*
  These are defines for the new features back-ported from KD8CEC's software
  these start from beyond 256 as Ian, KD8CEC has kept the first 256 bytes free for the base version
*/
constexpr uint16_t VFO_A_MODE = 256;  // 2: LSB, 3: USB
constexpr uint16_t VFO_B_MODE = 257;

/* values that are stored for the VFO modes */
constexpr uint8_t VFO_MODE_LSB = 2;
constexpr uint8_t VFO_MODE_USB = 3;

/* handkey, iambic a, iambic b : 0, 1, 2f */
constexpr uint16_t CW_KEY_TYPE = 358;
constexpr uint8_t IAMBICB = 0x10;  // 0 for Iambic A, 1 for Iambic B

/*
  The uBitX is an up-conversion transceiver. The first IF is at 45 MHz. The first IF frequency is not exactly at
  45 MHz but about 5 KHz lower, this shift is due to the loading on the 45 MHz crystal filter by the matching
  L-network used on either side.

  The first oscillator works between 48 MHz and 75 MHz. The signal is subtracted from the first oscillator to
  arrive at 45 MHz IF. Thus, it is inverted : LSB becomes USB and USB becomes LSB.

  The second IF of 11.059 MHz has a ladder crystal filter. If a second oscillator is used at 56 MHz (approx), the
  signal is subtracted FROM the oscillator, inverting a second time, and arrives at the 11.059 MHz ladder filter
  thus double inversion, keeps the sidebands as they originally were. If the second oscillator is at 33 MHz, the
  oscillator is subtracted from the signal, thus keeping the signal's sidebands inverted. The USB will become LSB.

  We use this technique to switch sidebands. This is to avoid placing the lsbCarrier close to 11 MHz where its
  fifth harmonic beats with the Arduino's 16 MHz oscillator's fourth harmonic
*/

/*
  we directly generate the CW by programming the Si5351 to the CW TX frequency, hence, both are different modes
  these are the parameter passed to startTx
*/
constexpr uint8_t TX_SSB = 0;
constexpr uint8_t TX_CW = 1;

/*
  The Arduino, unlike C/C++ on a regular computer with gigabytes of RAM, has *very little memory*.
  We have to be very careful with variables that are declared inside the functions as they are
  created in a memory region called the stack. The stack has just a few bytes of space on the Arduino
  if you declare large strings inside functions, they can easily exceed the capacity of the stack
  and corrupt your programs.
  *
  We circumvent this by declaring a few global buffers as 'kitchen counters' where we can
  slice and dice our strings. These strings are mostly used to control the display or handle
  the input and output from the USB port.
*/
extern char g_buffB[30];
extern char g_buffC[30];

/*
  here's a special string you can use that displays at the bottom of the home screen
  (try to keep it as small as possible)
*/
constexpr char g_customMessage[] = "AF7EC - Jesus rox!";

extern uint8_t g_vfoActive;

extern uint32_t g_vfoA;
extern uint32_t g_vfoB;
extern uint16_t g_sideTone;
extern uint32_t g_usbCarrier;

extern uint32_t g_frequency;
extern uint32_t g_ritTxFrequency;  // frequency is the current frequency on the dial
extern int32_t g_calibration;

extern bool g_ritOn;
extern bool g_cwMode;  // if cwMode is on, the RX frequency is tuned down by sidetone Hz instead of being zerobeat
extern bool g_iambicKey;

/*
  these are variables that control the keyer behaviour
*/
extern uint16_t g_cwSpeed;  // dot period in milliseconds
extern uint16_t g_cwDelayTime;
extern uint8_t g_keyerControl;

/*
  Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
*/
// extern bool g_doingCAT;           // CAT processing (debug only)
extern bool g_txCAT;              // turned on if the transmitting due to a CAT command
extern bool g_inTx;               // it is set to 1 if in transmit mode (whatever the reason : CW, PTT or CAT)
extern bool g_splitOn;            // working split, uses VFO B as the transmit frequency
extern bool g_isUSB;              // upper sideband was selected, this is reset to the default for the
                                  //  frequency when it crosses the frequency border of 10 MHz
extern bool g_menuOn;             // set to true when the menu is being displayed, if a menu item sets it to false, the menu is exited
extern uint32_t g_cwTimeout;      // milliseconds to go before the CW transmit line is released and the radio goes back to rx mode

/* forward declarations of functions implemented in the main file, ubitx_xxx.ino */
void activeDelay (uint16_t delay_by);
void saveVFOs ();
void setFrequency (uint32_t f);
void startTx (uint8_t txMode);
void stopTx ();
void ritEnable (uint32_t f);
void ritDisable ();
void checkCAT ();
void cwKeyer ();
void switchVFO (uint8_t vfoSelect);

/* forward declarations of functions in file ubitx_ui.cpp */
bool encoderButtonDown ();  // returns true if the encoder button is pressed  // was int
void displayVFO (uint8_t vfo);  // updates just the VFO frequency to show what is in 'frequency' variable  // was int vfo
void redrawVFOs ();    // redraws only the changed digits of the VFO
void guiUpdate (bool clearScreen = false, bool refreshVFOs = false);  // repaints the entire screen. Slow!!
void drawTx ();

/* forward declaration of functions in setup.cpp */
void doSetupMenu ();  // main setup function, displays the setup menu, calls various dialog boxes
void setupBFO ();
void setupFreq ();

/* displays a nice dialog box with a title and instructions as footnotes */
void displayDialog (const char * title, const char * instructions);
void printCarrierFreq (uint32_t freq);  // used to display the frequency in the command area

/* forward declarations of functions in encoder.cpp */
void encoderSetup ();
int16_t encoderRead ();

/* main functions to check if any button is pressed and other user interface events */
void doCommands ();  // does the commands with encoder to jump from button to button
void checkTouch ();  // does the commands with a touch on the buttons

/*
  We no longer use the standard SI5351 library because of its huge overhead due to many unused
  features consuming a lot of program space. Instead of depending on an external library we now use
  Jerry Gaffke's, KE7ER, lightweight standalone minimalist "si5351bx" routines. Here are some declarations
  used by Jerry's routines:
*/

/* forward declarations of functions in ubitx_si5351.cpp */
void si5351bxSetFreq (uint8_t clknum, uint32_t fout);
void initOscillators ();
void si5351SetCalibration (int32_t cal);  // calibration is a small value that is nudged to make up for
                                          //   the inaccuracies of the reference 25 MHz crystal frequency
#endif // _UBITX_H_
