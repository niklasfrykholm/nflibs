struct nfcd_ConfigData;

struct nfjp_Settings
{
	int unquoted_names;
	int c_comments;
	int implicit_root_object;
	int optional_commas;
	int equals_for_colon;
	int python_multiline_strings;
};

const char *nfjp_parse(const char *s, struct nfcd_ConfigData **cdp);
const char *nfjp_parse_with_settings(const char *s, struct nfcd_ConfigData **cdp, struct nfjp_Settings *settings);

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
void nfcd_set_loc(struct nfcd_ConfigData **cdp, nfcd_loc object, nfcd_loc key, nfcd_loc value);
nfcd_realloc nfcd_allocator(struct nfcd_ConfigData *cd, void **user_data);

struct Parser
{
	const char *s;
	int line_number;
	struct nfcd_ConfigData **cdp;
	struct nfjp_Settings *settings;
	char *error;
	jmp_buf env;
};

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
static void cb_free(struct Parser *p, struct CharBuffer *cb);
static inline void cb_push(struct Parser *p, struct CharBuffer *cb, char c);

#define LOC_BUFFER_STATIC_SIZE 128
struct LocBuffer
{
	int allocated;
	int n;
	nfcd_loc *data;
	nfcd_loc buffer[LOC_BUFFER_STATIC_SIZE];
};
static void lb_grow(struct Parser *p, struct LocBuffer *lb);
static void lb_free(struct Parser *p, struct LocBuffer *lb);
static inline void lb_push(struct Parser *p, struct LocBuffer *lb, nfcd_loc loc);

static void *temp_realloc(struct Parser *p, void *optr, int osize, int nsize);

static unsigned parse_codepoint(struct Parser *p);
static void cb_push_utf8_codepoint(struct Parser *p, struct CharBuffer *cb, unsigned codepoint);

const char *nfjp_parse(const char *s, struct nfcd_ConfigData **cdp)
{
	struct nfjp_Settings settings = {0};
	return nfjp_parse_with_settings(s, cdp, &settings);
}

const char *nfjp_parse_with_settings(const char *s, struct nfcd_ConfigData **cdp, struct nfjp_Settings *settings)
{
	struct Parser p = {s, 1, cdp, settings, 0};
	if (setjmp(p.env))
		return p.error;
	skip_whitespace(&p);
	nfcd_loc root = -1;
	if (p.settings->implicit_root_object && *p.s != '{') {
		if (*p.s == 0)
			root = nfcd_add_object(p.cdp, 0);
		else
			root = parse_members(&p);
	} else {
		root = parse_value(&p);
	}
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
	else if ((*p->s >= '0' && *p->s <= '9') || *p->s=='-')
		return parse_number(p);
	else if (*p->s == '{')
		return parse_object(p);
	else if (*p->s == '[')
		return parse_array(p);
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

	if (p->settings->python_multiline_strings && *p->s == '"' && p->s[1] == '"') {
		p->s += 2;
		while (*p->s && p->s[1] && p->s[2] &&
			(*p->s != '"' || p->s[1] != '"' || p->s[2] != '"' || p->s[3] == '"')) {
			cb_push(p, &cb, *p->s);
			++p->s;
		}
		skip_char(p, '"');
		skip_char(p, '"');
		skip_char(p, '"');
		cb_push(p, &cb, 0);
		nfcd_loc loc = nfcd_add_string(p->cdp, cb.s);
		cb_free(p, &cb);
		return loc;
	}

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
	nfcd_loc loc = nfcd_add_string(p->cdp, cb.s);
	cb_free(p, &cb);
	return loc;
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

nfcd_loc parse_object(struct Parser *p)
{
	skip_char(p, '{');
	skip_whitespace(p);
	nfcd_loc obj;
	if (*p->s == '}')
		obj = nfcd_add_object(p->cdp, 0);
	else
		obj = parse_members(p);
	skip_char(p, '}');
	return obj;
}

#define isbarename(c) \
	( ((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || \
	  ((c) >= '0' && (c) <= '9') || c == '_' || c == '-' )

nfcd_loc parse_name(struct Parser *p)
{
	skip_whitespace(p);
	if (p->settings->unquoted_names && isbarename(*p->s)) {
		struct CharBuffer cb = {0};
		while (isbarename(*p->s)) {
			cb_push(p, &cb, *p->s);
			++p->s;
		}
		cb_push(p, &cb, 0);
		nfcd_loc loc = nfcd_add_string(p->cdp, cb.s);
		cb_free(p, &cb);
		return loc;
	}

	return parse_string(p);
}

#undef isbarename

nfcd_loc parse_members(struct Parser *p)
{
	struct LocBuffer names = {0};
	struct LocBuffer values = {0};

	while (1) {
		nfcd_loc name = parse_name(p);
		lb_push(p, &names, name);
		skip_whitespace(p);
		if (p->settings->equals_for_colon && *p->s == '=')
			skip_char(p, '=');
		else
			skip_char(p, ':');
		skip_whitespace(p);
		nfcd_loc value = parse_value(p);
		lb_push(p, &values, value);
		skip_whitespace(p);
		if (*p->s == '}' || *p->s == 0)
			break;
		if (!p->settings->optional_commas)
			skip_char(p, ',');
		skip_whitespace(p);
	}

	nfcd_loc obj = nfcd_add_object(p->cdp, names.n);
	for (int i=0; i<names.n; ++i)
		nfcd_set_loc(p->cdp, obj, names.data[i], values.data[i]);

	lb_free(p, &names);
	lb_free(p, &values);

	return obj;
}

nfcd_loc parse_array(struct Parser *p)
{
	skip_char(p, '[');
	skip_whitespace(p);
	nfcd_loc arr;
	if (*p->s == ']') {
		skip_char(p, ']');
		arr = nfcd_add_array(p->cdp, 0);
	} else {
		arr = parse_elements(p);
	}
	return arr;
}

nfcd_loc parse_elements(struct Parser *p)
{
	struct LocBuffer elements = {0};

	while (1) {
		skip_whitespace(p);
		nfcd_loc element = parse_value(p);
		lb_push(p, &elements, element);
		skip_whitespace(p);
		if (*p->s == ']')
			break;
		if (!p->settings->optional_commas)
			skip_char(p, ',');
	}
	skip_char(p, ']');

	nfcd_loc arr = nfcd_add_array(p->cdp, elements.n);
	for (int i=0; i<elements.n; ++i)
		nfcd_push(p->cdp, arr, elements.data[i]);

	lb_free(p, &elements);
	return arr;
}

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
	while (isspace(*p->s) || *p->s == '/' || *p->s == ',') {
		if (*p->s == '\n') {
			++p->line_number;
			++p->s;
		} else if (isspace(*p->s)) {
			++p->s;
		} else if (*p->s == '/' && p->settings->c_comments) {
			// C++ style comment
			if (p->s[1] == '/') {
				while (*p->s && *p->s != '\n')
					++p->s;
				++p->line_number;
				++p->s;
			// C style comment
			} else if (p->s[1] == '*') {
				p->s += 2;
				while (*p->s && !(*p->s == '*' && p->s[1] == '/')) {
					if (*p->s == '\n')
						++p->line_number;
					++p->s;
				}
				skip_char(p, '*');
				skip_char(p, '/');
			} else
				return;
		} else if (*p->s ==',' && p->settings->optional_commas) {
			++p->s;
		} else {
			return;
		}
	}
}

static void skip_char(struct Parser *p, char c)
{
	if (*p->s != c) {
		if (*p->s >= 32)
			error(p, "Expected `%c`, saw `%c`", c, *p->s);
		else
			error(p, "Expected `%c`, saw `\\x%02x`", c, *p->s);
	}
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

static void cb_free(struct Parser *p, struct CharBuffer *cb)
{
	if (cb->s != cb->buffer)
		temp_realloc(p, cb->s, cb->allocated, 0);
}

static inline void cb_push(struct Parser *p, struct CharBuffer *cb, char c)
{
	if (cb->n >= cb->allocated)
		cb_grow(p, cb);
	cb->s[cb->n++] = c;
}

static void lb_grow(struct Parser *p, struct LocBuffer *lb)
{
	if (lb->allocated == 0) {
		lb->allocated = LOC_BUFFER_STATIC_SIZE;
		lb->data = lb->buffer;
		return;
	}

	int manual_copy = 0;
	if (lb->data == lb->buffer) {
		lb->data = 0;
		manual_copy = 1;
	}

	lb->data = temp_realloc(p, lb->data, sizeof(nfcd_loc)*lb->allocated, sizeof(nfcd_loc)*lb->allocated*2);
	lb->allocated *= 2;

	if (manual_copy)
		memcpy(lb->data, lb->buffer, lb->n);
}

static void lb_free(struct Parser *p, struct LocBuffer *lb)
{
	if (lb->data != lb->buffer)
		temp_realloc(p, lb->data, sizeof(nfcd_loc)*lb->allocated, 0);
}

static inline void lb_push(struct Parser *p, struct LocBuffer *lb, nfcd_loc loc)
{
	if (lb->n >= lb->allocated)
		lb_grow(p, lb);
	lb->data[lb->n++] = loc;
}

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
	nfcd_loc nfcd_object_keyloc(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
	const char *nfcd_object_key(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
	nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, nfcd_loc object, int i);
	nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, nfcd_loc object, const char *key);

	static void *realloc_f(void *ud, void *ptr, int osize, int nsize, const char *file, int line)
	{
		return realloc(ptr, nsize);
	}

	static void fail(const char *s, const char *format, ...)
	{
		const int ERROR_BUFFER_SIZE = 200;
		char error[ERROR_BUFFER_SIZE];

		va_list ap;
		va_start(ap, format);
		vsnprintf(error, ERROR_BUFFER_SIZE, format, ap);
		va_end(ap);

		fprintf(stderr, "%s\n\n%s\n", s, error);
		exit(1);
	}

	static void test_error(struct nfjp_Settings *settings, struct nfcd_ConfigData **cd, const char *s, const char *expected_err)
	{
		const char *err = nfjp_parse_with_settings(s, cd, settings);
		if (err == 0)
			fail(s, "Expected error `%s`, saw no error", expected_err);
		if (expected_err == 0 || strcmp(err, expected_err) != 0)
			fail(s, "Expected error `%s`, saw `%s`", expected_err, err);
	}

	void test(struct nfjp_Settings *settings, struct nfcd_ConfigData **cd, const char *json, const char *format, ...)
	{
		const char *err = nfjp_parse_with_settings(json, cd, settings);
		if (err)
			fail(json, "%s", err);
		nfcd_loc root = nfcd_root(*cd);

		const int STACK_MAX = 16;
		nfcd_loc stack[STACK_MAX];
		int stack_top = 0;
		stack[stack_top++] = root;

		va_list vl;
		va_start(vl, format);

		nfcd_loc END_OF_ARRAY = -1;
		nfcd_loc END_OF_OBJECT = -2;

		while (*format) {
			if (stack_top == 0)
				fail(json, "Stack empty!");
			nfcd_loc item = stack[--stack_top];
			switch (*format) {
				case 'n': if (nfcd_type(*cd, item) != NFCD_TYPE_NULL) fail(json, "Expected `null`"); break;
				case 't': if (nfcd_type(*cd, item) != NFCD_TYPE_TRUE) fail(json, "Expected `true`"); break;
				case 'f': if (nfcd_type(*cd, item) != NFCD_TYPE_FALSE) fail(json, "Expected `false`"); break;
				case 'd': {
					double number = nfcd_to_number(*cd, item);
					double expected_number = va_arg(vl, double);
					if (fabsf(number-expected_number) > 1e-1)
						fail(json, "Expected `%lf`, saw `%lf`", expected_number, number);
					break;
				}
				case 's': case 'k': {
					const char *string = nfcd_to_string(*cd, item);
					const char *expected_string = va_arg(vl, const char *);
					if (strcmp(string, expected_string))
						fail(json, "Expected `%s`, saw `%s`", expected_string, string);
					break;
				}
				case '[': {
					if (nfcd_type(*cd, item) != NFCD_TYPE_ARRAY)
						fail(json, "Expected array");
					assert(stack_top < STACK_MAX);
					stack[stack_top++] = END_OF_ARRAY;
					int n = nfcd_array_size(*cd, item);
					for (int i=n-1; i>=0; --i) {
						nfcd_loc ai = nfcd_array_item(*cd, item, i);
						assert(stack_top < STACK_MAX);
						stack[stack_top++] = ai;
					}
					break;
				}
				case ']':
					if (item != END_OF_ARRAY)
						fail(json, "Did not match end of array");
					break;
				case '{': {
					if (nfcd_type(*cd, item) != NFCD_TYPE_OBJECT)
						fail(json, "Expected object");
					assert(stack_top < STACK_MAX);
					stack[stack_top++] = END_OF_OBJECT;
					int n = nfcd_object_size(*cd, item);
					for (int i=n-1; i>=0; --i) {
						nfcd_loc ok = nfcd_object_keyloc(*cd, item, i);
						nfcd_loc ov = nfcd_object_value(*cd, item, i);
						assert(stack_top < STACK_MAX);
						stack[stack_top++] = ov;
						assert(stack_top < STACK_MAX);
						stack[stack_top++] = ok;
					}
					break;
				}
				case '}':
					if (item != END_OF_OBJECT)
						fail(json, "Did not match end of object");
					break;
				default: fail(json, "Bad format flag `%c`", *format);
			}
			++format;
		}

		va_end(vl);

		if (stack_top != 0)
			fail(json, "Unconsumed items");
	}

	int main(int argc, char **argv)
	{
		struct nfjp_Settings s = {0};

		struct nfcd_ConfigData *cd = nfcd_make(realloc_f, 0, 0, 0);
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		test(&s, &cd, "null", "n");
		test(&s, &cd, "true", "t");
		test(&s, &cd, "false", "f");
		test_error(&s, &cd, "fulse", "1: Expected `a`, saw `u`");
		test(&s, &cd, "\n\n    \tfalse   \n\n", "f");
		test_error(&s, &cd, "\n\n    \tfalse   \n\nx", "5: Unexpected character `x`");
		test_error(&s, &cd, "\n\nfulse", "3: Expected `a`, saw `u`");
		test(&s, &cd, "3.14", "d", 3.14);
		test(&s, &cd, "-3.14e-1", "d", -0.314);
		test_error(&s, &cd, "--3.14", "1: Bad number format");
		test_error(&s, &cd, ".1", "1: Unexpected character `.`");
		test_error(&s, &cd, "-.1", "1: Bad number format");
		test_error(&s, &cd, "00", "1: Unexpected character `0`");
		test_error(&s, &cd, "00.0", "1: Unexpected character `0`");
		test_error(&s, &cd, "0e", "1: Bad number format");
		test_error(&s, &cd, "0.", "1: Bad number format");
		test_error(&s, &cd, "0.e1", "1: Bad number format");
		test_error(&s, &cd, "0.0ee", "1: Bad number format");
		test_error(&s, &cd, "0.0++e", "1: Unexpected character `+`");
		test(&s, &cd, "\"niklas\"", "s", "niklas");
		test(&s, &cd,
			"\"01234567890123456789012345678901234567890123456789"
			"01234567890123456789012345678901234567890123456789"
			"01234567890123456789012345678901234567890123456789"
			"01234567890123456789012345678901234567890123456789\"", "s",
			"01234567890123456789012345678901234567890123456789"
			"01234567890123456789012345678901234567890123456789"
			"01234567890123456789012345678901234567890123456789"
			"01234567890123456789012345678901234567890123456789"
		);
		test_error(&s, &cd, "\"\n\"", "1: Literal control character in string");
		test(&s, &cd, "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", "s", "\"\\/\b\f\n\r\t");
		test(&s, &cd, "\"\\u00e4\\u6176\"", "s", "ä慶");
		test(&s, &cd, "[]", "[]");
		test(&s, &cd, "[1,2, 3 ,4 , 5 ]", "[ddddd]", 1.0, 2.0, 3.0, 4.0, 5.0);
		test_error(&s, &cd, "[1 2 3]", "1: Expected `,`, saw `2`");
		test(&s, &cd, "{}", "{}");
		test(&s, &cd, "{\"name\" : \"Niklas\", \"age\" : 41}", "{kskd}", "name", "Niklas", "age", 41.0);
		test_error(&s, &cd, "{1 2 3}", "1: Expected `\"`, saw `1`");
		test_error(&s, &cd, "{a: 10, b: 20}", "1: Expected `\"`, saw `a`");
		s.unquoted_names = 1;
		test(&s, &cd, "{a: 10, b: 20}", "{kdkd}", "a", 10.0, "b", 20.0);
		test_error(&s, &cd, "// Comment\n{a: 10, b: 20}", "1: Unexpected character `/`");
		s.c_comments = 1;
		test(&s, &cd, "// Comment\n{a: 10, b: 20}", "{kdkd}", "a", 10.0, "b", 20.0);
		test_error(&s, &cd, "// Bla\n/* Comment * /** // \n */\nz", "4: Unexpected character `z`");
		test_error(&s, &cd, "a:10, b:20", "1: Unexpected character `a`");
		s.implicit_root_object = 1;
		test(&s, &cd, "a:10, b:20", "{kdkd}", "a", 10.0, "b", 20.0);
		test_error(&s, &cd, "a:10 b:20", "1: Expected `,`, saw `b`");
		s.optional_commas = 1;
		test(&s, &cd, "a:10 b:20", "{kdkd}", "a", 10.0, "b", 20.0);
		test(&s, &cd, ",,a:10 b:20, , ,,", "{kdkd}", "a", 10.0, "b", 20.0);
		test_error(&s, &cd, "a=10 b=20", "1: Expected `:`, saw `=`");
		s.equals_for_colon = 1;
		test(&s, &cd, "a=10 b=20", "{kdkd}", "a", 10.0, "b", 20.0);
		s.implicit_root_object = 0;
		test_error(&s, &cd, "\"\"\" Bla \" Bla \"\"\"", "1: Unexpected character `\"`");
		s.python_multiline_strings = 1;
		test(&s, &cd, "\"\"\" Bla \" Bla \"\"\"", "s", " Bla \" Bla ");
		test(&s, &cd, "\"\"\"\"\" x \"\"\"\"\"", "s", "\"\" x \"\"");
	}

#endif
