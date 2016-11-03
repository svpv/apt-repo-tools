/*
 * One-way slab allocator
 * Writtern by Alexey Tourbin
 */
class Slab
{
    // this constant has been profiled to minimize RSS usage
    static const size_t slab_size = 4 << 20;
    char *slab;
    size_t fill;
public:
    Slab() : slab(NULL), fill(0) { }
    void *put(const void *data, size_t size)
    {
	if (slab == NULL || fill + size > slab_size) {
	    slab = new char[size > slab_size ? size : slab_size];
	    fill = 0;
	}
	void *ret = memcpy(slab + fill, data, size);
	fill += size;
	return ret;
    }
    char *strdup(const char *s)
    {
	size_t len = strlen(s);
	return (char *) put(s, len + 1);
    }
};
