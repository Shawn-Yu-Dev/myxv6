#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAX_TTY 5
#define CTRL_FILE "/tty_ctl"
#define CUR_FILE "/tty_cur"

int active_tty = -1;
int stdin_wr[MAX_TTY];
int stdout_rd[MAX_TTY];
int output_child = -1;

void
draw_status(void)
{
  write(1, "\r", 1);
  for (int i = 0; i < MAX_TTY; i++) {
    if (i == active_tty) {
      write(1, "[", 1);
      write(1, "0123456789" + i, 1);
      write(1, "*] ", 3);
    } else {
      write(1, "[", 1);
      write(1, "0123456789" + i, 1);
      write(1, "] ", 2);
    }
  }
  write(1, "\n", 1);
}

void
write_cur_tty(void)
{
  int fd = open(CUR_FILE, O_WRONLY | O_CREATE | O_TRUNC);
  if (fd < 0) return;
  char buf[2] = { (char)('0' + active_tty), '\n' };
  write(fd, buf, 2);
  close(fd);
}

void
switch_tty(int new_tty)
{
  if (new_tty < 0 || new_tty >= MAX_TTY || new_tty == active_tty) return;

  if (output_child > 0) {
    kill(output_child);
    wait(0);
  }

  active_tty = new_tty;
  write_cur_tty();

  output_child = fork();
  if (output_child < 0)
    return;
  if (output_child == 0) {
    char buf[512];
    int n;
    while ((n = read(stdout_rd[active_tty], buf, sizeof(buf))) > 0)
      write(1, buf, n);
    exit(0);
  }

  draw_status();
}

void
process_external_commands(void)
{
  int fd = open(CTRL_FILE, O_RDONLY);
  if (fd < 0) return;
  char buf[16];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return;
  buf[n] = '\0';
  unlink(CTRL_FILE);
  for (int i = 0; i < n; i++)
    if (buf[i] >= '1' && buf[i] <= '0' + MAX_TTY)
      switch_tty(buf[i] - '1');
}

void
show_tty_number(void)
{
  int fd = open(CUR_FILE, O_RDONLY);
  if (fd < 0) {
    printf("TTY: not in multiplexer\n");
    return;
  }
  char buf[4];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n > 0) {
    buf[n] = '\0';
    // buf is like "0\n"
    int num = buf[0] - '0' + 1;
    if (num >= 1 && num <= MAX_TTY)
      printf("TTY: %d\n", num);
  }
}

int
main(int argc, char *argv[])
{
  if (argv[0][0] == 't' && argv[0][1] == 't' && argv[0][2] == 'y' &&
      argv[0][3] >= '1' && argv[0][3] <= '0' + MAX_TTY && argv[0][4] == '\0') {
    int fd = open(CTRL_FILE, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) {
      printf("tty: multiplexer not running\n");
      exit(1);
    }
    char cmd[2] = { argv[0][3], '\n' };
    write(fd, cmd, 2);
    close(fd);
    exit(0);
  }

  // "tty" with no number suffix: check if multiplexer is running
  // (CTRL_FILE exists → running as daemon; CUR_FILE exists → just check)
  int fd = open(CUR_FILE, O_RDONLY);
  if (fd >= 0) {
    close(fd);
    show_tty_number();
    exit(0);
  }

  // Start multiplexer
  int pipe_in[2], pipe_out[2];

  for (int i = 0; i < MAX_TTY; i++) {
    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
      printf("tty: pipe failed\n");
      exit(1);
    }

    int pid = fork();
    if (pid < 0) break;
    if (pid == 0) {
      close(0); dup(pipe_in[0]);
      close(1); dup(pipe_out[1]);
      close(2); dup(pipe_out[1]);
      for (int j = 0; j < i; j++) {
        close(stdin_wr[j]); close(stdout_rd[j]);
      }
      close(pipe_in[0]); close(pipe_in[1]);
      close(pipe_out[0]); close(pipe_out[1]);
      char *sh_argv[] = { "sh", 0 };
      exec("sh", sh_argv);
      exit(1);
    }

    close(pipe_in[0]);
    close(pipe_out[1]);
    stdin_wr[i] = pipe_in[1];
    stdout_rd[i] = pipe_out[0];
  }

  int ctrl_pipe[2];
  pipe(ctrl_pipe);

  int helper = fork();
  if (helper == 0) {
    close(ctrl_pipe[0]);
    char c;
    while (read(0, &c, 1) > 0)
      write(ctrl_pipe[1], &c, 1);
    exit(0);
  }
  close(ctrl_pipe[1]);
  pipe_noblock(ctrl_pipe[0]);

  // Create mark file so "tty" command knows multiplexer is running
  fd = open(CTRL_FILE, O_WRONLY | O_CREATE | O_TRUNC);
  if (fd >= 0) close(fd);

  switch_tty(0);

  while (1) {
    char c;
    int n = read(ctrl_pipe[0], &c, 1);

    if (n > 0) {
      write(stdin_wr[active_tty], &c, 1);
    } else {
      process_external_commands();
      pause(1);
    }
  }

  if (output_child > 0) { kill(output_child); wait(0); }
  kill(helper); wait(0);
  unlink(CTRL_FILE);
  unlink(CUR_FILE);
  exit(0);
}
