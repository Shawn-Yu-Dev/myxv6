// cvm.h - shared declarations for the c4 small C compiler
// Virtual machine opcodes, tokens, types, and AST node definitions.

#include "kernel/types.h"
#include "user/user.h"

typedef int _int; // real 32-bit int for external interfaces

#define int long long

// VM base registers for position-independent bytecodes
extern int *code_base;   // base address of code buffer (e[])
extern char *data_base;  // base address of data buffer (data[])

// Global variables
extern char *p, *lp,   // current position in source code
  *data;               // data/bss pointer

extern int *e, *le,    // current position in emitted code
  *id,                 // currently parsed identifier
  *sym,                // symbol table (simple list of identifiers)
  tk,                  // current token
  ival,                // current token value
  ty,                  // current expression type
  loc,                 // local variable offset
  line,                // current line number
  src,                 // print source and assembly flag
  debug;               // print executed instructions

// tokens and classes (operators last and in precedence order)
enum {
  Num = 128,
  Fun,
  Sys,
  Glo,
  Loc,
  Id,
  Char,
  Else,
  Enum,
  If,
  Int,
  Return,
  Sizeof,
  While,
  Assign,
  Cond,
  Lor,
  Lan,
  Or,
  Xor,
  And,
  Eq,
  Ne,
  Lt,
  Gt,
  Le,
  Ge,
  Shl,
  Shr,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Inc,
  Dec,
  Brak
};

// opcodes
enum {
  LEA,
  IMM,
  JMP,
  JSR,
  BZ,
  BNZ,
  ENT,
  ADJ,
  LEV,
  DADDR,   // load data address: a = (int)(data_base + *pc++)
  LI,
  LC,
  SI,
  SC,
  PSH,
  OR,
  XOR,
  AND,
  EQ,
  NE,
  LT,
  GT,
  LE,
  GE,
  SHL,
  SHR,
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  OPEN,
  READ,
  CLOS,
  PRTF,
  MALC,
  FREE,
  MSET,
  MCMP,
  EXIT
};

// types
enum { CHAR, INT, PTR };

// identifier offsets (since we can't create an ident struct)
enum { Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz };

// --- AST node types ---
enum {
  // Expression nodes (keep values distinct from tokens)
  AST_IMM = 512,
  AST_NAME,
  AST_STRING,
  AST_DEREF,
  AST_ADDR,
  AST_NEG,
  AST_NOT,
  AST_BNOT,
  AST_PREINC,
  AST_PREDEC,
  AST_POSTINC,
  AST_POSTDEC,
  AST_CALL,
  AST_ASSIGN,
  AST_COND,
  AST_LOR,
  AST_LAN,
  AST_OR,
  AST_XOR,
  AST_AND,
  AST_EQ,
  AST_NE,
  AST_LT,
  AST_GT,
  AST_LE,
  AST_GE,
  AST_SHL,
  AST_SHR,
  AST_ADD,
  AST_SUB,
  AST_MUL,
  AST_DIV,
  AST_MOD,
  // Statement nodes
  AST_STMT_COMP,
  AST_STMT_IF,
  AST_STMT_WHILE,
  AST_STMT_RETURN,
  AST_STMT_EXPR,
  AST_STMT_EMPTY,
  AST_STMT_DECL,
  // Top-level nodes
  AST_FUNC,
  AST_GLOBAL,
  AST_ENUM,
};

typedef struct AstNode {
  int kind, ty;
  long long ival;                      // literal value or symbol offset
  char *sval;                          // string literal data pointer
  int *sym;                            // symbol table entry pointer
  int srcline;                         // source line number
  struct AstNode *left, *right, *next; // tree + list links
} AstNode;

// --- source line index for -s listing ---
extern char *lines[65536];
extern int nlines;

// Function declarations
void next();
AstNode *expr(int lev);
AstNode *stmt();
AstNode *alloc_node(int kind);
AstNode *binop(int kind, AstNode *l, AstNode *r);
void walk_expr(AstNode *n);
void walk_stmt(AstNode *n);
int run_program(int *pc, int *sp, int poolsz);
int run_bytecode_file(char *filename);
void save_bytecode(char *filename, int entry_offset, int code_words, int data_words);
