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

enum DisplayTimeState {DTInit, DTDisplay, DTIdle, DTWaitHrB, DTHrSwap, DTToSA} displayTime_state;
enum SetAlarmState {SAInit, SAIdle, SASetAla, SADisplay, SAHrInc, SAMinInc, SASaveAla, SAToDT} setAlarm_state;
enum LEDPWMState {LPInit, LPOff, LPOn, LPReset} LEDPWM_state;
enum AlarmOnState {AOInit, AOCheck, AOSendFlag, AOWaitSignal, AOReset} alarmOn_state;

/* admin variables
   These variables determine which SM is displayed */
unsigned char DTAdmin = 0;
unsigned char SAAdmin = 0;

unsigned char hourMode = 0; // 0 is 12 hour mode | 1 is 24 hour mode
uint8_t ampm; // 1 is PM | 0 is AM

/* DS3231 variables */
uint8_t hr, min, sec, year, mnth, day, dt;
uint8_t hrdec, mindec, yeardec, mnthdec, daydec, dtdec;

// The set alarm
uint8_t alarmSetHour = 0x0F;   
uint8_t alarmSetMin = 0x0F; 
unsigned char alarmSetAMPM = 0; 

uint8_t alarmHour = 12;
uint8_t alarmMin = 0;
unsigned char alarmAMPM = 1;

unsigned char minTimer = 0; // Refreshes display every 60s

unsigned char alarmOnFlag = 0; // If flag == 1, the alarm is on

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
	mindec = bcd2dec(min);
	yeardec = bcd2dec(year);
	mnthdec = bcd2dec(mnth);
	dtdec = bcd2dec(dt);
}

	

void DisplayTime_Init(){
	
	displayTime_state = DTInit;
	
}

void SetAlarm_Init() {
	
	setAlarm_state = SAInit;
	
}

void LEDPWM_Init() {
	
	LEDPWM_state = LPInit;
	TCCR0A = (1 << COM0A1 | 1 << WGM00 | 1 << WGM01); // Toggle OC0A on Compare Match Fast PWM
	TCCR0B = (1 << CS00);	// No prescalar
	
}

void AlarmOn_Init() {
	
	alarmOn_state = AOInit;
}

/* Display the current time, day, and date
   Can switch between 12 hour and 24 hour mode 
   Can give admin to the set alarm state   */
void DisplayTime_Tick() {
	
	// Transitions
	switch(displayTime_state) {
		
		case DTInit:
			DTAdmin = 1; // Start as admin
			displayTime_state = DTDisplay; 
		break;
		
		case DTDisplay:
			displayTime_state = DTIdle;
		break;
		
		case DTIdle:
			if(LEFT && !(RIGHT || UP || DOWN)) { // Admin to SA
				displayTime_state = DTToSA;
			}
			else if(DOWN && !(LEFT || RIGHT || UP)) { // Change the hour mode
				displayTime_state = DTWaitHrB;
			}
			else if(minTimer >= 150) { // Refresh the display after 1 minute
				displayTime_state = DTDisplay; 
			}
			else { // Do nothing
				displayTime_state = DTIdle;
			}
		break;
		
		case DTWaitHrB: // Wait for button to be unpressed
			if(DOWN) { 
				displayTime_state = DTWaitHrB;
			}
			else if(!DOWN) {
				displayTime_state = DTHrSwap;
			}
			else {
				displayTime_state = DTWaitHrB;
			}
		break;
		
		case DTHrSwap: // Change the hour mode
			displayTime_state = DTDisplay;
		break;
		
		case DTToSA:
			if(DTAdmin == 1 && SAAdmin == 0) { // If admin has been returned from SA
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
			if((hourMode == 0) && (ampm == 1)) {
				LCD_DisplayString(6, "PM");
			}
			else if((hourMode == 0) && (ampm == 0)){
				LCD_DisplayString(6, "AM");
			}
			SLCD_WriteData(17, (mnthdec / 10) + '0'); // Display date
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
		break;
		
		case DTIdle: // Wait for a button press
			minTimer++;
		break;
		
		case DTWaitHrB: // Wait for the hour button unpress
			minTimer++;
		break;
		
		case DTHrSwap: // Change the hour mode
			minTimer++;
			if(hourMode == 0) { // 12 to 24
				hourMode = 1;
				ds3231_setHr(hourMode, hr);
				/*
				if(alarmSetHour > 12) {
					alarmSetHour -= 12;
				}
				*/
			}
			else if(hourMode == 1) { 
				hourMode = 0;
				ds3231_setHr(hourMode, hr);
				/*
				if(alarmSetAMPM) {
					alarmSetHour += 12;
				}
				*/
			}
		break;	
		
		case DTToSA: // Give admin to SA
			if(DTAdmin == 1) {
				DTAdmin = 0;
				SAAdmin = 1;
			}
		break;
		
		default:
			DTAdmin = 1;
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
			if(SAAdmin == 0) {
				setAlarm_state = SAIdle;
			}
			else if(SAAdmin == 1 && !LEFT) { // Button unpressed
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
			setAlarm_state = SAInit;
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
		break;
		
		case SAToDT:
			alarmHour = 12; // Reset Set Alarm variables
			alarmMin = 0;
			alarmAMPM = 1;
			if(SAAdmin == 1) {
				SAAdmin = 0;
				DTAdmin = 1;
			}
		break;
		
		default:
		break;
	}
}

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
			/*
			if(UP) { // PLACEHOLDER FOR Bluetooth
				LEDPWM_state = LPReset;				
			}
			*/
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
		break;
		
		case LPReset:
			OCR0A = 0;
		break;
		
		default:
		break;
	}
}

void AlarmOn_Tick() {

	unsigned int alarmCheck; // Convert alarm setting to minutes for easier alarm checking
	unsigned int hourSum; // Convert current time to minutes to check against alarm	
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
			}
			if(alarmOffSignal) {
				alarmOn_state = AOReset;
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
			alarmOffSignal = 0;		
		break;
		
		default:
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

void StartSecPulse(unsigned portBASE_TYPE Priority) {
	
	xTaskCreate(DisplayTimeTask, (signed portCHAR *)"DisplayTimeTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
	xTaskCreate(SetAlarmTask, (signed portCHAR *)"SetAlarmTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
	xTaskCreate(LEDPWMTask, (signed portCHAR *)"LEDPWMTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);	
	xTaskCreate(AlarmOnTask, (signed portCHAR *)"AlarmOnTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL);
}

int main(void) {
	
	DDRA = 0x00; PORTA = 0xFF;
	DDRB = 0xFF; PORTB = 0x00;
	DDRD = 0xFE; PORTD = 0x01;
	DDRC = 0xC0; PORTC = 0x3F;

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