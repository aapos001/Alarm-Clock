// Permission to copy is granted provided that this header remains intact.
// This software is provided with no warranties.

#ifndef LCD_H
#define LCD_H

#include <stdio.h>
#include <util/delay.h>

#define SET_BIT(p,i) ((p) |= (1 << (i)))
#define CLR_BIT(p,i) ((p) &= ~(1 << (i)))
#define GET_BIT(p,i) ((p) & (1 << (i)))

/*-------------------------------------------------------------------------*/

//#define DATA_BUS PORTD		// port connected to pins 7-14 of LCD display
#define CONTROL_BUS PORTD	// port connected to pins 4 and 6 of LCD disp.
#define RS 6			// pin number of uC connected to pin 4 of LCD disp.
#define E 5				// pin number of uC connected to pin 6 of LCD disp.

/*-------------------------------------------------------------------------*/

void delay_ms(int miliSec) { //for 8 Mhz crystal
	int i,j;
	for(i=0;i<miliSec;i++) {
		for(j=0;j<775;j++) {
			asm("nop");
		}
	}
}

/*-------------------------------------------------------------------------*/

void transmit_data(unsigned char data) {
	/* for each bit of data */
	for(unsigned i = 0; i < 8; i++) {
		PORTD |= 0x01; // Set SRCLR to 1 allowing data to be set
		PORTD &= 0xFD; // Also clear SRCLK in preparation of sending data
		if(data & 0x80) {
			PORTD |= 0x08;
		}
		else {
			PORTD &= 0xF7;
		}// set SER = next bit of data to be sent.
		PORTD |= 0x02; // set SRCLK = 1. Rising edge shifts next bit of data into the shift register
		data = data << 1;
	}
	/* end for each bit of data */
	PORTD |= 0x04; // set RCLK = 1. Rising edge copies data from the "Shift" register to the "Storage" register
	PORTD &= 0xE0;  // clears all lines in preparation of a new transmission
}

void LCD_WriteCommand (unsigned char Command) {
	CLR_BIT(CONTROL_BUS,RS);
	transmit_data(Command); // added
	//DATA_BUS = Command;
	SET_BIT(CONTROL_BUS,E);
	asm("nop");
	CLR_BIT(CONTROL_BUS,E);
	delay_ms(2); // ClearScreen requires 1.52ms to execute
}

void LCD_ClearScreen(void) {
	LCD_WriteCommand(0x01);
}

void LCD_init(void) {
	delay_ms(100); //wait for 100 ms for LCD to power up
	LCD_WriteCommand(0x38);
	LCD_WriteCommand(0x06);
	LCD_WriteCommand(0x0f);
	LCD_WriteCommand(0x01);
	delay_ms(10);
}

void LCD_WriteData(unsigned char Data) {
	SET_BIT(CONTROL_BUS,RS);
	transmit_data(Data); // added
	//DATA_BUS = Data;
	SET_BIT(CONTROL_BUS,E);
	asm("nop");
	CLR_BIT(CONTROL_BUS,E);
	delay_ms(1);
}

void LCD_Cursor(unsigned char column) {
	if ( column < 17 ) { // 16x2 LCD: column < 17; 16x1 LCD: column < 9
		LCD_WriteCommand(0x80 + column - 1);
		} else { // 6x2 LCD: column - 9; 16x1 LCD: column - 1
		LCD_WriteCommand(0xB8 + column - 9);
	}
}

void LCD_DisplayString( unsigned char column,  char* string) {
	//LCD_ClearScreen();
	unsigned char c = column;
	while(*string) {
		LCD_Cursor(c++);
		LCD_WriteData(*string++);
	}
}

void SLCD_WriteData(unsigned char column, unsigned char Data) {
	LCD_Cursor(column);
	LCD_WriteData(Data);
}

#endif // LCD_H
