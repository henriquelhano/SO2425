#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>


#include "parser.h"
#include "operations.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"

sem_t SEM_BUFFER_SPACE;
sem_t SEM_BUFFER_CLIENTS;
pthread_mutex_t BUFFER_MUTEX;
//volatile sig_atomic_t sigur1_received = 0;

char BUFFER[MAX_SESSION_COUNT][1 + 3 * MAX_PIPE_PATH_LENGTH] = {0};
int BUFFER_READ_INDEX = 0;
int BUFFER_WRITE_INDEX = 0;


// Função para ordenar 
void insertionSort(char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE], size_t n) {
    char keyTemp[MAX_STRING_SIZE];
    char valueTemp[MAX_STRING_SIZE];

    for (size_t i = 1; i < n; i++) {
        // Armazena o elemento atual em variáveis temporárias
        strcpy(keyTemp, keys[i]);
        strcpy(valueTemp, values[i]);

        size_t j = i;
        // Move elementos maiores para frente até encontrar a posição correta
        while (j > 0 && strcmp(keys[j - 1], keyTemp) > 0) {
            strcpy(keys[j], keys[j - 1]);
            strcpy(values[j], values[j - 1]);
            j--;
        }

        // Insere o elemento na posição correta
        strcpy(keys[j], keyTemp);
        strcpy(values[j], valueTemp);
    }
}

// Função para processar um ficheiro .job e executar os comandos
void process_job_file(const char *input_path, const char *output_path, char *job_name, ThreadData *data) {
    int max_backups = data->max_backups;
    const char *output_dir = data->output_dir;

    // Abre o ficheiro .job
    int input_fd = open(input_path, O_RDONLY);
    if (input_fd < 0) {
        close(input_fd);
        return;
    }

    // Abre ou cria o ficheiro .out
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

    // Loop principal para processar comandos do ficheiro
    for (;;) {
      switch (get_next(input_fd)) {
          case CMD_WRITE:
              // Processa comando WRITE
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
              // Processa comando READ
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
              // Processa comando DELETE
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
              // Mostra os pares chave-valor guardados
              kvs_show(output_fd, data, 0);
              break;

          case CMD_WAIT:
              // Adiciona um atraso com base no comando WAIT
              if (parse_wait(input_fd, &delay, NULL) == -1) {
                  fprintf(stderr, "Invalid command. See HELP for usage\n");
                  continue;
              }
              if (delay > 0) {
                  kvs_wait(delay);
              }
              break;

          case CMD_BACKUP:
              // Cria um backup do armazenamento
              pthread_mutex_lock(&data->backup_mutex);
              if (data->active_backups >= max_backups) {
                  if (wait(NULL) > 0) {
                      data->active_backups--;
                  }
              }
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
              // Finaliza o processamento do ficheiro
              close(input_fd);
              close(output_fd);
              return;
      }
    }
}

// Função para processar ficheiros em threads
void *thread_process_files(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    for (;;) {
        pthread_mutex_lock(&data->file_mutex);

        // Verifica se ainda há ficheiros para processar
        if (data->current_index >= data->count) {
            pthread_mutex_unlock(&data->file_mutex);
            break;
        }

        // Obtém o próximo ficheiro e incrementa o índice
        const char *input_file = data->files[data->current_index++];
        pthread_mutex_unlock(&data->file_mutex);

        // Gera o caminho do ficheiro de saída
        char output_file[MAX_JOB_FILE_NAME_SIZE];
        snprintf(output_file, MAX_JOB_FILE_NAME_SIZE, "%.*s.out", (int)(strlen(input_file) - 4), input_file);

        // Processa o ficheiro
        process_job_file(input_file, output_file, strrchr(input_file, '/') + 1, data);
    }

    return NULL;
}

// Função para processar um diretório de ficheiros .job
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
    // Lê ficheiros .job do diretório
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


/*void handle_sigusr1(int sig){
  printf("Received SIGUSR1 in Handle\n");
  sigusr1_received = 1;
}
*/

void *host_FIFO(void *arg) {
  char *register_FIFO_name = (char *)arg;

  // Remove o pipe, se existir
  if (unlink(register_FIFO_name) != 0 && errno != ENOENT) {
    perror("unlink(%s) failed");
    exit(EXIT_FAILURE);
  }

  // Cria o pipe
  if (mkfifo(register_FIFO_name, 0777) != 0) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }

  int fifo_fd = open(register_FIFO_name, O_RDWR);
  if (fifo_fd == -1) {
    perror("Failed to open FIFO");
    exit(EXIT_FAILURE);
  }

  // Lê do FIFO
  //OP_CODE=1 | nome do pipe do cliente (para pedidos) | nome do pipe do cliente (para respostas) | nome do pipe do cliente (para notificações)
  while(1) {

    sem_wait(&SEM_BUFFER_SPACE);
    
    // Escreve no buffer
    // Não é necessário lock, visto que a thread anfitriã é a única a alterar o indice
    ssize_t bytes_read = read(fifo_fd, BUFFER[BUFFER_WRITE_INDEX], 1 + 3 * MAX_PIPE_PATH_LENGTH);
    
    if (bytes_read == 0) {
      // bytes_read == 0 indica EOF
      fprintf(stderr, "pipe closed\n");
      return NULL;
    } else if (bytes_read == -1) {
      // bytes_read == -1 indica erro
      fprintf(stderr, "read failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
    BUFFER_WRITE_INDEX = (BUFFER_WRITE_INDEX + 1) % MAX_SESSION_COUNT;
    sem_post(&SEM_BUFFER_CLIENTS);
        
  }
  
  close(fifo_fd);
  unlink(register_FIFO_name);
}

void *thread_manage_session(void *arg) {
  ThreadData *data = (ThreadData *)arg;

  char client_paths[1 + 3 * MAX_PIPE_PATH_LENGTH];
  while (1) {
    sem_wait(&SEM_BUFFER_CLIENTS);
    pthread_mutex_lock(&BUFFER_MUTEX);
    
    // Lê do buffer global
    // Lock necessário, visto que todas as threads gestoras podem alterar o indice
    strncpy(client_paths, BUFFER[BUFFER_READ_INDEX], (1 + 3 * MAX_PIPE_PATH_LENGTH));

    BUFFER_READ_INDEX = (BUFFER_READ_INDEX + 1) % MAX_SESSION_COUNT;
    pthread_mutex_unlock(&BUFFER_MUTEX);
    sem_post(&SEM_BUFFER_SPACE);

    // OP_CODE=1 | nome do pipe do cliente (para pedidos) | nome do pipe do cliente (para respostas) | nome do pipe do cliente (para notificações)
    int op_code_1;
    char req_pipe_path[MAX_PIPE_PATH_LENGTH];
    char resp_pipe_path[MAX_PIPE_PATH_LENGTH];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH];
    
    sscanf(client_paths, "%d|%[^|]|%[^|]|%s", &op_code_1, req_pipe_path, resp_pipe_path, notif_pipe_path);
    
    // importante: abrir os pipes pela ordem que abres no cliente em api.c com a flag contrária
    int resp_fd = open(resp_pipe_path, O_WRONLY);
    if(resp_fd == -1) {
      perror("Failed to open FIFO");
      exit(EXIT_FAILURE);
    }
    int req_fd = open(req_pipe_path, O_RDONLY);
    if(req_fd == -1) {
      perror("Failed to open FIFO");
      exit(EXIT_FAILURE);
    }
    int notif_fd = open(notif_pipe_path, O_WRONLY);
    if(notif_fd == -1) {
      perror("Failed to open FIFO");
      exit(EXIT_FAILURE);
    }

    send_msg(resp_fd, "10");

    char request[42] = {0};
    while(1) {
      // Ler do pipe de pedidos
      ssize_t bytes_read = read(req_fd, request, sizeof(request));
      if (bytes_read == 0) {
        // bytes_read == 0 indica EOF
        fprintf(stderr, "pipe closed\n");
        break; // FIXME is it supposed to return?
      } else if (bytes_read == -1) {
        // bytes_read == -1 indica erro
        fprintf(stderr, "read failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE); // FIXME is it supposed to return?
      }
    
      //parse request
      char op_code = request[0];
      char key[41];

      char response[2];
      response[0] = op_code;

      int result;
      switch (op_code) {
        case OP_CODE_DISCONNECT: 
          // OP_CODE=2
          //removerTodasSubscricoes(key,data);
          //response[1] = (char) result+'0';
          send_msg(notif_fd, response);

          break;
          
        case OP_CODE_SUBSCRIBE:
          // OP_CODE=3 | key
          strncpy(key, request + 1, 41);
          result = kvs_subscribe(key, notif_fd, data);
          response[1] = (char) result+'0';
          send_msg(notif_fd, response);

          break;
          
        case OP_CODE_UNSUBSCRIBE:
          // OP_CODE=4 | key
          strncpy(key, request + 1, 41);
          result = kvs_unsubscribe(key, notif_fd, data);
          response[1] = (char) result+'0';
          send_msg(notif_fd, response);

          break;

        default:
          fprintf(stderr, "Invalid command, op_code unrecognized\n");
          break;
      }

      // exits inner while loop
      if (op_code == 2) {
        break;
      }
      
    }
    close(resp_fd);
    close(req_fd);
    close(notif_fd);
  }
}
/*
void removerTodasSubscricoes(const char *key, ThreadData data) { //Após disconnect, remove todas as subscrições
    int index = hash(key);
    KeyNode *keyNode = ht->table[index];

    while (keyNode != NULL) {
        for (int i = 0; i < MAX_SESSION_COUNT; i++) {
          kvs_unsubscribe(key,i,data);
        }
        keyNode = keyNode->next; // Move to the next node
    }
    return 1; // Failed
}*/

int main(int argc, char *argv[]) {
  if (argc < 5) {
    fprintf(stderr, "Usage: %s <jobs_dir> <max_backups> <max_threads> <register_FIFO_name>\n", argv[0]);
    return 1;
  }
  if (kvs_init()) {
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  int max_backups = atoi(argv[2]);
  int max_threads = atoi(argv[3]);
  char *register_FIFO_name = argv[4];

  ThreadData data;

  // Inicialização de mutexes e rwlocks
  pthread_mutex_init(&data.file_mutex, NULL);
  pthread_rwlock_init(&data.rwlock, NULL);
  pthread_mutex_init(&data.backup_mutex, NULL);
  for (int i = 0; i < 26; i++) {
    pthread_rwlock_init(&data.rwlock_array[i], NULL);
  }

  // Thread para gerenciar o FIFO de registo
  pthread_t register_thread;
  pthread_mutex_init(&BUFFER_MUTEX, NULL);
  pthread_create(&register_thread, NULL, host_FIFO, register_FIFO_name);

  sem_init(&SEM_BUFFER_SPACE, 0, MAX_SESSION_COUNT);
  sem_init(&SEM_BUFFER_CLIENTS, 0, 0);

  pthread_t manager_threads[MAX_SESSION_COUNT];
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    pthread_create(&manager_threads[i], NULL, thread_manage_session, &data);
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
  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    pthread_join(manager_threads[i], NULL);
  }

  // Destruir mutexes e rwlocks
  pthread_mutex_destroy(&data.file_mutex);
  pthread_rwlock_destroy(&data.rwlock);
  pthread_mutex_destroy(&data.backup_mutex);
  for (int i = 0; i < 26; i++) {
    pthread_rwlock_destroy(&data.rwlock_array[i]);
  }

  // Encerra os semáforos
    if (sem_destroy(&SEM_BUFFER_SPACE) != 0) {
        perror("Failed to destroy SEM_BUFFER_SPACE");
    }

    if (sem_destroy(&SEM_BUFFER_CLIENTS) != 0) {
        perror("Failed to destroy SEM_BUFFER_CLIENTS");
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
