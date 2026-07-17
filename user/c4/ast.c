// ast.c - C4 AST parser and bytecode compiler
//
// Parses C source code into an Abstract Syntax Tree (AST),
// then walks the AST to emit VM bytecodes.  With -s, it also
// saves bytecodes to a .s file for later execution.

#include "cvm.h"
#include "kernel/fcntl.h"

// Opcode names for save/load (matching opname[] in cvm.c)
static const char *opname_save[] = {
  "LEA", "IMM", "JMP", "JSR", "BZ", "BNZ", "ENT", "ADJ", "LEV",
  "DADR",
  "LI", "LC", "SI", "SC", "PSH",
  "OR", "XOR", "AND", "EQ", "NE", "LT", "GT", "LE", "GE",
  "SHL", "SHR", "ADD", "SUB", "MUL", "DIV", "MOD",
  "OPEN", "READ", "CLOS", "PRTF", "MALC", "FREE", "MSET", "MCMP", "EXIT",
  0
};

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

// Forward declarations for recursive walk
void walk_expr(AstNode *n);
void walk_stmt(AstNode *n);

// ========== PARSER ==========

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
  extern const char opname[];
  while (le < e) {
    printf(
      "%8.4s",
      &opname[*++le * 5]);
    if (*le <= ADJ || *le == DADDR)
      printf(" %d\n", (_int) * ++le);
    else
      printf("\n");
  }
}

// Walk an expression node, emitting VM bytecodes
// All jump targets and data addresses use offsets relative to code_base/data_base.
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
    *++e = DADDR;     // was IMM, now DADDR for data offset
    *++e = (int)n->ival;
    break;
  case AST_NAME:
    if (n->ival < 0)  // negative = bp-relative offset -> local
      *++e = LEA;
    else              // non-negative = data offset -> global
      *++e = DADDR;
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
      if (n->left->ival < 0) {  // local: bp-relative
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {                  // global: data offset
        *++e = DADDR;
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
      if (n->left->ival < 0) {  // local: bp-relative
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {                  // global: data offset
        *++e = DADDR;
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
      if (n->left->ival < 0) {  // local: bp-relative
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {                  // global: data offset
        *++e = DADDR;
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
    // Args in source order
    while (a) {
      nargs++;
      a = a->next;
    }
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
      *++e = n->sym[Val]; // now an offset (set in Phase 2)
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
    *d1 = (int)((e + 3) - code_base);  // offset from code_base
    *++e = JMP;
    d2 = ++e;
    walk_expr(n->next); // else
    *d2 = (int)((e + 1) - code_base);  // offset from code_base
    break;
  }
  case AST_LOR:
    walk_expr(n->left);
    *++e = BNZ;
    d1 = ++e;
    walk_expr(n->right);
    *d1 = (int)((e + 1) - code_base);  // offset
    break;
  case AST_LAN:
    walk_expr(n->left);
    *++e = BZ;
    d1 = ++e;
    walk_expr(n->right);
    *d1 = (int)((e + 1) - code_base);  // offset
    break;
  case AST_ASSIGN: {
    // left side: push address
    if (n->left->kind == AST_NAME) {
      if (n->left->ival < 0) {  // local: bp-relative
        *++e = LEA;
        *++e = (int)n->left->ival;
      } else {                  // global: data offset
        *++e = DADDR;
        *++e = (int)n->left->ival;
      }
    } else {
      walk_expr(n->left);
    }
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
// All jump targets use offsets relative to code_base.
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
      *b = (int)((e + 3) - code_base);  // offset
      *++e = JMP;
      b = ++e;
      walk_stmt(n->next); // else
    }
    *b = (int)((e + 1) - code_base);  // offset
    break;
  }
  case AST_STMT_WHILE: {
    a = e + 1; // top of condition (absolute, used as reference)
    walk_expr(n->left);
    *++e = BZ;
    b = ++e;
    walk_stmt(n->right);
    *++e = JMP;
    *++e = (int)(a - code_base);     // JMP target = offset
    *b = (int)((e + 1) - code_base); // BZ target = offset
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

// Save bytecodes to a human-readable .s file
void
save_bytecode(char *filename, int entry_offset, int code_words, int data_words)
{
  int fd;
  int i;

  fd = open(filename, O_WRONLY | O_CREATE);
  if (fd < 0) {
    printf("could not open %s for writing\n", filename);
    return;
  }

  // Write entry point
  fprintf(fd, ".entry %d\n", (_int)entry_offset);

  // Write data section
  fprintf(fd, ".data %d\n", (_int)data_words);
  for (i = 0; i < data_words; i++)
    fprintf(fd, "%lld ", ((int *)data_base)[i]);
  fprintf(fd, "\n");

  // Write code section
  fprintf(fd, ".code %d\n", (_int)code_words);
  int *cp = code_base;
  int cw = 0;
  while (cw < code_words) {
    int op = cp[cw++];
    if (op >= 0 && op < 40) {
      fprintf(fd, "%s", opname_save[op]);
      if (op <= ADJ || op == DADDR) {
        int operand = cp[cw++];
        fprintf(fd, " %d", (_int)operand);
      }
    } else {
      fprintf(fd, "%d", (_int)op);
    }
    write(fd, "\n", 1);
  }

  close(fd);
}

_int
main(_int argc, char **argv)
{
  int fd, bt, ty, poolsz, *idmain;
  int *sp;                                            // stack for VM
  int i, *fn_id;                                      // temps
  AstNode *prog_root = 0, *prog_last = 0, *fn; // program AST list
  char *outfile = 0;                            // .s output file for -s

  char *input = 0;
  // Scan all arguments for -s <file> and -d
  while (argc > 0) {
    if (**argv == '-' && (*argv)[1] == 's') {
      --argc; ++argv;
      if (argc < 1) {
        printf("usage: c4 file.c [-s file.s]\n");
        return -1;
      }
      outfile = *argv;
      --argc; ++argv;
    } else if (**argv == '-' && (*argv)[1] == 'd') {
      debug = 1;
      --argc; ++argv;
    } else {
      input = *argv;
      --argc; ++argv;
    }
  }
  if (!input) {
    printf("usage: c4 file.c [-s file.s]\n");
    return -1;
  }

  // Check if input is a .s bytecode file
  int len = 0;
  while (input[len]) len++;
  if (len > 2 && input[len-2] == '.' && input[len-1] == 's') {
    // Run bytecode file directly
    return run_bytecode_file(input);
  }

  // Normal compilation mode
  if ((fd = open(input, 0)) < 0) {
    printf("could not open(%s)\n", input);
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

  // Set up base registers for position-independent addressing
  code_base = e;
  data_base = data;
  e = code_base - 1;  // make *++e write to index 0

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
  // Global variable addresses are stored as offsets from data_base
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
        // Global variable: store offset from data_base instead of absolute address
        id[Class] = Glo;
        id[Val] = (int)(data - data_base);  // offset
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
        // Function address: store offset from code_base
        sym_entry[Val] = (int)((e + 1) - code_base); // offset

        if (src || outfile) {
          printf("%d: %.*s", (_int)walker->srcline,
                 (_int)(p - lines[walker->srcline]), lines[walker->srcline]);
          list_opcodes();
        }
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
  int *jsr_addr = e + 1;
  *++e = JSR;
  *++e = 0; // placeholder, filled below
  *++e = EXIT;

  // Fill in main's address
  if (idmain[Class] != Fun) {
    printf("main() not defined\n");
    return -1;
  }
  jsr_addr[1] = (int)idmain[Val]; // already an offset from code_base

  // Calculate code and data sizes
  int code_words = (int)(e - code_base) + 1; // total words emitted
  int data_words = (int)(data - data_base);

  // If -s mode, save to file and exit
  if (outfile) {
    // Entry point is the offset of the JSR trampoline
    int entry_off = (int)(jsr_addr - code_base);
    save_bytecode(outfile, entry_off, code_words, data_words);
    return 0;
  }

  // Run: pass absolute address to run_program
  return run_program(code_base + (int)(jsr_addr - code_base), sp, poolsz);
}
