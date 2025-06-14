/**
 * @file send.ino
 * @brief CAN 통신 송신 예제 (Mini-MAC 인증 포함)
 *
 * 이 파일은 Mini-MAC 기반의 메시지 인증을 적용하여 CAN 버스로 데이터를 송신하는
 * 예제 코드입니다. 주기적으로 페이로드 데이터를 준비하고 Mini-MAC 태그를 생성한
 * 뒤, 이를 포함한 CAN 메시지를 전송합니다.
 */

#include "minimac.h"
#include <EEPROM.h>
#include <SPI.h>
#include <mcp_can.h>

/**
 * @brief Mini-MAC 인증이 적용되는 보호 대상 CAN 메시지 식별자.
 *
 * 이 CAN ID를 사용하는 메시지에 Mini-MAC 태그를 추가하여 송신 및 검증합니다.
 */
#define PROTECTED_ID 0x123

/**
 * @brief Mini-MAC 프로토콜에 사용되는 16바이트 비밀 키.
 *
 * 인증 태그 계산에 사용되는 키로, 송신자와 수신자가 동일한 값을 공유해야
 * 합니다.
 */
const uint8_t SECRET_KEY[MINIMAC_KEY_LEN] = {0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F,
                                             0x70, 0x81, 0x92, 0xA3, 0xB4, 0xC5,
                                             0xD6, 0xE7, 0xF8, 0x09};

/**
 * @brief CAN 버스 제어 객체.
 *
 * MCP2515 CAN 트랜시버를 제어하기 위한 MCP_CAN 클래스 인스턴스이며, 칩
 * 선택(CS)으로 10번 핀을 사용합니다.
 */
MCP_CAN CAN(10);

/**
 * @brief 시스템 초기화 함수로, 장치 설정을 수행합니다.
 *
 * 시리얼 통신을 115200 baud로 시작하고 Serial 포트가 열릴 때까지 대기합니다.
 * EEPROM 메모리를 전체 0xFF 값으로 초기화하여 과거 저장된 데이터를 지웁니다.
 * CAN 컨트롤러를 초기화(all ID 수신, 500kbps, 16MHz 클럭)한 후 정상 동작
 * 모드(MCP_NORMAL)로 설정합니다. Mini-MAC 프로토콜을 PROTECTED_ID와
 * SECRET_KEY로 초기화하여 메시지 인증 기능을 준비합니다 (fresh 상태 시작). 모든
 * 초기화가 완료되면 시리얼 모니터에 "[INFO] Sender Initialized" 메시지를
 * 출력합니다.
 */
void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

  // EEPROM 전체 초기화
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0xFF);
  }

  // CAN 초기화 (all IDs, 500kbps, 16MHz)
  if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK) {
    Serial.println("[ERROR] CAN Init Failed!");
    for (;;)
      ;
  }
  CAN.setMode(MCP_NORMAL);

  // Mini-MAC 초기화 (fresh 상태로 시작)
  minimac_init(PROTECTED_ID, SECRET_KEY);

  Serial.println("[INFO] Sender Initialized");
}

/**
 * @brief 주기적으로 메시지를 생성하여 전송하는 메인 루프 함수입니다.
 *
 * 예시 페이로드 데이터를 버퍼에 설정한 후, minimac_sign 함수를 호출하여 해당
 * 페이로드에 대한 Mini-MAC 인증 태그를 생성하고 부착합니다. 준비된 메시지를
 * PROTECTED_ID 식별자로 CAN 버스를 통해 송신합니다. 송신 결과를 시리얼 모니터에
 * "[INFO] Message sent" 또는 "[ERROR] Send failed" 형식으로 출력하고, 1초간
 * 대기한 후 다음 메시지를 준비합니다.
 */
void loop() {
  // 예시 페이로드: 0xDE 0xAD 0xBE 0xEF
  uint8_t buf[MINIMAC_MAX_DATA + MINIMAC_TAG_LEN];
  uint8_t payloadLen = 4;
  buf[0] = 0xDE;
  buf[1] = 0xAD;
  buf[2] = 0xBE;
  buf[3] = 0xEF;

  // Mini-MAC 태그 생성
  uint8_t totalLen = minimac_sign(buf, payloadLen);

  // CAN 전송
  byte result = CAN.sendMsgBuf(PROTECTED_ID, 0, totalLen, buf);
  if (result == CAN_OK) {
    Serial.println("[INFO] Message sent");
  } else {
    Serial.println("[ERROR] Send failed");
  }

  delay(1000);
}
