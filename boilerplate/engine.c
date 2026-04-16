/*
 * engine.c - Supervised Multi-Container Runtime (User Space)
 *
 * Intentionally partial starter:
 *   - command-line shape is defined
 *   - key runtime data structures are defined
 *   - bounded-buffer skeleton is defined
 *   - supervisor / client split is outlined
 *
 * Students are expected to design:
 *   - the control-plane IPC implementation
 *   - container lifecycle and metadata synchronization
 *   - clone + namespace setup for each container
 *   - producer/consumer behavior for log buffering
 *   - signal handling and graceful shutdown
 */

#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

#define STACK_SIZE (1024 * 1024)
#define SOCKET_PATH "/tmp/engine.sock"
#define BUFFER_SIZE 10

/* ---------------- STRUCTS ---------------- */

typedef struct {
    char id[32];
    char rootfs[256];
    char command[256];
    int pipe_fd;
} child_config_t;

typedef struct {
    char id[32];
    pid_t pid;
    char state[16];
} container_t;

typedef struct {
    char data[BUFFER_SIZE][256];
    int head, tail, count;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} log_buffer_t;

/* ---------------- GLOBALS ---------------- */

container_t containers[100];
int container_count = 0;

log_buffer_t log_buffer;
int logging_done = 0;

/* ---------------- BUFFER ---------------- */

void init_buffer() {
    log_buffer.head = log_buffer.tail = log_buffer.count = 0;
    pthread_mutex_init(&log_buffer.mutex, NULL);
    pthread_cond_init(&log_buffer.not_full, NULL);
    pthread_cond_init(&log_buffer.not_empty, NULL);
}

/* ---------------- PRODUCER ---------------- */

typedef struct {
    int fd;
    char id[32];
} producer_arg_t;

void *producer(void *arg) {
    producer_arg_t *p = (producer_arg_t *)arg;
    char buffer[256];

    while (1) {
        int n = read(p->fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) break;

        buffer[n] = '\0';

        pthread_mutex_lock(&log_buffer.mutex);

        while (log_buffer.count == BUFFER_SIZE)
            pthread_cond_wait(&log_buffer.not_full, &log_buffer.mutex);

        snprintf(log_buffer.data[log_buffer.tail], 256,
                 "%s:%s", p->id, buffer);

        log_buffer.tail = (log_buffer.tail + 1) % BUFFER_SIZE;
        log_buffer.count++;

        pthread_cond_signal(&log_buffer.not_empty);
        pthread_mutex_unlock(&log_buffer.mutex);
    }

    close(p->fd);
    free(p);
    return NULL;
}

/* ---------------- CONSUMER ---------------- */

void *consumer(void *arg) {
    (void)arg;

    while (!logging_done || log_buffer.count > 0) {
        pthread_mutex_lock(&log_buffer.mutex);

        while (log_buffer.count == 0 && !logging_done)
            pthread_cond_wait(&log_buffer.not_empty, &log_buffer.mutex);

        if (log_buffer.count == 0 && logging_done) {
            pthread_mutex_unlock(&log_buffer.mutex);
            break;
        }

        char line[256];
        strcpy(line, log_buffer.data[log_buffer.head]);

        log_buffer.head = (log_buffer.head + 1) % BUFFER_SIZE;
        log_buffer.count--;

        pthread_cond_signal(&log_buffer.not_full);
        pthread_mutex_unlock(&log_buffer.mutex);

        /* Extract container id */
        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        char *id = line;
        char *msg = colon + 1;

        char filename[64];
        sprintf(filename, "%s.log", id);

        FILE *f = fopen(filename, "a");
        if (f) {
            fprintf(f, "%s", msg);
            fclose(f);
        }
    }

    return NULL;
}

/* ---------------- CHILD ---------------- */

int child_fn(void *arg)
{
    child_config_t *cfg = (child_config_t *)arg;

    sethostname(cfg->id, strlen(cfg->id));

    dup2(cfg->pipe_fd, STDOUT_FILENO);
    dup2(cfg->pipe_fd, STDERR_FILENO);
    close(cfg->pipe_fd);

    chroot(cfg->rootfs);
    chdir("/");
    mount("proc", "/proc", "proc", 0, NULL);

    char *args[10];
    int i = 0;

    char *token = strtok(cfg->command, " ");
    while (token != NULL && i < 9) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;

    execvp(args[0], args);

    perror("exec");
    return 1;
}

/* ---------------- START ---------------- */

int start_container(child_config_t *cfg)
{
    int pipefd[2];
    pipe(pipefd);

    char *stack = malloc(STACK_SIZE);
    char *stack_top = stack + STACK_SIZE;

    cfg->pipe_fd = pipefd[1];

    pid_t pid = clone(child_fn, stack_top,
                      CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD,
                      cfg);

    if (pid < 0) {
        perror("clone");
        return -1;
    }

    close(pipefd[1]);

    producer_arg_t *p = malloc(sizeof(producer_arg_t));
    p->fd = pipefd[0];
    strcpy(p->id, cfg->id);

    pthread_t tid;
    pthread_create(&tid, NULL, producer, p);

    strcpy(containers[container_count].id, cfg->id);
    containers[container_count].pid = pid;
    strcpy(containers[container_count].state, "RUNNING");
    container_count++;

    printf("Started %s PID %d\n", cfg->id, pid);
    return pid;
}

/* ---------------- STOP ---------------- */

void stop_container(char *id)
{
    for (int i = 0; i < container_count; i++) {
        if (strcmp(containers[i].id, id) == 0) {
            kill(containers[i].pid, SIGTERM);
        }
    }
}

/* ---------------- PS ---------------- */

void list_containers(int client_fd)
{
    char buffer[512] = "";

    for (int i = 0; i < container_count; i++) {
        char line[128];
        sprintf(line, "%s %d %s\n",
                containers[i].id,
                containers[i].pid,
                containers[i].state);
        strcat(buffer, line);
    }

    write(client_fd, buffer, strlen(buffer));
}

/* ---------------- SIGNAL ---------------- */

void handle_sigint(int sig)
{
    printf("\nShutting down supervisor...\n");

    logging_done = 1;
    pthread_cond_broadcast(&log_buffer.not_empty);

    unlink(SOCKET_PATH);
    exit(0);
}

/* ---------------- SUPERVISOR ---------------- */

void run_supervisor(const char *rootfs)
{
    init_buffer();

    pthread_t consumer_thread;
    pthread_create(&consumer_thread, NULL, consumer, NULL);

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    int server_fd, client_fd;
    struct sockaddr_un addr;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    unlink(SOCKET_PATH);
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Supervisor running\n");

    while (1)
    {
        client_fd = accept(server_fd, NULL, NULL);

        char buffer[256] = {0};
        read(client_fd, buffer, sizeof(buffer));

        char *cmd = strtok(buffer, " ");

        if (strcmp(cmd, "start") == 0) {
            char *id = strtok(NULL, " ");
            char *rootfs = strtok(NULL, " ");
            char *prog = strtok(NULL, "");

            child_config_t *cfg = malloc(sizeof(child_config_t));
            strcpy(cfg->id, id);
            strcpy(cfg->rootfs, rootfs);
            strcpy(cfg->command, prog);

            start_container(cfg);
        }
        else if (strcmp(cmd, "ps") == 0) {
            list_containers(client_fd);
        }
        else if (strcmp(cmd, "stop") == 0) {
            char *id = strtok(NULL, " ");
            stop_container(id);
        }

        close(client_fd);

        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < container_count; i++) {
                if (containers[i].pid == pid) {
                    strcpy(containers[i].state, "STOPPED");
                }
            }
        }
    }
}

/* ---------------- CLIENT ---------------- */

int send_command(char *cmd)
{
    int sock;
    struct sockaddr_un addr;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    write(sock, cmd, strlen(cmd));

    char response[512] = {0};
    read(sock, response, sizeof(response));
    printf("%s\n", response);

    close(sock);
    return 0;
}

/* ---------------- MAIN ---------------- */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage\n");
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        run_supervisor(argv[2]);
        return 0;
    }

    char command[256] = "";
    for (int i = 1; i < argc; i++) {
        strcat(command, argv[i]);
        strcat(command, " ");
    }

    return send_command(command);
}
