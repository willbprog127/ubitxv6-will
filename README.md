## Current Status ##
Code **IS** compiling and working on radio


---


# uBitx v6
## uBitx v6.3.1 Arduino sketch
A fork of Ashhar Farhan's v6 sketch.  A work in progress, so be aware it may not work properly at times.  I will be customizing this to my own
needs.  Please file an Issue if you use this and have any trouble.

Most notes and important variables are mentioned in `ubitx.h`

* Bugs fixed from original
* Most unused and duplicate declarations have been removed
* `WPM` and `TON` adjustments can now be quit from the screen (instead of just the encoder button)
* Colors have been changed (feel free to fork and change per your taste)
* Different font than original (feel free to fork and change per your taste)
* Command-bar text shouldn't wipe out other buttons, text, etc now
* Code formatted to my specs (feel free to fork and change per your taste)

![ubitx - 2022-07-04 screenshot-cropped](https://user-images.githubusercontent.com/1296250/177121495-059c5d6b-81ff-4742-bf74-0be9ca47e6cb.jpg)

---

### Original README.md

IMPORTANT: It will compile only if you place this in the Arduino's own sketch directory! This is because of the restricted places that the Arduino searches for it's include files (the headers).

- This is refactored to remove dependencies on any library except the standard Arduino libraries of SPI, I2C, EEPROM, etc.
- This works with ILI9341 display controller. The pins used by the TFT display are the same as that of the 16x2 LCD display of the previous versions.
- As the files are now split into .cpp files, the nano gui, morse reader, etc. can be reused in other projects as well

This is released under GPL v3 license.
