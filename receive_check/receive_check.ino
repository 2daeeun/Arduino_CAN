/**
 * @file receive_check.ino
 * @brief CAN 통신 수신 예제 (Mini-MAC 인증 검증 포함)
 *
 * 이 파일은 Mini-MAC 기반의 메시지 인증을 적용하여 CAN 버스로 들어오는 메시지를
 * 검증하는 예제 코드입니다. 수신된 CAN 메시지의 ID와 인증 태그를 검사하여,
 * 유효한 메시지인지 여부를 판단합니다.
 */

#include "minimac.h"
#include <EEPROM.h>
#include <SPI.h>
#include <mcp_can.h>

/**
 * @brief Mini-MAC 인증이 적용되는 보호 대상 CAN 메시지 식별자.
 *
 * 송신 측과 동일한 식별자로, 이 ID의 메시지에 대해 인증 태그를 검증합니다.
 */
#define PROTECTED_ID 0x123

/**
 * @brief Mini-MAC 프로토콜에 사용되는 16바이트 비밀 키.
 *
 * 송신 측과 공유되는 키로, 수신된 메시지의 태그 검증 시 사용됩니다.
 */
const uint8_t SECRET_KEY[MINIMAC_KEY_LEN] = {0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F,
                                             0x70, 0x81, 0x92, 0xA3, 0xB4, 0xC5,
                                             0xD6, 0xE7, 0xF8, 0x09};

/**
 * @brief CAN 버스 제어 객체.
 *
 * MCP2515 기반 CAN 트랜시버를 제어하는 MCP_CAN 인스턴스이며, CS 핀 10번을
 * 사용합니다.
 */
MCP_CAN CAN(10);

/**
 * @brief 수신기 시스템 초기화 함수로, 필요한 설정을 수행합니다.
 *
 * 시리얼 통신을 115200 baud로 시작하고 Serial 연결을 기다립니다.
 * EEPROM 메모리 내용을 0xFF로 모두 채워 초기화합니다.
 * CAN 컨트롤러를 초기화(all ID 수신, 500kbps, 16MHz 클럭) 후 정상
 * 모드(MCP_NORMAL)로 설정합니다. Mini-MAC 프로토콜을 PROTECTED_ID와
 * SECRET_KEY로 초기화하여 수신 시 인증 검증을 수행할 준비를 합니다. 설정이
 * 완료되면 시리얼 모니터에 "[INFO] Receiver Initialized" 메시지를 출력합니다.
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

  Serial.println("[INFO] Receiver Initialized");
}

/**
 * @brief 수신 루프 함수로, 도착한 CAN 메시지를 확인하고 Mini-MAC 태그를
 * 검증합니다.
 *
 * 주기적으로 새로운 CAN 메시지의 도착 여부를 확인합니다. 대기 중 메시지가
 * 없으면 함수를 빠져나와 다음 주기를 준비합니다. 메시지가 수신되면 ID와 데이터
 * 길이를 읽은 후, 해당 ID가 보호 대상(PROTECTED_ID)인지 및 데이터 길이가 태그
 * 길이 이상인지 검사합니다. 보호 대상 ID가 아니거나 길이가 짧으면 해당 메시지를
 * 무시합니다. 올바른 메시지인 경우 데이터 버퍼를 페이로드와 태그로 분리합니다.
 * 분리한 페이로드와 수신 태그를 HEX 형식으로 시리얼 모니터에 출력하여 디버깅
 * 정보를 제공합니다. 마지막으로 minimac_verify 함수를 호출하여 태그의 유효성을
 * 검사하고, 인증이 성공하면 "[INFO] Auth OK", 실패하면 "[ERROR] Auth FAIL"을
 * 출력합니다.
 */
void loop() {
  // 메시지 도착 체크
  if (CAN.checkReceive() != CAN_MSGAVAIL) {
    delay(10);
    return;
  }

  // 메시지 읽기
  unsigned long rxId;
  uint8_t len;
  uint8_t buf[MINIMAC_MAX_DATA + MINIMAC_TAG_LEN];
  CAN.readMsgBuf(&rxId, &len, buf);

  Serial.print("[DBG] CAN received ID=0x");
  Serial.print(rxId, HEX);
  Serial.print(" len=");
  Serial.println(len);

  // ID 검증
  if (rxId != PROTECTED_ID) {
    Serial.println("[DBG] Ignored (unprotected ID)");
    return;
  }
  if (len < MINIMAC_TAG_LEN) {
    Serial.println("[ERROR] Frame too short");
    return;
  }

  // 페이로드/태그 분리
  uint8_t payloadLen = len - MINIMAC_TAG_LEN;
  uint8_t payload[MINIMAC_MAX_DATA];
  uint8_t tag[MINIMAC_TAG_LEN];
  memcpy(payload, buf, payloadLen);
  memcpy(tag, buf + payloadLen, MINIMAC_TAG_LEN);

  // 디버그: payload
  Serial.print("[DBG] payload = ");
  for (uint8_t i = 0; i < payloadLen; i++) {
    if (payload[i] < 0x10)
      Serial.print('0');
    Serial.print(payload[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // 디버그: recv tag
  Serial.print("[DBG] recv tag = ");
  for (uint8_t i = 0; i < MINIMAC_TAG_LEN; i++) {
    if (tag[i] < 0x10)
      Serial.print('0');
    Serial.print(tag[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // 검증
  Serial.println("[DBG] minimac_verify()");
  if (minimac_verify(payload, payloadLen, tag)) {
    Serial.println("[INFO] Auth OK");
  } else {
    Serial.println("[ERROR] Auth FAIL");
  }
}
