// forth.c -- xv6 用户态 Forth 硬件调试器
#include "kernel/types.h"
#include "user.h"

// ========== 配置常量 ==========
#define DSIZE     128
#define RSIZE     128
#define DICTSIZE  (4*1024)   // 字典大小
#define NAMELEN   31
#define BUFLEN    128

// 安全内存池（模拟物理内存，用于 @ ! c@ c! w@ w! 等操作）
#define HEAP_SIZE 1024  // HEAP_SIZE 调试时直接修改为硬件内存地址
int user_heap[HEAP_SIZE];    // 以 int 为单位，大小 4KB

// 模拟 I/O 空间（用于 inb / outb 等操作，可映射到真实端口）
#define IO_SIZE   256   // 调试时可映射到真实端口
unsigned char io_space[IO_SIZE];
// ========== 命令替换 ==========

void get_line(char *buf, int max) {
    int i = 0;
    char c;
    while(read(0, &c, 1) == 1 && i < max - 1) {
        if(c == '\n' || c == '\r') break;
        buf[i++] = c;
    }
    buf[i] = 0;
}

void print_hex(int v, int digits) {
    char hex[] = "0123456789abcdef";
    for(int i = (digits - 1) * 4; i >= 0; i -= 4) {
        printf("%c", hex[(v >> i) & 0xF]);
    }
}

// ========== 虚拟机状态 ==========
int  dstack[DSIZE];
int  dsp;
int  rstack[RSIZE];
int  rsp;

char dict[DICTSIZE];
int  dict_here;
char input_buf[BUFLEN];

int last_entry = -1;   // 字典链表头

// ========== 字典条目结构 ==========
#define LINK_OFF   0
#define FLAGS_OFF  4
#define LEN_OFF    5
#define NAME_OFF   6

// 内建操作码
enum OPCODE {
  OP_BYE=0,
  OP_DOT, OP_EMIT, OP_CR,
  OP_PLUS, OP_MINUS, OP_STAR, OP_SLASH, OP_MOD,
  OP_EQUAL, OP_LESS, OP_GREATER,
  OP_AND, OP_OR, OP_NOT, OP_XOR,
  OP_DUP, OP_DROP, OP_SWAP, OP_OVER, OP_ROT, OP_DEPTH,
  // 内存字访问（基于 user_heap 下标）
  OP_FETCH,   // @ ( index -- value )
  OP_STORE,   // ! ( value index -- )
  // 字节访问
  OP_C_FETCH, // c@ ( byte_index -- byte )
  OP_C_STORE, // c! ( byte byte_index -- )
  // 半字访问
  OP_W_FETCH, // w@ ( halfword_index -- halfword )
  OP_W_STORE, // w! ( halfword halfword_index -- )
  // 返回栈搬运
  OP_TOR,     // >r
  OP_FROMR,   // r>
  OP_RFETCH,  // r@
  // 调试工具
  OP_WORDS,   // words
  OP_DOTS,    // .s
  OP_DUMP,    // dump ( addr len -- ) 十六进制转储
  // I/O 模拟（可替换为真实端口访问）
  OP_INB,     // inb ( port -- byte )
  OP_OUTB,    // outb ( byte port -- )
  OP_INW,     // inw ( port -- word )
  OP_OUTW,    // outw ( word port -- )
  // 移位
  OP_SHL, OP_SHR,
  // 控制流
  OP_LIT, OP_BRANCH, OP_0BRANCH, OP_RET,
  OP_LASTOP
};

// ========== 基本栈操作 ==========
void dpush(int v) {
  if(dsp < DSIZE) dstack[dsp++] = v;
  else { printf("Data stack overflow\n"); exit(0); }
}
int dpop(void) {
  if(dsp > 0) return dstack[--dsp];
  else { printf("Data stack underflow\n"); exit(0); }
}
int dtop(void) {
  if(dsp > 0) return dstack[dsp-1];
  else { printf("Data stack empty\n"); exit(0); }
}
void rpush(int v) {
  if(rsp < RSIZE) rstack[rsp++] = v;
  else { printf("Return stack overflow\n"); exit(0); }
}
int rpop(void) {
  if(rsp > 0) return rstack[--rsp];
  else { printf("Return stack underflow\n"); exit(0); }
}

// ========== 字典基本操作 ==========
int dict_new(const char *name, int flags) {
  int len = strlen(name);
  if(len >= NAMELEN) len = NAMELEN-1;
  int total = NAME_OFF + len + 1;
  if(dict_here + total > DICTSIZE) {
    printf("Insufficient dictionary space\n"); exit(0);
  }
  int entry = dict_here;
  *(int *)(dict + entry + LINK_OFF) = 0;
  dict[entry + FLAGS_OFF] = flags;
  dict[entry + LEN_OFF] = len;
  memmove(dict + entry + NAME_OFF, name, len);
  dict[entry + NAME_OFF + len] = '\0';
  dict_here += total;
  return entry;
}

void dict_append_byte(int b) {
  if(dict_here >= DICTSIZE) { printf("Dictionary overflow\n"); exit(0); }
  dict[dict_here++] = b;
}
void dict_append_int(int v) {
  dict_append_byte(v & 0xFF);
  dict_append_byte((v >> 8) & 0xFF);
  dict_append_byte((v >> 16) & 0xFF);
  dict_append_byte((v >> 24) & 0xFF);
}
int dict_code_off(int entry) {
  return entry + NAME_OFF + dict[entry + LEN_OFF] + 1;
}
int dict_find(const char *word) {
  int entry = last_entry;
  while(entry != -1) {
    if(strcmp(dict + entry + NAME_OFF, word) == 0)
      return entry;
    entry = *(int *)(dict + entry + LINK_OFF);
  }
  return -1;
}
int dict_new_linked(const char *name, int flags) {
  int entry = dict_new(name, flags);
  *(int *)(dict + entry + LINK_OFF) = last_entry;
  last_entry = entry;
  return entry;
}

// ========== 内建操作执行 ==========
void exec_builtin(int op) {
  int a,b;
  switch(op) {
    case OP_BYE: exit(0); break;
    case OP_DOT: printf("%d ", dpop()); break;
    case OP_EMIT: printf("%c", (char)dpop()); break;
    case OP_CR: printf("\n"); break;
    case OP_PLUS: b=dpop(); a=dpop(); dpush(a+b); break;
    case OP_MINUS: b=dpop(); a=dpop(); dpush(a-b); break;
    case OP_STAR: b=dpop(); a=dpop(); dpush(a*b); break;
    case OP_SLASH: b=dpop(); a=dpop(); if(b==0){printf("Division by zero\n");exit(0);} if(a==0x80000000 && b==-1){printf("INT_MIN / -1 overflow\n");exit(0);} dpush(a/b); break;
    case OP_MOD: b=dpop(); a=dpop(); if(b==0){printf("Division by zero\n");exit(0);} if(a==0x80000000 && b==-1){printf("INT_MIN mod -1 overflow\n");exit(0);} dpush(a%b); break;
    case OP_EQUAL: b=dpop(); a=dpop(); dpush(a==b ? -1 : 0); break;
    case OP_LESS: b=dpop(); a=dpop(); dpush(a<b ? -1 : 0); break;
    case OP_GREATER: b=dpop(); a=dpop(); dpush(a>b ? -1 : 0); break;
    case OP_AND: b=dpop(); a=dpop(); dpush(a&b); break;
    case OP_OR:  b=dpop(); a=dpop(); dpush(a|b); break;
    case OP_XOR: b=dpop(); a=dpop(); dpush(a^b); break;
    case OP_NOT: a=dpop(); dpush(~a); break;
    case OP_DUP: dpush(dtop()); break;
    case OP_DROP: dpop(); break;
    case OP_SWAP: a=dpop(); b=dpop(); dpush(a); dpush(b); break;
    case OP_OVER: a=dpop(); b=dtop(); dpush(a); dpush(b); break;
    case OP_ROT: { int c=dpop(), b=dpop(), a=dpop(); dpush(b); dpush(c); dpush(a); } break;
    case OP_DEPTH: dpush(dsp); break;

    // ---------- 安全内存字访问（基于下标）----------
    case OP_FETCH: {   // @ ( idx -- n )
      int idx = dpop();
      if (idx < 0 || idx >= HEAP_SIZE) { printf("Invalid @ index %d\n", idx); exit(0); }
      dpush(user_heap[idx]);
      break;
    }
    case OP_STORE: {   // ! ( n idx -- )
      int idx = dpop();
      if (idx < 0 || idx >= HEAP_SIZE) { printf("Invalid ! index %d\n", idx); exit(0); }
      int val = dpop();
      user_heap[idx] = val;
      break;
    }

    // ---------- 字节访问（基于字节偏移）----------
    case OP_C_FETCH: { // c@ ( byte_off -- byte )
      int off = dpop();
      if (off < 0 || off >= HEAP_SIZE * sizeof(int)) { printf("Invalid c@ offset %d\n", off); exit(0); }
      unsigned char *base = (unsigned char*)user_heap;
      dpush(base[off]);
      break;
    }
    case OP_C_STORE: { // c! ( byte byte_off -- )
      int off = dpop();
      if (off < 0 || off >= HEAP_SIZE * sizeof(int)) { printf("Invalid c! offset %d\n", off); exit(0); }
      unsigned char *base = (unsigned char*)user_heap;
      int val = dpop();
      base[off] = val & 0xFF;
      break;
    }

    // ---------- 半字访问（基于半字偏移）----------
    case OP_W_FETCH: { // w@ ( half_off -- halfword )
      int off = dpop();
      if (off < 0 || off*2 + 1 >= HEAP_SIZE * (int)sizeof(int)) { printf("Invalid w@ offset %d\n", off); exit(0); }
      unsigned short *base = (unsigned short*)user_heap;
      dpush(base[off]);
      break;
    }
    case OP_W_STORE: { // w! ( halfword half_off -- )
      int off = dpop();
      if (off < 0 || off*2 + 1 >= HEAP_SIZE * (int)sizeof(int)) { printf("Invalid w! offset %d\n", off); exit(0); }
      unsigned short *base = (unsigned short*)user_heap;
      int val = dpop();
      base[off] = (unsigned short)val;
      break;
    }

    // 返回栈
    case OP_TOR:    rpush(dpop()); break;
    case OP_FROMR:  dpush(rpop()); break;
    case OP_RFETCH: 
      if(rsp > 0) dpush(rstack[rsp-1]);
      else { printf("Return stack empty\n"); exit(0); }
      break;

    // 工具
    case OP_WORDS: {
      int entry = last_entry;
      while(entry != -1) {
        printf("%s ", dict + entry + NAME_OFF);
        entry = *(int *)(dict + entry + LINK_OFF);
      }
      printf("\n");
      break;
    }
    case OP_DOTS:   // .s
      printf("<%d> ", dsp);
      for(int i = 0; i < dsp; i++) printf("%d ", dstack[i]);
      printf("\n");
      break;

    case OP_DUMP: { // dump ( addr len -- ) 其中 addr 是 user_heap 下标
      int len = dpop();
      int idx = dpop();
      if (idx < 0 || idx + len > HEAP_SIZE) { printf("Invalid dump range\n"); exit(0); }
      for (int i = 0; i < len; i += 8) {
        print_hex(idx + i, 4); printf(": ");
        for (int j = i; j < i + 8 && j < len; j++) {
          print_hex(user_heap[idx + j], 8); printf(" ");
        }
        printf("\n");
      }
      break;
    }

    // ---------- I/O 模拟 ----------
    case OP_INB: {   // inb ( port -- byte )
      int port = dpop();
      if (port < 0 || port >= IO_SIZE) { printf("Invalid inb port %d\n", port); exit(0); }
      dpush(io_space[port]);
      break;
    }
    case OP_OUTB: {  // outb ( byte port -- )
      int port = dpop();
      if (port < 0 || port >= IO_SIZE) { printf("Invalid outb port %d\n", port); exit(0); }
      int val = dpop();
      io_space[port] = val & 0xFF;
      break;
    }
    case OP_INW: {   // inw ( port -- word )  注意：读取两个连续端口
      int port = dpop();
      if (port < 0 || port+1 >= IO_SIZE) { printf("Invalid inw port %d\n", port); exit(0); }
      unsigned short val = io_space[port] | (io_space[port+1] << 8);
      dpush(val);
      break;
    }
    case OP_OUTW: {  // outw ( word port -- )
      int port = dpop();
      if (port < 0 || port+1 >= IO_SIZE) { printf("Invalid outw port %d\n", port); exit(0); }
      int val = dpop();
      io_space[port] = val & 0xFF;
      io_space[port+1] = (val >> 8) & 0xFF;
      break;
    }

    // 移位
    case OP_SHL: { int n = dpop(); int x = dpop(); if (n < 0 || n >= 32) { printf("Invalid shift amount %d\n", n); exit(0); } dpush(x << n); break; }
    case OP_SHR: { int n = dpop(); int x = dpop(); if (n < 0 || n >= 32) { printf("Invalid shift amount %d\n", n); exit(0); } dpush((unsigned)x >> n); break; }

    default: printf("Unknown builtin %d\n", op); exit(0);
  }
}

// ========== 内层解释器 ==========
int interpret(int entry) {
  int ip = dict_code_off(entry);
  while(1) {
    int op = dict[ip++] & 0xFF;
    if(op == OP_RET) return 0;
    else if(op == OP_LIT) {
      int v = (unsigned char)dict[ip] |
             ((unsigned char)dict[ip+1] << 8) |
             ((unsigned char)dict[ip+2] << 16) |
             ((unsigned char)dict[ip+3] << 24);
      ip += 4;
      dpush(v);
    }
    else if(op == OP_BRANCH) {
      int off = (unsigned char)dict[ip] |
               ((unsigned char)dict[ip+1] << 8) |
               ((unsigned char)dict[ip+2] << 16) |
               ((unsigned char)dict[ip+3] << 24);
      ip += off;
    }
    else if(op == OP_0BRANCH) {
      int off = (unsigned char)dict[ip] |
               ((unsigned char)dict[ip+1] << 8) |
               ((unsigned char)dict[ip+2] << 16) |
               ((unsigned char)dict[ip+3] << 24);
      ip += 4;
      if(dpop() == 0) ip += off;
    }
    else if(op < OP_LASTOP) {
      exec_builtin(op);
    }
    else { printf("Illegal instruction %d\n", op); exit(0); }
  }
}

// 自动为 branch/0branch 添加占位偏移量
void add_builtin(const char *name, int op) {
  dict_new_linked(name, 0);
  dict_append_byte(op);
  if (op == OP_BRANCH || op == OP_0BRANCH)
    dict_append_int(0);
  dict_append_byte(OP_RET);
}

// ========== 编译器 ==========
int compiling = 0;
int compile_entry;

void start_compile(const char *name) {
  compile_entry = dict_new_linked(name, 0);
  compiling = 1;
}
void finish_compile() {
  dict_append_byte(OP_RET);
  compiling = 0;
}
void compile_lit(int v) {
  dict_append_byte(OP_LIT);
  dict_append_int(v);
}

void compile_word(const char *word) {
  // 基本词
  if(strcmp(word, "bye")==0) dict_append_byte(OP_BYE);
  else if(strcmp(word, ".")==0) dict_append_byte(OP_DOT);
  else if(strcmp(word, "emit")==0) dict_append_byte(OP_EMIT);
  else if(strcmp(word, "cr")==0) dict_append_byte(OP_CR);
  else if(strcmp(word, "+")==0) dict_append_byte(OP_PLUS);
  else if(strcmp(word, "-")==0) dict_append_byte(OP_MINUS);
  else if(strcmp(word, "*")==0) dict_append_byte(OP_STAR);
  else if(strcmp(word, "/")==0) dict_append_byte(OP_SLASH);
  else if(strcmp(word, "mod")==0) dict_append_byte(OP_MOD);
  else if(strcmp(word, "=")==0) dict_append_byte(OP_EQUAL);
  else if(strcmp(word, "<")==0) dict_append_byte(OP_LESS);
  else if(strcmp(word, ">")==0) dict_append_byte(OP_GREATER);
  else if(strcmp(word, "and")==0) dict_append_byte(OP_AND);
  else if(strcmp(word, "or")==0) dict_append_byte(OP_OR);
  else if(strcmp(word, "xor")==0) dict_append_byte(OP_XOR);
  else if(strcmp(word, "not")==0) dict_append_byte(OP_NOT);
  else if(strcmp(word, "dup")==0) dict_append_byte(OP_DUP);
  else if(strcmp(word, "drop")==0) dict_append_byte(OP_DROP);
  else if(strcmp(word, "swap")==0) dict_append_byte(OP_SWAP);
  else if(strcmp(word, "over")==0) dict_append_byte(OP_OVER);
  else if(strcmp(word, "rot")==0) dict_append_byte(OP_ROT);
  else if(strcmp(word, "depth")==0) dict_append_byte(OP_DEPTH);
  // 内存
  else if(strcmp(word, "@")==0) dict_append_byte(OP_FETCH);
  else if(strcmp(word, "!")==0) dict_append_byte(OP_STORE);
  else if(strcmp(word, "c@")==0) dict_append_byte(OP_C_FETCH);
  else if(strcmp(word, "c!")==0) dict_append_byte(OP_C_STORE);
  else if(strcmp(word, "w@")==0) dict_append_byte(OP_W_FETCH);
  else if(strcmp(word, "w!")==0) dict_append_byte(OP_W_STORE);
  // 返回栈
  else if(strcmp(word, ">r")==0) dict_append_byte(OP_TOR);
  else if(strcmp(word, "r>")==0) dict_append_byte(OP_FROMR);
  else if(strcmp(word, "r@")==0) dict_append_byte(OP_RFETCH);
  // 工具
  else if(strcmp(word, "words")==0) dict_append_byte(OP_WORDS);
  else if(strcmp(word, ".s")==0) dict_append_byte(OP_DOTS);
  else if(strcmp(word, "dump")==0) dict_append_byte(OP_DUMP);
  // I/O
  else if(strcmp(word, "inb")==0) dict_append_byte(OP_INB);
  else if(strcmp(word, "outb")==0) dict_append_byte(OP_OUTB);
  else if(strcmp(word, "inw")==0) dict_append_byte(OP_INW);
  else if(strcmp(word, "outw")==0) dict_append_byte(OP_OUTW);
  // 移位
  else if(strcmp(word, "shl")==0) dict_append_byte(OP_SHL);
  else if(strcmp(word, "shr")==0) dict_append_byte(OP_SHR);
  // 跳转
  else if(strcmp(word, "branch")==0) dict_append_byte(OP_BRANCH);
  else if(strcmp(word, "0branch")==0) dict_append_byte(OP_0BRANCH);
  else {
    // 尝试用户自定义词或数字
    int entry = dict_find(word);
    if(entry != -1) {
      // --- 核心修改：内联编译逻辑 ---
      // 将该词定义区中的所有操作码直接复制到当前正在编译的词中
      int ip = dict_code_off(entry);
      while(1) {
        unsigned char op = dict[ip++];
        if(op == OP_RET) break; // 遇到结尾则停止拷贝
        
        dict_append_byte(op);
        
        // 如果拷贝的是常量指令，必须连同 4 字节数据一起拷贝
        if(op == OP_LIT || op == OP_BRANCH || op == OP_0BRANCH) {
          for(int i = 0; i < 4; i++) {
            dict_append_byte(dict[ip++]);
          }
        }
      }
    } else {
      // 保持原有的数字解析逻辑
      int val = 0, neg = 0;
      const char *p = word;
      if(*p == '-') { neg=1; p++; }
      for(; *p; p++) {
        if(*p < '0' || *p > '9') {
          printf("Undefined word: %s\n", word); exit(0);
        }
        val = val*10 + (*p - '0');
      }
      compile_lit(neg ? -val : val);
    }
  }
}

// ========== 字符串分词器 ==========
char *mystrtok(char *str, const char *delim) {
  static char *pos;
  if(str) pos = str;
  if(!pos) return 0;
  while(*pos && strchr(delim, *pos)) pos++;
  if(*pos == '\0') return 0;
  char *start = pos;
  while(*pos && !strchr(delim, *pos)) pos++;
  if(*pos) { *pos = '\0'; pos++; }
  return start;
}

// ========== 外层解释器 ==========
void outer_interpret() {
  char *delim = " \t\n";
  char *tok = mystrtok(input_buf, delim);
  while(tok) {
    if(compiling) {
      if(strcmp(tok, ";") == 0) finish_compile();
      else compile_word(tok);
    } else {
      if(strcmp(tok, ":") == 0) {
        tok = mystrtok(0, delim);
        if(!tok) { printf("Missing name\n"); return; }
        start_compile(tok);
      } else if(strcmp(tok, "bye") == 0) {
        exit(0);
      } else {
        int entry = dict_find(tok);
        if(entry != -1) {
          interpret(entry);
        } else {
          // 数字解析
          int val = 0, neg = 0;
          const char *p = tok;
          if(*p == '-') { neg=1; p++; }
          for(; *p; p++) {
            if(*p < '0' || *p > '9') {
              printf("Undefined word: %s\n", tok); goto next;
            }
            val = val*10 + (*p - '0');
          }
          dpush(neg ? -val : val);
        }
      }
    }
next:
    tok = mystrtok(0, delim);
  }
}

// ========== 初始化字典 ==========
void init_dict() {
  last_entry = -1;
  dict_here = 0;
  add_builtin("bye",   OP_BYE);
  add_builtin(".",     OP_DOT);
  add_builtin("emit",  OP_EMIT);
  add_builtin("cr",    OP_CR);
  add_builtin("+",     OP_PLUS);
  add_builtin("-",     OP_MINUS);
  add_builtin("*",     OP_STAR);
  add_builtin("/",     OP_SLASH);
  add_builtin("mod",   OP_MOD);
  add_builtin("=",     OP_EQUAL);
  add_builtin("<",     OP_LESS);
  add_builtin(">",     OP_GREATER);
  add_builtin("and",   OP_AND);
  add_builtin("or",    OP_OR);
  add_builtin("xor",   OP_XOR);
  add_builtin("not",   OP_NOT);
  add_builtin("dup",   OP_DUP);
  add_builtin("drop",  OP_DROP);
  add_builtin("swap",  OP_SWAP);
  add_builtin("over",  OP_OVER);
  add_builtin("rot",   OP_ROT);
  add_builtin("depth", OP_DEPTH);
  add_builtin("@",     OP_FETCH);
  add_builtin("!",     OP_STORE);
  add_builtin("c@",    OP_C_FETCH);
  add_builtin("c!",    OP_C_STORE);
  add_builtin("w@",    OP_W_FETCH);
  add_builtin("w!",    OP_W_STORE);
  add_builtin(">r",    OP_TOR);
  add_builtin("r>",    OP_FROMR);
  add_builtin("r@",    OP_RFETCH);
  add_builtin("words", OP_WORDS);
  add_builtin(".s",    OP_DOTS);
  add_builtin("dump",  OP_DUMP);
  add_builtin("inb",   OP_INB);
  add_builtin("outb",  OP_OUTB);
  add_builtin("inw",   OP_INW);
  add_builtin("outw",  OP_OUTW);
  add_builtin("shl",   OP_SHL);
  add_builtin("shr",   OP_SHR);
  add_builtin("branch",OP_BRANCH);
  add_builtin("0branch",OP_0BRANCH);
}

int main() {
  dsp = rsp = 0;
  compiling = 0;
  init_dict();

  printf("xv6 Forth Hardware Debugger (type 'bye' to exit)\n");
  printf("Memory: @ ! c@ c! w@ w! dump   I/O: inb outb inw outw\n");

  while(1) {
    printf("> ");
    get_line(input_buf, BUFLEN);
    if(input_buf[0] == 0) continue;
    outer_interpret();
    printf("\n");
  }
  return 0;
}