// c4.c - C in four functions

// char, int, and pointer types
// if, while, return, and expression statements
// just enough features to allow self-compilation and a bit more

// Written by Robert Swierczek
// Ported to xv6-riscv

#include "kernel/types.h"
#include "user/user.h"

typedef int _int; // real 32-bit int for external interfaces

#define int long long

char *p, *lp, // current position in source code
  *data;      // data/bss pointer

int *e, *le, // current position in emitted code
  *id,       // currently parsed identifier
  *sym,      // symbol table (simple list of identifiers)
  tk,        // current token
  ival,      // current token value
  ty,        // current expression type
  loc,       // local variable offset
  line,      // current line number
  src,       // print source and assembly flag
  debug;     // print executed instructions

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

AstNode *
alloc_node(int kind)
{
  AstNode *n = (AstNode *)malloc(sizeof(AstNode));
  memset(n, 0, sizeof(AstNode));
  n->kind = kind;
  n->srcline = line;
  return n;
}

// Build a binary AST node
AstNode *
binop(int kind, AstNode *l, AstNode *r)
{
  AstNode *n = alloc_node(kind);
  n->left = l;
  n->right = r;
  return n;
}

// --- source line index for -s listing ---
char *lines[65536];
int nlines;

// --- AST walker (forward declarations) ---
void walk_expr(AstNode *n);
void walk_stmt(AstNode *n);

void
next()
{
  char *pp;

  while ((tk = *p) != 0) {
    ++p;
    if (tk == '\n') {
      lines[line] = lp;
      lp = p;
      ++line;
    } else if (tk == '#') {
      while (*p != 0 && *p != '\n')
        ++p;
    } else if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') ||
               tk == '_') {
      pp = p - 1;
      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
             (*p >= '0' && *p <= '9') || *p == '_')
        tk = tk * 147 + *p++;
      tk = (tk << 6) + (p - pp);
      id = sym;
      while (id[Tk]) {
        if (tk == id[Hash] && !memcmp((char *)id[Name], pp, p - pp)) {
          tk = id[Tk];
          return;
        }
        id = id + Idsz;
      }
      id[Name] = (int)pp;
      id[Hash] = tk;
      tk = id[Tk] = Id;
      return;
    } else if (tk >= '0' && tk <= '9') {
      if ((ival = tk - '0') != 0) {
        while (*p >= '0' && *p <= '9')
          ival = ival * 10 + *p++ - '0';
      } else if (*p == 'x' || *p == 'X') {
        while ((tk = *++p) &&
               ((tk >= '0' && tk <= '9') || (tk >= 'a' && tk <= 'f') ||
                (tk >= 'A' && tk <= 'F')))
          ival = ival * 16 + (tk & 15) + (tk >= 'A' ? 9 : 0);
      } else {
        while (*p >= '0' && *p <= '7')
          ival = ival * 8 + *p++ - '0';
      }
      tk = Num;
      return;
    } else if (tk == '/') {
      if (*p == '/') {
        ++p;
        while (*p != 0 && *p != '\n')
          ++p;
      } else {
        tk = Div;
        return;
      }
    } else if (tk == '\'' || tk == '"') {
      pp = data;
      while (*p != 0 && *p != tk) {
        if ((ival = *p++) == '\\') {
          if ((ival = *p++) == 'n')
            ival = '\n';
        }
        if (tk == '"')
          *data++ = ival;
      }
      ++p;
      if (tk == '"')
        ival = (int)pp;
      else
        tk = Num;
      return;
    } else if (tk == '=') {
      if (*p == '=') {
        ++p;
        tk = Eq;
      } else
        tk = Assign;
      return;
    } else if (tk == '+') {
      if (*p == '+') {
        ++p;
        tk = Inc;
      } else
        tk = Add;
      return;
    } else if (tk == '-') {
      if (*p == '-') {
        ++p;
        tk = Dec;
      } else
        tk = Sub;
      return;
    } else if (tk == '!') {
      if (*p == '=') {
        ++p;
        tk = Ne;
      }
      return;
    } else if (tk == '<') {
      if (*p == '=') {
        ++p;
        tk = Le;
      } else if (*p == '<') {
        ++p;
        tk = Shl;
      } else
        tk = Lt;
      return;
    } else if (tk == '>') {
      if (*p == '=') {
        ++p;
        tk = Ge;
      } else if (*p == '>') {
        ++p;
        tk = Shr;
      } else
        tk = Gt;
      return;
    } else if (tk == '|') {
      if (*p == '|') {
        ++p;
        tk = Lor;
      } else
        tk = Or;
      return;
    } else if (tk == '&') {
      if (*p == '&') {
        ++p;
        tk = Lan;
      } else
        tk = And;
      return;
    } else if (tk == '^') {
      tk = Xor;
      return;
    } else if (tk == '%') {
      tk = Mod;
      return;
    } else if (tk == '*') {
      tk = Mul;
      return;
    } else if (tk == '[') {
      tk = Brak;
      return;
    } else if (tk == '?') {
      tk = Cond;
      return;
    } else if (tk == '~' || tk == ';' || tk == '{' || tk == '}' || tk == '(' ||
               tk == ')' || tk == ']' || tk == ',' || tk == ':')
      return;
  }
}

AstNode *
expr(int lev)
{
  AstNode *n, *n2, *l;
  int t, *id_saved;

  if (!tk) {
    printf("%d: unexpected eof in expression\n", (_int)line);
    exit(-1);
  } else if (tk == Num) {
    n = alloc_node(AST_IMM);
    n->ival = ival;
    n->ty = INT;
    next();
  } else if (tk == '"') {
    n = alloc_node(AST_STRING);
    n->ival = ival;
    n->sval = (char *)ival;
    next();
    while (tk == '"')
      next();
    data = (char *)(((int)data + sizeof(int)) & -sizeof(int));
    n->ty = PTR;
  } else if (tk == Sizeof) {
    next();
    if (tk == '(')
      next();
    else {
      printf("%d: open paren expected in sizeof\n", (_int)line);
      exit(-1);
    }
    t = INT;
    if (tk == Int)
      next();
    else if (tk == Char) {
      next();
      t = CHAR;
    }
    while (tk == Mul) {
      next();
      t = t + PTR;
    }
    if (tk == ')')
      next();
    else {
      printf("%d: close paren expected in sizeof\n", (_int)line);
      exit(-1);
    }
    n = alloc_node(AST_IMM);
    n->ival = (t == CHAR) ? sizeof(char) : sizeof(int);
    n->ty = INT;
  } else if (tk == Id) {
    id_saved = id;
    next();
    if (tk == '(') {
      // Function call
      next();
      l = 0;
      n2 = 0;
      {
        AstNode *first_arg = 0, *last_arg = 0;
        while (tk != ')') {
          n2 = expr(Assign);
          if (!first_arg)
            first_arg = n2;
          else
            last_arg->next = n2;
          last_arg = n2;
          if (tk == ',')
            next();
        }
        l = first_arg;
      }
      next();
      n = alloc_node(AST_CALL);
      n->sym = id_saved;
      n->left = l;
      n->ty = id_saved[Type];
    } else if (id_saved[Class] == Num) {
      n = alloc_node(AST_IMM);
      n->ival = id_saved[Val];
      n->ty = INT;
    } else {
      n = alloc_node(AST_NAME);
      n->sym = id_saved;
      if (id_saved[Class] == Loc)
        n->ival = loc - id_saved[Val];
      else if (id_saved[Class] == Glo)
        n->ival = id_saved[Val];
      else {
        printf("%d: undefined variable\n", (_int)line);
        exit(-1);
      }
      n->ty = id_saved[Type];
    }
  } else if (tk == '(') {
    next();
    if (tk == Int || tk == Char) {
      t = (tk == Int) ? INT : CHAR;
      next();
      while (tk == Mul) {
        next();
        t = t + PTR;
      }
      if (tk == ')')
        next();
      else {
        printf("%d: bad cast\n", (_int)line);
        exit(-1);
      }
      n = expr(Inc);
      n->ty = t;
    } else {
      n = expr(Assign);
      if (tk == ')')
        next();
      else {
        printf("%d: close paren expected\n", (_int)line);
        exit(-1);
      }
    }
  } else if (tk == Mul) {
    next();
    n = alloc_node(AST_DEREF);
    n->left = expr(Inc);
    if (n->left->ty > INT)
      n->ty = n->left->ty - PTR;
    else {
      printf("%d: bad dereference\n", (_int)line);
      exit(-1);
    }
  } else if (tk == And) {
    next();
    n = alloc_node(AST_ADDR);
    n->left = expr(Inc);
    if (n->left->kind != AST_NAME) {
      printf("%d: bad address-of\n", (_int)line);
      exit(-1);
    }
    n->ty = n->left->ty + PTR;
  } else if (tk == '!') {
    next();
    n = alloc_node(AST_NOT);
    n->left = expr(Inc);
    n->ty = INT;
  } else if (tk == '~') {
    next();
    n = alloc_node(AST_BNOT);
    n->left = expr(Inc);
    n->ty = INT;
  } else if (tk == Add) {
    next();
    n = expr(Inc);
    n->ty = INT;
  } else if (tk == Sub) {
    next();
    if (tk == Num) {
      n = alloc_node(AST_IMM);
      n->ival = -ival;
      next();
    } else {
      n = alloc_node(AST_NEG);
      n->left = expr(Inc);
    }
    n->ty = INT;
  } else if (tk == Inc || tk == Dec) {
    t = tk;
    next();
    n = alloc_node((t == Inc) ? AST_PREINC : AST_PREDEC);
    n->left = expr(Inc);
    if (n->left->kind != AST_NAME && n->left->kind != AST_DEREF) {
      printf("%d: bad lvalue in pre-increment\n", (_int)line);
      exit(-1);
    }
    n->ty = n->left->ty;
  } else {
    printf("%d: bad expression\n", (_int)line);
    exit(-1);
  }

  while (tk >= lev) { // precedence climbing
    t = n->ty;
    if (tk == Assign) {
      next();
      if (n->kind != AST_NAME && n->kind != AST_DEREF) {
        printf("%d: bad lvalue in assignment\n", (_int)line);
        exit(-1);
      }
      n2 = binop(AST_ASSIGN, n, expr(Assign));
      n2->ty = n->ty;
      n = n2;
    } else if (tk == Cond) {
      next();
      n2 = alloc_node(AST_COND);
      n2->left = n;
      n2->right = expr(Assign);
      if (tk == ':')
        next();
      else {
        printf("%d: conditional missing colon\n", (_int)line);
        exit(-1);
      }
      n2->next = expr(Cond);
      n2->ty = INT;
      n = n2;
    } else if (tk == Lor) {
      next();
      n = binop(AST_LOR, n, expr(Lan));
      n->ty = INT;
    } else if (tk == Lan) {
      next();
      n = binop(AST_LAN, n, expr(Or));
      n->ty = INT;
    } else if (tk == Or) {
      next();
      n = binop(AST_OR, n, expr(Xor));
      n->ty = INT;
    } else if (tk == Xor) {
      next();
      n = binop(AST_XOR, n, expr(And));
      n->ty = INT;
    } else if (tk == And) {
      next();
      n = binop(AST_AND, n, expr(Eq));
      n->ty = INT;
    } else if (tk == Eq) {
      next();
      n = binop(AST_EQ, n, expr(Lt));
      n->ty = INT;
    } else if (tk == Ne) {
      next();
      n = binop(AST_NE, n, expr(Lt));
      n->ty = INT;
    } else if (tk == Lt) {
      next();
      n = binop(AST_LT, n, expr(Shl));
      n->ty = INT;
    } else if (tk == Gt) {
      next();
      n = binop(AST_GT, n, expr(Shl));
      n->ty = INT;
    } else if (tk == Le) {
      next();
      n = binop(AST_LE, n, expr(Shl));
      n->ty = INT;
    } else if (tk == Ge) {
      next();
      n = binop(AST_GE, n, expr(Shl));
      n->ty = INT;
    } else if (tk == Shl) {
      next();
      n = binop(AST_SHL, n, expr(Add));
      n->ty = INT;
    } else if (tk == Shr) {
      next();
      n = binop(AST_SHR, n, expr(Add));
      n->ty = INT;
    } else if (tk == Add) {
      next();
      n2 = binop(AST_ADD, n, expr(Mul));
      n2->ty = (t > PTR) ? t : INT;
      n = n2;
    } else if (tk == Sub) {
      next();
      n2 = binop(AST_SUB, n, expr(Mul));
      n2->ty = (t > PTR) ? t : INT;
      n = n2;
    } else if (tk == Mul) {
      next();
      n = binop(AST_MUL, n, expr(Inc));
      n->ty = INT;
    } else if (tk == Div) {
      next();
      n = binop(AST_DIV, n, expr(Inc));
      n->ty = INT;
    } else if (tk == Mod) {
      next();
      n = binop(AST_MOD, n, expr(Inc));
      n->ty = INT;
    } else if (tk == Inc || tk == Dec) {
      n2 = alloc_node((tk == Inc) ? AST_POSTINC : AST_POSTDEC);
      n2->left = n;
      if (n2->left->kind != AST_NAME && n2->left->kind != AST_DEREF) {
        printf("%d: bad lvalue in post-increment\n", (_int)line);
        exit(-1);
      }
      n2->ty = n->ty;
      n = n2;
      next();
    } else if (tk == Brak) {
      next();
      n2 = alloc_node(AST_DEREF);
      n2->left = binop(AST_ADD, n, expr(Assign));
      if (tk == ']')
        next();
      else {
        printf("%d: close bracket expected\n", (_int)line);
        exit(-1);
      }
      if (t > PTR)
        n2->ty = t - PTR;
      else if (t == PTR)
        n2->ty = INT;
      else {
        printf("%d: pointer type expected\n", (_int)line);
        exit(-1);
      }
      n = n2;
    } else {
      printf("%d: compiler error tk=%d\n", (_int)line, (_int)tk);
      exit(-1);
    }
  }
  ty = n->ty;
  return n;
}

AstNode *
stmt()
{
  AstNode *n, *n2, *last;

  if (tk == If) {
    n = alloc_node(AST_STMT_IF);
    next();
    if (tk == '(')
      next();
    else {
      printf("%d: open paren expected\n", (_int)line);
      exit(-1);
    }
    n->left = expr(Assign);
    if (tk == ')')
      next();
    else {
      printf("%d: close paren expected\n", (_int)line);
      exit(-1);
    }
    n->right = stmt();
    if (tk == Else) {
      next();
      n->next = stmt();
    }
    return n;
  } else if (tk == While) {
    n = alloc_node(AST_STMT_WHILE);
    next();
    if (tk == '(')
      next();
    else {
      printf("%d: open paren expected\n", (_int)line);
      exit(-1);
    }
    n->left = expr(Assign);
    if (tk == ')')
      next();
    else {
      printf("%d: close paren expected\n", (_int)line);
      exit(-1);
    }
    n->right = stmt();
    return n;
  } else if (tk == Return) {
    n = alloc_node(AST_STMT_RETURN);
    next();
    if (tk != ';')
      n->left = expr(Assign);
    if (tk == ';')
      next();
    else {
      printf("%d: semicolon expected\n", (_int)line);
      exit(-1);
    }
    return n;
  } else if (tk == '{') {
    n = alloc_node(AST_STMT_COMP);
    last = 0;
    next();
    while (tk != '}') {
      n2 = stmt();
      if (!last)
        n->left = n2;
      else
        last->next = n2;
      last = n2;
    }
    next();
    return n;
  } else if (tk == ';') {
    n = alloc_node(AST_STMT_EMPTY);
    next();
    return n;
  } else {
    n = alloc_node(AST_STMT_EXPR);
    n->left = expr(Assign);
    if (tk == ';')
      next();
    else {
      printf("%d: semicolon expected\n", (_int)line);
      exit(-1);
    }
    return n;
  }
}

// ========== AST WALKER (emits VM bytecodes into e[]) ==========

static void
list_opcodes()
{
  while (le < e) {
    printf(
      "%8.4s",
      &"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,"
       "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
       "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,"[*++le * 5]);
    if (*le <= ADJ)
      printf(" %d\n", (_int) * ++le);
    else
      printf("\n");
  }
}

// Walk an expression node, emitting VM bytecodes
void
walk_expr(AstNode *n)
{
  int *d1, *d2, t;
  if (!n)
    return;
  switch (n->kind) {
  case AST_IMM:
    *++e = IMM;
    *++e = (int)n->ival;
    break;
  case AST_STRING:
    *++e = IMM;
    *++e = (int)n->ival;
    break;
  case AST_NAME:
    if (n->sym[Class] == Loc)
      *++e = LEA;
    else
      *++e = IMM;
    *++e = (int)n->ival;
    *++e = (n->ty == CHAR) ? LC : LI;
    break;
  case AST_DEREF:
    walk_expr(n->left);
    *++e = (n->ty == CHAR) ? LC : LI;
    break;
  case AST_ADDR:
    // Address-of: walk left as lvalue, strip the final load
    if (n->left->kind == AST_NAME) {
      if (n->left->sym[Class] == Loc) {
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {
        *++e = IMM;
        *++e = (int)n->left->ival;
      }
    } else {
      walk_expr(n->left);
    }
    break;
  case AST_NEG:
    *++e = IMM;
    *++e = -1;
    *++e = PSH;
    walk_expr(n->left);
    *++e = MUL;
    break;
  case AST_NOT:
    walk_expr(n->left);
    *++e = PSH;
    *++e = IMM;
    *++e = 0;
    *++e = EQ;
    break;
  case AST_BNOT:
    walk_expr(n->left);
    *++e = PSH;
    *++e = IMM;
    *++e = -1;
    *++e = XOR;
    break;
  case AST_PREINC:
  case AST_PREDEC: {
    // ++a: push address, load value, add size, store
    int sz = (n->left->ty > PTR) ? sizeof(int) : sizeof(char);
    if (n->left->kind == AST_NAME) {
      if (n->left->sym[Class] == Loc) {
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {
        *++e = IMM;
        *++e = (int)n->left->ival;
      }
    } else {
      walk_expr(n->left->left);
    }
    *++e = PSH;
    walk_expr(n->left); // load old value
    *++e = PSH;
    *++e = IMM;
    *++e = sz;
    *++e = (n->kind == AST_PREINC) ? ADD : SUB;
    *++e = (n->left->ty == CHAR) ? SC : SI;
    break;
  }
  case AST_POSTINC:
  case AST_POSTDEC: {
    int sz = (n->left->ty > PTR) ? sizeof(int) : sizeof(char);
    // push address
    if (n->left->kind == AST_NAME) {
      if (n->left->sym[Class] == Loc) {
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {
        *++e = IMM;
        *++e = (int)n->left->ival;
      }
    } else {
      walk_expr(n->left->left);
    }
    *++e = PSH;         // push old value
    walk_expr(n->left); // load value (end: addr; load)
    // Now: LEA addr; PSH; LEA addr; LC; result = loaded value, stack has addr
    // We need: push loaded, add size, store, result = loaded - size
    *++e = PSH;
    *++e = IMM;
    *++e = sz;
    *++e = (n->kind == AST_POSTINC) ? ADD : SUB;
    *++e = (n->left->ty == CHAR) ? SC : SI;
    *++e = PSH;
    *++e = IMM;
    *++e = sz;
    *++e = (n->kind == AST_POSTINC) ? SUB : ADD;
    break;
  }
  case AST_CALL: {
    int nargs = 0;
    AstNode *a = n->left;
    // Walk args in reverse order (rightmost first) to match current behavior
    // Args are stored with ->next pointing to the previous arg
    // First find the last arg
    while (a) {
      nargs++;
      a = a->next;
    }
    // Now emit args: we need them in source order, but the current code emits
    // left-to-right and pushes each. Our AST stores args in source order too.
    a = n->left;
    while (a) {
      walk_expr(a);
      *++e = PSH;
      a = a->next;
    }
    if (n->sym[Class] == Sys)
      *++e = n->sym[Val];
    else if (n->sym[Class] == Fun) {
      *++e = JSR;
      *++e = n->sym[Val];
    } else {
      printf("%d: bad function call\n", (_int)n->srcline);
      exit(-1);
    }
    if (nargs) {
      *++e = ADJ;
      *++e = nargs;
    }
    break;
  }
  case AST_COND: {
    walk_expr(n->left); // condition
    *++e = BZ;
    d1 = ++e;            // if false -> else
    walk_expr(n->right); // then
    *d1 = (int)(e + 3);
    *++e = JMP;
    d2 = ++e;
    walk_expr(n->next); // else
    *d2 = (int)(e + 1);
    break;
  }
  case AST_LOR:
    walk_expr(n->left);
    *++e = BNZ;
    d1 = ++e;
    walk_expr(n->right);
    *d1 = (int)(e + 1);
    break;
  case AST_LAN:
    walk_expr(n->left);
    *++e = BZ;
    d1 = ++e;
    walk_expr(n->right);
    *d1 = (int)(e + 1);
    break;
  case AST_ASSIGN: {
    // left side: push address
    if (n->left->kind == AST_NAME) {
      if (n->left->sym[Class] == Loc) {
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {
        *++e = IMM;
        *++e = (int)n->left->ival;
      }
    } else {
      walk_expr(n->left);
    }
    // Convert load to push: if n->left has a trailing LC/LI, it's wrong for lvalue
    // But we just emitted address above, so this is fine
    *++e = PSH;
    walk_expr(n->right);
    *++e = (n->left->ty == CHAR) ? SC : SI;
    break;
  }
  case AST_ADD:
    t = n->left->ty;
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    if (t > PTR) {
      *++e = PSH;
      *++e = IMM;
      *++e = sizeof(int);
      *++e = MUL;
    }
    *++e = ADD;
    break;
  case AST_SUB:
    t = n->left->ty;
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    if (t > PTR && t == n->right->ty) {
      *++e = SUB;
      *++e = PSH;
      *++e = IMM;
      *++e = sizeof(int);
      *++e = DIV;
    } else if (t > PTR) {
      *++e = PSH;
      *++e = IMM;
      *++e = sizeof(int);
      *++e = MUL;
      *++e = SUB;
    } else
      *++e = SUB;
    break;
  case AST_MUL:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = MUL;
    break;
  case AST_DIV:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = DIV;
    break;
  case AST_MOD:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = MOD;
    break;
  case AST_OR:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = OR;
    break;
  case AST_XOR:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = XOR;
    break;
  case AST_AND:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = AND;
    break;
  case AST_EQ:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = EQ;
    break;
  case AST_NE:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = NE;
    break;
  case AST_LT:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = LT;
    break;
  case AST_GT:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = GT;
    break;
  case AST_LE:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = LE;
    break;
  case AST_GE:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = GE;
    break;
  case AST_SHL:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = SHL;
    break;
  case AST_SHR:
    walk_expr(n->left);
    *++e = PSH;
    walk_expr(n->right);
    *++e = SHR;
    break;
  }
}

// Walk a statement node, emitting VM bytecodes
void
walk_stmt(AstNode *n)
{
  int *a, *b;
  if (!n)
    return;
  switch (n->kind) {
  case AST_STMT_EXPR:
    walk_expr(n->left);
    break;
  case AST_STMT_EMPTY:
    break;
  case AST_STMT_RETURN:
    if (n->left)
      walk_expr(n->left);
    *++e = LEV;
    break;
  case AST_STMT_IF: {
    walk_expr(n->left); // condition
    *++e = BZ;
    b = ++e;
    walk_stmt(n->right); // then
    if (n->next) {
      *b = (int)(e + 3);
      *++e = JMP;
      b = ++e;
      walk_stmt(n->next); // else
    }
    *b = (int)(e + 1);
    break;
  }
  case AST_STMT_WHILE: {
    a = e + 1; // top of condition
    walk_expr(n->left);
    *++e = BZ;
    b = ++e;
    walk_stmt(n->right);
    *++e = JMP;
    *++e = (int)a;
    *b = (int)(e + 1);
    break;
  }
  case AST_STMT_COMP: {
    AstNode *c = n->left;
    while (c) {
      walk_stmt(c);
      c = c->next;
    }
    break;
  }
  }
}

_int
main(_int argc, char **argv)
{
  int fd, bt, ty, poolsz, *idmain;
  int *pc, *sp, *bp, a = 0, cycle;                 // vm registers
  int i, *t, *fn_id;                                   // temps
  AstNode *prog_root = 0, *prog_last = 0, *fn; // program AST list

  --argc;
  ++argv;
  if (argc > 0 && **argv == '-' && (*argv)[1] == 's') {
    src = 1;
    --argc;
    ++argv;
  }
  if (argc > 0 && **argv == '-' && (*argv)[1] == 'd') {
    debug = 1;
    --argc;
    ++argv;
  }
  if (argc < 1) {
    printf("usage: c4 [-s] [-d] file ...\n");
    return -1;
  }

  if ((fd = open(*argv, 0)) < 0) {
    printf("could not open(%s)\n", *argv);
    return -1;
  }

  poolsz = 256 * 1024; // arbitrary size
  if (!(sym = malloc((uint)poolsz))) {
    printf("could not malloc(%d) symbol area\n", (_int)poolsz);
    return -1;
  }
  if (!(le = e = malloc((uint)poolsz))) {
    printf("could not malloc(%d) text area\n", (_int)poolsz);
    return -1;
  }
  if (!(data = malloc((uint)poolsz))) {
    printf("could not malloc(%d) data area\n", (_int)poolsz);
    return -1;
  }
  if (!(sp = malloc((uint)poolsz))) {
    printf("could not malloc(%d) stack area\n", (_int)poolsz);
    return -1;
  }

  memset(sym, 0, poolsz);
  memset(e, 0, poolsz);
  memset(data, 0, poolsz);

  p = "char else enum if int return sizeof while "
      "open read close printf malloc free memset memcmp exit void main";
  i = Char;
  while (i <= While) {
    next();
    id[Tk] = i++;
  } // add keywords to symbol table
  i = OPEN;
  while (i <= EXIT) {
    next();
    id[Class] = Sys;
    id[Type] = INT;
    id[Val] = i++;
  } // add library to symbol table
  next();
  id[Tk] = Char; // handle void type
  next();
  idmain = id; // keep track of main

  if (!(lp = p = malloc((uint)poolsz))) {
    printf("could not malloc(%d) source area\n", (_int)poolsz);
    return -1;
  }
  if ((i = read(fd, p, (int)(poolsz - 1))) <= 0) {
    printf("read() returned %d\n", (_int)i);
    return -1;
  }
  p[i] = 0;
  close(fd);

  // Phase 1: Parse declarations into AST
  line = 1;
  next();
  while (tk) {
    bt = INT; // basetype
    if (tk == Int)
      next();
    else if (tk == Char) {
      next();
      bt = CHAR;
    } else if (tk == Enum) {
      next();
      if (tk != '{')
        next();
      if (tk == '{') {
        next();
        i = 0;
        while (tk != '}') {
          if (tk != Id) {
            printf("%d: bad enum identifier %d\n", (_int)line, (_int)tk);
            return -1;
          }
          next();
          if (tk == Assign) {
            next();
            if (tk != Num) {
              printf("%d: bad enum initializer\n", (_int)line);
              return -1;
            }
            i = ival;
            next();
          }
          id[Class] = Num;
          id[Type] = INT;
          id[Val] = i++;
          if (tk == ',')
            next();
        }
        next();
      }
    }
    while (tk != ';' && tk != '}' && tk) {
      ty = bt;
      while (tk == Mul) {
        next();
        ty = ty + PTR;
      }
      if (tk != Id) {
        printf("%d: bad global declaration\n", (_int)line);
        return -1;
      }
      if (id[Class]) {
        printf("%d: duplicate global definition\n", (_int)line);
        return -1;
      }
      next();
      id[Type] = ty;
      if (tk == '(') { // function
        id[Class] = Fun;
        id[Val] = 0; // will be filled in during walk
        next();
        i = 0;
        while (tk != ')') {
          ty = INT;
          if (tk == Int)
            next();
          else if (tk == Char) {
            next();
            ty = CHAR;
          }
          while (tk == Mul) {
            next();
            ty = ty + PTR;
          }
          if (tk != Id) {
            printf("%d: bad parameter declaration\n", (_int)line);
            return -1;
          }
          if (id[Class] == Loc) {
            printf("%d: duplicate parameter definition\n", (_int)line);
            return -1;
          }
          id[HClass] = id[Class];
          id[Class] = Loc;
          id[HType] = id[Type];
          id[Type] = ty;
          id[HVal] = id[Val];
          id[Val] = i++;
          next();
          if (tk == ',')
            next();
        }
        next();
        if (tk != '{') {
          printf("%d: bad function definition\n", (_int)line);
          return -1;
        }
        loc = ++i;
        fn_id = id; // save function's symbol entry BEFORE next() changes id
        next();
          while (tk == Int || tk == Char) {
          bt = (tk == Int) ? INT : CHAR;
          next();
          while (tk != ';') {
            ty = bt;
            while (tk == Mul) {
              next();
              ty = ty + PTR;
            }
            if (tk != Id) {
              printf("%d: bad local declaration\n", (_int)line);
              return -1;
            }
            if (id[Class] == Loc) {
              printf("%d: duplicate local definition\n", (_int)line);
              return -1;
            }
            id[HClass] = id[Class];
            id[Class] = Loc;
            id[HType] = id[Type];
            id[Type] = ty;
            id[HVal] = id[Val];
            id[Val] = ++i;
            next();
            if (tk == ',')
              next();
          }
          next();
        }
        // Build function body AST
        fn = alloc_node(AST_FUNC);
        fn->left = 0; // will hold body statements
        fn->sym = fn_id;
        fn->ival = i - loc; // store local variable count
        // Parse body into compound statement AST
        {
          AstNode *last = 0;
          while (tk != '}') {
            AstNode *s = stmt();
            if (!fn->left)
              fn->left = s;
            else
              last->next = s;
            last = s;
          }
          next();
        }
        // Append to program list
        if (!prog_root)
          prog_root = fn;
        else
          prog_last->next = fn;
        prog_last = fn;
        // Unwind symbol table locals
        id = sym;
        while (id[Tk]) {
          if (id[Class] == Loc) {
            id[Class] = id[HClass];
            id[Type] = id[HType];
            id[Val] = id[HVal];
          }
          id = id + Idsz;
        }
      } else {
        id[Class] = Glo;
        id[Val] = (int)data;
        data = data + sizeof(int);
      }
      if (tk == ',')
        next();
    }
    next();
  }

  // Phase 2: Walk the program AST to emit bytecodes
  le = e; // reset listing pointer
  {
    AstNode *walker = prog_root;
    while (walker) {
      if (walker->kind == AST_FUNC) {
        // Emit function entry: ENT
        AstNode *body = walker->left;
        int *sym_entry = walker->sym;
        int nlocals = (int)walker->ival; // stored during parsing
        if (src) {
          printf("%d: %.*s", (_int)walker->srcline,
                 (_int)(p - lines[walker->srcline]), lines[walker->srcline]);
          list_opcodes();
        }
        sym_entry[Val] = (int)(e + 1); // set function address
        *++e = ENT;
        *++e = nlocals;
        // Walk statements
        while (body) {
          walk_stmt(body);
          body = body->next;
        }
        *++e = LEV;
      }
      walker = walker->next;
    }
  }

  // After all functions, emit JSR trampoline to main
  // This creates a proper return frame so LEV works correctly
  int *jsr_addr = e + 1;
  *++e = JSR;
  *++e = 0; // placeholder, filled below
  *++e = EXIT;

  // Now fill in main's address and set pc
  if (!idmain[Val]) {
    printf("main() not defined\n");
    return -1;
  }
  jsr_addr[1] = (int)idmain[Val]; // fill in JSR target

  if (src)
    return 0;

  // setup stack: push argc/argv for main's params
  bp = sp = (int *)((int)sp + poolsz);
  sp = (int *)((char *)sp - sizeof(int));
  *sp = argc;
  sp = (int *)((char *)sp - sizeof(int));
  *sp = (int)argv;

  // start executing from JSR
  pc = jsr_addr;

  // run...
  cycle = 0;
  while (1) {
    i = *pc++;
    ++cycle;
    if (debug) {
      printf(
        "%d> %.4s", (_int)cycle,
        &"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,"
         "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
         "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,"[i * 5]);
      if (i <= ADJ)
        printf(" %d\n", (_int)*pc);
      else
        printf("\n");
    }
    if (i == LEA)
      a = (int)(bp + *pc++); // load local address
    else if (i == IMM)
      a = *pc++; // load global address or immediate
    else if (i == JMP)
      pc = (int *)*pc; // jump
    else if (i == JSR) {
      sp = (int *)((char *)sp - sizeof(int));
      *sp = (int)(pc + 1);
      pc = (int *)*pc;
    } // jump to subroutine
    else if (i == BZ)
      pc = a ? pc + 1 : (int *)*pc; // branch if zero
    else if (i == BNZ)
      pc = a ? (int *)*pc : pc + 1; // branch if not zero
    else if (i == ENT) {
      sp = (int *)((char *)sp - sizeof(int));
      *sp = (int)bp;
      bp = sp;
      sp = (int *)((char *)sp - *pc++ * sizeof(int));
    } // enter subroutine
    else if (i == ADJ)
      sp = (int *)((char *)sp + *pc++ * sizeof(int)); // stack adjust
    else if (i == LEV) {
      sp = bp;
      bp = (int *)*sp++;
      pc = (int *)*sp++;
    } // leave subroutine
    else if (i == LI)
      a = *(int *)a; // load int
    else if (i == LC)
      a = *(char *)a; // load char
    else if (i == SI)
      *(int *)*sp++ = a; // store int
    else if (i == SC)
      a = *(char *)*sp++ = a; // store char
    else if (i == PSH) {
      sp = (int *)((char *)sp - sizeof(int));
      *sp = a;
    } // push

    else if (i == OR)
      a = *sp++ | a;
    else if (i == XOR)
      a = *sp++ ^ a;
    else if (i == AND)
      a = *sp++ & a;
    else if (i == EQ)
      a = *sp++ == a;
    else if (i == NE)
      a = *sp++ != a;
    else if (i == LT)
      a = *sp++ < a;
    else if (i == GT)
      a = *sp++ > a;
    else if (i == LE)
      a = *sp++ <= a;
    else if (i == GE)
      a = *sp++ >= a;
    else if (i == SHL)
      a = *sp++ << a;
    else if (i == SHR)
      a = *sp++ >> a;
    else if (i == ADD)
      a = *sp++ + a;
    else if (i == SUB)
      a = *sp++ - a;
    else if (i == MUL)
      a = *sp++ * a;
    else if (i == DIV)
      a = *sp++ / a;
    else if (i == MOD)
      a = *sp++ % a;

    else if (i == OPEN)
      a = open((char *)sp[1], *sp);
    else if (i == READ)
      a = read(sp[2], (char *)sp[1], *sp);
    else if (i == CLOS)
      a = close(*sp);
    else if (i == PRTF) {
      t = sp + pc[1];
      printf((char *)t[-1], t[-2], t[-3], t[-4], t[-5], t[-6]);
      a = 0;
    } else if (i == MALC)
      a = (int)malloc(*sp);
    else if (i == FREE)
      free((void *)*sp);
    else if (i == MSET)
      a = (int)memset((char *)sp[2], sp[1], *sp);
    else if (i == MCMP)
      a = memcmp((char *)sp[2], (char *)sp[1], *sp);
    else if (i == EXIT) {
      printf("exit(%d) cycle = %d\n", (_int)*sp, (_int)cycle);
      return (_int)*sp;
    } else {
      printf("unknown instruction = %d! cycle = %d\n", (_int)i, (_int)cycle);
      return -1;
    }
  }
}
