#include "kvs.h"

#include <ctype.h>
#include <stdlib.h>

#include "string.h"
#include <stdio.h> //////////////////////////////////
// Hash function based on key initial.
// @param key Lowercase alphabetical string.
// @return hash.
// NOTE: This is not an ideal hash function, but is useful for test purposes of
// the project
int hash(const char *key) {
  int firstLetter = tolower(key[0]);
  if (firstLetter >= 'a' && firstLetter <= 'z') {
    return firstLetter - 'a';
  } else if (firstLetter >= '0' && firstLetter <= '9') {
    return firstLetter - '0';
  }
  return -1; // Invalid index for non-alphabetic or number strings
}

struct HashTable *create_hash_table() {
  HashTable *ht = malloc(sizeof(HashTable));
  if (!ht)
    return NULL;
  for (int i = 0; i < TABLE_SIZE; i++) {
    ht->table[i] = NULL;
    pthread_mutex_init(&ht->wdr_lock[i], NULL);
  }
  return ht;
}

void lock_pair(HashTable *ht, char *key) {
  pthread_mutex_lock(&ht->wdr_lock[hash(key)]);
}

void unlock_pair(HashTable *ht, char *key) {
  pthread_mutex_unlock(&ht->wdr_lock[hash(key)]);
}

int write_pair(HashTable *ht, const char *key, const char *value) {
  int index = hash(key);

  // Search for the key node
  KeyNode *keyNode = ht->table[index];
  KeyNode *previousNode;

  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      // overwrite value
      free(keyNode->value);
      keyNode->value = strdup(value);
      notify(keyNode, NOTIFICATION_CHANGED);
      return 0;
    }
    previousNode = keyNode;
    keyNode = previousNode->next; // Move to the next node
  }
  // Key not found, create a new key node
  keyNode = malloc(sizeof(KeyNode));
  keyNode->key = strdup(key);        // Allocate memory for the key
  keyNode->value = strdup(value);    // Allocate memory for the value
  keyNode->subscription_list = NULL; // Initialize the subscription list
  pthread_mutex_init(&keyNode->sub_lock,
                     NULL);         // Initialize the subscription list lock
  keyNode->next = ht->table[index]; // Link to existing nodes
  ht->table[index] = keyNode; // Place new key node at the start of the list
  return 0;
}

char *read_pair(HashTable *ht, const char *key) {
  int index = hash(key);

  KeyNode *keyNode = ht->table[index];
  KeyNode *previousNode;
  char *value;

  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      value = strdup(keyNode->value);
      return value; // Return the value if found
    }
    previousNode = keyNode;
    keyNode = previousNode->next; // Move to the next node
  }
  return NULL; // Key not found
}

int delete_pair(HashTable *ht, const char *key) {
  int index = hash(key);

  // Search for the key node
  KeyNode *keyNode = ht->table[index];
  KeyNode *prevNode = NULL;

  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      // Key found; delete this node
      if (prevNode == NULL) {
        // Node to delete is the first node in the list
        ht->table[index] = keyNode->next; // Update the table to point to the next node
      } else {
        // Node to delete is not the first; bypass it
        prevNode->next = keyNode->next; // Link the previous node to the next node
      }
      // Notify subscribers
      notify(keyNode, NOTIFICATION_DELETED);
      // Free the memory allocated for the key and value
      free(keyNode->key);
      free(keyNode->value);
      free(keyNode); // Free the key node itself
      return 0;      // Exit the function
    }
    prevNode = keyNode;      // Move prevNode to current node
    keyNode = keyNode->next; // Move to the next node
  }
  return 1;
}

void free_table(HashTable *ht) {
  for (int i = 0; i < TABLE_SIZE; i++) {
    pthread_mutex_destroy(&ht->wdr_lock[i]);
    KeyNode *keyNode = ht->table[i];
    while (keyNode != NULL) {
      KeyNode *temp = keyNode;
      keyNode = keyNode->next;
      free(temp->key);
      free(temp->value);
      free_subscription_list(temp->subscription_list);
      pthread_mutex_destroy(&keyNode->sub_lock);
      free(temp);
    }
  }

  free(ht);
}

int subscribe(HashTable *ht, const char *key, const char *pipe_path,
              int notification_pipe_fd) {
  int index = hash(key);

  KeyNode *keyNode = ht->table[index];
  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      pthread_mutex_lock(&keyNode->sub_lock);
      // Verificar se já se encontra subscrito
      if (keyNode->subscription_list != NULL) {
        SubscriptionNode *subNode = keyNode->subscription_list->head;
        while (subNode != NULL) {
          if (strcmp(subNode->pipe_path, pipe_path) == 0) {
            pthread_mutex_unlock(&keyNode->sub_lock);
            return 1; // Already subscribed still sucess
          }
          subNode = subNode->next;
        }
      }
      SubscriptionNode *subNode = malloc(sizeof(SubscriptionNode));
      strcpy(subNode->pipe_path, pipe_path);
      subNode->notification_pipe_fd = notification_pipe_fd;
      if (keyNode->subscription_list == NULL) {
        keyNode->subscription_list = malloc(sizeof(SubscriptionList));
      } else {
        subNode->next = keyNode->subscription_list->head;
      }
      keyNode->subscription_list->head = subNode;
      pthread_mutex_unlock(&keyNode->sub_lock);
      return 1;
    }
    keyNode = keyNode->next;
  }
  return 0;
}

int unsubscribe(HashTable *ht, const char *key, const char *pipe_path) {
  int index = hash(key);

  KeyNode *keyNode = ht->table[index];
  while (keyNode != NULL) {
    if (strcmp(keyNode->key, key) == 0) {
      pthread_mutex_lock(&keyNode->sub_lock);
      if (keyNode->subscription_list == NULL) {
        pthread_mutex_unlock(&keyNode->sub_lock);
        return 1;
      }
      SubscriptionNode *subNode = keyNode->subscription_list->head;
      SubscriptionNode *prevNode = NULL;
      while (subNode != NULL) {
        if (strcmp(subNode->pipe_path, pipe_path) == 0) {
          if (prevNode == NULL) {
            keyNode->subscription_list->head = subNode->next;
          } else {
            prevNode->next = subNode->next;
          }
          free(subNode);
          pthread_mutex_unlock(&keyNode->sub_lock);
          return 0;
        }
        prevNode = subNode;
        subNode = subNode->next;
      }
      pthread_mutex_unlock(&keyNode->sub_lock);
    }
    keyNode = keyNode->next;
  }
  return 1;
}

void unsubscribe_client(HashTable *ht, const char *pipe_path) {
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = ht->table[i];
    while (keyNode != NULL) {
      if (keyNode->subscription_list == NULL) {
        keyNode = keyNode->next;
        continue;
      }
      SubscriptionNode *subNode = keyNode->subscription_list->head;
      SubscriptionNode *prevNode = NULL;
      while (subNode != NULL) {
        if (strcmp(subNode->pipe_path, pipe_path) == 0) {
          if (prevNode == NULL) {
            keyNode->subscription_list->head = subNode->next;
          } else {
            prevNode->next = subNode->next;
          }
          free(subNode);
          break;
        } else {
          prevNode = subNode;
          subNode = subNode->next;
        }
      }
      keyNode = keyNode->next;
    }
  }
}

void unsubscribe_all(HashTable *ht) {
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = ht->table[i];
    while (keyNode != NULL) {
      if (keyNode->subscription_list == NULL) {
        keyNode = keyNode->next;
        continue;
      }
      SubscriptionNode *subNode = keyNode->subscription_list->head;
      while (subNode != NULL) {
        SubscriptionNode *temp = subNode;
        subNode = subNode->next;
        free(temp);
      }
      keyNode->subscription_list->head = NULL;
      keyNode = keyNode->next;
    }
  }
}

void notify(KeyNode *keyNode, int operation) {
  if (keyNode->subscription_list == NULL) {
    return;
  }
  SubscriptionNode *subNode = keyNode->subscription_list->head;
  pthread_mutex_lock(&keyNode->sub_lock);
  while (subNode != NULL) {
    int fd = subNode->notification_pipe_fd;
    if (operation == NOTIFICATION_CHANGED) {
      char key[MAX_STRING_SIZE + 1];
      char value[MAX_STRING_SIZE + 1];
      fix_size(key, keyNode->key, MAX_STRING_SIZE + 1);
      fix_size(value, keyNode->value, MAX_STRING_SIZE + 1);
      write(fd, key, MAX_STRING_SIZE + 1);
      write(fd, value, MAX_STRING_SIZE + 1);
    } else if (operation == NOTIFICATION_DELETED) {
      char key[MAX_STRING_SIZE + 1];
      char deleted[MAX_STRING_SIZE + 1];
      fix_size(key, keyNode->key, MAX_STRING_SIZE + 1);
      fix_size(deleted, "DELETED", MAX_STRING_SIZE + 1);
      write(fd, key, MAX_STRING_SIZE + 1);
      write(fd, deleted, MAX_STRING_SIZE + 1);
    }
    subNode = subNode->next;
  }
  pthread_mutex_unlock(&keyNode->sub_lock);
}

void free_subscription_list(SubscriptionList *subList) {
  if (subList == NULL) {
    return;
  }
  SubscriptionNode *subNode = subList->head;
  while (subNode != NULL) {
    SubscriptionNode *temp = subNode;
    subNode = subNode->next;
    free(temp);
  }
  free(subList);
}