// cvm.c - C4 virtual machine and syntax mapping
//
// Provides the tokenizer (next()), opcode definitions, and
// global state shared across the compiler and VM.

#include "cvm.h"

// VM base registers for position-independent bytecodes
int *code_base;   // base address of code buffer
char *data_base;  // base address of data buffer
int *code_end;    // end of code buffer (exclusive)
char *data_end;   // end of data buffer (exclusive)
int *stack_base;  // bottom of stack
int stack_size;   // stack buffer size in bytes

// Global variable definitions
char *p, *lp,   // current position in source code
  *data;        // data/bss pointer

int *e, *le,    // current position in emitted code
  *id,          // currently parsed identifier
  *sym,         // symbol table (simple list of identifiers)
  tk,           // current token
  ival,         // current token value
  ty,           // current expression type
  loc,          // local variable offset
  line,         // current line number
  src,          // print source and assembly flag
  debug;        // print executed instructions

// Parameter base offset for local/param distinction
int param_base;

// Break/continue target tracking
int **break_targets;
int *break_target_sp;
int **continue_targets;
int *continue_target_sp;

// Switch case tracking
int **case_offsets;
int *case_offset_sp;
int case_count;

// Opcode name table (5 chars each, comma-separated, used for debug disassembly)
const char opname[] =
  "LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,DADR,LI  ,LC  ,SI  ,SC  ,PSH ,"
  "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
  "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,";

// --- source line index for -s listing ---
char *lines[65536];
int nlines;

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
      if (*p == '=') {
        ++p;
        tk = DivAssign;
        return;
      } else if (*p == '/') {
        ++p;
        while (*p != 0 && *p != '\n')
          ++p;
      } else if (*p == '*') {
        ++p; // skip '*'
        // skip block comment
        while (*p) {
          if (*p == '*' && *(p+1) == '/') {
            p += 2;
            break;
          }
          // Track newlines for line counting
          if (*p == '\n') {
            lines[line] = lp;
            lp = p + 1;
            ++line;
          }
          ++p;
        }
      } else {
        tk = Div;
        return;
      }
    } else if (tk == '\'' || tk == '"') {
      pp = data;
      while (*p != 0 && *p != tk) {
        if (*p == '\\') {
          ++p;
          if (*p == 'n') ival = '\n';
          else if (*p == 't') ival = '\t';
          else if (*p == 'r') ival = '\r';
          else if (*p == '0') ival = '\0';
          else if (*p == '\\') ival = '\\';
          else if (*p == '\'') ival = '\'';
          else if (*p == '"') ival = '"';
          else ival = *p;
          ++p;
        } else {
          ival = *p++;
        }
        if (tk == '"')
          *data++ = ival;
      }
      ++p;
      if (tk == '"')
        ival = (int)(pp - data_base);  // store data offset, not absolute addr
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
      } else if (*p == '=') {
        ++p;
        tk = AddAssign;
      } else
        tk = Add;
      return;
    } else if (tk == '-') {
      if (*p == '-') {
        ++p;
        tk = Dec;
      } else if (*p == '=') {
        ++p;
        tk = SubAssign;
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
      if (*p == '<') {
        ++p;
        if (*p == '=') { ++p; tk = ShlAssign; }
        else tk = Shl;
      } else if (*p == '=') {
        ++p;
        tk = Le;
      } else
        tk = Lt;
      return;
    } else if (tk == '>') {
      if (*p == '>') {
        ++p;
        if (*p == '=') { ++p; tk = ShrAssign; }
        else tk = Shr;
      } else if (*p == '=') {
        ++p;
        tk = Ge;
      } else
        tk = Gt;
      return;
    } else if (tk == '|') {
      if (*p == '|') {
        ++p;
        tk = Lor;
      } else if (*p == '=') {
        ++p;
        tk = OrAssign;
      } else
        tk = Or;
      return;
    } else if (tk == '&') {
      if (*p == '&') {
        ++p;
        tk = Lan;
      } else if (*p == '=') {
        ++p;
        tk = AndAssign;
      } else
        tk = And;
      return;
    } else if (tk == '^') {
      if (*p == '=') { ++p; tk = XorAssign; }
      else tk = Xor;
      return;
    } else if (tk == '%') {
      if (*p == '=') { ++p; tk = ModAssign; }
      else tk = Mod;
      return;
    } else if (tk == '*') {
      if (*p == '=') { ++p; tk = MulAssign; }
      else tk = Mul;
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
