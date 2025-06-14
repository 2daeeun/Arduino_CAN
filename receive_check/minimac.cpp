

#include "minimac.h"

static const int SIG_ADDR = 0;
static const uint32_t SIGVAL = 0xAA55AA55;
static const int DATA_ADDR = SIG_ADDR + sizeof(SIGVAL);

static uint16_t mm_id;
static uint8_t mm_key[MINIMAC_KEY_LEN];
static uint64_t mm_counter;
static MiniMacHist mm_hist[MINIMAC_HIST_LEN];
static uint8_t mm_hist_cnt;

static void debug_print_hex(const uint8_t *buf, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    if (buf[i] < 0x10)
      Serial.print('0');
    Serial.print(buf[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

static void print_u64(uint64_t v) {
  if (v == 0) {
    Serial.print('0');
    return;
  }
  char buf[21];
  buf[20] = '\0';
  int pos = 19;
  while (v > 0 && pos >= 0) {
    buf[pos--] = '0' + (v % 10);
    v /= 10;
  }
  Serial.print(&buf[pos + 1]);
}

static void compute_digest(const uint8_t *data, uint8_t len,
                           unsigned char digest[16]) {

  uint16_t buf_len = 8 + 2;
  for (uint8_t i = 0; i < mm_hist_cnt; i++)
    buf_len += mm_hist[i].len;
  buf_len += len;

  uint8_t *buf = (uint8_t *)malloc(buf_len);
  uint16_t off = 0;

  Serial.print("[DBG] counter = ");
  print_u64(mm_counter);
  Serial.println();

  uint64_t tmp = mm_counter;
  for (int i = 7; i >= 0; i--) {
    buf[off + i] = tmp & 0xFF;
    tmp >>= 8;
  }
  off += 8;

  buf[off++] = mm_id >> 8;
  buf[off++] = mm_id & 0xFF;
  Serial.print("[DBG] CAN ID = 0x");
  Serial.println(mm_id, HEX);

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

  Serial.print("[DBG] current_data = ");
  debug_print_hex(data, len);

  memcpy(buf + off, data, len);
  off += len;

  MD5 hasher;
  hasher.hmac_md5(buf, off, (void *)mm_key, MINIMAC_KEY_LEN, digest);

  Serial.print("[DBG] raw MD5 = ");
  debug_print_hex(digest, 16);

  free(buf);
}

static bool load_state(void) {
  uint32_t sig;

  EEPROM.get(SIG_ADDR, sig);
  if (sig != SIGVAL)
    return false;

  EEPROM.get(DATA_ADDR, mm_counter);
  EEPROM.get(DATA_ADDR + sizeof(mm_counter), mm_hist_cnt);

  int addr = DATA_ADDR + sizeof(mm_counter) + sizeof(mm_hist_cnt);
  for (uint8_t i = 0; i < mm_hist_cnt; i++) {

    EEPROM.get(addr, mm_hist[i].len);
    addr += sizeof(mm_hist[i].len);

    EEPROM.get(addr, mm_hist[i].data);
    addr += MINIMAC_MAX_DATA;
  }

  Serial.println("[DBG] load_state: loaded from EEPROM");
  Serial.print("  counter = ");
  print_u64(mm_counter);
  Serial.println();
  Serial.print("  history_count = ");
  Serial.println(mm_hist_cnt);

  return true;
}

static void save_state(void) {

  EEPROM.put(SIG_ADDR, SIGVAL);

  EEPROM.put(DATA_ADDR, mm_counter);
  EEPROM.put(DATA_ADDR + sizeof(mm_counter), mm_hist_cnt);

  int addr = DATA_ADDR + sizeof(mm_counter) + sizeof(mm_hist_cnt);
  for (uint8_t i = 0; i < mm_hist_cnt; i++) {

    EEPROM.put(addr, mm_hist[i].len);
    addr += sizeof(mm_hist[i].len);

    EEPROM.put(addr, mm_hist[i].data);
    addr += MINIMAC_MAX_DATA;
  }

  Serial.println("[DBG] save_state: saved to EEPROM");
  Serial.print("  counter = ");
  print_u64(mm_counter);
  Serial.println();
  Serial.print("  history_count = ");
  Serial.println(mm_hist_cnt);
}

void minimac_init(uint16_t can_id, const uint8_t *key) {

  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("[DBG] minimac_init()");

  mm_id = can_id;

  memcpy(mm_key, key, MINIMAC_KEY_LEN);

  if (!load_state()) {

    Serial.println("[DBG] minimac_init: no EEPROM state, initialize fresh");

    mm_counter = 0;

    mm_hist_cnt = 0;

    save_state();
  }
}

uint8_t minimac_sign(uint8_t *data, uint8_t payload_len) {

  Serial.println("[DBG] minimac_sign()");

  unsigned char digest[16];
  compute_digest(data, payload_len, digest);

  Serial.print("[DBG] sign: tag = ");
  debug_print_hex(digest, MINIMAC_TAG_LEN);

  memcpy(data + payload_len, digest, MINIMAC_TAG_LEN);
  uint8_t total = payload_len + MINIMAC_TAG_LEN;

  if (mm_hist_cnt == MINIMAC_HIST_LEN) {
    Serial.println("[DBG] sign: history full, dropping oldest");

    for (uint8_t i = 1; i < mm_hist_cnt; i++)
      mm_hist[i - 1] = mm_hist[i];
    mm_hist_cnt--;
  }

  mm_hist[mm_hist_cnt].len = payload_len;
  memcpy(mm_hist[mm_hist_cnt].data, data, payload_len);
  mm_hist_cnt++;
  Serial.print("[DBG] sign: new history_count = ");
  Serial.println(mm_hist_cnt);

  mm_counter++;
  Serial.print("[DBG] sign: new counter = ");
  print_u64(mm_counter);
  Serial.println();

  save_state();

  return total;
}

bool minimac_verify(const uint8_t *data, uint8_t payload_len,
                    const uint8_t *tag) {

  Serial.println("[DBG] minimac_verify()");

  unsigned char digest[16];
  compute_digest(data, payload_len, digest);

  Serial.print("[DBG] verify: expected tag = ");
  debug_print_hex(digest, MINIMAC_TAG_LEN);
  Serial.print("[DBG] verify: recv    tag = ");
  debug_print_hex(tag, MINIMAC_TAG_LEN);

  if (memcmp(digest, tag, MINIMAC_TAG_LEN) != 0) {
    Serial.println("[DBG] verify: FAILED");
    return false;
  }

  if (mm_hist_cnt == MINIMAC_HIST_LEN) {
    Serial.println("[DBG] verify: history full, dropping oldest");
    for (uint8_t i = 1; i < mm_hist_cnt; i++)
      mm_hist[i - 1] = mm_hist[i];
    mm_hist_cnt--;
  }

  mm_hist[mm_hist_cnt].len = payload_len;
  memcpy(mm_hist[mm_hist_cnt].data, data, payload_len);
  mm_hist_cnt++;
  Serial.print("[DBG] verify: new history_count = ");
  Serial.println(mm_hist_cnt);

  mm_counter++;
  Serial.print("[DBG] verify: new counter = ");
  print_u64(mm_counter);
  Serial.println();

  save_state();

  Serial.println("[DBG] verify: SUCCESS");
  return true;
}
