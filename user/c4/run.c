// run.c - C4 bytecode interpreter (virtual machine)
//
// Executes the VM bytecodes emitted by the compiler.
// Dispatches each opcode and handles system calls (open, read,
// printf, malloc, free, etc.).  Also loads .s bytecode files.

#include "cvm.h"
#include "kernel/fcntl.h"

int
run_program(int *pc, int *sp, int poolsz)
{
  int *bp, a = 0, cycle;
  int i, *t;

  // setup stack: push argc/argv for main's params
  bp = sp = (int *)((int)sp + poolsz);
  sp = (int *)((char *)sp - sizeof(int));
  *sp = 0; // argc placeholder (filled by JSR to main)
  sp = (int *)((char *)sp - sizeof(int));
  *sp = 0; // argv placeholder

  // start executing from JSR
  cycle = 0;
  while (1) {
    i = *pc++;
    ++cycle;
    if (debug) {
      extern const char opname[];
      printf(
        "%d> %.4s", (_int)cycle,
        &opname[i * 5]);
      if (i <= ADJ || i == DADDR)
        printf(" %d\n", (_int)*pc);
      else
        printf("\n");
    }
    if (i == LEA)
      a = (int)(bp + *pc++); // load local address
    else if (i == IMM)
      a = *pc++; // load immediate value
    else if (i == DADDR)
      a = (int)(data_base + *pc++); // load data address from offset
    else if (i == JMP) {
      int _off = *pc++;
      pc = code_base + _off;
    }
    else if (i == JSR) {
      sp = (int *)((char *)sp - sizeof(int));
      *sp = (int)(pc + 1);
      int _off = *pc;
      pc = code_base + _off;
    } else if (i == BZ) {
      int _off = *pc++;
      pc = a ? pc : code_base + _off;
    }
    else if (i == BNZ) {
      int _off = *pc++;
      pc = a ? code_base + _off : pc;
    }
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

// Load and run a .s bytecode file
int
run_bytecode_file(char *filename)
{
  int fd, poolsz = 256 * 1024;
  int *code, *dat;
  int entry_off = 0, code_count = 0, data_count = 0;
  int i, n, val;
  int *sp;

  if ((fd = open(filename, O_RDONLY)) < 0) {
    printf("could not open(%s)\n", filename);
    return -1;
  }

  // Read the entire file into a buffer
  char *fbuf = malloc(poolsz);
  if (!fbuf) {
    printf("malloc failed\n");
    close(fd);
    return -1;
  }
  n = read(fd, fbuf, poolsz - 1);
  close(fd);
  if (n <= 0) {
    printf("empty file\n");
    free(fbuf);
    return -1;
  }
  fbuf[n] = 0;

  // Parse the text format:
  //   .entry <offset>
  //   .data <count>
  //   <val1> <val2> ...
  //   .code <count>
  //   <op1> [operand]
  //   ...

  // First pass: count entries
  char *rp = fbuf;
  while (*rp) {
    if (*rp == '.' || *rp == '\n' || *rp == ' ') { ++rp; continue; }
    // Skip to next whitespace/line
    while (*rp && *rp != ' ' && *rp != '\n' && *rp != '\t') ++rp;
  }

  // Second pass: extract .entry, .data, .code sections
  rp = fbuf;

  // Read header
  // .entry <num>
  // .data <count>
  // then <count> integers (one per line or space separated)
  // .code <count>
  // then <count> lines with opcode names

  // Parse .entry
  while (*rp && *rp != '.') ++rp;
  if (!*rp) goto parse_err;
  rp += 6; // skip ".entry"
  while (*rp == ' ') ++rp;
  entry_off = 0;
  while (*rp >= '0' && *rp <= '9') { entry_off = entry_off * 10 + (*rp - '0'); ++rp; }

  // Parse .data count
  while (*rp && *rp != '.') ++rp;
  if (!*rp) goto parse_err;
  rp += 5; // skip ".data"
  while (*rp == ' ') ++rp;
  data_count = 0;
  while (*rp >= '0' && *rp <= '9') { data_count = data_count * 10 + (*rp - '0'); ++rp; }

  // Skip to data values
  while (*rp && *rp != '\n') ++rp;
  if (*rp) ++rp;

  // Allocate data
  dat = (int *)malloc(data_count * sizeof(int) + 16);
  if (!dat) { printf("malloc data failed\n"); free(fbuf); return -1; }
  memset(dat, 0, data_count * sizeof(int) + 16);
  data_base = (char *)dat;

  for (i = 0; i < data_count; i++) {
    while (*rp == ' ' || *rp == '\n') ++rp;
    val = 0;
    while (*rp >= '0' && *rp <= '9') {
      val = val * 10 + (*rp - '0');
      ++rp;
    }
    dat[i] = val;
  }

  // Find .code section
  while (*rp && *rp != '.') ++rp;
  if (!*rp) goto parse_err;
  rp += 5; // skip ".code"
  while (*rp == ' ') ++rp;
  code_count = 0;
  while (*rp >= '0' && *rp <= '9') { code_count = code_count * 10 + (*rp - '0'); ++rp; }

  // Skip to code values
  while (*rp && *rp != '\n') ++rp;
  if (*rp) ++rp;

  // Allocate code
  code = (int *)malloc(code_count * sizeof(int) + 16);
  if (!code) { printf("malloc code failed\n"); free(fbuf); return -1; }
  memset(code, 0, code_count * sizeof(int) + 16);
  code_base = code;

  // Parse code: each line is an opcode name optionally followed by operand
  char *opnames[] = {
    "LEA", "IMM", "JMP", "JSR", "BZ", "BNZ", "ENT", "ADJ", "LEV",
    "DADR",
    "LI", "LC", "SI", "SC", "PSH",
    "OR", "XOR", "AND", "EQ", "NE", "LT", "GT", "LE", "GE",
    "SHL", "SHR", "ADD", "SUB", "MUL", "DIV", "MOD",
    "OPEN", "READ", "CLOS", "PRTF", "MALC", "FREE", "MSET", "MCMP", "EXIT",
    0
  };
  int ci = 0;
  while (ci < code_count && *rp) {
    while (*rp == ' ' || *rp == '\n') ++rp;
    if (!*rp) break;

    // Read opcode name
    char oname[16];
    int oi = 0;
    while (*rp && *rp != ' ' && *rp != '\n' && oi < 14)
      oname[oi++] = *rp++;
    oname[oi] = 0;

    // Look up opcode
    int op = -1;
    for (int k = 0; opnames[k]; k++) {
      // compare up to 4 chars
      int match = 1;
      for (int j = 0; j < 4; j++) {
        char a = oname[j] >= 'a' && oname[j] <= 'z' ? oname[j] - 'a' + 'A' : oname[j];
        char b = opnames[k][j];
        if (!a && !b) break;
        if (a != b) { match = 0; break; }
        if (!a || !b) { match = 0; break; }
      }
      if (match) { op = k; break; }
    }
    if (op < 0) {
      printf("unknown opcode: %s\n", oname);
      free(code); free(dat); free(fbuf);
      return -1;
    }
    code[ci++] = op;

    // Read operand if opcode has one (opcodes <= ADJ or DADDR)
    if (op <= ADJ || op == DADDR) {
      while (*rp == ' ') ++rp;
      int sign = 1;
      if (*rp == '-') { sign = -1; ++rp; }
      if (*rp >= '0' && *rp <= '9') {
        val = 0;
        while (*rp >= '0' && *rp <= '9') {
          val = val * 10 + (*rp - '0');
          ++rp;
        }
        code[ci++] = val * sign;
      }
    }
  }

  free(fbuf);

  // Allocate stack
  if (!(sp = malloc((uint)poolsz))) {
    printf("could not malloc stack\n");
    free(code); free(dat);
    return -1;
  }

  // Run from entry point
  int *entry_pc = code_base + entry_off;
  return run_program(entry_pc, sp, poolsz);

parse_err:
  printf("parse error in %s\n", filename);
  free(fbuf);
  return -1;
}
