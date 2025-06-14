/**
 * @file minimac.h
 * @brief Mini-MAC 프로토콜 구현용 헤더 파일
 *
 * Mini-MAC은 CAN 버스 환경에서 경량화된 메시지 인증과 재생 공격 방어를 제공하는
 * HMAC 기반 프로토콜입니다. 본 헤더에는 키/태그 길이, 메시지 히스토리 정의와
 * 초기화·서명·검증 함수의 인터페이스가 포함되어 있습니다.
 */
#ifndef MINIMAC_H
#define MINIMAC_H

#include <Arduino.h>
#include <EEPROM.h>
#include <MD5.h> /**< ArduinoMD5 라이브러리 사용 */

//=== 설정 상수 ===
/** @def MINIMAC_KEY_LEN
 *  @brief Mini-MAC HMAC 키 길이 (16바이트, 128비트)
 */
#define MINIMAC_KEY_LEN 16

/** @def MINIMAC_TAG_LEN
 *  @brief Mini-MAC 다이제스트에서 사용할 태그 길이 (4바이트, 32비트)
 */
#define MINIMAC_TAG_LEN 4

/** @def MINIMAC_HIST_LEN
 *  @brief 메시지 히스토리 최대 개수 (λ = 5)
 */
#define MINIMAC_HIST_LEN 5

/** @def MINIMAC_MAX_DATA
 *  @brief CAN 데이터 필드 최대 길이 (8바이트)
 */
#define MINIMAC_MAX_DATA 8

/**
 * @struct MiniMacHist
 * @brief 과거 페이로드를 저장하기 위한 구조체
 *
 * @var MiniMacHist::len   저장된 페이로드 길이
 * @var MiniMacHist::data  페이로드 데이터(최대 8바이트)
 */
typedef struct {
  uint8_t len;                    /**< 페이로드 길이 (바이트) */
  uint8_t data[MINIMAC_MAX_DATA]; /**< 페이로드 데이터 버퍼 */
} MiniMacHist;

/**
 * @brief Mini-MAC 프로토콜 초기화
 * @param can_id 보호할 CAN 메시지 식별자 (16비트)
 * @param key    그룹 키 (128비트, 16바이트)
 *
 * EEPROM에서 이전 상태를 불러오고, 유효하지 않으면 내부 카운터와
 * 히스토리를 초기화하여 fresh 상태로 설정합니다.
 */
void minimac_init(uint16_t can_id, const uint8_t *key);

/**
 * @brief 송신 전 페이로드에 Mini-MAC 태그 생성 및 붙이기
 * @param data         서명할 페이로드 버퍼
 * @param payload_len  페이로드 길이(바이트)
 * @return 전체 데이터 길이 (payload_len + MINIMAC_TAG_LEN)
 *
 * data[0..payload_len-1] 구간을 포함해 HMAC-MD5를 수행하고,
 * 상위 4바이트를 태그로 data[payload_len..]에 덧붙입니다.
 * 내부 카운터와 히스토리를 갱신한 후 EEPROM에 저장합니다.
 */
uint8_t minimac_sign(uint8_t *data, uint8_t payload_len);

/**
 * @brief 수신 후 Mini-MAC 태그 검증 및 내부 상태 갱신
 * @param data         검증할 페이로드 버퍼
 * @param payload_len  페이로드 길이(바이트)
 * @param tag          수신된 태그 버퍼 (MINIMAC_TAG_LEN 바이트)
 * @return true  검증 성공 (내부 상태 갱신 및 EEPROM 저장)
 * @return false 검증 실패 (TAG 불일치)
 *
 * data와 tag를 이용해 HMAC-MD5 다이제스트를 재계산하고,
 * 다이제스트 상위 4바이트와 비교합니다. 성공 시 내부 카운터와
 * 히스토리를 업데이트합니다.
 */
bool minimac_verify(const uint8_t *data, uint8_t payload_len,
                    const uint8_t *tag);

#endif // MINIMAC_H
