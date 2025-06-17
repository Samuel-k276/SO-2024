#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include "parser.h"
#include "src/client/api.h"
#include "src/common/constants.h"
#include "src/common/io.h"

int flag_terminate = 0;

void *notif_read(void *args) {
  ssize_t bytes_read = 0;
  char key[MAX_STRING_SIZE + 1];
  char value[MAX_STRING_SIZE + 1];
  int NOTIF_FD = *((int*) args);
  while (1) {
    if(flag_terminate) {
      return NULL;
    }
    bytes_read = read(NOTIF_FD, key, MAX_STRING_SIZE + 1);
    if (bytes_read == 0) {
      terminate();
      close(STDIN_FILENO);
      return NULL;
    } else if (bytes_read == -1) {
      continue;
    }
    bytes_read = read(NOTIF_FD, value, MAX_STRING_SIZE + 1);
    fprintf(stderr, "(%s,%s)\n",key, value);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <client_unique_id> <register_pipe_path>\n",
            argv[0]);
    return 1;
  }

  char req_pipe_path[256] = "//tmp/A33_req";
  char resp_pipe_path[256] = "//tmp/A33_resp";
  char notif_pipe_path[256] = "//tmp/A33_notif";

  char keys[MAX_NUMBER_SUB][MAX_STRING_SIZE] = {0};
  unsigned int delay_ms;
  size_t num;

  strncat(req_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(resp_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(notif_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));

  // CREATE req, resp, notif pipes of client
  if(mkfifo(req_pipe_path, 0777) == -1) {
    fprintf(stderr, "Failed to create FIFO: %s\n", req_pipe_path);
    return 1;
  }
  if(mkfifo(resp_pipe_path, 0777) == -1) {
    fprintf(stderr, "Failed to create FIFO: %s\n", resp_pipe_path);
    return 1;
  }
  if(mkfifo(notif_pipe_path, 0777) == -1) {
    fprintf(stderr, "Failed to create FIFO: %s\n", notif_pipe_path);
    return 1;
  }

  int notif_pipe;
  if (kvs_connect(req_pipe_path, resp_pipe_path, argv[2],
                  notif_pipe_path, &notif_pipe)) {
    fprintf(stderr, "Failed to connect to the server\n");
    return 1;
  }

  pthread_t notif_thread;
  
  if (pthread_create(&notif_thread, NULL, notif_read, &notif_pipe) != 0)  {
      write(STDERR_FILENO, "Failed to create thread\n", 24);
  }
  
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

  while (1) {
    switch (get_next(STDIN_FILENO)) {
    case CMD_DISCONNECT:
      flag_terminate = 1;
      if (kvs_disconnect() != 0) {
        fprintf(stderr, "Failed to disconnect to the server\n");
        return 1;
      }
      // TODO: end notifications thread
      pthread_join(notif_thread, NULL); 
      printf("Disconnected from server\n");
      return 0;

    case CMD_SUBSCRIBE:
      num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
      if (num == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (!kvs_subscribe(keys[0])) {
        fprintf(stderr, "Command subscribe failed\n");
      }

      break;

    case CMD_UNSUBSCRIBE:
      num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
      if (num == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_unsubscribe(keys[0])) {
        fprintf(stderr, "Command unsubscribe failed\n");
      }

      break;

    case CMD_DELAY:
      if (parse_delay(STDIN_FILENO, &delay_ms) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay_ms > 0) {
        printf("Waiting...\n");
        delay(delay_ms);
      }
      break;

    case CMD_INVALID:
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_PING:
      if (kvs_ping()) {
        fprintf(stderr, "Command ping failed\n");
      }
      break;

    case CMD_HELP:
      fprintf(stderr,
          "Available commands:\n"
          " SUBSCRIBE  [key]\n"
          " UNSUBSCRIBE  [key]\n"
          " DELAY <delay_ms>\n"
          " DISCONNECT\n"
          " HELP\n");
      break;

    case CMD_EMPTY:
      break;

    case EOC:
      pthread_join(notif_thread, NULL); 
      // input should end in a disconnect, or it will loop here forever
      return 0;
    }
  }
}
