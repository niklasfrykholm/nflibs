// # Config data
//
// This library implements a dynamic generic data container that can hold
// bools, numbers, strings, arrays and objects. Basically, anything you can
// represent with a JSON file.

// ## Interface

struct nfcd_ConfigData;

enum {
	NFCD_TYPE_NULL, NFCD_TYPE_FALSE, NFCD_TYPE_TRUE, NFCD_TYPE_NUMBER, NFCD_TYPE_STRING,
	NFCD_TYPE_ARRAY, NFCD_TYPE_OBJECT
};

#define NFCD_TYPE_MASK (0x7)
#define NFCD_TYPE_BITS (3)

typedef int nfcd_loc;
typedef void * (*nfcd_realloc) (void *ud, void *ptr, int osize, int nsize, const char *file, int line);

struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int config_size, int stringtable_size);
void nfcd_free(struct nfcd_ConfigData *cd);

nfcd_loc nfcd_root(struct nfcd_ConfigData *cd);
int nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc);
double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc);
const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc);

int nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc arr);
nfcd_loc nfcd_array_item(struct nfcd_ConfigData *cd, nfcd_loc arr, int i);

int nfcd_object_size(struct nfcd_ConfigData *cd, nfcd_loc object);
nfcd_loc nfcd_object_keyloc(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
const char *nfcd_object_key(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, nfcd_loc object, const char *key);

nfcd_loc nfcd_null();
nfcd_loc nfcd_false();
nfcd_loc nfcd_true();
nfcd_loc nfcd_add_number(struct nfcd_ConfigData **cd, double n);
nfcd_loc nfcd_add_string(struct nfcd_ConfigData **cd, const char *s);
nfcd_loc nfcd_add_array(struct nfcd_ConfigData **cd, int size);
nfcd_loc nfcd_add_object(struct nfcd_ConfigData **cd, int size);
void nfcd_set_root(struct nfcd_ConfigData *cd, nfcd_loc root);

void nfcd_push(struct nfcd_ConfigData **cd, nfcd_loc array, nfcd_loc item);
void nfcd_set(struct nfcd_ConfigData **cd, nfcd_loc object, const char *key, nfcd_loc value);
void nfcd_set_loc(struct nfcd_ConfigData **cd, nfcd_loc object, nfcd_loc key, nfcd_loc value);

nfcd_realloc nfcd_allocator(struct nfcd_ConfigData *cd, void **user_data);

// ## Implementation

#include <memory.h>
#include <stdlib.h>
#include <assert.h>

struct nfst_StringTable;
void nfst_init(struct nfst_StringTable *st, int bytes, int average_string_size);
void nfst_grow(struct nfst_StringTable *st, int bytes);
int nfst_to_symbol(struct nfst_StringTable *st, const char *s);
int nfst_to_symbol_const(const struct nfst_StringTable *st, const char *s);
const char *nfst_to_string(struct nfst_StringTable *, int symbol);

// All the data is stored in a single buffer. A data reference (`nfcd_loc`)
// encodes the data type and the offset into this buffer in a single int.
//
// Strings are stored in an nfst_StringTable, so for strings the offset
// represents the offset into the string table.

// Container for the config data.
struct nfcd_ConfigData
{
	int total_bytes;
	int allocated_bytes;
	int used_bytes;
	nfcd_loc root;
	nfcd_realloc realloc;
	void *realloc_user_data;
};

// Header for array and object data. The data is stored in a chain of blocks.
// When the data grows dynamically, we add bigger and bigger blocks to the
// chain. When we pack the data for disk storage, these chais are coalesced
// into a single block.
//
// For an array, the data stored in a block consists of the array items:
//
//     [item] [item] ...
//
// For an object, the data consists of interleaved keys and values:
//
//     [key] [vaulue] [key] [value] ...
struct block
{
	int allocated_size;
	int size;
	nfcd_loc next_block;
};

// Represents a stored item in an object block.
struct object_item
{
	nfcd_loc key;
	nfcd_loc value;
};

// Extracts the offset from an `nfcd_loc` item.
#define LOC_OFFSET(loc)			((loc) >> NFCD_TYPE_BITS)

// Extracts the type from an `nfcd_loc` item.
#define LOC_TYPE(loc)			((loc) & NFCD_TYPE_MASK)

// Makes an `nfcd_loc` item from object and type.
#define MAKE_LOC(type, offset)	((type) | (offset) << NFCD_TYPE_BITS)

#define STRINGTABLE(cd)			((struct nfst_StringTable *)((char *)(cd) + (cd)->allocated_bytes))

static nfcd_loc write(struct nfcd_ConfigData **cdp, int type, void *p, int count, int zeroes);
static struct object_item *object_item(struct nfcd_ConfigData *cd, nfcd_loc object, int i);

static nfcd_loc write(struct nfcd_ConfigData **cdp, int type, void *p, int count, int zeroes)
{
	int total = count + zeroes;
	struct nfcd_ConfigData *cd = *cdp;
	while (cd->used_bytes + total > cd->allocated_bytes) {
		int string_bytes = cd->total_bytes - cd->allocated_bytes;
		int new_allocated_bytes = cd->allocated_bytes * 2;
		int new_total_bytes = new_allocated_bytes + string_bytes;
		cd = cd->realloc(cd->realloc_user_data, cd, cd->total_bytes, new_total_bytes,
			__FILE__, __LINE__);
		memmove((char *)cd + new_allocated_bytes, (char *)cd + cd->allocated_bytes, string_bytes);
		cd->total_bytes = new_total_bytes;
		cd->allocated_bytes = new_allocated_bytes;
		*cdp = cd;
	}
	nfcd_loc loc = MAKE_LOC(type, cd->used_bytes);
	memcpy((char *)cd + cd->used_bytes, p, count);
	cd->used_bytes += count;
	memset((char *)cd + cd->used_bytes, 0, zeroes);
	cd->used_bytes += zeroes;
	return loc;
}

// Creates a new `nfcd_ConfigData` object. The `realloc` function will be used for 
// allocating the data. `config_size` and `stringtable_size` specify the original size
// of the config data and the string table data. You can use 0 for a default size.
struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int config_size, int stringtable_size)
{
	if (!config_size)
		config_size = 8*1024;
	if (!stringtable_size)
		stringtable_size = 8*1024;

	int total_bytes = config_size + stringtable_size;

	struct nfcd_ConfigData *cd = realloc(ud, NULL, 0, total_bytes, __FILE__, __LINE__);

	cd->total_bytes = total_bytes;
	cd->allocated_bytes = config_size;
	cd->used_bytes = sizeof(*cd);
	cd->root = NFCD_TYPE_NULL;
	cd->realloc = realloc;
	cd->realloc_user_data = ud;

	nfst_init(STRINGTABLE(cd), stringtable_size, 15);

	return cd;
}

// Frees an nfcd_ConfigData object created by nfcd_make.
void nfcd_free(struct nfcd_ConfigData *cd)
{
	cd->realloc(cd->realloc_user_data, cd, cd->total_bytes, 0, __FILE__, __LINE__);
}

// Returns the root item of the config data.
nfcd_loc nfcd_root(struct nfcd_ConfigData *cd)
{
	return cd->root;
}

// Returns the type of the config data item `loc`.
int nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return LOC_TYPE(loc);
}

// Returns the numeric representation of `loc`.
double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return *(double *)((char *)cd + LOC_OFFSET(loc));
}

// Returns the string representation of `loc`.
const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return nfst_to_string(STRINGTABLE(cd), LOC_OFFSET(loc));
}

// Returns the number of array items in `loc`.
int nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc array)
{
	struct block *arr = (struct block *)((char *)cd + LOC_OFFSET(array));
	int sz = 0;
	sz += arr->size;
	while (arr->next_block) {
		arr = (struct block *)((char *)cd + LOC_OFFSET(arr->next_block));
		sz += arr->size;
	}
	return sz;
}

// Returns the item at index `i` of the array, or `nfcd_null()` if `i` is
// beyond the end of the array.
nfcd_loc nfcd_array_item(struct nfcd_ConfigData *cd, nfcd_loc array, int i)
{
	assert(i >= 0);
	struct block *arr = (struct block *)((char *)cd + LOC_OFFSET(array));
	while (arr->next_block && i >= arr->size) {
		i -= arr->size;
		arr = (struct block *)((char *)cd + LOC_OFFSET(arr->next_block));
	}
	if (i >= arr->size)
		return nfcd_null();
	nfcd_loc *items = (nfcd_loc *)(arr + 1);
	return items[i];
}

// Returns the number of key-value pairs in `loc`.
int nfcd_object_size(struct nfcd_ConfigData *cd, nfcd_loc obj)
{
	struct block *block = (struct block *)((char *)cd + LOC_OFFSET(obj));
	int sz = 0;
	sz += block->size;
	while (block->next_block) {
		block = (struct block *)((char *)cd + LOC_OFFSET(block->next_block));
		sz += block->size;
	}
	return sz;
}

// Returns the `i`th `object_item` in `loc`. Or `NULL` if `i` is beyond the
// end of the object.
static struct object_item *object_item(struct nfcd_ConfigData *cd, nfcd_loc object, int i)
{
	assert(i >= 0);
	struct block *block = (struct block *)((char *)cd + LOC_OFFSET(object));
	while (block->next_block && i >= block->size) {
		i -= block->size;
		block = (struct block *)((char *)cd + LOC_OFFSET(block->next_block));
	}
	if (i >= block->size)
		return NULL;
	struct object_item *items = (struct object_item *)(block + 1);
	return items + i;
}

// Returns the `i`th key as an `nfcd_loc`.
nfcd_loc nfcd_object_keyloc(struct nfcd_ConfigData *cd, nfcd_loc object, int i)
{
	struct object_item *item = object_item(cd, object, i);
	if (!item)
		return nfcd_null();
	return item->key;
}

// Returns the `i`th key as a string.
const char *nfcd_object_key(struct nfcd_ConfigData *cd, nfcd_loc object, int i)
{
	struct object_item *item = object_item(cd, object, i);
	if (!item)
		return NULL;
	return nfcd_to_string(cd, item->key);
}

// Returns the `i`th value.
nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, nfcd_loc object, int i)
{
	struct object_item *item = object_item(cd, object, i);
	if (!item)
		return nfcd_null();
	return item->value;
}

// Looks up the item with the key `key` in `object` and returns its value.
//
// If there is no item with the `key`, `nfcd_null()` is returned.
//
// Note that the lookup uses linear search O(n) to find the entry with the
// specified key. Objects are assumed to be "smallish" so that the extra
// overhead for more complicated lookups (hashtables, etc) is not needed.
// If you are dealing with an object with tens of thousands of keys, consider
// holding your own lookup structures to be able to quickly find items in
// the object, without the O(n) cost of `nfcd_object_lookup()`.
nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, nfcd_loc object, const char *key)
{
	nfcd_loc key_loc = MAKE_LOC(NFCD_TYPE_STRING, nfst_to_symbol_const(STRINGTABLE(cd), key));

	struct block *block = (struct block *)((char *)cd + LOC_OFFSET(object));
	while (1) {
		struct object_item *items = (struct object_item *)(block + 1);
		for (int i=0; i<block->size; ++i) {
			if (key_loc == items[i].key)
				return items[i].value;
		}
		if (block->next_block == 0)
			break;
		block = (struct block *)((char *)cd + LOC_OFFSET(block->next_block));
	}

	return nfcd_null();
}

// Returns the `null` object. 
nfcd_loc nfcd_null()
{
	return MAKE_LOC(NFCD_TYPE_NULL, 0);
}

// Returns the `false` object.
nfcd_loc nfcd_false()
{
	return MAKE_LOC(NFCD_TYPE_FALSE, 0);
}

// Returns the `true` object.
nfcd_loc nfcd_true()
{
	return MAKE_LOC(NFCD_TYPE_TRUE, 0);
}

// Adds the number `n` to the config data and returns its reference.
//
// Note that if there is not room enough for the number, the data will
// be reallocated and `*cd` will be modified. This is true for all
// functions that can add data to the config data.
nfcd_loc nfcd_add_number(struct nfcd_ConfigData **cdp, double n)
{
	return write(cdp, NFCD_TYPE_NUMBER, &n, sizeof(n), 0);
}

// Adds the string `s` to the config data nad returns its reference.
nfcd_loc nfcd_add_string(struct nfcd_ConfigData **cdp, const char *s)
{
	struct nfcd_ConfigData *cd = *cdp;
	struct nfst_StringTable *st = STRINGTABLE(cd);
	int sym = nfst_to_symbol(st, s);
	while (sym < 0) {
		int string_bytes = cd->total_bytes - cd->allocated_bytes;
		int new_string_bytes = string_bytes * 2;
		int new_total_bytes = cd->allocated_bytes + new_string_bytes;
		cd = cd->realloc(cd->realloc_user_data, cd, cd->total_bytes, new_total_bytes, __FILE__, __LINE__);
		cd->total_bytes = new_total_bytes;
		st = STRINGTABLE(cd);
		nfst_grow(st, new_string_bytes);
		sym = nfst_to_symbol(st, s);
		*cdp = cd;
	}

	return MAKE_LOC(NFCD_TYPE_STRING, sym);
}

// Adds a new array, preallocated to the specified size to the config data
// and returns its reference.
nfcd_loc nfcd_add_array(struct nfcd_ConfigData **cdp, int allocated_size)
{
	struct block a = {0};
	a.allocated_size = allocated_size;
	return write(cdp, NFCD_TYPE_ARRAY, &a, sizeof(a), allocated_size * sizeof(nfcd_loc));
}

// Adds a new object to the config adta and returns its reference.
nfcd_loc nfcd_add_object(struct nfcd_ConfigData **cdp, int allocated_size)
{
	struct block a = {0};
	a.allocated_size = allocated_size;
	return write(cdp, NFCD_TYPE_OBJECT, &a, sizeof(a), allocated_size * sizeof(struct object_item));
}

// Sets the root object of the config data.
void nfcd_set_root(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	cd->root = loc;
}

// Pushes `item` to the end of the `array`.
void nfcd_push(struct nfcd_ConfigData **cdp, nfcd_loc array, nfcd_loc item)
{
	struct block *arr = (struct block *)((char *)*cdp + LOC_OFFSET(array));
	while (arr->size == arr->allocated_size) {
		if (arr->next_block == 0)
			arr->next_block = nfcd_add_array(cdp, arr->allocated_size*2);
		arr = (struct block *)((char *)*cdp + LOC_OFFSET(arr->next_block));
	}
	nfcd_loc *items = (nfcd_loc *)(arr + 1);
	items[arr->size] = item;
	++arr->size;
}

// Sets the `key` to the `value` in the `object`.
void nfcd_set(struct nfcd_ConfigData **cdp, nfcd_loc object, const char *key, nfcd_loc value)
{
	nfcd_loc key_loc = nfcd_add_string(cdp, key);
	nfcd_set_loc(cdp, object, key_loc, value);
}

// Sets the `key` to the `value` in the `object`. Note that only string
// keys are allowed.
void nfcd_set_loc(struct nfcd_ConfigData **cdp, nfcd_loc object, nfcd_loc key, nfcd_loc value)
{
	struct block *block = (struct block *)((char *)*cdp + LOC_OFFSET(object));
	while (1) {
		struct object_item *items = (struct object_item *)(block + 1);
		for (int i=0; i<block->size; ++i) {
			if (items[i].key == key) {
				items[i].value = value;
				return;
			}
		}
		if (block->size < block->allocated_size)
			break;
		if (block->next_block == 0)
			block->next_block = nfcd_add_object(cdp, block->allocated_size*2);
		block = (struct block *)((char *)*cdp + LOC_OFFSET(block->next_block));
	}

	struct object_item *items = (struct object_item *)(block + 1);
	items[block->size].key = key;
	items[block->size].value = value;
	++block->size;
}

// Returns the allocateor and the user data of the config data.
nfcd_realloc nfcd_allocator(struct nfcd_ConfigData *cd, void **user_data)
{
	*user_data = cd->realloc_user_data;
	return cd->realloc;
}

#ifdef NFCD_UNIT_TEST

	#include <stdlib.h>
	#include <assert.h>

	struct memory_record
	{
		void *ptr;
		int size;
	};
	#define MAX_MEMORY_RECORDS 128
	static struct memory_record memlog[MAX_MEMORY_RECORDS];
	static int memlog_size = 0;
	
	static void *realloc_f(void *ud, void *ptr, int osize, int nsize, const char *file, int line)
	{
		void *nptr = realloc(ptr, nsize);

		if (ptr) {
			int index = -1;
			for (int i=0; i<memlog_size; ++i) {
				if (ptr == memlog[i].ptr)
					index = i;
			}
			assert(index >= 0);
			if (nsize > 0)
				memlog[index].size = nsize;
			else
				memlog[index] = memlog[--memlog_size];
		} else {
			assert(memlog_size < MAX_MEMORY_RECORDS);
			struct memory_record r = {.ptr = nptr, .size = nsize};
			memlog[memlog_size++] = r;
		}

		return nptr;
	}

	int main(int argc, char **argv)
	{
		struct nfcd_ConfigData *cd = nfcd_make(realloc_f, 0, 0, 0);
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		nfcd_set_root(cd, nfcd_false());
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_FALSE);
		nfcd_set_root(cd, nfcd_true());
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_TRUE);
		nfcd_set_root(cd, nfcd_null());
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		nfcd_set_root(cd, nfcd_add_number(&cd, 3.14));
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NUMBER);
		assert(nfcd_to_number(cd, nfcd_root(cd)) == 3.14);

		nfcd_set_root(cd, nfcd_add_string(&cd, "str"));
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_STRING);
		assert(strcmp(nfcd_to_string(cd, nfcd_root(cd)), "str") == 0);

		nfcd_loc arr = nfcd_add_array(&cd, 16);
		nfcd_push(&cd, arr, nfcd_add_number(&cd, 1));
		nfcd_push(&cd, arr, nfcd_add_number(&cd, 2));
		nfcd_push(&cd, arr, nfcd_add_number(&cd, 3));
		assert(nfcd_type(cd, arr) == NFCD_TYPE_ARRAY);
		assert(nfcd_array_size(cd, arr) == 3);
		assert(nfcd_type(cd, nfcd_array_item(cd, arr, 1)) == NFCD_TYPE_NUMBER);
		assert(nfcd_to_number(cd, nfcd_array_item(cd, arr,1)) == 2);
		assert(nfcd_type(cd, nfcd_array_item(cd, arr, 10)) == NFCD_TYPE_NULL);

		nfcd_loc obj = nfcd_add_object(&cd, 16);
		nfcd_set(&cd, obj, "name", nfcd_add_string(&cd, "Niklas"));
		nfcd_set(&cd, obj, "age", nfcd_add_number(&cd, 41));
		assert(nfcd_type(cd, obj) == NFCD_TYPE_OBJECT);
		assert(nfcd_object_size(cd, obj) == 2);
		assert(strcmp(nfcd_object_key(cd, obj, 1), "age") == 0);
		assert(nfcd_type(cd, nfcd_object_value(cd, obj, 0)) == NFCD_TYPE_STRING);
		assert(strcmp(nfcd_to_string(cd, nfcd_object_value(cd, obj, 0)), "Niklas") == 0);
		assert(nfcd_type(cd, nfcd_object_lookup(cd, obj, "age")) == NFCD_TYPE_NUMBER);
		assert(nfcd_to_number(cd, nfcd_object_lookup(cd, obj, "age")) == 41);
		assert(nfcd_type(cd, nfcd_object_lookup(cd, obj, "title")) == NFCD_TYPE_NULL);

		struct nfcd_ConfigData *copy = realloc_f(0, 0, 0, cd->total_bytes, __FILE__, __LINE__);
		memcpy(copy, cd, cd->total_bytes);
		assert(nfcd_type(copy, obj) == NFCD_TYPE_OBJECT);
		assert(nfcd_object_size(copy, obj) == 2);
		assert(strcmp(nfcd_object_key(copy, obj, 1), "age") == 0);
		assert(nfcd_type(copy, nfcd_object_value(copy, obj, 0)) == NFCD_TYPE_STRING);
		assert(strcmp(nfcd_to_string(copy, nfcd_object_value(copy, obj, 0)), "Niklas") == 0);
		assert(nfcd_type(copy, nfcd_object_lookup(copy, obj, "age")) == NFCD_TYPE_NUMBER);
		assert(nfcd_to_number(copy, nfcd_object_lookup(copy, obj, "age")) == 41);
		assert(nfcd_type(copy, nfcd_object_lookup(copy, obj, "title")) == NFCD_TYPE_NULL);

		nfcd_free(copy);
		nfcd_free(cd);
		assert(memlog_size == 0);
	}

#endif
