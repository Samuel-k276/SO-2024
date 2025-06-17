#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "api.h"

#include "src/common/constants.h"
#include "src/common/protocol.h"
#include "src/common/io.h"


int REQ_FD;
int RESP_FD;
int NOTIF_FD;

char REQ_PATH[MAX_PIPE_PATH_LENGTH];
char RESP_PATH[MAX_PIPE_PATH_LENGTH];
char NOTIF_PATH[MAX_PIPE_PATH_LENGTH];

int kvs_connect(char const *req_pipe_path, char const *resp_pipe_path,
                char const *server_pipe_path, char const *notif_pipe_path,
                int *notif_pipe) {
  int server_fd;
  char SERVER_FIFO[MAX_PIPE_PATH_LENGTH + 14];
  strcpy(SERVER_FIFO, server_pipe_path);
  strcat(SERVER_FIFO, "/A33_register");
  strcpy(REQ_PATH, req_pipe_path);
  strcpy(RESP_PATH, resp_pipe_path);
  strcpy(NOTIF_PATH, notif_pipe_path);

  // ESTABLISH CONNECTION WITH SERVER
  server_fd = open(SERVER_FIFO, O_WRONLY);
  if ( server_fd == -1) {
    fprintf(stderr, "Failed to open FIFO: %s\n", SERVER_FIFO);
    return 1;
  }

  // ASK SERVER
  char buffer[3*MAX_PIPE_PATH_LENGTH + 1];
  char padding[MAX_PIPE_PATH_LENGTH];
  strcpy(buffer, "1"); // OPCODE
  fix_size(padding, req_pipe_path, MAX_PIPE_PATH_LENGTH);
  memcpy(buffer + 1, padding, MAX_PIPE_PATH_LENGTH); // REQUEST PATH
  fix_size(padding, resp_pipe_path, MAX_PIPE_PATH_LENGTH);
  memcpy(buffer + 1 + MAX_PIPE_PATH_LENGTH, padding, MAX_PIPE_PATH_LENGTH); // RESPONSE PATH
  fix_size(padding, notif_pipe_path, MAX_PIPE_PATH_LENGTH);
  memcpy(buffer + 1 + 2*MAX_PIPE_PATH_LENGTH, padding, MAX_PIPE_PATH_LENGTH); // NOTIFICATION PATH
  write(server_fd, buffer , 3*MAX_PIPE_PATH_LENGTH + 1);

  // WAIT FOR SERVER TO ALSO OPEN FIFO
  REQ_FD = open(req_pipe_path, O_WRONLY);
    if (REQ_FD == -1) {
    fprintf(stderr, "Failed to open FIFO: %s\n", req_pipe_path);
    return 1;
  }

  RESP_FD = open(resp_pipe_path, O_RDONLY);
  if (RESP_FD == -1) {
    fprintf(stderr, "Failed to open FIFO: %s\n", resp_pipe_path);
    return 1;
  }

  *notif_pipe = open(notif_pipe_path, O_RDONLY);
  NOTIF_FD = *notif_pipe;
  if (*notif_pipe == -1) {
    fprintf(stderr, "Failed to open FIFO: %s\n", notif_pipe_path);
    return 1;
  }

  char success[3];
  ssize_t bytes_read = 0;

  bytes_read = read(RESP_FD, success, 2);
  while (bytes_read == 0) {
    bytes_read = read(RESP_FD, success, 2);
  }
  success[bytes_read] = '\0';
  fprintf(stderr, "Server returned %c for operation: connect\n", success[1]);
  if (strcmp(success, "10") == 0) {
    return 0;
  }
  return 1;
}

int kvs_disconnect(void) {
  write(REQ_FD, "2", 1);

  char success[3];
  ssize_t bytes_read = 0;
  bytes_read = read(RESP_FD, success, 2);
  while (bytes_read == 0) {
    bytes_read = read(RESP_FD, success, 2);
  }
  success[bytes_read] = '\0';
  fprintf(stderr, "Server returned %c for operation: disconnect\n", success[1]);
  if (strcmp(success, "20") == 0) {
    // close pipes and unlink pipe files
    terminate();
    return 0;
  }

  return 1;
}

int kvs_subscribe(const char *key) {
  // send subscribe message to request pipe and wait for response in response
  // pipe
  char buffer[MAX_STRING_SIZE + 2];
  char padding[MAX_STRING_SIZE + 1];
  strcpy(buffer, "3"); // OPCODE

  fix_size(padding, key, MAX_STRING_SIZE + 1);
  memcpy(buffer + 1, padding, MAX_STRING_SIZE); // KEY
  write(REQ_FD, buffer , MAX_STRING_SIZE + 2);

  char success[3];
  ssize_t bytes_read = 0;

  bytes_read = read(RESP_FD, success, 2);
  while (bytes_read == 0) {
    bytes_read = read(RESP_FD, success, 2);
  }
  success[bytes_read] = '\0';
  fprintf(stderr, "Server returned %c for operation: subscribe\n", success[1]);
  if (strcmp(success, "31") == 0) {
    return 1;
  }

  return 0;
}

int kvs_unsubscribe(const char *key) {
  // send unsubscribe message to request pipe and wait for response in response
  // pipe
  char buffer[MAX_STRING_SIZE + 2];
  char padding[MAX_STRING_SIZE + 1];
  strcpy(buffer, "4"); // OPCODE

  fix_size(padding, key, MAX_STRING_SIZE + 1);
  memcpy(buffer + 1, padding, MAX_STRING_SIZE); // KEY
  write(REQ_FD, buffer , MAX_STRING_SIZE + 2);

  char success[3];
  ssize_t bytes_read = 0;

  bytes_read = read(RESP_FD, success, 2);
  while (bytes_read == 0) {
    fprintf(stderr, "STUCK IN LOOP UNTIL READ\n");
    bytes_read = read(RESP_FD, success, 2);
  }

  success[bytes_read] = '\0';
  fprintf(stderr, "Server returned %c for operation: unsubscribe\n", success[1]);
  if (strcmp(success, "40") == 0) {
    return 0;
  }
  return 1;
}

int kvs_ping() {
  char buffer[MAX_STRING_SIZE + 2];
  strcpy(buffer, "5"); // OPCODE
  write(REQ_FD, buffer , MAX_STRING_SIZE + 2);


  char success[3];
  ssize_t bytes_read = 0;
  bytes_read = read(RESP_FD, success, 2);
  while (bytes_read == 0) {
    fprintf(stderr, "STUCK IN LOOP UNTIL READ\n");
    bytes_read = read(RESP_FD, success, 2);
  }

  success[bytes_read] = '\0';
  fprintf(stderr, "Server returned %c for operation: ping\n", success[1]);
  if (strcmp(success, "50") == 0) {
    return 0;
  }
  return 1;
}


void terminate() {
  close(REQ_FD);
  close(RESP_FD);
  close(NOTIF_FD);
  unlink(REQ_PATH);
  unlink(RESP_PATH);
  unlink(NOTIF_PATH);
  return;
}
