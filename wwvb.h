#ifndef wwvb_h
#define wwvb_h
/*
Designed for Arduino with the ATtiny85 @ 16MHz / 5V
(but should work with 3.3V / 5V leonardo/micro or uno/nano)

WWVB: 60Khz carrier, Amplitude modulated to Vp -17dB for signal low

Hopefully your wwvb receiver is insensitive to this -17dB value,
as this library uses pulse width modulation to set a 5% duty cycle
for the 'low' signal

Copyright (c) 2015 Mark Cooke, Martin Sniedze

Author/s: Mark Cooke, Martin Sniedze
* gihtub.com/micooke
* gihtub.com/mr-sneezy

License: MIT license (see LICENSE file)
*/
/*
-----------------------------------------------------------------------------
Minimum example (see minimum.ino)
-----------------------------------------------------------------------------

#include <TimeDateTools.h> // include before wwvb.h AND/OR ATtinyGPS.h
#include <wwvb.h> // include before ATtinyGPS.h
wwvb wwvb_tx;

// The ISR sets the PWM pulse width to correspond with the WWVB bit
#if defined(USE_OC1A)
ISR(TIMER1_COMPA_vect)
#elif defined(USE_OC1B)
ISR(TIMER1_COMPB_vect)
#endif
{
	cli(); // disable interrupts
	wwvb_tx.interrupt_routine();
	sei(); // enable interrupts
}

void setup()
{
	wwvb_tx.setup();

	// Note: The default is to include DateString & TimeString tools (see wwvb.h)
	wwvb_tx.set_time(__DATE__, __TIME__);
	wwvb_tx.start(); // Thats it
}
void loop()
{
	delay(1); // Recommended
}

-----------------------------------------------------------------------------
*/

#include <Arduino.h>

// If not defined, require TimeString and DateString conversion
// (i.e. to set the clock to the compiled time)
#if !defined(REQUIRE_TIMEDATESTRING)
#define REQUIRE_TIMEDATESTRING 1
#endif

#include <TimeDateTools.h>

// Default to using OC1B as the output ( pin mapping listed under wwvb::setup() )
// This is because OC1A uses an SPI pin on the ATtiny85 - you may want to use SPI and wwvb
#if !(defined(USE_OC1A) | defined(USE_OC1B))
#define USE_OC1B
#endif

// wwvb class - note, you will need to specify the interrupt routine in your main sketch


class wwvb
{
private:
	/*
	WWVB format:
	http://www.nist.gov/pml/div688/grp40/wwvb.cfm

	A frame of data takes 60 seconds to send, and starts at the start of a minute

	A frame of data contains:
	* 6 subframes
	* 10 bits of data per subframe / 60 bits of data
	* 1 bit of data per second
	*/

	//                            0   1   2   3   4   5   6   7   8   9
	//                            M  40  20  10   0   8   4   2   1   M
	volatile boolean MINS[10] = { 1,  1,  1,  1,  0,  1,  1,  1,  1,  1 };
	//                            -   -  20  10   0   8   4   2   1   M
	volatile boolean HOUR[10] = { 0,  0,  1,  1,  0,  1,  1,  1,  1,  1 };
	//                            -   - 200 100   0  80  40  20  10   M
	volatile boolean DOTY[10] = { 0,  0,  1,  1,  0,  1,  1,  1,  1,  1 };
	//                            8   4   2   1   -   -   +   -   +   M
	volatile boolean DUT1[10] = { 1,  1,  1,  1,  0,  0,  0,  1,  0,  1 };
	//                            0.8 0.4 0.2 0.1 -  80  40  20  10   M
	volatile boolean YEAR[10] = { 1,  1,  1,  1,  0,  1,  1,  1,  1,  1 };
	//                            8   4   2   1   - LYI LSW   2   1   M
	volatile boolean MISC[10] = { 1,  1,  1,  1,  0,  1,  1,  1,  1,  1 };

	// temp variables
	volatile uint8_t t_ss, t_mm, t_hh, t_DD, t_MM, t_YY;
	// internal variables, set by set_time ONLY
	volatile uint8_t mins_, hour_; // 00-59, 00-23
	volatile uint8_t DD_, MM_, YY_; // 1-31, 1-12, 00-99
	volatile uint16_t doty_; // 1=1 Jan, 365 = 31 Dec (or 366 in a leap year)
	volatile boolean is_leap_year_ = 0;
	volatile uint8_t daylight_savings_ = 0; // 00 - no, 10 - starts today, 11 - yes, 01 - ends today 

	// LOW :   Low for 0.2s / 1.0s (20% low duty cycle)
	// HIGH:   Low for 0.5s / 1.0s
	// MARKER: Low for 0.8s / 1.0s
	// hopefully your sketch can spare the extra 6 bytes for the convenience of being able to use WWVB_LOW etc.
	uint16_t WWVB_LOW, WWVB_HIGH, WWVB_MARKER, WWVB_ENDOFBIT;
	uint16_t pulse_width[3] = { WWVB_LOW, WWVB_HIGH, WWVB_MARKER };
	
	volatile uint16_t WWVB_LOWTIME;
	uint8_t PWM_LOW, PWM_HIGH;
	
	volatile uint16_t isr_count = 0;
	volatile uint8_t frame_index, subframe_index;

	volatile boolean _is_active = false;
public:
	void setup() {

		/*
		Setup the count values that correspond to 0.2s,0.5s,0.8s for wwvb encoding [LOW,HIGH,MARKER]
		i.e.  .0 .1 .2 .3 .4 .5 .6 .7 .8 .9 1.0 (1s = END OF BIT)
			  |     |________|________|______|
		LOW   :____/          _______________\
		HIGH  :______________/         ______\
		MARKER:_______________________/      \

		Note: Pulse shape is indicative of the amplitude of the 60kHz carrier signal

		Some math to work out how much we will be off by
		(Matlab code)
		%8MHz
		Ts = 1/(8e6/(2*66));
		tics = round([100,200,500,800,1000]*1e-3/ Ts);
		fprintf('[%d,%d,%d,%d,%d] -> [%0.6f,%0.6f,%0.6f,%0.6f,%0.6f]\n',tics, tics*Ts  - [.1,.2,.5,.8,1.0]);
		% [6061,12121,30303,48485,60606] -> [0.000006,-0.000004,-0.000001,0.000002,-0.000001]
		% Note: uint16_t maximum value is 65535 - so we can store the counter in 16 bits

		Ts = 1/(16e6/(2*133)); % This is the same Ts as the ATtiny85 in Fast PWM
		tics = round([100,200,500,800,1000]*1e-3/ Ts);
		fprintf('[%d,%d,%d,%d,%d] -> [%0.6f,%0.6f,%0.6f,%0.6f,%0.6f]\n',tics, tics*Ts  - [.1,.2,.5,.8,1.0]);
		% [6015,12030,30075,48120,60150] -> [-0.000001,-0.000001,-0.000003,-0.000005,-0.000006]
		% Note: uint16_t maximum value is 65535 - so we can store the counter in 16 bits
		*/

#if defined(__AVR_ATtiny85__) | defined(__AVR_ATtiny45__) | (F_CPU == 16000000)
		WWVB_LOW = 12121;
		WWVB_HIGH = 30303;
		WWVB_MARKER = 48485;
		WWVB_ENDOFBIT = 60606;
#else // Its a 8MHz clock
		WWVB_LOW = 12030;
		WWVB_HIGH = 30075;
		WWVB_MARKER = 48120;
		WWVB_ENDOFBIT = 60150;
#endif
		WWVB_LOWTIME = WWVB_LOW; //DEFAULT STATE

		pulse_width[0] = WWVB_LOW;
		pulse_width[1] = WWVB_HIGH;
		pulse_width[1] = WWVB_MARKER;

		// Generate 60kHz carrier
#if defined(__AVR_ATtiny85__) | defined(__AVR_ATtiny45__)
		/*
		OC0A | !OC1A : 0 PB0 MOSI PWM 8b
		OC1A |  OC0B : 1 PB1 MISO PWM 8b
		OC1B : 4 PB4
		!OC1B : 3 PB3
		*/
		// The ATtiny is setup to use Fast PWM mode off the internal PLL
		//
		// Also, note that i use PB1, PB4 for the ATtiny as these are the same numbers 
		// as their digital pin, and PB1 etc are printed on the digistump silkscreen

		bitSet(PLLCSR, PLLE);// Start PLL
		while (bitRead(PLLCSR, PLOCK) == 0); // wait until the PLL is locked
		bitSet(PLLCSR, PCKE);// Set clock source to asynchronous PCK

		PWM_HIGH = 66; // ~50% duty cycle
		PWM_LOW = 6; // ~5% duty cycle
					 // PLL clock (PCK) = 64MHz
					 // PWM frequency settings: ATtiny25/45/85 datasheet p99
		OCR1C = 132; // Set PWM to 60kHz (8MHz / (132 + 1)) = 60150Hz

#if defined(USE_OC1A)
		pinMode(PB1, OUTPUT);// setup OC1A PWM pin as output

		TCCR1 |= _BV(PWM1A) // Clear timer/counter after compare match to OCR1C
			| _BV(COM1A1);   // Clear the OC1A output line after match

		TIMSK |= _BV(OCIE1A);// enable compare match interrupt on Timer1

		OCR1A = PWM_LOW; // Set pulse width to 50% duty cycle
#elif defined(USE_OC1B)
		pinMode(PB4, OUTPUT);// setup OC1B PWM pin as output

		GTCCR = _BV(PWM1B) // Clear timer/counter after compare match to OCR1C
			| _BV(COM1B1);   // Clear the OC1B output line after match

		TIMSK |= _BV(OCIE1B);// enable compare match interrupt on Timer1

		OCR1B = PWM_LOW; // Set pulse width to 50% duty cycle
#endif
#else // This is a catchall for any other chip, but ive only tested it against the chips listed below
//#elif defined(__AVR_ATmega16U4__) | defined(__AVR_ATmega32U4__) | \
//       defined(__AVR_ATmega168__) | defined(__AVR_ATmega168P__) | defined(__AVR_ATmega328P__)
		/*
		ATmega32u4
		OC0A |  OC1C : 11 PB7 PWM 8/16b
		OC0B :  3 PD0 SCL PWM 8b | 18 PWM 10b?
		OC1A | !OC4B :  9 PB5 PWM 16b
		OC1B |  OC4B : 10 PB6 PWM 16b
		OC3A |  OCB4 :  5 PC6 PWM HS
		OC4A : 13 PC7 PWM 10b
		!OC4D : 12 PD6 PWM 16b

		ATmega328p
		OC0A :  6 PD6
		OC0B :  5 PD5
		OC1A :  9 PB1
		OC1B : 10 PB2 !SS
		OC2A : 11 PB3 MOSI
		OC2B :  3 PD3
		*/
		// User Phase & Frequency correct PWM for other chips (leonardo, uno et. al.)
		TCCR1B = _BV(WGM13); // Mode 8: Phase & Frequency correct PWM

#if (F_CPU == 16000000)
		ICR1 = 133; // Set PWM to 60kHz (16MHz / (2*133)) = 60150Hz
					// Yes, its 133 and not 132 because the compare is double sided : 0->133->132->1
		PWM_HIGH = 65; // ~50% duty cycle
		PWM_LOW = 6; // ~5% duty cycle
#elif (F_CPU == 8000000)
		ICR1 = 67; // Set PWM to 60.6kHz (8MHz / (2*66)) = 60606Hz
		PWM_HIGH = 33; // ~50% duty cycle
		PWM_LOW = 3; // ~5% duty cycle
#endif


#if defined(USE_OC1A)
		pinMode(9, OUTPUT);

		TCCR1A = _BV(COM1A1); // Clear OC1A on compare match to OCR1A
		
		OCR1A = PWM_LOW;
#elif defined(USE_OC1B)
		pinMode(10, OUTPUT);

		TCCR1A = _BV(COM1B1); // Clear OC1B on compare match to OCR1B

		OCR1B = PWM_LOW;
#endif

TIMSK1 |= _BV(TOIE1); // enable interrupt on Timer1 overflow

#endif
		// clear the indexing
		frame_index = 0;
		subframe_index = 0;
		isr_count = 0;
		set_dut1(); // This isnt set again - dut1 is unused (but still sent)

		sei(); // enable interrupts
	}

	void interrupt_routine()
	{
		/*
		This routine is checked at each counter overflow - i.e. at 60kHz

		if the 60kHz PWM pulse has been low for the correct time
		1. set the PWM pulse high

		if instead the PWM pulse has finished sending the data bit (1second has elapsed)
		2.1. set the PWM pulse low
		2.2. increment to the next bit in the subframe
		2.3. set the next WWVB_LOWTIME
		*/

		if (++isr_count == WWVB_LOWTIME)
		{
#if defined(USE_OC1A)
			OCR1A = PWM_HIGH;
#elif defined(USE_OC1B)
			OCR1B = PWM_HIGH;
#endif
		}
		else if (isr_count >= WWVB_ENDOFBIT)
		{
#if defined(USE_OC1A)
			OCR1A = PWM_LOW;
#elif defined(USE_OC1B)
			OCR1B = PWM_LOW;
#endif
			// increment the frame indices
			if (++frame_index == 60)
			{
				// increment to the next minute
				add_time(1, 0);

				// reset the frame_index
				frame_index = 0;
			}
			subframe_index = frame_index % 10;
			set_lowTime();

			// reset the isr_count (poor-mans timer, but its synced to the 60kHz)
			isr_count = 0;
		}
	}

	void start()
	{
		frame_index = 0;
		subframe_index = 0;
		set_lowTime();
#if defined(USE_OC1A)
		OCR1A = PWM_LOW;   // Set PWM to 5% duty cycle (signal LOW)
#elif defined(USE_OC1B)
		OCR1B = PWM_LOW;   // Set PWM to 5% duty cycle (signal LOW)
#endif
		resume();
	}
	void set_low()
	{
#if defined(USE_OC1A)
		OCR1A = PWM_LOW;   // Set PWM to 5% duty cycle (signal LOW)
#elif defined(USE_OC1B)
		OCR1B = PWM_LOW;   // Set PWM to 5% duty cycle (signal LOW)
#endif
	}
	void set_high()
	{
#if defined(USE_OC1A)
		OCR1A = PWM_HIGH;   // Set PWM to 50% duty cycle (signal LOW)
#elif defined(USE_OC1B)
		OCR1B = PWM_HIGH;   // Set PWM to 50% duty cycle (signal LOW)
#endif
	}
	void stop()
	{
		_is_active = false;

#if defined(__AVR_ATtiny85__) | defined(__AVR_ATtiny45__)
		bitClear(TCCR1, CS12); // Set clock prescalar to 000 (STOP Timer/Clock)
#else
		bitClear(TCCR1B, CS10); // Set clock prescalar to 000 (STOP Timer/Clock)
#endif
	}
	void resume()
	{
		_is_active = true;

#if defined(__AVR_ATtiny85__) | defined(__AVR_ATtiny45__)
		bitSet(TCCR1, CS12); // Set clock prescalar to 8 (64MHz / 8 = 8MHz)
#else
		bitSet(TCCR1B, CS10); // Set clock prescalar to 1 (16MHz or 8MHz - as per the base clock)
#endif
	}

	boolean is_active()
	{
		return _is_active;
	}
	void get_time(volatile uint8_t &_mins, volatile uint8_t &_hour,
		volatile uint8_t &_DD, volatile uint8_t &_MM, volatile uint8_t &_YY)
	{
		_mins = mins_;
		_hour = hour_;
		_DD = DD_;
		_MM = MM_;
		_YY = YY_;
	}
	
	void set_time(const uint8_t &_mins, const uint8_t &_hour,
		const uint8_t &_DD, const uint8_t &_MM, const uint8_t &_YY,
		const uint8_t _daylight_savings = 0)
	{
		// get the current time
		t_mm = _mins;
		t_hh = _hour;
		t_DD = _DD;
		t_MM = _MM;
		t_YY = _YY;

		//increment by 1 minute (last 3 digits specify increment in : hour, min, sec)
		addTimezone<volatile uint8_t>(t_ss, t_mm, t_hh, t_DD, t_MM, t_YY, 0, 1, 0);

		// set the correct frame bits
		set_time(_daylight_savings);
	}
	void add_time(const uint8_t &_mins, const uint8_t &_hour)
	{
		// get the current time
		get_time(t_mm, t_hh, t_DD, t_MM, t_YY);

		//increment by 1 minute (last 3 digits specify increment in : hour, min, sec)
		addTimezone<volatile uint8_t>(t_ss, t_mm, t_hh, t_DD, t_MM, t_YY, 0, _mins, _hour);
		
		// set the correct frame bits
		set_time();
	}
#if( REQUIRE_TIMEDATESTRING == 1)
	void set_time(char dateString[], char timeString[], const uint8_t _daylight_savings = 0)
	{

		uint8_t _mins, _hour, _secs, _DD, _MM, _YY;

		DateString_to_DDMMYY(dateString, _DD, _MM, _YY);
		TimeString_to_HHMMSS(timeString, _hour, _mins, _secs);

		set_time(_mins, _hour, _DD, _MM, _YY, _daylight_savings);
	}
#endif
private:
	// this function performs no range-checking of variables
	void set_time(const uint8_t _daylight_savings = 0)
	{
		boolean _is_leap_year = is_leap_year(2000 + t_YY);
		uint16_t _doty = to_day_of_the_year<volatile uint8_t>(t_DD, t_MM, _is_leap_year);

		// Note: these set commands are conditional on a difference 
		// between the set value and the internal (saved) state
		set_mins(t_mm);
		set_hour(t_hh);

		set_day(t_DD);
		set_month(t_MM);
		
		set_doty(_doty);
		//set_dut1(); // only need to do this once at setup as we set dut1 to all zeros
		set_year(t_YY);
		
		set_misc(_is_leap_year, _daylight_savings);
	}
	void set_mins(uint8_t _mins)
	{
		if (mins_ != _mins)
		{
			mins_ = _mins;

			// set MINS
			//                  0   1   2   3   4   5   6   7   8   9
			//                  M  40  20  10   0   8   4   2   1   M
			if (_mins >= 40) { _mins -= 40; MINS[1] = 1; }
			else { MINS[1] = 0; }
			if (_mins >= 20) { _mins -= 20; MINS[2] = 1; }
			else { MINS[2] = 0; }
			if (_mins >= 10) { _mins -= 10; MINS[3] = 1; }
			else { MINS[3] = 0; }
			if (_mins >= 8) { _mins -= 8; MINS[5] = 1; }
			else { MINS[5] = 0; }
			if (_mins >= 4) { _mins -= 4; MINS[6] = 1; }
			else { MINS[6] = 0; }
			if (_mins >= 2) { _mins -= 2; MINS[7] = 1; }
			else { MINS[7] = 0; }
			MINS[8] = _mins;
		}
	}
	void set_hour(uint8_t _hour)
	{
		if (hour_ != _hour)
		{
			hour_ = _hour;

			// set HOUR
			//                  0   1   2   3   4   5   6   7   8   9
			//                  -   -  20  10   0   8   4   2   1   M
			if (_hour >= 20) { _hour -= 20; HOUR[2] = 1; }
			else { HOUR[2] = 0; }
			if (_hour >= 10) { _hour -= 10; HOUR[3] = 1; }
			else { HOUR[3] = 0; }
			if (_hour >= 8) { _hour -= 8; HOUR[5] = 1; }
			else { HOUR[5] = 0; }
			if (_hour >= 4) { _hour -= 4; HOUR[6] = 1; }
			else { HOUR[6] = 0; }
			if (_hour >= 2) { _hour -= 2; HOUR[7] = 1; }
			else { HOUR[7] = 0; }
			HOUR[8] = _hour;
		}
	}
	void set_doty(uint16_t _doty)
	{
		if (doty_ != _doty)
		{
			doty_ = _doty;

			// DOTY             0   1   2   3   4   5   6   7   8   9
			//                  -   - 200 100   0  80  40  20  10   M
			if (_doty >= 200) { _doty -= 200; DOTY[2] = 1; }
			else { DOTY[2] = 0; }
			if (_doty >= 100) { _doty -= 100; DOTY[3] = 1; }
			else { DOTY[3] = 0; }
			if (_doty >= 80) { _doty -= 80; DOTY[5] = 1; }
			else { DOTY[5] = 0; }
			if (_doty >= 40) { _doty -= 40; DOTY[6] = 1; }
			else { DOTY[6] = 0; }
			if (_doty >= 20) { _doty -= 20; DOTY[7] = 1; }
			else { DOTY[7] = 0; }
			if (_doty >= 10) { _doty -= 10; DOTY[8] = 1; }
			else { DOTY[8] = 0; }
			
			if (_doty >= 8) { _doty -= 8; DUT1[0] = 1; }
			else { DUT1[0] = 0; }
			if (_doty >= 4) { _doty -= 4; DUT1[1] = 1; }
			else { DUT1[1] = 0; }
			if (_doty >= 2) { _doty -= 2; DUT1[2] = 1; }
			else { DUT1[2] = 0; }
			DUT1[3] = _doty;
		}
	}
	void set_dut1()
	{
		// DUT1             0   1   2   3   4   5   6   7   8   9
		//                  8   4   2   1   -   -   +   -   +   M
		DUT1[7] = 1; // set sign to -ve

		// clear the first 4 values
		YEAR[0] = 0;
		YEAR[1] = 0;
		YEAR[2] = 0;
		YEAR[3] = 0;
		// Note: +ve, -ve makes not difference because the DUT1 value
		// (resides in YEAR) is set to zero on the next line
	}
	void set_day(uint8_t _DD)
	{
		if (DD_ != _DD)
		{
			DD_ = _DD;
		}
	}
	void set_month(uint8_t _MM)
	{
		if (MM_ != _MM)
		{
			MM_ = _MM;
		}
	}
	void set_year(uint8_t _year)
	{
		if (YY_ != _year)
		{
			YY_ = _year;

			// YEAR             0   1   2   3   4   5   6   7   8   9
			//                0.8 0.4 0.2 0.1   -  80  40  20  10   M
			// MISC             0   1   2   3   4   5   6   7   8   9
			//                  8   4   2   1   - LYI LSW   2   1   M
			if (_year >= 80) { _year -= 80; YEAR[5] = 1; }
			else { YEAR[5] = 0; }
			if (_year >= 40) { _year -= 40; YEAR[6] = 1; }
			else { YEAR[6] = 0; }
			if (_year >= 20) { _year -= 20; YEAR[7] = 1; }
			else { YEAR[7] = 0; }
			if (_year >= 10) { _year -= 10; YEAR[8] = 1; }
			else { YEAR[8] = 0; }
			
			if (_year >= 8) { _year -= 8; MISC[0] = 1; }
			else { MISC[0] = 0; }
			if (_year >= 4) { _year -= 4; MISC[1] = 1; }
			else { MISC[1] = 0; }
			if (_year >= 2) { _year -= 2; MISC[2] = 1; }
			else { MISC[2] = 0; }
			MISC[3] = _year;
		}
	}
	void set_misc(const boolean &_is_leap_year, const uint8_t &_daylight_savings)
	{
		// set leap year, leap second and daylight saving time info
		//                  0   1   2   3   4   5   6   7   8   9
		//                  8   4   2   1   - LYI LSW   2   1   M
		if (is_leap_year_ != _is_leap_year)
		{
			is_leap_year_ = _is_leap_year;

			MISC[5] = _is_leap_year;
		}
		//MISC[5] = 0; // Ignore leap second
		if (daylight_savings_ != _daylight_savings)
		{
			daylight_savings_ = _daylight_savings;

			MISC[7] = (_daylight_savings >> 1) & 0x1;
			MISC[8] = _daylight_savings & 0x1;
		}

	}
	void set_lowTime()
	{
		// Check for a frame marker
		switch (frame_index)
		{
			// markers
		case 0:  // start frame
		case 9:  // end MINS
		case 19: // end HOUR
		case 29: // end DOTY
		case 39: // end DUT1
		case 49: // end YEAR
		case 59: // end MISC, end frame
			WWVB_LOWTIME = WWVB_MARKER;
			return;
		}

		// No frame marker, so set the subframe bit data
		subframe_index = frame_index % 10;
		if (frame_index < 10) // MINS
		{
			WWVB_LOWTIME = pulse_width[MINS[subframe_index]];
		}
		else if (frame_index < 20) // HOUR
		{
			WWVB_LOWTIME = pulse_width[HOUR[subframe_index]];
		}
		else if (frame_index < 30) // DOTY
		{
			WWVB_LOWTIME = pulse_width[DOTY[subframe_index]];
		}
		else if (frame_index < 40) // DUT1
		{
			WWVB_LOWTIME = pulse_width[DUT1[subframe_index]];
		}
		else if (frame_index < 50) // YEAR
		{
			WWVB_LOWTIME = pulse_width[YEAR[subframe_index]];
		}
		else if (frame_index < 60) // MISC
		{
			WWVB_LOWTIME = pulse_width[MISC[subframe_index]];
		}
	}
};

#endif