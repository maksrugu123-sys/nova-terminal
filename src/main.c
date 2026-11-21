#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define NOVA_VERSION "0.2.0-dev"
#define MAX_ARGS 128

static int use_color = 0;
static int running = 1;

static const char *C_RESET = "\033[0m";
static const char *C_BOLD = "\033[1m";
static const char *C_PROMPT = "\033[38;5;45m";
static const char *C_PATH = "\033[38;5;111m";
static const char *C_COMMAND = "\033[38;5;220m";
static const char *C_OK = "\033[38;5;82m";
static const char *C_WARN = "\033[38;5;214m";
static const char *C_ERROR = "\033[38;5;196m";
static const char *C_INFO = "\033[38;5;141m";

static const char *color(const char *value) {
    return use_color ? value : "";
}

static void print_error(const char *message) {
    fprintf(stderr, "%s%s[error]%s %s\n", color(C_BOLD), color(C_ERROR), color(C_RESET), message);
}

static void print_prompt(void) {
    char cwd[PATH_MAX];
    const char *home = getenv("HOME");
    const char *shown;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(cwd, sizeof(cwd), "?");
    }
    shown = cwd;
    if (home != NULL && home[0] != '\0') {
        size_t home_len = strlen(home);
        if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '/' || cwd[home_len] == '\0')) {
            shown = cwd[home_len] == '\0' ? "~" : cwd + home_len;
            if (shown[0] == '/') {
                static char short_path[PATH_MAX];
                snprintf(short_path, sizeof(short_path), "~%s", shown);
                shown = short_path;
            }
        }
    }

    printf("%s%sNOVA%s %s%s%s\n%s❯ %s", color(C_BOLD), color(C_PROMPT), color(C_RESET),
           color(C_PATH), shown, color(C_RESET), color(C_COMMAND), color(C_RESET));
    fflush(stdout);
}

static void print_help(void) {
    puts("NOVA Terminal " NOVA_VERSION " — built-in commands:");
    printf("  %shelp%s       Show this help\n", color(C_COMMAND), color(C_RESET));
    printf("  %sversion%s    Show version\n", color(C_COMMAND), color(C_RESET));
    printf("  %scd [DIR]%s   Change working directory\n", color(C_COMMAND), color(C_RESET));
    printf("  %sclear%s      Clear the terminal\n", color(C_COMMAND), color(C_RESET));
    printf("  %sexit%s       Exit NOVA Terminal\n", color(C_COMMAND), color(C_RESET));
    puts("Other commands are searched for in PATH and launched as external programs.");
    puts("Note: pipelines, redirection, shell quoting and job control are not implemented yet.");
}

static int run_builtin(char **argv) {
    if (argv[0] == NULL) return 1;

    if (strcmp(argv[0], "help") == 0) {
        print_help();
        return 1;
    }
    if (strcmp(argv[0], "version") == 0) {
        printf("%sNOVA Terminal %s%s\n", color(C_OK), NOVA_VERSION, color(C_RESET));
        return 1;
    }
    if (strcmp(argv[0], "clear") == 0) {
        fputs("\033[2J\033[H", stdout);
        return 1;
    }
    if (strcmp(argv[0], "exit") == 0) {
        running = 0;
        return 1;
    }
    if (strcmp(argv[0], "cd") == 0) {
        const char *target = argv[1];
        if (target == NULL || strcmp(target, "~") == 0) target = getenv("HOME");
        if (target == NULL || chdir(target) != 0) {
            char message[PATH_MAX + 64];
            snprintf(message, sizeof(message), "cd: %s: %s", target ? target : "HOME is not set", strerror(errno));
            print_error(message);
        }
        return 1;
    }
    return 0;
}

static void run_external(char **argv) {
    pid_t pid = fork();
    if (pid < 0) {
        print_error(strerror(errno));
        return;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "%snova: %s: %s%s\n", color(C_ERROR), argv[0], strerror(errno), color(C_RESET));
        _exit(errno == ENOENT ? 127 : 126);
    }
    for (;;) {
        int status;
        pid_t result = waitpid(pid, &status, 0);
        if (result >= 0) break;
        if (errno != EINTR) {
            print_error("could not wait for child process");
            break;
        }
    }
}

static size_t split_args(char *line, char **argv) {
    size_t count = 0;
    char *save = NULL;
    char *token = strtok_r(line, " \t\r\n", &save);
    while (token != NULL && count + 1 < MAX_ARGS) {
        argv[count++] = token;
        token = strtok_r(NULL, " \t\r\n", &save);
    }
    argv[count] = NULL;
    return count;
}

int main(void) {
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    const char *no_color = getenv("NO_COLOR");
    use_color = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && (no_color == NULL || no_color[0] == '\0');

    printf("%sNOVA Terminal %s%s\n", color(C_BOLD), NOVA_VERSION, color(C_RESET));
    printf("%sHybrid command environment for Termux.%s\n", color(C_INFO), color(C_RESET));
    puts("Type 'help' for commands. This is not a POSIX shell yet.");

    while (running) {
        char *argv[MAX_ARGS];
        print_prompt();
        length = getline(&line, &capacity, stdin);
        if (length < 0) {
            if (feof(stdin)) break;
            if (errno == EINTR) {
                clearerr(stdin);
                putchar('\n');
                continue;
            }
            print_error("failed to read command");
            break;
        }
        if (split_args(line, argv) == 0) continue;
        if (!run_builtin(argv)) run_external(argv);
    }

    free(line);
    puts("NOVA Terminal closed.");
    return 0;
}
