#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define CRED_FILE "/passwd"
#define MAX_LINE 64

// Simple hash to avoid storing plaintext passwords
unsigned int hash_password(const char *s) {
    unsigned int h = 5381;
    int c;
    while ((c = *s++))
        h = ((h << 5) + h) + c;
    return h;
}

int
main(int argc,char *argv[])
{
  char username[64];
  char password[64];
  char file_user[MAX_LINE];
  int fd;

  // Pre-read credentials from file
  fd = open(CRED_FILE, O_RDONLY);
  if (fd < 0) {
    printf("Cannot open %s\n", CRED_FILE);
    exit(1);
  }
  int n = read(fd, file_user, MAX_LINE - 1);
  file_user[n] = '\0';
  close(fd);

  // Parse user:hash (password is stored as numeric hash, not plaintext)
  char *colon = file_user;
  while (*colon && *colon != ':') colon++;
  if (*colon != ':') {
    printf("Invalid format in %s (expected user:hash)\n", CRED_FILE);
    exit(1);
  }
  *colon = '\0';
  char *u = file_user;
  char *hashstr = colon + 1;
  // Strip trailing newline from hash
  int hlen = 0;
  while (hashstr[hlen] && hashstr[hlen] != '\n') hlen++;
  hashstr[hlen] = '\0';
  // Parse the stored hash value
  unsigned int stored_hash = 0;
  for (int i = 0; hashstr[i]; i++) {
    if (hashstr[i] >= '0' && hashstr[i] <= '9')
      stored_hash = stored_hash * 10 + (hashstr[i] - '0');
  }

  // 准备需要传递的参数
  char *sh_args[] = { "sh", 0 };

  while(1) {
    printf("\nlogin: ");
    gets(username, sizeof(username));
    // 去掉换行符
    if (strlen(username) > 0 && username[strlen(username) - 1] == '\n') {
      username[strlen(username) - 1] = '\0';
    }

    printf("passwd: ");
    gets(password, sizeof(password));
    if (strlen(password) > 0 && password[strlen(password) - 1] == '\n') {
      password[strlen(password) - 1] = '\0';
    }

    if (strcmp(username, u) == 0 && hash_password(password) == stored_hash) {
      printf("\nLogin successful! \n");

      int pid = fork();
      if (pid < 0) {
        printf("login: fork failed\n");
        exit(1);
      }
      if (pid == 0) {
        // 子进程：执行 Shell
        exec("sh", sh_args);
        printf("login: exec sh failed\n");
        exit(1);
      } else {
        // 父进程（login）：等待 Shell 退出（比如用户输入 exit）
        // 一旦 Shell 退出，循环会再次运行，要求重新登录
        int status;
        wait(&status);
      }
    } else {
      printf("Login incorrect. Try again.\n");
    }
  }
  exit(0);
}
