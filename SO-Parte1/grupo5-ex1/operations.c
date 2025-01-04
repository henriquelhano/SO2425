#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kvs.h"
#include "constants.h"

#include <unistd.h>

static struct HashTable* kvs_table = NULL;


/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

int kvs_init() {
  if (kvs_table != NULL) {
    fprintf(stderr, "KVS state has already been initialized\n");
    return 1;
  }

  kvs_table = create_hash_table();
  return kvs_table == NULL;
}

int kvs_terminate() {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  free_table(kvs_table);
  return 0;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE]) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1;
    }

    for (size_t i = 0; i < num_pairs; i++) {
        if (write_pair(kvs_table, keys[i], values[i]) != 0) {
            fprintf(stderr, "Fail to write keypair (%s,%s)\n", keys[i], values[i]);
        }
    }

    return 0;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int output_fd) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1;
    }

    write(output_fd, "[", 1);
    for (size_t i = 0; i < num_pairs; i++) {
        char *result = read_pair(kvs_table, keys[i]);
        char buffer[MAX_STRING_SIZE];
        if (result == NULL) {
            sprintf(buffer, "(%s,KVSERROR)", keys[i]);
            write(output_fd, buffer, strlen(buffer));
        } else {
            sprintf(buffer, "(%s,%s)", keys[i], result);
            write(output_fd, buffer, strlen(buffer));
        }
        free(result);
    }
    write(output_fd, "]\n", 2);
    return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int output_fd) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1;
    }

    for (size_t i = 0; i < num_pairs; i++) {
        if (delete_pair(kvs_table, keys[i]) != 0) {
            char buffer[MAX_STRING_SIZE];
            sprintf(buffer, "[(%s,KVSMISSING)]\n", keys[i]);
            write(output_fd, buffer, strlen(buffer));
        }
    }

    return 0;
}

void kvs_show(int output_fd) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = kvs_table->table[i];
        
        while (keyNode != NULL) {
            int len = snprintf(NULL, 0, "(%s,%s)\n", keyNode->key, keyNode->value);
            char buffer[len + 1];
            snprintf(buffer, sizeof(buffer), "(%s,%s)\n", keyNode->key, keyNode->value);
            write(output_fd, buffer, strlen(buffer));
            keyNode = keyNode->next;
        }
    }
}

// fazer no exercicio 2
int kvs_backup() { 
  return 0;
}

void kvs_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}