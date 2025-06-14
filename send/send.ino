
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
    static unsigned long lastSend = 0;

    
    if (millis() - lastSend < 1000)
        return;
    lastSend = millis();

    
    
     uint8_t buf[MINIMAC_MAX_DATA] = { 0xAB, 0xCD };

    
     uint8_t payloadLen = 2;


    
    uint8_t totalLen = minimac_sign(buf, payloadLen);

    
    if (CAN.sendMsgBuf(PROTECTED_ID, 0, totalLen, buf) == CAN_OK) {
        Serial.print("Sent: ");
        for (uint8_t i = 0; i < totalLen; i++) {
            Serial.print(buf[i], HEX);
            Serial.print(' ');
        }
        Serial.println();
    } else {
        Serial.println("Send Failed");
    }
}
