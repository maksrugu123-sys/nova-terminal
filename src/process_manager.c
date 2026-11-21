#define _POSIX_C_SOURCE 200809L

#include "process_manager.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define NOVA_JOB_LIMIT 32
#define NOVA_COMMAND_LIMIT 256

typedef struct {
    int used;
    int done;
    int stopped;
    unsigned int id;
    pid_t pid;
    int status;
    char command[NOVA_COMMAND_LIMIT];
} NovaJob;

static NovaJob jobs[NOVA_JOB_LIMIT];
static unsigned int next_job_id = 1;

static NovaJob *find_job_id(unsigned int id) {
    size_t i;
    for (i = 0; i < NOVA_JOB_LIMIT; i++)
        if (jobs[i].used && jobs[i].id == id) return &jobs[i];
    return NULL;
}

static NovaJob *find_job_pid(pid_t pid) {
    size_t i;
    for (i = 0; i < NOVA_JOB_LIMIT; i++)
        if (jobs[i].used && jobs[i].pid == pid) return &jobs[i];
    return NULL;
}

static NovaJob *allocate_job(void) {
    size_t i;
    for (i = 0; i < NOVA_JOB_LIMIT; i++)
        if (!jobs[i].used) return &jobs[i];
    for (i = 0; i < NOVA_JOB_LIMIT; i++)
        if (jobs[i].done) return &jobs[i];
    return NULL;
}

static void update_job(NovaJob *job, int status) {
    job->done = 1;
    job->stopped = 0;
    job->status = status;
    if (WIFEXITED(status))
        printf("[%u] Done (exit %d) %s\n", job->id, WEXITSTATUS(status), job->command);
    else if (WIFSIGNALED(status))
        printf("[%u] Done (signal %d) %s\n", job->id, WTERMSIG(status), job->command);
    fflush(stdout);
}

static void reap_jobs(void) {
    size_t i;
    for (i = 0; i < NOVA_JOB_LIMIT; i++) {
        int status;
        pid_t result;
        if (!jobs[i].used || jobs[i].done || jobs[i].stopped) continue;
        do {
            result = waitpid(jobs[i].pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == jobs[i].pid) update_job(&jobs[i], status);
        else if (result < 0 && errno == ECHILD) {
            jobs[i].done = 1;
            jobs[i].status = 0;
        }
    }
}

static int parse_positive(const char *text, long *value) {
    char *end;
    long parsed;
    if (text == NULL || *text == '\0') return 0;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed <= 0) return 0;
    *value = parsed;
    return 1;
}

static void print_job_status(const NovaJob *job) {
    if (job->stopped) {
        printf("[%u] Stopped  %ld  %s\n", job->id, (long)job->pid, job->command);
    } else if (!job->done) {
        printf("[%u] Running  %ld  %s\n", job->id, (long)job->pid, job->command);
    } else if (WIFEXITED(job->status)) {
        printf("[%u] Done     %ld  exit %d  %s\n", job->id, (long)job->pid,
               WEXITSTATUS(job->status), job->command);
    } else if (WIFSIGNALED(job->status)) {
        printf("[%u] Done     %ld  signal %d  %s\n", job->id, (long)job->pid,
               WTERMSIG(job->status), job->command);
    } else {
        printf("[%u] Done     %ld  %s\n", job->id, (long)job->pid, job->command);
    }
}

static int wait_for_job(NovaJob *job) {
    int status;
    pid_t result;
    if (job->stopped) {
        fprintf(stderr, "wait: job %u is stopped; use fg %u to resume it\n",
                job->id, job->id);
        return 1;
    }
    if (job->done) {
        status = job->status;
    } else {
        do {
            result = waitpid(job->pid, &status, 0);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            perror("wait");
            return 1;
        }
        job->done = 1;
        job->status = status;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static int resume_foreground_job(NovaJob *job) {
    struct sigaction old_int, old_tstp, ignore_action;
    int status;
    pid_t result;

    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);
    (void)sigaction(SIGINT, &ignore_action, &old_int);
    (void)sigaction(SIGTSTP, &ignore_action, &old_tstp);

    if (kill(job->pid, SIGCONT) != 0) {
        perror("fg: SIGCONT");
        (void)sigaction(SIGINT, &old_int, NULL);
        (void)sigaction(SIGTSTP, &old_tstp, NULL);
        return 1;
    }
    job->stopped = 0;
    do {
        result = waitpid(job->pid, &status, WUNTRACED);
    } while (result < 0 && errno == EINTR);

    (void)sigaction(SIGINT, &old_int, NULL);
    (void)sigaction(SIGTSTP, &old_tstp, NULL);
    if (result < 0) {
        perror("fg: waitpid");
        return 1;
    }
    if (WIFSTOPPED(status)) {
        job->stopped = 1;
        job->status = status;
        printf("[%u] Stopped by signal %d\n", job->id, WSTOPSIG(status));
        return 128 + WSTOPSIG(status);
    }
    update_job(job, status);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

int nova_process_builtin(char **argv) {
    long number;
    NovaJob *job;
    size_t i;
    if (argv[0] == NULL) return 0;

    if (strcmp(argv[0], "jobs") == 0) {
        reap_jobs();
        for (i = 0; i < NOVA_JOB_LIMIT; i++)
            if (jobs[i].used) print_job_status(&jobs[i]);
        return 1;
    }
    if (strcmp(argv[0], "wait") == 0) {
        int result = 0;
        reap_jobs();
        if (argv[1] == NULL) {
            for (i = 0; i < NOVA_JOB_LIMIT; i++) {
                if (jobs[i].used && !jobs[i].done) {
                    int code = wait_for_job(&jobs[i]);
                    if (code != 0) result = code;
                }
                if (jobs[i].used && jobs[i].done) jobs[i].used = 0;
            }
            return result;
        }
        if (!parse_positive(argv[1], &number) || number > 2147483647L) {
            fputs("Usage: wait [JOB_ID]\n", stderr);
            return 1;
        }
        job = find_job_id((unsigned int)number);
        if (job == NULL) {
            fprintf(stderr, "wait: job %ld not found\n", number);
            return 1;
        }
        result = wait_for_job(job);
        if (job->done) job->used = 0;
        return result;
    }
    if (strcmp(argv[0], "fg") == 0) {
        if (argv[1] == NULL || !parse_positive(argv[1], &number) ||
            number > 2147483647L) {
            fputs("Usage: fg JOB_ID (stopped foreground jobs only)\n", stderr);
            return 1;
        }
        job = find_job_id((unsigned int)number);
        if (job == NULL || !job->stopped || job->done) {
            fprintf(stderr, "fg: stopped foreground job %ld not found\n", number);
            return 1;
        }
        return resume_foreground_job(job);
    }
    if (strcmp(argv[0], "kill") == 0) {
        int signal_number = SIGTERM;
        if (argv[1] == NULL || !parse_positive(argv[1], &number) ||
            number > 2147483647L) {
            fputs("Usage: kill PID [SIGNAL_NUMBER]\n", stderr);
            return 1;
        }
        job = find_job_pid((pid_t)number);
        if (job == NULL || job->done) {
            fprintf(stderr, "kill: PID %ld is not a running NOVA job\n", number);
            return 1;
        }
        if (argv[2] != NULL) {
            if (!parse_positive(argv[2], &number) || number > 64) {
                fputs("kill: signal must be a valid positive signal number\n", stderr);
                return 1;
            }
            signal_number = (int)number;
        }
        if (kill(job->pid, signal_number) != 0) {
            perror("kill");
            return 1;
        }
        return 1;
    }
    return 0;
}

int nova_process_run(char **argv, int background, const char *command_line) {
    pid_t pid;
    NovaJob *job = NULL;
    struct sigaction old_int, old_tstp, ignore_action, default_action;

    reap_jobs();
    if (background) {
        job = allocate_job();
        if (job == NULL) {
            fputs("nova: job table is full; wait for or clear existing jobs\n", stderr);
            return 1;
        }
    }

    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);
    memset(&default_action, 0, sizeof(default_action));
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);

    if (!background) {
        (void)sigaction(SIGINT, &ignore_action, &old_int);
        (void)sigaction(SIGTSTP, &ignore_action, &old_tstp);
    }
    pid = fork();
    if (pid < 0) {
        int saved_errno = errno;
        if (!background) {
            (void)sigaction(SIGINT, &old_int, NULL);
            (void)sigaction(SIGTSTP, &old_tstp, NULL);
        }
        errno = saved_errno;
        perror("nova: fork");
        return 1;
    }
    if (pid == 0) {
        (void)sigaction(SIGINT, &default_action, NULL);
        (void)sigaction(SIGTSTP, &default_action, NULL);
        (void)sigaction(SIGPIPE, &default_action, NULL);
        if (background) {
            int null_fd;
            (void)setpgid(0, 0);
            null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                (void)dup2(null_fd, STDIN_FILENO);
                if (null_fd != STDIN_FILENO) close(null_fd);
            }
        }
        execvp(argv[0], argv);
        fprintf(stderr, "nova: %s: %s\n", argv[0], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    if (background) {
        (void)setpgid(pid, pid);
        memset(job, 0, sizeof(*job));
        job->used = 1;
        job->id = next_job_id++;
        if (next_job_id == 0) next_job_id = 1;
        job->pid = pid;
        snprintf(job->command, sizeof(job->command), "%s",
                 command_line != NULL ? command_line : argv[0]);
        printf("[%u] %ld\n", job->id, (long)pid);
        return 0;
    }

    {
        int status;
        pid_t result;
        do {
            result = waitpid(pid, &status, WUNTRACED);
        } while (result < 0 && errno == EINTR);
        (void)sigaction(SIGINT, &old_int, NULL);
        (void)sigaction(SIGTSTP, &old_tstp, NULL);
        if (result < 0) {
            perror("nova: waitpid");
            return 1;
        }
        if (WIFSTOPPED(status)) {
            NovaJob *stopped_job = allocate_job();
            if (stopped_job == NULL) {
                (void)kill(pid, SIGCONT);
                (void)kill(pid, SIGTERM);
                (void)waitpid(pid, NULL, 0);
                fputs("nova: job table full; stopped process was terminated\n", stderr);
                return 1;
            }
            memset(stopped_job, 0, sizeof(*stopped_job));
            stopped_job->used = 1;
            stopped_job->stopped = 1;
            stopped_job->id = next_job_id++;
            if (next_job_id == 0) next_job_id = 1;
            stopped_job->pid = pid;
            stopped_job->status = status;
            snprintf(stopped_job->command, sizeof(stopped_job->command), "%s",
                     command_line != NULL ? command_line : argv[0]);
            printf("[%u] Stopped %ld %s\n", stopped_job->id, (long)pid,
                   stopped_job->command);
            return 128 + WSTOPSIG(status);
        }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    }
    return 1;
}

void nova_process_shutdown(void) {
    size_t i;
    for (i = 0; i < NOVA_JOB_LIMIT; i++) {
        if (jobs[i].used && !jobs[i].done) {
            if (jobs[i].stopped) (void)kill(jobs[i].pid, SIGCONT);
            (void)kill(jobs[i].pid, SIGHUP);
            (void)kill(jobs[i].pid, SIGTERM);
        }
    }
}
