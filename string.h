#pragma once

#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define SLIT(x) ((String){.data = (x), .count = strlen((x))})

typedef struct {
	char *data;
	int64_t count;
} String;

int string_equal(String one, String two)
{
	if (one.count != two.count) {
		return 0;
	}

	for (int64_t i = 0; i < one.count; i += 1) {
		if (one.data[i] != two.data[i]) {
			return 0;
		}
	}

	return 1;
}

String tokenize_by_space(String *source)
{
	int64_t count = 0;
	while (count < source->count && isspace(source->data[count])) count += 1;

	source->count -= count;
	source->data += count;

	String token = {
		.data = source->data,
		.count = 0,
	};

	count = 0;

	while (count < source->count && !isspace(source->data[count])) count += 1;

	source->count -= count;
	source->data += count;

	token.count = count;

	return token;
}

int is_valid_integer(String token)
{
	if (token.count == 0) {
		return 0;
	}

	for (int64_t i = 0; i < token.count; i += 1) {
		if (!isdigit(token.data[i])) {
			return 0;
		}
	}

	return 1;
}

int64_t parse_number(String token) // it is expected that you check the integer for being valid
{
	int64_t result = 0;

	for (int64_t i = 0; i < token.count; i += 1) {
		result = result * 10 + token.data[i] - '0';
	}

	return result;
}
