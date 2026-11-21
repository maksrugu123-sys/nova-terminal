#ifndef NOVA_PARSER_H
#define NOVA_PARSER_H

#include <stddef.h>

#define NOVA_TOKEN_LIMIT 128
#define NOVA_TOKEN_TEXT 4096

typedef enum {
    NOVA_TOK_WORD = 0,
    NOVA_TOK_PIPE,
    NOVA_TOK_REDIRECT_IN,
    NOVA_TOK_REDIRECT_OUT,
    NOVA_TOK_REDIRECT_APPEND,
    NOVA_TOK_AND_IF,
    NOVA_TOK_OR_IF,
    NOVA_TOK_BACKGROUND
} NovaTokenType;

typedef struct {
    NovaTokenType type;
    char text[NOVA_TOKEN_TEXT];
} NovaToken;

/*
 * Lex a command line into words and shell operators.
 * Quotes are removed; single-quoted text is literal; variables expand
 * outside single quotes. Returns token count, or -1 on malformed input.
 */
int nova_lex(const char *line, NovaToken *tokens, size_t capacity,
             char *error, size_t error_capacity);

#endif
