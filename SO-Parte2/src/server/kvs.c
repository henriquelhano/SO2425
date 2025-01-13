#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "kvs.h"
#include "string.h"




/**
 * helper function to send messages
 * retries to send whatever was not sent in the beginning
 */
void send_msg(int fd, char const *str) {
  size_t len = strlen(str);
  size_t written = 0;

  while (written < len) {
      ssize_t ret = write(fd, str + written, len - written);
      if (ret < 0) {
          perror("Write failed");
          exit(EXIT_FAILURE);
      }

      written += (size_t)ret;
  }
}

int notify_clients(HashTable *ht, const char *key) {
    int index = hash(key);
    KeyNode *keyNode = ht->table[index];
    char notification[2*MAX_STRING_SIZE];

    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            for (int i = 0; i < MAX_SESSION_COUNT; i++) {
                strncpy(notification, key, MAX_STRING_SIZE);
                strncpy(notification + MAX_STRING_SIZE, keyNode->value, MAX_STRING_SIZE);
                send_msg(keyNode->subscriber_fds[i], notification);
            }
            return 0;
        }
        keyNode = keyNode->next; // Move to the next node
    }
    return 1; // Key not found
}

int add_subscriber(HashTable *ht, const char *key, int subscriber_fd) {
    int index = hash(key);
    KeyNode *keyNode = ht->table[index];

    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            for (int i = 0; i < MAX_SESSION_COUNT; i++) {
                if (keyNode->subscriber_fds[i] == subscriber_fd) {
                    return 1; // Subscriber already exists
                }
            }
            for (int i = 0; i < MAX_SESSION_COUNT; i++) {
                if (keyNode->subscriber_fds[i] == 0) {
                    keyNode->subscriber_fds[i] = subscriber_fd;
                    return 0; // Subscriber added
                }
            }
            return 1; // No space for new subscriber
        }
        keyNode = keyNode->next; // Move to the next node
    }
    return 1; // Key not found 
}

int remove_subscriber(HashTable *ht, const char *key, int subscriber_fd) {
    int index = hash(key);
    KeyNode *keyNode = ht->table[index];

    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            for (int i = 0; i < MAX_SESSION_COUNT; i++) {
                if (keyNode->subscriber_fds[i] == subscriber_fd) {
                    keyNode->subscriber_fds[i] = 0;
                    return 0; // Subscriber removed
                }
            }
            return 1; // Subscriber not found
        }
        keyNode = keyNode->next; // Move to the next node
    }
    return 1; // Key not found
}

int hash(const char *key) {
    int firstLetter = tolower(key[0]); 
    if (firstLetter >= 'a' && firstLetter <= 'z') {
        return firstLetter - 'a'; 
    } else if (firstLetter >= '0' && firstLetter <= '9') {
        return firstLetter - '0'; 
    }
    return -1; 
}

// Cria uma nova tabela hash.
struct HashTable* create_hash_table() {
    HashTable *ht = malloc(sizeof(HashTable)); 
    if (!ht) return NULL; 
    for (int i = 0; i < TABLE_SIZE; i++) {
        ht->table[i] = NULL; // Inicializa todas as posições com NULL
    }
    return ht;
}

int write_pair(HashTable *ht, const char *key, const char *value) {
    int index = hash(key); 
    KeyNode *keyNode = ht->table[index];

    // Procura o par da chave
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            free(keyNode->value); // Liberta o valor antigo
            keyNode->value = strdup(value); // Atualiza o valor
            return 0;
        }
        keyNode = keyNode->next; // Move para o próximo par
    }

    // Chave não encontrada, cria um novo par
    keyNode = malloc(sizeof(KeyNode));
    keyNode->key = strdup(key); // Aloca memória e copia a chave
    keyNode->value = strdup(value); // Aloca memória e copia o valor
    keyNode->next = ht->table[index]; // Encadeia com os pares existentes
    ht->table[index] = keyNode; // Adiciona o novo par no início da lista
    return 0;
}

/// Lê o valor associado a uma chave na tabela hash.
char* read_pair(HashTable *ht, const char *key) {
    int index = hash(key); 
    KeyNode *keyNode = ht->table[index];

    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            return strdup(keyNode->value); 
        }
        keyNode = keyNode->next; 
    }
    return NULL;
}

/// Remove um par chave-valor da tabela hash.
int delete_pair(HashTable *ht, const char *key) {
    int index = hash(key); 
    KeyNode *keyNode = ht->table[index];
    KeyNode *prevNode = NULL;

    // Procura o par correspondente à chave
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            if (prevNode == NULL) {
                ht->table[index] = keyNode->next; // Remove o primeiro par da lista
            } else {
                prevNode->next = keyNode->next; // Remove o par do meio/fim da lista
            }
            free(keyNode->key); // Liberta a memória da chave
            free(keyNode->value); // Liberta a memória do valor
            free(keyNode); // Liberta o par
            return 0; // Sucesso
        }
        prevNode = keyNode; // Atualiza o par anterior
        keyNode = keyNode->next; // Move para o próximo par
    }
    return 1; // Chave não encontrada
}

/// Liberta toda a memória alocada pela tabela hash.
void free_table(HashTable *ht) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = ht->table[i];
        while (keyNode != NULL) {
            KeyNode *temp = keyNode; // Salva o par atual
            keyNode = keyNode->next; // Move para o próximo par
            free(temp->key); // Liberta a memória da chave
            free(temp->value); // Liberta a memória do valor
            free(temp); // Liberta o par
        }
    }
    free(ht); // Liberta a tabela hash
}
