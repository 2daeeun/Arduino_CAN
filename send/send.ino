#include <SPI.h>
#include <mcp_can.h>

const int SPI_CS_PIN = 10;
MCP_CAN CAN(SPI_CS_PIN);

void setup() {
  Serial.begin(115200);

  // idmodeset: MCP_ANY (모든 ID 허용)
  // speedset: CAN_500KBPS (500 kbps)
  // clockset: MCP_16MHZ (MCP2515에 16 MHz 크리스탈)
  while (CAN_OK != CAN.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ)) {
    Serial.println("CAN init 실패, 재시도 중...");
    delay(100);
  }
  // 정상 운영 모드로 전환 (loop-back이 아닌 실제 송수신 모드)
  CAN.setMode(MCP_NORMAL);

  Serial.println("CAN init 성공!");
}

void loop() {
  byte payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
  if (CAN.sendMsgBuf(0x100, 0, 4, payload) == CAN_OK) {
    Serial.println("메시지 전송 완료: ID=0x100");
  } else {
    Serial.println("메시지 전송 실패");
  }
  delay(500);
}
