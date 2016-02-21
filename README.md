## Arduino Nano with Nokia LCD 60Khz WWVB Transmitter for setting radio controlled 'Atomic Clocks' like Oregon Scientific, Baldr, Casio Wave Ceptor, Citizen etc. 
An Arduino based WWVB transmitter project.

Tested successfully on a Baldr brand WWVB clock.

Not tested successfully on other devices as yet.


* Author/s: [Mark Cooke](https://www.github.com/micooke), [Martin Sniedze](https://www.github.com/mr-sneezy)

## Requirements
* [TimeDateTools.h](https://github.com/micooke/ATtinyGPS/TimeDateTools.h)
* [ATtinyGPS.h](https://github.com/micooke/ATtinyGPS/ATtinyGPS.h) : for setting time based off a serial GPS
* [wwvb.h](https://github.com/micooke/WWVB/wwvb.h) : WWVB library
* Arduino Nano or clone
* Serial GPS module like a uBlox 6M or MTK 3329 that works with 5V supply.
* Nokia 5110 display module  - https://www.sparkfun.com/products/10168

##Options
* 3D printed coil bobbin (coil holder)  - http://www.thingiverse.com/thing:1358090

## About
http://www.nist.gov/pml/div688/grp40/wwvb.cfm

WWVB: 60kHz carrier, Amplitude modulated to Vp -17dB for signal low



## TODO:
* Draw up a schematic. If you can't wait it's actually fairly easy to use the pin diagram in the INO. 
* The GPS is direct connected to the Nano, the LCD also. Use a 1K to 500 ohm for the LCD back light limiting.
* Also to do, add LCD backlight control via the Nano. Use two digital pins via diodes and two resitors to have 'day & night' brightness settings, set by real time. 
