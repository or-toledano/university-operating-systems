#include <stdio.h>
#include <unistd.h>
#include <wait.h>
#include <stdlib.h>
#include <string.h>

#define NOT_FOUND (-1)


void set_handler(int signo, void(*handler)(int)) { /* Set handler of signo */
    struct sigaction sig_action;
    sig_action.sa_handler = handler;
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_flags = 0;
    if (sigaction(signo, &sig_action, NULL) == -1) {
        perror("Failed to set sigaction");
        exit(EXIT_FAILURE);
    }
}

/* Prepare and finalize calls for initialization and destruction of anything required */
int prepare(void) {
    set_handler(SIGINT, SIG_IGN); /* SIGINT shouldn't terminate our shell */
    set_handler(SIGCHLD, SIG_IGN); /* To get rid of zombies */
    return EXIT_SUCCESS;
}


int locate_pipe(int count, char **arglist) {
    for (int i = 1; i < count - 1; ++i) /* Assumptions of the pipe's location are used here */
        if (arglist[i][0] == '|')
            return i;
    return NOT_FOUND;
}

void execvp_or_error(char **arglist) {
    if (execvp(arglist[0], arglist) == -1) {
        perror("Failed executing");
        _exit(EXIT_FAILURE);
    }
}

void fork_fail__exit() {
    perror("Failed forking");
    _exit(EXIT_FAILURE);
}

/* arglist - a list of char* arguments (words) provided by the user
 * it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
 * count > 0
 * RETURNS - 1 if should continue, 0 otherwise */
int process_arglist(int count, char **arglist) {
    pid_t child_pid;
    int pipe_index = locate_pipe(count, arglist);
    if (pipe_index == NOT_FOUND) { /* A command without a pipe */
        _Bool to_background = count != 1 && arglist[count - 1][0] == '&'; /* We use the assumptions about & here */
        if (to_background)
            arglist[count - 1] = NULL; /* Remove the & */
        child_pid = fork();
        if (child_pid > 0) /* Parent */
        {
            if (!to_background) /* Wait only for foreground processes */
                waitpid(child_pid, NULL, 0);
        } else if (child_pid == 0) { /* Child */
            if (!to_background)
                set_handler(SIGINT, SIG_DFL); /* SIGINT should only terminate foreground processes */
            execvp_or_error(arglist);
        } else {
            perror("Failed forking");
            exit(EXIT_FAILURE);
        }
    } else { /* A command with a pipe */
        int pipefds[2];
        if (pipe(pipefds) == -1) {
            perror("Failed creating pipe");
            exit(EXIT_FAILURE);
        }
        int read_fd = pipefds[0];
        int write_fd = pipefds[1];
        child_pid = fork();
        if (child_pid == 0) { /* Child (left of the pipe) */
            close(read_fd);
            dup2(write_fd, STDOUT_FILENO); /* out of the child -> 'write' side of the pipe */
            arglist[pipe_index] = NULL; /* We take the left part of the arglist */
            set_handler(SIGINT, SIG_DFL); /* SIGINT should terminate foreground processes */
            execvp_or_error(arglist);
        } else if (child_pid < 0) {
            close(read_fd);
            close(write_fd);
            fork_fail__exit();
        }
        child_pid = fork();
        if (child_pid == 0) { /* Child (right of the pipe) */
            close(write_fd);
            dup2(read_fd, STDIN_FILENO); /* 'read' side of the pipe -> input of the child */
            arglist += pipe_index + 1;  /* We take the right part of the arglist */
            set_handler(SIGINT, SIG_DFL); /* SIGINT should terminate foreground processes */
            execvp_or_error(arglist);
        } else if (child_pid < 0) {
            close(read_fd);
            close(write_fd);
            fork_fail__exit();
        }
        close(write_fd);
        close(read_fd);
        wait(NULL);
        wait(NULL);
    }
    return 1;
}

int finalize(void) {
    return EXIT_SUCCESS;
}

