
#ifndef MINIMAC_H
#define MINIMAC_H

#include <Arduino.h>
#include <EEPROM.h>
#include <MD5.h>

#define MINIMAC_KEY_LEN 16
#define MINIMAC_TAG_LEN 4
#define MINIMAC_HIST_LEN 5
#define MINIMAC_MAX_DATA 8

typedef struct {
  uint8_t len;
  uint8_t data[MINIMAC_MAX_DATA];
} MiniMacHist;

void minimac_init(uint16_t can_id, const uint8_t *key);

uint8_t minimac_sign(uint8_t *data, uint8_t payload_len);

bool minimac_verify(const uint8_t *data, uint8_t payload_len,
                    const uint8_t *tag);

#endif
