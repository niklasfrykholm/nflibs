struct nfcd_ConfigData;

//

struct nfjp_Settings
{
	int comments;
	int naked_strings;
	int implicit_root_object;
	int ommit_commas;
	int equal_sign;
	int python_multiline_strings;	
};

const char *nfjp_parse(const char *s, struct nfcd_ConfigData **cdp);

// ## Implementation

#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <memory.h>

typedef int nfcd_loc;
typedef void * (*nfcd_realloc) (void *ud, void *ptr, int osize, int nsize, const char *file, int line);
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
nfcd_realloc nfcd_allocator(struct nfcd_ConfigData *cd, void **user_data);

struct Parser
{
	const char *s;
	int line_number;
	struct nfcd_ConfigData **cdp;
	char *error;
	jmp_buf env;
};

/*
struct NfcdLocVector
{
	int allocated;
	int n;
	nfcd_loc *data;
};
*/

static nfcd_loc parse_value(struct Parser *p);
static nfcd_loc parse_object(struct Parser *p);
static nfcd_loc parse_members(struct Parser *p);
static nfcd_loc parse_key(struct Parser *p);
static nfcd_loc parse_array(struct Parser *p);
static nfcd_loc parse_elements(struct Parser *p);
static nfcd_loc parse_name(struct Parser *p);
static nfcd_loc parse_string(struct Parser *p);
static nfcd_loc parse_number(struct Parser *p);
static nfcd_loc parse_true(struct Parser *p);
static nfcd_loc parse_false(struct Parser *p);
static nfcd_loc parse_null(struct Parser *p);

static void skip_whitespace(struct Parser *p);
static void skip_char(struct Parser *p, char c);

static void error(struct Parser *p, const char *s, ...);

#define CHAR_BUFFER_STATIC_SIZE 128
struct CharBuffer
{
	int allocated;
	int n;
	char *s;
	char buffer[CHAR_BUFFER_STATIC_SIZE];
};
static void cb_grow(struct Parser *p, struct CharBuffer *cb);
static inline void cb_push(struct Parser *p, struct CharBuffer *cb, char c);

static void *temp_realloc(struct Parser *p, void *optr, int osize, int nsize);

static unsigned parse_codepoint(struct Parser *p);
static void cb_push_utf8_codepoint(struct Parser *p, struct CharBuffer *cb, unsigned codepoint);

const char *nfjp_parse(const char *s, struct nfcd_ConfigData **cdp)
{
	struct Parser p = {s, 1, cdp, 0};
	if (setjmp(p.env))
		return p.error;
	skip_whitespace(&p);
	nfcd_loc root = parse_value(&p);
	skip_whitespace(&p);
	if (*p.s)
		error(&p, "Unexpected character `%c`", *p.s);
	nfcd_set_root(*cdp, root);
	return 0;
}

nfcd_loc parse_value(struct Parser *p)
{
	if (*p->s == '"')
		return parse_string(p);
	if ((*p->s >= '0' && *p->s <= '9') || *p->s=='-')
		return parse_number(p);
	//if (*p->s == '{')
	//	return parse_object(p);
	//lse if (*p->s == '[')
	//	return parse_array(p);
	else if (*p->s == 't')
		return parse_true(p);
	else if (*p->s == 'f')
		return parse_false(p);
	else if (*p->s == 'n')
		return parse_null(p);
	else
		error(p, "Unexpected character `%c`", *p->s);

	return nfcd_null();
}

nfcd_loc parse_string(struct Parser *p)
{
	struct CharBuffer cb = {0};
	skip_char(p, '"');

	while (1) {
		if (*p->s == 0 || *p->s == '"')
			break;
		else if (*p->s < 32)
			error(p, "Literal control character in string");
		else if (*p->s == '\\') {
			++p->s;
			char c = *p->s;
			++p->s;
			switch (c) {
				case '"': case '\\': case '/': cb_push(p, &cb, c); break;
				case 'b': cb_push(p, &cb, '\b'); break;
				case 'f': cb_push(p, &cb, '\f'); break;
				case 'n': cb_push(p, &cb, '\n'); break;
				case 'r': cb_push(p, &cb, '\r'); break;
				case 't': cb_push(p, &cb, '\t'); break;
				case 'u': cb_push_utf8_codepoint(p, &cb, parse_codepoint(p)); break;
				default: error(p, "Unexpected character `%c`", *p->s);
			}
		} else {
			cb_push(p, &cb, *p->s);
			++p->s;
		}
	}

	skip_char(p, '"');
	cb_push(p, &cb, 0);
	return nfcd_add_string(p->cdp, cb.s);
}

static nfcd_loc parse_number(struct Parser *p)
{
	int sign = 1;
	if (*p->s == '-') {
		sign = -1;
		++p->s;
	}

	int intp = 0;
	if (*p->s == '0') {
		++p->s;
	} else if (*p->s >= '1' && *p->s <= '9' ) {
		intp = (*p->s - '0');
		++p->s;
		while (*p->s >= '0' && *p->s <= '9' ) {
			intp = 10*intp + (*p->s - '0');
			++p->s;
		}
	} else
		error(p, "Bad number format");

	int fracp = 0;
	int fracdiv = 1;
	if (*p->s == '.') {
		++p->s;
		if (*p->s < '0' || *p->s > '9')
			error(p, "Bad number format");
		while (*p->s >= '0' && *p->s <= '9') {
			fracp = 10*fracp + (*p->s - '0');
			fracdiv *= 10;
			++p->s;
		}
	}

	int esign = 1;
	int ep = 0;
	if (*p->s == 'e' || *p->s == 'E') {
		++p->s;

		if (*p->s == '+')
			++p->s;
		else if (*p->s == '-') {
			esign = -1;
			++p->s;
		}

		if (*p->s >= '0' && *p->s <= '9') {
			ep = (*p->s - '0');
			++p->s;
		} else
			error(p, "Bad number format");

		while (*p->s >= '0' && *p->s <= '9') {
			ep = ep*10 + (*p->s - '0');
			++p->s;
		}
	}

	double v = (double)sign * ((double)intp + (double)fracp/(double)fracdiv) 
		* pow(10.0, (double)esign * (double)ep);

	return nfcd_add_number(p->cdp, v);
}

/*
nfcd_loc parse_object(struct Parser *p)
{
	skip_char('{');
	skip_whitespace();
	nfcd_loc obj;
	if (*p->s == '}')
		obj = nfcd_add_object(p->cdp, 0);
	else
		obj = parse_members(p);
	skip_char('}');
	return obj;
}

nfcd_loc parse_name(struct Parser *p)
{
	return parse_string(p);
}

nfcd_loc parse_members(struct Parser *p)
{
	struct NfcdLocVector names = {0};
	struct NfcdLocVector values = {0};

	while (true) {
		nfcd_loc name = parse_name(p);
		push_nfcdloc(names, key);
		skip_whitespace();
		skip_char(':');
		skip_whitespace();
		nfcd_loc value = parse_value(p);
		push_nfcdloc(values, value);
		skip_whitesspace();
		if (*p->s == '}')
			break;
	}

	nfcd_loc obj = nfcd_add_object(p->cdp, names.n);
	for (int i=0; i<names.n; ++i)
		nfcd_set(p->cdp, names.data[i], values.data[i]);

	temp_free(names.data);
	temp_free(names.data);

	return obj;
}

nfcd_loc parse_array(struct Parser *p)
{
	skip_char('[');
	skip_whitespace();
	nfcd_loc arr;
	if (*p == ']')
		arr = nfcd_add_array(p->cdp, 0);
	else
		arr = parse_elements(p);
	return arr;
}

nfcd_loc parse_elements(struct Parser *p)
{
	struct NfcdLocVector elements = {0};

	while (1) {
		skip_whitespace();
		nfcd_loc element = parse_value();
		push(elements, element);
		skip_whitespace();
		if (*p->s == ']')
			break;
	}

	nfcd_loc arr = nfcd_add_array(p->cdp, elements.n);
	for (int i=0; i<elements.n; ++i)
		nfcd_push(p->cdp, arr, elements.data[i]);

	temp_free(elements.data);

	return arr;
}

*/

nfcd_loc parse_true(struct Parser *p)
{
	skip_char(p, 't');
	skip_char(p, 'r');
	skip_char(p, 'u');
	skip_char(p, 'e');
	return nfcd_true();
}

nfcd_loc parse_false(struct Parser *p)
{
	skip_char(p, 'f');
	skip_char(p, 'a');
	skip_char(p, 'l');
	skip_char(p, 's');
	skip_char(p, 'e');
	return nfcd_false();
}

nfcd_loc parse_null(struct Parser *p)
{
	skip_char(p, 'n');
	skip_char(p, 'u');
	skip_char(p, 'l');
	skip_char(p, 'l');
	return nfcd_null();
}

static void skip_whitespace(struct Parser *p)
{
	while (isspace(*p->s)) {
		if (*p->s == '\n')
			++p->line_number;
		++p->s;
	}
}

static void skip_char(struct Parser *p, char c)
{
	if (*p->s != c)
		error(p, "Expected `%c`, saw `%c`", c, *p->s);
	++p->s;
}

static void error(struct Parser *p, const char *format, ...)
{
	const int ERROR_BUFFER_SIZE = 80;

	// Not thread-safe. hmm...
	static char error[ERROR_BUFFER_SIZE];

	p->error = error;
	int n = sprintf(p->error, "%i: ", p->line_number);
	va_list ap;
	va_start(ap, format);
	vsnprintf(p->error + n, ERROR_BUFFER_SIZE-n, format, ap);
	va_end(ap);

	longjmp(p->env, -1);
}

static void cb_grow(struct Parser *p, struct CharBuffer *cb)
{
	if (cb->allocated == 0) {
		cb->allocated = CHAR_BUFFER_STATIC_SIZE;
		cb->s = cb->buffer;
		return;
	}

	int manual_copy = 0;
	if (cb->s == cb->buffer) {
		cb->s = 0;
		manual_copy = 1;
	}

	cb->s = temp_realloc(p, cb->s, cb->allocated, cb->allocated*2);
	cb->allocated *= 2;

	if (manual_copy)
		memcpy(cb->s, cb->buffer, cb->n);
}

static inline void cb_push(struct Parser *p, struct CharBuffer *cb, char c)
{
	if (cb->n >= cb->allocated)
		cb_grow(p, cb);
	cb->s[cb->n++] = c;
}

/*
static void push(NfcdLocVector *v, nfcd_loc value)
{
	if (v->n >= v->allocated) {
		int size = v->allocated < 16 ? 16 : v->allocated * 2;
		v->data = temp_realloc(v->data, sizeof(nfcd_loc) * size);
	}
	v->data[v->n++] = value;
}
*/

static unsigned parse_codepoint(struct Parser *p)
{
	unsigned codepoint = 0;
	for (int i=0; i<4; ++i) {
		codepoint <<= 4;
		char c = *p->s;
		if (c >= 'a' && c <= 'f')
			codepoint += (c - 'a') + 10;
		else if (c >= 'A' && c <= 'F')
			codepoint += (c - 'A') + 10;
		else if (c >= '0' && c <= '9')
			codepoint += c - '0';
		else
			error(p, "Unexpected character `%c`", c);
		++p->s;
	}
	return codepoint;
}

static void cb_push_utf8_codepoint(struct Parser *p, struct CharBuffer *cb, unsigned codepoint)
{
	if (codepoint <= 0x7fu)
		cb_push(p, cb, codepoint);
	else if (codepoint <= 0x7ffu) {
		cb_push(p, cb, 0xc0 | ((codepoint >> 6) & 0x1f));
		cb_push(p, cb, 0x80 | ((codepoint >> 0) & 0x3f));
	} else if (codepoint <= 0xffffu) {
		cb_push(p, cb, 0xe0 | ((codepoint >> 12) & 0x0f));
		cb_push(p, cb, 0x80 | ((codepoint >> 6) & 0x3f));
		cb_push(p, cb, 0x80 | ((codepoint >> 0) & 0x3f));
	} else if (codepoint <= 0x1fffffu) {
		cb_push(p, cb, 0xf0 | ((codepoint >> 18) & 0x07));
		cb_push(p, cb, 0x80 | ((codepoint >> 12) & 0x3f));
		cb_push(p, cb, 0x80 | ((codepoint >> 6) & 0x3f));
		cb_push(p, cb, 0x80 | ((codepoint >> 0) & 0x3f));
	} else
		error(p, "Not an UTF-8 codepoint `%u`", codepoint);
}

static void *temp_realloc(struct Parser *p, void *optr, int osize, int nsize)
{
	void *realloc_ud;
	nfcd_realloc realloc_f = nfcd_allocator(*p->cdp, &realloc_ud);
	return realloc_f(realloc_ud, optr, osize, nsize, __FILE__, __LINE__);
}

#ifdef NFJP_UNIT_TEST

	#include <stdlib.h>
	#include <assert.h>
	#include <string.h>

	enum {
		NFCD_TYPE_NULL, NFCD_TYPE_FALSE, NFCD_TYPE_TRUE, NFCD_TYPE_NUMBER, NFCD_TYPE_STRING,
		NFCD_TYPE_ARRAY, NFCD_TYPE_OBJECT
	};

	typedef void * (*nfcd_realloc) (void *ud, void *ptr, int osize, int nsize, const char *file, int line);
	struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int config_size, int stringtable_size);
	nfcd_loc nfcd_root(struct nfcd_ConfigData *cd);
	int nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc);
	double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc);
	const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc);
	int nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc arr);
	nfcd_loc nfcd_array_item(struct nfcd_ConfigData *cd, nfcd_loc arr, int i);
	int nfcd_object_size(struct nfcd_ConfigData *cd, nfcd_loc object);
	const char *nfcd_object_key(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
	nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
	nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, nfcd_loc object, const char *key);

	static void *realloc_f(void *ud, void *ptr, int osize, int nsize, const char *file, int line)
	{
		return realloc(ptr, nsize);
	}

	void assert_strequal(const char *s, const char *expected)
	{
		if (strcmp(s, expected)) {
			fprintf(stderr, "Expected `%s`, saw `%s`\n", expected, s);
			assert(0);
		}
	}

	#define assert_numequal(v, e) assert(fabsf((v)-(e)) < 1e-7);

	int main(int argc, char **argv)
	{
		struct nfcd_ConfigData *cd = nfcd_make(realloc_f, 0, 0, 0);
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		{
			char *s = "null";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);
		}
		{
			char *s = "true";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_TRUE);
		}
		{
			char *s = "false";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_FALSE);
		}

		{
			char *s = "fulse";
			const char *err = nfjp_parse(s, &cd);
			assert_strequal(err, "1: Expected `a`, saw `u`");
		}

		{
			char *s = "\n\n    \tfalse   \n\n";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_FALSE);
		}
		{
			char *s = "\n\nfulse";
			const char *err = nfjp_parse(s, &cd);
			assert_strequal(err, "3: Expected `a`, saw `u`");
		}
		{
			char *s = "\n\n    \tfalse   \n\nx";
			const char *err = nfjp_parse(s, &cd);
			assert_strequal(err, "5: Unexpected character `x`");
		}

		{
			char *s = "3.14";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NUMBER);
			assert_numequal(nfcd_to_number(cd, nfcd_root(cd)), 3.14);
		}

		{
			char *s = "-3.14e-1";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NUMBER);
			assert_numequal(nfcd_to_number(cd, nfcd_root(cd)), -0.314);
		}

		{
			char *s[] = {"--3.14", ".1", "-.1", "00", "00.0", "0e",
				"0.", "0.e1", "0.0ee", "0.0++e"};
			int n = sizeof(s) / sizeof(s[0]);
			for (int i=0; i<n; ++i) {
				const char *err = nfjp_parse(s[i], &cd);
				assert(err != 0);
			}
		}

		{
			char *s = "\"niklas\"";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_STRING);
			assert_strequal(nfcd_to_string(cd, nfcd_root(cd)), "niklas");
		}

		{
			char *s = "\"01234567890123456789012345678901234567890123456789"
				        "01234567890123456789012345678901234567890123456789"
				        "01234567890123456789012345678901234567890123456789"
				        "01234567890123456789012345678901234567890123456789\"";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_STRING);
			assert(strlen(nfcd_to_string(cd, nfcd_root(cd))) == 200);
		}

		{
			char *s = "\"\n\"";
			const char *err = nfjp_parse(s, &cd);
			assert_strequal(err, "1: Literal control character in string");
		}

		{
			char *s = "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_STRING);
			assert_strequal(nfcd_to_string(cd, nfcd_root(cd)), "\"\\/\b\f\n\r\t");
		}

		{
			char *s = "\"\\u00e4\\u6176\"";
			char *dec = "ä慶";
			const char *err = nfjp_parse(s, &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_STRING);
			const char *res = nfcd_to_string(cd, nfcd_root(cd));
			assert_strequal(res, dec);
		}
	}

#endif
