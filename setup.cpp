/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
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

/* global variables */

/* these are used by the si5351 routines in the ubitx_5351 file */
extern int32_t g_calibration;

/* file-level variables */
static int16_t m_prevPuck = -1;

/* frequency calibration */
void setupFreq ()
{
  int16_t knob = 0;

  displayDialog("Set Frequency", "Push TUNE to Save");

  // round off to the nearest khz
  g_frequency = (g_frequency / 1000l) * 1000l;
  setFrequency(g_frequency);

  drawRawText("You should have a", 20, 53, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);
  drawRawText("signal exactly at ", 20, 77, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);

  ltoa(g_frequency / 1000l, g_buffC, 10);
  strcat(g_buffC, " KHz");
  drawRawText(g_buffC, 20, 107, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);

  drawRawText("Rotate to zero-beat", 20, 176, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);

  // keep clear of any previous button press
  while (encoderButtonDown())
    activeDelay(100);

  activeDelay(100);

  g_calibration = 0;

  // loop until the encoder button is pushed
  while (!encoderButtonDown())
  {
    knob = encoderRead();

    if (knob != 0)
      g_calibration += knob * 875;
    else
      continue;  // don't update the frequency or the display

    si5351bxSetFreq(0, g_usbCarrier);  // set the carrier oscillator back, CW TX turns it off
    si5351SetCalibration(g_calibration);
    setFrequency(g_frequency);

    // display new calibration value
    ltoa(g_calibration, g_buffB, 10);

    drawTextWithRectFilled(g_buffB, 80, 135, 160, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);
  }

  // store new value in eeprom
  EEPROM.put(MASTER_CAL, g_calibration);

  // reset the oscillators
  initOscillators();

  si5351SetCalibration(g_calibration);

  setFrequency(g_frequency);

  // debounce
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(100);
}

/* set BFO adjustment */
void setupBFO ()
{
  int16_t knob = 0;

  displayDialog("Set BFO", "Press TUNE to Save");

  g_usbCarrier = 11053000l;
  si5351bxSetFreq(0, g_usbCarrier);
  printCarrierFreq(g_usbCarrier);

  // loop until the encoder button is pushed
  while (!encoderButtonDown())
  {
    knob = encoderRead();

    if (knob != 0)
      g_usbCarrier -= 50 * knob;
    else
      continue;  // don't update the frequency or the display

    si5351bxSetFreq(0, g_usbCarrier);
    setFrequency(g_frequency);

    // display new bfo value
    printCarrierFreq(g_usbCarrier);

    activeDelay(100);
  }

  // store new value in eeprom
  EEPROM.put(USB_CAL, g_usbCarrier);

  si5351bxSetFreq(0, g_usbCarrier);

  setFrequency(g_frequency);

  displayVFO(g_vfoActive);

  g_menuOn = false;
}

/* sets CW transmit / receive delay */
static void setupCwDelay ()
{
  int16_t knob = 0;

  displayDialog("Set CW T/R Delay", "Press tune to Save");

  activeDelay(500);

  itoa(10 * (int16_t)g_cwDelayTime, g_buffB, 10);
  strcat(g_buffB, " msec");
  drawTextWithRectFilled(g_buffB, 80, 110, 160, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);

  // loop until the encoder button is pushed
  while (!encoderButtonDown())
  {
    knob = encoderRead();

    if (knob < 0 && g_cwDelayTime > 10)
      g_cwDelayTime -= 10;
    else if (knob > 0 && g_cwDelayTime < 100)
      g_cwDelayTime += 10;
    else
      continue;  // don't update the frequency or the display

    itoa(10 * (int16_t)g_cwDelayTime, g_buffB, 10);
    strcat(g_buffB, " msec");
    drawTextWithRectFilled(g_buffB, 80, 110, 160, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);
  }

  // store new value in eeprom
  EEPROM.put(CW_DELAYTIME, g_cwDelayTime);

  activeDelay(500);

  g_menuOn = false;
}

/* set up keyer type */
static void setupKeyer ()
{
  int8_t keyTemp;
  int16_t knob;

  displayDialog("Set CW Keyer", "Press tune to Save");

  if (!g_iambicKey)
    drawTextWithRectFilled("< Hand Key >", 60, 110, 195, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);
  else if (g_keyerControl & IAMBICB)
    drawTextWithRectFilled("< Iambic A >", 60, 110, 195, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);
  else
    drawTextWithRectFilled("< Iambic B >", 60, 110, 195, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);

  if (!g_iambicKey)
    keyTemp = 0;  // hand key
  else if (g_keyerControl & IAMBICB)
    keyTemp = 2;  // iambic B
  else
    keyTemp = 1;

  // loop until the encoder button is pushed
  while (!encoderButtonDown())
  {
    knob = encoderRead();

    if (knob == 0)
    {
      activeDelay(50);
      continue;
    }

    if (knob < 0 && keyTemp > 0)
      keyTemp--;

    if (knob > 0)
      keyTemp++;

    if (keyTemp > 2)
      keyTemp = 0;

    if (keyTemp == 0)
      drawTextWithRectFilled("< Hand Key >", 60, 110, 195, 35,  G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);  //, 100, 100, 120, 26, G_DISPLAY_CYAN, G_DISPLAY_BLACK, G_DISPLAY_BLACK);
    else if (keyTemp == 1)
      drawTextWithRectFilled("< Iambic A >", 60, 110, 195, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);  //, 100, 100, 120, 26, G_DISPLAY_CYAN, G_DISPLAY_BLACK, G_DISPLAY_BLACK);
    else if (keyTemp == 2)
      drawTextWithRectFilled("< Iambic B >", 60, 110, 195, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);  //, 100, 100, 120, 26, G_DISPLAY_CYAN, G_DISPLAY_BLACK, G_DISPLAY_BLACK);
  }

  activeDelay(500);

  if (keyTemp == 0)
    g_iambicKey = false;
  else if (keyTemp == 1)
  {
    g_iambicKey = true;
    g_keyerControl &= ~IAMBICB;
  }
  else if (keyTemp == 2) {
    g_iambicKey = true;
    g_keyerControl |= IAMBICB;
  }

  // store new value in eeprom
  EEPROM.put(CW_KEY_TYPE, keyTemp);

  g_menuOn = false;
}

/* shows setup menu */
static void drawSetupMenu ()
{
  displayClear(G_DISPLAY_NEWBACK);

  drawTextWithRectFilled("Setup", 10, 10, 300, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_NEWBACK); // heading
  drawRectNoFill(10, 10, 300, 220, G_DISPLAY_LIGHTGREY);  // screen border

  drawRawText("Set Freq...", 30, 50, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
  drawRawText("Set BFO...", 30, 80, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
  drawRawText("CW Delay...", 30, 110, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
  drawRawText("CW Keyer...", 30, 140, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
  drawRawText("Touch Screen...", 30, 170, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
  drawRawText("Exit", 30, 200, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
}

/* moves selection indicator */
static void movePuck (int16_t i)
{
  if (m_prevPuck >= 0)
    drawRectNoFill(15, 45 + (m_prevPuck * 30), 290, 30, G_DISPLAY_BLACK);

  drawRectNoFill(15, 45 + (i * 30), 290, 30, G_DISPLAY_WHITE);

  m_prevPuck = i;
}

/* displays radio's setup menu */
void doSetupMenu ()
{
  int16_t select = 0;
  int16_t i;

  drawSetupMenu();
  movePuck(select);

  // wait for the button to be raised up
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);  // debounce

  g_menuOn = true;

  while (g_menuOn)
  {
    i = encoderRead();

    // ### historical code note ###
    //
    // *** The commented-out code directly below was the original code from Ashhar Farhan. I believe that it
    // *** was incorrect because the movePuck() call should have been within brackets -- part of the 'if'
    // *** statement. The corrected code matches the style and function of the next 'if' statement where the
    // *** movePuck() call *was* in brackets
    //
    //  if (i > 0){
    //    if (select + i < 60)  // <<<--- a start bracket should have followed this
    //      select += i;
    //      movePuck(select/10);  // <<<--- this line was indented in a confusing way
    //  }  // <<<--- an end bracket should have preceded this

    // if there's an encoder change, change selection puck position
    if (i > 0 && select + i < 60)
    {
      select += i;
      movePuck(select / 10);
    }
    else if (i < 0 && select - i >= 0)
    {
      select += i;
      movePuck(select / 10);
    }

    if (!encoderButtonDown())
    {
      activeDelay(50);
      continue;
    }

    // wait for the touch to lift off and debounce
    while (encoderButtonDown())
      activeDelay(50);

    activeDelay(300);

    // run desired setup based on selection value
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

  // update screen, both clearing the screen and refreshing the vfos
  guiUpdate(true, false);
}
