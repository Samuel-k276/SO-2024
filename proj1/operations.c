#include "constants.h"
#include "kvs.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "auxiliar.h"

static struct HashTable *kvs_table = NULL;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

int kvs_init() {
  if (kvs_table != NULL) {
    fprintf(stderr, "KVS state has already been initialized\n");
    return EXIT_FAILURE;
  }
  kvs_table = create_hash_table();
  return kvs_table == NULL;
}

int kvs_terminate() {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return EXIT_FAILURE;
  }

  free_table(kvs_table);
  return EXIT_SUCCESS;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE],
  char values[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return EXIT_FAILURE;
  }
  for (size_t i = 0; i < num_pairs; i++) {
    if (write_pair(kvs_table, keys[i], values[i]) != 0) {
      fprintf(stderr, "Failed to write keypair (%s,%s)\n", keys[i], values[i]);
    }
  }
  return fd == -1;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return EXIT_FAILURE;
  }
  write(fd, "[", 1);
  for (size_t i = 0; i < num_pairs; i++) {
    char *result = read_pair(kvs_table, keys[i]);
    if (result == NULL) {
      write(fd, "(", 1);
      write(fd, keys[i], strlen(keys[i]));
      write(fd, ",KVSERROR)", 10);
    } else {
      write(fd, "(", 1);
      write(fd, keys[i], strlen(keys[i]));
      write(fd, ",", 1);
      write(fd, result, strlen(result));
      write(fd, ")", 1);
    }
    free(result);
  }
  write(fd, "]\n", 2);
  return EXIT_SUCCESS;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return EXIT_FAILURE;
  }
  int aux = 0;

  for (size_t i = 0; i < num_pairs; i++) {
    if (delete_pair(kvs_table, keys[i]) != 0) {
      if (!aux) {
        write(fd, "[", 1);
        aux = 1;
      }
      write(fd, "(", 1);
      write(fd, keys[i], strlen(keys[i]));
      write(fd, ",KVSMISSING)", 12);
    }
  }
  if (aux)
    write(fd, "]\n", 2);

  return EXIT_SUCCESS;
}

void kvs_show(int fd) {
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL) {
      write(fd, "(", 1);
      write(fd, keyNode->key, strlen(keyNode->key));
      write(fd, ", ", 2);
      write(fd, keyNode->value, strlen(keyNode->value));
      write(fd, ")\n", 2);
      keyNode = keyNode->next; // Move to the next node
    }
  }
}

int kvs_backup(char *filepath_out, int backup_counter) {
  char *backup_path = make_backup_path(filepath_out, (unsigned int)backup_counter);

  int bck = open(backup_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (bck == -1) {
    fprintf(stderr, "Failed to open backup file\n");
    return EXIT_FAILURE;
  }
  free(backup_path);
  kvs_show(bck);
  close(bck);
  return EXIT_SUCCESS; 
}

void kvs_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}