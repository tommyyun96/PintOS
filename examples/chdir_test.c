#include <syscall.h>
#include <stdio.h>
int main(int argc, char *argv[])
{
  mkdir("a");
  chdir("a");
  if(!chdir(".."))
    printf("Oh no...");
  return 0;
}
