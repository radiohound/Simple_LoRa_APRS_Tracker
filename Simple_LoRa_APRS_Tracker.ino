/* ========================================================================== *\
||		 Arduino with UART LoRa module as Simple APRS Tracker	      ||
||                                 Neo_Chen                                   ||
\* ========================================================================== */

/* ========================================================================== *\
|| Arduino with UART LoRa module as Simple APRS Tracker			      ||
|| Copyright (C) 2020 Kelei Chen					      ||
||									      ||
|| This program is free software: you can redistribute it and/or modify	      ||
|| it under the terms of the GNU General Public License as published by	      ||
|| the Free Software Foundation, either version 3 of the License, or	      ||
|| (at your option) any later version.					      ||
||									      ||
|| This program is distributed in the hope that it will be useful,	      ||
|| but WITHOUT ANY WARRANTY; without even the implied warranty of	      ||
|| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	      ||
|| GNU General Public License for more details.				      ||
||									      ||
|| You should have received a copy of the GNU General Public License	      ||
|| along with this program.  If not, see <https://www.gnu.org/licenses/>.     ||
\* ========================================================================== */

#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <TinyGPS++.h>
#include <RadioLib.h>

/* Pin Configuration */
#define PPS	26

#define GPS	Serial2
#define DBG	Serial

/* AX.25 */
#define _AX25_FLAG	((byte)0x7E)
#define _AX25_CALLSIGN_LENGTH	6

#define _MAX_PACKET_SIZE	332

/* Callsign & SSID */
typedef struct
{
	byte callsign[_AX25_CALLSIGN_LENGTH + 1];
	byte ssid;
} call_t;

/* User configurable variables */
const unsigned int period = 10;

call_t callsigns[] =
{
	// Callsign, SSID
	{"APZ072", 2},	// Destination (Being used to identify APRS software)
	{"BX4ACV", 2},	// Source (replace this with your own callsign & SSID
	{"WIDE1", 1},	// First repeater
	{"WIDE2", 2}	// Second repeater (up to 8)
};

const static char icon[] = "/$";
const static char comment[] = "PUNY TRACKER ON THE RUN";

/* It's not recommended to change code below */
#define CALLSIGNS_NUM	(sizeof(callsigns) / sizeof(callsigns[0]))
TinyGPSPlus gps;
SPIClass * hspi = new SPIClass(HSPI);

#ifdef ARDUINO_LORA_E5_DEV_BOARD
STM32WLx radio = new STM32WLx_Module(); 

//APRSClient aprs(&radio);  // had this in my code, but its not in this code. May not be needed

// set RF switch configuration for EBytes E77 dev board
// PB3 is an LED - activates while transmitting
// NOTE: other boards may be different!
//       Some boards may not have either LP or HP.
//       For those, do not set the LP/HP entry in the table.
static const uint32_t rfswitch_pins[] =
                         {PA6,  PA7,  PB3, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  {STM32WLx::MODE_IDLE,  {LOW,  LOW,  LOW}},
  {STM32WLx::MODE_RX,    {LOW, HIGH, LOW}},
  {STM32WLx::MODE_TX_LP, {HIGH, LOW, HIGH}},
  {STM32WLx::MODE_TX_HP, {HIGH, LOW, HIGH}},
  END_OF_MODE_TABLE,
};

#else
// NSS, DIO1, NRST, BUSY, SPI
SX1268 radio = new Module(15, 33, 23, 39, *hspi);
#endif

unsigned int volatile timer = period;

void pps(void);

void setup(void)
{
	DBG.begin(115200);
	GPS.begin(9600,SERIAL_8N1, 16, 17);
	pinMode(PPS, INPUT_PULLUP);

	attachInterrupt(digitalPinToInterrupt(PPS), pps, RISING);

	hspi->begin();
	radio.begin(433.775);
      #ifdef ARDUINO_LORA_E5_DEV_BOARD
      #else
	radio.setRfSwitchPins(2, 4);
      #endif
	radio.setOutputPower(22);
	radio.setBandwidth(125.0);
	radio.setCodingRate(5);
	radio.setSpreadingFactor(12);
	radio.setPreambleLength(32);
	radio.autoLDRO();
	radio.setCRC(2); // Enable 16 bit CRC
	
	DBG.println("INIT DONE!");
}

#define CONTROL_FIELD	0x3
#define PROTOCOL_ID	0xF0

unsigned int construct_packet(unsigned char *packet)
{
	unsigned int len = 0;
	unsigned int i = 0, j = 0;
	memset(packet, 0xFF, _MAX_PACKET_SIZE);

	for(j = 0; j < CALLSIGNS_NUM; j++)
	{
		for(i = 0; i < 7; i++)
		{
			packet[len++] = callsigns[j].callsign[i] << 1;
			if(callsigns[j].callsign[i] == '\0')
				packet[len - 1] = '\x20' << 1;
			if(i == 6)
				packet[len - 1] = ((j >= 2 ? 0x30 : 0x70) | callsigns[j].ssid) << 1;
				// Different bitmask for src / dst callsign & repeater callsign
		}
	}

	packet[len - 1]++; // The last repeater callsign in the list should have this bit flag set

	packet[len++] = CONTROL_FIELD;
	packet[len++] = PROTOCOL_ID;

	double lat_f = gps.location.lat();
	double lng_f = gps.location.lng();

	char lat_cardinal = lat_f < 0.0 ? 'S' : 'N';
	char lng_cardinal = lng_f < 0.0 ? 'W' : 'E';

	long lat_decmin = lround(fabs(lat_f) * 6000.0);
	long lng_decmin = lround(fabs(lng_f) * 6000.0);

	long lat_min = lat_decmin / 100;
	long lng_min = lng_decmin / 100;

	long lat_deg = lat_min / 60;
	long lng_deg = lng_min / 60;

	lat_decmin %= 100;
	lng_decmin %= 100;

	lat_min %= 60;
	lng_min %= 60;

	char pos[128];

	sprintf(pos, "!%02ld%02ld.%02ld%c%c%03ld%02ld.%02ld%c%c%03ld/%03ld /A=%06ld %s",
		lat_deg, lat_min, lat_decmin, lat_cardinal,
		icon[0],
		lng_deg, lng_min, lng_decmin, lng_cardinal,
		icon[1],
		gps.course.isValid() ? lround(gps.course.deg()) : 0,
		gps.speed.isValid() ? lround(gps.speed.knots()) : 0,
		gps.altitude.isValid() ? lround(gps.altitude.feet()) : 0,
		comment
	);

	DBG.println(pos);
	memcpy(packet + len, pos, strlen(pos));;
	len += strlen(pos);

	return len;
}

#define _MAX_LORA_SIZE 253

unsigned char packet[_MAX_PACKET_SIZE];
unsigned char tx_buf[_MAX_LORA_SIZE];
char dbg[128];
unsigned int len;
int ret;

void loop()
{
	/* Read GPS data */
	while(GPS.available() > 0)
	{
		gps.encode(GPS.read());
	}

	if(timer == 0)	/* Start transmitting APRS packet */
	{
		DBG.println("Time to transmit.");
		if(gps.location.isValid())
		{
			DBG.println("GPS Valid");
			len = construct_packet(packet);
			DBG.println("Packet Constructed");
			radio.transmit(packet, len);
			DBG.println("Packet Sent");
		}
		timer = UINT_MAX;	/* Reset timer */
	}
}

void pps(void)
{
	timer--;
	if(timer >= period)
		timer = period - 1;

	return;
}
