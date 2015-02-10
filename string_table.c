// # String Table
//
// This file implements a string table for *string interning*, i.e. converting
// back and forth between *strings* and `int32_t` representations which we call
// *symbols*. This can be used to compress data structures containing repeated
// strings.
//
// For cache friendliness all data is stored in a single continuous buffer.
// You can move this buffer around freely in memory. You are responsible for
// allocating the buffer. If the buffer runs out of memory you are responsible
// for resizing it before you can add more strings.
//
// See example code in the Unit Test section below.
//
// ## To Do
//
// * Four byte align strings for strcmp & hash performance?
// * Store hashes for faster rehashing?
// * Packing function that optimizes seed for minimal collisions
// * Faster hahsing and strlen for long strings?
//   - Is there any point? This will mostly be used for short strings anyway.

// # Includes

#include <stdint.h>

// ## Public interface

#define NFST_STRING_TABLE_FULL (-1)

struct nfst_StringTable;

void nfst_init(struct nfst_StringTable *st, int32_t bytes, int32_t average_string_size);
void nfst_grow(struct nfst_StringTable *st, int32_t bytes);
int32_t nfst_pack(struct nfst_StringTable *st);
int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s);
const char *nfst_to_string(struct nfst_StringTable *, int32_t symbol);

// ## Implementation

#include <assert.h>
#include <memory.h>

#define HASH_FACTOR (2.0f)

// We must have room for at leat one hash slot and one string
#define MIN_SIZE (sizeof(struct nfst_StringTable) + 1*(uint32_t) + 4)

#define MAX(a,b) ((a) > (b) ? (a) : (b))

struct HashAndLength
{
	uint32_t hash;
	uint32_t length;
};

static inline struct HashAndLength hash_and_length(const char *start);
static inline uint16_t *hashtable_16(struct nfst_StringTable *st);
static inline uint32_t *hashtable_32(struct nfst_StringTable *st);
static inline char *strings(struct nfst_StringTable *st);
static inline int32_t available_string_bytes(struct nfst_StringTable *st);

// Structure representing a string table. The data for the table is stored
// directly after this header in memory and consists of a hash table
// followed by a string data block.
struct nfst_StringTable
{
	// The total size of the allocated data, including this header.
    int32_t allocated_bytes;

    // The number of strings in the table.
    int32_t count;

    // Does the hash table use 16 bit slots
    int32_t uses_16_bit_hash_slots;

    // Total number of slots in the hash table.
    int32_t num_hash_slots;

    // The current number of bytes used for string data.
    int32_t string_bytes;
};

// Initializes an empty string table in the specified memory area. `bytes` is
// the total ammount of memory allocated at the pointer and `average_strlen` is
// the expected average length of the strings that will be added.
void nfst_init(struct nfst_StringTable *st, int32_t bytes, int32_t average_strlen)
{
	assert(bytes >= MIN_SIZE);

	st->allocated_bytes = bytes;
	st->count = 0;
	
	float bytes_per_string = average_strlen + 1 + sizeof(uint16_t) * HASH_FACTOR;
	float num_strings = (bytes - sizeof(*st)) / bytes_per_string;
	st->num_hash_slots = MAX(num_strings * HASH_FACTOR, 1);

	int32_t bytes_for_strings_16 = bytes - sizeof(*st) - sizeof(uint16_t) * st->num_hash_slots;
	int32_t bytes_for_strings_32 = bytes - sizeof(*st) - sizeof(uint32_t) * st->num_hash_slots;
	st->uses_16_bit_hash_slots = bytes_for_strings_32 <= 64 * 1024;

	memset(hashtable_16(st), 0, st->num_hash_slots * 
		(st->uses_16_bit_hash_slots ? sizeof(uint16_t) : sizeof(uint32_t)));
	
	// Empty string is stored at index 0. This way, we can use 0 as a marker for
	// empty hash slots.
	strings(st)[0] = 0;
	st->string_bytes = 1;
}

void nfst_grow(struct nfst_StringTable *st, int32_t bytes)
{
	assert(bytes >= st->allocated_bytes);

	const char * const old_strings = strings(st);

	st->allocated_bytes = bytes;

	float average_strlen = st->count > 0 ? (float)st->string_bytes / (float)st->count : 15.0f;
	float bytes_per_string = average_strlen + 1 + sizeof(uint16_t) * HASH_FACTOR;
	float num_strings = (bytes - sizeof(*st)) / bytes_per_string;
	st->num_hash_slots = MAX(num_strings * HASH_FACTOR, st->num_hash_slots);

	int32_t bytes_for_strings_16 = bytes - sizeof(*st) - sizeof(uint16_t) * st->num_hash_slots;
	int32_t bytes_for_strings_32 = bytes - sizeof(*st) - sizeof(uint32_t) * st->num_hash_slots;
	st->uses_16_bit_hash_slots = bytes_for_strings_32 <= 64*1024;

	char * const new_strings = strings(st);
	memmove(new_strings, old_strings, st->string_bytes);

	// Rebuild hash table
	const char *s = new_strings + 2;
	if (st->uses_16_bit_hash_slots) {
		memset(hashtable_16(st), 0, st->num_hash_slots * sizeof(uint16_t));

		uint16_t * const ht = hashtable_16(st);
		while (s < new_strings + st->string_bytes) {
			const struct HashAndLength hl = hash_and_length(s);
			uint32_t i = hl.hash % st->num_hash_slots;
			while (ht[i])
				i = (i + 1) % st->num_hash_slots;
			ht[i] = s - new_strings;
			s = s + hl.length + 1;
		}
	} else {
		memset(hashtable_32(st), 0, st->num_hash_slots * sizeof(uint32_t));

		uint32_t * const ht = hashtable_32(st);
		while (s < new_strings + st->string_bytes) {
			const struct HashAndLength hl = hash_and_length(s);
			uint32_t i = hl.hash % st->num_hash_slots;
			while (ht[i])
				i = (i + 1) % st->num_hash_slots;
			ht[i] = s - new_strings;
			s = s + hl.length + 1;
		}
	}
}

// Returns the symbol for the string `s`. If `s` is not already in the table,
// it is added. If `s` can't be added because the table is full, the function
// returns `NFST_STRING_TABLE_FULL`.
//
// The empty string is guaranteed to have the symbol `0`.
int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s)
{
	// "" maps to 0
	if (!*s) return 0;

	const struct HashAndLength hl = hash_and_length(s);
	char * const strs = strings(st);
		
	uint32_t i = 0;
	if (st->uses_16_bit_hash_slots) {
		uint16_t * const ht = hashtable_16(st);
		i = hl.hash % st->num_hash_slots;
		while (ht[i]) {
			if (strcmp(s, strs + ht[i]) == 0)
				return ht[i];
			i = (i+1) % st->num_hash_slots;
		}
	} else {
		uint32_t * const ht = hashtable_32(st);
		i = hl.hash % st->num_hash_slots;
		while (ht[i]) {
			if (strcmp(s, strs + ht[i]) == 0)
				return ht[i];
			i = (i+1) % st->num_hash_slots;
		}
	}

	if (st->count + 1 >= st->num_hash_slots)
		return NFST_STRING_TABLE_FULL;

	if ( (float)st->num_hash_slots / (float)(st->count + 1) < HASH_FACTOR)
		return NFST_STRING_TABLE_FULL;

	char * const dest = strs + st->string_bytes;
	if (st->string_bytes + hl.length + 1 > available_string_bytes(st))
		return NFST_STRING_TABLE_FULL;

	const uint32_t symbol = st->string_bytes;
	if (st->uses_16_bit_hash_slots) {
		if (symbol > 64 * 1024)
			return NFST_STRING_TABLE_FULL;
		hashtable_16(st)[i] = symbol;
	} else {
		hashtable_32(st)[i] = symbol;
	}
	st->count++;
	memcpy(dest, s, hl.length + 1);
	st->string_bytes += hl.length + 1;
	return symbol;
}

// Returns the string corresponding to the `symbol`. Calling this with a
// value which is not a symbol returned by `nfst_to_symbol()` results in
// undefined behavior.
const char *nfst_to_string(struct nfst_StringTable *st, int32_t symbol)
{
	return strings(st) + symbol;
}

static inline struct HashAndLength hash_and_length(const char *start)
{
	// The hash function is borrowed from Lua.
	//
	// Since we need to walk the entire string anyway for finding the
	// length, this is a decent hash function.

	uint32_t h = 0;
	const char *s = start;
	for (; *s; ++s)
		h = h ^ ((h<<5) + (h>>2) + (unsigned char)*s);

	struct HashAndLength result = {h, s-start};
	return result;
}

static inline uint16_t *hashtable_16(struct nfst_StringTable *st)
{
	return (uint16_t *)(st + 1);
}

static inline uint32_t *hashtable_32(struct nfst_StringTable *st)
{
	return (uint32_t *)(st + 1);
}

static inline char *strings(struct nfst_StringTable *st)
{
	return st->uses_16_bit_hash_slots ?
		 (char *)(hashtable_16(st) + st->num_hash_slots) :
		 (char *)(hashtable_32(st) + st->num_hash_slots);
}

static inline int32_t available_string_bytes(struct nfst_StringTable *st)
{
	return st->uses_16_bit_hash_slots ?
		st->allocated_bytes - sizeof(*st) - st->num_hash_slots * sizeof(uint16_t) :
		st->allocated_bytes - sizeof(*st) - st->num_hash_slots * sizeof(uint32_t);
}

// ## Unit Test

#ifdef UNIT_TEST

	#include <stdio.h>
	#include <assert.h>
	#include <stdlib.h>

	#define assert_strequal(a,b)	assert(strcmp((a), (b)) == 0)

	static struct nfst_StringTable *grow(struct nfst_StringTable *st)
	{
		st = realloc(st, st->allocated_bytes * 2);
		nfst_grow(st, st->allocated_bytes * 2);
		return st;
	}

	int main(int argc, char **argv)
	{
		struct HashAndLength hl = hash_and_length("niklas frykholm");
		assert(hl.length == 15);
		
		// Basic test
		{
			char buffer[1024];
			struct nfst_StringTable * const st = (struct nfst_StringTable *)buffer;
			nfst_init(st, 1024, 10);

			assert(nfst_to_symbol(st, "") == 0);
			assert_strequal("", nfst_to_string(st, 0));

			int32_t sym_niklas = nfst_to_symbol(st, "niklas");
			int32_t sym_frykholm = nfst_to_symbol(st, "frykholm");

			assert(sym_niklas == nfst_to_symbol(st, "niklas"));
			assert(sym_frykholm == nfst_to_symbol(st, "frykholm"));
			assert(sym_niklas != sym_frykholm);

			assert_strequal("niklas", nfst_to_string(st, sym_niklas));
			assert_strequal("frykholm", nfst_to_string(st, sym_frykholm));
		}

		// Grow test
		{
			struct nfst_StringTable * st = realloc(NULL, MIN_SIZE);
			nfst_init(st, MIN_SIZE, 4);
			
			assert(nfst_to_symbol(st, "01234567890123456789") == NFST_STRING_TABLE_FULL);

			for (int32_t i = 0; i<10000; ++i) {
				char s[10];
				sprintf(s, "%i", i);
				int32_t sym = nfst_to_symbol(st, s);
				while (sym == NFST_STRING_TABLE_FULL) {
					st = grow(st);
					sym = nfst_to_symbol(st, s);
				}
				assert_strequal(s, nfst_to_string(st, sym));
			}

			// Pack: st = st_realloc(st, nfst_minimal_size(st), 0);
			
			for (int32_t i=0; i<10000; ++i) {
				char s[10];
				sprintf(s, "%i", i);
				int32_t sym = nfst_to_symbol(st, s);
				assert(sym > 0);
				assert_strequal(s, nfst_to_string(st, sym));
			}

			free(st);
		}
	}

#endif

#ifdef PROFILER

	#include <stdio.h>
	#include <stdlib.h>
	#include <time.h>

	int main(int argc, char **argv)
	{
		struct nfst_StringTable *st = malloc(128*1024);
		nfst_init(st, 128*1024, 4);

		char s[10000][5];
		for (int i=0; i<10000; ++i)
			sprintf(s[i], "%i", i);

		clock_t start = clock();
		srand(0);
		for (int i=0; i<10000000; ++i)
			nfst_to_symbol(st, s[rand() % 10000]);
		clock_t stop = clock();

		float delta = ((double)(stop-start)) / CLOCKS_PER_SEC;
		printf("Time: %f\n", delta);
		printf("Memory use: %i\n", st->allocated_bytes);
	}

#endif