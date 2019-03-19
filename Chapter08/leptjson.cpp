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
#include <stdio.h>   // sprintf()


#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256
#endif

#define EXPECT(c, ch)    do {assert(*(c->json)==(ch)); c->json++;}while(0)
#define ISDIGIT(ch)      ((ch)>='0' && (ch)<='9')
#define ISDIGIT1TO9(ch)  ((ch)>='1' && (ch)<='9')
#define PUTC(c, ch)      do {*(char*)lept_context_push(c, sizeof(char)) = (ch);}while(0)
#define PUTS(c, s, len)  memcpy(lept_context_push(c, len), s, len)

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

static int lept_parse_value(lept_context* c, lept_value* v); // 声明

static int lept_parse_array(lept_context* c, lept_value* v)
{
	size_t i, size = 0;
	int ret;
	EXPECT(c, '[');
	lept_parse_whitespace(c);
	if (*c->json == ']')
	{
		c->json++;
		lept_set_array(v, 0);
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
			lept_set_array(v, size);
			memcpy(v->e, lept_context_pop(c, size * sizeof(lept_value)), size * sizeof(lept_value)); // set all the elements in the e in one time.
			// e不能用lept_free()释放，因为e里面可能包含着字符串的地址，v需要该地址指向的字符串，因此不能释放
			v->size = size;
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
	size_t i, size = 0;
	lept_member m;
	// m内的v、k也不需要用lept_free()释放，字符串需要用最外部的v访问
	int ret;
	EXPECT(c, '{');
	lept_parse_whitespace(c);
	if (*c->json == '}')
	{
		c->json++;
		lept_set_object(v, 0);
		return LEPT_PARSE_OK;
	}
	m.k = NULL;
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
			c->json++;
			lept_set_object(v, size);
			memcpy(v->m, lept_context_pop(c, sizeof(lept_member)* size), sizeof(lept_member)* size);
			v->msize = size;
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
// 生成器
static void lept_stringify_string(lept_context *c, const char *s, size_t len)
{
	static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
	char *head, *p;
	assert(s != NULL);
	size_t size = len * 6 + 2;
	p = head = static_cast<char*>(lept_context_push(c, size));
	*p++ = '"';
	for (size_t i = 0; i < len; ++i)
	{
		unsigned char ch = static_cast<unsigned char>(s[i]);
		switch (ch)
		{
		case '\"': *p++ = '\\'; *p++ = '\"'; break;
		case '\\': *p++ = '\\'; *p++ = '\\'; break;
		case '\b': *p++ = '\\'; *p++ = 'b';  break;
		case '\f': *p++ = '\\'; *p++ = 'f';  break;
		case '\n': *p++ = '\\'; *p++ = 'n';  break;
		case '\r': *p++ = '\\'; *p++ = 'r';  break;
		case '\t': *p++ = '\\'; *p++ = 't';  break;
		default:
			if (ch < 0x20)
			{
				*p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
				*p++ = hex_digits[ch >> 4];
				*p++ = hex_digits[ch & 15];
			}
			else
				*p++ = s[i];
		}
	}
	*p++ = '"';
	c->top -= size - (p - head);
}

static void lept_stringify_value(lept_context *c, const lept_value *v)
{
	char *buffer;
	switch (v->type)
	{
	case LEPT_NULL:   PUTS(c, "null", 4); break;
	case LEPT_FALSE:  PUTS(c, "false", 5); break;
	case LEPT_TRUE:   PUTS(c, "true", 4); break;
	case LEPT_STRING: lept_stringify_string(c, v->s, v->len); break;
	case LEPT_NUMBER:
		buffer = static_cast<char*>(lept_context_push(c, 32));
		c->top -= 32 - sprintf(buffer, "%.17g", v->n); break;
	case LEPT_ARRAY:
		PUTC(c, '[');
		for (size_t i = 0; i < v->size; ++i)
		{
			if (i > 0)
				PUTC(c, ',');
			lept_stringify_value(c, &v->e[i]);
		}
		PUTC(c, ']');
		break;
	case LEPT_OBJECT:
		PUTC(c, '{');
		for (size_t i = 0; i != v->msize; ++i)
		{
			if (i > 0)
				PUTC(c, ',');
			lept_stringify_string(c, v->m[i].k, v->m[i].klen);
			PUTC(c, ':');
			lept_stringify_value(c, &v->m[i].v);
		}
		PUTC(c, '}');
		break;
	default:
		assert(0 && "invalid type");
	}
}

char* lept_stringify(const lept_value *v, size_t *length)
{
	lept_context c;
	assert(v != NULL);
	c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
	c.top = 0;
	lept_stringify_value(&c, v);
	// 如果传入的length是一个空指针，这个参数就没有用了
	if (length)
		*length = c.top;
	PUTC(&c, '\0');
	return c.stack;
}

void lept_copy(lept_value *dst, const lept_value *src)
{
	assert(dst != NULL && src != NULL && dst != src);
	// 涉及到字符串的需要特殊处理
	switch (src->type)
	{
	case LEPT_STRING:
		lept_set_string(dst, src->s, src->len);
		break;
	case LEPT_ARRAY:
		lept_set_array(dst, src->size);
		for (size_t i = 0; i != src->size; ++i)
			lept_copy(lept_pushback_array_element(dst), &(src->e[i]));
		break;
	case LEPT_OBJECT:
		lept_set_object(dst, src->msize);
		for (size_t i = 0; i != src->msize; ++i)
			lept_copy(lept_set_object_value(dst, src->m[i].k, src->m[i].klen), &(src->m[i].v));
		break;
	default:
		lept_free(dst);
		memcpy(dst, src, sizeof(lept_value));
		break;
	}
}
void lept_move(lept_value *dst, lept_value *src)
{
	assert(dst != NULL && src != NULL && dst != src);
	lept_free(dst);
	memcpy(dst, src, sizeof(lept_value));
	lept_init(src);
}
void lept_swap(lept_value *lhs, lept_value *rhs)
{
	assert(lhs != NULL && rhs != NULL);
	if (lhs != rhs)
	{
		lept_value temp;
		memcpy(&temp, lhs, sizeof(lept_value));
		memcpy(lhs, rhs, sizeof(lept_value));
		memcpy(rhs, &temp, sizeof(lept_value));
	}
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
int lept_is_equal(const lept_value *lhs, const lept_value *rhs)
{
	assert(lhs != NULL && rhs != NULL);
	if (lhs->type != rhs->type)
		return 0;
	switch (lhs->type)
	{
	case LEPT_STRING:
		return (lhs->len == rhs->len) && (memcmp(lhs->s, rhs->s, lhs->len) == 0);
	case LEPT_NUMBER:
		return lhs->n == rhs->n;
	case LEPT_ARRAY:
		if (lhs->size != rhs->size)
			return 0;
		for (size_t i = 0; i != lhs->size; ++i)
		{
			if (!lept_is_equal(&(lhs->e[i]), &(rhs->e[i])))
				return 0;
		}
		return 1;
	case LEPT_OBJECT:
		if (lhs->msize != rhs->msize)
			return 0;
		for (size_t i = 0; i != lhs->msize; ++i)
		{
			size_t index = lept_find_object_index(lhs, rhs->m[i].k, rhs->m[i].klen);
			if (index == LEPT_KEY_NOT_EXIST || !lept_is_equal(&(lhs->m[index].v), &(rhs->m[i].v)))
				return 0;
		}
		return 1;
	default:
		return 1;
	}
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


void lept_set_array(lept_value *v, size_t capacity)
{
	assert(v != NULL);
	lept_free(v);
	v->type = LEPT_ARRAY;
	v->e_capacity = capacity;
	v->size = 0;
	v->e = capacity > 0 ? static_cast<lept_value*>(malloc(capacity * sizeof(lept_value))) : NULL;
}
size_t lept_get_array_size(const lept_value *v)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->size;
}
size_t lept_get_array_capacity(const lept_value *v)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->e_capacity;
}
void lept_reserve_array(lept_value *v, size_t capacity)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->e_capacity < capacity)
	{
		v->e_capacity = capacity;
		v->e = (lept_value*)realloc(v->e, capacity*sizeof(lept_value));
	}
}
void lept_shrink_array(lept_value *v)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->e_capacity > v->size)
	{
		v->e_capacity = v->size;
		v->e = (lept_value*)realloc(v->e, v->e_capacity*sizeof(lept_value));
	}
}
void lept_clear_array(lept_value *v)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	lept_erase_array_element(v, 0, v->size);
}
lept_value* lept_get_array_element(const lept_value* v, size_t index)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	assert(index < v->size);
	return &(v->e[index]);
}
lept_value* lept_pushback_array_element(lept_value *v)
{
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->size == v->e_capacity)
		lept_reserve_array(v, v->size ? v->size * 2 : 1);
	lept_init(&(v->e[v->size]));
	return &(v->e[v->size++]);
}
void lept_popback_array_element(lept_value *v)
{
	assert(v != NULL && v->type == LEPT_ARRAY && v->size > 0);
	lept_free(&(v->e[v->size - 1]));
	--v->size;
}
lept_value* lept_insert_array_element(lept_value *v, size_t index)
{
	assert(v != NULL && v->type == LEPT_ARRAY && index <= v->size);
	lept_pushback_array_element(v);
	for (size_t i = v->size - 1; i != index; --i)
		lept_move(&(v->e[i]), &(v->e[i - 1]));
	return &(v->e[index]);
}
void lept_erase_array_element(lept_value *v, size_t index, size_t count)
{
	assert(v != NULL && v->type == LEPT_ARRAY && index + count <= v->size);
	if (count != 0)
	{
		for (size_t i = 0; i != count; ++i)
			lept_free(&(v->e[index + i]));
		for (size_t i = index + count; i != v->size; ++i)
			lept_move(&(v->e[i - count]), &(v->e[i]));
		v->size = v->size - count;
	}
}

void lept_set_object(lept_value *v, size_t capacity)
{
	assert(v != NULL);
	lept_free(v);
	v->type = LEPT_OBJECT;
	v->m_capacity = capacity;
	v->msize = 0;
	v->m = capacity > 0 ? static_cast<lept_member*>(malloc(capacity * sizeof(lept_member))) : NULL;
}
size_t lept_get_object_size(const lept_value* v)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->msize;
}
size_t lept_get_object_capacity(const lept_value *v)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->m_capacity;
}
void lept_reserve_object(lept_value *v, size_t capacity)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	if (v->m_capacity < capacity)
	{
		v->m_capacity = capacity;
		v->m = static_cast<lept_member*>(realloc(v->m, capacity * sizeof (lept_member)));
	}
}
void lept_shrink_object(lept_value *v)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	if (v->m_capacity > v->msize)
	{
		v->m_capacity = v->msize;
		v->m = static_cast<lept_member*>(realloc(v->m, v->m_capacity * sizeof (lept_member)));
	}
}
void lept_clear_object(lept_value *v)
{
	assert(v != NULL && v->type == LEPT_OBJECT);
	if (v->msize)
	{
		for (size_t i = 0; i != v->msize; ++i)
		{
			free(v->m[i].k);
			lept_free(&(v->m[i].v));
		}
		v->msize = 0;
	}
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
size_t lept_find_object_index(const lept_value *v, const char *key, size_t klen)
{
	assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);
	for (size_t i = 0; i != v->msize; ++i)
	{
		if (v->m[i].klen == klen && memcmp(v->m[i].k, key, klen) == 0)
			return i;
	}
	return LEPT_KEY_NOT_EXIST;
}
lept_value* lept_find_object_value(lept_value *v, const char *key, size_t klen)
{
	size_t index = lept_find_object_index(v, key, klen);
	return (index != LEPT_KEY_NOT_EXIST) ? &(v->m[index].v) : NULL;
}
lept_value* lept_set_object_value(lept_value *v, const char *key, size_t klen)
{
	assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);

	size_t index = lept_find_object_index(v, key, klen);
	if (index != LEPT_KEY_NOT_EXIST)
		return &(v->m[index].v);

	if (v->msize == v->m_capacity)
		lept_reserve_object(v, v->msize ? v->msize * 2 : 1);
	v->m[v->msize].k = static_cast<char*>(malloc(klen + 1));
	memcpy(v->m[v->msize].k, key, klen);
	v->m[v->msize].k[klen] = '\0';
	v->m[v->msize].klen = klen;
	return &(v->m[v->msize++].v);
}
void lept_remove_object_value(lept_value *v, size_t index)
{
	assert(v != NULL && v->type == LEPT_OBJECT && index < v->msize);
	free(v->m[index].k);
	for (size_t i = index + 1; i != v->msize; ++i)
	{
		v->m[i - 1].k = v->m[i].k;
		lept_move(&(v->m[i - 1].v), &(v->m[i].v));
	}
	--v->msize;
}
