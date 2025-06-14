#include <SPI.h>
#include <mcp_can.h>
#include "minimac.h"


#define PROTECTED_ID  0x123
const uint8_t SECRET_KEY[MINIMAC_KEY_LEN] = {
    0x1A,0x2B,0x3C,0x4D,0x5E,0x6F,0x70,0x81,
    0x92,0xA3,0xB4,0xC5,0xD6,0xE7,0xF8,0x09
};

MCP_CAN CAN(10);  


void setup(void)
{
    Serial.begin(115200);
    while (!Serial)
         ;

    if (CAN.begin(MCP_STD, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
        Serial.println("CAN Init Failed!");
        for (;;)
             ;
    }
    CAN.setMode(MCP_NORMAL);

    minimac_init(PROTECTED_ID, SECRET_KEY);
}


void loop(void)
{
    
    if (CAN.checkReceive() != CAN_MSGAVAIL)
        return;

    long unsigned rxId;
    unsigned char len;
    unsigned char buf[8];
    CAN.readMsgBuf(&rxId, &len, buf);

    
    if (rxId != PROTECTED_ID)
        return;

    
    if (len < MINIMAC_TAG_LEN) {
        Serial.println("Frame too short");
        return;
    }

    
    uint8_t payloadLen = len - MINIMAC_TAG_LEN;
    uint8_t payload[MINIMAC_MAX_DATA];
    uint8_t tag[MINIMAC_TAG_LEN];
    memcpy(payload, buf, payloadLen);
    memcpy(tag, buf + payloadLen, MINIMAC_TAG_LEN);

    
    if (minimac_verify(payload, payloadLen, tag)) {
        Serial.print("Auth OK: ");
        for (uint8_t i = 0; i < payloadLen; i++) {
            Serial.print(payload[i], HEX);
            Serial.print(' ');
        }
        Serial.println();
    } else {
        Serial.println("Auth FAIL");
    }
}
