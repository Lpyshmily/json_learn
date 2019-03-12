#ifndef LEPTJSON_H_
#define LEPTJSON_H_

enum lept_type {LEPT_NULL, LEPT_FALSE, LEPT_TRUE, LEPT_NUMBER, LEPT_STRING, LEPT_ARRAY, LEPT_OBJECT};

struct lept_value
{
	double n;
	lept_type type;
};

enum parse_state {LEPT_PARSE_OK = 0, LEPT_PARSE_EXPECT_VALUE, LEPT_PARSE_INVALID_VALUE,
	LEPT_PARSE_ROOT_NOT_SINGULAR, LEPT_PARSE_NUMBER_TOO_BIG};

int lept_parse(lept_value* v, const char* json);

lept_type lept_get_type(const lept_value* v);
double lept_get_number(const lept_value* v);

#endif