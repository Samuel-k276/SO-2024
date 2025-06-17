#define _DEFAULT_SOURCE

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdatomic.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "auxiliar.h"

// TASK FOR THREADS
typedef struct {
  int last_task;
  char *file_in;
  char *file_out;
} ThreadData;

atomic_int active_count_sb; // SHOW/BACKUPS
atomic_int active_count_wd; // WRITE/DELETE

pthread_mutex_t lock; // GLOBAL LOCK FOR SB/WD
pthread_cond_t sb_proceed; // SHOW/BACKUPS CAN NOW HAPPEN, WRITE/DELETE HAVE FINISHED
pthread_cond_t wd_proceed; // WRITE/DELETE CAN NOW HAPPEN, SHOW/BACKUPS HAVE FINISHED
sem_t *sem_backups; // SEMAPHORE FOR NUMBER OF BACKUPS AT A TIME

volatile int task_count = 0; // TASKS LEFT
pthread_mutex_t queue; // QUEUE LOCK
pthread_cond_t queue_proceed; // QUEUE CONDITIONAL VARIABLE 
ThreadData task_queue[128]; // TASKS QUEUE

// LOCK MANAGEMENT
void start_sb() {
  // STOP THE FLOW OF WRITE/DELETE 
  pthread_mutex_lock(&lock);
  // WAIT FOR ACTIVE WRITE/DELETE 
  while (atomic_load(&active_count_wd) > 0)  {
    pthread_cond_wait(&sb_proceed, &lock);
  }
  atomic_fetch_add(&active_count_sb, 1);
  pthread_mutex_unlock(&lock);
}

void start_wd() {
  // STOP THE FLOW OF SHOW/BACKUP
  pthread_mutex_lock(&lock);
  // WAIT FOR ACTIVE SHOW/BACKUP
  while (atomic_load(&active_count_sb) > 0)  {
    pthread_cond_wait(&wd_proceed, &lock);
  }
  atomic_fetch_add(&active_count_wd, 1);
  pthread_mutex_unlock(&lock);
}

void stop_sb() {
  atomic_fetch_sub(&active_count_sb, 1);
  if(atomic_load(&active_count_sb) == 0) {
    // SIGNAL TO LET WRITE/DELETE START
    pthread_cond_signal(&wd_proceed); 
  }
}

void stop_wd() {
    atomic_fetch_sub(&active_count_wd, 1);
  if(atomic_load(&active_count_wd) == 0) {
    // SIGNAL TO LET SHOW/BACKUP START
    pthread_cond_signal(&sb_proceed);
  }
}

// SIGNAL CHILD TERMINATED HANDLER
void handle_sigchild (int sig) {
  (void) sig;
  while ( waitpid(-1, NULL, WNOHANG) > 0){
    // DECREMENT SHOW/BACKUP COUNTER
    stop_sb();
    // INCREMENT AVAILABLE BACKUPS
    sem_post(sem_backups);
  }
}

// FUNCTION OF TASK
void kvs(ThreadData task) {
  char* filepath_in = task.file_in;
  char *filepath_out = task.file_out;

  // OPEN FILE IN (.jobs)
  int file_job = open(filepath_in, O_RDONLY);
  if (file_job == -1) {
    fprintf(stderr, "Failed to open file\n");
  }

  // OPEN FILE OUT (.out)
  int file_out = open(filepath_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file_out == -1) {
    fprintf(stderr, "Failed to open file\n");
  }
  // INITIALIZE NUMBER OF BACKUPS FOR THIS FILE
  int backup_counter = 0;
  // RUN KVS ON FILE UNTIL EOF
  while (1) {
    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    switch (get_next(file_job)) {
      case CMD_WRITE:
        num_pairs =
        parse_write(file_job, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
        if (num_pairs == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        start_wd();
        if (kvs_write(num_pairs, keys, values, file_out)) {
          fprintf(stderr, "Failed to write pair\n");
        }
        stop_wd();
        break;

      case CMD_READ:
        num_pairs =
        parse_read_delete(file_job, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

        if (num_pairs == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }
        if (kvs_read(num_pairs, keys, file_out)) {
          fprintf(stderr, "Failed to read pair\n");
        }
        break;

      case CMD_DELETE:
        num_pairs =
        parse_read_delete(file_job, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

        if (num_pairs == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        start_wd();

        if (kvs_delete(num_pairs, keys, file_out)) {
          fprintf(stderr, "Failed to delete pair\n");
        }
        stop_wd();
        break;

      case CMD_SHOW:
        start_sb();
        kvs_show(file_out);
        stop_sb();
        break;

      case CMD_WAIT:
        if (parse_wait(file_job, &delay, NULL) == -1) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (delay > 0) {
          write(file_out, "Waiting...\n", 11);
          kvs_wait(delay);
        }
        break;

      case CMD_BACKUP:
        start_sb();
        int pid = fork();
        backup_counter++;
        if (pid < 0 ) {
          fprintf(stderr, "Failed to perform backup.\n");
          break;
        }

        if (pid == 0) {
          sem_wait(sem_backups);
          if (kvs_backup(filepath_out, backup_counter)) {
            fprintf(stderr, "Failed to perform backup.\n");
          }
          _exit(0);
        } else if (pid > 0) {

        }
        break;
      case CMD_INVALID:
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        write(file_out,
              "Available commands:\n"
              "  WRITE [(key,value)(key2,value2),...]\n"
              "  READ [key,key2,...]\n"
              "  DELETE [key,key2,...]\n"
              "  SHOW\n"
              "  WAIT <delay_ms>\n"
              "  BACKUP\n"
              "  HELP\n",
              139);
        break;

      case CMD_EMPTY:
        break;

      case EOC:
        close(file_job);
        close(file_out);
        goto end_loops;
      }
    }
  end_loops:
  // WAIT PARENT FOR CHILD PROCESS TO FINISH
  while (waitpid(-1, NULL, 0) > 0) {
    sem_post(sem_backups);
    stop_sb();
  };
  // MALLOC NO LONGER NEEDED
  free(filepath_in);
  free(filepath_out);
}

// THREAD POOL
void *start_thread() {
  while(1) {
    ThreadData task;
    pthread_mutex_lock(&queue);
    while (task_count == 0) {
      // WAIT UNTIL THREAD IS SIGNALED 
      pthread_cond_wait(&queue_proceed, &queue);
    }
    // GET TASK
    task = task_queue[0];
    // REMOVE TASK FROM QUEUE AND SHIFT
    for(int i = 0; i < task_count - 1; i++) {
      task_queue[i] = task_queue[i + 1];
    }
    task_count--;
    // TERMINATE THREAD IF ITS A LAST TASK
    if(task.last_task) {
      pthread_mutex_unlock(&queue);
      return NULL;
    }
    pthread_mutex_unlock(&queue);
    // EXECUTE TASK
    kvs(task);
  }
}

// INSTERT TASK IN QUEUE
void insert_task(ThreadData task) {
  pthread_mutex_lock(&queue);
  task_queue[task_count] = task;
  task_count++;
  pthread_mutex_unlock(&queue);
  // SIGNAL WAITING THREAD TO EXECUTE TASK
  pthread_cond_signal(&queue_proceed);
}

int main(int argc, char *argv[]) {
  //INITIALIZE LOCK AND VARIABLE CONDITIONS
  // SB/WD
  pthread_mutex_init(&lock, NULL);
  pthread_cond_init(&sb_proceed, NULL);
  pthread_cond_init(&wd_proceed, NULL);
  // THREAD POOL QUEUE
  pthread_mutex_init(&queue, NULL);
  pthread_cond_init(&queue_proceed, NULL);

  // INITIALIZE KVS
  if (kvs_init()) {
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  // ERROR IF COMMAND IS LESS THAN 3 ARGUMENTS
  if (argc < 3) {
    write(STDERR_FILENO,
          "Usage: ./istkvs <directory_path> <available_backups> <max_threads>\n", 67);
    return EXIT_FAILURE;
  }

  // DIRECTORY PATH
  const char *directory_path = argv[1];
  // MAX BACKUPS AT A TIME
  int max_backups = atoi(argv[2]);
  // MAX THREADS AT A TIME
  unsigned int max_threads = (unsigned int) atoi(argv[3]);

  // BACKUPS SEMAPHORE CREATE 
  sem_backups = sem_open("backups", O_CREAT, 0666, max_backups);
  if (sem_backups == SEM_FAILED) {
      write(STDERR_FILENO, "Failed to open semaphore backups\n", 25);
      return 1;
  }
  
  // SIGACTION STRUCT TO HANDLE SIGCHILD
  struct sigaction sa; {
    sa.sa_handler = &handle_sigchild;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
  };

  // OPEN DIRECTORY
  DIR *dir = opendir(directory_path);
  if (dir == NULL) {
    write(STDERR_FILENO, "Failed to open directory\n", 25);
    return EXIT_FAILURE;
  }

  // CREATE THREADS
  pthread_t threads[max_threads];
  int i;
  for(i = 0; i < (int) max_threads; i++) {
    if (pthread_create(&threads[i], NULL, &start_thread, NULL) != 0)  {
      write(STDERR_FILENO, "Failed to create thread\n", 24);
    }
  }

  struct dirent *entry;
  ThreadData task;

  // GET ALL FILES IN DIR
  while ((entry = readdir(dir)) != NULL) {
    char *filename_in = entry->d_name;
    char *extension = get_extension(filename_in);

    // CHECK IF REGULAR FILE AND EXTENSION IS .job
    if ((entry->d_type != DT_REG) || extension == NULL ||
        strcmp(extension, ".job")) {
      continue;
    }

  // CREATE TASK WITH PATH IN/OUT
  task.last_task = 0;
  task.file_in = make_in_path(directory_path, filename_in);
  task.file_out = make_out_path(directory_path, filename_in);

  // INERT TASK IN QUEUE
  insert_task(task);
  }

  // CREATE TASKS TO TERMINATE THREADS
  for(i = 0; i < (int) max_threads; i++) {
      ThreadData terminate_task;
      terminate_task.last_task = 1;
      insert_task(terminate_task);
  }

  // MAIN WAITS FOR ALL THREADS TO TERMINATE BEFORE CLOSING
  for(i=0; i < (int) max_threads; i++) {
    pthread_join(threads[i], NULL);
  }

  // CLOSE/DESTROY 
  pthread_mutex_destroy(&queue);
  pthread_cond_destroy(&queue_proceed);

  pthread_mutex_destroy(&lock);
  pthread_cond_destroy(&sb_proceed);
  pthread_cond_destroy(&wd_proceed);

  sem_close(sem_backups);
  sem_unlink("backups");

  kvs_terminate();
  if (closedir(dir)!=0) {
    printf("Failed to close directory");
  }
  return 0;
}