#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdlib.h>  /* NULL, strtod() */

struct lept_context
{
	const char* json;
};

void EXPECT(lept_context* c, char ch)
{
	assert(*(c->json) == ch);
	c->json++;
}
bool ISDIGIT(char ch)
{
	return (ch >= '0' && ch <= '9');
}
bool ISDIGIT1TO9(char ch)
{
	return (ch >= '1' && ch <= '9');
}

static void lept_parse_whitesapce(lept_context* c)
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

static int lept_parse_value(lept_context* c, lept_value* v)
{
	switch (*(c->json))
	{
	case 't':  return lept_parse_literal(c, v, "true", LEPT_TRUE);
	case 'f':  return lept_parse_literal(c, v, "false", LEPT_FALSE);
	case 'n':  return lept_parse_literal(c, v, "null", LEPT_NULL);
	case '\0': return LEPT_PARSE_EXPECT_VALUE;
	default:   return lept_parse_number(c,v);
	}
}

int lept_parse(lept_value* v, const char* json)
{
	lept_context c;
	int ret;
	assert(v != NULL);
	c.json = json;
	v->type = LEPT_NULL; // intitialize
	lept_parse_whitesapce(&c);
	ret = lept_parse_value(&c, v);
	if (ret == LEPT_PARSE_OK)
	{
		lept_parse_whitesapce(&c);
		if (*(c.json) != '\0')
		{
			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
			v->type = LEPT_NULL;
		}
	}
	return ret;
}

lept_type lept_get_type(const lept_value* v)
{
	assert(v != NULL);
	return v->type;
}

double lept_get_number(const lept_value* v)
{
	assert(v != NULL && v->type == LEPT_NUMBER);
	return v->n;
}