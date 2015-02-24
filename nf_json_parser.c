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

const char *nfjp_parse(const char *s, int len, struct nfcd_ConfigData **cdp);

// ## Implementation

#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

typedef int nfcd_loc;
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

struct Parser
{
	const char *s;
	const char * const end;
	int line_number;
	struct nfcd_ConfigData **cdp;
	char *error;
	jmp_buf env;
};

struct NfcdLocVector
{
	int allocated;
	int n;
	nfcd_loc *data;
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
static void push(struct NfcdLocVector *vec, nfcd_loc value);

const char *nfjp_parse(const char *s, int len, struct nfcd_ConfigData **cdp)
{
	struct Parser p = {s, s+len, 1, cdp, 0};
	if (setjmp(p.env))
		return p.error;
	skip_whitespace(&p);
	nfcd_loc root = parse_value(&p);
	skip_whitespace(&p);
	if (p.s < p.end)
		error(&p, "Unexpected character `%c`", *p.s);
	nfcd_set_root(*cdp, root);
	return 0;
}

nfcd_loc parse_value(struct Parser *p)
{
	//if (*p->s == '"')
	//	return parse_string(p);
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

/*
nfcd_loc parse_string(struct Parser *p)
{
	skip_char(p, '"');

	// The most common case is a string with no quoted characters.
	// Detect this case and parse the string without any memory allocation.
	{
		char *start = p->s;
		char *s = p->s;
		while (s < end && s != '"' && s != '\\')
			++s;
		if (*s == '"') {
			++s;
			p->s = s;
			return nfcd_add_string(cdp, start, s-start);
		}
	}

	// Quoted string,
	{
		CharVector cv = {0};

		char *s = p->s;
		while (s < p->end) {
			if (*s == '"')
				break;

		}
	}
}
*/

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
		error(p, "Unxpected character `%c`", *p->s);

	int fracp = 0;
	int fracdiv = 1;
	if (*p->s == '.') {
		++p->s;
		while (*p->s >= '0' && *p->s <= '9') {
			fracp = 10*fracp + (*p->s - '0');
			fracdiv *= 10;
			++p->s;
		}
	}

	int esign = 1;
	int ep = 0;
	if (*p->s == 'e' || *p->s == 'E') {
		if (*p->s == '+')
			++p->s;
		else if (*p->s == '-') {
			ep = -1;
			++p->s;
		}

		if (*p->s >= '0' && *p->s <= '9') {
			ep = (*p->s - '0');
			++p->s;
		} else
			error(p, "Unexpected character `%c`", *p->s);

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
	while (p->s < p->end && isspace(*p->s)) {
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
			fprintf(stderr, "Expected `%s`, saw `%s`", expected, s);
			assert(0);
		}
	}

	int main(int argc, char **argv)
	{
		struct nfcd_ConfigData *cd = nfcd_make(realloc_f, 0, 0, 0);
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		{
			char *s = "null";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);
		}

		{
			char *s = "true";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_TRUE);
		}

		{
			char *s = "false";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_FALSE);
		}

		{
			char *s = "fulse";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert_strequal(err, "1: Expected `a`, saw `u`");
		}

		{
			char *s = "\n\n    \tfalse   \n\n";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_FALSE);
		}

		{
			char *s = "\n\nfulse";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert_strequal(err, "3: Expected `a`, saw `u`");
		}

		{
			char *s = "\n\n    \tfalse   \n\nx";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert_strequal(err, "5: Unexpected character `x`");
		}

		{
			char *s = "3.14";
			const char *err = nfjp_parse(s, strlen(s), &cd);
			assert(err == 0);
			assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NUMBER);
			assert(nfcd_to_number(cd, nfcd_root(cd)) == 3.14);
		}
	}

#endif