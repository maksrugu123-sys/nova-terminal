#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "editor.h"
#include "shell_features.h"
#include "process_manager.h"
#include "parser.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define NOVA_VERSION "0.2.0-dev"
#define MAX_ARGS 128
#define LINE_MAX_SIZE 4096
#define HISTORY_LIMIT 200

static int use_color = 0;
static int running = 1;
static char *history[HISTORY_LIMIT];
static size_t history_count = 0;
static struct termios saved_termios;
static int raw_active = 0;

static const char *C_RESET = "\033[0m";
static const char *C_BOLD = "\033[1m";
static const char *C_PROMPT = "\033[38;5;45m";
static const char *C_PATH = "\033[38;5;111m";
static const char *C_COMMAND = "\033[38;5;220m";
static const char *C_OK = "\033[38;5;82m";
static const char *C_ERROR = "\033[38;5;196m";
static const char *C_INFO = "\033[38;5;141m";
static const char *C_HINT = "\033[38;5;244m";

static const char *C_WARN = "\033[38;5;214m";
static const char *C_FILE = "\033[38;5;81m";
static const char *C_DIR = "\033[38;5;39m";

static void set_theme(const char *name) {
    if (strcmp(name, "nova") == 0) {
        C_PROMPT="\033[38;5;45m"; C_PATH="\033[38;5;111m"; C_COMMAND="\033[38;5;220m";
        C_OK="\033[38;5;82m"; C_ERROR="\033[38;5;196m"; C_INFO="\033[38;5;141m";
    } else if (strcmp(name, "matrix") == 0) {
        C_PROMPT="\033[38;5;46m"; C_PATH="\033[38;5;112m"; C_COMMAND="\033[38;5;118m";
        C_OK="\033[38;5;82m"; C_ERROR="\033[38;5;196m"; C_INFO="\033[38;5;48m";
    } else if (strcmp(name, "sunset") == 0) {
        C_PROMPT="\033[38;5;213m"; C_PATH="\033[38;5;177m"; C_COMMAND="\033[38;5;220m";
        C_OK="\033[38;5;114m"; C_ERROR="\033[38;5;196m"; C_INFO="\033[38;5;207m";
    } else if (strcmp(name, "ocean") == 0) {
        C_PROMPT="\033[38;5;51m"; C_PATH="\033[38;5;75m"; C_COMMAND="\033[38;5;159m";
        C_OK="\033[38;5;84m"; C_ERROR="\033[38;5;203m"; C_INFO="\033[38;5;117m";
    } else if (strcmp(name, "mono") == 0) {
        C_PROMPT="\033[1;37m"; C_PATH="\033[37m"; C_COMMAND="\033[1;37m";
        C_OK="\033[1;37m"; C_ERROR="\033[1;37m"; C_INFO="\033[37m";
    }
}

static size_t utf8_prev(const char *s, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && (((unsigned char)s[pos] & 0xC0U) == 0x80U)) pos--;
    return pos;
}

static size_t utf8_next(const char *s, size_t len, size_t pos) {
    if (pos >= len) return len;
    pos++;
    while (pos < len && (((unsigned char)s[pos] & 0xC0U) == 0x80U)) pos++;
    return pos;
}


static const char *color(const char *value) {
    return use_color ? value : "";
}

static void print_error(const char *message) {
    fprintf(stderr, "%s%s[error]%s %s\n", color(C_BOLD), color(C_ERROR), color(C_RESET), message);
}

static void restore_terminal(void) {
    if (raw_active) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
        raw_active = 0;
    }
}

static int enable_raw_terminal(void) {
    struct termios raw;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_termios) != 0) return 0;
    raw = saved_termios;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | IEXTEN | ISIG);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_oflag |= OPOST;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return 0;
    raw_active = 1;
    return 1;
}

static void print_prompt(void) {
    char cwd[PATH_MAX];
    const char *home = getenv("HOME");
    const char *shown;

    if (getcwd(cwd, sizeof(cwd)) == NULL) snprintf(cwd, sizeof(cwd), "?");
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

    printf("%s%sNOVA%s %s%s%s %s❯ %s", color(C_BOLD), color(C_PROMPT), color(C_RESET),
           color(C_PATH), shown, color(C_RESET), color(C_COMMAND), color(C_RESET));
    fflush(stdout);
}

static void redraw_line(const char *prompt_line, size_t length, size_t cursor) {
    (void)length;
    fputs("\r\033[2K", stdout);
    print_prompt();
    if (prompt_line[0] != '\0') fwrite(prompt_line, 1, strlen(prompt_line), stdout);
    if (cursor < strlen(prompt_line)) {
        printf("\033[%zuD", strlen(prompt_line) - cursor);
    }
    fflush(stdout);
}

static const char *history_path(void) {
    static char path[PATH_MAX];
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') return NULL;
    if (snprintf(path, sizeof(path), "%s/.nova_history", home) >= (int)sizeof(path)) return NULL;
    return path;
}

static void load_history(void) {
    const char *path = history_path();
    FILE *file;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    if (path == NULL || (file = fopen(path, "r")) == NULL) return;
    while ((n = getline(&line, &cap, file)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;
        if (history_count == HISTORY_LIMIT) {
            free(history[0]);
            memmove(history, history + 1, (HISTORY_LIMIT - 1) * sizeof(history[0]));
            history_count--;
        }
        history[history_count++] = strdup(line);
    }
    free(line);
    fclose(file);
}

static void add_history(const char *line) {
    const char *path;
    FILE *file;
    size_t len = strlen(line);
    if (len == 0 || (history_count > 0 && strcmp(history[history_count - 1], line) == 0)) return;
    if (history_count == HISTORY_LIMIT) {
        free(history[0]);
        memmove(history, history + 1, (HISTORY_LIMIT - 1) * sizeof(history[0]));
        history_count--;
    }
    history[history_count] = strdup(line);
    if (history[history_count] != NULL) history_count++;

    path = history_path();
    if (path != NULL && (file = fopen(path, "a")) != NULL) {
        (void)fwrite(line, 1, len, file);
        (void)fputc('\n', file);
        fclose(file);
    }
}

static void complete_path(char *line, size_t *length, size_t *cursor) {
    size_t start = *cursor;
    size_t token_start;
    char token[PATH_MAX];
    char directory[PATH_MAX];
    char prefix[PATH_MAX];
    char candidate[PATH_MAX];
    const char *slash;
    const char *base;
    DIR *dir;
    struct dirent *entry;
    char *matches[128];
    size_t match_count = 0, i, common;
    int has_slash;

    while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t') start--;
    token_start = start;
    if (*cursor - token_start >= sizeof(token)) return;
    memcpy(token, line + token_start, *cursor - token_start);
    token[*cursor - token_start] = '\0';

    slash = strrchr(token, '/');
    has_slash = slash != NULL;
    if (has_slash) {
        size_t dlen = (size_t)(slash - token);
        if (dlen == 0) strcpy(directory, "/");
        else {
            memcpy(directory, token, dlen);
            directory[dlen] = '\0';
        }
        base = slash + 1;
    } else {
        strcpy(directory, ".");
        base = token;
    }
    snprintf(prefix, sizeof(prefix), "%s", base);
    dir = opendir(directory);
    if (dir == NULL) return;

    while ((entry = readdir(dir)) != NULL && match_count < 128) {
        size_t plen = strlen(prefix);
        if (strncmp(entry->d_name, prefix, plen) != 0) continue;
        if (entry->d_name[0] == '.' && prefix[0] != '.') continue;
        matches[match_count] = strdup(entry->d_name);
        if (matches[match_count] != NULL) match_count++;
    }
    closedir(dir);
    if (match_count == 0) return;

    common = strlen(matches[0]);
    for (i = 1; i < match_count; i++) {
        size_t j = 0;
        while (j < common && matches[i][j] == matches[0][j]) j++;
        common = j;
    }

    if (match_count == 1 || common > strlen(prefix)) {
        size_t old_token_len = *cursor - token_start;
        size_t replacement_len = (has_slash ? (size_t)(slash - token + 1) : 0) + common;
        size_t new_len = token_start + replacement_len + (*length - *cursor);
        if (new_len < LINE_MAX_SIZE) {
            if (has_slash) memcpy(candidate, token, (size_t)(slash - token + 1));
            else candidate[0] = '\0';
            candidate[has_slash ? (size_t)(slash - token + 1) : 0] = '\0';
            strncat(candidate, matches[0], common);
            memmove(line + token_start + replacement_len, line + *cursor, *length - *cursor + 1);
            memcpy(line + token_start, candidate, replacement_len);
            *length = new_len;
            *cursor = token_start + replacement_len;
            if (match_count == 1) {
                char full[PATH_MAX];
                struct stat st;
                if (snprintf(full, sizeof(full), "%s/%s", directory, matches[0]) < (int)sizeof(full) &&
                    stat(full, &st) == 0 && S_ISDIR(st.st_mode) && *length + 1 < LINE_MAX_SIZE) {
                    memmove(line + *cursor + 1, line + *cursor, *length - *cursor + 1);
                    line[(*cursor)++] = '/';
                    (*length)++;
                } else if (*length + 1 < LINE_MAX_SIZE) {
                    memmove(line + *cursor + 1, line + *cursor, *length - *cursor + 1);
                    line[(*cursor)++] = ' ';
                    (*length)++;
                }
            }
        }
        (void)old_token_len;
    } else {
        putchar('\n');
        for (i = 0; i < match_count; i++) {
            char full[PATH_MAX];
            struct stat item_stat;
            const char *item_color = C_FILE;
            if (snprintf(full, sizeof(full), "%s/%s", directory, matches[i]) < (int)sizeof(full) &&
                stat(full, &item_stat) == 0 && S_ISDIR(item_stat.st_mode)) item_color = C_DIR;
            printf("%s%s%s  ", color(item_color), matches[i], color(C_RESET));
        }
        putchar('\n');
    }
    for (i = 0; i < match_count; i++) free(matches[i]);
}

static ssize_t read_line(char *buffer, size_t capacity) {
    size_t length = 0, cursor = 0;
    size_t history_position = history_count;
    char saved_current[LINE_MAX_SIZE] = "";
    int raw = enable_raw_terminal();

    buffer[0] = '\0';
    if (!raw) {
        if (fgets(buffer, (int)capacity, stdin) == NULL) return -1;
        return (ssize_t)strlen(buffer);
    }

    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 0) { restore_terminal(); return -1; }
        if (n < 0) {
            if (errno == EINTR) continue;
            restore_terminal();
            return -1;
        }
        if (c == '\r' || c == '\n') {
            buffer[length] = '\0';
            putchar('\n');
            restore_terminal();
            return (ssize_t)length;
        }
        if (c == 3) {
            buffer[0] = '\0';
            putchar('^'); putchar('C'); putchar('\n');
            restore_terminal();
            return 0;
        }
        if (c == 4) {
            if (length == 0) { restore_terminal(); return -1; }
            continue;
        }
        if (c == 127 || c == 8) {
            if (cursor > 0) {
                size_t prev = utf8_prev(buffer, cursor);
                memmove(buffer + prev, buffer + cursor, length - cursor + 1);
                length -= cursor - prev; cursor = prev;
                redraw_line(buffer, length, cursor);
            }
            continue;
        }
        if (c == 9) {
            complete_path(buffer, &length, &cursor);
            redraw_line(buffer, length, cursor);
            continue;
        }
        if (c == 27) {
            unsigned char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1 || read(STDIN_FILENO, &seq[1], 1) != 1 || seq[0] != '[') continue;
            if (seq[1] == 'A') {
                if (history_position > 0) {
                    if (history_position == history_count) snprintf(saved_current, sizeof(saved_current), "%s", buffer);
                    history_position--;
                    snprintf(buffer, capacity, "%s", history[history_position]);
                    length = cursor = strlen(buffer);
                    redraw_line(buffer, length, cursor);
                }
            } else if (seq[1] == 'B') {
                if (history_position < history_count) {
                    history_position++;
                    snprintf(buffer, capacity, "%s", history_position == history_count ? saved_current : history[history_position]);
                    length = cursor = strlen(buffer);
                    redraw_line(buffer, length, cursor);
                }
            } else if (seq[1] == 'C' && cursor < length) {
                cursor = utf8_next(buffer, length, cursor);
                redraw_line(buffer, length, cursor);
            } else if (seq[1] == 'D' && cursor > 0) {
                cursor = utf8_prev(buffer, cursor);
                redraw_line(buffer, length, cursor);
            } else if (seq[1] == 'H') {
                cursor = 0; redraw_line(buffer, length, cursor);
            } else if (seq[1] == 'F') {
                cursor = length; redraw_line(buffer, length, cursor);
            }
            continue;
        }
        if (c >= 32 && length + 1 < capacity) {
            memmove(buffer + cursor + 1, buffer + cursor, length - cursor + 1);
            buffer[cursor++] = (char)c;
            length++;
            redraw_line(buffer, length, cursor);
        }
    }
}


static int ensure_directory(const char *path) {
    if (mkdir(path, 0755) == 0) return 1;
    if (errno == EEXIST) {
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    }
    return 0;
}

static void workspace_command(char **argv) {
    char cwd[PATH_MAX];
    if (argv[1] == NULL || strcmp(argv[1], "info") == 0) {
        FILE *f = fopen(".nova/workspace.conf", "r");
        char name[256];
        if (f == NULL) {
            puts("No NOVA workspace in this directory. Use: workspace init [NAME]");
            return;
        }
        if (fgets(name, sizeof(name), f) != NULL) {
            name[strcspn(name, "\r\n")] = '\0';
            printf("Workspace: %s\n", name);
        }
        fclose(f);
        if (getcwd(cwd, sizeof(cwd)) != NULL) printf("Directory: %s\n", cwd);
        return;
    }
    if (strcmp(argv[1], "init") == 0) {
        const char *name = argv[2];
        FILE *f;
        if (name == NULL) {
            if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, "workspace");
            name = strrchr(cwd, '/');
            name = name == NULL ? cwd : name + 1;
        }
        if (!ensure_directory(".nova")) {
            print_error("cannot create .nova directory");
            return;
        }
        f = fopen(".nova/workspace.conf", "w");
        if (f == NULL) { print_error("cannot write workspace metadata"); return; }
        fprintf(f, "%s\n", name);
        fclose(f);
        printf("%sWorkspace initialized: %s%s\n", color(C_OK), name, color(C_RESET));
        return;
    }
    puts("Usage: workspace [info | init [NAME]]");
}

static void kernel_command(char **argv) {
    struct stat st;
    if (argv[1] == NULL || strcmp(argv[1], "status") == 0) {
        if (stat("kernel", &st) == 0 && S_ISDIR(st.st_mode)) {
            puts("Kernel project scaffold: present");
            if (access("kernel/README.md", R_OK) == 0) puts("Documentation: kernel/README.md");
        } else {
            puts("Kernel project scaffold: not initialized");
            puts("Use: kernel init");
        }
        puts("Note: this checks project files; it does not inspect or modify the device kernel.");
        return;
    }
    if (strcmp(argv[1], "init") == 0) {
        FILE *f;
        if (!ensure_directory("kernel")) {
            print_error("cannot create kernel directory");
            return;
        }
        if (access("kernel/README.md", F_OK) == 0) {
            puts("kernel/ already exists; leaving existing files unchanged.");
            return;
        }
        f = fopen("kernel/README.md", "w");
        if (f == NULL) { print_error("cannot create kernel/README.md"); return; }
        fputs("# Kernel project scaffold\n\n"
              "This directory is a project workspace only. It is not a bootable kernel.\n"
              "Record the target device, architecture, boot chain, source baseline, "
              "toolchain and recovery plan before attempting device-level work.\n", f);
        fclose(f);
        puts("Created kernel/README.md. This is documentation scaffolding, not a kernel implementation.");
        return;
    }
    puts("Usage: kernel [status | init]");
}

static void print_help(void) {
    puts("NOVA Terminal " NOVA_VERSION " — built-in commands:");
    printf("  %shelp%s       Show this help\n", color(C_COMMAND), color(C_RESET));
    printf("  %sversion%s    Show version\n", color(C_COMMAND), color(C_RESET));
    printf("  %scd [DIR]%s   Change working directory\n", color(C_COMMAND), color(C_RESET));
    printf("  %sclear%s      Clear the terminal\n", color(C_COMMAND), color(C_RESET));
    printf("  %shistory%s    Show recent command history\n", color(C_COMMAND), color(C_RESET));
    printf("  %stheme [NAME]%s Switch palette: nova, matrix, sunset, ocean, mono\n", color(C_COMMAND), color(C_RESET));
    printf("  %sexport NAME=VALUE%s Set an environment variable\n", color(C_COMMAND), color(C_RESET));
    printf("  %sunset NAME%s Remove an environment variable\n", color(C_COMMAND), color(C_RESET));
    printf("  %senv%s         List environment variables\n", color(C_COMMAND), color(C_RESET));
    printf("  %salias%s       List or define command aliases\n", color(C_COMMAND), color(C_RESET));
    printf("  %sunalias NAME%s Remove an alias\n", color(C_COMMAND), color(C_RESET));
    printf("  %sjobs%s       List background jobs\n", color(C_COMMAND), color(C_RESET));
    printf("  %swait [ID]%s  Wait for background jobs\n", color(C_COMMAND), color(C_RESET));
    printf("  %skill PID%s   Send SIGTERM to a managed job\n", color(C_COMMAND), color(C_RESET));
    puts("Use 'command &' to start a background process; use 'ps' to inspect processes.");
    printf("  %sedit FILE%s  Open the lightweight line editor\n", color(C_COMMAND), color(C_RESET));
    printf("  %sworkspace%s  Manage a NOVA workspace\n", color(C_COMMAND), color(C_RESET));
    printf("  %skernel%s     Manage kernel project scaffolding\n", color(C_COMMAND), color(C_RESET));
    printf("  %sexit%s       Exit NOVA Terminal\n", color(C_COMMAND), color(C_RESET));
    puts("Tab completes filesystem paths; Up/Down browse saved history.");
    puts("Other commands are searched for in PATH and launched as external programs.");
    printf("%sNote:%s full POSIX shell syntax and terminal job control are not implemented.\\n",
           color(C_WARN), color(C_RESET));
}

static int run_builtin(char **argv) {
    if (argv[0] == NULL) return 1;
    if (strcmp(argv[0], "help") == 0) { print_help(); return 1; }
    if (strcmp(argv[0], "version") == 0) {
        printf("%sNOVA Terminal %s%s\n", color(C_OK), NOVA_VERSION, color(C_RESET));
        return 1;
    }
    if (strcmp(argv[0], "clear") == 0) { fputs("\033[2J\033[H", stdout); return 1; }
    if (strcmp(argv[0], "theme") == 0) {
        if (argv[1] == NULL) {
            puts("Available themes: nova matrix sunset ocean mono");
        } else if (strcmp(argv[1], "nova") == 0 || strcmp(argv[1], "matrix") == 0 ||
                   strcmp(argv[1], "sunset") == 0 || strcmp(argv[1], "ocean") == 0 ||
                   strcmp(argv[1], "mono") == 0) {
            set_theme(argv[1]);
            printf("%sTheme changed: %s%s\n", color(C_OK), argv[1], color(C_RESET));
        } else print_error("unknown theme; use: theme nova|matrix|sunset|ocean|mono");
        return 1;
    }
    if (strcmp(argv[0], "edit") == 0) { (void)nova_edit_file(argv[1]); return 1; }
    if (strcmp(argv[0], "workspace") == 0) { workspace_command(argv); return 1; }
    if (strcmp(argv[0], "kernel") == 0) { kernel_command(argv); return 1; }
    if (strcmp(argv[0], "history") == 0) {
        size_t i;
        for (i = 0; i < history_count; i++) printf("%s%3zu%s  %s\n", color(C_HINT), i + 1, color(C_RESET), history[i]);
        return 1;
    }
    if (strcmp(argv[0], "exit") == 0) { running = 0; return 1; }
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



#define NOVA_PIPELINE_LIMIT 16

typedef struct {
    char *argv[MAX_ARGS];
    const char *input;
    const char *output;
    int append;
} NovaExecCommand;

static int nova_status_from_wait(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static int nova_apply_redirects(const NovaExecCommand *cmd) {
    int fd;
    if (cmd->input) {
        fd = open(cmd->input, O_RDONLY);
        if (fd < 0) { perror(cmd->input); return 0; }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return 0; }
        close(fd);
    }
    if (cmd->output) {
        fd = open(cmd->output, O_WRONLY | O_CREAT |
                  (cmd->append ? O_APPEND : O_TRUNC), 0666);
        if (fd < 0) { perror(cmd->output); return 0; }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return 0; }
        close(fd);
    }
    return 1;
}

static int nova_execute_pipeline(NovaExecCommand *cmds, size_t count,
                                 int background, const char *original) {
    pid_t pids[NOVA_PIPELINE_LIMIT];
    int previous = -1, result = 0;
    struct sigaction old_int, old_tstp, ignore_action, default_action;
    size_t i;
    if (count == 0 || count > NOVA_PIPELINE_LIMIT) return 2;
    if (background && count > 1) {
        fputs("nova: background pipelines are not supported yet\n", stderr);
        return 2;
    }

    if (count == 1 && !background) {
        int saved_in = -1, saved_out = -1, handled = 0;
        int redirected = cmds[0].input != NULL || cmds[0].output != NULL;
        if (redirected) {
            fflush(NULL);
            saved_in = dup(STDIN_FILENO);
            saved_out = dup(STDOUT_FILENO);
            if (saved_in < 0 || saved_out < 0 || !nova_apply_redirects(&cmds[0])) {
                if (saved_in >= 0) { (void)dup2(saved_in, STDIN_FILENO); close(saved_in); }
                if (saved_out >= 0) { (void)dup2(saved_out, STDOUT_FILENO); close(saved_out); }
                return 1;
            }
        }
        handled = run_builtin(cmds[0].argv);
        if (!handled) handled = nova_shell_builtin(cmds[0].argv);
        if (!handled) handled = nova_process_builtin(cmds[0].argv);
        fflush(NULL);
        if (redirected) {
            (void)dup2(saved_in, STDIN_FILENO);
            (void)dup2(saved_out, STDOUT_FILENO);
            close(saved_in);
            close(saved_out);
        }
        if (handled) return 0;
        return nova_process_run(cmds[0].argv, 0, original);
    }
    if (background && count == 1) {
        if (cmds[0].input || cmds[0].output) {
            fputs("nova: background redirection is not supported yet\n", stderr);
            return 2;
        }
        return nova_process_run(cmds[0].argv, 1, original);
    }

    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);
    memset(&default_action, 0, sizeof(default_action));
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    (void)sigaction(SIGINT, &ignore_action, &old_int);
    (void)sigaction(SIGTSTP, &ignore_action, &old_tstp);

    for (i = 0; i < count; i++) {
        int fds[2] = {-1, -1};
        pid_t pid;
        if (i + 1 < count && pipe(fds) != 0) {
            (void)sigaction(SIGINT, &old_int, NULL);
            (void)sigaction(SIGTSTP, &old_tstp, NULL);
            perror("nova: pipe");
            if (previous >= 0) close(previous);
            return 1;
        }
        pid = fork();
        if (pid < 0) {
            (void)sigaction(SIGINT, &old_int, NULL);
            (void)sigaction(SIGTSTP, &old_tstp, NULL);
            perror("nova: fork");
            if (previous >= 0) close(previous);
            if (fds[0] >= 0) close(fds[0]);
            if (fds[1] >= 0) close(fds[1]);
            return 1;
        }
        if (pid == 0) {
            (void)sigaction(SIGINT, &default_action, NULL);
            (void)sigaction(SIGTSTP, &default_action, NULL);
            if (previous >= 0) { (void)dup2(previous, STDIN_FILENO); close(previous); }
            if (fds[1] >= 0) { (void)dup2(fds[1], STDOUT_FILENO); close(fds[1]); }
            if (fds[0] >= 0) close(fds[0]);
            if (!nova_apply_redirects(&cmds[i])) _exit(1);
            {
                int handled = run_builtin(cmds[i].argv);
                if (!handled) handled = nova_shell_builtin(cmds[i].argv);
                if (handled) {
                    fflush(NULL);
                    _exit(0);
                }
            }
            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "nova: %s: %s\n", cmds[i].argv[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }
        pids[i] = pid;
        if (previous >= 0) close(previous);
        if (fds[1] >= 0) close(fds[1]);
        previous = fds[0];
    }
    if (previous >= 0) close(previous);
    for (i = 0; i < count; i++) {
        int child_status;
        pid_t waited;
        do { waited = waitpid(pids[i], &child_status, 0); }
        while (waited < 0 && errno == EINTR);
        if (waited < 0) { perror("nova: waitpid"); result = 1; }
        else if (i + 1 == count) result = nova_status_from_wait(child_status);
    }
    (void)sigaction(SIGINT, &old_int, NULL);
    (void)sigaction(SIGTSTP, &old_tstp, NULL);
    return result;
}

static int execute_line(const char *line, const char *original) {
    NovaToken tokens[NOVA_TOKEN_LIMIT];
    NovaExecCommand commands[NOVA_PIPELINE_LIMIT];
    char error[160];
    int n = nova_lex(line, tokens, NOVA_TOKEN_LIMIT, error, sizeof(error));
    int i = 0, status = 0, condition = 0;
    if (n < 0) { fprintf(stderr, "nova: parse: %s\n", error); return 2; }
    if (n == 0) return 0;

    while (i < n) {
        size_t count = 0;
        int background = 0, skip;
        for (;;) {
            NovaExecCommand *cmd;
            size_t argc = 0;
            if (count >= NOVA_PIPELINE_LIMIT) {
                fputs("nova: pipeline is too long\n", stderr); return 2;
            }
            cmd = &commands[count++];
            memset(cmd, 0, sizeof(*cmd));
            while (i < n && tokens[i].type != NOVA_TOK_PIPE &&
                   tokens[i].type != NOVA_TOK_AND_IF &&
                   tokens[i].type != NOVA_TOK_OR_IF &&
                   tokens[i].type != NOVA_TOK_BACKGROUND) {
                NovaTokenType type = tokens[i].type;
                if (type == NOVA_TOK_WORD) {
                    if (argc + 1 >= MAX_ARGS) {
                        fputs("nova: too many arguments\n", stderr); return 2;
                    }
                    cmd->argv[argc++] = tokens[i++].text;
                } else if (type == NOVA_TOK_REDIRECT_IN ||
                           type == NOVA_TOK_REDIRECT_OUT ||
                           type == NOVA_TOK_REDIRECT_APPEND) {
                    NovaTokenType redirect = type;
                    i++;
                    if (i >= n || tokens[i].type != NOVA_TOK_WORD) {
                        fputs("nova: redirection requires a file name\n", stderr); return 2;
                    }
                    if (redirect == NOVA_TOK_REDIRECT_IN) cmd->input = tokens[i].text;
                    else {
                        cmd->output = tokens[i].text;
                        cmd->append = redirect == NOVA_TOK_REDIRECT_APPEND;
                    }
                    i++;
                } else {
                    fputs("nova: unexpected token\n", stderr); return 2;
                }
            }
            cmd->argv[argc] = NULL;
            if (argc == 0) { fputs("nova: empty command\n", stderr); return 2; }
            if (i < n && tokens[i].type == NOVA_TOK_PIPE) { i++; continue; }
            break;
        }
        if (i < n && tokens[i].type == NOVA_TOK_BACKGROUND) {
            background = 1;
            i++;
            if (i < n) {
                fputs("nova: '&' must terminate the command line\n", stderr);
                return 2;
            }
        }
        skip = (condition == 1 && status != 0) ||
               (condition == 2 && status == 0);
        if (!skip) status = nova_execute_pipeline(commands, count, background, original);
        if (i >= n) break;
        if (tokens[i].type == NOVA_TOK_AND_IF) condition = 1;
        else if (tokens[i].type == NOVA_TOK_OR_IF) condition = 2;
        else { fputs("nova: unexpected operator\n", stderr); return 2; }
        i++;
        if (i >= n) {
            fputs("nova: missing command after conditional operator\n", stderr);
            return 2;
        }
    }
    return status;
}

int main(void) {
    char line[LINE_MAX_SIZE];
    const char *no_color = getenv("NO_COLOR");
    use_color = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && (no_color == NULL || no_color[0] == '\0');
    (void)signal(SIGPIPE, SIG_IGN);
    load_history();

    printf("%sNOVA Terminal %s%s\n", color(C_BOLD), NOVA_VERSION, color(C_RESET));
    printf("%sHybrid command environment for Termux.%s\n", color(C_INFO), color(C_RESET));
    puts("Type 'help' for commands. This is not a POSIX shell yet.");

    while (running) {
        char original[LINE_MAX_SIZE];
        ssize_t length;
        print_prompt();
        length = read_line(line, sizeof(line));
        if (length < 0) break;
        if (length == 0) continue;
        snprintf(original, sizeof(original), "%s", line);
        add_history(original);
        nova_shell_expand_alias(line, sizeof(line));
        restore_terminal();
        (void)execute_line(line, original);
        (void)enable_raw_terminal();
    }

    restore_terminal();
    nova_process_shutdown();
    for (size_t i = 0; i < history_count; i++) free(history[i]);
    puts("NOVA Terminal closed.");
    return 0;
}
