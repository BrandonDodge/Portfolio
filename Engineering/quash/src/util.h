#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Allocates n bytes or aborts with a readable message if allocation fails. */
void *xmalloc(size_t n);

/* Reallocates p to n bytes or aborts with a readable message if it fails. */
void *xrealloc(void *p, size_t n);

/* Duplicates a C string or aborts with a readable message if allocation fails. */
char *xstrdup(const char *s);

#endif /* UTIL_H */