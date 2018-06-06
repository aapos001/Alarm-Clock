#include <stdint.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <stdbool.h> 
#include <string.h> 
#include <math.h> 
#include <avr/io.h> 
#include <avr/interrupt.h> 
#include <avr/eeprom.h> 
#include <avr/portpins.h> 
#include <avr/pgmspace.h> 
#include "lcd.h"
#include "ds3231.h"
#include "i2c_master.h"
#include "usart_ATmega1284.h"
 
//FreeRTOS include files 
#include "FreeRTOS.h" 
#include "task.h" 
#include "croutine.h" 

#define LEFT (!(PINA & 0x04)) // Button 1 - SA
#define RIGHT (!(PINA & 0x08)) // Button 2 - cancel
#define UP (!(PINA & 0x10)) // Button 3  - SA minute
#define DOWN (!(PINA & 0x20)) // Button 4 - hourMode / SA hour 

enum DisplayTimeState {DTInit, DTDisplay, DTIdle, DTWaitHrB, DTHrSwap, DTToST, DTToSA} displayTime_state;
enum SetAlarmState {SAInit, SAIdle, SASetAla, SADisplay, SAHrInc, SAMinInc, SASaveAla, SAToDT} setAlarm_state;
enum SetTimeState {STInit, STIdle, STSetTime, STDisplay, STHrInc, STMinInc, STSaveTime, STToDT} setTime_state;
enum LEDPWMState {LPInit, LPOff, LPOn, LPReset} LEDPWM_state;
enum AlarmOnState {AOInit, AOCheck, AOSendFlag, AOWaitSignal, AOReset} alarmOn_state;
enum SpeakerOnState {SInit, SOff, SOn, SReset} speakerOn_state;

/* 0x01 DTAdmin 
   0x02 SAAdmin 
   0x04 STAdmin
*/
unsigned char Admin = 0x01; 

unsigned char hourMode = 0; // 0 is 12 hour mode | 1 is 24 hour mode
uint8_t ampm; // 1 is PM | 0 is AM

/* DS3231 variables */
uint8_t hr, min, sec, year, mnth, day, dt;
uint8_t hrdec, mindec, secdec, yeardec, mnthdec, daydec, dtdec;

// The set alarm
uint8_t alarmSetHour = 0x0F;   
uint8_t alarmSetMin = 0x0F; 
unsigned char alarmSetAMPM = 0; 
unsigned char alarmIsSet = 0; // display on main time time until alarm

uint8_t alarmHour = 12;
uint8_t alarmMin = 0;
unsigned char alarmAMPM = 1;

uint8_t timeHour = 12;
uint8_t timeMin	= 0;
unsigned timeAMPM = 1;

unsigned char minTimer = 0; // Refreshes display every 60s

unsigned char alarmOnFlag = 0; // If flag == 1, the alarm is on
unsigned int alarmCheck; // Convert alarm setting to minutes for easier alarm checking
unsigned int hourSum; // Convert current time to minutes to check against alarm
/*
const double G = 392;
const double A = 440;
const double F = 349.23;
const double E = 329.63;
const double D = 293.67;
const double C = 261.63;
*/

int i = 0; // song counter
double alarmSong[40] = {392, 440, 392, 349.23, 
						329.63, 349.23, 392, 392, 
						293.67, 329.63, 349.23, 349.23, 
						329.63, 349.23, 392, 392,
						392, 440, 392, 349.23,
						329.63, 349.23, 392, 392,
						293.67, 293.67, 392, 392,
						329.63, 261.63, 261.63, 261.63};

void UpdateTime() {
	
	/* Time variables */
	ds3231_get(&hr,&min,&sec,&year,&mnth,&dt,&day);
	if(hourMode == 0) { // 12 hour
		ampm = hr;
		ampm &= 0x20; // ampm bit
		ampm = (ampm>>5);
		hrdec = bcd2dec(hr & 0x1F);
	}
	else if(hourMode == 1) { // 24 hour
		hrdec = bcd2dec(hr & 0x3F);
	}
	secdec = bcd2dec(sec);
	mindec = bcd2dec(min);
	yeardec = bcd2dec(year);
	mnthdec = bcd2dec(mnth);
	dtdec = bcd2dec(dt);
}

void set_PWM(double frequency) {
	
	// Keeps track of the currently set frequency
	// Will only update the registers when the frequency
	// changes, plays music uninterrupted.
	static double current_frequency;
	if (frequency != current_frequency) {

		if (!frequency) TCCR3B &= 0x08; //stops timer/counter
		else TCCR3B |= 0x03; // resumes/continues timer/counter
		
		// prevents OCR3A from overflowing, using prescaler 64
		// 0.954 is smallest frequency that will not result in overflow
		if (frequency < 0.954) OCR3A = 0xFFFF;
		
		// prevents OCR3A from underflowing, using prescaler 64					// 31250 is largest frequency that will not result in underflow
		else if (frequency > 31250) OCR3A = 0x0000;
		
		// set OCR3A based on desired frequency
		else OCR3A = (short)(8000000 / (128 * frequency)) - 1;

		TCNT3 = 0; // resets counter
		current_frequency = frequency;
	}
}	

void DisplayTime_Init(){
	
	displayTime_state = DTInit;
	Admin = 0x01; // Start as admin
	
}

void SetAlarm_Init() {
	
	setAlarm_state = SAInit;
	
}

void SetTime_Init() {
	
	setTime_state = STInit;
}

void LEDPWM_Init() {
	
	LEDPWM_state = LPInit;
	TCCR0A = (1 << COM0A1 | 1 << WGM00 | 1 << WGM01); // Toggle OC0A on Compare Match Fast PWM
	TCCR0B = (1 << CS00);	// No prescalar
	
}

void AlarmOn_Init() {
	
	alarmOn_state = AOInit;
}

void SpeakerOn_Init() {
	
	speakerOn_state = SInit;
	TCCR3A = (1 << COM3A0);
	// COM3A0: Toggle PB6 on compare match between counter and OCR3A
	TCCR3B = (1 << WGM32) | (1 << CS31) | (1 << CS30);
	// WGM32: When counter (TCNT3) matches OCR3A, reset counter
	// CS31 & CS30: Set a prescaler of 64
	set_PWM(392);
}

/* Display the current time, day, and date
   Can switch between 12 hour and 24 hour mode 
   Can give admin to the set alarm state and set time state  */
void DisplayTime_Tick() {
	
	// Transitions
	switch(displayTime_state) {
		
		case DTInit:
			displayTime_state = DTDisplay; 
		break;
		
		case DTDisplay:
			displayTime_state = DTIdle;
		break;
		
		case DTIdle:
			if(LEFT && !(RIGHT || UP || DOWN)) { // Admin to SA
				displayTime_state = DTToSA;
			}
			else if(DOWN && !(LEFT || RIGHT || UP)) { // change hour mode
				displayTime_state = DTWaitHrB;
			}
			else if(RIGHT && !(LEFT || UP || DOWN)) { // Admin to ST
				displayTime_state = DTToST;
			}
			else if(minTimer >= 5) { // Refresh the display after 1 second
				displayTime_state = DTDisplay; 
			}
			else { // Do nothing
				displayTime_state = DTIdle;
			}
		break;
		
		case DTWaitHrB:
			if(DOWN) {
				displayTime_state = DTWaitHrB;
			}
			else {
				displayTime_state = DTHrSwap;
			}
		break;
		
		case DTHrSwap:
			displayTime_state = DTIdle;
		break;
		
		case DTToST: 
			if(Admin == 0x01) { // If admin has been returned from ST
				displayTime_state = DTDisplay;
			}
			else {
				displayTime_state = DTToST;
			}
		break;
		
		case DTToSA:
			if(Admin == 0x01) { // If admin has been returned from SA
				displayTime_state = DTDisplay;
			}
			else {
				displayTime_state = DTToSA;
			}
		break;
		
		default:
			displayTime_state = DTInit;
		break;
	}
	
	// Actions
	switch(displayTime_state) {
		
		case DTInit:
		break;
		
		case DTDisplay:
			minTimer = 0; // Reset the minute timer
			UpdateTime();
			LCD_ClearScreen();
			SLCD_WriteData(1,(hrdec / 10) + '0'); // Display time
			SLCD_WriteData(2, (hrdec % 10) + '0');
			SLCD_WriteData(3, ':');
			SLCD_WriteData(4, (mindec / 10) + '0'); 
			SLCD_WriteData(5, (mindec % 10) + '0'); 
			SLCD_WriteData(6, ':');
			SLCD_WriteData(7, (secdec / 10) + '0');
			SLCD_WriteData(8, (secdec % 10) + '0');
			if((hourMode == 0) && (ampm == 1)) {
				LCD_DisplayString(9, "PM");
			}
			else if((hourMode == 0) && (ampm == 0)){
				LCD_DisplayString(9, "AM");
			}
			if(alarmIsSet) {
				LCD_DisplayString(17, "Alarm ");
				if(hourMode == 0) {
					if(alarmSetHour >= 13) {
						SLCD_WriteData(23, ((alarmSetHour - 12) / 10) + '0');
						SLCD_WriteData(24, ((alarmSetHour - 12) % 10) + '0');
					}
					else {
						SLCD_WriteData(23, (alarmSetHour / 10) + '0');
						SLCD_WriteData(24, (alarmSetHour % 10) + '0');
					}
					SLCD_WriteData(25, ':');
					SLCD_WriteData(26, (alarmSetMin / 10) + '0');
					SLCD_WriteData(27, (alarmSetMin % 10) + '0');
					if(alarmSetAMPM) {
						LCD_DisplayString(28, "PM");
					}
					else {
						LCD_DisplayString(28, "AM");
					}
				}
				else {
					if(alarmSetAMPM) {
						SLCD_WriteData(23, ((alarmSetHour + 12) / 10) + '0');
						SLCD_WriteData(24, ((alarmSetHour + 12) % 10) + '0');					
					}
					else {
						SLCD_WriteData(23, (alarmSetHour / 10) + '0');
						SLCD_WriteData(24, (alarmSetHour % 10) + '0');						
					}

					SLCD_WriteData(25, ':');
					SLCD_WriteData(26, (alarmSetMin / 10) + '0');
					SLCD_WriteData(27, (alarmSetMin % 10) + '0');					
				}
			}
			/* DISPLAY DATE FUNCTIONALITY
			SLCD_WriteData(17, (mnthdec / 10) + '0');
			SLCD_WriteData(18, (mnthdec % 10) + '0'); 
			SLCD_WriteData(19, '/');
			SLCD_WriteData(20, (dtdec / 10) + '0');
			SLCD_WriteData(21, (dtdec % 10) + '0');
			LCD_DisplayString(22, "/20");
			SLCD_WriteData(25, (yeardec / 10) + '0');
			SLCD_WriteData(26, (yeardec % 10) + '0');
			switch(day) {
				case 1:
					LCD_DisplayString(28, "SUN");
				break;
				case 2:
					LCD_DisplayString(28, "MON");
				break;
				case 3:
					LCD_DisplayString(28, "TUE");
				break;
				case 4:
					LCD_DisplayString(28, "WED");
				break;
				case 5:
					LCD_DisplayString(28, "THU");
				break;
				case 6:
					LCD_DisplayString(28, "FRI");
				break;
				case 7:
					LCD_DisplayString(28, "SAT");
				break;
				default:
					LCD_DisplayString(28, "broke");
				break;
			}
			*/			
		break;
		
		case DTIdle: // Wait for a button press
			minTimer++;
		break;
		
		case DTWaitHrB:
			minTimer++;
		break;
		
		case DTHrSwap: // Change the hour mode
			minTimer++;
			if(hourMode == 0) { // 12 to 24
				hourMode = 1;
				ds3231_setHr(hourMode, hr);
				if(alarmSetHour > 12) {
					alarmSetHour -= 12;
				}
			}
			else if(hourMode == 1) { 
				hourMode = 0;
				ds3231_setHr(hourMode, hr);
				if(alarmSetAMPM) {
					alarmSetHour += 12;
				}
			}
		break;	
				
		case DTToST: // Give admin to ST
			if(Admin == 0x01) {
				Admin = 0x04;
			}
			minTimer++;
		break;
		
		case DTToSA: // Give admin to SA
			if(Admin == 0x01) {
				Admin = 0x02;
			}
			minTimer++;
		break;
		
		default:
			Admin = 0x01;
		break;
	}
		
}

/* Display an alarm to be set
   Button 1 - Set Alarm displayed
   Button 2 - Cancel
   Button 3 - Increase minute
   Button 4 - Increase hour
*/
void SetAlarm_Tick() {
	
	// Transitions
	switch(setAlarm_state) {
		
		case SAInit:
			setAlarm_state = SAIdle;
		break;
		
		case SAIdle: // Wait for admin
			if(Admin != 0x02) {
				setAlarm_state = SAIdle;
			}
			else if(Admin == 0x02 && !LEFT)	{ // Button unpressed
				setAlarm_state = SADisplay;
			}
		break;
		
		case SADisplay: // Output current alarm setting time
			setAlarm_state = SASetAla;
		break;
		
		case SASetAla: // Wait for button inputs
			if(LEFT && !(RIGHT || UP || DOWN)) { // Save alarm
				setAlarm_state = SASaveAla;
			}
			else if(RIGHT && !(LEFT || UP || DOWN)) { // Cancel alarm
				setAlarm_state = SAToDT;
			}
			else if(UP && !(LEFT || RIGHT || DOWN)) { // Increase minutes
				setAlarm_state = SAMinInc;
			}
			else if(DOWN && !(LEFT || RIGHT || UP)) { // Increase hours
				setAlarm_state = SAHrInc;
			}
		break;
		
		case SASaveAla: // Save alarm
			setAlarm_state = SAToDT;
		break;
		
		case SAToDT: // Return admin to DT
			setAlarm_state = SAIdle;
		break;
		
		case SAMinInc: // Inc minutes
			setAlarm_state = SADisplay;
		break;
		
		case SAHrInc: // Inc hours
			setAlarm_state = SADisplay;
		break;
		
		default:
			setAlarm_state = SAInit;
		break;
	}
	
	// Actions
	switch(setAlarm_state) {
		
		case SAInit:
		break;
		
		case SAIdle: // Waiting for admin
		break;
		
		case SADisplay: // Display current alarm setting
			LCD_ClearScreen();
			LCD_DisplayString(1, "Set Alarm");
			if(hourMode == 0 && alarmHour >= 13) {
				SLCD_WriteData(17,((alarmHour - 12) / 10) + '0'); // Display time
				SLCD_WriteData(18, ((alarmHour - 12) % 10) + '0');				
			}
			else {
				SLCD_WriteData(17,(alarmHour / 10) + '0'); // Display time
				SLCD_WriteData(18, (alarmHour % 10) + '0');				
			}
			SLCD_WriteData(19, ':');
			SLCD_WriteData(20, (alarmMin / 10) + '0'); 
			SLCD_WriteData(21, (alarmMin % 10) + '0'); 
			if((hourMode == 0) && (alarmAMPM == 1)) {
				LCD_DisplayString(22, "PM");
			}
			else if((hourMode == 0) && (alarmAMPM == 0)){
				LCD_DisplayString(22, "AM");
			}			
		break;
		
		case SASetAla: // Wait for input
		break;
		
		case SAHrInc: // Inc hours
			alarmHour++;
			if(hourMode == 0) { // 12 hour mode settings
				if(alarmHour > 24) {
					alarmHour = 1;
				}
				if(alarmHour >= 12 && alarmHour < 24) {
					alarmAMPM = 1;
				}
				else {
					alarmAMPM = 0;
				}
				
			}
			else if(hourMode == 1 && alarmHour >= 24) { // 24 hour mode settings
				alarmHour = 0;
			}
		break;
		
		case SAMinInc: // Inc mins
			alarmMin++;
			if(alarmMin >= 60) {
				alarmMin = 0;
			}
		break;
		
		case SASaveAla:
			alarmSetHour = alarmHour;
			alarmSetMin = alarmMin;
			alarmSetAMPM = alarmAMPM;
			alarmIsSet = 1;
		break;
		
		case SAToDT:
			alarmHour = 12; // Reset Set Alarm variables
			alarmMin = 0;
			alarmAMPM = 1;
			if(Admin == 0x02) {
				Admin = 0x01;
			}
		break;
		
		default:
		break;
	}
}

/* Display a time to be set
   Button 1 - Set Time displayed
   Button 2 - Cancel
   Button 3 - Increase minute
   Button 4 - Increase hour
*/
void SetTime_Tick() {
	
// Transitions
	switch(setTime_state) {
	
		case STInit:
			setTime_state = STIdle;
		break;
	
		case STIdle: // Wait for admin
			if(Admin != 0x04) {
				setTime_state = STIdle;
			}
			else if(Admin == 0x04 && !DOWN)	{ // Button unpressed
			setTime_state = STDisplay;
			}
		break;
	
		case STDisplay: // Output current time setting time
			setTime_state = STSetTime;
		break;
	
		case STSetTime: // Wait for button inputs
			if(LEFT && !(RIGHT || UP || DOWN)) { // Save time
				setTime_state = STSaveTime;
			}
			else if(RIGHT && !(LEFT || UP || DOWN)) { // Cancel time
				setTime_state = STToDT;
			}
			else if(UP && !(LEFT || RIGHT || DOWN)) { // Increase minutes
				setTime_state = STMinInc;
			}
			else if(DOWN && !(LEFT || RIGHT || UP)) { // Increase hours
				setTime_state = STHrInc;
			}
		break;
	
		case STSaveTime: // Save time
			setTime_state = STToDT;
		break;
	
		case STToDT: // Return admin to DT
			setTime_state = STIdle;
		break;
	
		case STMinInc: // Inc minutes
			setTime_state = STDisplay;
		break;
	
		case STHrInc: // Inc hours
			setTime_state = STDisplay;
		break;
	
		default:
			setTime_state = STInit;
		break;
		
	}	
	
	// Actions
	switch(setTime_state) {
	
		case STInit:
		break;
		
		case STIdle: // wait for admin
		break;
		
		case STDisplay: // display current set time
			LCD_ClearScreen();
			LCD_DisplayString(1, "Set Time");
			if(hourMode == 0 && timeHour >= 13) {
				SLCD_WriteData(17,((timeHour - 12) / 10) + '0'); // Display time
				SLCD_WriteData(18, ((timeHour - 12) % 10) + '0');
			}
			else {
				SLCD_WriteData(17,(timeHour / 10) + '0'); // Display time
				SLCD_WriteData(18, (timeHour % 10) + '0');
			}
			SLCD_WriteData(19, ':');
			SLCD_WriteData(20, (timeMin / 10) + '0');
			SLCD_WriteData(21, (timeMin % 10) + '0');
			if((hourMode == 0) && (timeAMPM == 1)) {
				LCD_DisplayString(22, "PM");
			}
			else if((hourMode == 0) && (timeAMPM == 0)){
				LCD_DisplayString(22, "AM");
			}
		break;
		
		case STSetTime: // wait for input
		break;
		
		case STSaveTime: // call set time function from ds32131.h
			if(hourMode == 0 && timeHour >= 13) {
				timeHour -= 12;
			}
			timeHour = dec2bcd(timeHour);
			timeMin = dec2bcd(timeMin);
			ds3231_setTime(timeHour, timeMin, 0, timeAMPM, hourMode);
		break;
		
		case STToDT:
			timeHour = 12; // Reset Set time variables
			timeMin = 0;
			timeAMPM = 1;
			if(Admin == 0x04) {
				Admin = 0x01;
			}
		break;
		
		case STMinInc:
			timeMin++;
			if(timeMin >= 60) {
				timeMin = 0;
			}
		break;
		
		case STHrInc:
			timeHour++;
			if(hourMode == 0) { // 12 hour mode settings
				if(timeHour > 24) {
					timeHour = 1;
				}
				if(timeHour >= 12 && timeHour < 24) {
					timeAMPM = 1;
				}
				else {
					timeAMPM = 0;
				}
				
			}
			else if(hourMode == 1 && timeHour >= 24) { // 24 hour mode settings
				timeHour = 0;
			}
		break;
		
		default:
			setTime_state = STInit;
		break;	
	}
	
};

// Turns light on and off
void LEDPWM_Tick() {
	
	// Transitions
	switch(LEDPWM_state) {
		
		case LPInit:
			LEDPWM_state = LPOff;
		break;
		
		case LPOff:
			if(alarmOnFlag) {
				LEDPWM_state = LPOn;
			}
			else {
				LEDPWM_state = LPOff;
			}
		break;
		
		case LPOn:
			if(!alarmOnFlag) {
				LEDPWM_state = LPReset;
			}
			else {
				LEDPWM_state = LPOn;
			}
		break;
		
		case LPReset:
			LEDPWM_state = LPOff;
		break;
		
		default:
			LEDPWM_state = LPInit;
		break;
	}
	
	// Actions
	switch(LEDPWM_state) {
		
		case LPInit:
		break;
		
		case LPOff:
		break;
		
		case LPOn:
			if(OCR0A < 255) { // LED gets to max brightness
			OCR0A++;
			}
			PORTB |= 0x20;
		break;
		
		case LPReset:
			OCR0A = 0;
			PORTB &= 0xDF;
		break;
		
		default:
		break;
	}
}

void AlarmOn_Tick() {

	unsigned char alarmOffSignal = 0; // If signal == 1, turn the alarm off
	// Transitions
	switch(alarmOn_state) {
		
		case AOInit:
			alarmOn_state = AOCheck;
		break;
		
		case AOCheck:
			if((hourMode == 0) && (alarmSetHour == 24 || ((ampm == 1) && (hrdec < 12)))) { // 12 hour mode at midnight or after 1pm
					hourSum = (hrdec * 60) + mindec + 720;
			}
			else if ((hourMode == 1) && (alarmSetHour == 24)){ // 24 hour mode at midnight
					hourSum = mindec + 1440;
			}
			else {
					hourSum = (hrdec * 60) + mindec;
			}
			alarmCheck = (alarmSetHour * 60) + alarmSetMin;
			if(((alarmCheck - hourSum) <= 10)) { // Turn "on" alarm 10 minutes before
				// FIX FOR EDGE CASE ALARM AT MIDNIGHT
				alarmOnFlag = 1;
				alarmOn_state = AOSendFlag;
			}		
		break;
		
		case AOSendFlag:
			if(USART_HasTransmitted(0)) {
				alarmOn_state = AOWaitSignal;
			}
			else {
				alarmOn_state = AOSendFlag;
			}
		break;
		
		case AOWaitSignal:
			if(USART_HasReceived(0)) {
				alarmOffSignal = USART_Receive(0);
				USART_Flush(0);
				//alarmOn_state = AOReset;	
				if(alarmOffSignal) {
					alarmOn_state = AOReset;
				}
			}
			else {
				alarmOn_state = AOWaitSignal;
			}
		break;
		
		case AOReset:
			alarmOn_state = AOCheck;
		break;
		
		default:
			alarmOn_state = AOInit;
		break;
	}
	
	// Actions
	switch(alarmOn_state) {
		
		case AOInit:
		break;
		
		case AOCheck:
		break;
		
		case AOSendFlag:
			if(USART_IsSendReady(0)) {
				USART_Send(0x01, 0);
			}
		break;
		
		case AOWaitSignal:
		break;
		
		case AOReset:
			alarmSetHour = 0x0F;
			alarmSetMin = 0x0F;
			alarmSetAMPM = 0;
			alarmOnFlag = 0;
			alarmIsSet = 0;
			alarmOffSignal = 0;		
		break;
		
		default:
		break;
	}
}

void SpeakerOn_Tick() {
	
	// Transitions
	switch(speakerOn_state) {
		
		case SInit:
			speakerOn_state = SOff;
		break;
		
		case SOff:
			
			if((hourMode == 0) && (alarmSetHour == 24 || ((ampm == 1) && (hrdec < 12)))) { // 12 hour mode at midnight or after 1pm
				hourSum = (hrdec * 60) + mindec + 720;
			}
			else if ((hourMode == 1) && (alarmSetHour == 24)){ // 24 hour mode at midnight
				hourSum = mindec + 1440;
			}
			else {
				hourSum = (hrdec * 60) + mindec;
			}
			alarmCheck = (alarmSetHour * 60) + alarmSetMin;
			
			if(alarmCheck == hourSum) { // Turn "on" alarm on alarm time
				speakerOn_state = SOn;
			}
			else {
				speakerOn_state = SOff;
			}
		break;
		
		case SOn:
			if(!alarmOnFlag) {
				speakerOn_state = SOff;
			}
			else {
				speakerOn_state = SOn;
			}
		break;
		
		case SReset:
			speakerOn_state = SOff;
		break;
		
		default:
			speakerOn_state = SInit;
		break;
		
	}
		
	// Actions
	switch(speakerOn_state) {
	
		case SInit:
		break;
		
		case SOff:
			set_PWM(0);
		break;
		
		case SOn:
			set_PWM(alarmSong[i]);
			if(i >= 40) {
				i = 0;
			}
			else {
				i++;
			}
		break;
		
		case SReset:
			set_PWM(0);
		break;	
		
		default:
			set_PWM(0);
		break;
	}
}

void DisplayTimeTask() {
	
	DisplayTime_Init();
	for(;;) {
		DisplayTime_Tick();
		vTaskDelay(200);
	}
}

void SetAlarmTask() {
	
	SetAlarm_Init();
	for(;;) {
		SetAlarm_Tick();
		vTaskDelay(50);
	}
}

void SetTimeTask() {
	
	SetTime_Init();
	for(;;) {
		SetTime_Tick();
		vTaskDelay(50);
	}
}

void LEDPWMTask() {
	
	LEDPWM_Init();
	for(;;) {
		LEDPWM_Tick();
		vTaskDelay(200);
	}
}

void AlarmOnTask() {
	
	AlarmOn_Init();
	for(;;) {
		AlarmOn_Tick();
		vTaskDelay(1000);
	}	
}

void SpeakerOnTask() {
	
	SpeakerOn_Init();
	for(;;) {
		SpeakerOn_Tick();
		vTaskDelay(500);
	}	
}

void StartSecPulse(unsigned portBASE_TYPE Priority) {
	
	xTaskCreate(DisplayTimeTask, (signed portCHAR *)"DisplayTimeTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
	xTaskCreate(SetAlarmTask, (signed portCHAR *)"SetAlarmTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
	xTaskCreate(SetTimeTask, (signed portCHAR *)"SetTimeTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
	xTaskCreate(LEDPWMTask, (signed portCHAR *)"LEDPWMTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);	
	xTaskCreate(AlarmOnTask, (signed portCHAR *)"AlarmOnTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
	xTaskCreate(SpeakerOnTask, (signed portCHAR *)"SpeakerOnTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
}

int main(void) {
	
	DDRA = 0x00; PORTA = 0xFF;
	DDRB = 0xFF; PORTB = 0x00;
	DDRD = 0xFE; PORTD = 0x01;
	DDRC = 0xEC; PORTC = 0x13;

	LCD_init();
	ds3231_init();	
	_delay_ms(100);
	initUSART(0);
	USART_Flush(0);
	
	/* hour, minute, second, am/pm, year, month, date, day */
	//ds3231_set(0x12, 0x29, 0x00, 0x01, 0x18, 0x04, 0x23, 0x02);
	
    //Start Tasks  
    StartSecPulse(1);
    //RunSchedular 
    vTaskStartScheduler(); 
	
	return 0; 
}	