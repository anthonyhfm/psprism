#include <cstdarg>
#include <cstdio>

#include "compat.h"

void av_log(int level, const char *fmt, ...) {
	(void)level;
	va_list vl;
	va_start(vl, fmt);
	(void)vsnprintf(nullptr, 0, fmt, vl);
	va_end(vl);
}
