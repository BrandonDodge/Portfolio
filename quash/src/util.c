// util.c — small allocation helpers that fail fast with clear diagnostics.

#include <stdio.h>   // fprintf, stderr
#include <stdlib.h>  // malloc, realloc, free, abort
#include <string.h>  // strdup
#include "util.h"    // declarations

void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (!p) {
    fprintf(stderr, "xmalloc: failed to allocate %zu bytes\n", n);
    abort();  // abort makes it obvious (and avoids undefined behavior later)
  }
  return p;
}

void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) {
    fprintf(stderr, "xrealloc: failed to resize to %zu bytes\n", n);
    abort();
  }
  return q;
}

char *xstrdup(const char *s) {
  if (!s) {
    fprintf(stderr, "xstrdup: NULL source string\n");
    abort();
  }
  char *d = strdup(s);
  if (!d) {
    fprintf(stderr, "xstrdup: failed to duplicate string of length %zu\n", strlen(s));
    abort();
  }
  return d;
}