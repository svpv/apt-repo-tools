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

// Compress a few headers in a single chunk.
// Headers have magic, to be written to pkglist.
static void *zhdrv(std::vector<Header> const& hh, size_t& zsize)
{
    assert(hh.size() >= 1);
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
    LZ4F_preferences_t pref;
    memset(&pref, 0, sizeof pref);
    pref.frameInfo.blockSizeID = LZ4F_max256KB;
    pref.frameInfo.contentSize = ssum;
    zsize = LZ4F_compressFrameBound(ssum, &pref);
    void *zblob = malloc(zsize);
    assert(zblob);
    zsize = LZ4F_compressFrame(zblob, zsize, bb, ssum, &pref);
    assert(!LZ4F_isError(zsize));
    free(bb);
    // NB: zblob alloc size is suboptimal; the caller may want
    // to reallocate it or, better yet, to put it on a slab.
    return zblob;
}

#include <arpa/inet.h>

// Decompress the headers.
static void unzhdrv(std::vector<Header>& hh, const void *zblob, size_t zsize)
{
    LZ4F_decompressionContext_t dctx;
    size_t ret = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    assert(!LZ4F_isError(ret));
    LZ4F_frameInfo_t frameInfo;
    size_t zread = zsize;
    ret = LZ4F_getFrameInfo(dctx, &frameInfo, zblob, &zread);
    assert(!LZ4F_isError(ret));
    zblob = (char *) zblob + zread, zsize -= zread;
    size_t blobsize = frameInfo.contentSize;
    assert(blobsize);
    void *blob = malloc(blobsize);
    assert(blob);
    zread = zsize;
    // The docs say that LZ4F_decompress should be called in a loop.  However,
    // this is only useful for piecemeal decompression.  LZ4F_decompress also
    // seems to be able to decompress the whole thing at once.
    ret = LZ4F_decompress(dctx, blob, &blobsize, zblob, &zread, NULL);
    assert(ret == 0);
    assert(blobsize == frameInfo.contentSize);
    char *p = (char *) blob;
    do {
	assert(blobsize > sizeof zhdr_magic);
	assert(memcmp(p, zhdr_magic, sizeof zhdr_magic) == 0);
	p += sizeof zhdr_magic, blobsize -= sizeof zhdr_magic;
	Header h = headerImport(p, 0, HEADERIMPORT_COPY | HEADERIMPORT_FAST);
	assert(h);
	hh.push_back(h);
	// headerSizeof won't work here
	unsigned *ei = (unsigned *) p;
	unsigned il = ntohl(ei[0]);
	unsigned dl = ntohl(ei[1]);
	size_t hsize = 8 + 16 * il + dl;
	assert(hsize <= blobsize);
	p += hsize, blobsize -= hsize;
    } while (blobsize);
    free(blob);
    LZ4F_freeDecompressionContext(dctx);
}
