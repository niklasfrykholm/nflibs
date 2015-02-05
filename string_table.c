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
// * grow() and shrink() functions.
// * Use 16 bit hash table when allocated_bytes < 64 K.
// * Four byte align strings for performance
// * Store hashes for faster rehashing?

#include <stdint.h>
#include <string.h>

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

// This value is returned if the string table is full. In this case, you must
// allocate a bigger buffer for the string table.
enum {NFST_STRING_TABLE_FULL = -1};

// Returns the symbol for the string `s`. If `s` is not already in the table,
// it is added. If `s` can't be added because the table is full, the function
// returns `NFST_STRING_TABLE_FULL`.
int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s);

// Returns the string corresponding to the `symbol`. Calling this with a
// value which is not a symbol results in undefined behavior.
const char *nfst_to_string(struct nfst_StringTable *, int32_t symbol);

// ## Implementation

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

static inline int32_t *hashtable(struct nfst_StringTable *st)
{
	return (int32_t *)(st + 1);
}

static inline char *strings(struct nfst_StringTable *st)
{
	return (char *)(hashtable(st) + st->num_hash_slots);
}

inline void nfst_init(struct nfst_StringTable *st, int32_t bytes)
{
	const float guessed_string_length = 10.0f;
	const float hash_fill_rate = 0.8f;
	const float max_items = ((float)bytes) / (4.0f / hash_fill_rate + guessed_string_length);
	const float num_hash_slots = max_items / hash_fill_rate;

	st->allocated_bytes = bytes;
	st->count = 0;
	st->num_hash_slots = (uint32_t)num_hash_slots;
	// Ensure that 0 is never used in hashtable, so it means empty slot.
	st->strings_bytes = 1;
	memset(hashtable(st), 0, st->num_hash_slots * sizeof(uint32_t));
}

inline int32_t nfst_to_symbol(struct nfst_StringTable *st, const char *s)
{
	// "" maps to 0
	if (!*s) return 0;

	struct HashAndLength hl = compute_hash_and_length(s);

	int32_t *ht = hashtable(st);
	char *strs = strings(st);
	uint32_t i = hl.hash % st->num_hash_slots;
	while (ht[i]) {
		if (strcmp(s, strs + ht[i]) == 0)
			return ht[i];
		i = (i+1) % st->num_hash_slots;
	}

	const float max_hash_fill_rate = 0.8;
	if ((float)st->count / (float)st->num_hash_slots > max_hash_fill_rate)
		return NFST_STRING_TABLE_FULL;

	char *dest = strs + st->strings_bytes;
	if (dest + hl.length + 1 - (char *)st > st->allocated_bytes)
		return NFST_STRING_TABLE_FULL;

	int32_t symbol = st->strings_bytes;
	ht[i] = symbol;
	st->count++;
	memcpy(dest, s, hl.length + 1);
	st->strings_bytes += hl.length + 1;
	return symbol;
}

inline const char *nfst_to_string(struct nfst_StringTable *st, int32_t symbol)
{
	if (!symbol) return "";
	return strings(st) + symbol;
}

// ## Unit Test

#ifdef UNIT_TEST

	#include <stdio.h>

	int main(int argc, char **argv)
	{
		struct HashAndLength hl = compute_hash_and_length("niklas frykholm");
		printf("%u %i\n", hl.hash, hl.length);

		char buffer[1024];

		struct nfst_StringTable * const st = (struct nfst_StringTable *)buffer;
		nfst_init(st, 1024);

		int32_t sym_1 = nfst_to_symbol(st, "Niklas");
		int32_t sym_2 = nfst_to_symbol(st, "Frykholm");
		const char *str_1 = nfst_to_string(st, sym_1);
		const char *str_2 = nfst_to_string(st, sym_2);

		printf("%i\n", sym_1);
		printf("%i\n", sym_2);
		printf("%s\n", str_1);
		printf("%s\n", str_2);
	}

#endif