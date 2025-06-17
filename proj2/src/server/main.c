#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>



#include "constants.h"
#include "io.h"
#include "operations.h"
#include "parser.h"
#include "pthread.h"
#include "src/common/constants.h"
#include "src/common/io.h"

struct SharedData {
  DIR *dir;
  char *dir_name;
  pthread_mutex_t directory_mutex;
};

typedef struct {
  int terminate_task;
  char *req_path;
  char *resp_path;
  char *notif_path;
  int req_fd;
  int resp_fd;
  int notif_fd;
}  SessionData;

int flag_wipe = 0;
int flag_terminate = 0;

atomic_int active_count_sb = 0; // SHOW/BACKUPS
atomic_int active_count_wd = 0; // WRITE/DELETE

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // GLOBAL LOCK FOR SB/WD
pthread_cond_t sb_proceed = PTHREAD_COND_INITIALIZER; // SHOW/BACKUPS CAN NOW HAPPEN, WRITE/DELETE HAVE FINISHED
pthread_cond_t wd_proceed = PTHREAD_COND_INITIALIZER; // WRITE/DELETE CAN NOW HAPPEN, SHOW/BACKUPS HAVE FINISHED

pthread_mutex_t n_current_backups_lock = PTHREAD_MUTEX_INITIALIZER;

sem_t sessions; // ACTIVE SESSIONS SEMAPHORE
volatile int task_count = 0; // WAITING SESSIONS COUNT
SessionData task_queue[128]; // WAITING SESSIONS DATA
SessionData current_tasks[MAX_SESSION_COUNT]; // CURRENT ACTIVE SESSIONS
pthread_mutex_t active_clients;
pthread_mutex_t queue; // QUEUE LOCK
pthread_cond_t queue_proceed; // QUEUE CONDITIONAL VARIABLE 

size_t active_backups = 0; // Number of active backups
size_t max_backups;        // Maximum allowed simultaneous backups
size_t max_threads;        // Maximum allowed simultaneous threads
char *jobs_directory = NULL;
char *FIFO_path = NULL;

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

void handle_sigsur1(int sig) {
  (void) sig;
  flag_wipe = 1;
}

void handle_sigint(int sig) {
  (void) sig;
  flag_terminate = 1;
}

int filter_job_files(const struct dirent *entry) {
  const char *dot = strrchr(entry->d_name, '.');
  if (dot != NULL && strcmp(dot, ".job") == 0) {
    return 1; // Keep this file (it has the .job extension)
  }
  return 0;
}

static int entry_files(const char *dir, struct dirent *entry, char *in_path,
                       char *out_path) {
  const char *dot = strrchr(entry->d_name, '.');
  if (dot == NULL || dot == entry->d_name || strlen(dot) != 4 ||
      strcmp(dot, ".job")) {
    return 1;
  }

  if (strlen(entry->d_name) + strlen(dir) + 2 > MAX_JOB_FILE_NAME_SIZE) {
    fprintf(stderr, "%s/%s\n", dir, entry->d_name);
    return 1;
  }

  strcpy(in_path, dir);
  strcat(in_path, "/");
  strcat(in_path, entry->d_name);

  strcpy(out_path, in_path);
  strcpy(strrchr(out_path, '.'), ".out");

  return 0;
}

static int run_job(int in_fd, int out_fd, char *filename) {
  size_t file_backups = 0;
  while (1) {
    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    switch (get_next(in_fd)) {
    case CMD_WRITE:
      num_pairs =
          parse_write(in_fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      start_wd();

      if (kvs_write(num_pairs, keys, values)) {
        write_str(STDERR_FILENO, "Failed to write pair\n");
      }

      stop_wd();

      break;

    case CMD_READ:
      num_pairs =
          parse_read_delete(in_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_read(num_pairs, keys, out_fd)) {
        write_str(STDERR_FILENO, "Failed to read pair\n");
      }
      break;

    case CMD_DELETE:

      num_pairs =
          parse_read_delete(in_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      start_wd();

      if (kvs_delete(num_pairs, keys, out_fd)) {
        write_str(STDERR_FILENO, "Failed to delete pair\n");
      }

      stop_wd();
      
      break;

    case CMD_SHOW:
      start_sb();
      kvs_show(out_fd);
      stop_sb();
      break;

    case CMD_WAIT:
      if (parse_wait(in_fd, &delay, NULL) == -1) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay > 0) {
        printf("Waiting %d seconds\n", delay / 1000);
        kvs_wait(delay);
      }
      break;

    case CMD_BACKUP:
      pthread_mutex_lock(&n_current_backups_lock);
      if (active_backups >= max_backups) {
        wait(NULL);
      } else {
        active_backups++;
      }
      pthread_mutex_unlock(&n_current_backups_lock);
      start_sb();
      int aux = kvs_backup(++file_backups, filename, jobs_directory);
      stop_sb();
      if (aux < 0) {
        write_str(STDERR_FILENO, "Failed to do backup\n");
      } else if (aux == 1) {
        return 1;
      }
      break;

    case CMD_INVALID:
      write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
      break;

    case CMD_HELP:
      write_str(STDOUT_FILENO,
                "Available commands:\n"
                "  WRITE [(key,value)(key2,value2),...]\n"
                "  READ [key,key2,...]\n"
                "  DELETE [key,key2,...]\n"
                "  SHOW\n"
                "  WAIT <delay_ms>\n"
                "  BACKUP\n" // Not implemented
                "  HELP\n");

      break;

    case CMD_EMPTY:
      break;

    case EOC:
      printf("EOF\n");
      return 0;
    }
  }
}

// frees arguments
static void *get_file(void *arguments) {
  struct SharedData *thread_data = (struct SharedData *)arguments;
  DIR *dir = thread_data->dir;
  char *dir_name = thread_data->dir_name;

  if (pthread_mutex_lock(&thread_data->directory_mutex) != 0) {
    fprintf(stderr, "Thread failed to lock directory_mutex\n");
    return NULL;
  }

  struct dirent *entry;
  char in_path[MAX_JOB_FILE_NAME_SIZE], out_path[MAX_JOB_FILE_NAME_SIZE];
  while ((entry = readdir(dir)) != NULL) {
    if (entry_files(dir_name, entry, in_path, out_path)) {
      continue;
    }

    if (pthread_mutex_unlock(&thread_data->directory_mutex) != 0) {
      fprintf(stderr, "Thread failed to unlock directory_mutex\n");
      return NULL;
    }

    int in_fd = open(in_path, O_RDONLY);
    if (in_fd == -1) {
      write_str(STDERR_FILENO, "Failed to open input file: ");
      write_str(STDERR_FILENO, in_path);
      write_str(STDERR_FILENO, "\n");
      pthread_exit(NULL);
    }

    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd == -1) {
      write_str(STDERR_FILENO, "Failed to open output file: ");
      write_str(STDERR_FILENO, out_path);
      write_str(STDERR_FILENO, "\n");
      pthread_exit(NULL);
    }

    int out = run_job(in_fd, out_fd, entry->d_name);

    close(in_fd);
    close(out_fd);

    if (out) {
      if (closedir(dir) == -1) {
        fprintf(stderr, "Failed to close directory\n");
        return 0;
      }

      exit(0);
    }

    if (pthread_mutex_lock(&thread_data->directory_mutex) != 0) {
      fprintf(stderr, "Thread failed to lock directory_mutex\n");
      return NULL;
    }
  }

  if (pthread_mutex_unlock(&thread_data->directory_mutex) != 0) {
    fprintf(stderr, "Thread failed to unlock directory_mutex\n");
    return NULL;
  }

  pthread_exit(NULL);
}

void run_session(SessionData session) {
  fprintf(stderr, "session REQ PATHS: %s\n", session.req_path);
  fprintf(stderr, "session RESP PATHS: %s\n", session.resp_path);
  fprintf(stderr, "session NOTIF PATHS: %s\n", session.notif_path);
  int req_fd = open(session.req_path, O_RDONLY);
  if (req_fd == -1 ) {
    fprintf(stderr, "Failed to open req FIFO: %d\n", req_fd);
    return;
  }

  int resp_fd = open(session.resp_path, O_WRONLY);
  if (resp_fd == -1 ) {
    fprintf(stderr, "Failed to open resp FIFO: %d\n", resp_fd);
    return;
  }

  int notif_fd = open(session.notif_path, O_WRONLY);
  if (notif_fd == -1 ) {
    fprintf(stderr, "Failed to open notif FIFO: %d\n", notif_fd);
    return;
  }
  int i = 0;
  while (strcmp(current_tasks[i].resp_path, session.resp_path) != 0) {
    i++;
  }
  current_tasks[i].req_fd = req_fd;
  current_tasks[i].resp_fd = resp_fd;
  current_tasks[i].notif_fd = notif_fd;

  write(resp_fd, "10", 2);
  char key[MAX_STRING_SIZE + 1];

  while (1) {
    switch (get_request(req_fd)) {
    case OP_CODE_DISCONNECT:
      kvs_unsubscribe_client(session.notif_path);
      write(resp_fd, "20", 2);
      close(req_fd);
      close(resp_fd);
      close(notif_fd);
      free(session.req_path);
      free(session.resp_path);
      free(session.notif_path);
      return;

    case OP_CODE_SUBSCRIBE:
      read(req_fd, key, 41);
      if(!kvs_subscribe(key, session.notif_path, notif_fd)) {
        fprintf(stderr, "FAILED TO SUBSCRIBE TO: %s\n", key);
        write(resp_fd, "30", 2);
        break;
      }
      write(resp_fd, "31", 2);
      break;

    case OP_CODE_UNSUBSCRIBE:
      read(req_fd, key, 41);
      if(kvs_unsubscribe(key, session.notif_path)) {
        fprintf(stderr, "FAILED TO UNSUBSCRIBE TO: %s\n", key);
        write(resp_fd, "41", 2);
        break;
      }
      write(resp_fd, "40", 2);
      break;

    case OP_CODE_PING:
      write(resp_fd, "50", 2);
      break;

    case OP_CODE_INVALID:
      free(session.req_path);
      free(session.resp_path);
      free(session.notif_path);
      return;
    }


  }
}

void insert_task(SessionData task) {
  pthread_mutex_lock(&queue);
  task_queue[task_count] = task;
  task_count++;
  pthread_mutex_unlock(&queue);
  // SIGNAL WAITING THREAD TO EXECUTE TASK
  pthread_cond_signal(&queue_proceed);
}

void *start_thread() {
  // BLOCK SIGSUR1 IN CLIENT THREADS
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);
  int i;
  while(1) {
    SessionData session;
    pthread_mutex_lock(&queue);
    while (task_count == 0) {
      // WAIT UNTIL THREAD IS SIGNALED 
      pthread_cond_wait(&queue_proceed, &queue);
    }
    sem_wait(&sessions);
    // GET TASK
    session = task_queue[0];
    // REMOVE TASK FROM QUEUE AND SHIFT
    for(i = 0; i < task_count - 1; i++) {
      task_queue[i] = task_queue[i + 1];
    }
    task_count--;
    // TERMINATE THREAD IF ITS A TERMINATE TASK
    if(session.terminate_task) {
      pthread_mutex_unlock(&queue);
      return NULL;
    }
    pthread_mutex_unlock(&queue);
    // ADD TASK TO EMPTY SLOT
    pthread_mutex_lock(&active_clients);
    i = 0;
    while (current_tasks[i].resp_path != 0) {
      i++;
    }
    current_tasks[i] = session;
    pthread_mutex_unlock(&active_clients);
    // EXECUTE TASK
    run_session(session);
    // REMOVE CLIENT FROM ACTIVE CLIENTS
    pthread_mutex_lock(&active_clients);
    i = 0;
    while (strcmp(session.resp_path, current_tasks[i].resp_path) != 0) {
      i++;
    }
    current_tasks[i].req_path = 0;
    current_tasks[i].resp_path = 0;
    current_tasks[i].notif_path = 0;
    pthread_mutex_unlock(&active_clients);
    sem_post(&sessions);
  }
}

static void dispatch_threads(DIR *dir) {
  pthread_t *threads = malloc(max_threads * sizeof(pthread_t));

  if (threads == NULL) {
    fprintf(stderr, "Failed to allocate memory for threads\n");
    return;
  }

  struct SharedData thread_data = {dir, jobs_directory,
                                   PTHREAD_MUTEX_INITIALIZER};

  for (size_t i = 0; i < max_threads; i++) {
    if (pthread_create(&threads[i], NULL, get_file, (void *)&thread_data) !=
        0) {
      fprintf(stderr, "Failed to create thread %zu\n", i);
      pthread_mutex_destroy(&thread_data.directory_mutex);
      free(threads);
      return;
    }
  }

  
// CREATE FIFO REGISTER FILE
  char FIFO_pathname[MAX_PIPE_PATH_LENGTH + 14];
  strcpy(FIFO_pathname, FIFO_path);
  strcat(FIFO_pathname, "/A33_register");
  printf("%s\n", FIFO_pathname);
  if(mkfifo(FIFO_pathname, 0777) == -1) {
    fprintf(stderr, "Failed to create FIFO: %s\n", FIFO_pathname);
    return;
  }


  sem_init(&sessions, 0, MAX_NUMBER_SUB); // INITIALIZE SEMAPHORE SESSIONS
  // ACTIVE CLIENTS LOCK
  pthread_mutex_init(&active_clients, NULL);
  // THREAD POOL QUEUE init
  pthread_mutex_init(&queue, NULL);
  pthread_cond_init(&queue_proceed, NULL);
  // CREATE MANAGER THREADS
  int i = 0;
  pthread_t session_threads[MAX_SESSION_COUNT];
  for(i = 0; i < (int) MAX_SESSION_COUNT; i++) {
    if (pthread_create(&session_threads[i], NULL, &start_thread, NULL) != 0)  {
      write(STDERR_FILENO, "Failed to create thread\n", 24);
    }
  }

  // READ FIFO REGISTER
  int FIFO_fd = open(FIFO_pathname, O_RDONLY | O_NONBLOCK);
  if (FIFO_fd == -1) {
    fprintf(stderr, "Failed to open FIFO: %s\n", FIFO_pathname);
    return ;
  }

  pthread_t job_threads;
      if (pthread_create(&job_threads, NULL, &start_thread, NULL) != 0)  {
      write(STDERR_FILENO, "Failed to create thread\n", 24);
    }

  char opcode[1];
  ssize_t bytes_read;
  SessionData session = {0};

  while (1) {
    if (flag_wipe || flag_terminate) {
      for ( i = 0; i < MAX_SESSION_COUNT; i++) {
        if (current_tasks[i].req_fd > 0) {
          close(current_tasks[i].req_fd);
          close(current_tasks[i].resp_fd);
          close(current_tasks[i].notif_fd);
        }
      }      
      kvs_unsubscribe_all();
      flag_wipe = 0;

      // Fechar a FIFO de registo
      if (flag_terminate) {
        close(FIFO_fd);
        if (unlink(FIFO_pathname) == -1) {
          write(STDERR_FILENO, "Failed to unlink FIFO\n", 23);
        }
        for (i = 0; i < (int) max_threads; i++) {
          if (pthread_cancel(threads[i]) != 0) {
            write(STDERR_FILENO, "Failed to cancel thread\n", 25);
          }
        }
        // Terminar
        for (i = 0; i < (int) max_threads; i++) {
          if (pthread_join(threads[i], NULL) != 0) {
            write(STDERR_FILENO, "Failed to join thread\n", 23);
          }
        }
        write(STDERR_FILENO, "\n", 1);
        exit(0);
      }
    }

    bytes_read = read(FIFO_fd, opcode, 1);
    if (bytes_read <= 0) {
      continue;
    }
    if (!strcmp(opcode, "1")) {
      continue;
    }
    
    session.req_path = (char *)malloc(MAX_PIPE_PATH_LENGTH * sizeof(char));
    session.resp_path = (char *)malloc(MAX_PIPE_PATH_LENGTH * sizeof(char));
    session.notif_path = (char *)malloc(MAX_PIPE_PATH_LENGTH * sizeof(char));

    bytes_read =read(FIFO_fd, session.req_path, MAX_PIPE_PATH_LENGTH);
    bytes_read = read(FIFO_fd, session.resp_path, MAX_PIPE_PATH_LENGTH);
    bytes_read =read(FIFO_fd, session.notif_path, MAX_PIPE_PATH_LENGTH);
    insert_task(session);
  }

  for (i = 0; i < (int) max_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Failed to join thread %u\n", i);
      pthread_mutex_destroy(&thread_data.directory_mutex);
      free(threads);
      return;
    }
  }

  if (pthread_mutex_destroy(&thread_data.directory_mutex) != 0) {
    fprintf(stderr, "Failed to destroy directory_mutex\n");
  }

  free(threads);
}

int main(int argc, char **argv) {
  // PROCESS ID TO SEND SIGNAL SIGUSR1
  pid_t pid = getpid();
  fprintf(stderr, "PID OF SERVER: %d\n", pid);

  // SIGACTION STRUCT TO HANDLE SIGSUR1
  struct sigaction sa; {
    sa.sa_handler = &handle_sigsur1;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
  };

  struct sigaction sa_sigint; {
    sa_sigint.sa_handler = &handle_sigint;
    sa_sigint.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_sigint, NULL);
  };

  // PUT ALL ACTIVE SESSIONS TO 0
  SessionData start = {0};
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    current_tasks[i] = start;
  }

  if (argc < 5) {
    write_str(STDERR_FILENO, "Usage: ");
    write_str(STDERR_FILENO, argv[0]);
    write_str(STDERR_FILENO, " <jobs_dir>");
    write_str(STDERR_FILENO, " <max_threads>");
    write_str(STDERR_FILENO, " <max_backups> \n");
    write_str(STDERR_FILENO, " <name_of_FIFO_register> \n");
    return 1;
  }

  jobs_directory = argv[1];

  char *endptr;
  max_backups = strtoul(argv[3], &endptr, 10);

  if (*endptr != '\0') {
    fprintf(stderr, "Invalid max_proc value\n");
    return 1;
  }

  max_threads = strtoul(argv[2], &endptr, 10);

  if (*endptr != '\0') {
    fprintf(stderr, "Invalid max_threads value\n");
    return 1;
  }

  FIFO_path = argv[4];

  if(sizeof(FIFO_path) > MAX_PIPE_PATH_LENGTH) {
    write_str(STDERR_FILENO, "Invalid length of pipe path\n");
    return 0;
  }


  if (max_backups <= 0) {
    write_str(STDERR_FILENO, "Invalid number of backups\n");
    return 0;
  }

  if (max_threads <= 0) {
    write_str(STDERR_FILENO, "Invalid number of threads\n");
    return 0;
  }

  if (kvs_init()) {
    write_str(STDERR_FILENO, "Failed to initialize KVS\n");
    return 1;
  }

  DIR *dir = opendir(argv[1]);
  if (dir == NULL) {
    fprintf(stderr, "Failed to open directory: %s\n", argv[1]);
    return 0;
  }

  dispatch_threads(dir); // DO JOB FILES AND SESSIONS

  if (closedir(dir) == -1) {
    fprintf(stderr, "Failed to close directory\n");
    return 0;
  }

  while (active_backups > 0) {
    wait(NULL);
    active_backups--;
  }

  kvs_terminate();

  return 0;
}
