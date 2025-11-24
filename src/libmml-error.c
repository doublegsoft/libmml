/*
** ██╗░░░░░██╗██████╗░███╗░░░███╗███╗░░░███╗██╗░░░░░
** ██║░░░░░██║██╔══██╗████╗░████║████╗░████║██║░░░░░
** ██║░░░░░██║██████╦╝██╔████╔██║██╔████╔██║██║░░░░░
** ██║░░░░░██║██╔══██╗██║╚██╔╝██║██║╚██╔╝██║██║░░░░░
** ███████╗██║██████╦╝██║░╚═╝░██║██║░╚═╝░██║███████╗
** ╚══════╝╚═╝╚═════╝░╚═╝░░░░░╚═╝╚═╝░░░░░╚═╝╚══════╝
*/
#include <stdlib.h>
#include <string.h>

#include "libmml-error.h"

struct mml_error_s
{
  int code;

  char* msg;
};

// global error variable
mml_error_t last; 

const char*
mml_error_msg(void)
{
  return last.msg;
}

int
mml_error_code(void)
{
  return last.code;
}

void
mml_error_set(int           code, 
              const char*   msg)
{
  if (last.msg != NULL)
    free(last.msg);
  if (msg == NULL)
  {
    last.msg = NULL;
    last.code = code;
    return;
  }
  size_t len = strlen(msg);
  last.msg = (char*)malloc(len + 1);
  if (last.msg == NULL)
  {
    last.code = code;
    return;
  }
  memcpy(last.msg, msg, len + 1);
  last.msg[len] = '\0';
  last.code = code;
}