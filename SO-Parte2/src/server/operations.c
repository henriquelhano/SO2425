#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "operations.h"
#include "constants.h"
#include "src/common/constants.h"

#include <unistd.h>
#include <fcntl.h>


static struct HashTable* kvs_table = NULL;

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


int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE], ThreadData *data) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state has already been initialized\n");
        return 1;
    }

    // Array para verificar quais índices da tabela hash estão bloqueados
    int hashed[26] = {0};
    // Bloqueio global para evitar alterações durante a escrita
    pthread_rwlock_rdlock(&data->rwlock);
    for (size_t i = 0; i < num_pairs; i++) {
        int index = hash(keys[i]);
        if (hashed[index] == 0) { // Bloqueia o índice somente se ainda não estiver bloqueado
            pthread_rwlock_wrlock(&data->rwlock_array[index]);
            hashed[index] = 1;
        }
    }

    // Adiciona os pares chave-valor à tabela hash
    for (size_t i = 0; i < num_pairs; i++) {
        if (write_pair(kvs_table, keys[i], values[i]) != 0) {
            fprintf(stderr, "Fail to write keypair (%s,%s)\n", keys[i], values[i]);
        }
    }

    // Liberta os bloqueios dos índices da tabela
    for (int i = 0; i < 26; i++) {
        if (hashed[i] == 1) {
            pthread_rwlock_unlock(&data->rwlock_array[i]);
            hashed[i] = 0;
        }
    }
    pthread_rwlock_unlock(&data->rwlock); // Liberta o bloqueio global

    return 0;
}

/// Lê múltiplos pares chave-valor da tabela hash.
/// Escreve os resultados no ficheiro especificado.
int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int output_fd, ThreadData *data) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1;
    }

    int hashed[26] = {0}; // Verifica quais índices estão bloqueados para leitura
    for (size_t i = 0; i < num_pairs; i++) {
        int index = hash(keys[i]);
        if (hashed[index] == 0) {
            pthread_rwlock_rdlock(&data->rwlock_array[index]);
            hashed[index] = 1;
        }
    }

    write(output_fd, "[", 1); 
    for (size_t i = 0; i < num_pairs; i++) {
        char *result = read_pair(kvs_table, keys[i]);
        char buffer[MAX_STRING_SIZE];
        if (result == NULL) {
            sprintf(buffer, "(%s,KVSERROR)", keys[i]); // Chave não encontrada
        } else {
            sprintf(buffer, "(%s,%s)", keys[i], result); // Chave encontrada
            free(result); // Liberta a memória alocada
        }
        write(output_fd, buffer, strlen(buffer));
    }
    write(output_fd, "]\n", 2); 

    for (int i = 0; i < 26; i++) {
        if (hashed[i] == 1) {
            pthread_rwlock_unlock(&data->rwlock_array[i]);
            hashed[i] = 0;
        }
    }

    return 0;
}

// Função que apaga pares chave-valor da tabela KVS
int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int output_fd, ThreadData *data) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1; 
    }

    // Array de controle para verificar posições de hash já processadas
    int hashed[TABLE_SIZE] = {0};
    // Lock de leitura para garantir consistência durante a verificação
    pthread_rwlock_rdlock(&data->rwlock);
    
    // Verifica a posição de hash de cada chave e aplica o lock de escrita, se necessário
    for (size_t i = 0; i < num_pairs; i++) {
        int position = hash(keys[i]);
        if (hashed[position] == 0) {
            pthread_rwlock_wrlock(&data->rwlock_array[position]);
            hashed[position] = 1;  // Marca a posição como bloqueada
        }
    }

    // Variável para controlar se há erro ao apagar chaves
    int has_error = 0;
    for (size_t i = 0; i < num_pairs; i++) {
        // Tenta apagar o par chave-valor
        if (delete_pair(kvs_table, keys[i]) != 0) {
            if (!has_error) {
                write(output_fd, "[", 1);  // Escreve o parêntese de abertura apenas uma vez
                has_error = 1;
            }
            // Se a chave não existir, escreve um erro no formato (key,KVSMISSING)
            char buffer[MAX_STRING_SIZE];
            sprintf(buffer, "(%s,KVSMISSING)", keys[i]);
            write(output_fd, buffer, strlen(buffer));
        }
    }

    // Fecha o parêntese de erro se houve falha ao apagar alguma chave
    if (has_error) {
        write(output_fd, "]\n", 2); 
    }

    // Liberta os locks das posições que foram processadas
    for (int i = 0; i < TABLE_SIZE; i++) {
        if (hashed[i] == 1) {
            pthread_rwlock_unlock(&data->rwlock_array[i]);
            hashed[i] = 0;  // Marca a posição como desbloqueada
        }
    }
    
    // Liberta o lock global após a operação de apagar pares chave-valor
    pthread_rwlock_unlock(&data->rwlock);

    return 0; 
}

// Função que exibe todos os pares chave-valor da tabela KVS
void kvs_show(int output_fd, ThreadData *data, int use_rwlock) {
    // Se necessário, aplica lock de escrita para garantir consistência
    if (use_rwlock) {
        pthread_rwlock_wrlock(&data->rwlock);
    }

    // Itera sobre todas as posições da tabela de hash e escreve os pares chave-valor
    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = kvs_table->table[i];
        while (keyNode != NULL) {
            // Formata cada par chave-valor e escreve no ficheiro de saída
            int len = snprintf(NULL, 0, "(%s, %s)\n", keyNode->key, keyNode->value);
            char buffer[len + 1];
            snprintf(buffer, sizeof(buffer), "(%s, %s)\n", keyNode->key, keyNode->value);
            write(output_fd, buffer, strlen(buffer));
            keyNode = keyNode->next;  // Avança para o próximo par na lista
        }
    }

    // Liberta o lock de escrita, se aplicado
    if (use_rwlock) {
        pthread_rwlock_unlock(&data->rwlock);
    }
}

// Função que realiza o backup da tabela KVS para um arquivo de backup
int kvs_backup(int backup, const char *output_dir, const char *job_name) {
    // Cria o caminho para o arquivo de backup baseado no diretório de saída e nome do .job
    char backup_output_path[MAX_JOB_FILE_NAME_SIZE];
    snprintf(backup_output_path, MAX_JOB_FILE_NAME_SIZE, "%s/%.*s-%d.bck", output_dir, (int)(strlen(job_name) - 4), job_name, backup);

    // Abre o arquivo de backup para escrita
    int backup_fd = open(backup_output_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (backup_fd == -1) {
        perror("Erro ao abrir o ficheiro de backup");
        return -1;  // Retorna erro se o arquivo não puder ser aberto
    }
    kvs_show(backup_fd, NULL, 0);
    close(backup_fd);

    return 0;
}

// Função que aguarda um atraso especificado (em milissegundos)
void kvs_wait(unsigned int delay_ms) {
    struct timespec delay = delay_to_timespec(delay_ms);
    nanosleep(&delay, NULL);  
}

int kvs_subscribe(const char *key, int fd, ThreadData *data) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1;
    }
    
    int index = hash(key);
    pthread_rwlock_wrlock(&data->rwlock_array[index]);

    int result = add_subscriber(kvs_table, key, fd);
    
    pthread_rwlock_unlock(&data->rwlock_array[index]);
    
    return result;
}

int kvs_unsubscribe(const char *key, int fd, ThreadData *data) {
    if (kvs_table == NULL) {
        fprintf(stderr, "KVS state must be initialized\n");
        return 1;
    }

    int index = hash(key);
    pthread_rwlock_wrlock(&data->rwlock_array[index]);

    int result = remove_subscriber(kvs_table, key, fd);

    pthread_rwlock_unlock(&data->rwlock_array[index]);

    return result;
}

