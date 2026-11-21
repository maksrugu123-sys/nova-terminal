#define _POSIX_C_SOURCE 200809L

#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define EDITOR_LINE_LIMIT 10000

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
    int dirty;
} Buffer;

static void free_buffer(Buffer *b) {
    size_t i;
    for (i = 0; i < b->count; i++) free(b->items[i]);
    free(b->items);
}

static int reserve(Buffer *b, size_t wanted) {
    char **next;
    size_t cap = b->capacity ? b->capacity : 16;
    if (wanted > EDITOR_LINE_LIMIT) return 0;
    while (cap < wanted) cap *= 2;
    next = realloc(b->items, cap * sizeof(*next));
    if (next == NULL) return 0;
    b->items = next;
    b->capacity = cap;
    return 1;
}

static int insert_line(Buffer *b, size_t index, const char *text) {
    char **copy;
    if (index > b->count || !reserve(b, b->count + 1)) return 0;
    copy = strdup(text);
    if (copy == NULL) return 0;
    memmove(b->items + index + 1, b->items + index,
            (b->count - index) * sizeof(*b->items));
    b->items[index] = copy;
    b->count++;
    b->dirty = 1;
    return 1;
}

static int load_file(Buffer *b, const char *path) {
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    if (f == NULL) return errno == ENOENT ? 0 : -1;
    while ((n = getline(&line, &cap, f)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (!insert_line(b, b->count, line)) {
            free(line);
            fclose(f);
            return -1;
        }
    }
    free(line);
    if (ferror(f)) { fclose(f); return -1; }
    fclose(f);
    b->dirty = 0;
    return 1;
}

static int save_file(Buffer *b, const char *path) {
    FILE *f = fopen(path, "w");
    size_t i;
    if (f == NULL) return 0;
    for (i = 0; i < b->count; i++) {
        if (fprintf(f, "%s\n", b->items[i]) < 0) {
            fclose(f);
            return 0;
        }
    }
    if (fclose(f) != 0) return 0;
    b->dirty = 0;
    return 1;
}

static void print_buffer(const Buffer *b) {
    size_t i;
    if (b->count == 0) {
        puts("(empty file)");
        return;
    }
    for (i = 0; i < b->count; i++) printf("%4zu | %s\n", i + 1, b->items[i]);
}

static char *skip_spaces(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int nova_edit_file(const char *path) {
    Buffer b = {0};
    char *input = NULL;
    size_t input_cap = 0;
    ssize_t n;
    int done = 0;

    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "edit: usage: edit FILE\n");
        return 2;
    }
    if (load_file(&b, path) < 0) {
        fprintf(stderr, "edit: could not read %s\n", path);
        free_buffer(&b);
        return 1;
    }

    printf("NOVA edit — %s\n", path);
    puts("Commands: :p, :a, :i N TEXT, :r N TEXT, :d N, :w, :q, :wq");
    puts("In :a mode, enter one line at a time; a single '.' ends input.");

    while (!done) {
        fputs("edit> ", stdout);
        fflush(stdout);
        n = getline(&input, &input_cap, stdin);
        if (n < 0) break;
        while (n > 0 && (input[n - 1] == '\n' || input[n - 1] == '\r')) input[--n] = '\0';

        if (strcmp(input, ":p") == 0) {
            print_buffer(&b);
        } else if (strcmp(input, ":a") == 0) {
            puts("Append mode ('.' on a line ends):");
            for (;;) {
                fputs("... ", stdout);
                fflush(stdout);
                n = getline(&input, &input_cap, stdin);
                if (n < 0) { done = 1; break; }
                while (n > 0 && (input[n - 1] == '\n' || input[n - 1] == '\r')) input[--n] = '\0';
                if (strcmp(input, ".") == 0) break;
                if (!insert_line(&b, b.count, input)) {
                    puts("edit: cannot add line (memory or line limit reached)");
                    break;
                }
            }
        } else if (input[0] == ':' && (input[1] == 'i' || input[1] == 'r' || input[1] == 'd') && input[2] == ' ') {
            char *end;
            long number = strtol(input + 3, &end, 10);
            char *text = skip_spaces(end);
            if (end == input + 3 || number < 1 || (size_t)number > b.count + (input[1] == 'i' ? 1U : 0U)) {
                puts("edit: invalid line number");
                continue;
            }
            if (input[1] == 'd') {
                size_t index = (size_t)number - 1;
                free(b.items[index]);
                memmove(b.items + index, b.items + index + 1,
                        (b.count - index - 1) * sizeof(*b.items));
                b.count--;
                b.dirty = 1;
            } else {
                size_t index = (size_t)number - 1;
                if (input[1] == 'r') {
                    char *copy = strdup(text);
                    if (copy == NULL) { puts("edit: out of memory"); continue; }
                    free(b.items[index]);
                    b.items[index] = copy;
                    b.dirty = 1;
                } else if (!insert_line(&b, index, text)) {
                    puts("edit: cannot insert line");
                }
            }
        } else if (strcmp(input, ":w") == 0 || strcmp(input, ":wq") == 0) {
            if (save_file(&b, path)) {
                puts("Saved.");
                if (strcmp(input, ":wq") == 0) done = 1;
            } else {
                perror("edit: save");
            }
        } else if (strcmp(input, ":q") == 0) {
            if (b.dirty) {
                puts("Unsaved changes. Use :w to save or :q! to discard.");
            } else done = 1;
        } else if (strcmp(input, ":q!") == 0) {
            done = 1;
        } else if (input[0] == '\0') {
            continue;
        } else {
            puts("Unknown editor command. Type :p to view or :w to save.");
        }
    }

    free(input);
    free_buffer(&b);
    return 0;
}
