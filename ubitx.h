/*
    This source file is under General Public License version 3.

    This uBitX sketch uses a built-in Si5351 library

    Most source code is meant to be understood by the compilers and the computers.
    Code that has to be hackable needs to be well understood and properly documented.
    Donald Knuth coined the term 'Literate Programming' to indicate code that is written be
    easily read and understood.

    The Raduino is a small board that includes the Arduino Nano, a TFT display and
    an Si5351a frequency synthesizer. The Raduino is manufactured by HF Signals Electronics Pvt Ltd

    To learn more about Arduino you may visit www.arduino.cc.

    Below are the libraries to be included for building the Raduino

    The EEPROM library is used to store settings like the frequency memory, calibration data, etc.

    The main chip which generates up to three oscillators of various frequencies in the
    Raduino is the Si5351a. To learn more about Si5351a you can download the datasheet
    from www.silabs.com although, strictly speaking it is not a requirement to understand this code.

    The Wire.h library is used to talk to the Si5351 and we also declare an instance of
    Si5351 object to control the clocks.

    Some information in these files may be left-overs from earlier versions of the uBitX line.  Please
    file an Issue on this project's github to let me know
*/

#ifndef _UBITX_H_
#define _UBITX_H_

#include <stdint.h>
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>


/*
  We need to carefully pick assignment of pin for various purposes.
  There are two sets of completely programmable pins on the Raduino.

  First, on the top of the board, in line with the LCD connector is an 8-pin connector
  that is largely meant for analog inputs and front-panel control. It has a regulated 5v output,
  ground and six pins. Each of these six pins can be individually programmed
  either as an analog input, a digital input or a digital output.

  This connector is marked 'CONTROLS' in the schematic.

  The pins are assigned as follows (left to right, display facing you):
     Pin 1 (Violet), A7, SPARE
     Pin 2 (Blue),   A6, KEYER (DATA)
     Pin 3 (Green), +5v
     Pin 4 (Yellow), Gnd
     Pin 5 (Orange), A3, PTT
     Pin 6 (Red),    A2, F BUTTON
     Pin 7 (Brown),  A1, ENC B
     Pin 8 (Black),  A0, ENC A
  Note: A5, A4 are wired to the Si5351 as I2C interface

  Though, this can be assigned anyway, for this application of the Arduino, we will make the following
  assignment:
  A2 will connect to the PTT line, which is the usually a part of the mic connector
  A3 is connected to a push button that can momentarily ground this line. This will be used for RIT/Bandswitching, etc.
  A6 is to implement a keyer, it is reserved and not yet implemented
  A7 is connected to a center pin of good quality 100K or 10K linear potentiometer with the two other ends connected to
  ground and +5v lines available on the connector. This implements the tuning mechanism  // <<<--- ???

  The second set of 16 pins on the Raduino's bottom connector are have the three clock outputs and the digital lines to control the rig.
  This assignment is as follows :
    Pin   1   2    3    4    5    6    7    8    9    10   11   12   13   14   15   16
         GND +5V CLK0  GND  GND  CLK1 GND  GND  CLK2  GND  D2   D3   D4   D5   D6   D7
  These too are flexible with what you may do with them, for the Raduino, we use them to :
  - TX_RX line : Switches between Transmit and Receive after sensing the PTT or the morse keyer
  - CW_KEY line : turns on the carrier for CW
*/

/*
   The ubitx is powered by an Arduino Nano. The pin assignment is as follows
*/

#define ENC_A (A0)          // Tuning encoder interface
#define ENC_B (A1)          // Tuning encoder interface
#define FBUTTON (A2)        // Tuning encoder interface
#define PTT   (A3)          // Sense it for ssb and as a straight key for cw operation
#define ANALOG_KEYER (A6)   // This is used as keyer. The analog port has 4.7K pull up resistor.
                            // Details are in the circuit description on www.hfsignals.com

#define TX_RX (7)           // Pin from the Nano to the radio to switch to TX (HIGH) and RX(LOW)
#define PIN_CW_TONE (6)         // Generates a square wave sidetone while sending the CW.
#define TX_LPF_A (5)        // The 30 MHz LPF is permanently connected in the output of the PA...
#define TX_LPF_B (4)        //  ...Alternatively, either 3.5 MHz, 7 MHz or 14 Mhz LPFs are...
#define TX_LPF_C (3)        //  ...switched inline depending upon the TX frequency
#define CW_KEY (2)          //  Pin goes high during CW keydown to transmit the carrier.
                            // The CW_KEY is needed in addition to the TX/RX key as the
                            // key can be up within a tx period

/*
   These are the indices where these user changable settings are stored in the EEPROM
*/
#define MASTER_CAL 0

#define USB_CAL 8

/*
    these are ids of the vfos as well as their offset into the eeprom storage - DON'T change these values!
*/
#define VFO_A 16
#define VFO_B 20
#define CW_SIDETONE 24
#define CW_SPEED 28
#define CW_DELAYTIME 48

/* the screen calibration parameters : int slopeX=104, slopeY=137, offsetX=28, offsetY=29; */
#define SLOPE_X 32
#define SLOPE_Y 36
#define OFFSET_X 40
#define OFFSET_Y 44

/*
    These are defines for the new features back-ported from KD8CEC's software
    these start from beyond 256 as Ian, KD8CEC has kept the first 256 bytes free for the base version
*/
#define VFO_A_MODE  256 // 2: LSB, 3: USB
#define VFO_B_MODE  257

/* values that are stored for the VFO modes */
#define VFO_MODE_LSB 2
#define VFO_MODE_USB 3

/* handkey, iambic a, iambic b : 0, 1, 2f */
#define CW_KEY_TYPE 358
#define IAMBICB 0x10 // 0 for Iambic A, 1 for Iambic B

/*
    The uBITX is an upconversion transceiver. The first IF is at 45 MHz.
    The first IF frequency is not exactly at 45 Mhz but about 5 khz lower,
    this shift is due to the loading on the 45 Mhz crystal filter by the matching
    L-network used on it's either sides.

    The first oscillator works between 48 Mhz and 75 MHz. The signal is subtracted
    from the first oscillator to arriive at 45 Mhz IF. Thus, it is inverted : LSB becomes USB
    and USB becomes LSB.

    The second IF of 11.059 Mhz has a ladder crystal filter. If a second oscillator is used at
    56 Mhz (appox), the signal is subtracted FROM the oscillator, inverting a second time, and arrives
    at the 11.059 Mhz ladder filter thus doouble inversion, keeps the sidebands as they originally were.
    If the second oscillator is at 33 Mhz, the oscilaltor is subtracated from the signal,
    thus keeping the signal's sidebands inverted. The USB will become LSB.

    We use this technique to switch sidebands. This is to avoid placing the lsbCarrier close to
    11 MHz where its fifth harmonic beats with the arduino's 16 Mhz oscillator's fourth harmonic
*/

/*
  we directly generate the CW by programming the Si5351 to the cw tx frequency, hence, both are different modes
  these are the parameter passed to startTx
*/
#define TX_SSB 0
#define TX_CW  1

/*
    The Arduino, unlike C/C++ on a regular computer with gigabytes of RAM, has very little memory.
    We have to be very careful with variables that are declared inside the functions as they are
    created in a memory region called the stack. The stack has just a few bytes of space on the Arduino
    if you declare large strings inside functions, they can easily exceed the capacity of the stack
    and corrupt your programs.
    *
    We circumvent this by declaring a few global buffers as 'kitchen counters' where we can
    slice and dice our strings. These strings are mostly used to control the display or handle
    the input and output from the USB port.
*/
extern char gbuffC[30];
extern char gbuffB[30];

/*
   here's a special string you can use that displays at the bottom of the home screen
   (try to keep it as small as possible)
*/
const char customString[] = "AF7EC - Jesus rox!";  //  <<<--- was 19

extern uint8_t vfoActive;

extern uint32_t vfoA;
extern uint32_t vfoB;
extern uint32_t sideTone;
extern uint32_t usbCarrier;

extern uint32_t frequency;
extern uint32_t ritTxFrequency;  // frequency is the current frequency on the dial
extern int32_t calibration;

extern bool ritOn;
extern bool cwMode;  // if cwMode is on, the rx frequency is tuned down by sidetone hz instead of being zerobeat
extern bool iambicKey;

/*
    these are variables that control the keyer behaviour
*/
extern uint16_t cwSpeed;  // dot period in milliseconds
extern uint16_t cwDelayTime;
extern uint8_t keyerControl;

/*
   Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
*/
extern bool txCAT;              // turned on if the transmitting due to a CAT command
extern bool inTx;               // it is set to 1 if in transmit mode (whatever the reason : cw, ptt or cat)
extern bool splitOn;            // working split, uses VFO B as the transmit frequency
extern bool isUSB;              // upper sideband was selected, this is reset to the default for the
                                //  frequency when it crosses the frequency border of 10 MHz
extern bool menuOn;             // set to true when the menu is being displayed, if a menu item sets it to false, the menu is exited
extern uint32_t cwTimeout;      // milliseconds to go before the cw transmit line is released and the radio goes back to rx mode

/* forward declarations of functions implemented in the main file, ubitx_xxx.ino */
void activeDelay(uint16_t delay_by);
void saveVFOs();
void setFrequency(uint32_t f);
void startTx(uint8_t txMode);
void stopTx();
void ritEnable(uint32_t f);
void ritDisable();
void checkCAT();
void cwKeyer(void);
void switchVFO(uint8_t vfoSelect);  // was int vfoSelect

/* forward declarations of functions in file ubitx_ui.cpp */
bool encoderButtonDown(); // returns true if the encoder button is pressed  // was int
void displayVFO(uint8_t vfo); // updates just the VFO frequency to show what is in 'frequency' variable  // was int vfo
void displayVFOs();   // updates both vfos
void redrawVFOs();    // redraws only the changed digits of the vfo
void guiUpdate(bool clearScreen = false, bool refreshVFOs = false);     // repaints the entire screen. Slow!!
void drawCommandbar(char * text);
void drawTx();
/*
    getValueByKnob() provides a reusable dialog box to get a value from the encoder, the prefix and postfix
    are useful to concatanate the values with text like "Set Freq to " x " KHz"
*/
int getValueByKnob(int16_t minimum, int16_t maximum, int16_t stepSize, int16_t initial, const char * prefix, const char * postfix);

/* forward declaration of functions in setup.cpp */
void doSetupMenu(); // main setup function, displays the setup menu, calls various dialog boxes
void setupBFO();
void setupFreq();

/* displays a nice dialog box with a title and instructions as footnotes */
void displayDialog(const char * title, const char * instructions);
void printCarrierFreq(uint32_t freq); // used to display the frequency in the command area

/* forward declarations of functions in encoder.cpp */
void encoderSetup();
int16_t encoderRead();

/* main functions to check if any button is pressed and other user interface events */
void doCommands();  // does the commands with encoder to jump from button to button
void checkTouch(); // does the commands with a touch on the buttons

/*
  The main chip which generates up to three oscillators of various frequencies in the
  Raduino is the Si5351a. To learn more about Si5351a you can download the datasheet
  from www.silabs.com although, strictly speaking it is not a requirement to understand this code.

  We no longer use the standard SI5351 library because of its huge overhead due to many unused
  features consuming a lot of program space. Instead of depending on an external library we now use
  Jerry Gaffke's, KE7ER, lightweight standalone mimimalist "si5351bx" routines (see further down the
  code). Here are some declarations used by Jerry's routines:
*/

/* forward declarations of functions in ubitx_si5351.cpp */
void si5351bxSetFreq(uint8_t clknum, uint32_t fout);
void initOscillators();
void si5351SetCalibration(int32_t cal); // calibration is a small value that is nudged to make up for
                                        //   the inaccuracies of the reference 25 MHz crystal frequency

#endif // _UBITX_H_
