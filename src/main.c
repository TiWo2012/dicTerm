#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <raylib.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static struct termios original_termios;

static void restore_terminal(void) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

static void enable_raw_mode(void) {
  tcgetattr(STDIN_FILENO, &original_termios);
  atexit(restore_terminal);

  struct termios raw = original_termios;
  cfmakeraw(&raw);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(void) {
  InitWindow(800, 600, "dicTerm");
  enable_raw_mode();

  int master_fd;
  pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
  if (pid == -1) {
    perror("forkpty");
    return 1;
  }

  fcntl(master_fd, F_SETFL, O_NONBLOCK);

  if (pid == 0) {
    execl("/bin/bash", "bash", NULL);
    perror("execl");
    _exit(1);
  }

  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(master_fd, &readfds);

    int maxfd = (STDIN_FILENO > master_fd) ? STDIN_FILENO : master_fd;

    struct timeval tv = {0, 16000};
    if (select(maxfd + 1, &readfds, NULL, NULL, &tv) == -1) {
      if (errno == EINTR)
        goto draw;

      perror("select");
      break;
    }

    if (FD_ISSET(STDIN_FILENO, &readfds)) {
      char buf[4096];
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

      if (n > 0) {
        ssize_t written = 0;
        while (written < n) {
          ssize_t r = write(master_fd, buf + written, (size_t)(n - written));
          if (r > 0) {
            written += r;
          } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          } else {
            break;
          }
        }
      }
    }

    if (FD_ISSET(master_fd, &readfds)) {
      char buf[4096];
      ssize_t n = read(master_fd, buf, sizeof(buf));

      if (n <= 0)
        break;

      DrawText(buf, 10, 10, 20, BLACK);
    }

draw:
    EndDrawing();
  }

  CloseWindow();

  return 0;
}
