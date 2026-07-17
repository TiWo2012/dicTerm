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

#define ROWS 36
#define COLS 100

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

static void scroll_buffer(char buf[ROWS][COLS]) {
  for (int i = 0; i < ROWS - 1; i++)
    memcpy(buf[i], buf[i + 1], COLS);

  memset(buf[ROWS - 1], ' ', COLS);
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

  char screen[ROWS][COLS];
  for (int r = 0; r < ROWS; r++)
    memset(screen[r], ' ', COLS);

  int cx = 0, cy = 0;

  while (!WindowShouldClose()) {
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

      for (ssize_t i = 0; i < n; i++) {
        char ch = buf[i];

        if (ch == '\n') {
          cx = 0;
          cy++;
          if (cy >= ROWS) {
            scroll_buffer(screen);
            cy = ROWS - 1;
          }
        } else if (ch == '\r') {
          cx = 0;
        } else if (ch == '\t') {
          int next = ((cx / 8) + 1) * 8;
          if (next < COLS)
            cx = next;
        } else if (ch == '\b') {
          if (cx > 0)
            cx--;
        } else if (ch >= ' ') {
          screen[cy][cx] = ch;
          cx++;
          if (cx >= COLS) {
            cx = 0;
            cy++;
            if (cy >= ROWS) {
              scroll_buffer(screen);
              cy = ROWS - 1;
            }
          }
        }
      }
    }

draw:
    BeginDrawing();
    ClearBackground(RAYWHITE);

    for (int r = 0; r < ROWS; r++) {
      char line[COLS + 1];
      memcpy(line, screen[r], COLS);
      line[COLS] = '\0';

      int len = COLS;
      while (len > 0 && line[len - 1] == ' ')
        len--;
      line[len] = '\0';

      DrawText(line, 10, 10 + r * 20, 20, BLACK);
    }

    EndDrawing();
  }

  CloseWindow();

  return 0;
}
