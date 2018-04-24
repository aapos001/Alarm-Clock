#include "ds3231.h"
#include "i2c_master.h"
#include <util/delay.h>

#define DS3231_READ 0xD1
#define DS3231_WRITE 0xD0

/* DS3231 conversions */
uint8_t dec2bcd(uint8_t d)
{
	return ((d/10 * 16) + (d % 10));
}

uint8_t bcd2dec(uint8_t b)
{
	return ((b/16 * 10) + (b % 16));
}

void ds3231_init(void) {
	
	i2c_init();

}

void ds3231_set(uint8_t hr,uint8_t min,uint8_t sec,uint8_t ampm,uint8_t yr,uint8_t mnth,uint8_t dt,uint8_t day) {
	
	/* The first byte transmitted by the master is the slave address. 
	Next follows a number of date bytes. The slaves returns an ACK 
	after each received byte. Data is transferred with MSB first.
	DS3231 pg 16 */
	
	hr &= 0x1F; // clear first 3 bits which are ampm, 24 hour bits
	hr |= 0x40; // standard is AMPM, D6 = 1, D5 = 0
	hr |= (ampm<<5); // 1 = PM 0 = AM
	
	i2c_start(DS3231_WRITE); // write
	i2c_write(0x00); // starting at address of seconds register
	i2c_write(sec); // increments to next register after every byte
	i2c_write(min);
	i2c_write(hr);
	i2c_write(day);
	i2c_write(dt);
	i2c_write(mnth);
	i2c_write(yr);
	i2c_stop();
	
}

void ds3231_get(uint8_t *h,uint8_t *m,uint8_t *s,uint8_t *yr,uint8_t *mnth,uint8_t *dt,uint8_t *day) {
	
	/* The first byte transmitted by the master is the slave address.
	The slave then returns an ACK. Next follows a number of data bytes
	from slave to master. The master returns ACK after all received 
	bytes other than the last one. The last is a NACK. The master 
	generates the START and STOP. DS3231 pg 16*/
	
	i2c_start(DS3231_WRITE);
	i2c_write(0x00);
	
	i2c_start(DS3231_READ); // read
	
	*s = i2c_read_ack(); // seconds
	*m = i2c_read_ack(); // minutes
	*h = i2c_read_ack(); // hour
	*day = i2c_read_ack(); // day
	*dt = i2c_read_ack(); // date
	*mnth = i2c_read_ack(); // month 
	*yr = i2c_read_nack(); // year
	
	i2c_stop();
	
}

void ds3231_setHr(uint8_t hour_ref, uint8_t hr) {
	
	/* If hour_ref == 0x00, set clock to 12 hour
	If hour_ref == 0x01 set clock to 24 hour  */
	
	uint8_t hr_hold = hr;
	i2c_start(DS3231_WRITE); // write
	i2c_write(0x02); // hour register
	if(hr_hold & 0x40) { // currently 12 hour mode
		if(hour_ref == 0x00) { // it is already in 12 hour mode
			i2c_stop();
			return;
		}
		else { // change to 24 hour mode
			if(hr_hold & 0x20) { // if PM, add 12
				hr_hold = bcd2dec(hr_hold & 0x1F);
				if(hr_hold == 12) {
					//hr_hold = 0;
				}
				else {
					hr_hold += 12;
				}
				hr_hold = dec2bcd(hr_hold);
				i2c_write(hr_hold & 0x3F);
				i2c_stop();
				return;
			}
			else { // if AM, do nothing except for 12AM
				hr_hold = bcd2dec(hr_hold & 0x1F); // AM
				if(hr_hold == 12) { // if 12AM, sub 12
					hr_hold = 0;
				}
				hr_hold = dec2bcd(hr_hold);
				i2c_write(hr_hold & 0x3F);
				i2c_stop();
				return;
			}
		}
	}
	else { // currently 24 hour mode
		if(hour_ref == 0x01) { // it is already 24 hour mode
			i2c_stop();
			return;
		}
		else { // change to 12 hour mode
			hr_hold &= 0x3F; // clear non hour bits
			hr_hold = bcd2dec(hr_hold);
			if(hr_hold == 0) { // if 12 am add 12
				hr_hold = 12;
				hr_hold = dec2bcd(hr_hold);
				i2c_write(hr_hold | 0x40);
				i2c_stop();
				return;
			}
			else if(hr_hold == 12) {
				hr_hold = dec2bcd(hr_hold);
				i2c_write(hr_hold | 0x60);
				i2c_stop();
				return;
			}
			else if(hr_hold > 12) { // set pm bit and sub 12
				hr_hold -= 12;
				hr_hold = dec2bcd(hr_hold);
				i2c_write(hr_hold | 0x60);
				i2c_stop();
				return;
			}
			else { // keep am 
				hr_hold = dec2bcd(hr_hold);
				i2c_write(hr_hold | 0x40);
				i2c_stop();
				return;
			}
		}
	}
}

void ds3231_getT(uint8_t *temp) {
	
	/* read upper byte temperature reg 0x11 */
	i2c_start(DS3231_WRITE);
	i2c_write(0x11);
		
	i2c_start(DS3231_READ); // read
	*temp = i2c_read_nack();
	i2c_stop();
		
}