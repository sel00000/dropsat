#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

/* CRC-16/CCITT-FALSE: 다항식 0x1021, 초기값 0xFFFF, 반사 없음 */
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

#endif
