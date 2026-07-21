// C89 Feature Test for c4 compiler
// Tests working features and documents known limitations

int printf();

/* for loop (with inc in body, not inc clause) */
int test_for()
{
  int i;
  int sum = 0;
  for (i = 1; i <= 5; ) {
    sum = sum + i;
    i = i + 1;
  }
  printf("for sum=%d (expect 15)\n", sum);
  return 0;
}

/* do-while */
int test_do_while()
{
  int i = 0;
  do {
    i = i + 1;
  } while (i < 3);
  printf("do-while i=%d (expect 3)\n", i);
  return 0;
}

/* break */
int test_break()
{
  int i;
  int sum = 0;
  for (i = 0; i < 10; ) {
    if (i == 5)
      break;
    sum = sum + i;
    i = i + 1;
  }
  printf("break sum=%d (expect 10)\n", sum);
  return 0;
}

/* switch/case */
int test_switch()
{
  int x = 2;
  int result = 0;
  switch (x) {
    case 1: result = 10; break;
    case 2: result = 20; break;
    default: result = -1; break;
  }
  printf("switch result=%d (expect 20)\n", result);
  return 0;
}

/* goto/label */
int test_goto()
{
  int x = 0;
  goto mylabel;
  x = 42;
mylabel:
  printf("goto x=%d (expect 0)\n", x);
  return 0;
}

/* comma operator */
int test_comma()
{
  int x;
  x = (5, 10);
  printf("comma x=%d (expect 10)\n", x);
  return 0;
}

/* compound assignments */
int test_compound()
{
  int x = 10;
  x += 5;
  printf("+= x=%d (expect 15)\n", x);
  x -= 3;
  printf("-= x=%d (expect 12)\n", x);
  x *= 2;
  printf("*= x=%d (expect 24)\n", x);
  x /= 4;
  printf("/= x=%d (expect 6)\n", x);
  return 0;
}

int
main()
{
  printf("=== C89 Tests ===\n");
  test_for();
  test_do_while();
  test_break();
  test_switch();
  test_goto();
  test_comma();
  test_compound();
  printf("=== All tests done ===\n");
  return 0;
}
