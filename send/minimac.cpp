/**
 * @file minimac.cpp
 * @brief Mini-MAC 프로토콜 구현 (EEPROM 상태 관리, HMAC-MD5 기반 MAC 생성/검증)
 */

#include "minimac.h"

/// EEPROM 레이아웃: 시그니처 및 데이터 저장 시작 주소
static const int    SIG_ADDR   = 0;
static const uint32_t SIGVAL   = 0xAA55AA55;
static const int    DATA_ADDR  = SIG_ADDR + sizeof(SIGVAL);

/// 보호할 CAN ID, 그룹 키, 카운터, 메시지 히스토리
static uint16_t    mm_id;                        ///< CAN ID (그룹 식별자)
static uint8_t     mm_key[MINIMAC_KEY_LEN];      ///< 공유 그룹 키
static uint64_t    mm_counter;                   ///< 64비트 메시지 카운터
static MiniMacHist mm_hist[MINIMAC_HIST_LEN];    ///< 최근 λ개 메시지 히스토리
static uint8_t     mm_hist_cnt;                  ///< 히스토리 항목 수 (≤ λ)

/**
 * @brief 디버깅용: 바이트 배열을 16진수로 출력
 * @param buf   출력할 바이트 배열
 * @param len   배열 길이(Byte)
 */
static void debug_print_hex(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) Serial.print('0');
        Serial.print(buf[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}

/**
 * @brief 디버깅용: 64비트 부호 없는 정수를 10진수 문자열로 변환해 출력
 * @param v     변환할 64비트 값
 */
static void print_u64(uint64_t v)
{
    if (v == 0) {
        Serial.print('0');
        return;
    }
    char buf[21];      // 최대 20자리 숫자 + 널 종료
    buf[20] = '\0';
    int pos = 19;
    while (v > 0 && pos >= 0) {
        buf[pos--] = '0' + (v % 10);
        v /= 10;
    }
    Serial.print(&buf[pos + 1]);
}

/**
 * @brief Mini-MAC용 HMAC-MD5 다이제스트 계산
 * @param data    서명할 페이로드 데이터 버퍼
 * @param len     페이로드 길이(Byte)
 * @param digest  결과 다이제스트 저장 버퍼(16바이트)
 *
 * 메시지 카운터(mm_counter), CAN ID(mm_id), 최근 메시지 히스토리(mm_hist),
 * 그리고 현재 페이로드(data)를 하나의 연속 버퍼에 결합한 후,
 * HMAC-MD5를 수행하여 16바이트 다이제스트를 생성한다.
 * 각 단계별 내부 상태는 Serial 디버그 출력으로 확인 가능하다.
 */
static void compute_digest(const uint8_t *data, uint8_t len, unsigned char digest[16])
{
    /* (1) 버퍼 크기 계산:
     *     - 메시지 카운터(mm_counter, 8바이트)
     *     - CAN ID(mm_id, 2바이트)
     *     - 과거 메시지 히스토리(mm_hist_cnt개, 각 항목 len 바이트)
     *     - 현재 페이로드(data, len 바이트)
     *   위 항목의 총합(buf_len)을 계산한 뒤, 이를 저장할 버퍼(buf)를 동적 할당.
     *   off 변수는 buf 내 현재 쓰기 위치를 나타냄.
     */
    uint16_t buf_len = 8 + 2;
    for (uint8_t i = 0; i < mm_hist_cnt; i++)
        buf_len += mm_hist[i].len;
    buf_len += len;

    uint8_t *buf = (uint8_t *)malloc(buf_len);
    uint16_t off = 0;

    /* (2) 카운터 삽입 (big-endian):
     *   - 64비트 카운터를 빅엔디안 순서로 buf[off..off+7]에 저장
     *   - Serial.print로 현재 카운터 값을 10진수 문자열로 출력
     */
    Serial.print("[DBG] counter = ");
    print_u64(mm_counter);
    Serial.println();

    uint64_t tmp = mm_counter;
    for (int i = 7; i >= 0; i--) {
        buf[off + i] = tmp & 0xFF;
        tmp >>= 8;
    }
    off += 8;

    /* (3) CAN ID 삽입:
     *   - mm_id 상위 바이트(buf[off])와 하위 바이트(buf[off+1])를 저장
     *   - Serial.print로 16진수 형태의 CAN ID 출력
     */
    buf[off++] = mm_id >> 8;
    buf[off++] = mm_id & 0xFF;
    Serial.print("[DBG] CAN ID = 0x");
    Serial.println(mm_id, HEX);

    /* (4) 메시지 히스토리 삽입:
     *   - 저장된 히스토리 개수(mm_hist_cnt)만큼 반복
     *   - 각 항목(mm_hist[i].data, length mm_hist[i].len)을 buf에 복사
     *   - debug_print_hex로 각 히스토리 데이터 덤프
     */
    Serial.print("[DBG] history_count = ");
    Serial.println(mm_hist_cnt);

    for (uint8_t i = 0; i < mm_hist_cnt; i++) {
        Serial.print("[DBG] hist[");
        Serial.print(i);
        Serial.print("] = ");
        debug_print_hex(mm_hist[i].data, mm_hist[i].len);

        memcpy(buf + off, mm_hist[i].data, mm_hist[i].len);
        off += mm_hist[i].len;
    }

    /* (5) 현재 페이로드 삽입:
     *   - data[0..len-1]를 buf에 연속 복사
     *   - debug_print_hex로 페이로드 덤프
     */
    Serial.print("[DBG] current_data = ");
    debug_print_hex(data, len);

    memcpy(buf + off, data, len);
    off += len;

    /* (6) HMAC-MD5 계산:
     *   - MD5.hmac_md5(buf, off, mm_key, MINIMAC_KEY_LEN, digest)를 호출하여
     *     전체 입력(buf, 길이 off)에 대한 HMAC-MD5 다이제스트 생성
     *   - debug_print_hex로 16바이트 raw MD5 덤프
     *   - 동적 할당된 buf 메모리 해제
     */
    MD5 hasher;
    hasher.hmac_md5(buf, off, (void *)mm_key, MINIMAC_KEY_LEN, digest);

    Serial.print("[DBG] raw MD5 = ");
    debug_print_hex(digest, 16);

    free(buf);
}

/**
 * @brief EEPROM에서 Mini-MAC 상태 불러오기
 *
 * EEPROM에 저장된 시그니처(SIGVAL)를 확인한 뒤,
 * 유효하면 mm_counter, mm_hist_cnt 및 메시지 히스토리 배열을 복원한다.
 *
 * @return true  EEPROM에 유효한 상태가 있어 복원 성공  
 * @return false 시그니처 불일치로 초기화가 필요함  
 */
static bool load_state(void)
{
    uint32_t sig;

    /* (1) 시그니처 확인 */
    EEPROM.get(SIG_ADDR, sig);
    if (sig != SIGVAL)
        return false;

    /* (2) 카운터 및 히스토리 개수 복원 */
    EEPROM.get(DATA_ADDR,                     mm_counter);
    EEPROM.get(DATA_ADDR + sizeof(mm_counter), mm_hist_cnt);

    /* (3) 히스토리 항목 복원 */
    int addr = DATA_ADDR + sizeof(mm_counter)
                      + sizeof(mm_hist_cnt);
    for (uint8_t i = 0; i < mm_hist_cnt; i++) {
        /* (3a) 각 히스토리 길이 로드 */
        EEPROM.get(addr, mm_hist[i].len);
        addr += sizeof(mm_hist[i].len);

        /* (3b) 고정 크기 버퍼에 과거 페이로드 데이터 로드 */
        EEPROM.get(addr, mm_hist[i].data);
        addr += MINIMAC_MAX_DATA;
    }

    /* (4) 디버그 출력으로 복원된 상태 확인 */
    Serial.println("[DBG] load_state: loaded from EEPROM");
    Serial.print("  counter = ");
    print_u64(mm_counter);
    Serial.println();
    Serial.print("  history_count = ");
    Serial.println(mm_hist_cnt);

    return true;
}

/**
 * @brief Mini-MAC 상태를 EEPROM에 저장
 *
 * 현재 mm_counter, mm_hist_cnt 및 메시지 히스토리 배열을
 * EEPROM에 시그니처와 함께 순차 기록하여 재부팅 시에도 상태 유지.
 */
static void save_state(void)
{
    /* (1) 시그니처 기록 */
    EEPROM.put(SIG_ADDR, SIGVAL);

    /* (2) 카운터 및 히스토리 개수 기록 */
    EEPROM.put(DATA_ADDR,                       mm_counter);
    EEPROM.put(DATA_ADDR + sizeof(mm_counter),  mm_hist_cnt);

    /* (3) 히스토리 항목 기록 */
    int addr = DATA_ADDR + sizeof(mm_counter)
                      + sizeof(mm_hist_cnt);
    for (uint8_t i = 0; i < mm_hist_cnt; i++) {
        /* (3a) 각 히스토리 길이 저장 */
        EEPROM.put(addr, mm_hist[i].len);
        addr += sizeof(mm_hist[i].len);

        /* (3b) 고정 크기 버퍼에 과거 페이로드 데이터 저장 */
        EEPROM.put(addr, mm_hist[i].data);
        addr += MINIMAC_MAX_DATA;
    }

    /* (4) 디버그 출력으로 저장된 상태 확인 */
    Serial.println("[DBG] save_state: saved to EEPROM");
    Serial.print("  counter = ");
    print_u64(mm_counter);
    Serial.println();
    Serial.print("  history_count = ");
    Serial.println(mm_hist_cnt);
}

/**
 * @brief Mini-MAC 초기화 및 EEPROM 동기화
 * @param can_id 보호할 CAN 메시지 식별자 (16비트)
 * @param key    Mini-MAC HMAC 키 (128비트, 16바이트)
 *
 * Serial 포트를 115200bps로 초기화하고, mm_id와 mm_key 전역 변수에
 * 인자를 설정한다. EEPROM에서 이전에 저장된 mm_counter와 메시지
 * 히스토리를 불러오되(load_state()), 저장된 시그니처가 없으면
 * fresh 상태로 간주하여 mm_counter와 mm_hist_cnt를 0으로 초기화한
 * 뒤(save_state()), EEPROM에 초기 상태를 기록한다.
 * 디버그용으로 Serial.print를 통해 초기화 과정을 출력한다.
 */
void minimac_init(uint16_t can_id, const uint8_t *key)
{
    /* Serial 초기화: 디버그 출력용 */
    Serial.begin(115200);
    while (!Serial)
        /* 시리얼 포트가 준비될 때까지 대기 */;
    Serial.println("[DBG] minimac_init()");

    /* (1) CAN ID 설정: 보호할 그룹 식별자 */
    mm_id = can_id;

    /* (2) 그룹 키 복사: 16바이트 비밀키 */
    memcpy(mm_key, key, MINIMAC_KEY_LEN);

    /* (3) EEPROM에서 이전 상태 불러오기 */
    if (!load_state()) {
        /* EEPROM에 유효한 시그니처 없음: fresh 초기화 */
        Serial.println("[DBG] minimac_init: no EEPROM state, initialize fresh");

        /* (3a) 카운터 초기화 */
        mm_counter   = 0;

        /* (3b) 히스토리 개수 초기화 */
        mm_hist_cnt  = 0;

        /* (3c) 초기 상태 EEPROM에 저장 */
        save_state();
    }
}

/**
 * @brief 송신할 메시지에 Mini-MAC 태그 생성 및 내부 상태 갱신
 * @param data        서명할 페이로드 버퍼, 호출 후 buf[payload_len..] 위치에 태그가 덧붙여짐
 * @param payload_len 페이로드 길이(Byte)
 * @return 전체 전송 길이 (payload_len + MINIMAC_TAG_LEN)
 *
 * 전달받은 페이로드(data, payload_len)를 바탕으로 HMAC-MD5 다이제스트를 계산하여
 * 상위 4바이트(tag)를 data 뒤에 덧붙인다. 이후 메시지 히스토리(mm_hist)와
 * 메시지 카운터(mm_counter)를 갱신하고 EEPROM에 저장(save_state)한다.
 */
uint8_t minimac_sign(uint8_t *data, uint8_t payload_len)
{
    /* 디버그: 함수 진입 */
    Serial.println("[DBG] minimac_sign()");

    /* (1) HMAC 입력 구성 및 다이제스트 계산 */
    unsigned char digest[16];
    compute_digest(data, payload_len, digest);

    /* (2) 디버그: 생성된 다이제스트의 태그 부분 출력 */
    Serial.print("[DBG] sign: tag = ");
    debug_print_hex(digest, MINIMAC_TAG_LEN);

    /* (3) 태그(4바이트) 붙이기 */
    memcpy(data + payload_len, digest, MINIMAC_TAG_LEN);
    uint8_t total = payload_len + MINIMAC_TAG_LEN;

    /* (4) 메시지 히스토리 순환 버퍼 관리 */
    if (mm_hist_cnt == MINIMAC_HIST_LEN) {
        Serial.println("[DBG] sign: history full, dropping oldest");
        /* 가장 오래된 히스토리 항목 삭제 */
        for (uint8_t i = 1; i < mm_hist_cnt; i++)
            mm_hist[i - 1] = mm_hist[i];
        mm_hist_cnt--;
    }
    /* 새로운 페이로드를 히스토리에 추가 */
    mm_hist[mm_hist_cnt].len = payload_len;
    memcpy(mm_hist[mm_hist_cnt].data, data, payload_len);
    mm_hist_cnt++;
    Serial.print("[DBG] sign: new history_count = ");
    Serial.println(mm_hist_cnt);

    /* (5) 카운터 증가 및 디버그 출력 */
    mm_counter++;
    Serial.print("[DBG] sign: new counter = ");
    print_u64(mm_counter);
    Serial.println();

    /* (6) EEPROM에 상태 저장 */
    save_state();

    return total;
}

/**
 * @brief 수신된 메시지의 Mini-MAC 태그 검증 및 상태 동기화
 * @param data        검증할 페이로드 버퍼
 * @param payload_len 페이로드 길이(Byte)
 * @param tag         수신된 태그 버퍼 (MINIMAC_TAG_LEN 바이트)
 * @return true  검증 성공 및 내부 상태 갱신
 * @return false 검증 실패 (TAG 불일치)
 *
 * data와 tag를 기반으로 HMAC-MD5 다이제스트를 재계산하여 수신된
 * tag와 비교한다. 검증 성공 시 메시지 히스토리(mm_hist)와
 * 카운터(mm_counter)를 갱신하고 EEPROM에 저장(save_state)한 뒤
 * true를 반환한다. 실패 시 false 반환하며 상태는 갱신되지 않음.
 */
bool minimac_verify(const uint8_t *data, uint8_t payload_len, const uint8_t *tag)
{
    /* 디버그: 함수 진입 */
    Serial.println("[DBG] minimac_verify()");

    /* (1) HMAC 입력 구성 및 다이제스트 재계산 */
    unsigned char digest[16];
    compute_digest(data, payload_len, digest);

    /* (2) 디버그: 기대 태그(expected) 및 수신 태그(received) 출력 */
    Serial.print("[DBG] verify: expected tag = ");
    debug_print_hex(digest, MINIMAC_TAG_LEN);
    Serial.print("[DBG] verify: recv    tag = ");
    debug_print_hex(tag, MINIMAC_TAG_LEN);

    /* (3) 태그 비교: 불일치 시 실패 처리 */
    if (memcmp(digest, tag, MINIMAC_TAG_LEN) != 0) {
        Serial.println("[DBG] verify: FAILED");
        return false;
    }

    /* (4) 히스토리 순환 버퍼 관리 (가득 찼다면 가장 오래된 항목 삭제) */
    if (mm_hist_cnt == MINIMAC_HIST_LEN) {
        Serial.println("[DBG] verify: history full, dropping oldest");
        for (uint8_t i = 1; i < mm_hist_cnt; i++)
            mm_hist[i - 1] = mm_hist[i];
        mm_hist_cnt--;
    }

    /* (5) 성공 페이로드를 히스토리에 추가 */
    mm_hist[mm_hist_cnt].len = payload_len;
    memcpy(mm_hist[mm_hist_cnt].data, data, payload_len);
    mm_hist_cnt++;
    Serial.print("[DBG] verify: new history_count = ");
    Serial.println(mm_hist_cnt);

    /* (6) 카운터 증가 및 디버그 출력 */
    mm_counter++;
    Serial.print("[DBG] verify: new counter = ");
    print_u64(mm_counter);
    Serial.println();

    /* (7) EEPROM에 상태 저장 */
    save_state();

    Serial.println("[DBG] verify: SUCCESS");
    return true;
}