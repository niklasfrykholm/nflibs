// # Memory Tracker
//
// This files implements a memory tracker. Using the memory tracker you log
// allocations and frees with calls to `nfmt_record_malloc()` and
// `nfmt_record_free()`. The logged data can later be read out with a call
// to `nfmt_read()` for transmission over the network, saving to disk, etc.
//
// You are responsible for reading out the data at regular intervals. If you
// don't do that, the data buffer will overflow and recording is
// suspended. If this happens, a special marker `RECORD_TYPE_OUT_OF_MEMORY` is
// inserted in the output stream, so that consumers can tell what has happened.
//
// The format of the recorded buffer is a sequence of:
//
// ```
// [ EVENT_TYPE ] [ EVENT_DATA ]
// ```
//
// The event type is an integer and can be one of
//
// ```cpp
// enum {RECORD_TYPE_MALLOC, RECORD_TYPE_FREE, RECORD_TYPE_SYMBOL, RECORD_TYPE_OUT_OF_MEMORY};
// ```
// See the code for a description of the data that is logged for each type of event.

// ## Interface

struct nfmt_Buffer {
	char *start;
	char *end;
};

void nfmt_init();
void nfmt_record_malloc(void *p, int size, const char *tag, const char *file, int line);
void nfmt_record_free(void *p);
struct nfmt_Buffer nfmt_read();

// ## Implementation

#include <memory.h>
#include <stdlib.h>

struct nfst_StringTable;
void nfst_init(struct nfst_StringTable *st, int bytes, int average_string_size);
int nfst_to_symbol(struct nfst_StringTable *st, const char *s);

#define STREAM_SIZE (16*1024)
#define STRING_TABLE_SIZE (2*1024)

#define MIN(a,b) ((a) < (b) ? (a) : (b))

enum {RECORD_TYPE_MALLOC, RECORD_TYPE_FREE, RECORD_TYPE_SYMBOL, RECORD_TYPE_OUT_OF_MEMORY};

struct Stream
{
	char buffer[STREAM_SIZE];
	int start;
	int size;
};

struct MallocRecord
{
	void *p;
	int size;
	int tag_sym;
	int file_sym;
	int line;
};

static inline int to_symbol(const char *s);
static void record(int type, const void *data_1, int size_1, const void *data_2, int size_2);
static inline void write(const void * data, int size);

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

// Records data for a malloc operation. The tag is an arbitrary logged string
// to identify the system that made the allocation. 
void nfmt_record_malloc(void *p, int size, const char *tag, const char *file, int line)
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
	int count = MIN(stream.start + stream.size, STREAM_SIZE) - stream.start;
	b.end = b.start + count;
	stream.start = (stream.start + count) % STREAM_SIZE;
	stream.size -= count;
	return b;
}

static inline int to_symbol(const char *s)
{
	int before_count = strings->count;
	int sym = nfst_to_symbol(strings, s);
	
	// New symbol, add it to the stream.
	if (strings->count > before_count)
		record(RECORD_TYPE_SYMBOL, &sym, sizeof(sym), s, strlen(s)+1);

	return sym;
}


static void record(int type, const void *data_1, int size_1, const void *data_2, int size_2)
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

static inline void write(const void *data, int size)
{
	const char *p = data;
	int size_1 = MIN(STREAM_SIZE - stream.start, size);
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