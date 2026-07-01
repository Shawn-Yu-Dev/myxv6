#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  // 向标准输出写入清屏及复位光标的转义序列
  write(1, "\033[2J\033[H", 7);
  exit(0);
}