#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"  // 必须引入，为了使用 O_RDONLY
#include "user/user.h"

#define TAPE_SIZE 30000
#define CODE_SIZE 30000

// tape 是图灵机的“纸带”（内存格），初始化为 0
char tape[TAPE_SIZE] = {0};
char *p = tape;             // p 是纸带的数据指针

// code 用于存放从文件读入的 Brainfuck 源代码
char code[CODE_SIZE];
char *c = code;             // c 是代码的指令指针

int main(int argc, char** argv) {
    // 检查命令行参数，确保用户传入了文件名
    if (argc < 2) {
        printf("Usage: %s <bf_file>\n", argv[0]);
        exit(1); // xv6 中退出程序必须使用 exit()
    }

    // 打开 Brainfuck 源代码文件 (使用 xv6 的 open)
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("Error: Cannot open file %s\n", argv[1]);
        exit(1);
    }

    // 将文件内容一次性读入 code 数组中，并获取实际读取的字节数（长度）
    int len = read(fd, code, CODE_SIZE);
    if (len < 0) {
        printf("Error: Read file failed\n");
        close(fd);
        exit(1);
    }
    close(fd);

    // 开始解释执行，直到代码指针 c 超出文件范围
    while (c < code + len) {
        switch (*c) {
            case '>':
                p++; // 数据指针右移
                break;
            case '<':
                p--; // 数据指针左移
                break;
            case '+':
                (*p)++; // 当前格子数值加 1
                break;
            case '-':
                (*p)--; // 当前格子数值减 1
                break;
            case '.':
                write(1, p, 1);
                break;
            case ',': {
                char buf_ch;
                // 从标准输入(fd为0)读取1个字符
                if (read(0, &buf_ch, 1) > 0) {
                    *p = buf_ch;
                } else {
                    *p = 0; // 读到 EOF 或出错时赋 0
                }
                break;
            }

            case '[':
                if (!*p) {
                    int bracket_count = 1;
                    // 增加 c < code + len - 1 限制，防止向后越界
                    while (bracket_count > 0 && c < code + len - 1) {
                      c++;
                    if (*c == '[') bracket_count++;
                    else if (*c == ']') bracket_count--;
                    }
                }
            break;

            case ']':
                if (*p) {
                int bracket_count = 1;
                // 增加 c > code 限制，防止向前越界
                while (bracket_count > 0 && c > code) {
                    c--;
                    if (*c == ']') bracket_count++;
                    else if (*c == '[') bracket_count--;
                }
            }
            break;
        }
        c++; // 执行完当前指令，指向下一个字符
    }
    exit(0); // 正常退出
}