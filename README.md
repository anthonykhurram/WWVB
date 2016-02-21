## Arduino Nano with Nokia LCD 60Khz WWVB Transmitter for setting radio controlled 'Atomic Clocks' like Oregon Scientific, Baldr, Casio Wave Ceptor, Citizen etc. 
An Arduino based WWVB transmitter project. The point of this is to get sub second time accuracy on radio controlled clocks in countries that don't support it.

## Tested successfully on a Baldr brand WWVB clock so far.
https://www.youtube.com/watch?v=lA-yf535rdY :Demo video of a radio controlled clock setting its time from the project.

## Not tested successfully on other devices as yet.

## Basic system info
The Nano uses the serial UTC time strings from the GPS module to synchronize a low power WWVB time signal transmission (range approx 1 meter).
The arduino code converts the UTC time to local time for the Nokia LCD, and also converts UTC to different offset for the WWVB generation.
The reason for that is the WWVB clock may not be built to be used in the time zone you are in (like Australia).
So a clock made for the USA, Japan or Europe can be set to display correct for any other time zone automatically (I have a Baldr desk clock made for the USA, and use it in Australia). 
The Nokia LCD shows current local time as read from the serial GPS module. The lower half of the LCD shows the WWVB and GPS statuses.
The WWVB time information will be output for 6 minutes before the Nano will re-sync with the GPS time and repeat. 
Radio Controlled clocks periodically try to receive a WWVB signal during the night and update the time. This project should enable clocks and watches within a meter or so to do this also.
The anticipated usage would be to have this on a bedside table and your radio controlled wrist watch and alarm clock will update each night to sub second accurate time (just like it would in countries like the USA, Japan, Europe, China).

Compile time options are:
GPS module type
Local time zone offset
WWVB time zone offset
Nokia LCD contrast value

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
* The GPS is direct connected to the Nano, the LCD also. Use a 1K to 500 ohm for the LCD back light limiting. The coil is driven directly through a limiting resistor or through an LED (the real current limiting is by the AVRs internal I/O limiter).
* Also to do, add LCD backlight control via the Nano. Use two digital pins via diodes and two resistors to have 'day & night' brightness settings, set by real time. 
