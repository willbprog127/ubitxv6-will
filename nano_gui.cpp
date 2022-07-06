/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
#include "ubitx.h"
#include "nano_gui.h"

/*
    * display panel pin assignments *
    14  T_IRQ           2 std   changed
    13  T_DOUT              (parallel to SOD/MOSI, pin 9 of display)
    12  T_DIN               (parallel to SDI/MISO, pin 6 of display)
    11  T_CS            9   (we need to specify this)
    10  T_CLK               (parallel to SCK, pin 7 of display)
    9   SDO(MSIO) 12    12  (spi)
    8   LED       A0    8   (not needed, permanently on +3.3v) (resistor from 5v,
    7   SCK       13    13  (spi)
    6   SDI       11    11  (spi)
    5   D/C       A3    7   (changable)
    4   RESET     A4    9   (not needed, permanently +5v)
    3   CS        A5    10  (changable)
    2   GND       GND
    1   VCC       VCC

    The model is called tjctm24028-spi
    it uses an ILI9341 display controller and an  XPT2046 touch controller.
*/

#define TFT_CS 10
#define CS_PIN  8     // this is the pin to select the touch controller on spi interface
#define TFT_RS  9

const byte maxVBuff = 64;

const int16_t zThreshold = 400;
const byte mSecThreshold = 3;

const SPISettings spiSetting = SPISettings(2000000, MSBFIRST, SPI_MODE0);

struct Point ts_point;

const GFXfont * gfxFont = NULL;  // <<<--- added const

char vbuff[maxVBuff];

/* filled later by the screen calibration routine */
int slope_x = 104;
int slope_y = 137;
int offset_x = 28;
int offset_y = 29;

static uint32_t msraw = 0x80000000;
static int16_t xraw = 0;
static int16_t yraw = 0;
static int16_t zraw = 0;
// static uint8_t rotation = 1;  //  <<<--- never assigned a value


/* get touch calibration info from eeprom */
void readTouchCalibration() {
  EEPROM.get(SLOPE_X, slope_x);
  EEPROM.get(SLOPE_Y, slope_y);
  EEPROM.get(OFFSET_X, offset_x);
  EEPROM.get(OFFSET_Y, offset_y);
}


/* write touch calibration info to eeprom */
void writeTouchCalibration() {
  EEPROM.put(SLOPE_X, slope_x);
  EEPROM.put(SLOPE_Y, slope_y);
  EEPROM.put(OFFSET_X, offset_x);
  EEPROM.put(OFFSET_Y, offset_y);
}


/* */
static int16_t touch_besttwoavg(int16_t x , int16_t y , int16_t z) {

  int16_t da;
  int16_t db;
  int16_t dc;
  int16_t reta = 0;

  if (x > y)
    da = x - y;
  else
    da = y - x;

  if (x > z)
    db = x - z;
  else
    db = z - x;

  if (z > y)
    dc = z - y;
  else
    dc = y - z;

  if (da <= db && da <= dc)
    reta = (x + y) >> 1;
  else if (db <= da && db <= dc)
    reta = (x + z) >> 1;
  else reta = (y + z) >> 1;

  return (reta);
}


/* */
static void touch_update() {
  int16_t data[6];

  uint32_t now = millis();

  if (now - msraw < mSecThreshold)
    return;

  SPI.beginTransaction(spiSetting); // SPI_SETTING);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0xB1);  // Z1

  int16_t z1 = SPI.transfer16(0xC1) >> 3; // Z2
  int z = z1 + 4095;
  int16_t z2 = SPI.transfer16(0x91 /* X */) >> 3;

  z -= z2;

  if (z >= zThreshold) {
    SPI.transfer16(0x91);  // dummy X measure, 1st is always noisy

    data[0] = SPI.transfer16(0xD1) >> 3;  // Y
    data[1] = SPI.transfer16(0x91) >> 3;  // X
    data[2] = SPI.transfer16(0xD1) >> 3;  // Y
    data[3] = SPI.transfer16(0x91) >> 3;  // X
  } else
    data[0] = data[1] = data[2] = data[3] = 0; // Compiler warns these values may be used unset on early exit.

  data[4] = SPI.transfer16(0xD0) >> 3;  // Last Y touch power down
  data[5] = SPI.transfer16(0) >> 3;

  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  if (z < 0)
    z = 0;

  if (z < zThreshold) {
    zraw = 0;
    return;
  }

  zraw = z;

  int16_t x = touch_besttwoavg(data[0], data[2], data[4]);
  int16_t y = touch_besttwoavg(data[1], data[3], data[5]);

  // good read completed, set wait
  if (z >= zThreshold) {
    msraw = now;

    // most of the following commented out because 'rotation' is
    // never assigned a value besides the '1' initialization!

    //switch (rotation) {
      //case 0:
        //xraw = 4095 - y;
        //yraw = x;
        //break;

      //case 1:
        xraw = x;
        yraw = y;
        //break;

      //case 2:
        //xraw = y;
        //yraw = 4095 - x;
        //break;

      //default: // 3
        //xraw = 4095 - x;
        //yraw = 4095 - y;
    //}
  }
}


/* */
bool readTouch() {

  touch_update();

  if (zraw >= zThreshold) {
    ts_point.x = xraw;
    ts_point.y = yraw;
    return true;
  }

  return false;
}


/* */
void scaleTouch(struct Point * p) {
  p->x = ((long)(p->x - offset_x) * 10l) / (long)slope_x;
  p->y = ((long)(p->y - offset_y) * 10l) / (long)slope_y;
}


#if !defined(__INT_MAX__) || (__INT_MAX__ > 0xFFFF)
#define pgm_read_pointer(addr) ((void *)pgm_read_dword(addr))
#else
#define pgm_read_pointer(addr) ((void *)pgm_read_word(addr))
#endif


/* */
inline GFXglyph * pgm_read_glyph_ptr(const GFXfont * gfxFont, uint8_t c) {
#ifdef __AVR__
  return &(((GFXglyph *)pgm_read_pointer(&gfxFont->glyph))[c]);
#else
  // expression in __AVR__ section may generate "dereferencing type-punned pointer will break strict-aliasing rules" warning
  // In fact, on other platforms (such as STM32) there is no need to do this pointer magic as program memory may be read in a usual way
  // So expression may be simplified
  return gfxFont->glyph + c;
#endif //__AVR__
}


/* */
inline uint8_t * pgm_read_bitmap_ptr(const GFXfont * gfxFont) {
#ifdef __AVR__
  return (uint8_t *)pgm_read_pointer(&gfxFont->bitmap);
#else
  // expression in __AVR__ section generates "dereferencing type-punned pointer will break strict-aliasing rules" warning
  // In fact, on other platforms (such as STM32) there is no need to do this pointer magic as program memory may be read in a usual way
  // So expression may be simplified
  return gfxFont->bitmap;
#endif //__AVR__
}


/* */
inline static void utft_write(unsigned char d) {
  SPI.transfer(d);
}


/* */
inline static void utftCmd(unsigned char VH) {
  *(portOutputRegister(digitalPinToPort(TFT_RS))) &=  ~digitalPinToBitMask(TFT_RS); // LCD_RS=0;
  utft_write(VH);
}


/* */
inline static void utftData(unsigned char VH) {
  *(portOutputRegister(digitalPinToPort(TFT_RS))) |=  digitalPinToBitMask(TFT_RS); // LCD_RS=1;
  utft_write(VH);
}


/* */
static void utftAddress(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2) {

  utftCmd(0x2a);  // column address set

  utftData(x1 >> 8);
  utftData(x1);
  utftData(x2 >> 8);
  utftData(x2);
  utftCmd(0x2b);  // page address set

  utftData(y1 >> 8);
  utftData(y1);
  utftData(y2 >> 8);
  utftData(y2);
  utftCmd(0x2c);  // memory write
}


/* */
void displayPixel(unsigned int x, unsigned int y, unsigned int color) {

  digitalWrite(TFT_CS, LOW);

  utftCmd(0x02c); // write_memory_start
  utftAddress(x, y, x, y);
  utftData(color >> 8);
  utftData(color);

  digitalWrite(TFT_CS, HIGH);
}


/* */
void quickFill(int x1, int y1, int x2, int y2, int color) {

  unsigned long ncount = (unsigned long)(x2 - x1 + 1) * (unsigned long)(y2 - y1 + 1);
  int k = 0;

  // set the window
  digitalWrite(TFT_CS, LOW);
  utftCmd(0x02c); // write_memory_start
  utftAddress(x1, y1, x2, y2);
  *(portOutputRegister(digitalPinToPort(TFT_RS))) |=  digitalPinToBitMask(TFT_RS); // LCD_RS=1;

  while (ncount) {
    k = 0;

    for (int i = 0; i < maxVBuff / 2; i++) {
      vbuff[k++] = color >> 8;
      vbuff[k++] = color & 0xff;
    }

    if (ncount > maxVBuff / 2) {
      SPI.transfer(vbuff, maxVBuff);
      ncount -= maxVBuff / 2;
    } else {
      SPI.transfer(vbuff, (int)ncount * 2);
      ncount = 0;
    }

    // checkCAT();  // <<<--- ugh ugh ugh
  }

  checkCAT();

  digitalWrite(TFT_CS, HIGH);
}


/* */
void displayHline(unsigned int x, unsigned int y, unsigned int l, unsigned int color) {
  quickFill(x, y, x + l, y, color);
}


/* */
void displayVline(unsigned int x, unsigned int y, unsigned int l, unsigned int color) {
  quickFill(x, y, x, y + l, color);
}


/* */
void displayClear(unsigned int color) {
  quickFill(0, 0, 319, 239, color);
}


/* */
void displayRect(unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int hicolor, unsigned int lowcolor) {

  if (lowcolor == 0)
    lowcolor = hicolor;

  displayHline(x, y, w, hicolor);
  displayHline(x, y + h, w, lowcolor);
  displayVline(x, y, h, hicolor);
  displayVline(x + w, y, h, lowcolor);
}


/* */
void displayFillrect(const unsigned int x, const unsigned int y, const unsigned int w, const unsigned int h, const unsigned int color) {
  quickFill(x, y, x + w, y + h, color);
}


/* */
void xpt2046_Init() {
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
}


/* */
void displayInit(void) {

  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV4); // 4 MHz (half speed)
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);

  gfxFont = &ubitx_font;
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_RS, OUTPUT);

  digitalWrite(TFT_CS, LOW); //CS

  utftCmd(0xCB);
  utftData(0x39);
  utftData(0x2C);
  utftData(0x00);
  utftData(0x34);
  utftData(0x02);

  utftCmd(0xCF);
  utftData(0x00);
  utftData(0XC1);
  utftData(0X30);

  utftCmd(0xE8);
  utftData(0x85);
  utftData(0x00);
  utftData(0x78);

  utftCmd(0xEA);
  utftData(0x00);
  utftData(0x00);

  utftCmd(0xED);
  utftData(0x64);
  utftData(0x03);
  utftData(0X12);
  utftData(0X81);

  utftCmd(0xF7);
  utftData(0x20);

  utftCmd(0xC0);    // Power control
  utftData(0x23);   // VRH[5:0]

  utftCmd(0xC1);    // Power control
  utftData(0x10);   // SAP[2:0];BT[3:0]

  utftCmd(0xC5);    // VCM control
  utftData(0x3e);   // Contrast
  utftData(0x28);

  utftCmd(0xC7);    // VCM control2
  utftData(0x86);   // --

  utftCmd(0x36);    // Memory Access Control
  utftData(0x28);   // Make this horizontal display

  utftCmd(0x3A);
  utftData(0x55);

  utftCmd(0xB1);
  utftData(0x00);
  utftData(0x18);

  utftCmd(0xB6);    // Display Function Control
  utftData(0x08);
  utftData(0x82);
  utftData(0x27);

  utftCmd(0x11);    // Exit Sleep
  delay(120);

  utftCmd(0x29);    // Display on
  utftCmd(0x2c);
  digitalWrite(TFT_CS, HIGH);

  // now to init the touch screen controller
  // ts.begin();
  // ts.setRotation(1);
  xpt2046_Init();

  readTouchCalibration();
}



/* *************************************************************************
   @brief   Draw a single character
    @param    x   Bottom left corner x coordinate
    @param    y   Bottom left corner y coordinate
    @param    c   The 8-bit font-indexed character (likely ascii)
    @param    color 16-bit 5-6-5 Color to draw chraracter with
    @param    bg 16-bit 5-6-5 Color to fill background with (if same as color, no background)
    @param    size_x  Font magnification level in X-axis, 1 is 'original' size
    @param    size_y  Font magnification level in Y-axis, 1 is 'original' size
*/
void displayChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg) {
  //
  c -= (uint8_t)pgm_read_byte(&gfxFont->first);

  GFXglyph * glyph  = pgm_read_glyph_ptr(gfxFont, c);
  uint8_t * bitmap = pgm_read_bitmap_ptr(gfxFont);

  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
  uint8_t w  = pgm_read_byte(&glyph->width);
  uint8_t h  = pgm_read_byte(&glyph->height);
  int8_t xo = pgm_read_byte(&glyph->xOffset);
  int8_t yo = pgm_read_byte(&glyph->yOffset);

  uint8_t xx;
  uint8_t yy;

  uint8_t bits = 0;
  uint8_t bit = 0;

  int k;

  digitalWrite(TFT_CS, LOW);

  for (yy = 0; yy < h; yy++) {
    k = 0;

    for (xx = 0; xx < w; xx++) {
      if (!(bit++ & 7)) {
        bits = pgm_read_byte(&bitmap[bo++]);
      }

      if (bits & 0x80) {
        vbuff[k++] = color >> 8;
        vbuff[k++] = color & 0xff;
      } else {
        vbuff[k++] = bg >> 8;
        vbuff[k++] = bg & 0xff;
      }

      bits <<= 1;

    }

    utftAddress(x + xo, y + yo + yy, x + xo + w, y + yo + yy);
    *(portOutputRegister(digitalPinToPort(TFT_RS))) |= digitalPinToBitMask(TFT_RS); // LCD_RS=1;
    SPI.transfer(vbuff, k);
    // checkCAT();  // <<<---
  }
  checkCAT();
}


/* */
int displayTextExtent(const char * text) {

  int ext = 0;
  while (*text) {
    char c = *text++;
    uint8_t first = pgm_read_byte(&gfxFont->first);

    if ((c >= first) && (c <= (uint8_t)pgm_read_byte(&gfxFont->last))) {
      GFXglyph * glyph  = pgm_read_glyph_ptr(gfxFont, c - first);
      ext += (uint8_t)pgm_read_byte(&glyph->xAdvance);
    }
  } // end of the while loop of the characters to be printed

  return ext;
}


/* */
void displayRawText(const char * text, int x1, int y1, int color, int background) {  // <<<--- changed to const char
  while (*text) {

    char c = *text++;

    uint8_t first = pgm_read_byte(&gfxFont->first);

    if ((c >= first) && (c <= (uint8_t)pgm_read_byte(&gfxFont->last))) {
      GFXglyph * glyph  = pgm_read_glyph_ptr(gfxFont, c - first);
      uint8_t w = pgm_read_byte(&glyph->width);
      uint8_t h = pgm_read_byte(&glyph->height);

      if ((w > 0) && (h > 0)) { // Is there an associated bitmap?
        // int16_t xo = (int8_t)pgm_read_byte(&glyph->xOffset); // sic
        displayChar(x1, y1 + TEXT_LINE_HEIGHT, c, color, background);
        // checkCAT();  // <<<---
      }

      x1 += (uint8_t)pgm_read_byte(&glyph->xAdvance);
    }
  } // end of the while loop of the characters to be printed

  checkCAT();
}


// The generic routine to display one line on the LCD
void displayText(const char * text, int x1, int y1, int w, int h, unsigned int color, unsigned int background, unsigned int borderhigh, unsigned int borderlow) {  // <<<--- changed to const char

  if (borderlow == 0)
    borderlow = borderhigh;

  displayFillrect(x1, y1, w , h, background);  // erase spot where text will be
  displayRect(x1, y1, w , h, borderhigh, borderlow); // DISPLAY_3DBOTTOM);  // <<<--- no no, Will!!!

  x1 += (w - displayTextExtent(text)) / 2;
  y1 += (h - TEXT_LINE_HEIGHT) / 2;

  while (*text) {
    char c = *text++;

    uint8_t first = pgm_read_byte(&gfxFont->first);

    if ((c >= first) && (c <= (uint8_t)pgm_read_byte(&gfxFont->last))) {
      GFXglyph * glyph  = pgm_read_glyph_ptr(gfxFont, c - first);
      uint8_t ww = pgm_read_byte(&glyph->width);
      uint8_t hh = pgm_read_byte(&glyph->height);

      if ((ww > 0) && (hh > 0)) { // Is there an associated bitmap?
        //int16_t xo = (int8_t)pgm_read_byte(&glyph->xOffset); // sic
        displayChar(x1, y1 + TEXT_LINE_HEIGHT, c, color, background);
        // checkCAT();  // <<<--- blebroasdflkjt
      }

      x1 += (uint8_t)pgm_read_byte(&glyph->xAdvance);
    }
  } // end of the while loop of the characters to be printed

  checkCAT();  // <<<---
}


/* */
void doTouchCalibration() {
  int x1;
  int y1;
  int x2;
  int y2;
  int x3;
  int y3;
  int x4;
  int y4;

  displayClear(DISPLAY_BLACK);
  displayText("Click on the cross", 20, 100, 200, 50, DISPLAY_WHITE, DISPLAY_BLACK, DISPLAY_BLACK);

  // TOP-LEFT
  displayHline(10, 20, 20, DISPLAY_WHITE);
  displayVline(20, 10, 20, DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  while (readTouch())
    delay(100);

  x1 = ts_point.x;
  y1 = ts_point.y;

  // rubout the previous cross
  displayHline(10, 20, 20, DISPLAY_BLACK);
  displayVline(20, 10, 20, DISPLAY_BLACK);

  delay(1000);

  // TOP RIGHT
  displayHline(290, 20, 20, DISPLAY_WHITE);
  displayVline(300, 10, 20, DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  while (readTouch())
    delay(100);

  x2 = ts_point.x;
  y2 = ts_point.y;

  displayHline(290, 20, 20, DISPLAY_BLACK);
  displayVline(300, 10, 20, DISPLAY_BLACK);

  delay(1000);

  // BOTTOM LEFT
  displayHline(10, 220, 20, DISPLAY_WHITE);
  displayVline(20, 210, 20, DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  x3 = ts_point.x;
  y3 = ts_point.y;

  while (readTouch())
    delay(100);

  displayHline(10, 220, 20, DISPLAY_BLACK);
  displayVline(20, 210, 20, DISPLAY_BLACK);

  delay(1000);

  // BOTTOM RIGHT
  displayHline(290, 220, 20, DISPLAY_WHITE);
  displayVline(300, 210, 20, DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  x4 = ts_point.x;
  y4 = ts_point.y;

  displayHline(290, 220, 20, DISPLAY_BLACK);
  displayVline(300, 210, 20, DISPLAY_BLACK);

  // we average two readings and divide them by half and store them as scaled integers 10 times their actual, fractional value
  // the x points are located at 20 and 300 on x axis, hence, the delta x is 280, we take 28 instead, to preserve fractional value,
  // there are two readings (x1, x2) and (x3, x4). Hence, we have to divide by 28 * 2 = 56
  slope_x = ((x4 - x3) + (x2 - x1)) / 56;
  // the y points are located at 20 and 220 on the y axis, hence, the delta is 200. we take it as 20 instead, to preserve the fraction value
  // there are two readings (y1, y2) and (y3, y4). Hence we have to divide by 20 * 2 = 40
  slope_y = ((y3 - y1) + (y4 - y2)) / 40;

  // x1, y1 is at 20 pixels
  offset_x = x1 + -((20 * slope_x) / 10);
  offset_y = y1 + -((20 * slope_y) / 10);

  /*
    Serial.print(x1);Serial.print(':');Serial.println(y1);
    Serial.print(x2);Serial.print(':');Serial.println(y2);
    Serial.print(x3);Serial.print(':');Serial.println(y3);
    Serial.print(x4);Serial.print(':');Serial.println(y4);

    //for debugging
    Serial.print(slope_x); Serial.print(' ');
    Serial.print(slope_y); Serial.print(' ');
    Serial.print(offset_x); Serial.print(' ');
    Serial.println(offset_y); Serial.println(' ');
  */
  writeTouchCalibration();

  displayClear(DISPLAY_BLACK);
}
