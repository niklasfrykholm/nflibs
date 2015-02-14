#include <stdint.h>

struct nfcd_ConfigData;

enum {
	NFCD_TYPE_NIL, NFCD_TYPE_FALSE, NFCD_TYPE_TRUE, NFCD_TYPE_NUMBER, NFCD_TYPE_STRING,
	NFCD_TYPE_ARRAY, NFCD_TYPE_OBJECT
};

#define NFCD_TYPE_MASK (0x7)
#define NFCD_TYPE_BITS (3)

typedef uint32_t nfcd_loc;
typedef void * (*nfcd_realloc) (void *ud, void *ptr, int32_t osize, int32_t nsize, const char *file, int32_t line);

struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int32_t config_size, int32_t stringtable_size);

nfcd_loc nfcd_root(struct nfcd_ConfigData *cd);
int32_t nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc);
double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc);
const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc);

int32_t nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc loc);
nfcd_loc nfcd_array_element(struct nfcd_ConfigData *cd, int32_t i);

int32_t nfcd_object_size(struct nfcd_ConfigData *cd, nfcd_loc loc);
nfcd_loc nfcd_object_key(struct nfcd_ConfigData *cd, int32_t i);
nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, int32_t i);
nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, const char *key);

nfcd_loc nfcd_add_false(struct nfcd_ConfigData *cd);
nfcd_loc nfcd_add_true(struct nfcd_ConfigData *cd);
nfcd_loc nfcd_add_string(struct nfcd_ConfigData *cd, const char *s);
nfcd_loc nfcd_add_number(struct nfcd_ConfigData *cd, double n);
nfcd_loc nfcd_add_array(struct nfcd_ConfigData *cd, int32_t size, nfcd_loc *data);
nfcd_loc nfcd_add_object(struct nfcd_ConfigData *cd, int32_t size, nfcd_loc *keys, nfcd_loc *values);
void nfcd_set_root(struct nfcd_ConfigData *cd, nfcd_loc root);

void nfcd_set_array_item(struct nfcd_ConfigData *cd, nfcd_loc array, int32_t index, nfcd_loc item);
void nfcd_set_object_value(struct nfcd_ConfigData *cd, nfcd_loc object, const char *key, nfcd_loc value);

// IMPLEMENTATION

struct nfst_StringTable;
void nfst_init(struct nfst_StringTable *st, int32_t bytes, int32_t average_string_size);

// nfcd_loc encodes type and offset into data
// array:  [size allocated loc...]
// object: [size allocated keys... values...]

struct nfcd_ConfigData
{
	int32_t allocated_bytes;
	struct nfst_StringTable *string_table;
	nfcd_loc root;
};

struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int32_t config_size, int32_t stringtable_size)
{
	if (!config_size)
		config_size = 8*1024;
	if (!stringtable_size)
		stringtable_size = 8*1024;

	struct nfcd_ConfigData *cd = realloc(ud, 0, 0, config_size, __FILE__, __LINE__);
	struct nfst_StringTable *st = realloc(ud, 0, 0, stringtable_size, __FILE__, __LINE__);

	cd->allocated_bytes = config_size;
	cd->string_table = st;
	cd->root = NFCD_TYPE_NIL;

	nfst_init(st, stringtable_size, 15);

	return cd;
}

nfcd_loc nfcd_root(struct nfcd_ConfigData *cd)
{
	return cd->root;
}

int32_t nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return (loc & NFCD_TYPE_MASK);
}

#ifdef NFCD_UNIT_TEST

	#include <stdlib.h>

	static void *realloc_f(void *ud, void *ptr, int32_t osize, int32_t nsize, const char *file, int32_t line)
	{
		return realloc(ptr, nsize);
	}


	int main(int argc, char **argv)
	{
		struct nfcd_ConfigData *cd = nfcd_make(realloc_f, 0, 0, 0);
	}

#endif
