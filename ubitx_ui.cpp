/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
// #include "morse.h"
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
  // const char * morse;  // Removed because Morse button function disabled from factory
};

const uint8_t maxButtons = 17;

const struct Button buttons[maxButtons] PROGMEM = {
  // Morse letters removed because Morse button function disabled from factory
  {0, 8, 159, 38,  "A"},  // , "A"},  //  <<<--- changed y from 10, height from 36
  {160, 8, 159, 38, "B"},  // , "B"},  //  <<<--- changed y from 10, height from 36

  {0, 80, 60, 36,  "RIT"},  // , "R"},
  {64, 80, 60, 36, "USB"},  // , "U"},
  {128, 80, 60, 36, "LSB"},  // , "L"},
  {192, 80, 60, 36, "CW"},  // , "M"},
  {256, 80, 60, 36, "SPL"},  // , "S"},

  {0, 120, 60, 36, "80"},  // , "8"},
  {64, 120, 60, 36, "40"},  // , "4"},
  {128, 120, 60, 36, "30"},  // , "3"},
  {192, 120, 60, 36, "20"},  // , "2"},
  {256, 120, 60, 36, "17"},  // , "7"},

  {0, 160, 60, 36, "15"},  // , "5"},
  {64, 160, 60, 36, "10"},  // , "1"},
  {128, 160, 60, 36, "WPM"},  // , "W"},
  {192, 160, 60, 36, "TON"},  // , "T"},
  {256, 160, 60, 36, "FRQ"}  // , "F"},
};


const uint8_t maxKeys = 15;

const struct Button keypad[maxKeys] PROGMEM = {
  {0, 80, 60, 36,  "1"},  // , "1"},
  {64, 80, 60, 36, "2"},  // , "2"},
  {128, 80, 60, 36, "3"},  // , "3"},
  {192, 80, 60, 36,  ""},  // , ""},
  {256, 80, 60, 36,  "OK"},  // , "K"},

  {0, 120, 60, 36,  "4"},  // , "4"},
  {64, 120, 60, 36,  "5"},  // , "5"},
  {128, 120, 60, 36,  "6"},  // , "6"},
  {192, 120, 60, 36,  "0"},  // , "0"},
  {256, 120, 60, 36,  "<-"},  // , "B"},

  {0, 160, 60, 36,  "7"},  // , "7"},
  {64, 160, 60, 36, "8"},  // , "8"},
  {128, 160, 60, 36, "9"},  // , "9"},
  {192, 160, 60, 36,  ""},  // , ""},
  {256, 160, 60, 36,  "Can"}  // , "C"},
};

char vfoDisplay[12];

bool inTone = false;
bool inValByKnob = false;
bool endValByKnob = false;


/* draw one button on the screen and set its attributes */
void btnDraw(Button * btn) {

  // vfoA
  if (!strcmp(btn->text, "A")) {
    memset(vfoDisplay, 0, sizeof(vfoDisplay));
    displayVFO(VFO_A);
  }
  // vfoB
  else if (!strcmp(btn->text, "B")) {
    memset(vfoDisplay, 0, sizeof(vfoDisplay));
    displayVFO(VFO_B);
  }
  // and the rest... (Gilligan's Island reference omitted)
  else if ((!strcmp(btn->text, "RIT") && ritOn == true)       ||
           (!strcmp(btn->text, "USB") && isUSB == true)       ||
           (!strcmp(btn->text, "LSB") && isUSB == false)      ||
           (!strcmp(btn->text, "SPL") && splitOn == true)     ||
           (!strcmp(btn->text, "TON") && inTone == true)      ||
           (!strcmp(btn->text, "WPM") && inValByKnob == true) ||
           (!strcmp(btn->text, "CW")  && cwMode == true) )
    // display 'reverse' button, indicating an 'on' or 'enabled' condition
    displayText(btn->text, btn->x, btn->y, btn->w, btn->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_ORANGE, DISPLAY_ORANGE);
  else
    // display normal button
    displayText(btn->text, btn->x, btn->y, btn->w, btn->h, DISPLAY_DIMGOLD, DISPLAY_BLACK, DISPLAY_DARKGREY, DISPLAY_3DBOTTOM);  // DISPLAY_GREEN // <<<---
}


/*
  get button from text - 'searchText' is input, 'btn' is output
*/
void getButton(const char * searchText, Button * btn) {  // <<<--- changed to const char

  for (uint8_t i = 0; i < maxButtons; i++) {

    memcpy_P(btn, buttons + i, sizeof(Button));  // copy button from PROGMEM

    if (!strcmp(searchText, btn->text))
      return;
  }
}


/* formats the frequency given in f */
void formatFreq(uint32_t f, char * buffOut) {

  // thanks Jack Purdum W8TEE
  // replaced fsprint commmands by str commands for code size reduction

  memset(buffOut, 0, 11);  // <<<--- was buff, 0, 10
  memset(gbuffB, 0, sizeof(gbuffB));

  ultoa(f, gbuffB, DEC);

  // one MHz digit if less than 10 MHz, two digits if more
  if (f < 10000000l) {
    buffOut[0] = ' ';
    strncat(buffOut, gbuffB, 4);
    strcat(buffOut, ".");
    strncat(buffOut, &gbuffB[4], 2);
  } else {
    strncat(buffOut, gbuffB, 5);
    strcat(buffOut, ".");
    strncat(buffOut, &gbuffB[5], 2);
  }
}


/* clear command area (area below VFOs and above 'standard' buttons) */
void clearCommandbar() {
    displayFillrect(0, 48, 320, 30, DISPLAY_WILLBACK);
}


/* draws text in the command area (area below VFOs and above 'standard' buttons) */
void drawCommandbar(char * text) {
  clearCommandbar();
  displayRawText(text, 30, 53, DISPLAY_WHITE, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


/* A generic control to read variable values */
int16_t getValueByKnob(int16_t minimum, int16_t maximum, int16_t stepSize,  int16_t initial,
    const char * prefix, const char * postfix, Button * btn = NULL) {  // <<<--- added const to pre and post

  inValByKnob = true;

  int16_t knob = 0;
  int16_t knobValue;

  while (encoderButtonDown())
    activeDelay(100);

  activeDelay(200);

  knobValue = initial;

  strcpy(gbuffB, prefix);
  itoa(knobValue, gbuffC, 10);
  strcat(gbuffB, gbuffC);
  strcat(gbuffB, postfix);
  drawCommandbar(gbuffB);

  if (btn != NULL)
    btnDraw(btn);

  while (!encoderButtonDown() && digitalRead(PTT) == HIGH && endValByKnob == false) {

      knob = encoderRead();

      if (knob != 0) {
        if (knobValue > minimum && knob < 0)
          knobValue -= stepSize;
        if (knobValue < maximum && knob > 0)
          knobValue += stepSize;

        strcpy(gbuffB, prefix);
        itoa(knobValue, gbuffC, 10);
        strcat(gbuffB, gbuffC);
        strcat(gbuffB, postfix);
        drawCommandbar(gbuffB);
      }

      checkTouch();
      checkCAT();
  }

  clearCommandbar();

  inValByKnob = false;

  if (btn != NULL)
    btnDraw(btn);

  return knobValue;
}


/* display the frequency in the command area */
void printCarrierFreq(uint32_t freq) {

  memset(gbuffB, 0, sizeof(gbuffB));
  memset(gbuffC, 0, sizeof(gbuffC));

  ultoa(freq, gbuffB, DEC);

  strncat(gbuffC, gbuffB, 2);
  strcat(gbuffC, ".");
  strncat(gbuffC, &gbuffB[2], 3);
  strcat(gbuffC, ".");
  strncat(gbuffC, &gbuffB[5], 1);

  displayText(gbuffC, 110, 100, 100, 30, DISPLAY_WILLBACK, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_CYAN, DISPLAY_NAVY, DISPLAY_NAVY);
}


/* displays 'dialog' text for setup menus */
void displayDialog(const char * title, const char * instructions) {
  displayClear(DISPLAY_BLACK);
  displayRect(10, 10, 300, 220, DISPLAY_WHITE);
  displayHline(20, 45, 280, DISPLAY_WHITE);
  displayRect(12, 12, 296, 216, DISPLAY_WHITE);
  displayRawText(title, 20, 20, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  displayRawText(instructions, 20, 200, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


/* display one vfo, depending on which is passed in */
void displayVFO(uint8_t vfo) {  // was int vfo

  int16_t x;
  int16_t y;
  uint16_t displayColor = 0;   //   <<<--- Was not initialized originally

  Button btn;

  if (vfo == VFO_A) {
    getButton("A", &btn);

    if (splitOn) {
      if (vfoActive == VFO_A)
        strcpy(gbuffC, "R:");
      else
        strcpy(gbuffC, "T:");

    } else
      strcpy(gbuffC, "A:");

    if (vfoActive == VFO_A) {
      formatFreq(frequency, gbuffC + 2);
      displayColor = DISPLAY_WHITE;
    } else {
      formatFreq(vfoA, gbuffC + 2);
      displayColor = DISPLAY_DIMGOLD; // DISPLAY_GREEN;  // <<<---
    }
  }

  if (vfo == VFO_B) {
    getButton("B", &btn);

    if (splitOn) {
      if (vfoActive == VFO_B)
        strcpy(gbuffC, "R:");
      else
        strcpy(gbuffC, "T:");
    } else
      strcpy(gbuffC, "B:");

    if (vfoActive == VFO_B) {
      formatFreq(frequency, gbuffC + 2);
      displayColor = DISPLAY_WHITE;
    } else {
      displayColor = DISPLAY_DIMGOLD;   // DISPLAY_GREEN; // <<<---
      formatFreq(vfoB, gbuffC + 2);
    }
  }

  // black out vfo button only if first char of vfoDisplay is "\0"
  if (vfoDisplay[0] == 0) {

    displayFillrect(btn.x, btn.y, btn.w, btn.h, DISPLAY_BLACK);  //  <<<---

    // display highlight rectangle around vfo button if it's active
    if (vfoActive == vfo)
      displayRect(btn.x, btn.y, btn.w , btn.h, DISPLAY_WHITE, DISPLAY_3DBOTTOM);
    else
      displayRect(btn.x, btn.y, btn.w , btn.h, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  }

  uint8_t cleanWidth = 16;
  uint8_t cleanHeight = 22;

  x = btn.x + 6;
  y = btn.y + 6;  // <<<--- was 3

  char * text = gbuffC;

  for (uint16_t i = 0; i <= strlen(gbuffC); i++) {  // was int
    char digit = gbuffC[i];

    if (digit != vfoDisplay[i]) {

      // clean up artifacts from previous character(s)
      displayFillrect(x, y, cleanWidth, cleanHeight, DISPLAY_BLACK);   // <<<--- was x, y, 15, btn.h - 6
      // checkCAT();

      displayChar(x, y + textLineHeight + 3, digit, displayColor, DISPLAY_BLACK);

      // checkCAT();  //  <<<--- preoccupation with checking cat!  disabled to speed up drawing
    }

    // if (digit == ':' || digit == '.')  // <<<--- decimal is a bit off
    if (digit == ':')
      x += 7;
    else if (digit == '.')
      x += 11;
    else
      x += 16;

    text++;
  } // end of the while loop of the characters to be printed

  strcpy(vfoDisplay, gbuffC);
}


/* display both vfos */
void displayVFOs() {

  memset(vfoDisplay, 0, sizeof(vfoDisplay));
  displayVFO(VFO_A);

  memset(vfoDisplay, 0, sizeof(vfoDisplay));
  displayVFO(VFO_B);
}


/* displays the RIT TX frequency with horizontal position depending on which VFO is active */
void displayRIT() {

  if (ritOn) {
    memset(gbuffC, 0, sizeof(gbuffC));
    strcpy(gbuffC, "TX:");
    formatFreq(ritTxFrequency, gbuffC + 3);

    if (vfoActive == VFO_A)
      // show rit info on left side when vfoA is active
      displayText(gbuffC, 0, 48, 165, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);  //  <<<--- was gbuffC, 0, 45, 159, 30
    else
      // show rit info on right side when vfoB is active
      displayText(gbuffC, 153, 48, 165, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);
  }
  else
    clearCommandbar();
}


/* fastTune() is disabled because I don't like it :-P */
///* tunes fast */
//void fastTune() {
  //int16_t encoder;

  //// if the btn is down, wait until it is up
  //while (encoderButtonDown())
    //activeDelay(50);

  //activeDelay(300);

  //displayRawText("Fast tune", 100, 55, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);

  //while (true) {
    //checkCAT();

    //// exit after debouncing the encoderButtonDown
    //if (encoderButtonDown()) {
      //displayFillrect(100, 55, 120, 30, DISPLAY_WILLBACK); //DISPLAY_NAVY);

      //// wait until the button is realsed and then return
      //while (encoderButtonDown())
        //activeDelay(50);

      //activeDelay(300);

      //return;
    //}

    //encoder = encoderRead();

    //if (encoder != 0) {

      //if (encoder > 0 && frequency < 30000000l)
        //frequency += 50000l;
      //else if (encoder < 0 && frequency > 600000l)
        //frequency -= 50000l;

      //setFrequency(frequency);

      //displayVFO(vfoActive);
    //}
  //}  // end of the event loop
//}


/* use the keypad to manually enter a frequency */
void enterFreq() {

  // display number-pad buttons
  for (int8_t i = 0; i < maxKeys; i++) {
    Button btn1;
    memcpy_P(&btn1, keypad + i, sizeof(Button));
    btnDraw(&btn1);
  }

  int16_t cursor_pos = 0;

  memset(gbuffC, 0, sizeof(gbuffC));

  while (true) {

    checkCAT();

    if (!readTouch())
      continue;

    scaleTouch(&tsPoint);

    for (uint8_t i = 0; i < maxKeys; i++) {

      Button btn2;
      memcpy_P(&btn2, keypad + i, sizeof(Button));

      int16_t x2 = btn2.x + btn2.w;
      int16_t y2 = btn2.y + btn2.h;

      if (btn2.x < tsPoint.x && tsPoint.x < x2 &&
          btn2.y < tsPoint.y && tsPoint.y < y2) {

        // we're done
        if (!strcmp(btn2.text, "OK")) {

          long frq = atol(gbuffC);

          // update the frequency if entered value is valid
          if (30000 >= frq && frq > 100) {

            frequency = frq * 1000l;

            setFrequency(frequency);

            if (vfoActive == VFO_A)
              vfoA = frequency;
            else
              vfoB = frequency;

            saveVFOs();
          }

          // redraw screen, only refresh vfos
          guiUpdate(false, true);

          return;  // get out
        }
        // delete last number
        else if (!strcmp(btn2.text, "<-")) {
          gbuffC[cursor_pos] = 0;

          if (cursor_pos > 0)
            cursor_pos--;

          gbuffC[cursor_pos] = 0;
        }
        // cancel
        else if (!strcmp(btn2.text, "Can")) {
          // redraw screen, only refresh vfos
          guiUpdate(false, false);

          return;  // get out
        }
        // valid number 0 through 9
        else if ('0' <= btn2.text[0] && btn2.text[0] <= '9') {
          gbuffC[cursor_pos++] = btn2.text[0];
          gbuffC[cursor_pos] = 0;
        }
      }
    } // end of the button scanning loop

    // display frequency entered so far -- if any
    strcpy(gbuffB, gbuffC);
    strcat(gbuffB, " KHz");
    displayText(gbuffB, 0, 48, 320, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);

    delay(300);

    while (readTouch())
      checkCAT();
  } // end of event loop : while(true)

  // we should never reach this spot!!
}


/* shows info at bottom of home screen */
void drawStatusbar() {

  displayFillrect(0, 201, 320, 40, DISPLAY_WILLBACK); //DISPLAY_NAVY);  // <<<--- 0, 201, 320, 39,...

  // i don't care for the following info at the bottom of my screen, but feel free to re-enable it

  //strcpy(gbuffB, " cw:");
  //int16_t wpm = 1200 / cwSpeed;
  //itoa(wpm, gbuffC, 10);
  //strcat(gbuffB, gbuffC);
  //strcat(gbuffB, "wpm, ");
  //itoa(sideTone, gbuffC, 10);
  //strcat(gbuffB, gbuffC);
  //strcat(gbuffB, "hz");

  //displayRawText(gbuffB, 0, 210, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  displayRawText(customString, 0, 215, DISPLAY_CYAN, DISPLAY_WILLBACK);
}


/* show TX indicator when transmitting */
void drawTx() {
  if (inTx)
    displayText("TX", 280, 48, 37, 28, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_BLUE);
  else
    displayFillrect(280, 48, 37, 28, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


/* (re)draws home screen, optionally clearing the whole screen and/or refreshing vfos */
void guiUpdate(bool clearScreen, bool refreshVFOs) {

  // use the current frequency as the VFO frequency for the active VFO
  if (clearScreen == true)
    displayClear(DISPLAY_WILLBACK); //DISPLAY_NAVY);

  if (refreshVFOs == true)
    displayVFOs();

  // checkCAT();  //  <<<--- get out of here!
  displayRIT();

  checkCAT();

  // force the display to refresh everything
  // display all the buttons
  for (uint8_t i = 0; i < maxButtons; i++) {
    Button btn;

    memcpy_P(&btn, buttons + i, sizeof(Button));
    btnDraw(&btn);

    // checkCAT();  // <<<--- really, GET OUT!!! :-P
  }

  checkCAT();  // <<<---
  drawStatusbar();

  //checkCAT();
}


/* toggles RIT mode */
void ritToggle(Button * btn) {

  // toggle rit status
  if (ritOn == false)
    ritEnable(frequency);
  else
    ritDisable();

  // draw the rit button
  btnDraw(btn);

  // draw rit TX frequency, if rit enabled
  displayRIT();
}


/* toggles split mode */
void splitToggle(Button * btn1) {

  if (splitOn)
    splitOn = false;
  else
    splitOn = true;

  // draw split button
  btnDraw(btn1);

  // disable rit
  ritDisable();

  Button btn2;
  getButton("RIT", &btn2);

  // draw disabled rit button
  btnDraw(&btn2);

  // this will clear rit text from command area
  // when rit is disabled
  displayRIT();

  // refresh vfos
  displayVFOs();
}


/* toggles CW mode */
void cwToggle(Button * btn) {

  if (cwMode == false)
    cwMode = true;
  else
    cwMode = false;

  setFrequency(frequency);

  // redraw CW button with new status
  btnDraw(btn);
}


/* switch between the two sidebands (lower or upper) */
void sidebandToggle(Button * btn1) {

  if (!strcmp(btn1->text, "LSB")) {
    if (isUSB == false)  //  <<<--- keep - seems extraneous, but saves drawing time overall
      return;

    isUSB = false;
  } else {
    if (isUSB == true)  //  <<<--- keep - seems extraneous, but saves drawing time overall
      return;

    isUSB = true;
  }

  Button btn2;

  getButton("USB", &btn2);

  // redraw USB button with new status
  btnDraw(&btn2);

  getButton("LSB", &btn2);

  // redraw LSB button with new status
  btnDraw(&btn2);

  saveVFOs();
}


/* */
void redrawVFOs() {

  ritDisable();

  Button btn;
  getButton("RIT", &btn);
  btnDraw(&btn);

  displayRIT();

  displayVFOs();

  // draw the lsb/usb buttons, the sidebands might have changed
  getButton("LSB", &btn);
  btnDraw(&btn);

  getButton("USB", &btn);
  btnDraw(&btn);
}


/* switch to a new band */
void switchBand(uint32_t bandfreq) {

  uint32_t offset;

  if (3500000l <= frequency && frequency <= 4000000l)
    offset = frequency - 3500000l;
  else if (24800000l <= frequency && frequency <= 25000000l)
    offset = frequency - 24800000l;
  else
    offset = frequency % 1000000l;

  setFrequency(bandfreq + offset);

  memset(vfoDisplay, 0, sizeof(vfoDisplay));  // set to clear whole vfo button

  displayVFO(vfoActive);

  saveVFOs();
}


/* set CW keyer speed */
void setCwSpeed() {  // <<<--- Was int, changed to void

  uint16_t wpm;  // <<<--- was int

  Button btn;
  getButton("WPM", &btn);

  if (inValByKnob == false) {

    wpm = 1200 / cwSpeed;

    wpm = getValueByKnob(1, 100, 1,  wpm, "CW: ", " WPM", &btn);
  } else {
    endValByKnob = true;
    return;
  }

  cwSpeed = 1200 / wpm;

  // store new value in eeprom
  EEPROM.put(CW_SPEED, cwSpeed);

  activeDelay(500);
}


/* set the side-tone frequency */
void setCwTone() {

    int16_t knob = 0;

    bool oneTime = false;

    Button btn;
    getButton("TON", &btn);

    if (inTone == true) {
        inTone = false;

        // draw TON button as OFF / normal
        btnDraw(&btn);

        checkCAT();
        activeDelay(20);

    } else {
        inTone = true;

        // draw TON button as ON
        btnDraw(&btn);

        // loop, checking for encoder, encoder button and inTone changes
        while (digitalRead(PTT) == HIGH && !encoderButtonDown() && inTone) {

            knob = encoderRead();

            if (knob > 0 && sideTone < 2000)
                sideTone += 10;
            else if (knob < 0 && sideTone > 100 )
                sideTone -= 10;
            else {
                checkTouch();
                if (oneTime == true)
                    continue; // don't update the frequency or the display
            }

            oneTime = true;

            tone(PIN_CW_TONE, sideTone);

            itoa(sideTone, gbuffC, 10);
            strcpy(gbuffB, "CW Tone: ");
            strcat(gbuffB, gbuffC);
            strcat(gbuffB, " Hz");
            drawCommandbar(gbuffB);

            checkCAT();
            activeDelay(20);
        }
    }

    noTone(PIN_CW_TONE);

    // store new value in eeprom
    EEPROM.put(CW_SIDETONE, sideTone);

    clearCommandbar();
}


/* do appropriate action based on the button passed in */
void doCommand(Button * btn) {

  if (!strcmp(btn->text, "RIT"))
    ritToggle(btn);
  else if (!strcmp(btn->text, "LSB"))
    sidebandToggle(btn);
  else if (!strcmp(btn->text, "USB"))
    sidebandToggle(btn);
  else if (!strcmp(btn->text, "CW"))
    cwToggle(btn);
  else if (!strcmp(btn->text, "SPL"))
    splitToggle(btn);
  else if (!strcmp(btn->text, "A")) {
    if (vfoActive != VFO_A)    //  if (vfoActive == VFO_A)
      switchVFO(VFO_A);
  }
  else if (!strcmp(btn->text, "B")) {
    if (vfoActive != VFO_B)    //  if (vfoActive == VFO_B)
      switchVFO(VFO_B);
  }
  else if (!strcmp(btn->text, "80"))
    switchBand(3500000l);
  else if (!strcmp(btn->text, "40"))
    switchBand(7000000l);
  else if (!strcmp(btn->text, "30"))
    switchBand(10100000l);  //  <<<--- was 10000000l
  else if (!strcmp(btn->text, "20"))
    switchBand(14000000l);
  else if (!strcmp(btn->text, "17"))
    switchBand(18000000l);
  else if (!strcmp(btn->text, "15"))
    switchBand(21000000l);
  else if (!strcmp(btn->text, "10"))
    switchBand(28000000l);
  else if (!strcmp(btn->text, "FRQ"))
    enterFreq();
  else if (!strcmp(btn->text, "WPM"))
    setCwSpeed();
  else if (!strcmp(btn->text, "TON"))
    setCwTone();
}


/*
  run the correct command based on which button on the screen was touched
*/
void checkTouch() {

  if (!readTouch())
    return;

  while (readTouch())
    checkCAT();

  scaleTouch(&tsPoint);

  // if a touch is on a button, run the correct action for it
  for (uint8_t i = 0; i < maxButtons; i++) {

    Button btn;
    memcpy_P(&btn, buttons + i, sizeof(Button));

    int16_t x2 = btn.x + btn.w;
    int16_t y2 = btn.y + btn.h;

    if (btn.x < tsPoint.x && tsPoint.x < x2 &&
        btn.y < tsPoint.y && tsPoint.y < y2)
      doCommand(&btn);
  }
}


/* returns true if the encoder button is pressed */
bool encoderButtonDown() {  // was int

  if (digitalRead(FBUTTON) == HIGH)
    return false;  // 0;
  else
    return true;  // 1;
}


/* draw focus rectangle around button */
void drawFocus(uint8_t ibtn, uint16_t color) {  // was int ibtn

  Button btn;

  memcpy_P(&btn, buttons + ibtn, sizeof(Button));

  displayRect(btn.x, btn.y, btn.w, btn.h, color);
}


/* click button on home screen with encoder button */
void doCommands() {

  int16_t select = 0;
  int16_t i;
  uint8_t prevButton = 0;  //  <<<--- Not initialized originally  // was int

  // wait for the button to be raised up
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);  // debounce

  menuOn = true;  //  <<<--- was 2(?) previously;

  while (menuOn) {

    // check if the knob's button was pressed
    if (encoderButtonDown()) {

      Button btn;
      memcpy_P(&btn, buttons + select / 10, sizeof(Button));

      doCommand(&btn);

      // unfocus the buttons
      drawFocus(select, DISPLAY_BLUE);

      if (vfoActive == VFO_A)
        drawFocus(0, DISPLAY_WHITE);
      else
        drawFocus(1, DISPLAY_WHITE);

      // wait for the button to be up and debounce
      while (encoderButtonDown())
        activeDelay(100);

      activeDelay(500);

      menuOn = false;

      return;
    }

    i = encoderRead();

    if (i == 0) {
      activeDelay(50);
      continue;
    }

    if (i > 0) {
      if (select + i < maxButtons * 10)
        select += i;
    }

    if (i < 0 && select + i >= 0)
      select += i;      // caught ya, i is already -ve here, so you add it

    if (prevButton == select / 10)
      continue;

    // we are on a new button
    drawFocus(prevButton, DISPLAY_BLUE);
    drawFocus(select / 10, DISPLAY_WHITE);
    prevButton = select / 10;
  }

  // debounce the button
  while (encoderButtonDown())
    activeDelay(50);

  activeDelay(50);

  menuOn = false;

  checkCAT();
}
