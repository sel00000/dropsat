#ifndef SIPHASH_H
#define SIPHASH_H

#include <stddef.h>
#include <stdint.h>

/*
 * SipHash-2-4 (Aumasson & Bernstein, 2012)
 * 128비트 키로 짧은 메시지의 64비트 인증 태그(MAC)를 만든다. 명령 프레임 위조 방지에 쓴다.
 * 명령 내용은 공개돼도 문제없고 위조·재전송만 막으면 되므로 암호화 없이 인증만 한다.
 * (ITU 전파규칙 25.2A: 아마추어 위성의 제어 신호는 암호화가 허용되지만 텔레메트리는 공개해야 한다)
 */
uint64_t siphash24(const uint8_t key[16], const uint8_t *msg, size_t len);

#endif
