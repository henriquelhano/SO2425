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

void processar_ficheiro_job(const char *caminho_input, const char *output_path) {
  int input_fd = open(caminho_input, O_RDONLY); // Abre o ficheiro de input em modo READ
  if (input_fd < 0) {
      return;
  }

  int output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Abre o ficheiro de output em modo WRITE
  // Se o ficheiro não existir, cria um novo 
  if (output_fd < 0) {
      close(input_fd);
      return;
  }

  char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
  char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
  unsigned int delay;
  size_t num_pairs;

  while (1) { /*Todos os comandos têm o respetivo output. Daqui, eu faço o loop infinito 
  para ver o output que irá mandar para o ficheiro com a extensão .out*/

  switch (get_next(input_fd)) {
    case CMD_WRITE:
      num_pairs = parse_write(input_fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) { // Se for dado WRITE sem nenhum par 
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_write(num_pairs, keys, values)) { // Se não conseguir escrever (se kvs_write retornar 1)
        fprintf(stderr, "Failed to write pair\n");
      }

      break;

    case CMD_READ:
      num_pairs = parse_read_delete(input_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) { // Se não houver nada para ler
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_read(num_pairs, keys, output_fd)) { // Se não conseguir ler (se kvs_read retornar 1)
        fprintf(stderr, "Failed to read pair\n");
      }
      break;

    case CMD_DELETE:
      num_pairs = parse_read_delete(input_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

      if (num_pairs == 0) { // Se não houver nada para apagar
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_delete(num_pairs, keys, output_fd)) { // Se não conseguir apagar (se kvs_delete retornar 1)
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
      /*essas linhas estão em comentário porque o output vai para o waiting mas não é suposto imprimir waiting
      no ficheiro .out. Escrevemos essas linhas por questão de debugging*/
    case CMD_BACKUP:

      if (kvs_backup()) {
        fprintf(stderr, "Failed to perform backup.\n");
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
          "  BACKUP\n" // Not implemented
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

void processar_diretoria_do_job(const char *input_dir, const char *output_dir) {
  DIR *dir = opendir(input_dir); // Abre a diretoria
  if (!dir) { // Se não existir a determinada diretoria
      fprintf(stderr, "Failed to open directory: %s\n", input_dir);
      return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) { // Ler diretoria
      if (strstr(entry->d_name, ".job")) { // Verifica se o ficheiro tem a extensão .job
          char caminho_input[MAX_JOB_FILE_NAME_SIZE]; // Criar variável que armazena o caminho do .job
          char output_path[MAX_JOB_FILE_NAME_SIZE]; // Criar variável que armazena o caminho do .out
          // Cria o caminho completo para o ficheiro .job
          snprintf(caminho_input, MAX_JOB_FILE_NAME_SIZE, "%s/%s", input_dir, entry->d_name);

          // Substitui a extensão .job por .out e muda o conteudo com o conteudo de output, output_dir
          snprintf(output_path, MAX_JOB_FILE_NAME_SIZE, "%s/%.*s.out", output_dir, (int)(strlen(entry->d_name) - 4), entry->d_name);

          processar_ficheiro_job(caminho_input, output_path); // Lê o ficheiro
      }
  }

  closedir(dir); // Fecha a diretoria
}

int main(int argc, char *argv[]) {
  
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <input_dir> <output_dir>\n", argv[0]);
    return 1;
  }
  if (kvs_init()) {
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  processar_diretoria_do_job(argv[1], argv[1]);

  kvs_terminate();
}