#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "api.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"

int REQ_FD_WR;
int RESP_FD_RD;
int NOTIF_FD_RD;

char REQ_PIPE_PATH[MAX_PIPE_PATH_LENGTH];
char RESP_PIPE_PATH[MAX_PIPE_PATH_LENGTH];
char NOTIF_PIPE_PATH[MAX_PIPE_PATH_LENGTH];

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


void copy_and_pad(const char *src, char *dest) {
  size_t len = strlen(src);
  if (len > MAX_PIPE_PATH_LENGTH) len = MAX_PIPE_PATH_LENGTH;
  memcpy(dest, src, len);
  memset(dest + len, '\0', MAX_PIPE_PATH_LENGTH - len);  // Fill the rest with '\0'
}

void build_message_paths(const char *req_pipe_path, const char *resp_pipe_path, const char *notifications_pipe_path, char *message) {
    snprintf(message, MAX_PIPE_PATH_LENGTH, "1|%s|%s|%s", req_pipe_path, resp_pipe_path, notifications_pipe_path);
}
void build_message(const char *req_pipe_path, const char *resp_pipe_path, const char *notifications_pipe_path, char *message) {
  // Start with OP_CODE=1
  message[0] = (char) 1;

  // Copy and pad the pipe names into the message
  copy_and_pad(req_pipe_path, message + 1);
  copy_and_pad(resp_pipe_path, message + 1 + MAX_PIPE_PATH_LENGTH);
  copy_and_pad(notifications_pipe_path, message + 1 + 2 * MAX_PIPE_PATH_LENGTH);
}

char read_msg(int fd) {
  char buffer[MAX_STRING_SIZE];
  ssize_t ret = read(fd, buffer, MAX_STRING_SIZE);
  if (ret == 0) {
      // ret == 0 indicates EOF
      fprintf(stderr, "pipe closed\n");
      return '\0';
  } else if (ret == -1) {
      // ret == -1 indicates error
      fprintf(stderr, "read failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }

  return buffer[1];
}

int kvs_connect(char const *req_pipe_path, char const *resp_pipe_path,
                char const *notif_pipe_path, char const *server_pipe_path, int *notif_pipe) {
  int fifo_fd_wr = open(server_pipe_path, O_WRONLY);
  if (fifo_fd_wr == -1) {
    perror("Failed to open FIFO");
    exit(EXIT_FAILURE);
  }

  /* remove pipes if they exist */
  if (unlink(req_pipe_path) != 0 && errno != ENOENT) {
    perror("unlink(%s) failed");
    exit(EXIT_FAILURE);
  }
  if (unlink(resp_pipe_path) != 0 && errno != ENOENT) {
    perror("unlink(%s) failed");
    exit(EXIT_FAILURE);
  }
  if (unlink(notif_pipe_path) != 0 && errno != ENOENT) {
    perror("unlink(%s) failed");
    exit(EXIT_FAILURE);
  }

  /* create pipes */
  if (mkfifo(req_pipe_path, 0640) != 0) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }
  if (mkfifo(resp_pipe_path, 0640) != 0) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }
  if (mkfifo(notif_pipe_path, 0640) != 0) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }
  strcpy(REQ_PIPE_PATH, req_pipe_path);
  strcpy(RESP_PIPE_PATH, resp_pipe_path);
  strcpy(NOTIF_PIPE_PATH, notif_pipe_path);

  // send connect message to request pipe and wait for response in response pipe
  char message[1 + 3 * MAX_PIPE_PATH_LENGTH] = {0};

  build_message_paths(req_pipe_path, resp_pipe_path, notif_pipe_path, message);

  send_msg(fifo_fd_wr, message);

  RESP_FD_RD = open(resp_pipe_path, O_RDONLY);
  if (RESP_FD_RD == -1) {
    perror("Failed to open FIFO");
    exit(EXIT_FAILURE);
  }

  REQ_FD_WR = open(req_pipe_path, O_WRONLY);
  if (REQ_FD_WR == -1) {
    perror("Failed to open FIFO");
    exit(EXIT_FAILURE);
  }

  NOTIF_FD_RD = open(notif_pipe_path, O_RDONLY);
  if (NOTIF_FD_RD == -1) {
    perror("Failed to open FIFO");
    exit(EXIT_FAILURE);
  }
  *notif_pipe = NOTIF_FD_RD;

  char result = read_msg(RESP_FD_RD);
  printf("Server returned %c for operation: connect\n", result);
  if (result != '0') {
    close(fifo_fd_wr);
    close(RESP_FD_RD);
    close(REQ_FD_WR);
    close(NOTIF_FD_RD);
    return 1;
  }

  close(fifo_fd_wr);
  return 0;
}

int kvs_disconnect(void) {
  // close pipes and unlink pipe files
  char message_to_send[42] = {0};
  message_to_send[0] = 2;
  send_msg(REQ_FD_WR, message_to_send);
  char result = read_msg(RESP_FD_RD);
  printf("Server returned %d for operation: disconnect\n", (int) result);

  if (result == '\0') {
    close(RESP_FD_RD);
    close(REQ_FD_WR);
    close(NOTIF_FD_RD);
    unlink(REQ_PIPE_PATH);
    unlink(RESP_PIPE_PATH);
    unlink(NOTIF_PIPE_PATH);
    return 0;
  }

  return 1;
}

int kvs_subscribe(const char *key) {
  char message_to_send[42] = {0};
  message_to_send[0] = 3;
  strncpy(message_to_send + 1, key, 41);
  send_msg(REQ_FD_WR, message_to_send);
  char result = read_msg(RESP_FD_RD);
  printf("Server returned %d for operation: subscribe\n", (int) result);

  if (result != '\0') {
    return 1;
  }

  return 0;
}

int kvs_unsubscribe(const char *key) {
  char message_to_send[42] = {0};
  message_to_send[0] = 4;
  strncpy(message_to_send + 1, key, 41);
  send_msg(REQ_FD_WR, message_to_send);

  char result = read_msg(RESP_FD_RD);
  printf("Server returned %d for operation: unsubscribe\n", (int) result);

  if (result != '\0') {
    return 1;
  }
  return 0;
}
// void sigusr1(int signal){
// printf("Received SIGUSR1\n");
//   if (REQ_PIPE_PATH == {0} || RESP_PIPE_PATH == {0} || NOTIF_PIPE_PATH == {0}){
//     fprintf(stderr, "Error: Pipes not initialized. Call kvs_connect() first.\n");
//     return;
//   }
//   if (unlink(REQ_PIPE_PATH) != 0 ||
//       unlink(RESP_PIPE_PATH) != 0 ||
//       unlink(NOTIF_PIPE_PATH) != 0){
//     perror("Failed to delete pipes");
//   }
//   REQ_PIPE_PATH = {0};
//   RESP_PIPE_PATH = {0};
//   NOTIF_PIPE_PATH = {0};

// printf("Successfully disconnected all clients\n");
// }
