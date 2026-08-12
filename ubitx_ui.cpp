/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
#include "ubitx.h"
#include "nano_gui.h"

/*
  The user interface of the ubitx consists of the encoder, the encoder's push-button
  and the TFT LCD display including the display's touch controller.
*/

/* buttons used for changing bands, selecting new frequencies, RIT, CW, etc */
struct Button {
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;

  const char * text;
};

/* file-level constants */

/* main screen buttons */
static constexpr uint8_t M_MAX_BUTTONS = 17;

static const struct Button buttons[M_MAX_BUTTONS] PROGMEM = {
  {0, 8, 159, 38, "A"},
  {160, 8, 159, 38, "B"},

  {0, 80, 60, 36, "RIT"},
  {64, 80, 60, 36, "USB"},
  {128, 80, 60, 36, "LSB"},
  {192, 80, 60, 36, "CW"},
  {256, 80, 60, 36, "SPL"},

  {0, 120, 60, 36, "80"},
  {64, 120, 60, 36, "40"},
  {128, 120, 60, 36, "30"},
  {192, 120, 60, 36, "20"},
  {256, 120, 60, 36, "17"},

  {0, 160, 60, 36, "15"},
  {64, 160, 60, 36, "10"},
  {128, 160, 60, 36, "SPD"},
  {192, 160, 60, 36, "TON"},
  {256, 160, 60, 36, "FRQ"}
};

/* manual frequency input number-pad buttons */
static constexpr uint8_t M_MAX_NUMPAD_KEYS = 15;

static const struct Button m_keypad[M_MAX_NUMPAD_KEYS] PROGMEM = {
  {0, 80, 60, 36, "1"},
  {64, 80, 60, 36, "2"},
  {128, 80, 60, 36, "3"},
  {192, 80, 60, 36, ""},
  {256, 80, 60, 36, "OK"},

  {0, 120, 60, 36, "4"},
  {64, 120, 60, 36, "5"},
  {128, 120, 60, 36, "6"},
  {192, 120, 60, 36, "0"},
  {256, 120, 60, 36, "<-"},

  {0, 160, 60, 36, "7"},
  {64, 160, 60, 36, "8"},
  {128, 160, 60, 36, "9"},
  {192, 160, 60, 36, ""},
  {256, 160, 60, 36, "Can"}
};

/* file-level variables */
static char m_vfoDisplay[12];

static bool m_inTone = false;
static bool m_inValByKnob = false;
static bool m_endValByKnob = false;

/* draw one button on the screen and set its attributes */
static void btnDraw (const Button * btn)
{
  // vfoA
  if (btn->text[0] == 'A' && btn->text[1] == '\0')  // this approach is faster than strcmp
  {
    memset(m_vfoDisplay, 0, sizeof(m_vfoDisplay));
    displayVFO(VFO_A);
  }
  // vfoB
  else if (btn->text[0] == 'B' && btn->text[1] == '\0')  // this approach is faster than strcmp
  {
    memset(m_vfoDisplay, 0, sizeof(m_vfoDisplay));
    displayVFO(VFO_B);
  }
  // and the rest... (Gilligan's Island reference omitted)
  else if ((strcmp(btn->text, "RIT") == 0 && g_ritOn)       ||
           (strcmp(btn->text, "USB") == 0 && g_isUSB)       ||
           (strcmp(btn->text, "LSB") == 0 && !g_isUSB)      ||
           (strcmp(btn->text, "SPL") == 0 && g_splitOn)     ||
           (strcmp(btn->text, "TON") == 0 && m_inTone)      ||
           (strcmp(btn->text, "SPD") == 0 && m_inValByKnob) ||
           (strcmp(btn->text, "CW")  == 0 && g_cwMode)
          )
    // display 'reverse' button, indicating an 'on' or 'enabled' condition
    drawTextWithRectFilled(btn->text, btn->x, btn->y, btn->w, btn->h, G_DISPLAY_BLACK, G_DISPLAY_ORANGE,
      G_DISPLAY_ORANGE, G_DISPLAY_ORANGE);
  else
    // display normal button
    drawTextWithRectFilled(btn->text, btn->x, btn->y, btn->w, btn->h, G_DISPLAY_DIMGOLD, G_DISPLAY_BLACK,
      G_DISPLAY_DARKGREY, G_DISPLAY_3DBOTTOM);
}

/*
  get button from text - 'searchText' is input, 'btn' is output
*/
static void getButton (const char * searchText, Button * btn)
{
  for (uint8_t i = 0; i < M_MAX_BUTTONS; i++)
  {
    memcpy_P(btn, buttons + i, sizeof(Button));  // copy button from PROGMEM

    if (strcmp(searchText, btn->text) == 0)
      return;
  }
}

/* formats the frequency given in f */
static void formatFreq (uint32_t f, char * buffOut)
{
  // thanks Jack Purdum W8TEE
  // replaced fsprint commmands with str commands for code size reduction

  memset(buffOut, 0, 15);  // we memset only to 15 for performance and because that's
  memset(g_buffB, 0, 15);  // more than enough for what we're using it for here

  ultoa(f, g_buffB, DEC);

  // one MHz digit if less than 10 MHz, two digits if more
  // (DO NOT use snprintf() here as its slower)
  if (f < 10000000l)
  {
    // below 10 MHz
    buffOut[0] = ' ';
    strncat(buffOut, g_buffB, 4);
    strcat(buffOut, ".");
    strncat(buffOut, &g_buffB[4], 2);
  }
  else
  {
    // above 10 MHz
    strncat(buffOut, g_buffB, 5);
    strcat(buffOut, ".");
    strncat(buffOut, &g_buffB[5], 2);
  }
}

/* clear command area (area below VFOs and above 'standard' buttons) */
static void clearCommandbar ()
{
  drawRectFilled(0, 48, 320, 30, G_DISPLAY_NEWBACK);
}

/* draws text in the command area (area below VFOs and above 'standard' buttons) */
static void drawCommandbar (const char * text)
{
  clearCommandbar();
  drawRawText(text, 30, 53, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK);
}

/*
  provides a reusable dialog box to get a value from the encoder, the prefix and postfix
  are useful to concatenate the values with text like "Set Freq to " x " KHz"
*/
static int16_t getValueByKnob (int16_t minimum, int16_t maximum, int16_t stepSize, int16_t initial,
    const char * prefix, const char * postfix, Button * btn = NULL)
{
  m_inValByKnob = true;

  int16_t knob = 0;
  int16_t knobValue;

  while (encoderButtonDown())
    activeDelay(100);

  activeDelay(200);

  knobValue = initial;

  strcpy(g_buffB, prefix);
  itoa(knobValue, g_buffC, 10);
  strcat(g_buffB, g_buffC);
  strcat(g_buffB, postfix);

  drawCommandbar(g_buffB);

  if (btn)
    btnDraw(btn);

  // encoder value change loop, exits with encoder button push
  while (!encoderButtonDown() && digitalRead(PTT) == HIGH && !m_endValByKnob)
  {
      knob = encoderRead();

      if (knob != 0)
      {
        if (knobValue > minimum && knob < 0)
          knobValue -= stepSize;
        if (knobValue < maximum && knob > 0)
          knobValue += stepSize;

        strcpy(g_buffB, prefix);
        itoa(knobValue, g_buffC, 10);
        strcat(g_buffB, g_buffC);
        strcat(g_buffB, postfix);
        drawCommandbar(g_buffB);
      }

      checkTouch();
      checkCAT();
  }

  clearCommandbar();

  m_inValByKnob = false;

  if (btn)
    btnDraw(btn);

  return knobValue;
}

/* display the frequency in the command area */
void printCarrierFreq (uint32_t freq)
{
  memset(g_buffB, 0, 15);  // we memset only to 15 for performance and because that's
  memset(g_buffC, 0, 15);  // more than enough for what we're using it for here

  ultoa(freq, g_buffB, DEC);

  strncat(g_buffC, g_buffB, 2);
  strcat(g_buffC, ".");
  strncat(g_buffC, &g_buffB[2], 3);
  strcat(g_buffC, ".");
  strncat(g_buffC, &g_buffB[5], 1);

  drawTextWithRectFilled(g_buffC, 80, 110, 160, 35, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_CYAN);
}

/* displays 'dialog' text for setup menus */
void displayDialog (const char * title, const char * instructions)
{
  displayClear(G_DISPLAY_NEWBACK);
  drawRectNoFill(10, 10, 300, 220, G_DISPLAY_LIGHTGREY);
  drawHLine(20, 45, 280, G_DISPLAY_LIGHTGREY);
  drawRawText(title, 20, 20, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);
  drawRawText(instructions, 20, 200, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);
}

/* display one vfo, depending on which is passed in */
void displayVFO (uint8_t vfo)
{
  int16_t x;
  int16_t y;
  uint16_t displayColor = 0;

  Button btn;

  // deal with vfo 'A'
  if (vfo == VFO_A)
  {
    getButton("A", &btn);

    if (g_splitOn)
    {
      if (g_vfoActive == VFO_A)
        strcpy(g_buffC, "R:");
      else
        strcpy(g_buffC, "T:");
    }
    else
      strcpy(g_buffC, "A:");

    if (g_vfoActive == VFO_A)
    {
      formatFreq(g_frequency, g_buffC + 2);
      displayColor = G_DISPLAY_WHITE;
    }
    else
    {
      formatFreq(g_vfoA, g_buffC + 2);
      displayColor = G_DISPLAY_DIMGOLD;
    }
  }

  // deal with vfo 'B'
  if (vfo == VFO_B)
  {
    getButton("B", &btn);

    if (g_splitOn)
    {
      if (g_vfoActive == VFO_B)
        strcpy(g_buffC, "R:");
      else
        strcpy(g_buffC, "T:");
    }
    else
      strcpy(g_buffC, "B:");

    if (g_vfoActive == VFO_B)
    {
      formatFreq(g_frequency, g_buffC + 2);
      displayColor = G_DISPLAY_WHITE;
    }
    else
    {
      displayColor = G_DISPLAY_DIMGOLD;
      formatFreq(g_vfoB, g_buffC + 2);
    }
  }

  // black out vfo button only if first char of m_vfoDisplay is "\0"
  if (m_vfoDisplay[0] == 0)
  {
    drawRectFilled(btn.x, btn.y, btn.w, btn.h, G_DISPLAY_BLACK);

    // display highlight rectangle around vfo button if it's active
    if (g_vfoActive == vfo)
      drawRectNoFill(btn.x, btn.y, btn.w, btn.h, G_DISPLAY_WHITE, G_DISPLAY_LIGHTGREY);
    else
      drawRectNoFill(btn.x, btn.y, btn.w, btn.h, G_DISPLAY_BLACK);
  }

  uint8_t cleanWidth = 16;
  uint8_t cleanHeight = 22;

  x = btn.x + 6;
  y = btn.y + 6;

  // draw vfo characters over blank background rects
  for (uint16_t i = 0; i <= strlen(g_buffC); i++)
  {
    char digit = g_buffC[i];

    if (digit != m_vfoDisplay[i])
    {
      // clean up artifacts from previous character(s)
      drawRectFilled(x, y, cleanWidth, cleanHeight, G_DISPLAY_BLACK);
      // checkCAT();

      // draw vfo character
      displayChar(x, y + G_TEXT_LINE_HEIGHT + 3, digit, displayColor, G_DISPLAY_BLACK);
      // checkCAT();  //  <<<--- preoccupation with checking cat!  disabled to speed up drawing
    }

    if (digit == ':')
      x += 7;
    else if (digit == '.')
      x += 11;
    else
      x += 16;
  }  // end of the while loop of the characters to be printed

  strcpy(m_vfoDisplay, g_buffC);
}

/* display both vfos */
static void displayVFOs ()
{
  memset(m_vfoDisplay, 0, sizeof(m_vfoDisplay));
  displayVFO(VFO_A);

  memset(m_vfoDisplay, 0, sizeof(m_vfoDisplay));
  displayVFO(VFO_B);
}

/* displays the RIT TX frequency with horizontal position depending on which VFO is active */
static void displayRIT ()
{
  if (g_ritOn)
  {
    memset(g_buffC, 0, sizeof(g_buffC));
    strcpy(g_buffC, "TX:");
    formatFreq(g_ritTxFrequency, g_buffC + 3);

    if (g_vfoActive == VFO_A)
      // show rit info on left side when vfoA is active
      drawTextWithRectFilled(g_buffC, 0, 48, 165, 30, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_NEWBACK);
    else
      // show rit info on right side when vfoB is active
      drawTextWithRectFilled(g_buffC, 153, 48, 165, 30, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_NEWBACK);
  }
  else
    clearCommandbar();
}

/* allows direct touchscreen input of desired frequency */
static void enterFreq ()
{
  // display number-pad buttons
  for (int8_t i = 0; i < M_MAX_NUMPAD_KEYS; i++)
  {
    Button btn1;
    memcpy_P(&btn1, m_keypad + i, sizeof(Button));
    btnDraw(&btn1);
  }

  int16_t cursor_pos = 0;

  memset(g_buffC, 0, sizeof(g_buffC));

  while (true)
  {
    checkCAT();  //  <<<--- the cat is okay, I PROMISE!!!

    if (!readTouch())
      continue;

    scaleTouch(&g_tsPoint);

    // loop through buttons and handle input
    for (uint8_t i = 0; i < M_MAX_NUMPAD_KEYS; i++)
    {
      Button btn2;
      memcpy_P(&btn2, m_keypad + i, sizeof(Button));

      int16_t x2 = btn2.x + btn2.w;
      int16_t y2 = btn2.y + btn2.h;

      if (btn2.x < g_tsPoint.x && g_tsPoint.x < x2 &&
          btn2.y < g_tsPoint.y && g_tsPoint.y < y2)
      {
        // accept entered frequency
        if (strcmp(btn2.text, "OK") == 0)
        {
          long frq = atol(g_buffC);

          // update the frequency only if entered frequency is valid
          if (frq < 60000 && frq > 125)  // (frq <= 30000 && frq > 100)  // wider range than stock
          {
            g_frequency = frq * 1000l;

            setFrequency(g_frequency);

            if (g_vfoActive == VFO_A)
              g_vfoA = g_frequency;
            else
              g_vfoB = g_frequency;

            saveVFOs();
          }

          // redraw screen, don't clear screen, do refresh vfos
          guiUpdate(false, true);

          return;
        }
        // delete last number
        else if (strcmp(btn2.text, "<-") == 0)
        {
          g_buffC[cursor_pos] = 0;

          if (cursor_pos > 0)
            cursor_pos--;

          g_buffC[cursor_pos] = 0;
        }
        // cancel
        else if (strcmp(btn2.text, "Can") == 0)
        {
          // redraw screen, don't clear screen, don't refresh vfos
          guiUpdate(false, false);

          return;  // get out
        }
        // valid number 0 through 9
        else if (btn2.text[0] >= '0' && btn2.text[0] <= '9')
        {
          g_buffC[cursor_pos++] = btn2.text[0];
          g_buffC[cursor_pos] = 0;
        }
      }
    }  // end of the button scanning loop

    // display frequency entered so far -- if any
    strcpy(g_buffB, g_buffC);
    strcat(g_buffB, " KHz");
    drawTextWithRectFilled(g_buffB, 0, 48, 320, 30, G_DISPLAY_WHITE, G_DISPLAY_NEWBACK, G_DISPLAY_NEWBACK);

    delay(300);

    while (readTouch())
      checkCAT();
  }  // end of event loop : while(true)
}

/* shows info at bottom of home screen */
void drawStatusbar ()
{
  // clear status bar area with background colour
  drawRectFilled(0, 201, 320, 40, G_DISPLAY_NEWBACK);

  // i don't like the following info at the bottom of my screen, but feel free to re-enable it
  // ==========================================
  // strcpy(g_buffB, " cw:");
  // int16_t wpm = 1200 / g_cwSpeed;
  // itoa(wpm, g_buffC, 10);
  // strcat(g_buffB, g_buffC);
  // strcat(g_buffB, "wpm, ");
  // itoa(g_sideTone, g_buffC, 10);
  // strcat(g_buffB, g_buffC);
  // strcat(g_buffB, "hz");

  // drawRawText(g_buffB, 0, 215, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);
  // ==========================================

  // display custom message string (set in ubitx.h)
  drawRawText(g_customMessage, 0, 215, G_DISPLAY_CYAN, G_DISPLAY_NEWBACK);
}

/* show TX indicator when transmitting */
void drawTx ()
{
  if (g_inTx)
    drawTextWithRectFilled("TX", 280, 48, 37, 28, G_DISPLAY_BLACK, G_DISPLAY_ORANGE, G_DISPLAY_BLUE);
  else
    clearCommandbar();
}

/* (re)draws home screen, optionally clearing the whole screen and/or refreshing vfos */
void guiUpdate (bool clearScreen, bool refreshVFOs)
{
  // use the current frequency as the VFO frequency for the active VFO
  if (clearScreen)
    displayClear(G_DISPLAY_NEWBACK);

  if (refreshVFOs)
    displayVFOs();

  // checkCAT();  //  <<<--- get out of here!
  displayRIT();

  checkCAT();

  // force the display to refresh everything

  // display all home screen buttons
  for (uint8_t i = 0; i < M_MAX_BUTTONS; i++)
  {
    Button btn;
    memcpy_P(&btn, buttons + i, sizeof(Button));
    btnDraw(&btn);

    // checkCAT();  // <<<--- really, GET OUT!!! :-P
  }

  checkCAT();

  if (clearScreen)
    drawStatusbar();

  //checkCAT();
}

/* toggles RIT mode */
void ritToggle (const Button * btn)
{
  // toggle rit status
  if (!g_ritOn)
    ritEnable(g_frequency);
  else
    ritDisable();

  // draw the rit button
  btnDraw(btn);

  // draw rit TX frequency, if rit enabled
  displayRIT();
}

/* toggles split mode */
void splitToggle (const Button * btnIn)
{
  if (g_splitOn)
    g_splitOn = false;
  else
    g_splitOn = true;

  // draw split button
  btnDraw(btnIn);

  // disable rit
  ritDisable();

  // draw disabled rit button
  Button btn;
  getButton("RIT", &btn);
  btnDraw(&btn);

  // this will clear rit text from command area
  // when rit is disabled
  displayRIT();

  // refresh vfos
  displayVFOs();
}

/* toggles CW mode */
void cwToggle (const Button * btn)
{
  if (!g_cwMode)
    g_cwMode = true;
  else
    g_cwMode = false;

  setFrequency(g_frequency);

  // redraw CW button with new status
  btnDraw(btn);
}

/* switch between the two sidebands (lower or upper) */
void sidebandToggle (const Button * btnIn)
{
  if (strcmp(btnIn->text, "LSB") == 0)
  {
    if (!g_isUSB)  //  <<<--- keep - saves drawing time
      return;

    g_isUSB = false;
  }
  else
  {
    if (g_isUSB)  //  <<<--- keep - saves drawing time
      return;

    g_isUSB = true;
  }

  Button btnTemp;

  // get usb button
  getButton("USB", &btnTemp);

  // redraw USB button with new status
  btnDraw(&btnTemp);

  // get lsb button
  getButton("LSB", &btnTemp);

  // redraw LSB button with new status
  btnDraw(&btnTemp);

  saveVFOs();
}

/* */
void redrawVFOs ()
{
  ritDisable();

  Button btnTemp;

  // get rit button
  getButton("RIT", &btnTemp);
  btnDraw(&btnTemp);

  displayRIT();

  displayVFOs();

  // draw the lsb/usb buttons, the sidebands might have changed
  getButton("LSB", &btnTemp);
  btnDraw(&btnTemp);

  getButton("USB", &btnTemp);
  btnDraw(&btnTemp);
}

/* switch to a new band */
void switchBand (uint32_t bandfreq)
{
  uint32_t offset;

  // * figure out the offset based on which band we're switching to *

  // 80 m  (3.5 – 4.0 MHz) - 80 meters doesn't start on a MHz boundary
  if (g_frequency >= 3500000UL && g_frequency <= 4000000UL)
    offset = g_frequency - 3500000UL;

  // 30 m  (10.100 – 10.150 MHz) - 30 meters doesn't start on a MHz boundary
  else if (g_frequency >= 10100000UL && g_frequency <= 10150000UL)
    offset = g_frequency - 10100000UL;

  // 17 m  (18.068 – 18.168 MHz) - 17 meters doesn't start on a MHz boundary
  else if (g_frequency >= 18068000UL && g_frequency <= 18168000UL)
    offset = g_frequency - 18068000UL;

  // all other bands – keep the kHz part inside the current MHz
  // all other (supported) bands start on a MHz boundary
  else
    offset = g_frequency % 1000000UL;

  setFrequency(bandfreq + offset);

  memset(m_vfoDisplay, 0, sizeof(m_vfoDisplay));  // set to clear whole vfo button

  displayVFO(g_vfoActive);

  saveVFOs();
}

/* set CW keyer speed */
void setCwSpeed ()
{
  uint16_t wpm;

  // no re-entrance
  if (!m_inValByKnob)
  {
    wpm = 1200 / g_cwSpeed;

    Button btn;
    getButton("SPD", &btn);

    wpm = getValueByKnob(1, 100, 1,  wpm, "CW: ", " WPM", &btn);
  }
  else
  {
    m_endValByKnob = true;
    return;
  }

  g_cwSpeed = 1200 / wpm;

  // store new value in eeprom
  EEPROM.put(CW_SPEED, g_cwSpeed);

  activeDelay(500);
}

/* set the side-tone frequency */
void setCwTone ()
{
  int16_t knob = 0;

  bool oneTime = false;

  Button btn;
  getButton("TON", &btn);

  if (m_inTone)
  {
    m_inTone = false;

    // draw TON button as OFF / standard
    btnDraw(&btn);

    checkCAT();
    activeDelay(20);

  }
  else
  {
    m_inTone = true;

    // draw TON button as ON
    btnDraw(&btn);

    // loop, checking for encoder, encoder button and m_inTone changes
    while (digitalRead(PTT) == HIGH && !encoderButtonDown() && m_inTone)
    {
      knob = encoderRead();

      if (knob > 0 && g_sideTone < 2000)
        g_sideTone += 10;
      else if (knob < 0 && g_sideTone > 100 )
        g_sideTone -= 10;
      else
      {
        checkTouch();

        if (oneTime)
          continue;  // don't update the frequency or the display
      }

      oneTime = true;

      tone(PIN_CW_TONE, g_sideTone);

      itoa(g_sideTone, g_buffC, 10);
      strcpy(g_buffB, "CW Tone: ");
      strcat(g_buffB, g_buffC);
      strcat(g_buffB, " Hz");
      drawCommandbar(g_buffB);

      checkCAT();
      activeDelay(20);
    }
  }

  noTone(PIN_CW_TONE);

  // store new value in eeprom
  EEPROM.put(CW_SIDETONE, g_sideTone);

  clearCommandbar();
}

/* do appropriate action based on the button passed in */
void doCommand (const Button * btn)
{
  if (strcmp(btn->text, "RIT") == 0)
    ritToggle(btn);
  else if (strcmp(btn->text, "LSB") == 0)
    sidebandToggle(btn);
  else if (strcmp(btn->text, "USB") == 0)
    sidebandToggle(btn);
  else if (strcmp(btn->text, "CW") == 0)
    cwToggle(btn);
  else if (strcmp(btn->text, "SPL") == 0)
    splitToggle(btn);
  //else if (strcmp(btn->text, "A") == 0)
  else if (btn->text[0] == 'A' && btn->text[1] == '\0')  // keep - faster than strcmp for single character
  {
    if (g_vfoActive == VFO_A)
      return;
    else
      switchVFO(VFO_A);
  }
  //else if (strcmp(btn->text, "B") == 0)
  else if (btn->text[0] == 'B' && btn->text[1] == '\0')  // keep - faster than strcmp for single character
  {
    if (g_vfoActive == VFO_B)
      return;
    else
      switchVFO(VFO_B);
  }
  else if (strcmp(btn->text, "80") == 0)
    switchBand(3500000l);
  else if (strcmp(btn->text, "40") == 0)
    switchBand(7000000l);
  else if (strcmp(btn->text, "30") == 0)
    switchBand(10100000l);
  else if (strcmp(btn->text, "20") == 0)
    switchBand(14000000l);
  else if (strcmp(btn->text, "17") == 0)
    switchBand(18068000l);
  else if (strcmp(btn->text, "15") == 0)
    switchBand(21000000l);
  else if (strcmp(btn->text, "10") == 0)
    switchBand(28000000l);
  else if (strcmp(btn->text, "FRQ") == 0)
    enterFreq();
  else if (strcmp(btn->text, "SPD") == 0)
    setCwSpeed();
  else if (strcmp(btn->text, "TON") == 0)
    setCwTone();
}

/*
  run the correct command based on which button on the screen was touched
*/
void checkTouch ()
{
  if (!readTouch())
    return;

  while (readTouch())
    checkCAT();

  scaleTouch(&g_tsPoint);

  // if a touch is on a button, run the correct action for it
  for (uint8_t i = 0; i < M_MAX_BUTTONS; i++)
  {
    Button btn;

    memcpy_P(&btn, buttons + i, sizeof(Button));

    int16_t x2 = btn.x + btn.w;
    int16_t y2 = btn.y + btn.h;

    // if touch was in button rect, run its command
    if (btn.x < g_tsPoint.x && g_tsPoint.x < x2 && btn.y < g_tsPoint.y && g_tsPoint.y < y2)
      doCommand(&btn);
  }
}

/* returns true if the encoder button is pressed */
bool encoderButtonDown ()
{
  if (digitalRead(FBUTTON) == HIGH)
    return false;
  else
    return true;
}

/* draw focus rectangle around button */
void drawFocus (uint8_t ibtn, uint16_t color)
{
  Button btn;

  memcpy_P(&btn, buttons + ibtn, sizeof(Button));

  drawRectNoFill(btn.x, btn.y, btn.w, btn.h, color);
}

/* user encoder button to click on-screen button on home screen */
void doCommands ()
{
  int16_t select = 0;
  int16_t i;
  uint8_t prevButton = 0;

  // wait for the button to be raised up
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);  // debounce

  g_menuOn = true;

  while (g_menuOn)
  {
    // check if the knob's button was pressed
    if (encoderButtonDown())
    {
      Button btn;
      memcpy_P(&btn, buttons + select / 10, sizeof(Button));

      doCommand(&btn);

      // unfocus the buttons
      drawFocus(select, G_DISPLAY_BLUE);

      if (g_vfoActive == VFO_A)
        drawFocus(0, G_DISPLAY_WHITE);
      else
        drawFocus(1, G_DISPLAY_WHITE);

      // wait for the button to be up and debounce
      while (encoderButtonDown())
        activeDelay(100);

      activeDelay(500);

      g_menuOn = false;

      return;
    }

    i = encoderRead();

    if (i == 0)
    {
      activeDelay(50);
      continue;
    }

    if (i > 0)
    {
      if (select + i < M_MAX_BUTTONS * 10)
        select += i;
    }

    if (i < 0 && select + i >= 0)
      select += i;

    if (prevButton == select / 10)
      continue;

    // we are on a new button
    drawFocus(prevButton, G_DISPLAY_BLUE);
    drawFocus(select / 10, G_DISPLAY_WHITE);
    prevButton = select / 10;
  }

  // debounce the button
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);

  g_menuOn = false;

  checkCAT();
}
