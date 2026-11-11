#ifndef SHENLONG_STDINT_H
#define SHENLONG_STDINT_H
#include "kernel.h"
#define UINT8_MAX 255u
#define UINT16_MAX 65535u
#define UINT32_MAX 0xffffffffu
#define UINT64_MAX 0xffffffffffffffffULL
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647-1)
#define INT64_MAX 9223372036854775807LL
#define UINT32_C(x) x##U
#define UINT64_C(x) x##ULL
#define INT32_C(x) x
#define INT64_C(x) x##LL
#define SIZE_MAX UINT64_MAX
#endif
