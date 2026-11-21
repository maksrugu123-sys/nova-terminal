#define _POSIX_C_SOURCE 200809L

#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append_char(char *out, size_t *used, char c) {
    if (*used + 1 >= NOVA_TOKEN_TEXT) return 0;
    out[(*used)++] = c;
    out[*used] = '\0';
    return 1;
}

static int append_text(char *out, size_t *used, const char *value) {
    size_t n = strlen(value);
    if (n >= NOVA_TOKEN_TEXT - *used) return 0;
    memcpy(out + *used, value, n);
    *used += n;
    out[*used] = '\0';
    return 1;
}

static int variable_name_char(unsigned char c, int first) {
    if (c == '_') return 1;
    if (first) return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    return isalnum(c) != 0;
}

static int expand_variable(const char **cursor, char *out, size_t *used) {
    const char *p = *cursor;
    char name[256];
    size_t n = 0;
    const char *value;
    int braced = 0;

    if (*p != '$') return 0;
    p++;
    if (*p == '{') {
        braced = 1;
        p++;
    }
    if (!variable_name_char((unsigned char)*p, 1)) {
        if (!append_char(out, used, '$')) return -1;
        *cursor = p;
        return 1;
    }
    while (variable_name_char((unsigned char)*p, n == 0)) {
        if (n + 1 >= sizeof(name)) return -1;
        name[n++] = *p++;
    }
    if (braced) {
        if (*p != '}') return -1;
        p++;
    }
    name[n] = '\0';
    value = getenv(name);
    if (value != NULL && !append_text(out, used, value)) return -1;
    *cursor = p;
    return 1;
}

static int push_token(NovaToken *tokens, size_t capacity, size_t *count,
                      NovaTokenType type, const char *text) {
    NovaToken *token;
    if (*count >= capacity) return 0;
    token = &tokens[(*count)++];
    token->type = type;
    token->text[0] = '\0';
    if (text != NULL && snprintf(token->text, sizeof(token->text), "%s", text)
                            >= (int)sizeof(token->text)) return 0;
    return 1;
}

static int operator_at(const char *p, NovaTokenType *type, size_t *length) {
    if (p[0] == '|' && p[1] == '|') {
        *type = NOVA_TOK_OR_IF; *length = 2; return 1;
    }
    if (p[0] == '&' && p[1] == '&') {
        *type = NOVA_TOK_AND_IF; *length = 2; return 1;
    }
    if (p[0] == '>' && p[1] == '>') {
        *type = NOVA_TOK_REDIRECT_APPEND; *length = 2; return 1;
    }
    *length = 1;
    switch (*p) {
        case '|': *type = NOVA_TOK_PIPE; return 1;
        case '<': *type = NOVA_TOK_REDIRECT_IN; return 1;
        case '>': *type = NOVA_TOK_REDIRECT_OUT; return 1;
        case '&': *type = NOVA_TOK_BACKGROUND; return 1;
        default: return 0;
    }
}

int nova_lex(const char *line, NovaToken *tokens, size_t capacity,
             char *error, size_t error_capacity) {
    const char *p = line;
    size_t count = 0;

    if (error != NULL && error_capacity > 0) error[0] = '\0';
    if (line == NULL || tokens == NULL || capacity == 0) {
        if (error != NULL && error_capacity) snprintf(error, error_capacity, "invalid lexer arguments");
        return -1;
    }

    while (*p != '\0') {
        size_t used = 0;
        int started = 0;
        char word[NOVA_TOKEN_TEXT] = "";
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (*p == '\0') break;

        {
            NovaTokenType op;
            size_t op_len;
            if (operator_at(p, &op, &op_len)) {
                if (!push_token(tokens, capacity, &count, op, NULL)) goto too_many;
                p += op_len;
                continue;
            }
        }

        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') {
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                started = 1;
                while (*p != '\0' && *p != quote) {
                    if (quote == '"' && *p == '$') {
                        int expanded = expand_variable(&p, word, &used);
                        if (expanded < 0) goto malformed_variable;
                        if (expanded > 0) continue;
                    }
                    if (quote == '"' && *p == '\\' && p[1] != '\0') {
                        p++;
                        if (!append_char(word, &used, *p++)) goto word_too_long;
                    } else if (!append_char(word, &used, *p++)) goto word_too_long;
                }
                if (*p != quote) {
                    if (error != NULL && error_capacity) snprintf(error, error_capacity, "unclosed quote");
                    return -1;
                }
                p++;
                continue;
            }
            if (*p == '\\') {
                started = 1;
                p++;
                if (*p == '\0') {
                    if (error != NULL && error_capacity) snprintf(error, error_capacity, "trailing escape");
                    return -1;
                }
                if (!append_char(word, &used, *p++)) goto word_too_long;
                continue;
            }
            if (*p == '$') {
                int expanded;
                started = 1;
                expanded = expand_variable(&p, word, &used);
                if (expanded < 0) goto malformed_variable;
                if (expanded > 0) continue;
            }
            {
                NovaTokenType op;
                size_t op_len;
                if (operator_at(p, &op, &op_len)) break;
            }
            started = 1;
            if (!append_char(word, &used, *p++)) goto word_too_long;
        }
        if (started && !push_token(tokens, capacity, &count, NOVA_TOK_WORD, word))
            goto too_many;
    }
    return (int)count;

too_many:
    if (error != NULL && error_capacity) snprintf(error, error_capacity, "too many tokens or token too large");
    return -1;
word_too_long:
    if (error != NULL && error_capacity) snprintf(error, error_capacity, "word exceeds %d bytes", NOVA_TOKEN_TEXT - 1);
    return -1;
malformed_variable:
    if (error != NULL && error_capacity) snprintf(error, error_capacity, "invalid or oversized variable expansion");
    return -1;
}
