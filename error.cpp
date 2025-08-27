#include <errno.h>
#include <stdarg.h>

#include "unp.h"

// static void err_doit(int errnoflag, int error, const char *fmt, va_list ap) {
//   char buf[MAXLINE];
//   vsnprintf(buf, MAXLINE - 1, fmt, ap);

//   if (errnoflag) {
//     snprintf(buf + strlen(buf), MAXLINE - 1 - strlen(buf), ": %s",
//              strerror(error));
//   }
//   strcat(buf, "\n");
//   fflush(stdout);
//   fputs(buf, stderr);
//   fflush(NULL);
// }

static void err_doit(int errnoflag, int error, const char *fmt, va_list ap) {
  char buf[MAXLINE];
  int len = vsnprintf(buf, MAXLINE - 1, fmt, ap);

  if (len < 0) len = 0;
  if (len >= MAXLINE - 1) len = MAXLINE - 2;

  if (errnoflag && len < MAXLINE - 3)  // 留出空间给 ": " + error + "\n"
  {
    snprintf(buf + len, MAXLINE - len, ": %s", strerror(error));
    len = strlen(buf);
  }

  if (len < MAXLINE - 1) {
    strcat(buf, "\n");
  }
  fflush(stdout);
  fputs(buf, stderr);
  fflush(NULL);
}

/*
 * fatal error unrelated to a system call
 * print a message and terminate *
 */

void err_quit(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  err_doit(1, errno, fmt, ap);
  va_end(ap);
  exit(1);
}

void err_msg(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  err_doit(1, errno, fmt, ap);
  va_end(ap);
}
