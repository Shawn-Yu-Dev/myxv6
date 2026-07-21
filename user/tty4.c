#include "kernel/types.h"
#include "user/user.h"
int main(int argc, char *argv[]) {
  exec("tty", (char *[]){"tty4", 0});
  return -1;
}
