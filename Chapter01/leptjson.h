#ifndef LEPTJSON_H_
#define LEPTJSON_H_

enum lept_type {LEPT_NULL, LEPT_FALSE, LEPT_TRUE, LEPY_NUMBER, LEPT_STRING, LEPT_ARRAY, LEPT_OBJECT};

struct lept_value
{
	lept_type type;
};

enum parse_state {LEPT_PARSE_OK = 0, LEPT_PARSE_EXPECT_VALUE, LEPT_PARSE_INVALID_VALUE, LEPT_PARSE_ROOT_NOT_SINGULAR};

int lept_parse(lept_value* v, const char* json);

lept_type lept_get_type(const lept_value* v);

#endif