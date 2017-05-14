// Copyright (c) 2017 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

// Writes exactly size bytes.
static bool xwrite(int fd, const void *buf, size_t size)
{
    assert(size);
    bool zero = false;
    do {
	ssize_t ret = write(fd, buf, size);
	if (ret < 0) {
	    if (errno == EINTR)
		continue;
	    return false;
	}
	if (ret == 0) {
	    if (zero) {
		// write(2) keeps returning zero
		errno = EAGAIN;
		return false;
	    }
	    zero = true;
	    continue;
	}
	zero = false;
	assert(ret <= size);
	buf = (char *) buf + ret;
	size -= ret;
    } while (size);

    return true;
}

#include <stdio.h> // sys_errlist

// A thread-safe strerror(3) replacement.
static const char *xstrerror(int errnum)
{
    // Some of the great minds say that sys_errlist is deprecated.
    // Well, at least it's thread-safe, and it does not deadlock.
    if (errnum > 0 && errnum < sys_nerr)
	return sys_errlist[errnum];
    return "Unknown error";
}

// Helpers to fill err[2] arg.
#define ERRNO(func) err[0] = func, err[1] = xstrerror(errno)
#define ERRLZ4(func, ret) err[0] = func, err[1] = LZ4F_getErrorName(ret)
#define ERRSTR(str) err[0] = __func__, err[1] = str

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <lz4frame.h>
#include "lz4writer.h"

struct lz4writer {
    int fd;
    bool error;
    bool writeContentSize;
    uint64_t contentSize;
    off_t pos0;
    LZ4F_compressionContext_t zctx;
    unsigned char frameHeader[LZ4F_HEADER_SIZE_MAX];
    size_t zbufSize;
    unsigned char zbuf[];
};

// Big inputs are processed in 256K chunks.
#define CHUNK (256 << 10)

struct lz4writer *lz4writer_fdopen(int fd, bool writeContentSize, bool writeChecksum, const char *err[2])
{
    LZ4F_preferences_t pref;
    memset(&pref, 0, sizeof pref);
    pref.frameInfo.blockSizeID = LZ4F_max256KB;
    pref.frameInfo.contentChecksumFlag = writeChecksum;

    size_t zbufSize = LZ4F_compressBound(CHUNK, &pref);
    assert(!LZ4F_isError(zbufSize));

    struct lz4writer *zw = malloc(sizeof *zw + zbufSize);
    if (!zw)
	return ERRNO("malloc"), NULL;

    memset(zw, 0, sizeof *zw);
    zw->fd = fd;
    zw->zbufSize = zbufSize;

    if (writeContentSize) {
	zw->writeContentSize = true;
	zw->pos0 = lseek(fd, 0, SEEK_CUR);
	if (zw->pos0 == (off_t) -1)
	    return ERRNO("lseek"),
		   free(zw), NULL;
    }

    size_t ret = LZ4F_createCompressionContext(&zw->zctx, LZ4F_VERSION);
    if (LZ4F_isError(ret))
	return ERRLZ4("LZ4F_createCompressionContext", ret),
	       free(zw), NULL;

    size_t zsize = LZ4F_compressBegin(zw->zctx, zw->frameHeader, sizeof zw->frameHeader, &pref);
    if (LZ4F_isError(zsize))
	return ERRLZ4("LZ4F_compressBegin", zsize),
	       LZ4F_freeCompressionContext(zw->zctx), free(zw), NULL;

    // Content zsize is not known yet.
    assert(zsize == sizeof zw->frameHeader - 8);
    zsize += writeContentSize * 8;

    if (!xwrite(fd, zw->frameHeader, zsize))
	return ERRNO("write"),
	       LZ4F_freeCompressionContext(zw->zctx), free(zw), NULL;

    return zw;
}

bool lz4writer_write(struct lz4writer *zw, const void *buf, size_t size, const char *err[2])
{
    if (zw->error)
	return ERRSTR("previous write failed"), false;

    while (size) {
	size_t chunk = size < CHUNK ? size : CHUNK;
	size_t zsize = LZ4F_compressUpdate(zw->zctx, zw->zbuf, zw->zbufSize, buf, chunk, NULL);
	size -= chunk, buf = (const char *) buf + chunk;
	if (zsize == 0)
	    continue;
	if (LZ4F_isError(zsize))
	    return ERRLZ4("LZ4F_compressUpdate", zsize),
		   zw->error = true, false;
	if (!xwrite(zw->fd, zw->zbuf, zsize))
	    return ERRNO("write"),
		   zw->error = true, false;
    }
    return true;
}

static void justClose(struct lz4writer *zw)
{
    close(zw->fd);
    LZ4F_freeCompressionContext(zw->zctx);
    free(zw);
}

#include "lz4fix.h"

bool lz4writer_close(struct lz4writer *zw, const char *err[2])
{
    if (zw->error)
	return ERRSTR("previous write failed"),
	       justClose(zw), false;

    size_t zsize = LZ4F_compressEnd(zw->zctx, zw->zbuf, zw->zbufSize, NULL);
    if (LZ4F_isError(zsize))
	return ERRLZ4("LZ4F_compressEnd", zsize),
	       LZ4F_freeCompressionContext(zw->zctx), free(zw), NULL;
    assert(zsize);

    if (!xwrite(zw->fd, zw->zbuf, zsize))
	return ERRNO("write"),
	       justClose(zw), false;

    if (!zw->writeContentSize)
	return justClose(zw), true;

    off_t pos = lseek(zw->fd, zw->pos0, SEEK_SET);
    if (pos == (off_t) -1)
	return ERRNO("lseek"),
	       justClose(zw), false;
    assert(pos == zw->pos0);

    if (!lz4fix(zw->frameHeader, zw->contentSize))
	return ERRSTR("cannot fix lz4 frame header"),
	       justClose(zw), false;

    if (!xwrite(zw->fd, zw->frameHeader, sizeof zw->frameHeader))
	return ERRNO("write"),
	       justClose(zw), false;

    return justClose(zw), true;
}

// ex:set ts=8 sts=4 sw=4 noet:
