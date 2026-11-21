#ifndef NOVA_SHELL_FEATURES_H
#define NOVA_SHELL_FEATURES_H

#include <stddef.h>

int nova_shell_builtin(char **argv);
void nova_shell_expand_alias(char *line, size_t capacity);
void nova_shell_expand_variables(char **argv);

#endif
