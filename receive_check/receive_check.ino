#include <SPI.h>
#include <mcp_can.h>

const int SPI_CS_PIN = 10;
MCP_CAN CAN(SPI_CS_PIN);

void setup() {
  Serial.begin(115200);
  // begin() 호출 수정
  while (CAN_OK != CAN.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ)) {
    Serial.println("CAN init 실패, 재시도 중...");
    delay(100);
  }
  CAN.setMode(MCP_NORMAL);
  Serial.println("CAN init 성공!");
}

void loop() {
  unsigned long id;
  byte len;
  byte buf[8];

  // 메시지 수신 확인
  if (CAN_MSGAVAIL == CAN.checkReceive()) {
    // readMsgBuf(&id, &len, buf) 호출
    if (CAN.readMsgBuf(&id, &len, buf) == CAN_OK) {
      Serial.print("수신: ID=0x");
      Serial.print(id, HEX);
      Serial.print(" 데이터=[");
      for (byte i = 0; i < len; i++) {
        Serial.print("0x");
        Serial.print(buf[i], HEX);
        if (i < len - 1) Serial.print(", ");
      }
      Serial.println("]");
    } else {
      Serial.println("메시지 읽기 실패");
    }
  }
}
