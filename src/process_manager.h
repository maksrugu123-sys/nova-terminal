#ifndef NOVA_PROCESS_MANAGER_H
#define NOVA_PROCESS_MANAGER_H

int nova_process_builtin(char **argv);
int nova_process_run(char **argv, int background, const char *command_line);
void nova_process_shutdown(void);

#endif
