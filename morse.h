/*
  This source file is under General Public License version 3.

  Detailed comments are available in the ubitx.h file
*/

/* sends out morse code at the speed set by cwSpeed */
extern uint16_t cwSpeed;            // this is the dot period in milliseconds
void morseText(char * text);
