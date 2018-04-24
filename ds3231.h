/* DS3231 library functions */
#ifndef DS3231_H
#define DS3231_H

#include <avr/io.h>

void ds3231_init(void);
void ds3231_set(uint8_t hr,uint8_t min,uint8_t sec,uint8_t ampm,uint8_t yr,uint8_t mnth,uint8_t dt,uint8_t day);
void ds3231_get(uint8_t *h,uint8_t *m,uint8_t *s,uint8_t *yr,uint8_t *mnth,uint8_t *dt,uint8_t *day);
void ds3231_setHr(uint8_t hour_ref, uint8_t hr);
void ds3231_getT(uint8_t *temp);

#endif