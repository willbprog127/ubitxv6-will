/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include "ubitx.h"
#include "nano_gui.h"

/*
  The CAT protocol is used by many radios to provide remote control to computers through
  the serial port.

  This is very much a work in progress. Parts of this code have been liberally
  borrowed from other GPL works like hamlib.

  WARNING: This is an unstable version. While it has worked with fldigi,
  it gives time out error with WSJTX 1.8.0
*/

/* global variables */
// bool g_doingCAT = false;  // (debug only)

/* file-level constants */

/* for broken protocol */
static constexpr uint16_t M_CAT_RECEIVE_TIMEOUT = 500;

static constexpr uint8_t M_CAT_MODE_LSB = 0x00;
static constexpr uint8_t M_CAT_MODE_USB = 0x01;
//static constexpr uint8_t M_CAT_MODE_CW  = 0x02;  // unused, but keep
//static constexpr uint8_t M_CAT_MODE_CWR = 0x03;  // unused, but keep
//static constexpr uint8_t M_CAT_MODE_AM  = 0x04;  // unused, but keep
//static constexpr uint8_t M_CAT_MODE_FM  = 0x08;  // unused, but keep
//static constexpr uint8_t M_CAT_MODE_DIG = 0x0A;  // unused, but keep
//static constexpr uint8_t M_CAT_MODE_PKT = 0x0C;  // unused, but keep
//static constexpr uint8_t M_CAT_MODE_FMN = 0x88;  // unused, but keep

/* file-level variables */
static uint32_t m_rxBufferArriveTime = 0;
static uint8_t m_rxBufferCheckCount = 0;
static uint8_t m_cat[5];
static bool m_insideCat = false;

/* set high nibble */
static uint8_t setHighNibble (uint8_t b, uint8_t v)
{
  // Clear the high nibble
  b &= 0x0f;
  // Set the high nibble
  return b | ((v & 0x0f) << 4);
}

/* set low nibble */
static uint8_t setLowNibble (uint8_t b, uint8_t v)
{
  // Clear the low nibble
  b &= 0xf0;
  // Set the low nibble
  return b | (v & 0x0f);
}

/* get high nibble */
static uint8_t getHighNibble (uint8_t b)
{
  return (b >> 4) & 0x0f;
}

/* get low nibble */
static uint8_t getLowNibble (uint8_t b)
{
  return b & 0x0f;
}

/*
  Takes a number and produces the requested number of decimal digits, starting
  from the least significant digit
*/
static void getDecimalDigits (uint32_t number, uint8_t * result, int16_t digits)
{
  for (int16_t i = 0; i < digits; i++)
  {
    // "Mask off" (in a decimal sense) the LSD and return it
    result[i] = number % 10;
    // "Shift right" (in a decimal sense)
    number /= 10;
  }
}

/* Takes a frequency and writes it into the CAT command buffer in BCD form. */
static void writeFreq (uint32_t freq, uint8_t * cmd)
{
  // Convert the frequency to a set of decimal digits. We are taking 9 digits
  // so that we can get up to 999 MHz. But the protocol doesn't care about the
  // LSD (1's place), so we ignore that digit.
  uint8_t digits[9];

  getDecimalDigits(freq, digits, 9);

  // Start from the LSB and get each nibble
  cmd[3] = setLowNibble(cmd[3], digits[1]);
  cmd[3] = setHighNibble(cmd[3], digits[2]);
  cmd[2] = setLowNibble(cmd[2], digits[3]);
  cmd[2] = setHighNibble(cmd[2], digits[4]);
  cmd[1] = setLowNibble(cmd[1], digits[5]);
  cmd[1] = setHighNibble(cmd[1], digits[6]);
  cmd[0] = setLowNibble(cmd[0], digits[7]);
  cmd[0] = setHighNibble(cmd[0], digits[8]);
}

/* This function takes a frequency that is encoded using 4 bytes of BCD
  representation and turns it into an long measured in Hz.

  [12][34][56][78] = 123.45678? MHz
*/
static uint32_t readFreq (uint8_t * cmd)
{
  // Pull off each of the digits
  uint8_t d7 = getHighNibble(cmd[0]);
  uint8_t d6 = getLowNibble(cmd[0]);
  uint8_t d5 = getHighNibble(cmd[1]);
  uint8_t d4 = getLowNibble(cmd[1]);
  uint8_t d3 = getHighNibble(cmd[2]);
  uint8_t d2 = getLowNibble(cmd[2]);
  uint8_t d1 = getHighNibble(cmd[3]);
  uint8_t d0 = getLowNibble(cmd[3]);
  return
    (uint32_t)d7 * 100000000L +
    (uint32_t)d6 * 10000000L +
    (uint32_t)d5 * 1000000L +
    (uint32_t)d4 * 100000L +
    (uint32_t)d3 * 10000L +
    (uint32_t)d2 * 1000L +
    (uint32_t)d1 * 100L +
    (uint32_t)d0 * 10L;
}

/* catReadEEPRom */
static void catReadEEPRom ()
{
  // for remove warnings
  uint8_t temp0 = m_cat[0];
  uint8_t temp1 = m_cat[1];

  m_cat[0] = 0;
  m_cat[1] = 0;

  switch (temp1)
  {
    case 0x45:
      if (temp0 == 0x03)
      {
        m_cat[0] = 0x00;
        m_cat[1] = 0xD0;
      }
      break;

    case 0x47:
      if (temp0 == 0x03)
      {
        m_cat[0] = 0xDC;
        m_cat[1] = 0xE0;
      }
      break;

    case 0x55:
      // 0 : VFO A/B  0 = VFO-A, 1 = VFO-B
      // 1 : MTQMB Select  0 = (Not MTQMB), 1 = MTQMB ("Memory Tune Quick Memory Bank")
      // 2 : QMB Select  0 = (Not QMB), 1 = QMB  ("Quick Memory Bank")
      // 3 :
      // 4 : Home Select  0 = (Not HOME), 1 = HOME memory
      // 5 : Memory/MTUNE select  0 = Memory, 1 = MTUNE
      // 6 :
      // 7 : MEM/VFO Select  0 = Memory, 1 = VFO (A or B - see bit 0)
      m_cat[0] = 0x80 + (g_vfoActive == VFO_B ? 1 : 0);
      m_cat[1] = 0x00;
      break;

    case 0x57:
      // 0 : 1-0  AGC Mode  00 = Auto, 01 = Fast, 10 = Slow, 11 = Off
      // 2  DSP On/Off  0 = Off, 1 = On  (Display format)
      // 4  PBT On/Off  0 = Off, 1 = On  (Passband Tuning)
      // 5  NB On/Off 0 = Off, 1 = On  (Noise Blanker)
      // 6  Lock On/Off 0 = Off, 1 = On  (Dial Lock)
      // 7  FST (Fast Tuning) On/Off  0 = Off, 1 = On  (Fast tuning)

      m_cat[0] = 0xC0;
      m_cat[1] = 0x40;
      break;

    // band select VFO A Band Select  0000 = 160 M, 0001 = 75 M, 0010 = 40 M,
    // 0011 = 30 M, 0100 = 20 M, 0101 = 17 M, 0110 = 15 M, 0111 = 12 M, 1000 = 10 M,
    // 1001 = 6 M, 1010 = FM BCB, 1011 = Air, 1100 = 2 M, 1101 = UHF, 1110 = (Phantom)
    case 0x59:
      // https://www.ka7oei.com/ft817_memmap.html
      // CAT_BUFF[0] = 0xC2;
      // CAT_BUFF[1] = 0x82;
      break;

    // Beep Volume (0-100) (#13)
    case 0x5C:
      m_cat[0] = 0xB2;
      m_cat[1] = 0x42;
      break;

    case 0x5E:
      // 3-0 : CW Pitch (300-1000 Hz) (#20)  From 0 to E (HEX) with 0 = 300 Hz and each step representing 50 Hz
      // 5-4 :  Lock Mode (#32) 00 = Dial, 01 = Freq, 10 = Panel
      // 7-6 :  Op Filter (#38) 00 = Off, 01 = SSB, 10 = CW
      // CAT_BUFF[0] = 0x08;
      m_cat[0] = (g_sideTone - 300) / 50;
      m_cat[1] = 0x25;
      break;

    // Sidetone (Volume) (#44)
    case 0x61 :
      m_cat[0] = g_sideTone % 50;
      m_cat[1] = 0x08;
      break;

    case  0x5F:
      // 4-0  CW Weight (1.:2.5-1:4.5) (#22)  From 0 to 14 (HEX) with 0 = 1:2.5, incrementing in 0.1 weight steps
      // 5  420 ARS (#2)  0 = Off, 1 = On
      // 6  144 ARS (#1)  0 = Off, 1 = On
      // 7  Sql/RF-G (#45)  0 = Off, 1 = On
      m_cat[0] = 0x32;
      m_cat[1] = 0x08;
      break;

    // CW Delay (10-2500 ms) (#17)  From 1 to 250 (decimal) with each step representing 10 ms
    case 0x60:
      m_cat[0] = g_cwDelayTime;
      m_cat[1] = 0x32;
      break;

    case 0x62:
      // 5-0  CW Speed (4-60 WPM) (#21) From 0 to 38 (HEX) with 0 = 4 WPM and 38 = 60 WPM (1 WPM steps)
      // 7-6  Batt-Chg (6/8/10 Hours (#11)  00 = 6 Hours, 01 = 8 Hours, 10 = 10 Hours
      // CAT_BUFF[0] = 0x08;
      m_cat[0] = 1200 / g_cwSpeed - 4;
      m_cat[1] = 0xB2;
      break;

    case 0x63:
      // 6-0  VOX Gain (#51)  Contains 1-100 (decimal) as displayed
      // 7  Disable AM/FM Dial (#4) 0 = Enable, 1 = Disable
      m_cat[0] = 0xB2;
      m_cat[1] = 0xA5;
      break;

    case 0x64:
      break;

    // 6-0  SSB Mic (#46) Contains 0-100 (decimal) as displayed
    case 0x67:
      m_cat[0] = 0xB2;
      m_cat[1] = 0xB2;
    break;      case 0x69 : //FM Mic (#29)  Contains 0-100 (decimal) as displayed

    case 0x78 :
      if (g_isUSB)
        m_cat[0] = M_CAT_MODE_USB;
      else
        m_cat[0] = M_CAT_MODE_LSB;

      if (m_cat[0] != 0) m_cat[0] = 1 << 5;
      break;

    case 0x79:
      // 1-0  TX Power (All bands)  00 = High, 01 = L3, 10 = L2, 11 = L1
      // 3  PRI On/Off  0 = Off, 1 = On
      // DW On/Off  0 = Off, 1 = On
      // SCN (Scan) Mode  00 = No scan, 10 = Scan up, 11 = Scan down
      // ART On/Off  0 = Off, 1 = On
      m_cat[0] = 0x00;
      m_cat[1] = 0x00;
      break;

    // SPLIT
    case 0x7A:
      // 7A  0 HF Antenna Select 0 = Front, 1 = Rear
      // 7A  1 6 M Antenna Select  0 = Front, 1 = Rear
      // 7A  2 FM BCB Antenna Select 0 = Front, 1 = Rear
      // 7A  3 Air Antenna Select  0 = Front, 1 = Rear
      // 7A  4 2 M Antenna Select  0 = Front, 1 = Rear
      // 7A  5 UHF Antenna Select  0 = Front, 1 = Rear
      // 7A  6 ? ?
      // 7A  7 SPL On/Off  0 = Off, 1 = On

      m_cat[0] = (g_splitOn ? 0xFF : 0x7F);
      break;

    case 0xB3:
      m_cat[0] = 0x00;
      m_cat[1] = 0x4D;
      break;
  }

  // send the data
  Serial.write(m_cat, 2);
}

/* */
static void processCATCommand2 (uint8_t * cmd)
{
  uint8_t response[5];
  uint32_t f;

  switch (cmd[4])
  {
    /*  case 0x00:
        response[0]=0;
        Serial.write(response, 1);
        break;
    */
    case 0x01:
      // set frequency
      f = readFreq(cmd);
      setFrequency(f);
      displayVFO(g_vfoActive);
      response[0] = 0;
      Serial.write(response, 1);
      // sprintf(g_buffB, "set:%ld", f);
      // printLine2(g_buffB);
      break;

    case 0x02:
      // split on
      g_splitOn = true;  //1;
      break;

    case 0x82:
      // split off
      g_splitOn = false;  //0;
      break;

    case 0x03:
      writeFreq(g_frequency, response);  // Put the frequency into the buffer

      if (g_isUSB)
        response[4] = 0x01;  // USB
      else
        response[4] = 0x00;  // LSB
      Serial.write(response, 5);
      //printLine2("cat:getfreq");
      break;

    // set mode
    case 0x07:
      if (cmd[0] == 0x00 || cmd[0] == 0x03)
        g_isUSB = false;
      else
        g_isUSB = true;

      response[0] = 0x00;
      Serial.write(response, 1);
      setFrequency(g_frequency);
      // printLine2("cat: mode changed");
      // updateDisplay();
      break;

    // PTT On
    case 0x08:
      if (!g_inTx)
      {
        response[0] = 0;
        g_txCAT = true;
        startTx(TX_SSB);
        displayVFO(g_vfoActive);
      }
      else
        response[0] = 0xf0;

      Serial.write(response, 1);
      displayVFO(g_vfoActive);
      break;

    // PTT Off
    case 0x88:
      if (g_inTx)
      {
        stopTx();
        g_txCAT = false;
      }

      response[0] = 0;
      Serial.write(response, 1);
      displayVFO(g_vfoActive);
      break;

    // toggle the VFOs
    case 0x81:
      response[0] = 0;

      if (g_vfoActive == VFO_A)
        switchVFO(VFO_B);
      else
        switchVFO(VFO_A);

      Serial.write(response, 1);
      // updateDisplay();  // <<<---
      displayVFO(g_vfoActive);
      break;

    // Read FT-817 EEPROM Data  (for comfirtable)  // <<--- What does 'confirtable' mean?
    case 0xBB:
      catReadEEPRom();
      break;

    case 0xe7 :
      // get receiver status, we have hardcoded this as
      // as we dont' support ctcss, etc.
      response[0] = 0x09;
      Serial.write(response, 1);
      break;

    case 0xf7:
      {
        boolean isHighSWR = false;
        boolean isSplitOn = false;

        /*
          Inverted -> *ptt = ((p->tx_status & 0x80) == 0); <-- souce code in ft817.c (hamlib)
        */
        response[0] = ((g_inTx ? 0 : 1) << 7) +
                      ((isHighSWR ? 1 : 0) << 6) +  // hi swr off / on
                      ((isSplitOn ? 1 : 0) << 5) + // Split on / off
                      (0 << 4) +  // dummy data
                      0x08;  // P0 meter data

        Serial.write(response, 1);
      }
      break;

    default:
      // somehow, get this to print the four bytes
      ultoa(*((uint32_t *)cmd), g_buffC, HEX);  //16);

      response[0] = 0x00;
      Serial.write(response[0]);
  }

  m_insideCat = false;
}

/* check for cat commands / data */
void checkCAT()
{
  uint8_t i;

  // Check Serial Port Buffer
  if (Serial.available() == 0)
  {
    // Set Buffer Clear status
    m_rxBufferCheckCount = 0;
    return;
  }
  else if (Serial.available() < 5)
  {
    // First Arrived
    if (m_rxBufferCheckCount == 0)
    {
      m_rxBufferCheckCount = Serial.available();
      m_rxBufferArriveTime = millis() + M_CAT_RECEIVE_TIMEOUT;  // Set time for timeout
    }
    else if (m_rxBufferArriveTime < millis())
    {
      // Clear Buffer
      for (i = 0; i < Serial.available(); i++)
        m_rxBufferCheckCount = Serial.read();
      m_rxBufferCheckCount = 0;
    }
    else if (m_rxBufferCheckCount < Serial.available())
    {
      // Increase buffer count, slow arrive
      m_rxBufferCheckCount = Serial.available();
      m_rxBufferArriveTime = millis() + M_CAT_RECEIVE_TIMEOUT;  // Set time for timeout
    }
    return;
  }

  // Arrived CAT DATA
  for (i = 0; i < 5; i++)
    m_cat[i] = Serial.read();

  // this code is not re-entrant.
  if (m_insideCat)
    return;

  m_insideCat = true;

  // This routine is enabled to debug the cat protocol
  /*
    int16_t catCount = 0;

    catCount++;

    if (m_cat[4] != 0xf7 && m_cat[4] != 0xbb && m_cat[4] != 0x03){
      sprintf(g_buffB, "%d %02x %02x%02x%02x%02x", catCount, m_cat[4],m_cat[0], m_cat[1], m_cat[2], m_cat[3]);
      printLine2(g_buffB);
    }
  */

  /*
    if (!g_doingCAT)
    {
      g_doingCAT = true;
      drawTextWithRectFilled("CAT on", 100, 120, 100, 40, G_DISPLAY_ORANGE, G_DISPLAY_BLACK, G_DISPLAY_WHITE);
    }
  */
  processCATCommand2(m_cat);

  m_insideCat = false;
}
