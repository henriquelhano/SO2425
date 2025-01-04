#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>

#include "parser.h"
#include "operations.h"

void insertionSort(char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE], size_t n) {
    char keyTemp[MAX_STRING_SIZE];
    char valueTemp[MAX_STRING_SIZE];

    for (size_t i = 1; i < n; i++) {
        // Armazena o elemento atual em uma variável temporária
        strcpy(keyTemp, keys[i]);
        strcpy(valueTemp, values[i]);

        size_t j = i; // Usa size_t para manter compatibilidade
        while (j > 0 && strcmp(keys[j - 1], keyTemp) > 0) {
            // Move os elementos maiores para frente
            strcpy(keys[j], keys[j - 1]);
            strcpy(values[j], values[j - 1]);
            j--;
        }

        // Insere o elemento na posição correta
        strcpy(keys[j], keyTemp);
        strcpy(values[j], valueTemp);
    }
}


void process_job_file(const char *input_path, const char *output_path, char *job_name, ThreadData *data) {
  int max_backups = data->max_backups;
  const char *output_dir = data->output_dir;

  int input_fd = open(input_path, O_RDONLY);
  if (input_fd < 0) {
    close(input_fd);
    return;
  }
  int output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (output_fd < 0) {
    close(output_fd);
    return;
  }

  char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
  char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
  unsigned int delay;
  size_t num_pairs;
  int total_backups = 0;

  while (true) {

  switch (get_next(input_fd)) {
    case CMD_WRITE:
      num_pairs = parse_write(input_fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }
      insertionSort(keys, values, num_pairs);
      if (kvs_write(num_pairs, keys, values, data)) {
        fprintf(stderr, "Failed to write pair\n");
      }
      break;

    case CMD_READ:
      num_pairs = parse_read_delete(input_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }
      insertionSort(keys, values, num_pairs);
      if (kvs_read(num_pairs, keys, output_fd, data)) {
        fprintf(stderr, "Failed to read pair\n");
      }
      break;

    case CMD_DELETE:
      num_pairs = parse_read_delete(input_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }
      insertionSort(keys, values, num_pairs);
      if (kvs_delete(num_pairs, keys, output_fd, data)) {
        fprintf(stderr, "Failed to delete pair\n");
      }
      break;

    case CMD_SHOW:
      kvs_show(output_fd, data);
      break;

    case CMD_WAIT:
      if (parse_wait(input_fd, &delay, NULL) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay > 0) {
        kvs_wait(delay);
      }
      break;

    case CMD_BACKUP:
      pthread_mutex_lock(&data->backup_mutex);
      if (data->active_backups >= max_backups) {
        if (wait(NULL) > 0) {
          data->active_backups--;
        }
      }
      // Dar lock para o fork não ficar corrompido
      for (int i = 0; i < TABLE_SIZE; i++) {
        pthread_rwlock_rdlock(&data->rwlock_array[i]);
      }
      pid_t pid = fork();
      if (pid == 0) { // Processo filho
        total_backups++;
        kvs_backup(total_backups, output_dir, job_name);
        kvs_terminate();
        exit(0);
      } else { // Processo pai  
        total_backups++;
        data->active_backups++;
        for (int i = 0; i < TABLE_SIZE; i++) {
          pthread_rwlock_unlock(&data->rwlock_array[i]);
        }
        pthread_mutex_unlock(&data->backup_mutex);
      }

      break;

    case CMD_INVALID:
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_HELP:
      printf( 
          "Available commands:\n"
          "  WRITE [(key,value),(key2,value2),...]\n"
          "  READ [key,key2,...]\n"
          "  DELETE [key,key2,...]\n"
          "  SHOW\n"
          "  WAIT <delay_ms>\n"
          "  BACKUP\n"
          "  HELP\n"
      );

      break;
      
    case CMD_EMPTY:
      break;

    case EOC:
      close(input_fd);
      close(output_fd);
      return;
    }
  }
}

void *thread_process_files(void *arg) {
  ThreadData *data = (ThreadData *)arg;

  while (true) {
    pthread_mutex_lock(&data->file_mutex);

    // Verifica se ainda há arquivos para processar
    if (data->current_index >= data->count) {
      pthread_mutex_unlock(&data->file_mutex);
      break;
    }

    // Obtém o próximo arquivo e incrementa o índice
    const char *input_file = data->files[data->current_index++];
    pthread_mutex_unlock(&data->file_mutex);

    // Gera o caminho de saída
    char output_file[MAX_JOB_FILE_NAME_SIZE];
    snprintf(output_file, MAX_JOB_FILE_NAME_SIZE, "%.*s.out", (int)(strlen(input_file) - 4), input_file);
    // Processa o arquivo
    process_job_file(input_file, output_file, strrchr(input_file, '/') + 1, data);
  }

  return NULL;
}

void process_job_directory(const char *input_dir, const char *output_dir, int max_backups, ThreadData *data) {
  DIR *dir = opendir(input_dir);
  if (!dir) {
      fprintf(stderr, "Failed to open directory: %s\n", input_dir);
      return;
  }

  data->count = 0;
  data->current_index = 0;
  data->output_dir = output_dir;
  data->max_backups = max_backups;
  data->active_backups = 0;

  struct dirent *entry;
  // Ler os arquivos .job do diretório e guardar os paths em data->files
  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".job")) {
      int len = snprintf(data->files[data->count], MAX_JOB_FILE_NAME_SIZE, "%s/%s", input_dir, entry->d_name);
      if (len < 0 || len >= MAX_JOB_FILE_NAME_SIZE) {
        fprintf(stderr, "Warning: Truncated filename for '%s/%s'.\n", input_dir, entry->d_name);
      } else {
        data->count++;
      }
    }
  }
  closedir(dir);
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <input_dir> <output_dir>\n", argv[0]);
    return 1;
  }
  if (kvs_init()) {
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }


  int max_backups = atoi(argv[2]);
  int max_threads = atoi(argv[3]);

  ThreadData data;
  
  // Inicialização de mutexes e rwlocks
  pthread_mutex_init(&data.file_mutex, NULL);
  pthread_rwlock_init(&data.rwlock, NULL);
  pthread_mutex_init(&data.backup_mutex, NULL);
  
  for (int i = 0; i < 26; i++) {
    pthread_rwlock_init(&data.rwlock_array[i], NULL);
  }

  process_job_directory(argv[1], argv[1], max_backups, &data);

  pthread_t threads[max_threads];

  // Criação de threads
  for (int i = 0; i < max_threads; i++) {
    pthread_create(&threads[i], NULL, thread_process_files, &data);
  }

  // Aguardar as threads finalizarem
  for (int i = 0; i < max_threads; i++) {
    pthread_join(threads[i], NULL);
  }

  pthread_mutex_destroy(&data.file_mutex);
  pthread_rwlock_destroy(&data.rwlock);
  pthread_mutex_destroy(&data.backup_mutex);
  
  for (int i = 0; i < 26; i++) {
    pthread_rwlock_destroy(&data.rwlock_array[i]);
  }

  // Aguardar os processos filhos finalizarem
  while (data.active_backups > 0) {
    if (wait(NULL) > 0) {
      data.active_backups--;
    } 
  }
  kvs_terminate();
  return 0;
}
