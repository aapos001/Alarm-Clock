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
 
//FreeRTOS include files 
#include "FreeRTOS.h" 
#include "task.h" 
#include "croutine.h" 
#include "usart_ATmega1284.h"

void A2D_init() { // FSR reading
	ADCSRA |= (1 << ADEN) | (1 << ADSC) | (1 << ADATE);
	// ADEN: Enables analog-to-digital conversion
	// ADSC: Starts analog-to-digital conversion
	// ADATE: Enables auto-triggering, allowing for constant
	//	    analog to digital conversions.
}

enum AlarmOffState {AOInit,AOWaitAlarm,AOWaitFSR,AOPress,AOOff} alarmOff_state;

unsigned char threeSecCount = 0;
unsigned char alarmOn = 0;


void AlarmOff_Init(){
	
	alarmOff_state = AOInit;
}

void AlarmOff_Tick(){
	
	// Transitions
	switch(alarmOff_state){
		
		case  AOInit:
			alarmOff_state = AOWaitAlarm;
		break;
		
		case AOWaitAlarm: 
			if(USART_HasReceived(0)) { // check if the alarm is on
				alarmOn = USART_Receive(0);
				USART_Flush(0);
				//alarmOff_state = AOWaitFSR;
				if(alarmOn) {
					alarmOn = 0;
					alarmOff_state = AOWaitFSR;
				}
			}
			else {
				alarmOff_state = AOWaitAlarm;	
			}
		break;
		
		case AOWaitFSR:
			if(PINA & 0x01) {
				alarmOff_state = AOPress;
			}
			else {
				alarmOff_state = AOWaitFSR;	
			}	
		break;
		
		case AOPress:
			
			if(threeSecCount >= 30) {
				alarmOff_state = AOOff;	
			}
			else if (PINA & 0x01){
				alarmOff_state = AOPress;					
			}
			else {
				alarmOff_state = AOWaitFSR;
			}
			
		break;
		
		case AOOff:
			if(USART_HasTransmitted(0)) {
				alarmOff_state = AOWaitAlarm;
			}
			else {
				alarmOff_state = AOOff;
			}
		break;
		
		default:
			alarmOff_state = AOInit;
		break;
	}
	
	// Actions
	switch(alarmOff_state){
		
		case AOInit:
		break;
		
		case AOWaitAlarm:
			PORTB = 0x00;
		break;
		
		case AOWaitFSR:
			threeSecCount = 0;
			PORTB = 0x00;
		break;
		
		case AOPress:
			threeSecCount++;
			PORTB = threeSecCount;
		break;
		
		case AOOff:
			threeSecCount = 0;
			if(USART_IsSendReady(0)) { // Send off signal through USART
				USART_Send(0x01, 0);
				PORTB = 0xFF;
			}			
		break;
		
		default:
			threeSecCount = 0;
		break;
	}
}

void AlarmOffTask()
{
	AlarmOff_Init();
   for(;;) 
   { 	
	AlarmOff_Tick();
	vTaskDelay(100); 
   } 
}

void StartSecPulse(unsigned portBASE_TYPE Priority)
{
	xTaskCreate(AlarmOffTask, (signed portCHAR *)"AlarmOffTask", configMINIMAL_STACK_SIZE, NULL, Priority, NULL );
}	
 
int main(void) 
{ 
	DDRB = 0xFF; PORTB = 0x00;
	DDRA = 0x00; PORTA = 0xFF;
   
   A2D_init();
   initUSART(0);
   USART_Flush(0);
   //Start Tasks  
   StartSecPulse(1);
    //RunSchedular 
   vTaskStartScheduler(); 
 
   return 0; 
}