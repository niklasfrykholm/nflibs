#include <stdint.h>

struct nfcd_ConfigData;

enum {
	NFCD_TYPE_NIL, NFCD_TYPE_FALSE, NFCD_TYPE_TRUE, NFCD_TYPE_NUMBER, NFCD_TYPE_STRING,
	NFCD_TYPE_ARRAY, NFCD_TYPE_OBJECT,
};

typedef int32_t nfcd_loc;
typedef void * (*nfcd_realloc) (void *ud, void *ptr, int32_t osize, int32_t nsize, const char *file, int32_t line);

struct nfcd_ConfigData *nfcd_make(int32_t size, nfcd_realloc realloc, void *ud);

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

// nfcd_loc encodes type and offset into data
// array:  [size allocated loc...]
// object: [size allocated keys... values...]

struct nfcd_ConfigData
{
	int32_t allocated_bytes;
	struct nfst_StringTable *string_table;
};

struct nfcd_ConfigData *nfcd_make(int32_t size, nfcd_realloc realloc, void *ud)
{
	if (!size)
		size = 16*1024;

	struct nfcd_ConfigData *cd = realloc(ud, 0, 0, size/2, __FILE__, __LINE__);
	struct nfst_StringTable *st = realloc(ud, 0, 0, size/2, __FILE__, __LINE__);

	cd->allocated_bytes = size/2;
	cd->string_table = st;

	return cd;
}

#ifdef NFCD_UNIT_TEST

	#include <stdlib.h>

	static void *realloc_f(void *ud, void *ptr, int32_t osize, int32_t nsize, const char *file, int32_t line)
	{
		return realloc(ptr, nsize);
	}


	int main(int argc, char **argv)
	{
		struct nfcd_ConfigData *cd = nfcd_make(0, realloc_f, 0);
	}

#endif
