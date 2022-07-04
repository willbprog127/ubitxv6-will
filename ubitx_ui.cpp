/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
#include "morse.h"
#include "ubitx.h"
#include "nano_gui.h"

/*
   The user interface of the ubitx consists of the encoder, the push-button within it
   and the TFT LCD display.
*/

/* buttons used for changing bands, selecting new frequencies, etc */
struct Button {
  int x;
  int y;
  int w;
  int h;

  const char * text;
  const char * morse;
};


#define MAX_BUTTONS 17

const struct Button btn_set[MAX_BUTTONS] PROGMEM = {
  {0, 8, 159, 38,  "VFOA", "A"},  //  <<<--- changed y from 10, height from 36
  {160, 8, 159, 38, "VFOB", "B"},  //  <<<--- changed y from 10, height from 36

  {0, 80, 60, 36,  "RIT", "R"},
  {64, 80, 60, 36, "USB", "U"},
  {128, 80, 60, 36, "LSB", "L"},
  {192, 80, 60, 36, "CW", "M"},
  {256, 80, 60, 36, "SPL", "S"},

  {0, 120, 60, 36, "80", "8"},
  {64, 120, 60, 36, "40", "4"},
  {128, 120, 60, 36, "30", "3"},
  {192, 120, 60, 36, "20", "2"},
  {256, 120, 60, 36, "17", "7"},

  {0, 160, 60, 36, "15", "5"},
  {64, 160, 60, 36, "10", "1"},
  {128, 160, 60, 36, "WPM", "W"},
  {192, 160, 60, 36, "TON", "T"},
  {256, 160, 60, 36, "FRQ", "F"},
};


#define MAX_KEYS 15  // <<<--- changed from 17 to 15 because there's only 15 items

const struct Button keypad[MAX_KEYS] PROGMEM = {
  {0, 80, 60, 36,  "1", "1"},
  {64, 80, 60, 36, "2", "2"},
  {128, 80, 60, 36, "3", "3"},
  {192, 80, 60, 36,  "", ""},
  {256, 80, 60, 36,  "OK", "K"},

  {0, 120, 60, 36,  "4", "4"},
  {64, 120, 60, 36,  "5", "5"},
  {128, 120, 60, 36,  "6", "6"},
  {192, 120, 60, 36,  "0", "0"},
  {256, 120, 60, 36,  "<-", "B"},

  {0, 160, 60, 36,  "7", "7"},
  {64, 160, 60, 36, "8", "8"},
  {128, 160, 60, 36, "9", "9"},
  {192, 160, 60, 36,  "", ""},
  {256, 160, 60, 36,  "Can", "C"},
};

char vfoDisplay[12];
int enc_prev_state = 3;


/*
  get button from text - text _in_,  btn _out_
*/
void getButton(const char * text, struct Button * btn) {  // <<<--- changed to const char
  for (int i = 0; i < MAX_BUTTONS; i++) {

    memcpy_P(btn, btn_set + i, sizeof(struct Button));

    if (!strcmp(text, btn->text))
      return;
  }
}


/* This formats the frequency given in f */
void formatFreq(long f, char * buff) {

  // Serial.println("formatFreq");

  // tks Jack Purdum W8TEE
  // replaced fsprint commmands by str commands for code size reduction

  memset(buff, 0, 10);
  memset(gbuffB, 0, sizeof(gbuffB));

  ultoa(f, gbuffB, DEC);

  // one mhz digit if less than 10 M, two digits if more
  if (f < 10000000l) {
    buff[0] = ' ';
    strncat(buff, gbuffB, 4);
    strcat(buff, ".");
    strncat(buff, &gbuffB[4], 2);
  } else {
    strncat(buff, gbuffB, 5);
    strcat(buff, ".");
    strncat(buff, &gbuffB[5], 2);
  }
}


/* clear command text area (below VFOs and above 'standard' buttons) */
void clearCommandbar() {
    displayFillrect(0, 48, 320, 30, DISPLAY_WILLBACK);
}

/* */
void drawCommandbar(char * text) {

  // Serial.println("drawCommandbar");

  //displayFillrect(30, 45, 280, 32, DISPLAY_WILLBACK); //DISPLAY_NAVY);  // <<<--- clearCommandbar now?
  clearCommandbar();
  displayRawText(text, 30, 45, DISPLAY_WHITE, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


/* A generic control to read variable values */
int getValueByKnob(int minimum, int maximum, int step_size,  int initial,
    const char * prefix, const char * postfix) {  // <<<--- added const to pre and post
  int knob = 0;
  int knob_value;

  while (btnDown())
    active_delay(100);

  active_delay(200);

  knob_value = initial;

  strcpy(gbuffB, prefix);
  itoa(knob_value, gbuffC, 10);
  strcat(gbuffB, gbuffC);
  strcat(gbuffB, postfix);
  drawCommandbar(gbuffB);

  while (!btnDown() && digitalRead(PTT) == HIGH) {

    knob = enc_read();

    if (knob != 0) {
      if (knob_value > minimum && knob < 0)
        knob_value -= step_size;
      if (knob_value < maximum && knob > 0)
        knob_value += step_size;

      strcpy(gbuffB, prefix);
      itoa(knob_value, gbuffC, 10);
      strcat(gbuffB, gbuffC);
      strcat(gbuffB, postfix);
      drawCommandbar(gbuffB);
    }

    checkCAT();
  }

  displayFillrect(30, 41, 280, 32, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  return knob_value;
}


/* display the frequency in the command area */
void printCarrierFreq(unsigned long freq) {

  // Serial.println("printCarrierFreq");

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


/* */
void displayDialog(const char * title, const char * instructions) {
  displayClear(DISPLAY_BLACK);
  displayRect(10, 10, 300, 220, DISPLAY_WHITE);
  displayHline(20, 45, 280, DISPLAY_WHITE);
  displayRect(12, 12, 296, 216, DISPLAY_WHITE);
  displayRawText(title, 20, 20, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  displayRawText(instructions, 20, 200, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


/* */
void displayVFO(int vfo) {

  // Serial.println("displayVFO");
  int x;
  int y;
  int displayColor = 0;   //   <<<--- Was not initialized originally

  Button btn;

  if (vfo == VFO_A) {
    getButton("VFOA", &btn);

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
    getButton("VFOB", &btn);

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

  // black out vfo button
  if (vfoDisplay[0] == 0) {

    uint16_t newX = btn.x - 1;
    uint16_t newY;
    uint16_t newW;
    uint16_t newH;

    // displayFillrect(btn.x, btn.y, btn.w, btn.h, DISPLAY_BLACK);  //  <<<---
    displayFillrect(btn.x - 1, btn.y - 1, btn.w + 2, btn.h + 2, DISPLAY_BLACK);

    // display highlight rectangle around vfo button if it's active
    if (vfoActive == vfo)
      displayRect(btn.x, btn.y, btn.w , btn.h, DISPLAY_WHITE, DISPLAY_3DBOTTOM);
    else
      displayRect(btn.x, btn.y, btn.w , btn.h, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  }

  x = btn.x + 6;
  y = btn.y + 6; // 3;  // <<<--- was 3

  char * text = gbuffC;

  // for (int i = 0; i <= strlen(gbuffC); i++) {  // <<<--- Was int originally, now uint16_t
  for (uint16_t i = 0; i <= strlen(gbuffC); i++) {
    char digit = gbuffC[i];

    if (digit != vfoDisplay[i]) {

      // displayFillrect(x, y, 15, btn.h - 6, DISPLAY_BLACK);  // <<<--- testing testing testing
      // checkCAT();

      displayChar(x, y + TEXT_LINE_HEIGHT + 3, digit, displayColor, DISPLAY_BLACK);

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

  // Serial.println("displayVFOs");

  memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  displayVFO(VFO_A);

  memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  displayVFO(VFO_B);
}


/* */
void btnDraw(struct Button * btn) {

  if (!strcmp(btn->text, "VFOA")) {
    memset(vfoDisplay, 0, sizeof(vfoDisplay));
    displayVFO(VFO_A);
  }
  else if (!strcmp(btn->text, "VFOB")) {
    memset(vfoDisplay, 0, sizeof(vfoDisplay));
    displayVFO(VFO_B);
  }
  else if ((!strcmp(btn->text, "RIT") && ritOn == true) ||
           (!strcmp(btn->text, "USB") && isUSB == true) ||
           (!strcmp(btn->text, "LSB") && isUSB == false) ||
           (!strcmp(btn->text, "SPL") && splitOn == true))
    displayText(btn->text, btn->x, btn->y, btn->w, btn->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_ORANGE, DISPLAY_ORANGE);
  else if (!strcmp(btn->text, "CW") && cwMode == true)
    displayText(btn->text, btn->x, btn->y, btn->w, btn->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_ORANGE, DISPLAY_ORANGE);
  else
    displayText(btn->text, btn->x, btn->y, btn->w, btn->h, DISPLAY_DIMGOLD, DISPLAY_BLACK, DISPLAY_DARKGREY, DISPLAY_3DBOTTOM);  // DISPLAY_GREEN // <<<---
}


/* */
void displayRIT() {

  // Serial.println("displayRIT");
  // displayFillrect(0, 41, 320, 30, DISPLAY_WILLBACK); //DISPLAY_NAVY);  // <<<--- seems like this is done in displayText??

  if (ritOn) {
    strcpy(gbuffC, "TX:");
    formatFreq(ritTxFrequency, gbuffC + 3);

    if (vfoActive == VFO_A)
      displayText(gbuffC, 0, 45, 159, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);
    else
      displayText(gbuffC, 160, 45, 159, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);
  }
  else {
    clearCommandbar();
    //if (vfoActive == VFO_A)
      //displayText("", 0, 45, 159, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);
    //else
      //displayText("", 160, 45, 159, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);
  }
}


/* */
void fastTune() {
  int encoder;

  // if the btn is down, wait until it is up
  while (btnDown())
    active_delay(50);

  active_delay(300);

  displayRawText("Fast tune", 100, 55, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);

  while (true) {
    checkCAT();

    // exit after debouncing the btnDown
    if (btnDown()) {
      displayFillrect(100, 55, 120, 30, DISPLAY_WILLBACK); //DISPLAY_NAVY);

      // wait until the button is realsed and then return
      while (btnDown())
        active_delay(50);

      active_delay(300);

      return;
    }

    encoder = enc_read();

    if (encoder != 0) {

      if (encoder > 0 && frequency < 30000000l)
        frequency += 50000l;
      else if (encoder < 0 && frequency > 600000l)
        frequency -= 50000l;

      setFrequency(frequency);

      displayVFO(vfoActive);
    }
  }  // end of the event loop
}


/* */
void enterFreq() {
  // force the display to refresh everything
  // display all the buttons

  // int f;    // <<<--- Uncertain if this is used further down -- two scopes of
  // 'f' shadowed originally, second 'f' changed to 'frq'

  for (int i = 0; i < MAX_KEYS; i++) {
    struct Button btn1;
    memcpy_P(&btn1, keypad + i, sizeof(struct Button));
    btnDraw(&btn1);
  }

  int cursor_pos = 0;
  memset(gbuffC, 0, sizeof(gbuffC));
  // f = frequency / 1000l;    // <<<--- Uncertain if this is used further down

  while (true) {
    checkCAT();

    if (!readTouch())
      continue;

    scaleTouch(&ts_point);

    // int total = sizeof(btn_set) / sizeof(struct Button);

    for (int i = 0; i < MAX_KEYS; i++) {
      struct Button btn2;
      memcpy_P(&btn2, keypad + i, sizeof(struct Button));

      int x2 = btn2.x + btn2.w;
      int y2 = btn2.y + btn2.h;

      if (btn2.x < ts_point.x && ts_point.x < x2 &&
          btn2.y < ts_point.y && ts_point.y < y2) {

        if (!strcmp(btn2.text, "OK")) {
          long frq = atol(gbuffC);

          if (30000 >= frq && frq > 100) {
            frequency = frq * 1000l;
            setFrequency(frequency);

            if (vfoActive == VFO_A)
              vfoA = frequency;
            else
              vfoB = frequency;

            saveVFOs();
          }

          guiUpdate();
          return;
        }
        else if (!strcmp(btn2.text, "<-")) {
          gbuffC[cursor_pos] = 0;

          if (cursor_pos > 0)
            cursor_pos--;

          gbuffC[cursor_pos] = 0;
        }
        else if (!strcmp(btn2.text, "Can")) {
          guiUpdate(false, false);
          return;
        }
        else if ('0' <= btn2.text[0] && btn2.text[0] <= '9') {
          gbuffC[cursor_pos++] = btn2.text[0];
          gbuffC[cursor_pos] = 0;
        }
      }
    } // end of the button scanning loop

    strcpy(gbuffB, gbuffC);
    strcat(gbuffB, " KHz");
    displayText(gbuffB, 0, 42, 320, 30, DISPLAY_WHITE, DISPLAY_WILLBACK, DISPLAY_WILLBACK); //DISPLAY_NAVY, DISPLAY_NAVY);

    delay(300);

    while (readTouch())
      checkCAT();
  } // end of event loop : while(true)
}


/* shows info at bottom of home screen */
void drawStatusbar() {

  // Serial.println("drawStatusbar");

  displayFillrect(0, 201, 320, 39, DISPLAY_WILLBACK); //DISPLAY_NAVY);

  strcpy(gbuffB, " cw:");
  int wpm = 1200 / cwSpeed;
  itoa(wpm, gbuffC, 10);
  strcat(gbuffB, gbuffC);
  strcat(gbuffB, "wpm, ");
  itoa(sideTone, gbuffC, 10);
  strcat(gbuffB, gbuffC);
  strcat(gbuffB, "hz");

  displayRawText(gbuffB, 0, 210, DISPLAY_CYAN, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


/* */
void drawTx() {
  if (inTx)
    displayText("TX", 280, 48, 37, 28, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_BLUE);
  else
    displayFillrect(280, 48, 37, 28, DISPLAY_WILLBACK); //DISPLAY_NAVY);
}


///* */
//void drawStatusbar() {
  //drawCWStatus();
//}


/* */
void guiUpdate(bool clearScreen, bool refreshVFOs) {

  // Serial.println("guiUpdate");

  /*
    if (doingCAT)
      return;
  */

  // use the current frequency as the VFO frequency for the active VFO
  if (clearScreen == true)
    displayClear(DISPLAY_WILLBACK); //DISPLAY_NAVY);
  //else
    //clearCommandbar();  //  <<<--- clearing in displayRIT() anyway

  //memset(vfoDisplay, 0, 12);
  //displayVFO(VFO_A);

  //checkCAT();

  //memset(vfoDisplay, 0, 12);
  //displayVFO(VFO_B);
  if (refreshVFOs == true)
    displayVFOs();

  // checkCAT();  //  <<<---
  displayRIT();

  checkCAT();

  // force the display to refresh everything
  // display all the buttons
  for (int i = 0; i < MAX_BUTTONS; i++) {
    struct Button btn;

    memcpy_P(&btn, btn_set + i, sizeof(struct Button));
    btnDraw(&btn);

    // checkCAT();  // <<<---
  }

  checkCAT();  // <<<---
  drawStatusbar();

  //checkCAT();
}


///* this builds up the top line of the display with frequency and mode */
//void updateDisplay() {
  //displayVFO(vfoActive);
//}


/*
   The A7 And A6 are purely analog lines on the Arduino Nano
   These need to be pulled up externally using two 10 K resistors

   There are excellent pages on the Internet about how these encoders work
   and how they should be used. We have elected to use the simplest way
   to use these encoders without the complexity of interrupts etc to
   keep it understandable.

   The enc_state returns a two-bit number such that each bit reflects the current
   value of each of the two phases of the encoder

   The enc_read returns the number of net pulses counted over 50 msecs.
   If the puluses are -ve, they were anti-clockwise, if they are +ve, the
   were in the clockwise directions. Higher the pulses, greater the speed
   at which the enccoder was spun
*/
/*
  byte enc_state (void) {
    //Serial.print(digitalRead(ENC_A)); Serial.print(":");Serial.println(digitalRead(ENC_B));
    return (digitalRead(ENC_A) == 1 ? 1 : 0) + (digitalRead(ENC_B) == 1 ? 2: 0);
  }

  int enc_read(void) {
  int result = 0;
  byte newState;
  int enc_speed = 0;

  long stop_by = millis() + 200;

  while (millis() < stop_by) { // check if the previous state was stable
    newState = enc_state(); // Get current state

  //    if (newState != enc_prev_state)
  //      active_delay(20);

    if (enc_state() != newState || newState == enc_prev_state)
      continue;
    //these transitions point to the encoder being rotated anti-clockwise
    if ((enc_prev_state == 0 && newState == 2) ||
      (enc_prev_state == 2 && newState == 3) ||
      (enc_prev_state == 3 && newState == 1) ||
      (enc_prev_state == 1 && newState == 0)){
        result--;
      }
    //these transitions point o the enccoder being rotated clockwise
    if ((enc_prev_state == 0 && newState == 1) ||
      (enc_prev_state == 1 && newState == 3) ||
      (enc_prev_state == 3 && newState == 2) ||
      (enc_prev_state == 2 && newState == 0)){
        result++;
      }
    enc_prev_state = newState; // Record state for next pulse interpretation
    enc_speed++;
    active_delay(1);
  }
  //if (result)
  //  Serial.println(result);
  return(result);
  }
*/


/* */
void ritToggle(struct Button * btn) {

  if (ritOn == false)
    ritEnable(frequency);
  else
    ritDisable();

  btnDraw(btn);
  displayRIT();
}


/* */
void splitToggle(struct Button * btn1) {

  if (splitOn)
    splitOn = false;
  else
    splitOn = true;

  btnDraw(btn1);

  // disable rit as well
  ritDisable();

  struct Button btn2;
  getButton("RIT", &btn2);
  btnDraw(&btn2);

  displayRIT();

  //memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  //displayVFO(VFO_A);

  //memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  //displayVFO(VFO_B);
  displayVFOs();
}


/* */
void vfoReset() {

  // Serial.println("vfoReset");

  Button btn;

  if (vfoActive == VFO_A)
    vfoB = vfoA;
  else
    vfoA = vfoB;

  if (splitOn) {
    getButton("SPL", &btn);
    splitToggle(&btn);
  }

  if (ritOn) {
    getButton("RIT", &btn);
    ritToggle(&btn);
  }

  //memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  //displayVFO(VFO_A);

  //memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  //displayVFO(VFO_B);
  displayVFOs();

  saveVFOs();
}


/* */
void cwToggle(struct Button * btn) {
  if (cwMode == false)
    cwMode = true;
  else
    cwMode = false;

  setFrequency(frequency);
  btnDraw(btn);
}


/* */
void sidebandToggle(struct Button * btn1) {

  if (!strcmp(btn1->text, "LSB")) {
    if (isUSB == false)  //  <<<--- seems extraneous, but saves drawing time
      return;

    isUSB = false;
  } else {
    if (isUSB == true)  //  <<<--- seems extraneous, but saves drawing time
      return;

    isUSB = true;
  }

  struct Button btn2;

  getButton("USB", &btn2);
  btnDraw(&btn2);

  getButton("LSB", &btn2);
  btnDraw(&btn2);

  saveVFOs();
}


/* */
void redrawVFOs() {

  // Serial.println("redrawVFOs");  //  <<<--- test test test

  struct Button btn;

  ritDisable();
  getButton("RIT", &btn);
  btnDraw(&btn);
  displayRIT();

  //memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  //displayVFO(VFO_A);

  //memset(vfoDisplay, 0, sizeof(vfoDisplay));   // <<<--- Why are we doing this here?
  //displayVFO(VFO_B);
  displayVFOs();

  // draw the lsb/usb buttons, the sidebands might have changed
  getButton("LSB", &btn);
  btnDraw(&btn);

  getButton("USB", &btn);
  btnDraw(&btn);
}


/* */
void switchBand(long bandfreq) {
  long offset;

  //  Serial.println(frequency);
  //  Serial.println(bandfreq);
  if (3500000l <= frequency && frequency <= 4000000l)
    offset = frequency - 3500000l;
  else if (24800000l <= frequency && frequency <= 25000000l)
    offset = frequency - 24800000l;
  else
    offset = frequency % 1000000l;

  //  Serial.println(offset);

  setFrequency(bandfreq + offset);
  //updateDisplay();  // <<<---
  displayVFO(vfoActive);
  saveVFOs();
}


/* */
// int setCwSpeed() {  // <<<--- Was int, changed to void
void setCwSpeed() {
  // int knob = 0;
  int wpm;

  wpm = 1200 / cwSpeed;

  wpm = getValueByKnob(1, 100, 1,  wpm, "CW: ", " WPM");

  cwSpeed = 1200 / wpm;

  EEPROM.put(CW_SPEED, cwSpeed);

  active_delay(500);

  drawStatusbar();
  //    printLine2("");
  //    updateDisplay();
}


/* */
void setCwTone() {
  int knob = 0;
  // int prev_sideTone;

  tone(CW_TONE, sideTone);

  // disable all clock 1 and clock 2
  while (digitalRead(PTT) == HIGH && !btnDown())
  {
    knob = enc_read();

    if (knob > 0 && sideTone < 2000)
      sideTone += 10;
    else if (knob < 0 && sideTone > 100 )
      sideTone -= 10;
    else
      continue; // don't update the frequency or the display

    tone(CW_TONE, sideTone);
    itoa(sideTone, gbuffC, 10);
    strcpy(gbuffB, "CW Tone: ");
    strcat(gbuffB, gbuffC);
    strcat(gbuffB, " Hz");
    drawCommandbar(gbuffB);
    //printLine2(gbuffB);

    checkCAT();
    active_delay(20);
  }

  noTone(CW_TONE);

  // save the setting
  EEPROM.put(CW_SIDETONE, sideTone);

  displayFillrect(30, 41, 280, 32, DISPLAY_WILLBACK); //DISPLAY_NAVY);
  drawStatusbar();
  //  printLine2("");
  //  updateDisplay();
}


/* */
void doCommand(struct Button * btn) {

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
  else if (!strcmp(btn->text, "VFOA")) {
    if (vfoActive == VFO_A)
      //fastTune();  // <<<--- no thank you -- too easy to trigger
      return;
    else
      switchVFO(VFO_A);
  }
  else if (!strcmp(btn->text, "VFOB")) {
    if (vfoActive == VFO_B)
      //fastTune();  // <<<--- no thank you -- too easy to trigger
      return;
    else
      switchVFO(VFO_B);
  }
  else if (!strcmp(btn->text, "A=B"))
    vfoReset();
  else if (!strcmp(btn->text, "80"))
    switchBand(3500000l);
  else if (!strcmp(btn->text, "40"))
    switchBand(7000000l);
  else if (!strcmp(btn->text, "30"))
    switchBand(10000000l);
  else if (!strcmp(btn->text, "20"))
    switchBand(14000000l);
  else if (!strcmp(btn->text, "17"))
    switchBand(18000000l);
  else if (!strcmp(btn->text, "15"))
    switchBand(21000000l);
  else if (!strcmp(btn->text, "13"))
    switchBand(24800000l);
  else if (!strcmp(btn->text, "10"))
    switchBand(28000000l);
  else if (!strcmp(btn->text, "FRQ"))
    enterFreq();
  else if (!strcmp(btn->text, "WPM"))
    setCwSpeed();
  else if (!strcmp(btn->text, "TON"))
    setCwTone();
}


/* */
void checkTouch() {

  if (!readTouch())
    return;

  while (readTouch())
    checkCAT();

  scaleTouch(&ts_point);

  /* //debug code
    Serial.print(ts_point.x); Serial.print(' ');Serial.println(ts_point.y);
  */
  // int total = sizeof(btn_set) / sizeof(struct Button);

  for (int i = 0; i < MAX_BUTTONS; i++) {
    struct Button btn;
    memcpy_P(&btn, btn_set + i, sizeof(struct Button));

    int x2 = btn.x + btn.w;
    int y2 = btn.y + btn.h;

    if (btn.x < ts_point.x && ts_point.x < x2 &&
        btn.y < ts_point.y && ts_point.y < y2)
      doCommand(&btn);
  }
}


/* returns true if the button is pressed */
int btnDown() {
  if (digitalRead(FBUTTON) == HIGH)
    return 0;
  else
    return 1;
}


/* */
void drawFocus(int ibtn, int color) {

  // Serial.println("drawFocus");

  struct Button btn;

  memcpy_P(&btn, btn_set + ibtn, sizeof(struct Button));

  displayRect(btn.x, btn.y, btn.w, btn.h, color);
}


/* */
void doCommands() {
  int select = 0;
  int i;
  int prevButton = 0;  //  <<<--- Not initialized originally
  int btnState;

  // wait for the button to be raised up
  while (btnDown())
    active_delay(50);

  active_delay(50);  // debounce

  menuOn = true; //2;

  while (menuOn) {

    // check if the knob's button was pressed
    btnState = btnDown();

    if (btnState) {
      struct Button btn;
      memcpy_P(&btn, btn_set + select / 10, sizeof(struct Button));

      doCommand(&btn);

      // unfocus the buttons
      drawFocus(select, DISPLAY_BLUE);

      if (vfoActive == VFO_A)
        drawFocus(0, DISPLAY_WHITE);
      else
        drawFocus(1, DISPLAY_WHITE);

      // wait for the button to be up and debounce
      while (btnDown())
        active_delay(100);

      active_delay(500);
      return;
    }

    i = enc_read();

    if (i == 0) {
      active_delay(50);
      continue;
    }

    if (i > 0) {
      if (select + i < MAX_BUTTONS * 10)
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
  //  guiUpdate();

  //debounce the button
  while (btnDown())
    active_delay(50);

  active_delay(50);

  checkCAT();
}
