#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
  int i;
  //while (argc == 4 && !strcmp(argv[0], "echo") && !strcmp(argv[1], "x") && !strcmp(argv[2], "y") && !strcmp(argv[3], "z"));
  for (i = 0; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");

  return EXIT_SUCCESS;
}
