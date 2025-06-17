#ifndef KEY_VALUE_STORE_H
#define KEY_VALUE_STORE_H
#define TABLE_SIZE 26

#include "io.h"
#include "src/common/constants.h"
#include "src/common/io.h"
#include <pthread.h>
#include <stddef.h>

typedef struct SubscriptionNode {
  char pipe_path[MAX_PIPE_PATH_LENGTH];
  int notification_pipe_fd;
  struct SubscriptionNode *next;
} SubscriptionNode;

typedef struct SubscriptionList {
  SubscriptionNode *head;
} SubscriptionList;

typedef struct KeyNode {
  char *key;
  char *value;
  SubscriptionList *subscription_list;
  pthread_mutex_t sub_lock; // Ainda nao implementado
  struct KeyNode *next;
} KeyNode;

typedef struct HashTable {
  KeyNode *table[TABLE_SIZE];
  pthread_mutex_t wdr_lock[TABLE_SIZE]; // WRITE/DELETE/READ LOCK
} HashTable;

enum {
  NOTIFICATION_CHANGED = 1,
  NOTIFICATION_DELETED = 2,
};

/// Creates a new KVS hash table.
/// @return Newly created hash table, NULL on failure
struct HashTable *create_hash_table();

void lock_pair(HashTable *ht, char *key);

void unlock_pair(HashTable *ht, char *key);

int hash(const char *key);
// Writes a key value pair in the hash table.
// @param ht The hash table.
// @param key The key.
// @param value The value.
// @return 0 if successful.
int write_pair(HashTable *ht, const char *key, const char *value);

// Reads the value of a given key.
// @param ht The hash table.
// @param key The key.
// return the value if found, NULL otherwise.
char *read_pair(HashTable *ht, const char *key);

/// Deletes a pair from the table.
/// @param ht Hash table to read from.
/// @param key Key of the pair to be deleted.
/// @return 0 if the node was deleted successfully, 1 otherwise.
int delete_pair(HashTable *ht, const char *key);

/// Frees the hashtable.
/// @param ht Hash table to be deleted.
void free_table(HashTable *ht);

/// Subscribes a pipe to a key.
/// @param ht Hash table to subscribe to.
/// @param key Key to subscribe to.
/// @param pipe_path Path to the pipe to subscribe.
/// @param notification_pipe_fd File descriptor of the pipe to notify.
/// @return 1 if the subscription was successful, 0 otherwise.
int subscribe(HashTable *ht, const char *key, const char *pipe_path,
              int notification_pipe_fd);

/// Unsubscribes a pipe from a key.
/// @param ht Hash table to unsubscribe from.
/// @param key Key to unsubscribe from.
/// @param pipe_path Path to the pipe to unsubscribe.
int unsubscribe(HashTable *ht, const char *key, const char *pipe_path);

/// Unsubscribes all pipes from all keys.
/// @param ht Hash table to unsubscribe from.
void unsubscribe_all(HashTable *ht);

/// Unsubscribes all pipes from a client.
/// @param ht Hash table to unsubscribe from.
/// @param pipe_path Path to the pipe to unsubscribe.
void unsubscribe_client(HashTable *ht, const char *pipe_path);

/// Notifies all subscribers of a key.
/// @param keyNode Key node to notify.
/// @param operation Notification operation.
void notify(KeyNode *keyNode, int operation);

/// Frees the subscription list of a key node.
/// @param list Subscription list to be freed.
void free_subscription_list(SubscriptionList *list);

#endif // KVS_H
