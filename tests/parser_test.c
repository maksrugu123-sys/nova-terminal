#define _POSIX_C_SOURCE 200809L

#include "../src/parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lex(const char *line, NovaToken *tokens) {
    char error[128];
    int count = nova_lex(line, tokens, NOVA_TOKEN_LIMIT, error, sizeof(error));
    if (count < 0) fprintf(stderr, "lexer error for [%s]: %s\n", line, error);
    return count;
}

int main(void) {
    NovaToken tokens[NOVA_TOKEN_LIMIT];
    int n;

    n = lex("echo 'hello world' \"two words\"", tokens);
    assert(n == 3);
    assert(tokens[0].type == NOVA_TOK_WORD && strcmp(tokens[0].text, "echo") == 0);
    assert(strcmp(tokens[1].text, "hello world") == 0);
    assert(strcmp(tokens[2].text, "two words") == 0);

    assert(setenv("NOVA_LEX_TEST", "expanded value", 1) == 0);
    n = lex("echo '$NOVA_LEX_TEST' \"$NOVA_LEX_TEST\" $NOVA_LEX_TEST", tokens);
    assert(n == 4);
    assert(strcmp(tokens[1].text, "$NOVA_LEX_TEST") == 0);
    assert(strcmp(tokens[2].text, "expanded value") == 0);
    assert(strcmp(tokens[3].text, "expanded value") == 0);

    n = lex("cat < in.txt | grep \"a b\" >> out.txt && echo ok || echo fail &", tokens);
    assert(n == 15);
    assert(tokens[1].type == NOVA_TOK_REDIRECT_IN);
    assert(tokens[3].type == NOVA_TOK_PIPE);
    assert(tokens[6].type == NOVA_TOK_REDIRECT_APPEND);
    assert(tokens[8].type == NOVA_TOK_AND_IF);
    assert(tokens[11].type == NOVA_TOK_OR_IF);
    assert(tokens[14].type == NOVA_TOK_BACKGROUND);

    n = lex("echo one\\ two", tokens);
    assert(n == 2 && strcmp(tokens[1].text, "one two") == 0);

    {
        char error[128];
        assert(nova_lex("echo 'unfinished", tokens, NOVA_TOKEN_LIMIT,
                        error, sizeof(error)) == -1);
        assert(strstr(error, "unclosed quote") != NULL);
    }

    puts("NOVA parser lexer tests passed.");
    return 0;
}
