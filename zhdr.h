/*
 * Compressed rpm headers
 * Written by Alexey Tourbin
 */
#include <lz4frame.h>

// Compressed headers are valid chunks which, when sequentially written
// to a file, produce a stream; the file can be later decompressed with
// a decompressor.
#define ZHDR_SUFFIX ".lz4"

// RPM header magic which headerUnload cannot make for us.
static unsigned char zhdr_magic[8] = {
    0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
};

// Small headers will be unloaded on the stack.
#define ZHDR_MAX_STACK (32 << 10)

static void *zhdr_do(Header h, char *buf, size_t size, size_t& zsize)
{
    void *blob = headerUnload(h);
    assert(blob);
    memcpy(buf, zhdr_magic, sizeof zhdr_magic);
    memcpy(buf + sizeof zhdr_magic, blob, size);
    free(blob);
    size += sizeof zhdr_magic;
    zsize = LZ4F_compressFrameBound(size, NULL);
    void *zblob = malloc(zsize);
    assert(zblob);
    zsize = LZ4F_compressFrame(zblob, zsize, buf, size, NULL);
    assert(!LZ4F_isError(zsize));
    // NB: zblob alloc size is suboptimal; the caller may want
    // to reallocate it or, better yet, to put it on a slab.
    return zblob;
}

// Compress with magic, to be written to pkglist.
static void *zhdr(Header h, size_t& zsize)
{
    size_t size = headerSizeof(h, HEADER_MAGIC_NO);
    assert(size > 0);
    void *zblob;
    if (size + sizeof zhdr_magic > ZHDR_MAX_STACK) {
	char *buf = (char *) malloc(size + sizeof zhdr_magic);
	assert(buf);
	zblob = zhdr_do(h, buf, size, zsize);
	free(buf);
    }
    else {
	char buf[size + sizeof zhdr_magic];
	zblob = zhdr_do(h, buf, size, zsize);
    }
    return zblob;
}

// Compress a few headers in a single chunk.
static void *zhdrv(std::vector<Header> const& hh, size_t& zsize)
{
    if (hh.size() == 1)
	return zhdr(hh[0], zsize);
    assert(hh.size() > 1);
    std::vector<size_t> ss;
    ss.reserve(hh.size());
    size_t ssum = hh.size() * sizeof zhdr_magic;
    for (size_t i = 0; i < hh.size(); i++) {
	size_t size = headerSizeof(hh[i], HEADER_MAGIC_NO);
	ss[i] = size;
	ssum += size;
    }
    char *bb = (char *) malloc(ssum);
    assert(bb);
    char *pp = bb;
    for (size_t i = 0; i < hh.size(); i++) {
	void *blob = headerUnload(hh[i]);
	assert(blob);
	memcpy(pp, zhdr_magic, sizeof zhdr_magic);
	memcpy(pp + sizeof zhdr_magic, blob, ss[i]);
	free(blob);
	pp += ss[i] + sizeof zhdr_magic;
    }
    assert(pp == bb + ssum);
    zsize = LZ4F_compressFrameBound(ssum, NULL);
    void *zblob = malloc(zsize);
    assert(zblob);
    zsize = LZ4F_compressFrame(zblob, zsize, bb, ssum, NULL);
    assert(!LZ4F_isError(zsize));
    free(bb);
    // NB: zblob alloc size is suboptimal, see above
    return zblob;
}
