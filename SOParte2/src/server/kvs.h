#ifndef KEY_VALUE_STORE_H
#define KEY_VALUE_STORE_H

#define TABLE_SIZE 26

#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "kvs.h"
#include "string.h"


#include "constants.h"
#include "src/common/constants.h"

typedef struct {
    char files[MAX_WRITE_SIZE][MAX_JOB_FILE_NAME_SIZE];// Lista de ficheiros .job
    int count;                                         // Número total de ficheiros
    int current_index;                                 // Índice do próximo ficheiro a processar
    pthread_mutex_t file_mutex;                        // Mutex para proteger o acesso ao índice
    const char *output_dir;                            // Diretório de saída
    int max_backups;                                   // Número máximo de backups
    pthread_rwlock_t rwlock;                           // Rwlock global para o proteger o comando show
    pthread_rwlock_t rwlock_array[26];                 // Rwlocks para cada letra do alfabeto
    int active_backups;                                // Backups em atividade
    pthread_mutex_t backup_mutex;                      // Protege o acesso ao número de backups ativos
} ThreadData;

typedef struct KeyNode {
    char *key;
    char *value;
    int subscriber_fds[MAX_SESSION_COUNT];
    struct KeyNode *next;
} KeyNode;

typedef struct HashTable {
    KeyNode *table[TABLE_SIZE];
} HashTable;

void send_msg(int fd, char const *str);
int notify_clients(HashTable *ht, const char *key);
int add_subscriber(HashTable *ht, const char *key, int subscriber_fd);
int remove_subscriber(HashTable *ht, const char *key, int subscriber_fd);



/// Hash function based on key initial.
/// @param key Lowercase alphabetical string.
/// @return hash.
/// NOTE: This is not an ideal hash function, but is useful for test purposes of the project
int hash(const char *key);

/// Creates a new event hash table.
/// @return Newly created hash table, NULL on failure
struct HashTable *create_hash_table();

/// Appends a new key value pair to the hash table.
/// @param ht Hash table to be modified.
/// @param key Key of the pair to be written.
/// @param value Value of the pair to be written.
/// @return 0 if the node was appended successfully, 1 otherwise.
int write_pair(HashTable *ht, const char *key, const char *value);

/// Deletes the value of given key.
/// @param ht Hash table to delete from.
/// @param key Key of the pair to be deleted.
/// @return 0 if the node was deleted successfully, 1 otherwise.
char* read_pair(HashTable *ht, const char *key);

/// Appends a new node to the list.
/// @param list Event list to be modified.
/// @param key Key of the pair to read.
/// @return 0 if the node was appended successfully, 1 otherwise.
int delete_pair(HashTable *ht, const char *key);

/// Frees the hashtable.
/// @param ht Hash table to be deleted.
void free_table(HashTable *ht);


#endif  // KVS_H

