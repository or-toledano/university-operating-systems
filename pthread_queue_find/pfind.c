#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>

#define ARG_NUM 4
#define INIT_THREADNO 0
#define INIT_FINISHED_THREADNO 1

static struct global_s {
    unsigned int max_thread_number;
    unsigned int found_counter;
    char *sterm;
    char *init_dir_name;
    DIR *init_dir;
    pthread_mutex_t queue_lock;
    pthread_mutex_t nonempty_cond_lock;
    pthread_mutex_t found_counter_lock;
    pthread_cond_t nonempty_cond;
} global = {.found_counter = 0};

struct thread_resources { /* Free this if a thread exits */
    unsigned int tid;
    char *name_to_free;
    DIR *dir_to_close;
    struct dnode *node_to_free;
};

struct thread_resources *safe_thread_resources_ctor(unsigned int tid) {

    struct thread_resources *t_res = malloc(sizeof(struct thread_resources));
    if (t_res == NULL) {
        fprintf(stderr, "malloc failed for thread %u: %s\n", tid,
                strerror(errno));
        if (tid == INIT_THREADNO) closedir(global.init_dir);
        pthread_exit((void *)EXIT_FAILURE);
    }
    t_res->tid = tid;
    return t_res;
}

void safe_thread_resources_dtor(struct thread_resources *t_res) {
    /* This condition means: we are not in the init thread or we are in the init
     * thread but after the init */
    if (t_res->tid != INIT_THREADNO) free(t_res->name_to_free);
    free(t_res->node_to_free);
    if (closedir(t_res->dir_to_close) < 0) {
        fprintf(stderr, "closedir failed for thread %u: %s\n", t_res->tid,
                strerror(errno));
        free(t_res);
        pthread_exit((void *)EXIT_FAILURE);
    }
}

void *safe_malloc(size_t size, struct thread_resources *t_res) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "malloc failed for thread %u: %s\n", t_res->tid,
                strerror(errno));
        safe_thread_resources_dtor(t_res);
        pthread_exit((void *)EXIT_FAILURE);
    }
    return ptr;
}

DIR *safe_opendir(char *name, struct thread_resources *t_res) {
    DIR *curr_dir = opendir(name);
    if (curr_dir == NULL) {
        fprintf(stderr, "opendir failed for thread %u: %s\n", t_res->tid,
                strerror(errno));
        safe_thread_resources_dtor(t_res);
        pthread_exit((void *)EXIT_FAILURE);
    }

    return curr_dir;
}

struct dnode {
    char *name; /* Path from the search root directory */
    DIR *dir;
    SIMPLEQ_ENTRY(dnode)
    queue_node;
};

/*
 * Returns name_prefix + '\0' + '/' + rel_name
 */
char *concat_path_file(char *name_prefix, char *rel_name,
                       struct thread_resources *t_res) {
    size_t prev_name_len = strlen(name_prefix);
    char *name = safe_malloc(
        (prev_name_len + strlen(rel_name) + 2) * sizeof(char), t_res);
    strcpy(name, name_prefix);
    name[prev_name_len] = '/';
    name[prev_name_len + 1] = '\0';
    strcat(name, rel_name);
    return name;
}

struct dnode *node_ctor(char *name, DIR *curr_dir,
                        struct thread_resources *t_res) {
    struct dnode *node = safe_malloc(sizeof(struct dnode), t_res);
    node->name = name;
    node->dir = curr_dir;
    return node;
}

bool is_regular_directory(char *name) {
    return (bool)(strcmp(name, ".") && strcmp(name, ".."));
}

bool safe_isdir(char *path, struct thread_resources *t_res) {
    struct stat buf;
    if (lstat(path, &buf) < 0) {
        fprintf(stderr, "lstat failed for thread %u: %s\n", t_res->tid,
                strerror(errno));
        safe_thread_resources_dtor(t_res);

        pthread_exit((void *)EXIT_FAILURE);
    }

    return S_ISDIR(buf.st_mode);
}

void safe_pthread_mutex_lock(pthread_mutex_t *mutex,
                             struct thread_resources *t_res) {
    if (pthread_mutex_lock(mutex) < 0) {
        fprintf(stderr, "safe_pthread_mutex_lock failed for thread %u: %s\n",
                t_res->tid, strerror(errno));
        safe_thread_resources_dtor(t_res);
        pthread_exit((void *)EXIT_FAILURE);
    }
}

void safe_pthread_mutex_unlock(pthread_mutex_t *mutex,
                               struct thread_resources *t_res) {
    if (pthread_mutex_unlock(mutex) < 0) {
        fprintf(stderr, "safe_pthread_mutex_unlock failed for thread %u: %s\n",
                t_res->tid, strerror(errno));
        safe_thread_resources_dtor(t_res);
        pthread_exit((void *)EXIT_FAILURE);
    }
}

/*
 * This is one computation of a thread: popping a directory from the queue,
 * searching for the term in the file names for that directory, and inserting
 * directories in that directory to the queue. dir, dir_name are: the initial
 * root directory to begin with, and its name. sterm is the search term.
 */
bool scan_dir(void *tid_arg) {
    static bool all_threads_asleep = false;
    static unsigned int asleep_counter = 0;
    static SIMPLEQ_HEAD(head_s, dnode) head;
    unsigned int tid = (unsigned int)(intptr_t)(tid_arg);
    char *new_name, *dir_name, *temp;
    struct dnode *curr_node, *new_node;
    struct dirent *dir_ent;
    DIR *new_dir;
    struct thread_resources *t_res = safe_thread_resources_ctor(tid);
    while (true) {
        *t_res = (struct thread_resources){t_res->tid};
        if (tid == INIT_THREADNO) { /* Initialize the currently empty queue */
            safe_pthread_mutex_lock(&global.queue_lock, t_res);
            SIMPLEQ_INIT(&head);
            safe_pthread_mutex_unlock(&global.queue_lock, t_res);
            curr_node = node_ctor(global.init_dir_name, global.init_dir, t_res);
            t_res->node_to_free = curr_node;
        } else { /* Dequeue a dir for the current thread */
            safe_pthread_mutex_lock(&global.nonempty_cond_lock, t_res);
            ++asleep_counter;
            while (SIMPLEQ_EMPTY(&head)) {
                /* The next if reduces unnecessary broadcasts compared to */
                if (all_threads_asleep) { /* the next if */
                    safe_pthread_mutex_unlock(&global.nonempty_cond_lock,
                                              t_res);
                    pthread_exit((void *)EXIT_SUCCESS);
                }
                if (asleep_counter == global.max_thread_number) {
                    all_threads_asleep = true;
                    pthread_cond_broadcast(&global.nonempty_cond);
                    safe_pthread_mutex_unlock(&global.nonempty_cond_lock,
                                              t_res);
                    pthread_exit((void *)EXIT_SUCCESS);
                }
                pthread_cond_wait(&global.nonempty_cond,
                                  &global.nonempty_cond_lock);
            }
            --asleep_counter;
            safe_pthread_mutex_lock(&global.queue_lock, t_res);
            curr_node = SIMPLEQ_FIRST(&head);
            t_res->node_to_free = curr_node;
            SIMPLEQ_REMOVE_HEAD(&head, queue_node);
            safe_pthread_mutex_unlock(&global.queue_lock, t_res);
            safe_pthread_mutex_unlock(&global.nonempty_cond_lock, t_res);
            t_res->name_to_free = curr_node->name;
        }
        t_res->dir_to_close = curr_node->dir;
        while ((dir_ent = readdir(curr_node->dir))) {
            dir_name = dir_ent->d_name;
            new_name = concat_path_file(curr_node->name, dir_name, t_res);
            if (safe_isdir(new_name, t_res)) {
                if (is_regular_directory(dir_name)) {
                    new_dir = safe_opendir(new_name, t_res);
                    new_node = node_ctor(new_name, new_dir, t_res);
                    safe_pthread_mutex_lock(&global.nonempty_cond_lock, t_res);
                    safe_pthread_mutex_lock(&global.queue_lock, t_res);
                    SIMPLEQ_INSERT_TAIL(&head, new_node, queue_node);
                    pthread_cond_signal(&global.nonempty_cond);
                    safe_pthread_mutex_unlock(&global.queue_lock, t_res);
                    safe_pthread_mutex_unlock(&global.nonempty_cond_lock,
                                              t_res);
                }
            } else if (strstr(dir_name, global.sterm) != NULL) {
                temp = concat_path_file(curr_node->name, dir_name, t_res);
                printf("%s\n", temp);
                free(temp);
                safe_pthread_mutex_lock(&global.found_counter_lock, t_res);
                ++global.found_counter;
                safe_pthread_mutex_unlock(&global.found_counter_lock, t_res);
            }
        }
        safe_thread_resources_dtor(t_res);
        if (tid == INIT_THREADNO) {
            tid = INIT_FINISHED_THREADNO;
            t_res->tid = INIT_FINISHED_THREADNO;
        }
    }
}

void handler() {
    printf("Search stopped, found %d files\n", global.found_counter);
    exit(EXIT_SUCCESS);
}

void parallel_find() {
    bool all_threads_failed = true;
    pthread_t threads[global.max_thread_number];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    struct sigaction sig_action;
    sig_action.sa_handler = handler;
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_flags = 0;
    if (sigaction(SIGINT, &sig_action, NULL) < 0) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }
    pthread_create(&threads[0], &attr, (void *)scan_dir,
                   (void *)(intptr_t)INIT_THREADNO);
    /* Initialization. The arg = i + 1 is because we used tid 1 for the init
     * thread after the init stage  */
    for (unsigned int i = INIT_FINISHED_THREADNO; i < global.max_thread_number;
         ++i) {
        if (pthread_create(&threads[i], &attr, (void *)scan_dir,
                           (void *)(intptr_t)(i + 1))) {
            fprintf(stderr, "pthread_create failed for thread %u: %s\n", i,
                    strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    void *t_ret_val;
    for (unsigned int i = 0; i < global.max_thread_number; ++i) {
        if (pthread_join(threads[i], &t_ret_val)) {
            fprintf(stderr, "pthread_join failed for thread %u: %s\n", i,
                    strerror(errno));
            exit(EXIT_FAILURE);
        }
        if ((int)(intptr_t)t_ret_val == 0) all_threads_failed = false;
    }
    printf("Done searching, found %d files\n", global.found_counter);
    exit(all_threads_failed);
}

void handle_args(int argc, char *argv[]) {
    if (argc != ARG_NUM) {
        errno = EINVAL;
        perror("invalid number of arguments");
        exit(EXIT_FAILURE);
    }
    if (!(global.init_dir = opendir(argv[1]))) {
        errno = EINVAL;
        perror("not a searchable directory");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&global.queue_lock, NULL) ||
        pthread_mutex_init(&global.nonempty_cond_lock, NULL) ||
        pthread_mutex_init(&global.found_counter_lock, NULL)) {
        perror("pthread_mutex_init failed");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&global.nonempty_cond, NULL)) {
        perror("pthread_mutex_init failed");
        exit(EXIT_FAILURE);
    }
    global.init_dir_name = argv[1];
    global.sterm = argv[2];
    global.max_thread_number = (unsigned int)atoi(argv[3]);
}

int main(int argc, char *argv[]) {
    handle_args(argc, argv);
    parallel_find();
}
