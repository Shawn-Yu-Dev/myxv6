// forth.c -- 微型 Forth 解释器，适配 xv6 用户态
#include "kernel/types.h"
#include "user.h"

typedef unsigned long uintptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

// ========== 配置常量 ==========
#define DSIZE  128
#define RSIZE  128
#define DICTSIZE 2048
#define NAMELEN  16
#define BUFLEN   128

// ========== 虚拟机状态 ==========

#define HEAP_SIZE 256
int user_heap[HEAP_SIZE];        // 用于 @ ! 的安全内存
int heap_ptr = 0;                // 简单分配指针

int  dstack[DSIZE];
int  dsp;
int  rstack[RSIZE];
int  rsp;

char dict[DICTSIZE];
int  dict_here;

char input_buf[BUFLEN];

int last_entry = -1;   // 字典最新条目偏移

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
  OP_AND, OP_OR, OP_NOT,
  OP_DUP, OP_DROP, OP_SWAP, OP_OVER, OP_ROT, OP_DEPTH,
  // ---- 新增操作码 ----
  OP_FETCH,   // @  读取内存
  OP_STORE,   // !  写入内存
  OP_TOR,     // >r 压入返回栈
  OP_FROMR,   // r> 弹出返回栈
  OP_RFETCH,  // r@ 复制返回栈顶
  OP_WORDS,   // words 列出所有单词
  OP_DOTS,    // .s  打印数据栈快照
  // -------------------
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
  if(dict_here >= DICTSIZE) {
    printf("Dictionary overflow\n"); exit(0);
  }
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
    case OP_SLASH: b=dpop(); a=dpop(); if(b==0){printf("Division by zero\n");exit(0);} dpush(a/b); break;
    case OP_MOD: b=dpop(); a=dpop(); if(b==0){printf("Division by zero\n");exit(0);} dpush(a%b); break;
    case OP_EQUAL: b=dpop(); a=dpop(); dpush(a==b ? -1 : 0); break;
    case OP_LESS: b=dpop(); a=dpop(); dpush(a<b ? -1 : 0); break;
    case OP_GREATER: b=dpop(); a=dpop(); dpush(a>b ? -1 : 0); break;
    case OP_AND: b=dpop(); a=dpop(); dpush(a&b); break;
    case OP_OR: b=dpop(); a=dpop(); dpush(a|b); break;
    case OP_NOT: a=dpop(); dpush(~a); break;
    case OP_DUP: dpush(dtop()); break;
    case OP_DROP: dpop(); break;
    case OP_SWAP: a=dpop(); b=dpop(); dpush(a); dpush(b); break;
    case OP_OVER: a=dpop(); b=dtop(); dpush(a); dpush(b); break;
    case OP_ROT: { int c=dpop(), b=dpop(), a=dpop(); dpush(b); dpush(c); dpush(a); } break;
    case OP_DEPTH: dpush(dsp); break;

    // ---- 新增操作实现 ----
    case OP_FETCH: {   // @ ( idx -- n )  读取 user_heap[idx]
      int idx = dpop();
      if (idx < 0 || idx >= HEAP_SIZE) {
        printf("Invalid fetch index %d (max %d)\n", idx, HEAP_SIZE - 1);
        exit(0);
      }
      dpush(user_heap[idx]);
      break;
    }

    case OP_STORE: {   // ! ( n idx -- )  写入 n 到 user_heap[idx]
      int idx = dpop();
      if (idx < 0 || idx >= HEAP_SIZE) {
        printf("Invalid store index %d (max %d)\n", idx, HEAP_SIZE - 1);
        exit(0);
      }
      int val = dpop();
      user_heap[idx] = val;
      break;
    }

    case OP_TOR:     // >r ( n -- ) ( R: -- n )
      rpush(dpop());
      break;

    case OP_FROMR:   // r> ( -- n ) ( R: n -- )
      dpush(rpop());
      break;

    case OP_RFETCH:  // r@ ( -- n ) ( R: n -- n )
      if(rsp > 0) dpush(rstack[rsp-1]);
      else { printf("Return stack empty\n"); exit(0); }
      break;

    case OP_WORDS: { // words ( -- ) 打印所有已定义词
      int entry = last_entry;
      while(entry != -1) {
        printf("%s ", dict + entry + NAME_OFF);
        entry = *(int *)(dict + entry + LINK_OFF);
      }
      printf("\n");
      break;
    }

    case OP_DOTS:    // .s ( -- ) 打印数据栈快照
      printf("<%d> ", dsp);
      for(int i = 0; i < dsp; i++)
        printf("%d ", dstack[i]);
      printf("\n");
      break;
    // -------------------
    default: printf("Unknown built-in operation %d\n", op); exit(0);
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
    else {
      printf("Illegal instruction %d\n", op); exit(0);
    }
  }
}

// ---- 修改 add_builtin：branch/0branch 需要附加占位偏移量 ----
void add_builtin(const char *name, int op) {
  dict_new_linked(name, 0);
  dict_append_byte(op);
  // branch 和 0branch 后面必须跟着 4 字节偏移量，否则解释器会越界
  if (op == OP_BRANCH || op == OP_0BRANCH) {
    dict_append_int(0);   // 偏移量 0，相当于直接执行下一条指令
  }
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
  else if(strcmp(word, "not")==0) dict_append_byte(OP_NOT);
  else if(strcmp(word, "dup")==0) dict_append_byte(OP_DUP);
  else if(strcmp(word, "drop")==0) dict_append_byte(OP_DROP);
  else if(strcmp(word, "swap")==0) dict_append_byte(OP_SWAP);
  else if(strcmp(word, "over")==0) dict_append_byte(OP_OVER);
  else if(strcmp(word, "rot")==0) dict_append_byte(OP_ROT);
  else if(strcmp(word, "depth")==0) dict_append_byte(OP_DEPTH);
  // ---- 新增编译支持 ----
  else if(strcmp(word, "@")==0) dict_append_byte(OP_FETCH);
  else if(strcmp(word, "!")==0) dict_append_byte(OP_STORE);
  else if(strcmp(word, ">r")==0) dict_append_byte(OP_TOR);
  else if(strcmp(word, "r>")==0) dict_append_byte(OP_FROMR);
  else if(strcmp(word, "r@")==0) dict_append_byte(OP_RFETCH);
  else if(strcmp(word, "words")==0) dict_append_byte(OP_WORDS);
  else if(strcmp(word, ".s")==0) dict_append_byte(OP_DOTS);
  else if(strcmp(word, "branch")==0) dict_append_byte(OP_BRANCH);
  else if(strcmp(word, "0branch")==0) dict_append_byte(OP_0BRANCH);
  // -------------------
  else {
    int entry = dict_find(word);
    if(entry != -1) {
      printf("User vocabulary retrieval has not yet been implemented.\n"); exit(0);
    } else {
      int val = 0, neg = 0;
      const char *p = word;
      if(*p == '-') { neg=1; p++; }
      for(; *p; p++) {
        if(*p < '0' || *p > '9') {
          printf("Undefined words: %s\n", word); exit(0);
        }
        val = val*10 + (*p - '0');
      }
      compile_lit(neg ? -val : val);
    }
  }
}

// ========== 简单的字符串分词器 (替代 strtok) ==========
char *mystrtok(char *str, const char *delim) {
  static char *pos;
  if(str) pos = str;
  if(!pos) return NULL;

  // 跳过开头的分隔符
  while(*pos && strchr(delim, *pos)) pos++;
  if(*pos == '\0') return NULL;

  char *start = pos;
  // 找到下一个分隔符
  while(*pos && !strchr(delim, *pos)) pos++;
  if(*pos) {
    *pos = '\0';
    pos++;
  }
  return start;
}

// ========== 外层解释器 ==========
void outer_interpret() {
  char *delim = " \t\n";
  char *tok = mystrtok(input_buf, delim);
  while(tok != NULL) {
    if(compiling) {
      if(strcmp(tok, ";") == 0) {
        finish_compile();
      } else {
        compile_word(tok);
      }
    } else {
      if(strcmp(tok, ":") == 0) {
        tok = mystrtok(NULL, delim);
        if(tok == NULL) { printf("Missing name\n"); return; }
        start_compile(tok);
      } else if(strcmp(tok, "bye")==0) {
        exit(0);
      // 原来的 "words" 直接处理已移除，改为通过内建词 OP_WORDS 执行
      } else {
        int entry = dict_find(tok);
        if(entry != -1) {
          interpret(entry);
        } else {
          int val = 0, neg = 0;
          const char *p = tok;
          if(*p == '-') { neg=1; p++; }
          for(; *p; p++) {
            if(*p < '0' || *p > '9') {
              printf("Undefined word: %s\n", tok); break;
            }
            val = val*10 + (*p - '0');
          }
          dpush(neg ? -val : val);
        }
      }
    }
    tok = mystrtok(NULL, delim);
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
  add_builtin("not",   OP_NOT);
  add_builtin("dup",   OP_DUP);
  add_builtin("drop",  OP_DROP);
  add_builtin("swap",  OP_SWAP);
  add_builtin("over",  OP_OVER);
  add_builtin("rot",   OP_ROT);
  add_builtin("depth", OP_DEPTH);

  // ---- 新增内建词 ----
  add_builtin("@",     OP_FETCH);
  add_builtin("!",     OP_STORE);
  add_builtin(">r",    OP_TOR);
  add_builtin("r>",    OP_FROMR);
  add_builtin("r@",    OP_RFETCH);
  add_builtin("words", OP_WORDS);
  add_builtin(".s",    OP_DOTS);
  add_builtin("branch",  OP_BRANCH);
  add_builtin("0branch", OP_0BRANCH);
  // -------------------
}

int main() {
  dsp = 0;
  rsp = 0;
  compiling = 0;
  init_dict();

  printf("xv6 Forth micro-interpreter (enter 'bye' to exit)\n");

  while(1) {
    printf("> ");
    gets(input_buf, BUFLEN);
    if(input_buf[0] == 0) continue;
    outer_interpret();
    printf("\n");
  }
}