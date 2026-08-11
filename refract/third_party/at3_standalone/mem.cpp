/*
 * default memory allocator for libavutil
 * Copyright (c) 2002 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * default memory allocator for libavutil
 */

#define _XOPEN_SOURCE 600

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "intreadwrite.h"
#include "mem.h"

 /**
  * Multiply two size_t values checking for overflow.
  * @return  0 if success, AVERROR(EINVAL) if overflow.
  */
static inline int av_size_mult(size_t a, size_t b, size_t *r) {
    size_t t = a * b;
    /* Hack inspired from glibc: only try the division if nelem and elsize
     * are both greater than sqrt(SIZE_MAX). */
    if ((a | b) >= ((size_t)1 << (sizeof(size_t) * 4)) && a && t / a != b)
        return AVERROR(EINVAL);
    *r = t;
    return 0;
}

void *av_malloc(size_t size) {
    void *ptr = NULL;
    const size_t allocation_size = size ? size : 1;
#if defined(_MSC_VER)
    ptr = _aligned_malloc(allocation_size, 32);
#else
    if (posix_memalign(&ptr, 32, allocation_size) != 0)
        ptr = NULL;
#endif
    if (!ptr && !size) {
        // Compensate for platforms that don't allow zero-size allocations (not sure if this is actually an issue)
        return av_malloc(1);
    }
    return ptr;
}

void av_free(void *ptr) {
#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void av_freep(void *arg) {
    void *val;
    memcpy(&val, arg, sizeof(val));
    memset(arg, 0, sizeof(val));
    av_free(val);
}

void *av_mallocz(size_t size) {
    void *ptr = av_malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}
