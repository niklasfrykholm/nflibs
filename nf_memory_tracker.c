// ## Includes

#include <stdint.h>

// ## Interface

struct nfmt_Buffer {
	uint8_t *start;
	uint8_t *end;
};

void nfmt_init();
void nfmt_record_malloc(void *p, uint32_t size, const char *tag, const char *file, int32_t line);
void nfmt_record_free(void *p);
struct nfmt_Buffer nfmt_read();

// # Implementation

#include <memory.h>

struct nfst_StringTable
{
    int32_t allocated_bytes;
    int32_t count;
    int32_t uses_16_bit_hash_slots;
    int32_t num_hash_slots;
    int32_t string_bytes;
};
void nfst_init(struct nfst_StringTable *st, int32_t bytes, int32_t average_string_size);
int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s);

#define STREAM_SIZE (16*1024)
#define STRING_TABLE_SIZE (2*1024)

#define MIN(a,b) ((a) < (b) ? (a) : (b))

enum {RECORD_TYPE_MALLOC, RECORD_TYPE_FREE, RECORD_TYPE_SYMBOL, RECORD_TYPE_OUT_OF_MEMORY};

struct Stream
{
	uint8_t buffer[STREAM_SIZE];
	int32_t start;
	int32_t size;
};

struct MallocRecord
{
	void *p;
	uint32_t size;
	int32_t tag_sym;
	int32_t file_sym;
	int32_t line;
};

static inline int32_t to_symbol(const char *s);
static void record(int32_t type, const void *data_1, int32_t size_1, const void *data_2, int32_t size_2);
static inline void write(const void * data, int32_t size);

static char string_table_buffer[STRING_TABLE_SIZE];
static struct Stream stream;
static struct nfst_StringTable *strings;

// Initializes the memory tracker. You should call this before calling any other
// memory tracking functions.
void nfmt_init()
{
	strings = (struct nfst_StringTable *)string_table_buffer;
	nfst_init(strings, STRING_TABLE_SIZE, 15);
}

// Records data for a malloc operation
void nfmt_record_malloc(void *p, uint32_t size, const char *tag, const char *file, int32_t line)
{
	struct MallocRecord mr;
	mr.p = p;
	mr.size = size;
	mr.tag_sym = to_symbol(tag);
	mr.file_sym = to_symbol(file);
	mr.line = line;
	record(RECORD_TYPE_MALLOC, &mr, sizeof(mr), NULL, 0);
}

// Records data for a free operation.
void nfmt_record_free(void *p)
{
	record(RECORD_TYPE_FREE, &p, sizeof(p), NULL, 0);
}

// Consumes a chunk of data from the streams. You should call this regularily to prevent
// the streams from overflowing.
struct nfmt_Buffer nfmt_read()
{
	struct nfmt_Buffer b;
	b.start = stream.buffer + stream.start;
	int32_t count = MIN(stream.start + stream.size, STREAM_SIZE) - stream.start;
	b.end = b.start + count;
	stream.start = (stream.start + count) % STREAM_SIZE;
	stream.size -= count;
	return b;
}

static inline int32_t to_symbol(const char *s)
{
	int32_t before_count = strings->count;
	int32_t sym = nfst_to_symbol(strings, s);
	
	// New symbol, add it to the stream.
	if (strings->count > before_count)
		record(RECORD_TYPE_SYMBOL, &sym, sizeof(sym), s, strlen(s)+1);

	return sym;
}


static void record(int32_t type, const void *data_1, int32_t size_1, const void *data_2, int32_t size_2)
{
	// If we don't have enough memory to record the message, record an OUT_OF_MEMORY message
	// instead. (The extra sizeof(type) is there because otherwise we wouldn't be able to
	// record an OUT_OF_MEMORY message if we completely filled the buffer).
	if (stream.size + sizeof(type) + size_1 + size_2 + sizeof(type) > STREAM_SIZE) {
		type = RECORD_TYPE_OUT_OF_MEMORY;
		size_1 = size_2 = 0;
	}

	// At this point, if we don't have enough memory for the message -- skip it. If this
	// happens, we should already have recorded an OUT_OF_MEMORY message at some earlier
	// point.
	if (stream.size + sizeof(type) + size_1 + size_2 > STREAM_SIZE)
		return;

	// stream.start is 4-byte aligned, so no need to check for half message
	memcpy(stream.buffer + stream.start, &type, sizeof(type));
	stream.start = (stream.start + sizeof(type)) % STREAM_SIZE;
	stream.size += sizeof(type);

	write(&type, sizeof(type));
	write(data_1, size_2);
	write(data_2, size_2);
}

static inline void write(const void *data, int32_t size)
{
	const char *p = data;
	int32_t size_1 = MIN(STREAM_SIZE - stream.start, size);
	memcpy(stream.buffer + stream.start, p, size_1);
	stream.start = (stream.start + size_1) % STREAM_SIZE;
	stream.size += size_1;
	if (size_1 < size) {
		p += size_1;
		size -= size_1;
		memcpy(stream.buffer + stream.start, p, size);
	}
	while (stream.start % 4) {
		stream.buffer[stream.start] = 0;
		stream.start = (stream.start + 1) % STREAM_SIZE;
		stream.size++;
	}
}

#ifdef NFMT_UNIT_TEST

	#include <stdio.h>

	int main(int argc, char **argv)
	{
		nfmt_init();

		nfmt_record_malloc(0, 1024, "test", __FILE__, __LINE__);
		nfmt_record_free(0);
		struct nfmt_Buffer b = nfmt_read();
	}

#endif