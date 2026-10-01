/*
 * 명령 인증 키 (128비트)
 *
 * ⚠ 시험용 키다. 실제 비행 모델에서는 이 파일을 쓰지 말고, 통합 직전에 FRAM 보안 영역에
 *   키를 주입한 뒤 hal에서 읽어 오게 바꿔야 한다. 키가 저장소(git)에 올라가면 안 된다.
 */
#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>

static const uint8_t CMD_KEY[16] = {
    0x44, 0x52, 0x4F, 0x50, 0x53, 0x41, 0x54, 0x2D,   /* "DROPSAT-" */
    0x54, 0x45, 0x53, 0x54, 0x4B, 0x45, 0x59, 0x31    /* "TESTKEY1" */
};

#endif
