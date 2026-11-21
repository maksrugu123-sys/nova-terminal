#define _POSIX_C_SOURCE 200809L

#include "shell_features.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOVA_ALIAS_LIMIT 64
#define NOVA_ALIAS_NAME 64
#define NOVA_ALIAS_VALUE 512
#define NOVA_ARGUMENT_LIMIT 128
#define NOVA_EXPANSION_LIMIT 4096

typedef struct {
    char name[NOVA_ALIAS_NAME];
    char value[NOVA_ALIAS_VALUE];
} NovaAlias;

static NovaAlias aliases[NOVA_ALIAS_LIMIT];
static size_t alias_count;
static char expanded[NOVA_ARGUMENT_LIMIT][NOVA_EXPANSION_LIMIT];

static int valid_name(const char *name) {
    size_t i;
    if (name[0] != '_' && !(name[0] >= 'A' && name[0] <= 'Z') &&
        !(name[0] >= 'a' && name[0] <= 'z')) return 0;
    for (i = 1; name[i] != '\0'; i++) {
        if (name[i] != '_' && !(name[i] >= 'A' && name[i] <= 'Z') &&
            !(name[i] >= 'a' && name[i] <= 'z') &&
            !(name[i] >= '0' && name[i] <= '9')) return 0;
    }
    return 1;
}

static NovaAlias *lookup_alias(const char *name) {
    size_t i;
    for (i = 0; i < alias_count; i++)
        if (strcmp(aliases[i].name, name) == 0) return &aliases[i];
    return NULL;
}

static void list_environment(void) {
    extern char **environ;
    char **entry;
    for (entry = environ; *entry != NULL; entry++) puts(*entry);
}

static void define_alias(char **argv) {
    char definition[NOVA_ALIAS_NAME + NOVA_ALIAS_VALUE + 2];
    char name[NOVA_ALIAS_NAME];
    char *equals;
    size_t used = 0, i, name_length;
    NovaAlias *item;

    if (argv[1] == NULL) {
        for (i = 0; i < alias_count; i++)
            printf("alias %s=%s\n", aliases[i].name, aliases[i].value);
        return;
    }
    for (i = 1; argv[i] != NULL; i++) {
        size_t part = strlen(argv[i]);
        if (used + part + (i > 1 ? 1U : 0U) >= sizeof(definition)) {
            puts("alias: definition is too long");
            return;
        }
        if (i > 1) definition[used++] = ' ';
        memcpy(definition + used, argv[i], part);
        used += part;
    }
    definition[used] = '\0';
    equals = strchr(definition, '=');
    if (equals == NULL || equals == definition || equals[1] == '\0') {
        puts("Usage: alias NAME=COMMAND");
        return;
    }
    name_length = (size_t)(equals - definition);
    if (name_length >= sizeof(name)) {
        puts("alias: name is too long");
        return;
    }
    memcpy(name, definition, name_length);
    name[name_length] = '\0';
    if (!valid_name(name)) {
        puts("alias: invalid name");
        return;
    }
    item = lookup_alias(name);
    if (item == NULL) {
        if (alias_count == NOVA_ALIAS_LIMIT) {
            puts("alias: alias table is full");
            return;
        }
        item = &aliases[alias_count++];
        snprintf(item->name, sizeof(item->name), "%s", name);
    }
    if (strlen(equals + 1) >= sizeof(item->value)) {
        puts("alias: command is too long");
        return;
    }
    snprintf(item->value, sizeof(item->value), "%s", equals + 1);
}

static void remove_alias(char **argv) {
    size_t i;
    if (argv[1] == NULL) {
        puts("Usage: unalias NAME");
        return;
    }
    for (i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, argv[1]) == 0) {
            memmove(&aliases[i], &aliases[i + 1],
                    (alias_count - i - 1) * sizeof(aliases[0]));
            alias_count--;
            return;
        }
    }
    fprintf(stderr, "unalias: %s: not found\n", argv[1]);
}

int nova_shell_builtin(char **argv) {
    char *equals;
    if (argv[0] == NULL) return 0;

    if (strcmp(argv[0], "env") == 0) {
        list_environment();
        return 1;
    }
    if (strcmp(argv[0], "export") == 0) {
        if (argv[1] == NULL) {
            list_environment();
            return 1;
        }
        equals = strchr(argv[1], '=');
        if (equals == NULL) {
            const char *value = getenv(argv[1]);
            if (value != NULL) printf("%s=%s\n", argv[1], value);
            else fprintf(stderr, "export: %s is not set\n", argv[1]);
            return 1;
        }
        {
            char key[256];
            size_t n = (size_t)(equals - argv[1]);
            if (n == 0 || n >= sizeof(key)) {
                fputs("export: invalid variable name\n", stderr);
                return 1;
            }
            memcpy(key, argv[1], n);
            key[n] = '\0';
            if (!valid_name(key) || setenv(key, equals + 1, 1) != 0)
                perror("export");
        }
        return 1;
    }
    if (strcmp(argv[0], "unset") == 0) {
        if (argv[1] == NULL || !valid_name(argv[1]))
            fputs("Usage: unset NAME\n", stderr);
        else if (unsetenv(argv[1]) != 0) perror("unset");
        return 1;
    }
    if (strcmp(argv[0], "alias") == 0) {
        define_alias(argv);
        return 1;
    }
    if (strcmp(argv[0], "unalias") == 0) {
        remove_alias(argv);
        return 1;
    }
    return 0;
}

void nova_shell_expand_alias(char *line, size_t capacity) {
    char name[NOVA_ALIAS_NAME];
    char replacement[NOVA_EXPANSION_LIMIT];
    const char *p = line;
    size_t n = 0;
    NovaAlias *item;

    while (*p == ' ' || *p == '\t') p++;
    while (*p != '\0' && *p != ' ' && *p != '\t' &&
           n + 1 < sizeof(name)) name[n++] = *p++;
    name[n] = '\0';
    if (n == 0 || (item = lookup_alias(name)) == NULL) return;
    while (*p == ' ' || *p == '\t') p++;
    if (snprintf(replacement, sizeof(replacement), "%s%s%s",
                 item->value, *p ? " " : "", p) >= (int)sizeof(replacement)) return;
    snprintf(line, capacity, "%s", replacement);
}

void nova_shell_expand_variables(char **argv) {
    size_t i;
    for (i = 0; argv[i] != NULL && i < NOVA_ARGUMENT_LIMIT; i++) {
        const char *p = argv[i];
        size_t used = 0;
        while (*p != '\0' && used + 1 < sizeof(expanded[i])) {
            if (*p != '$') {
                expanded[i][used++] = *p++;
                continue;
            }
            p++;
            {
                char name[256];
                size_t n = 0;
                const char *value;
                int braced = (*p == '{');
                if (braced) p++;
                while (*p != '\0' && n + 1 < sizeof(name)) {
                    if (braced ? *p == '}' :
                        !(*p == '_' || (*p >= 'A' && *p <= 'Z') ||
                          (*p >= 'a' && *p <= 'z') ||
                          (*p >= '0' && *p <= '9'))) break;
                    name[n++] = *p++;
                }
                if (braced && *p == '}') p++;
                if (n == 0) {
                    expanded[i][used++] = '$';
                    continue;
                }
                name[n] = '\0';
                value = getenv(name);
                if (value != NULL) {
                    size_t vlen = strlen(value);
                    if (vlen > sizeof(expanded[i]) - used - 1)
                        vlen = sizeof(expanded[i]) - used - 1;
                    memcpy(expanded[i] + used, value, vlen);
                    used += vlen;
                }
            }
        }
        expanded[i][used] = '\0';
        argv[i] = expanded[i];
    }
}
