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

#include <stdint.h>
#include <string.h>
#include <assert.h>

// ## Public interface

// Structure representing a string table. The data for the table is stored
// directly after this header in memory and consists of a hash table
// followed by a string data block.
struct nfst_StringTable
{
	// The total size of the allocated data, including this header.
    int32_t allocated_bytes;

    // The number of strings in the table.
    int32_t count;

    // Total number of slots in the hash table.
    int32_t num_hash_slots;

    // The current number of bytes used for string data.
    int32_t strings_bytes;
};

// Initializes an empty string table in the specified memory area. `bytes` is
// the number of bytes that the string table may use.
void nfst_init(struct nfst_StringTable *st, int32_t bytes);

// Resizes the string table `st` to use `new_bytes`. You can use this to
// grow or shrink the string table. Usually you should pair this with a
// `realloc` to also grow or shrkink the memory buffer. If the `dest`
// table, the first that fit will be preserved.
//
// Returns 0 if all strings were copied, a non-zero value if the string
// list was truncated.
int nfst_resize(struct nfst_StringTable *st, int32_t new_bytes);

// Returns the minimal size that can hold all the strings in the table.
// This can be used to "shrink-to-fit" the string table.
int32_t nfst_minimal_size(struct nfst_StringTable *st);

// This value is returned by `nfst_to_symbol()` if the string table is full.
// In this case, you should allocate a bigger buffer for it and copy into it
// with `nfst_copy` if you want to be able to keep adding strings.
enum {NFST_STRING_TABLE_FULL = -1};

// Returns the symbol for the string `s`. If `s` is not already in the table,
// it is added. If `s` can't be added because the table is full, the function
// returns `NFST_STRING_TABLE_FULL`.
//
// The empty string is guaranteed to have the symbol `0`.
int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s);

// Returns the string corresponding to the `symbol`. Calling this with a
// value which is not a symbol returned by `nfst_to_symbol()` results in
// undefined behavior.
const char *nfst_to_string(struct nfst_StringTable *, int32_t symbol);

// ## Implementation

// Intial size of data structures will assume strings are of this length
// (including the NULL byte).
static const int32_t GUESSED_STRING_LENGTH = 16;

// We won't fill the hash table beyond this rate (in percent).
static const int32_t MAX_HASH_FILL_RATE = 80;

// Hashes and computes the length of the string in a single operation.
// (To avoid having to do two passes over the string.)
struct HashAndLength
{
	uint32_t hash;
	uint32_t length;
};
static inline struct HashAndLength compute_hash_and_length(const char *start)
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

static inline int hash_item_size(int32_t bytes)
{
	return bytes > 64*1024 ? sizeof(uint32_t) : sizeof(uint16_t);
}

static inline char *strings(struct nfst_StringTable *st)
{
	return hash_item_size(st->allocated_bytes) == 2 ?
		 (char *)(hashtable_16(st) + st->num_hash_slots) :
		 (char *)(hashtable_32(st) + st->num_hash_slots);
}

static inline int32_t max_items(int32_t bytes, int32_t average_string_length)
{
	const int32_t data_bytes = bytes - sizeof(struct nfst_StringTable);
	const int32_t item_size_10 = hash_item_size(bytes) * 10 * 100 / MAX_HASH_FILL_RATE + 10 * average_string_length;
	const int32_t result = data_bytes * 10 / item_size_10;
	return result;
}

static inline int32_t bytes(int32_t count, int32_t string_bytes)
{
	const int32_t num_hash_slots = count * 100 / MAX_HASH_FILL_RATE;
	const int32_t size_16 = num_hash_slots * sizeof(uint16_t) + string_bytes;
	if (size_16 <= 64 * 1024)
		return size_16;
	return num_hash_slots * sizeof(uint32_t) + string_bytes;
}

// We must have room for at leat one hash slot and one string
static int32_t MIN_SIZE = sizeof(struct nfst_StringTable) + 1*(uint32_t) + 4;

inline void nfst_init(struct nfst_StringTable *st, int32_t bytes)
{
	assert(bytes > MIN_SIZE);

	st->allocated_bytes = bytes;
	st->count = 0;
	st->num_hash_slots = max_items(bytes, GUESSED_STRING_LENGTH) * 100 / MAX_HASH_FILL_RATE;
	
	// Make sure we have at least one hash slot to avoid having to check
	// for it to avoid division by zero errors everywhere.
	if (st->num_hash_slots < 1)
		st->num_hash_slots = 1;
	memset(hashtable_16(st), 0, st->num_hash_slots * hash_item_size(bytes));

	// Empty string is stored at index 0. This way, we can use 0 as a marker for
	// empty hash slots.
	strings(st)[0] = 0;
	st->strings_bytes = 1;

}

int nfst_resize(struct nfst_StringTable *st, int32_t new_bytes)
{
	assert(new_bytes > MIN_SIZE);

	const char * const old_strings = strings(st);

	// Determine the number of hash slots
	int32_t truncated = 0;
	if (new_bytes >= st->allocated_bytes) {
		const int32_t average_string_length = 
			st->count > 16 ? st->strings_bytes / st->count
			: GUESSED_STRING_LENGTH;
		st->num_hash_slots = max_items(new_bytes, average_string_length);
		if (st->num_hash_slots < 1)
			st->num_hash_slots = 1;
	} else if (bytes(st->count, st->strings_bytes) <= new_bytes) {
		st->num_hash_slots = st->count * 100 / MAX_HASH_FILL_RATE;
	} else {
		truncated = 1;

		// Find out how many strings to include
		int32_t count = 0;
		int32_t strings_bytes = 0;

		const char *s = old_strings;
		while (count < st->count+1) {
			while (*s)
				++s;
			if (bytes(count+1, s-old_strings) > new_bytes)
				break;
			++count;
			strings_bytes = s-old_strings;
			++s;
		}

		st->count = count;
		st->strings_bytes = strings_bytes;
		st->num_hash_slots = count * 100 / MAX_HASH_FILL_RATE;
	}

	st->allocated_bytes = new_bytes;
	char * const new_strings = strings(st);
	memmove(new_strings, old_strings, st->strings_bytes);

	// Rebuild hash table
	
	const char *s = new_strings;
	if (hash_item_size(new_bytes) == 2) {
		memset(hashtable_16(st), 0, st->num_hash_slots * sizeof(uint16_t));

		uint16_t * const ht = hashtable_16(st);
		while (s < new_strings + st->strings_bytes) {
			const struct HashAndLength hl = compute_hash_and_length(s);
			uint32_t i = hl.hash % st->num_hash_slots;
			while (ht[i])
				i = (i + 1) % st->num_hash_slots;
			ht[i] = s - new_strings;
			s = s + hl.length + 1;
		}
	} else {
		memset(hashtable_32(st), 0, st->num_hash_slots * sizeof(uint32_t));

		uint32_t * const ht = hashtable_32(st);
		while (s < new_strings + st->strings_bytes) {
			const struct HashAndLength hl = compute_hash_and_length(s);
			uint32_t i = hl.hash % st->num_hash_slots;
			while (ht[i])
				i = (i + 1) % st->num_hash_slots;
			ht[i] = s - new_strings;
			s = s + hl.length + 1;
		}
	}

	return truncated;
}

inline int32_t nfst_minimal_size(struct nfst_StringTable *st)
{
	const int32_t num_hash_slots = st->count * 100 / MAX_HASH_FILL_RATE;
	const int32_t size_16 = sizeof(struct nfst_StringTable)
		+ num_hash_slots * sizeof(uint16_t)
		+ st->strings_bytes;
	if (size_16 <= 64*1024)
		return size_16;
	return sizeof(struct nfst_StringTable)
		+ num_hash_slots * sizeof(uint32_t)
		+ st->strings_bytes;
}

inline int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s)
{
	// "" maps to 0
	if (!*s) return 0;

	const struct HashAndLength hl = compute_hash_and_length(s);
	char * const strs = strings(st);
		
	uint32_t i = 0;
	if (hash_item_size(st->allocated_bytes) == 2) {
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

	if ((st->count+1) * 100 / st->num_hash_slots > MAX_HASH_FILL_RATE)
		return NFST_STRING_TABLE_FULL;

	char * const dest = strs + st->strings_bytes;
	if (dest + hl.length + 1 - (char *)st > st->allocated_bytes)
		return NFST_STRING_TABLE_FULL;

	const uint32_t symbol = st->strings_bytes;
	if (hash_item_size(st->allocated_bytes) == 2)
		hashtable_16(st)[i] = symbol;
	else
		hashtable_32(st)[i] = symbol;
	st->count++;
	memcpy(dest, s, hl.length + 1);
	st->strings_bytes += hl.length + 1;
	return symbol;
}

inline const char *nfst_to_string(struct nfst_StringTable *st, int32_t symbol)
{
	return strings(st) + symbol;
}

// ## Unit Test

#ifdef UNIT_TEST

	#include <stdio.h>
	#include <assert.h>
	#include <stdlib.h>

	#define assert_strequal(a,b)	assert(strcmp((a), (b)) == 0)

	static struct nfst_StringTable *st_realloc(struct nfst_StringTable *st, int32_t bytes, int32_t expect_trunc)
	{
		if (!st) {
			st = realloc(st, bytes);
			nfst_init(st, bytes);
		} else if (bytes == 0) {
			free(st);
			return NULL;
		} else if (bytes > st->allocated_bytes) {
			st = realloc(st, bytes);
			nfst_resize(st, bytes);
		} else {
			int truncated = nfst_resize(st, bytes);
			assert(expect_trunc && truncated || !expect_trunc && !truncated);
			st = realloc(st, bytes);
		}

		return st;
	}
	
	int main(int argc, char **argv)
	{
		struct HashAndLength hl = compute_hash_and_length("niklas frykholm");
		assert(hl.length == 15);
		
		// Basic test
		{
			char buffer[1024];
			struct nfst_StringTable * const st = (struct nfst_StringTable *)buffer;
			nfst_init(st, 1024);

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
			struct nfst_StringTable * st = st_realloc(NULL, 24, 0);

			assert(nfst_to_symbol(st, "01234567890123456789") == NFST_STRING_TABLE_FULL);

			for (int32_t i = 0; i<10000; ++i) {
				char s[10];
				sprintf(s, "%i", i);
				int32_t sym = nfst_to_symbol(st, s);
				while (sym == NFST_STRING_TABLE_FULL) {
					st = st_realloc(st, st->allocated_bytes*2, 0);
					sym = nfst_to_symbol(st, s);
				}
				assert_strequal(s, nfst_to_string(st, sym));
			}

			st = st_realloc(st, nfst_minimal_size(st), 0);
			
			for (int32_t i=0; i<10000; ++i) {
				char s[10];
				sprintf(s, "%i", i);
				int32_t sym = nfst_to_symbol(st, s);
				assert(sym > 0);
				assert_strequal(s, nfst_to_string(st, sym));
			}

			int32_t sym_300 = nfst_to_symbol(st, "300");
			st = st_realloc(st, 4 * 1024, 1);
			assert(sym_300 == nfst_to_symbol(st, "300"));
			
			st_realloc(st, 0, 1);
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
		nfst_init(st, 128*1024);

		char s[10000][5];
		for (int i=0; i<10000; ++i)
			sprintf(s[i], "%i", i);

		clock_t start = clock();
		srand(0);
		for (int i=0; i<1000000; ++i)
			nfst_to_symbol(st, s[rand() % 10000]);
		clock_t stop = clock();

		float delta = ((double)(stop-start)) / CLOCKS_PER_SEC;
		printf("Time: %f\n", delta);
		printf("Memory use: %i\n", nfst_minimal_size(st));
	}

#endif