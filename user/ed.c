#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAX_LINES 500      // 最大支持 500 行
#define MAX_LINE_LEN 128   // 每行最大 128 字节

// 文本缓冲区：用二维数组存储每一行的文本
char buffer[MAX_LINES][MAX_LINE_LEN];
int total_lines = 0;       // 缓冲区当前总行数
int current_line = -1;     // 当前光标所在的行号（0-indexed）
char filename[64];         // 当前打开的文件名

// 辅助函数：安全地获取字符串长度
int my_strlen(char *s) {
    int len = 0;
    while(s[len]) len++;
    return len;
}

// 辅助函数：去除行尾的换行符
void trim_newline(char *s) {
    int len = my_strlen(s);
    if (len > 0 && s[len - 1] == '\n') {
        s[len - 1] = '\0';
    }
}

// 从标准输入读入一行数据（支持退格等基本处理）
int get_line(char *s, int max) {
    int i = 0;
    char c;
    while (read(0, &c, 1) > 0) {
        if (c == '\r' || c == '\n') {
            s[i++] = '\n';
            break;
        }
        if (i < max - 1) {
            s[i++] = c;
        }
    }
    s[i] = '\0';
    return i;
}

// 读取文件到缓冲区
void load_file() {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        // 如果文件不存在，提示是新文件
        printf("? %s [New File]\n", filename);
        total_lines = 0;
        current_line = -1;
        return;
    }

    char c;
    int line_idx = 0;
    int char_idx = 0;

    while (read(fd, &c, 1) > 0 && line_idx < MAX_LINES) {
        if (char_idx < MAX_LINE_LEN - 1) {
            buffer[line_idx][char_idx++] = c;
        }
        if (c == '\n') {
            buffer[line_idx][char_idx] = '\0';
            trim_newline(buffer[line_idx]); // 统一去掉换行符，方便管理
            line_idx++;
            char_idx = 0;
        }
    }
    // 处理最后一行没有换行符的情况
    if (char_idx > 0 && line_idx < MAX_LINES) {
        buffer[line_idx][char_idx] = '\0';
        trim_newline(buffer[line_idx]);
        line_idx++;
    }

    total_lines = line_idx;
    current_line = total_lines - 1; // 默认光标停留在最后一行
    close(fd);
    printf("%d lines read\n", total_lines);
}

// 保存缓冲区到文件
void save_file() {
    int fd = open(filename, O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) {
        printf("? cannot write to %s\n", filename);
        return;
    }
    for (int i = 0; i < total_lines; i++) {
        write(fd, buffer[i], my_strlen(buffer[i]));
        write(fd, "\n", 1);
    }
    close(fd);
    printf("%d lines written\n", total_lines);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ed <filename>\n");
        exit(0);
    }

    // 复制文件名
    int i;
    for (i = 0; argv[1][i] && i < 63; i++) filename[i] = argv[1][i];
    filename[i] = '\0';

    // 初始化加载文件
    load_file();

    char cmd_buf[64];
    while (1) {
        // ed 不输出花哨的提示符，直接等待命令输入
        get_line(cmd_buf, sizeof(cmd_buf));
        trim_newline(cmd_buf);

        char cmd = cmd_buf[0];

        // 1. 打印当前行命令：p
        if (cmd == 'p') {
            if (current_line >= 0 && current_line < total_lines) {
                printf("%s\n", buffer[current_line]);
            } else {
                printf("?\n"); // 标准 ed 的经典错误提示
            }
        }
        // 2. 追加文本命令：a
        else if (cmd == 'a') {
            char input[MAX_LINE_LEN];
            while (1) {
                get_line(input, sizeof(input));
                trim_newline(input);

                // 输入单独一个 "." 则退出追加模式
                if (input[0] == '.' && input[1] == '\0') {
                    break;
                }

                // 在当前行后面插入
                if (total_lines >= MAX_LINES) {
                    printf("? buffer overflow\n");
                    break;
                }

                // 将当前行之后的行往后挪腾出位置
                for (int j = total_lines; j > current_line + 1; j--) {
                    int k = 0;
                    while ((buffer[j][k] = buffer[j-1][k])) k++;
                }

                // 写入新行
                int k = 0;
                while ((buffer[current_line + 1][k] = input[k])) k++;

                current_line++;
                total_lines++;
            }
        }
        // 3. 删除当前行命令：d
        else if (cmd == 'd') {
            if (current_line >= 0 && current_line < total_lines) {
                // 后面的行往前覆盖
                for (int j = current_line; j < total_lines - 1; j++) {
                    int k = 0;
                    while ((buffer[j][k] = buffer[j+1][k])) k++;
                }
                total_lines--;
                if (current_line >= total_lines) {
                    current_line = total_lines - 1;
                }
            } else {
                printf("?\n");
            }
        }
        // 4. 保存文件命令：w
        else if (cmd == 'w') {
            save_file();
        }
        // 5. 退出命令：q
        else if (cmd == 'q') {
            break;
        }
        // 6. 输入数字直接跳转到对应行（ed的经典特性，注意这里为了用户体验转化为 1-indexed 输入）
        else if (cmd >= '0' && cmd <= '9') {
            int line_num = atoi(cmd_buf);
            if (line_num >= 1 && line_num <= total_lines) {
                current_line = line_num - 1;
                printf("%s\n", buffer[current_line]);
            } else {
                printf("?\n");
            }
        }
        // 未知命令
        else if (my_strlen(cmd_buf) > 0) {
            printf("?\n");
        }
    }

    exit(0);
}