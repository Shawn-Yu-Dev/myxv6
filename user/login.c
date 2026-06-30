#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define USERNAME "root"
#define PASSWORD "root"

int
main(int argc,char *argv[])
{
  char username[32];
  char password[32];

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

    if (strcmp(username, USERNAME) == 0 && strcmp(password, PASSWORD) == 0) {
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