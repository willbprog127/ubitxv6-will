/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

#include "ubitx.h"

/* *************  SI5315 routines - thanks Jerry Gaffke, KE7ER   ***********************
   An minimalist standalone set of Si5351 routines.
   VCOA is fixed at 875 MHz, VCOB not used.
   The output msynth dividers are used to generate 3 independent clocks
   with 1 hz resolution to any frequency between 4 khz and 109 MHz.

   Usage:
   Call si5351bxInit() once at startup with no args;
   Call si5351bxSetFreq(clknum, freq) each time one of the
   three output CLK pins is to be updated to a new frequency.
   A freq of 0 serves to shut down that output clock.

   The global variable si5351bxVCOA starts out equal to the nominal VCOA
   frequency of 25 MHz * 35 = 875,000,000 Hz.  To correct for 25 MHz crystal errors,
   the user can adjust this value.  The vco frequency will not change but
   the number used for the (a + b / c) output msynth calculations is affected.
   Example:  We call for a 5 MHz signal, but it measures to be 5.001 MHz.
   So the actual vcoa frequency is 875 MHz * 5.001 / 5.000 = 875,175,000 Hz,
   To correct for this error:  si5351bxVCOA = 875,175,000;

   Most users will never need to generate clocks below 500 KHz.
   But it is possible to do so by loading a value between 0 and 7 into
   the global variable si5351bxRDiv, be sure to return it to a value of 0
   before setting some other CLK output pin.  The affected clock will be
   divided down by a power of two defined by 2 ** si5351bxRDiv
   A value of zero gives a divide factor of 1, a value of 7 divides by 128.
   This lightweight method is a reasonable compromise for a seldom used feature.
*/

#define BB0(x) ((uint8_t)x)             // break int32 into bytes
#define BB1(x) ((uint8_t)(x >> 8))
#define BB2(x) ((uint8_t)(x >> 16))

#define SI5351BX_ADDR   0x60            // I2C address of Si5351 (typical)
#define SI5351BX_XTALPF 2               // 1 = 6 pf,  2 = 8pf,  3 = 10pf

/* if using 27 MHz crystal, set _XTAL = 27000000, _MSA = 33.  Then vco = 891 MHz */
#define SI5351BX_XTAL 25000000          // crystal freq in Hz
#define SI5351BX_MSA  35                // VCOA is at 25 MHz * 35 = 875 MHz

extern int32_t calibration;

/* customization variables */
uint32_t si5351bxVCOA = (SI5351BX_XTAL * SI5351BX_MSA);  // 25 MHz crystal calibrate
uint8_t  si5351bxRDiv = 0;             // 0 - 7, CLK pin sees fout / (2 ** rdiv)
uint8_t  si5351bxDrive[3] = {3, 3, 3}; // 0 = 2 ma, 1 = 4 ma, 2 = 6 ma, 3 = 8 ma, for CLK 0, 1, 2
uint8_t  si5351bxClkEnable = 0xFF;         // all CLK output drivers off


/* write single value to si5351 reg via I2C */
void i2cWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(SI5351BX_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}


/* write array to si5351 reg via I2C */
void i2cWriten(uint8_t reg, uint8_t * vals, uint8_t vcnt) {
  Wire.beginTransmission(SI5351BX_ADDR);
  Wire.write(reg);

  while (vcnt--)
    Wire.write(*vals++);

  Wire.endTransmission();
}


/*
    initialize si5351
    call once at power-up, start PLLA
*/
void si5351bxInit() {

  uint32_t msxp1;

  Wire.begin();

  i2cWrite(149, 0);                     // spreadSpectrum off
  i2cWrite(3, si5351bxClkEnable);       // disable all CLK output drivers
  i2cWrite(183, SI5351BX_XTALPF << 6);  // set 25 MHz crystal load capacitance
  msxp1 = 128 * SI5351BX_MSA - 512;     // and msxp2 = 0, msxp3 = 1, not fractional

  uint8_t vals[8] = {0, 1, BB2(msxp1), BB1(msxp1), BB0(msxp1), 0, 0, 0};

  i2cWriten(26, vals, 8);               // Write to 8 PLLA msynth regs
  i2cWrite(177, 0x20);                  // Reset PLLA  (0x80 resets PLLB)

  // initializing the ppl2 as well
  i2cWriten(34, vals, 8);               // Write to 8 PLLA msynth regs
  i2cWrite(177, 0xa0);                  // Reset PLLA  & PPLB (0x80 resets PLLB)
  //  <<<--- do we need Wire.endTransmission here???
}


/* set a CLK to fout Hz */
void si5351bxSetFreq(uint8_t clknum, uint32_t fout) {

  uint32_t msa;
  uint32_t msb;
  uint32_t msc;
  uint32_t msxp1;
  uint32_t msxp2;
  uint32_t msxp3p2top;

  if ((fout < 500000) || (fout > 109000000)) // if clock freq out of range
    si5351bxClkEnable |= 1 << clknum;      //  shut down the clock
  else {
    msa = si5351bxVCOA / fout;   // Integer part of vco/fout
    msb = si5351bxVCOA % fout;   // Fractional part of vco/fout
    msc = fout;                   // Divide by 2 till fits in reg

    while (msc & 0xfff00000) {
      msb = msb >> 1;
      msc = msc >> 1;
    }

    msxp1 = (128 * msa + 128 * msb / msc - 512) | (((uint32_t)si5351bxRDiv) << 20);
    msxp2 = 128 * msb - 128 * msb / msc * msc; // msxp3 == msc;
    msxp3p2top = (((msc & 0x0F0000) << 4) | msxp2);     // 2 top nibbles
    uint8_t vals[8] = {BB1(msc), BB0(msc), BB2(msxp1), BB1(msxp1),
                        BB0(msxp1), BB2(msxp3p2top), BB1(msxp2), BB0(msxp2)};
    i2cWriten(42 + (clknum * 8), vals, 8); // Write to 8 msynth regs

    i2cWrite(16 + clknum, 0x0C | si5351bxDrive[clknum]); // use local msynth

    si5351bxClkEnable &= ~(1 << clknum);   // Clear bit to enable clock
  }

  i2cWrite(3, si5351bxClkEnable);        // Enable / disable clock
}


/* apply the calibration correction factor */
void si5351SetCalibration(int32_t cal) {
  si5351bxVCOA = (SI5351BX_XTAL * SI5351BX_MSA) + cal;
  si5351bxSetFreq(0, usbCarrier);
}


/* initialize the SI5351 */
void initOscillators() {
  si5351bxInit();
  si5351bxVCOA = (SI5351BX_XTAL * SI5351BX_MSA) + calibration; // apply calibration correction factor
  si5351bxSetFreq(0, usbCarrier);
}
