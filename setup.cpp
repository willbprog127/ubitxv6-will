/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
// #include "morse.h"
#include "ubitx.h"
#include "nano_gui.h"

/*
  Menus
  - - -
    The setup menu is accessed by pressing and holding the encoder button
    - the main loop() constantly looks for a button press and calls doMenu() when it detects
        a function button press
    - as the encoder is rotated, at every 10th pulse, the next or the previous menu
        item is displayed. Each menu item is controlled by it's own function
    - each menu function may be called to display itself
    - each of these menu routines is called with a button parameter
    - the btn flag denotes if the menu itme was clicked on or not
    - If the menu item is clicked on, then it is selected
    - If the menu item is NOT clicked on, then the menu's prompt is to be displayed
*/

/* these are used by the si5351 routines in the ubitx_5351 file */
extern int32_t calibration;
extern uint32_t si5351bxVCOA;

static int16_t prevPuck = -1;


/* frequency calibration */
void setupFreq() {

  int16_t knob = 0;

  displayDialog("Set Frequency", "Push TUNE to Save");

  // round off to the nearest khz
  frequency = (frequency / 1000l) * 1000l;
  setFrequency(frequency);

  displayRawText("You should have a", 20, 50, DISPLAY_CYAN, DISPLAY_WILLBACK);
  displayRawText("signal exactly at ", 20, 75, DISPLAY_CYAN, DISPLAY_WILLBACK);

  ltoa(frequency / 1000l, gbuffC, 10);
  strcat(gbuffC, " KHz");
  displayRawText(gbuffC, 20, 100, DISPLAY_CYAN, DISPLAY_WILLBACK);

  displayRawText("Rotate to zerobeat", 20, 180, DISPLAY_CYAN, DISPLAY_WILLBACK);

  // keep clear of any previous button press
  while (encoderButtonDown())
    activeDelay(100);

  activeDelay(100);

  calibration = 0;

  // loop until the encoder button is pushed
  while (!encoderButtonDown())
  {
    knob = encoderRead();

    if (knob != 0)
      calibration += knob * 875;
    else
      continue; // don't update the frequency or the display

    si5351bxSetFreq(0, usbCarrier);  // set the carrier oscillator back, cw tx turns it off
    si5351SetCalibration(calibration);
    setFrequency(frequency);

    // display new calibration value
    ltoa(calibration, gbuffB, 10);
    displayText(gbuffB, 100, 140, 100, 26, DISPLAY_CYAN, DISPLAY_WILLBACK, DISPLAY_WHITE);
  }

  // store new value in eeprom
  EEPROM.put(MASTER_CAL, calibration);

  // reset the oscillators
  initOscillators();

  si5351SetCalibration(calibration);

  setFrequency(frequency);

  // debounce
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(100);
}


/* set BFO adjustment */
void setupBFO() {

  int16_t knob = 0;

  displayDialog("Set BFO", "Press TUNE to Save");

  usbCarrier = 11053000l;
  si5351bxSetFreq(0, usbCarrier);
  printCarrierFreq(usbCarrier);

  // loop until the encoder button is pushed
  while (!encoderButtonDown()) {

    knob = encoderRead();

    if (knob != 0)
      usbCarrier -= 50 * knob;
    else
      continue; // don't update the frequency or the display

    si5351bxSetFreq(0, usbCarrier);
    setFrequency(frequency);

    // display new bfo value
    printCarrierFreq(usbCarrier);

    activeDelay(100);
  }

  // store new value in eeprom
  EEPROM.put(USB_CAL, usbCarrier);

  si5351bxSetFreq(0, usbCarrier);

  setFrequency(frequency);

  // updateDisplay(); // <<<---
  displayVFO(vfoActive);

  menuOn = false;
}


/* sets CW transmit / receive delay */
void setupCwDelay() {

  int16_t knob = 0;

  displayDialog("Set CW T/R Delay", "Press tune to Save");

  activeDelay(500);

  itoa(10 * (int16_t)cwDelayTime, gbuffB, 10);
  strcat(gbuffB, " msec");
  displayText(gbuffB, 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);

  // loop until the encoder button is pushed
  while (!encoderButtonDown()) {
    knob = encoderRead();

    if (knob < 0 && cwDelayTime > 10)
      cwDelayTime -= 10;
    else if (knob > 0 && cwDelayTime < 100)
      cwDelayTime += 10;
    else
      continue; // don't update the frequency or the display

    itoa(10 * (int16_t)cwDelayTime, gbuffB, 10);
    strcat(gbuffB, " msec");
    displayText(gbuffB, 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);
  }

  // store new value in eeprom
  EEPROM.put(CW_DELAYTIME, cwDelayTime);

  activeDelay(500);

  menuOn = false;
}


/* set up keyer type */
void setupKeyer() {

  int16_t tmp_key;
  int16_t knob;

  displayDialog("Set CW Keyer", "Press tune to Save");

  if (!iambicKey)
    displayText("< Hand Key >", 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);
  else if (keyerControl & IAMBICB)
    displayText("< Iambic A >", 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);
  else
    displayText("< Iambic B >", 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);

  if (!iambicKey)
    tmp_key = 0;  // hand key
  else if (keyerControl & IAMBICB)
    tmp_key = 2;  // iambic B
  else
    tmp_key = 1;

  // loop until the encoder button is pushed
  while (!encoderButtonDown()) {
    knob = encoderRead();

    if (knob == 0) {
      activeDelay(50);
      continue;
    }

    if (knob < 0 && tmp_key > 0)
      tmp_key--;

    if (knob > 0)
      tmp_key++;

    if (tmp_key > 2)
      tmp_key = 0;

    if (tmp_key == 0)
      displayText("< Hand Key >", 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);
    else if (tmp_key == 1)
      displayText("< Iambic A >", 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);
    else if (tmp_key == 2)
      displayText("< Iambic B >", 100, 100, 120, 26, DISPLAY_CYAN, DISPLAY_BLACK, DISPLAY_BLACK);
  }

  activeDelay(500);

  if (tmp_key == 0)
    iambicKey = false;
  else if (tmp_key == 1) {
    iambicKey = true;
    keyerControl &= ~IAMBICB;
  }
  else if (tmp_key == 2) {
    iambicKey = true;
    keyerControl |= IAMBICB;
  }

  // store new value in eeprom
  EEPROM.put(CW_KEY_TYPE, tmp_key);

  menuOn = false;
}


/* shows setup menu */
void drawSetupMenu() {

  displayClear(DISPLAY_BLACK);

  displayText("Setup", 10, 10, 300, 35, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WHITE);
  displayRect(10, 10, 300, 220, DISPLAY_WHITE);

  displayRawText("Set Freq...", 30, 50, DISPLAY_WHITE, DISPLAY_BLACK);
  displayRawText("Set BFO...", 30, 80, DISPLAY_WHITE, DISPLAY_BLACK);
  displayRawText("CW Delay...", 30, 110, DISPLAY_WHITE, DISPLAY_BLACK);
  displayRawText("CW Keyer...", 30, 140, DISPLAY_WHITE, DISPLAY_BLACK);
  displayRawText("Touch Screen...", 30, 170, DISPLAY_WHITE, DISPLAY_BLACK);
  displayRawText("Exit", 30, 200, DISPLAY_WHITE, DISPLAY_BLACK);
}


/* moves selection indicator */
void movePuck(int16_t i) {

  if (prevPuck >= 0)
    displayRect(15, 45 + (prevPuck * 30), 290, 30, DISPLAY_BLACK); // y was 49  // height was 25

  displayRect(15, 45 + (i * 30), 290, 30, DISPLAY_WHITE);  // y was 49  // height was 25

  prevPuck = i;
}


/* displays radio's setup menu */
void doSetupMenu() {

  int16_t select = 0;
  int16_t i;

  drawSetupMenu();
  movePuck(select);

  // wait for the button to be raised up
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);  // debounce

  menuOn = true; //2;

  while (menuOn) {

    i = encoderRead();

    if (i > 0) {
      if (select + i < 60)
        select += i;

      movePuck(select / 10);  // #### <<<--- indented as part of 'if' above originally, separated due to no brackets ####
    }

    if (i < 0 && select - i >= 0) {
      select += i;      // caught ya, i is already -ve here, so you add it
      movePuck(select / 10);
    }

    if (!encoderButtonDown()) {
      activeDelay(50);
      continue;
    }

    // wait for the touch to lift off and debounce
    while (encoderButtonDown())
      activeDelay(50);

    activeDelay(300);

    if (select < 10)
      setupFreq();
    else if (select < 20 )
      setupBFO();
    else if (select < 30 )
      setupCwDelay();
    else if (select < 40)
      setupKeyer();
    else if (select < 50)
      doTouchCalibration();
    else
      break;  // exit setup was chosen

    drawSetupMenu();
  }

  // debounce the button
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);

  checkCAT();

  guiUpdate(true, true);
}
