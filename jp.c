#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "arena.h"
#include "string.h"
#include "da.h"

#define UNREACHABLE(x) (fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #x))

typedef enum {
	OK,
	UNKNOWN_TOKEN,
	SOURCE_FILE_OMITTED,
	CONTROL_FLOW_STACK_UNDERFLOW,
	INCORRECT_ELSE_USAGE,
	INCORRECT_DO_USAGE,
} Error;

typedef struct {
	String *items;
	int64_t count;
	int64_t capacity;
} Tokens;

typedef struct { char *name; int64_t size; } FileProfile;
typedef struct {
	FileProfile *items;
	int64_t count;
	int64_t capacity;
} FileProfiles;

typedef char * File;
typedef struct {
	File *items;
	int64_t count;
	int64_t capacity;
} Files;

typedef enum {
	CF_IF,
	CF_ELSE,
	CF_DO,
	CF_FOR,
	CF_END,
} ControlFlowType;

typedef struct { int64_t label; int64_t end_label; ControlFlowType type; } ControlFlow;
typedef struct {
	ControlFlow *items;
	int64_t count;
	int64_t capacity;
} ControlFlows;

typedef struct {
	Arena *input;
	Arena *output;
	Arena *error;

	Files in_files;
	File out_file;

	Tokens tokens;
	FileProfiles fps;

	//TypeStack typ; // TODO: implement this

	int64_t if_label_count;
	int64_t if_end_label_count;

	int64_t for_label_count;
	int64_t for_end_label_count;

	ControlFlows cfs;
} JP_Context;

void append_format_string(Arena *arena, char *format, ...)
{
	va_list args;
	va_start(args, format);

	va_list test;
	va_copy(test, args);

	int count = vsnprintf(NULL, 0, format, test);
	char *appender = arena_push(arena, count + 1, 0);
	vsnprintf(appender, count + 1, format, args);
	arena->pointer -= 1;

	va_end(args);
}

char *stringify_error(Error code)
{
	switch (code) {
	case OK: return "OK";
	case UNKNOWN_TOKEN: return "UNKNOWN_TOKEN";
	case SOURCE_FILE_OMITTED: return "SOURCE_FILE_OMITTED";
	case CONTROL_FLOW_STACK_UNDERFLOW: return "CONTROL_FLOW_STACK_UNDERFLOW";
	case INCORRECT_ELSE_USAGE: return "INCORRECT_ELSE_USAGE";
	case INCORRECT_DO_USAGE: return "INCORRECT_DO_USAGE";
	}
}

void report_error(Error code, String token, JP_Context *jpc)
{
	int64_t file_index = 0;
	int64_t file_start_index = 0;
	int64_t token_index = token.data - (char *)jpc->input - sizeof(Arena);
	for (; file_start_index <= token_index; file_index += 1) {
		file_start_index += jpc->fps.items[file_index].size;
	}

	file_index -= 1;

	char *file_start = file_start_index - jpc->fps.items[file_index].size + (char *)jpc->input + sizeof(Arena);
	int64_t size = jpc->fps.items[file_index].size;
	char *line_start = file_start;

	token_index = token.data - file_start;

	int64_t col = 1, row = 1;
	for (int64_t i = 0; i < token_index; i += 1) {
		if (file_start[i] == '\n') {
			col = 0;
			row += 1;
			line_start = &file_start[i];
		}

		col += 1;
	}

	if (*line_start == '\n') {
		line_start += 1;
	}

	int64_t line_length = 1;
	while (&line_start[line_length] < &file_start[size] && line_start[line_length] != '\n') {
		line_length += 1;
	}

	append_format_string(jpc->error, "%s:%ld:%ld: ERROR: %s\n%.*s\n%*.c\n",
			jpc->fps.items[file_index].name, row, col, stringify_error(code),
			(int)line_length, line_start, (int)col, '^');
}

String append_file(JP_Context *jpc, char *input_file)
{
	FILE *fp = fopen(input_file, "rb");

	fseek(fp, 0, SEEK_END);
	int64_t size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (size == 0) {
		return (String){0};
	}

	size -= 1;

	char *appender = arena_push(jpc->input, size, 0);
	fread(appender, sizeof(char), size, fp);

	da_append(&jpc->fps, ((FileProfile){.name = input_file, .size = size}));

	return (String) {
		.data = appender,
		.count = size,
	};
}

Tokens tokenize_file(String *source)
{
	Tokens tokens = {0};

	while (source->count > 0) {
		String token = tokenize_by_space(source);
		da_append(&tokens, token);
	}

	return tokens;
}

Error pop_control_flow(ControlFlows *cfs, ControlFlow *cf)
{
	*cf = (ControlFlow){0};

	if (cfs->count == 0) {
		return CONTROL_FLOW_STACK_UNDERFLOW;
	}

	cfs->count -= 1;
	*cf = cfs->items[cfs->count];

	return OK;
}

void emit_if(JP_Context *jpc)
{
	ControlFlow cf;
	Error code = pop_control_flow(&jpc->cfs, &cf);

	append_format_string(jpc->output, "POP\nCMP 0\n");

	if (code != OK || cf.type != CF_ELSE) {
		da_append(&jpc->cfs, cf);

		append_format_string(jpc->output, "JE IF %ld\n", jpc->if_label_count);

		da_append(&jpc->cfs, ((ControlFlow) {
			.type = CF_IF,
			.end_label = jpc->if_end_label_count,
			.label = jpc->if_label_count
		}));

		jpc->if_end_label_count += 1;
	} else {
		append_format_string(jpc->output, "JE IF %ld\n", cf.label);

		da_append(&jpc->cfs, ((ControlFlow) {
			.type = CF_IF,
			.end_label = cf.end_label,
			.label = jpc->if_label_count
		}));
	}

	jpc->if_label_count += 1;
}

void emit_else(JP_Context *jpc, String token)
{
	ControlFlow top = {0};
	Error code = pop_control_flow(&jpc->cfs, &top);

	if (code != OK) report_error(code, token, jpc);

	if (top.type != CF_IF) {
		report_error(INCORRECT_ELSE_USAGE, token, jpc);
	}

	append_format_string(jpc->output, "J END_IF %ld\n"
					"LABEL IF %ld\n", top.end_label, top.end_label, top.label + 1);

	da_append(&jpc->cfs, ((ControlFlow){.type = CF_ELSE, .label = top.label + 1, .end_label = top.end_label}));
}

void emit_end(JP_Context *jpc, String token)
{
	ControlFlow top = {0};
	Error code = pop_control_flow(&jpc->cfs, &top);

	if (code != OK) report_error(code, token, jpc);

	switch (top.type) {
	case CF_IF: {
		append_format_string(jpc->output, "LABEL IF %ld\n", top.label);
	case CF_ELSE:
		append_format_string(jpc->output, "LABEL END_IF %ld\n", top.end_label);
	} break;
	case CF_DO: {
		append_format_string(jpc->output, "J FOR %ld\nLABEL END_FOR %ld\n", top.label, top.end_label);
	} break;
	default: {
		UNREACHABLE(CF_END);
	} exit(1);
	}
}

void emit_for(JP_Context *jpc)
{
	append_format_string(jpc->output, "LABEL FOR %ld\n", jpc->for_label_count);

	da_append(&jpc->cfs, ((ControlFlow){
		.type = CF_FOR,
		.label = jpc->for_label_count,
		.end_label = jpc->for_end_label_count,
	}));

	jpc->for_label_count += 1;
	jpc->for_end_label_count += 1;
}

void emit_do(JP_Context *jpc, String token)
{
	ControlFlow cf;
	Error code = pop_control_flow(&jpc->cfs, &cf);

	if (code != OK) report_error(code, token, jpc);

	if (cf.type != CF_FOR) report_error(INCORRECT_DO_USAGE, token, jpc);

	append_format_string(jpc->output, "POP\nCMP 0\nJE END_FOR %ld\n", cf.end_label);

	da_append(&jpc->cfs, ((ControlFlow){
		.type = CF_DO,
		.end_label = cf.end_label,
		.label = cf.label,
	}));
}

void evaluate_token(String token, JP_Context *jpc)
{
	Error code = OK;
	(void)code;

	if (token.count == 0) {
		return;
	}

	if (string_equal(token, SLIT("/"))) {
		append_format_string(jpc->output, "DIV\n");
	} else if (string_equal(token, SLIT("*"))) {
		append_format_string(jpc->output, "MULT\n");
	} else if (string_equal(token, SLIT("-"))) {
		append_format_string(jpc->output, "SUB\n");
	} else if (string_equal(token, SLIT("+"))) {
		append_format_string(jpc->output, "SUB\n");
	} else if (string_equal(token, SLIT("print"))) {
		append_format_string(jpc->output, "PRINT\n");
	} else if (string_equal(token, SLIT("dup"))) {
		append_format_string(jpc->output, "DUP\n");
	} else if (string_equal(token, SLIT("swap"))) {
		append_format_string(jpc->output, "SWAP\n");
	} else if (string_equal(token, SLIT("nip"))) {
		append_format_string(jpc->output, "NIP\n");
	} else if (string_equal(token, SLIT("over"))) {
		append_format_string(jpc->output, "OVER\n");
	} else if (string_equal(token, SLIT("<"))) {
		append_format_string(jpc->output, "LT\n");
	} else if (string_equal(token, SLIT(">"))) {
		append_format_string(jpc->output, "GT\n");
	} else if (string_equal(token, SLIT("="))) {
		append_format_string(jpc->output, "EQ\n");
	} else if (string_equal(token, SLIT("if"))) {
		emit_if(jpc);
	} else if (string_equal(token, SLIT("else"))) {
		emit_else(jpc, token);
	} else if (string_equal(token, SLIT("end"))) {
		emit_end(jpc, token);
	} else if (string_equal(token, SLIT("for"))) {
		emit_for(jpc);
	} else if (string_equal(token, SLIT("do"))) {
		emit_do(jpc, token);
	} else if (is_valid_integer(token)) {
		append_format_string(jpc->output, "PUSH %ld\n", parse_number(token));
	} else {
		report_error(UNKNOWN_TOKEN, token, jpc);
	}
}

int is_valid_jp_file(char *filename)
{
	return (0 == strncmp(&filename[strlen(filename) - 3], ".jp", 3));
}

void report_init_error(Error code)
{
	fprintf(stderr, "%s\n", stringify_error(code));
	// TODO: also print a help message on top of this to direct the user
	exit(1);
}

void parse_args(int argc, char **argv, JP_Context *jpc)
{
	for (int64_t i = 0; i < argc; i += 1) {
		if (is_valid_jp_file(argv[i])) {
			da_append(&jpc->in_files, argv[i]);
		} else if (0 == strncmp(argv[i], "-o", 2)) {
			argc += 1;
			jpc->out_file = argv[i];
		}
	}

	if (jpc->in_files.count == 0) {
		report_init_error(SOURCE_FILE_OMITTED);
	}

	if (jpc->out_file == 0) {
		jpc->out_file = strdup(jpc->in_files.items[0]);
		size_t output_len = strlen(jpc->out_file);
		jpc->out_file[output_len - 1] = 0;
		jpc->out_file[output_len - 2] = 's';
	}
}

int main(int argc, char **argv)
{
	JP_Context jpc = {
		.input = arena_create(MIB(1), KIB(128), KIB(16)),
		.output = arena_create(MIB(1), KIB(128), KIB(16)),
		.error  = arena_create(MIB(1), KIB(128), KIB(16)),

		.tokens = {0},
		.fps = {0},

		.in_files = {0},
		.out_file = 0,

		.if_label_count = 0,
		.if_end_label_count = 0,

		.for_label_count = 0,
		.for_end_label_count = 0,

		.cfs = {0},

		//.typ = {0},
	};

	parse_args(argc, argv, &jpc);

	char *input_file = "source.jp";
	String file = append_file(&jpc, input_file);
	jpc.tokens = tokenize_file(&file);

	for (int64_t i = 0; i < jpc.tokens.count; i += 1) {
		evaluate_token(jpc.tokens.items[i], &jpc);
	}

	if (jpc.error->pointer != sizeof(Arena)) {
		fprintf(stderr, "%.*s\n", (int)(jpc.error->pointer - sizeof(Arena)), (char *)jpc.error + sizeof(Arena));
		exit(1);
	} else {
		FILE *fp = fopen(jpc.out_file, "wb");
		size_t count = fwrite(jpc.output + 1, sizeof(char), jpc.output->pointer - sizeof(Arena), fp);
		assert(count == jpc.output->pointer - sizeof(Arena));
		fclose(fp);
	}

	return 0;
}
