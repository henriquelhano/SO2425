#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "constants.h"
#include "parser.h"
#include "operations.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <time.h> 

void process_job_file(const char *input_path, const char *output_path, int max_backups, const char *output_dir, char *job_name) {
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
  int num_backups = 0;
  int active_backups = 0;

  while (1) {

  switch (get_next(input_fd)) {
    case CMD_WRITE:
      num_pairs = parse_write(input_fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_write(num_pairs, keys, values)) {
        fprintf(stderr, "Failed to write pair\n");
      }

      break;

    case CMD_READ:
      num_pairs = parse_read_delete(input_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_read(num_pairs, keys, output_fd)) {
        fprintf(stderr, "Failed to read pair\n");
      }
      break;

    case CMD_DELETE:
      num_pairs = parse_read_delete(input_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_delete(num_pairs, keys, output_fd)) {
        fprintf(stderr, "Failed to delete pair\n");
      }
      break;

    case CMD_SHOW:
      kvs_show(output_fd);
      break;

    case CMD_WAIT:
      if (parse_wait(input_fd, &delay, NULL) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay > 0) {
        //const char *message = "Waiting...\n";
        //write(output_fd, message, strlen(message)); // Escreve no arquivo .out
        kvs_wait(delay);
      }
      break;

    case CMD_BACKUP: // Comando implementado no exercicio 2
      if (active_backups >= max_backups) {
        /*if (wait(NULL) > 0) {
          active_backups--;
        }*/
      }
      pid_t pid = fork(); // Gerar processo(s) filho 
      if (pid == 0) { // O processo filho executa os backup-s
        num_backups++;
        kvs_backup(num_backups, output_dir, job_name);
        exit(0); //impede que o filho continue a trabalhar 
      } else { // Processo pai (Controla o número de backups)
        active_backups++;
        num_backups++;
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

void process_job_directory(const char *input_dir, const char *output_dir, int max_backups) {
  DIR *dir = opendir(input_dir);
  if (!dir) {
      fprintf(stderr, "Failed to open directory: %s\n", input_dir);
      return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
      // Verifica se o arquivo tem a extensão .job
      if (strstr(entry->d_name, ".job")) {
          char input_path[MAX_JOB_FILE_NAME_SIZE];
          char output_path[MAX_JOB_FILE_NAME_SIZE];

          // Cria o caminho completo para o arquivo .job
          snprintf(input_path, MAX_JOB_FILE_NAME_SIZE, "%s/%s", input_dir, entry->d_name);

          // Substitui a extensão .job por .out e muda para o diretório de saída
          snprintf(output_path, MAX_JOB_FILE_NAME_SIZE, "%s/%.*s.out", output_dir, (int)(strlen(entry->d_name) - 4), entry->d_name);

          process_job_file(input_path, output_path, max_backups, output_dir, entry->d_name);
      }
  }

  closedir(dir);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <input_dir> <output_dir>\n", argv[0]);
    return 1;
  }
  if (kvs_init()) {
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }
  clock_t start = clock();
  int max_backups = atoi(argv[2]);

  process_job_directory(argv[1], argv[1], max_backups);
  clock_t end = clock();
  double cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
  printf("Tempo de execução: %f segundos\n", cpu_time_used);

  kvs_terminate();
  return 0;
}
