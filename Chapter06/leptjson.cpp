#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdlib.h>  /* NULL, malloc(), realloc(), free(), strtod() */
#include <string.h>  /* memcpy() */

#define LEPT_PARSE_STACK_INIT_SIZE 256

#define EXPECT(c, ch)    do {assert(*(c->json)==(ch)); c->json++;}while(0)
#define ISDIGIT(ch)      ((ch)>='0' && (ch)<='9')
#define ISDIGIT1TO9(ch)  ((ch)>='1' && (ch)<='9')
#define PUTC(c, ch)      do {*(char*)lept_context_push(c, sizeof(char)) = (ch);}while(0)

struct lept_context
{
	const char* json;
	char* stack;
	size_t size, top;
};

static void* lept_context_push(lept_context* c, size_t size)
{
	void* ret;
	assert(size > 0);
	if (c->top + size >= c->size)
	{
		if (c->size == 0)
			c->size = LEPT_PARSE_STACK_INIT_SIZE;
		while (c->top + size >= c->size)
			c->size += c->size >> 1;  /* c->size * 1.5 */
		c->stack = (char*)realloc(c->stack, c->size);
	}
	ret = c->stack + c->top;
	c->top += size;
	return ret;
}

static void* lept_context_pop(lept_context* c, size_t size)
{
	assert(c->top >= size);
	return c->stack + (c->top -= size);
}

static void lept_parse_whitespace(lept_context* c)
{
	const char* p = c->json;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	c->json = p;
}

static int lept_parse_literal(lept_context* c, lept_value* v, const char* literal, lept_type type)
{
	size_t i;
	EXPECT(c, literal[0]);
	for (i = 0; literal[i + 1]; i++)
	{
		if (c->json[i] != literal[i + 1])
			return LEPT_PARSE_INVALID_VALUE;
	}
	c->json += i;
	v->type = type;
	return LEPT_PARSE_OK;
}

static int lept_parse_number(lept_context* c, lept_value* v)
{
	const char* p = c->json;
	if (*p == '-')
		p++;
	if (*p == '0')
		p++;
	else
	{
		if (!ISDIGIT(*p))
			return LEPT_PARSE_INVALID_VALUE;
		do p++; while (ISDIGIT(*p));
	}
	if (*p == '.')
	{
		p++;
		if (!ISDIGIT(*p))
			return LEPT_PARSE_INVALID_VALUE;
		do p++; while (ISDIGIT(*p));
	}
	if (*p == 'e' || *p == 'E')
	{
		p++;
		if (*p == '+' || *p == '-')
			p++;
		do p++; while (ISDIGIT(*p));
	}
	errno = 0;
	v->n = strtod(c->json, NULL);
	if (errno == ERANGE && abs(v->n) == HUGE_VAL)
		return LEPT_PARSE_NUMBER_TOO_BIG;
	v->type = LEPT_NUMBER;
	c->json = p;
	return LEPT_PARSE_OK;
}

static const char* lept_parse_hex4(const char* p, unsigned* u)
{
	int i;
	*u = 0;
	for (i = 0; i < 4; i++)
	{
		char ch = *p++;
		*u <<= 4;
		if      (ch >= '0' && ch <= '9') *u |= ch - '0';
		else if (ch >= 'A' && ch <= 'F') *u |= ch - 'A' + 10;
		else if (ch >= 'a' && ch <= 'f') *u |= ch - 'a' + 10;
		else return NULL;
	}
	return p;
}

static void lept_encode_utf8(lept_context* c, unsigned u)
{
	if (u <= 0x7F)
		PUTC(c, u & 0xFF);
	else if (u <= 0x7FF)
	{
		PUTC(c, 0xC0 | ((u >> 6) & 0xFF));
		PUTC(c, 0x80 | ( u       & 0x3F));
	}
	else if (u <= 0xFFFF)
	{
		PUTC(c, 0xE0 | ((u >> 12) & 0xFF));
		PUTC(c, 0x80 | ((u >>  6) & 0x3F));
		PUTC(c, 0x80 | ( u        & 0x3F));
	}
	else
	{
		assert(u <= 0x10FFFF);
		PUTC(c, 0xF0 | ((u >> 18) & 0xFF));
		PUTC(c, 0x80 | ((u >> 12) & 0x3F));
		PUTC(c, 0x80 | ((u >>  6) & 0x3F));
		PUTC(c, 0x80 | ( u        & 0x3F));
	}
}

#define STRING_ERROR(ret) do {c->top = head; return ret;}while(0)

static int lept_parse_string_raw(lept_context* c, char** str, size_t* len)
{
	size_t head = c->top;
	unsigned u, u2; // for unicode to utf8
	const char* p;
	EXPECT(c, '\"');
	p = c->json;
	while (1)
	{
		char ch = *p++;
		switch (ch)
		{
		case '\"':
			*len = c->top - head;
			*str = (char*)lept_context_pop(c, *len);
			c->json = p;
			return LEPT_PARSE_OK;
		case '\\':
			switch (*p++)
			{
			case '\"': PUTC(c, '\"'); break;
			case '\\': PUTC(c, '\\'); break;
			case '/':  PUTC(c, '/' ); break;
			case 'b':  PUTC(c, '\b'); break;
			case 'f':  PUTC(c, '\f'); break;
			case 'n':  PUTC(c, '\n'); break;
			case 'r':  PUTC(c, '\r'); break;
			case 't':  PUTC(c, '\t'); break;
			case 'u':
				if (!(p = lept_parse_hex4(p, &u)))
					STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
				if (u >= 0xD800 && u <= 0xDBFF)
				{
					if (*p++ != '\\')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (*p++ != 'u')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (!(p = lept_parse_hex4(p, &u2)))
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
					if (u2<0xDC00 || u2>0xDFFF)
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
				}
				lept_encode_utf8(c, u);
				break;
			default:
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
			}
			break;
		case '\0':
			STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
		default:
			if ((unsigned char)ch < 0x20)
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
			PUTC(c, ch);
		}
	}
}

static int lept_parse_string(lept_context* c, lept_value* v)
{
	int ret;
	char* s;
	size_t len;
	ret = lept_parse_string_raw(c, &s, &len);
	if (ret == LEPT_PARSE_OK)
		lept_set_string(v, s, len);
	return ret;
}

static int lept_parse_value(lept_context* c, lept_value* v);

static int lept_parse_array(lept_context* c, lept_value* v)
{
	size_t i, size = 0;
	int ret;
	EXPECT(c, '[');
	lept_parse_whitespace(c);
	if (*c->json == ']')
	{
		c->json++;
		v->type = LEPT_ARRAY;
		v->size = 0;           // blank array
		v->e = NULL;
		return LEPT_PARSE_OK;
	}
	while (1)
	{
		lept_value e;
		lept_init(&e);
		if ((ret = lept_parse_value(c, &e))!=LEPT_PARSE_OK)
			break;
		memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));
		size++;
		lept_parse_whitespace(c);
		if (*c->json == ',')
		{
			c->json++;
			lept_parse_whitespace(c);
		}
		else if (*c->json == ']')
		{
			c->json++;
			v->type = LEPT_ARRAY;
			v->size = size;
			size *= sizeof(lept_value);
			memcpy(v->e = (lept_value*)malloc(size), lept_context_pop(c, size), size); // set all the elements in the e in one time.
			// e不能用lept_free()释放，因为e里面可能包含着字符串的地址，v需要该地址指向的字符串，因此不能释放
			return LEPT_PARSE_OK;
		}
		else
		{
			ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
			break;
		}
	}
	// pop and free values on the stack
	// 但是如果出错了，已经放到stack里面的需要被释放
	for (i=0;i<size;i++)
		lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));
	return ret;
}

static int lept_parse_object(lept_context* c, lept_value* v)
{
	size_t i, size;
	lept_member m;
	// m内的v、k也不需要用lept_free()释放，字符串需要用最外部的v访问
	int ret;
	EXPECT(c, '{');
	lept_parse_whitespace(c);
	if (*c->json == '}')
	{
		c->json++;
		v->type = LEPT_OBJECT;
		v->m = 0;
		v->msize = 0;
		return LEPT_PARSE_OK;
	}
	m.k = NULL;
	size = 0;
	while (1)
	{
		char* str;
		lept_init(&m.v);
		// parse key
		if (*c->json != '"')
		{
			ret = LEPT_PARSE_MISS_KEY;
			break;
		}
		ret = lept_parse_string_raw(c, &str, &m.klen);
		if (ret != LEPT_PARSE_OK)
			break;
		memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen);
		m.k[m.klen] = '\0';
		// parse ws colon ws
		lept_parse_whitespace(c);
		if (*c->json != ':')
		{
			ret = LEPT_PARSE_MISS_COLON;
			break;
		}
		c->json++;
		lept_parse_whitespace(c);
		// parse value
		ret = lept_parse_value(c, &m.v);
		if (ret != LEPT_PARSE_OK)
			break;
		memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(lept_member));
		size++;
		m.k = NULL;

		lept_parse_whitespace(c);
		if (*c->json == ',')
		{
			c->json++;
			lept_parse_whitespace(c);
		}
		else if (*c->json == '}')
		{
			size_t s = sizeof(lept_member)* size;
			c->json++;
			v->type = LEPT_OBJECT;
			v->msize = size;
			v->m = (lept_member*)malloc(s);
			memcpy(v->m, lept_context_pop(c, s), s);
			return LEPT_PARSE_OK;
		}
		else
		{
			ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
			break;
		}
	}
	// pop and free members on the stack
	free(m.k);
	for (i = 0; i < size; i++)
	{
		lept_member* wrong_m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
		free(wrong_m->k);
		lept_free(&wrong_m->v);
	}
	v->type = LEPT_NULL;
	return ret;

}

static int lept_parse_value(lept_context* c, lept_value* v)
{
	switch (*(c->json))
	{
	case 't':  return lept_parse_literal(c, v, "true", LEPT_TRUE);
	case 'f':  return lept_parse_literal(c, v, "false", LEPT_FALSE);
	case 'n':  return lept_parse_literal(c, v, "null", LEPT_NULL);
	case '\0': return LEPT_PARSE_EXPECT_VALUE;
	case '\"': return lept_parse_string(c, v);
	case '[':  return lept_parse_array(c, v);
	case '{':  return lept_parse_object(c, v);
	default:   return lept_parse_number(c, v);
	}
}

int lept_parse(lept_value* v, const char* json)
{
	lept_context c;
	int ret;
	assert(v != NULL);
	c.json = json;
	c.stack = NULL;
	c.size = c.top = 0;
	lept_init(v);
	lept_parse_whitespace(&c);
	ret = lept_parse_value(&c, v);
	if (ret == LEPT_PARSE_OK)
	{
		lept_parse_whitespace(&c);
		if (*(c.json) != '\0')
		{
			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
			v->type = LEPT_NULL;
		}
	}
	assert(c.top == 0);
	free(c.stack); // only one free stack
	return ret;
}
// free string
void lept_free(lept_value* v)
{
	assert(v != NULL);
	size_t i;
	switch (v->type)
	{
	case LEPT_STRING:
		free(v->s);
		break;
	case LEPT_ARRAY:
		for (i = 0; i < v->size; i++)
			lept_free(&v->e[i]);
		free(v->e);
		break;
	case LEPT_OBJECT:
		for (i = 0; i < v->msize; i++)
		{
			free(v->m[i].k);
			lept_free(&(v->m[i].v));
		}
		free(v->m);
		break;
	default:
		break;
	}
	v->type = LEPT_NULL;
}

lept_type lept_get_type(const lept_value* v)
{
	assert(v != NULL);
	return v->type;
}

int lept_get_boolean(const lept_value* v)
{
	assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
	return v->type == LEPT_TRUE;
}
void lept_set_boolean(lept_value* v, int b)
{
	lept_free(v);
	v->type = b ? LEPT_TRUE : LEPT_FALSE;
}

double lept_get_number(const lept_value* v)
{
	assert(v != NULL && v->type == LEPT_NUMBER);
	return v->n;
}
void lept_set_number(lept_value* v, double n)
{
	lept_free(v);
	v->n = n;
	v->type = LEPT_NUMBER;
}

const char* lept_get_string(const lept_value* v)
{
	assert(v != NULL && v->type == LEPT_STRING);
	return v->s;
}
size_t lept_get_string_length(const lept_value* v)
{
	assert(v != NULL && v->type == LEPT_STRING);
	return v->len;
}
void lept_set_string(lept_value* v, const char* s, size_t len)
{
	assert(v != NULL && (s != NULL || len == 0));
	lept_free(v);
	v->s = (char*)malloc(len + 1);
	memcpy(v->s, s, len);
	v->s[len] = '\0';
	v->len = len;
	v->type = LEPT_STRING;
}

size_t lept_get_array_size(const lept_value* v)
{
	assert(v!=NULL && v->type==LEPT_ARRAY);
	return v->size;
}
lept_value* lept_get_array_element(const lept_value* v, size_t index)
{
	assert(v!=NULL && v->type==LEPT_ARRAY);
	assert(index < v->size);
	return &(v->e[index]);
}

size_t lept_get_object_size(const lept_value* v)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->msize;
}
const char* lept_get_object_key(const lept_value* v, size_t index)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->msize);
	return v->m[index].k;
}
size_t lept_get_object_key_length(const lept_value* v, size_t index)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->msize);
	return v->m[index].klen;
}
lept_value* lept_get_object_value(const lept_value* v, size_t index)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->msize);
	return &(v->m[index].v);
}