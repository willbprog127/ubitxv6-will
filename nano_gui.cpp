/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include <EEPROM.h>
#include "ubitx.h"
#include "nano_gui.h"

/*
    display panel pin assignments
    - - -
    PIN NAME      MCU   ?   DESC
    ------------------------------------------------------------------
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

    display is model TJCTM24028-SPI - TFT LCD 2.8 inch 240×320 RGB SPI display with touchscreen
    it uses an ILI9341 display controller and an XPT2046 touch controller.
*/

/* file-level constants */
static constexpr uint8_t M_TFT_CS = 10;   // display chip-select pin
static constexpr uint8_t M_CS_PIN = 8;   // touch select pin on spi interface
static constexpr uint8_t M_TFT_RS = 9;   // display reset pin

static constexpr int16_t M_Z_THRESHOLD = 400;
static constexpr uint8_t M_SEC_THRESHOLD = 3;

static constexpr uint8_t M_MAX_V_BUFF = 64;

/* file-level variables */
static const SPISettings m_spiSetting = SPISettings(2000000, MSBFIRST, SPI_MODE0);
static const GFXfont * m_gfxFont = NULL;

static char m_vBuff[M_MAX_V_BUFF];

/* filled by the screen calibration routine */
static int16_t m_slopeX = 104;
static int16_t m_slopeY = 137;
static int16_t m_offsetX = 28;
static int16_t m_offsetY = 29;

static uint32_t m_msRaw = 0x80000000;
static int16_t m_xRaw = 0;
static int16_t m_yRaw = 0;
static int16_t m_zRaw = 0;

struct Point g_tsPoint;

/* get touch calibration info from eeprom */
static void readTouchCalibration ()
{
  EEPROM.get(SLOPE_X, m_slopeX);
  EEPROM.get(SLOPE_Y, m_slopeY);
  EEPROM.get(OFFSET_X, m_offsetX);
  EEPROM.get(OFFSET_Y, m_offsetY);
}

/* write touch calibration info to eeprom */
static void writeTouchCalibration ()
{
  EEPROM.put(SLOPE_X, m_slopeX);
  EEPROM.put(SLOPE_Y, m_slopeY);
  EEPROM.put(OFFSET_X, m_offsetX);
  EEPROM.put(OFFSET_Y, m_offsetY);
}

/*
 return the average of the two closest values among x, y, z.
 used to reject a single outlier from three touch samples.
*/
static int16_t touchBestTwoAvg (int16_t x, int16_t y, int16_t z)
{
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

  return reta;
}

/*
  Read touchscreen
  Measures pressure (Z)
 - If touched, takes several X/Y samples
 - Uses touchBestTwoAvg() to reject outliers
 - Updates m_xRaw, m_yRaw, m_zRaw
 - Rate-limited by M_SEC_THRESHOLD
*/
static void touchUpdate ()
{
  int16_t data[6];

  uint32_t now = millis();

  // rate limiting
  if (now - m_msRaw < M_SEC_THRESHOLD)
    return;

  memset(data, 0, sizeof(data));

  SPI.beginTransaction(m_spiSetting);

  digitalWrite(M_CS_PIN, LOW);  // display controller chip select

  // read touch pressure
  SPI.transfer(0xB1);  // Z1

  int16_t z1 = SPI.transfer16(0xC1) >> 3;  // Z2
  int16_t z = z1 + 4095;
  int16_t z2 = SPI.transfer16(0x91) >> 3;  // X

  z -= z2;

  // is a valid touch
  if (z >= M_Z_THRESHOLD)
  {
    SPI.transfer16(0x91);  // dummy X measure, 1st is always noisy

    data[0] = SPI.transfer16(0xD1) >> 3;  // Y
    data[1] = SPI.transfer16(0x91) >> 3;  // X
    data[2] = SPI.transfer16(0xD1) >> 3;  // Y
    data[3] = SPI.transfer16(0x91) >> 3;  // X
  }
  else
    // is NOT a valid touch
    data[0] = data[1] = data[2] = data[3] = 0;

  data[4] = SPI.transfer16(0xD0) >> 3;  // Last Y touch power down
  data[5] = SPI.transfer16(0) >> 3;

  digitalWrite(M_CS_PIN, HIGH);
  SPI.endTransaction();

  if (z < 0)
    z = 0;

  if (z < M_Z_THRESHOLD)
  {
    m_zRaw = 0;
    return;
  }

  m_zRaw = z;

  int16_t x = touchBestTwoAvg(data[0], data[2], data[4]);
  int16_t y = touchBestTwoAvg(data[1], data[3], data[5]);

  // good read completed, set wait
  if (z >= M_Z_THRESHOLD)
  {
    m_msRaw = now;

    m_xRaw = x;
    m_yRaw = y;
  }
}

/* was there a valid touch? */
bool readTouch ()
{
  touchUpdate();

  if (m_zRaw >= M_Z_THRESHOLD)
  {
    g_tsPoint.x = m_xRaw;
    g_tsPoint.y = m_yRaw;

    return true;
  }

  return false;
}

/* scales touch values - in place */
void scaleTouch (struct Point * p)
{
  p->x = ((long)(p->x - m_offsetX) * 10l) / (long)m_slopeX;
  p->y = ((long)(p->y - m_offsetY) * 10l) / (long)m_slopeY;
}

/* alias PROGMEM helpers */
#if !defined(__INT_MAX__) || (__INT_MAX__ > 0xFFFF)
#define pgmReadPointer(addr) ((void *)pgm_read_dword(addr))
#else
#define pgmReadPointer(addr) ((void *)pgm_read_word(addr))
#endif

/* get pointer to font glyph */
inline GFXglyph * pgmReadGlyphPtr (const GFXfont * m_gfxFont, uint8_t c)
{
#ifdef __AVR__
  return &(((GFXglyph *)pgmReadPointer(&m_gfxFont->glyph))[c]);
#else
  // expression in __AVR__ section may generate "dereferencing type-punned pointer will break strict-aliasing rules" warning
  // In fact, on other platforms (such as STM32) there is no need to do this pointer magic as program memory may be read in a usual way
  // So expression may be simplified
  return m_gfxFont->glyph + c;
#endif //__AVR__
}

/* get pointer to font bitmap */
inline uint8_t * pgmReadBitmapPtr (const GFXfont * m_gfxFont)
{
#ifdef __AVR__
  return (uint8_t *)pgmReadPointer(&m_gfxFont->bitmap);
#else
  // expression in __AVR__ section generates "dereferencing type-punned pointer will break strict-aliasing rules" warning
  // In fact, on other platforms (such as STM32) there is no need to do this pointer magic as program memory may be read in a usual way
  // So expression may be simplified
  return m_gfxFont->bitmap;
#endif //__AVR__
}

/* display SPI send command wrapper */
inline static void utftCmd (uint8_t vh)
{
  *(portOutputRegister(digitalPinToPort(M_TFT_RS))) &= ~digitalPinToBitMask(M_TFT_RS);
  SPI.transfer(vh);
}

/* display SPI send data wrapper */
inline static void utftData (uint8_t vh)
{
  *(portOutputRegister(digitalPinToPort(M_TFT_RS))) |= digitalPinToBitMask(M_TFT_RS);
  SPI.transfer(vh);
}

/* display SPI send position info wrapper */
static void utftAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
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

/* fill a rectangle on the display - used for lines, filled rectangles, etc */
static void quickFill (int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
{
  uint32_t ncount = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);
  uint16_t k = 0;

  digitalWrite(M_TFT_CS, LOW);  // screen controller chip select
  utftCmd(0x02c);  // write_memory_start
  utftAddress(x1, y1, x2, y2);  // set position on screen
  *(portOutputRegister(digitalPinToPort(M_TFT_RS))) |=  digitalPinToBitMask(M_TFT_RS);

  while (ncount)
  {
    k = 0;

    for (uint16_t i = 0; i < M_MAX_V_BUFF / 2; i++)
    {
      m_vBuff[k++] = color >> 8;
      m_vBuff[k++] = color & 0xff;
    }

    if (ncount > M_MAX_V_BUFF / 2)
    {
      SPI.transfer(m_vBuff, M_MAX_V_BUFF);
      ncount -= M_MAX_V_BUFF / 2;
    }
    else
    {
      SPI.transfer(m_vBuff, (int16_t)ncount * 2);
      ncount = 0;
    }

    // checkCAT();  // <<<--- ugh ugh ugh
  }

  checkCAT();

  digitalWrite(M_TFT_CS, HIGH);
}

/* draw horizontal line */
void drawHLine (uint16_t x, uint16_t y, uint16_t l, uint16_t color)
{
  quickFill(x, y, x + l, y, color);
}

/* draw vertical line */
static void drawVLine (uint16_t x, uint16_t y, uint16_t l, uint16_t color)
{
  quickFill(x, y, x, y + l, color);
}

/* fill whole display with specified color, overwriting all contents */
void displayClear (uint16_t color)
{
  quickFill(0, 0, 319, 239, color);
}

/* draw rectangle on screen - NO fill */
void drawRectNoFill (uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t hicolor, uint16_t lowcolor)
{
  // make lowcolor hicolor if missing
  if (lowcolor == 0)
    lowcolor = hicolor;

  drawHLine(x + 1, y, w - 2, hicolor);  // top line
  drawHLine(x + 1, y + h, w - 2, lowcolor);  // bottom line
  drawVLine(x, y + 1, h - 2, hicolor);  // left line
  drawVLine(x + w, y + 1, h - 2, lowcolor);  // right line
}

/* draw rectangle on screen - WITH fill */
void drawRectFilled (uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  quickFill(x, y, x + w, y + h, color);
}

/* initialize touch controller */
static void touchControllerInit ()
{
  pinMode(M_CS_PIN, OUTPUT);  // set pin mode for M_CS_PIN to output
  digitalWrite(M_CS_PIN, HIGH);  // set M_CS_PIN to high
}

/* initialize the display */
void displayInit ()
{
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV4);  // 4 MHz (half speed)
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);

  m_gfxFont = &ubitxFont;
  pinMode(M_TFT_CS, OUTPUT);
  pinMode(M_TFT_RS, OUTPUT);

  digitalWrite(M_TFT_CS, LOW);  // screen controller chip select

  utftCmd(0xCB);    // power control A
  utftData(0x39);
  utftData(0x2C);
  utftData(0x00);
  utftData(0x34);
  utftData(0x02);

  utftCmd(0xCF);    // power control B
  utftData(0x00);
  utftData(0XC1);
  utftData(0X30);

  utftCmd(0xE8);    // driver timing control A
  utftData(0x85);
  utftData(0x00);
  utftData(0x78);

  utftCmd(0xEA);    // driver timing control B
  utftData(0x00);
  utftData(0x00);

  utftCmd(0xED);    // power on sequence
  utftData(0x64);
  utftData(0x03);
  utftData(0X12);
  utftData(0X81);

  utftCmd(0xF7);    // charge pump ratio control
  utftData(0x20);

  utftCmd(0xC0);    // power control 1
  utftData(0x23);   // VRH[5:0] - 4.60 V

  utftCmd(0xC1);    // power control 2
  utftData(0x10);   // SAP[2:0];BT[3:0] - 3.65 V

  utftCmd(0xC5);    // VCM control
  utftData(0x3e);   // Contrast - 4.250
  utftData(0x28);   // 3.700

  utftCmd(0xC7);    // VCM control2
  utftData(0x86);   // VMH + 6

  utftCmd(0x36);    // Memory Access Control
  utftData(0x28);   // Make this horizontal display

  utftCmd(0x3A);    // pixel format set
  utftData(0x55);

  utftCmd(0xB1);    // frame rate control
  utftData(0x00);
  utftData(0x18);

  utftCmd(0xB6);    // display function control
  utftData(0x08);
  utftData(0x82);
  utftData(0x27);

  utftCmd(0x11);    // exit sleep

  delay(120);

  utftCmd(0x29);    // display on

  utftCmd(0x2c);    // memory write

  digitalWrite(M_TFT_CS, HIGH);

  // init the touch screen controller
  touchControllerInit();

  readTouchCalibration();
}

/*
  Draw a single character
  - - -
    x     Bottom left corner x coordinate
    y     Bottom left corner y coordinate
    c     The 8-bit font-indexed character (likely ascii)
    color 16-bit 5-6-5 Color to draw chraracter with
    bg    16-bit 5-6-5 Color to fill background with (if same as color, no background)
*/
void displayChar (int16_t x, int16_t y, uint8_t c, uint16_t color, uint16_t bg)
{
  c -= (uint8_t)pgm_read_byte(&m_gfxFont->first);

  GFXglyph * glyph  = pgmReadGlyphPtr(m_gfxFont, c);
  uint8_t * bitmap = pgmReadBitmapPtr(m_gfxFont);

  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
  uint8_t w  = pgm_read_byte(&glyph->width);
  uint8_t h  = pgm_read_byte(&glyph->height);
  int8_t xo = pgm_read_byte(&glyph->xOffset);
  int8_t yo = pgm_read_byte(&glyph->yOffset);

  uint8_t xx;
  uint8_t yy;

  uint8_t bits = 0;
  uint8_t bit = 0;

  int16_t k;

  digitalWrite(M_TFT_CS, LOW);  // screen controller chip select

  for (yy = 0; yy < h; yy++)
  {
    k = 0;

    for (xx = 0; xx < w; xx++)
    {
      if (!(bit++ & 7))
        bits = pgm_read_byte(&bitmap[bo++]);

      if (bits & 0x80)
      {
        m_vBuff[k++] = color >> 8;
        m_vBuff[k++] = color & 0xff;
      }
      else
      {
        m_vBuff[k++] = bg >> 8;
        m_vBuff[k++] = bg & 0xff;
      }

      bits <<= 1;
    }

    // set position on display
    utftAddress(x + xo, y + yo + yy, x + xo + w, y + yo + yy);
    *(portOutputRegister(digitalPinToPort(M_TFT_RS))) |= digitalPinToBitMask(M_TFT_RS);
    SPI.transfer(m_vBuff, k);
    // checkCAT();  // <<<---
  }

  checkCAT();
}

/* get a text string's extents */
static int16_t getTextExtent (const char * text)
{
  int16_t ext = 0;

  while (*text)
  {
    char c = *text++;

    uint8_t first = pgm_read_byte(&m_gfxFont->first);

    if ((c >= first) && (c <= (uint8_t)pgm_read_byte(&m_gfxFont->last)))
    {
      GFXglyph * glyph  = pgmReadGlyphPtr(m_gfxFont, c - first);
      ext += (uint8_t)pgm_read_byte(&glyph->xAdvance);
    }
  }  // end of the while loop of the characters to be printed

  return ext;
}

/* display a text string with fg and bg color only - NO rectangles, NO borders */
void drawRawText (const char * text, int16_t x1, int16_t y1, uint16_t color, uint16_t background)
{
  while (*text)
  {
    char c = *text++;

    uint8_t first = pgm_read_byte(&m_gfxFont->first);

    if ((c >= first) && (c <= (uint8_t)pgm_read_byte(&m_gfxFont->last)))
    {
      GFXglyph * glyph  = pgmReadGlyphPtr(m_gfxFont, c - first);
      uint8_t w = pgm_read_byte(&glyph->width);
      uint8_t h = pgm_read_byte(&glyph->height);

      if ((w > 0) && (h > 0))  // is there an associated bitmap?
        displayChar(x1, y1 + G_TEXT_LINE_HEIGHT, c, color, background);

      x1 += (uint8_t)pgm_read_byte(&glyph->xAdvance);
    }
  }  // end of the character printing while loop

  checkCAT();
}

/*
  display a text string with fg, bg, upper and lower border colors, including filled rect where text is displayed
  not specifying borderlow will make it the same color as upperborder
*/
void drawTextWithRectFilled (const char * text, int16_t x1, int16_t y1, int16_t w, int16_t h, uint16_t color,
  uint16_t background, uint16_t upperborder, uint16_t lowerborder)
{
  // default value for lowerborder is 0 - set in forward declaration in nano_gui.h
  if (lowerborder == 0)
    lowerborder = upperborder;

  drawRectFilled(x1, y1, w , h, background);  // fill in area where text will be
  drawRectNoFill(x1 - 1, y1 - 1, w + 1, h + 1, upperborder, lowerborder);  // draw border

  x1 += (w - getTextExtent(text)) / 2;
  y1 += (h - G_TEXT_LINE_HEIGHT) / 2;

  while (*text)
  {
    char c = *text++;

    uint8_t first = pgm_read_byte(&m_gfxFont->first);

    if ((c >= first) && (c <= (uint8_t)pgm_read_byte(&m_gfxFont->last)))
    {
      GFXglyph * glyph  = pgmReadGlyphPtr(m_gfxFont, c - first);

      uint8_t ww = pgm_read_byte(&glyph->width);
      uint8_t hh = pgm_read_byte(&glyph->height);

      if ((ww > 0) && (hh > 0))  // is there an associated bitmap?
        displayChar(x1, y1 + G_TEXT_LINE_HEIGHT, c, color, background);

      x1 += (uint8_t)pgm_read_byte(&glyph->xAdvance);
    }
  }  // end of the character printing while loop

  checkCAT();  // <<<---
}

/* do touch controller calibration - tapping stylus / fingernail on-screen crosses */
void doTouchCalibration ()
{
  int16_t x1;
  int16_t y1;

  int16_t x2;
  int16_t y2;

  int16_t x3;
  int16_t y3;

  int16_t x4;
  int16_t y4;

  displayClear(G_DISPLAY_BLACK);
  drawTextWithRectFilled("Click on the cross", 20, 100, 200, 50, G_DISPLAY_WHITE, G_DISPLAY_BLACK, G_DISPLAY_BLACK);

  // top-left
  drawHLine(10, 20, 20, G_DISPLAY_WHITE);
  drawVLine(20, 10, 20, G_DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  while (readTouch())
    delay(100);

  x1 = g_tsPoint.x;
  y1 = g_tsPoint.y;

  // clear previous cross
  drawHLine(10, 20, 20, G_DISPLAY_BLACK);
  drawVLine(20, 10, 20, G_DISPLAY_BLACK);

  delay(1000);

  // top-right
  drawHLine(290, 20, 20, G_DISPLAY_WHITE);
  drawVLine(300, 10, 20, G_DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  while (readTouch())
    delay(100);

  x2 = g_tsPoint.x;
  y2 = g_tsPoint.y;

  // clear previous cross
  drawHLine(290, 20, 20, G_DISPLAY_BLACK);
  drawVLine(300, 10, 20, G_DISPLAY_BLACK);

  delay(1000);

  // bottom-left
  drawHLine(10, 220, 20, G_DISPLAY_WHITE);
  drawVLine(20, 210, 20, G_DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  x3 = g_tsPoint.x;
  y3 = g_tsPoint.y;

  while (readTouch())
    delay(100);

  // clear previous cross
  drawHLine(10, 220, 20, G_DISPLAY_BLACK);
  drawVLine(20, 210, 20, G_DISPLAY_BLACK);

  delay(1000);

  // bottom-right
  drawHLine(290, 220, 20, G_DISPLAY_WHITE);
  drawVLine(300, 210, 20, G_DISPLAY_WHITE);

  while (!readTouch())
    delay(100);

  x4 = g_tsPoint.x;
  y4 = g_tsPoint.y;

  // clear previous cross
  drawHLine(290, 220, 20, G_DISPLAY_BLACK);
  drawVLine(300, 210, 20, G_DISPLAY_BLACK);

  // we average two readings and divide them by half and store them as scaled integers 10 times their actual, fractional value
  // the x points are located at 20 and 300 on x axis, hence, the delta x is 280, we take 28 instead, to preserve fractional value,
  // there are two readings (x1, x2) and (x3, x4). Hence, we have to divide by 28 * 2 = 56
  m_slopeX = ((x4 - x3) + (x2 - x1)) / 56;
  // the y points are located at 20 and 220 on the y axis, hence, the delta is 200. we take it as 20 instead, to preserve the fraction value
  // there are two readings (y1, y2) and (y3, y4). Hence we have to divide by 20 * 2 = 40
  m_slopeY = ((y3 - y1) + (y4 - y2)) / 40;

  // x1, y1 is at 20 pixels
  m_offsetX = x1 + -((20 * m_slopeX) / 10);
  m_offsetY = y1 + -((20 * m_slopeY) / 10);

  // store in eeprom
  writeTouchCalibration();

  // erase all teh thingz
  displayClear(G_DISPLAY_BLACK);
}
