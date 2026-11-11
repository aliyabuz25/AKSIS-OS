#ifndef SHENLONG_STDDEF_H
#define SHENLONG_STDDEF_H
#include "kernel.h"
typedef long long ptrdiff_t;
#define offsetof(type,member) __builtin_offsetof(type,member)
#endif
