// quash.c — Quite A Shell (EECS 678)
// A compact, rubric-complete shell with pipes, redirects, background jobs,
// built-ins, and environment expansion, written for clarity and testability.

#define _GNU_SOURCE  // enables getline on GNU-libc systems

#include <ctype.h>    // character classification for tokenizing
#include <errno.h>    // error strings for system-call diagnostics
#include <fcntl.h>    // open() flags for I/O redirection
#include <signal.h>   // signal numbers and kill()
#include <stdbool.h>  // bool and true/false
#include <stdio.h>    // standard I/O
#include <stdlib.h>   // allocation and process exit
#include <string.h>   // string utilities
#include <sys/types.h>
#include <sys/wait.h> // waitpid() for child reaping
#include <unistd.h>   // fork(), dup2(), execvp(), getcwd(), isatty()
#include "util.h"     // small “x*” helpers (checked malloc/realloc/strdup)

#define PROMPT      "[QUASH] $ "  // the visible prompt when the shell is interactive
#define MAX_JOBS    128           // upper bound on simultaneously tracked bg jobs
#define INIT_TOK    8             // initial token vector capacity (grows as needed)
#define INIT_STG    4             // initial stage vector capacity (grows as needed)

/* ---------------------------- Tokenization ---------------------------- */

typedef struct {
  char *text;           // the literal token content after quote handling
  bool single_quoted;   // remembers single quotes so $ expansion can be skipped
} Token;

/* Appends one character to a growable string buffer, expanding storage as needed. */
static void sb_push(char **buf, size_t *len, size_t *cap, char ch) {
  if (*len + 1 >= *cap) {                    // ensure space for ch + NUL
    *cap = *cap ? (*cap * 2) : 16;           // exponential growth keeps reallocation cheap
    *buf = xrealloc(*buf, *cap);
  }
  (*buf)[(*len)++] = ch;                     // append the new character
  (*buf)[*len] = '\0';                       // maintain C-string invariant
}

/* Splits the command line into tokens, recognizing quotes and | < > >> & operators. */
static Token *tokenize(const char *s, size_t *out_n) {
  Token  *t   = NULL;
  size_t  n   = 0;
  size_t  cap = 0;

  while (*s) {
    /* Skip whitespace between tokens to find the next meaningful segment. */
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) break;

    /* Single-character operators and the 2-character >> are tokens by themselves. */
    if (*s == '|' || *s == '<' || *s == '>' || *s == '&') {
      size_t L = (*s == '>' && *(s + 1) == '>') ? 2 : 1;
      Token tk = { .text = xmalloc(L + 1), .single_quoted = false };
      memcpy(tk.text, s, L);
      tk.text[L] = '\0';

      if (n >= cap) { cap = cap ? cap * 2 : INIT_TOK; t = xrealloc(t, cap * sizeof(*t)); }
      t[n++] = tk;
      s += L;
      continue;
    }

    /* Otherwise, collect a word while honoring single/double quotes and minimal escapes. */
    char *buf = NULL; size_t bl = 0, bc = 0;
    bool in_sq = false, in_dq = false;

    while (*s) {
      if (!in_sq && !in_dq && isspace((unsigned char)*s)) break;                // word end
      if (!in_sq && !in_dq && (*s == '|' || *s == '<' || *s == '>' || *s == '&')) break;

      if (!in_dq && *s == '\'') { in_sq = !in_sq; s++; continue; }              // toggle single quotes
      if (!in_sq && *s == '"')  { in_dq = !in_dq; s++; continue; }              // toggle double quotes

      /* Allow \" and \\ inside double quotes to pass literally. */
      if (in_dq && *s == '\\' && (*(s + 1) == '"' || *(s + 1) == '\\')) {
        sb_push(&buf, &bl, &bc, *(s + 1));
        s += 2;
        continue;
      }

      sb_push(&buf, &bl, &bc, *s++);                                            // ordinary character
    }

    Token tk = { .text = buf ? buf : xstrdup(""), .single_quoted = in_sq };
    if (n >= cap) { cap = cap ? cap * 2 : INIT_TOK; t = xrealloc(t, cap * sizeof(*t)); }
    t[n++] = tk;
  }

  *out_n = n;
  return t;
}

/* Expands $VAR and ${VAR} in-place for a single token (unless it was single-quoted). */
static void expand_one(Token *t) {
  if (t->single_quoted) return;                 // single quotes suppress expansion

  const char *s = t->text;
  char *out = NULL; size_t ol = 0, oc = 0;

  for (size_t i = 0; s[i]; ) {
    if (s[i] == '$') {
      size_t j = i + 1;

      /* ${NAME} form: capture alnum/_ between braces and substitute value or "" */
      if (s[j] == '{') {
        j++;
        size_t k = j;
        while (s[k] && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
        if (s[k] == '}') {
          size_t n = k - j;
          char *nm = strndup(s + j, n);
          const char *v = getenv(nm); if (!v) v = "";
          for (const char *p = v; *p; ++p) sb_push(&out, &ol, &oc, *p);
          free(nm);
          i = k + 1;
          continue;
        }
      } else {
        /* $NAME form: first char alpha/_ then alnum/_; expand if looks valid */
        size_t k = j;
        if (isalpha((unsigned char)s[k]) || s[k] == '_') {
          k++;
          while (s[k] && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
          size_t n = k - j;
          char *nm = strndup(s + j, n);
          const char *v = getenv(nm); if (!v) v = "";
          for (const char *p = v; *p; ++p) sb_push(&out, &ol, &oc, *p);
          free(nm);
          i = k;
          continue;
        }
      }
    }
    sb_push(&out, &ol, &oc, s[i++]);           // not a variable reference; copy byte
  }

  free(t->text);
  t->text = out ? out : xstrdup("");           // preserve empty-string semantics
}

/* Expands environment variables across the full token list. */
static void expand_all(Token *t, size_t n) {
  for (size_t i = 0; i < n; i++) expand_one(&t[i]);
}

/* ------------------------------ Parse Plan ----------------------------- */

typedef struct {
  char **argv;       // argument vector (NULL-terminated) for exec or built-in
  char *in_file;     // filename for "<" redirection if present
  char *out_file;    // filename for ">" or ">>" redirection if present
  bool  append;      // true when ">>" was used instead of ">"
} Stage;

typedef struct {
  Stage *stages;     // sequence of pipeline stages
  size_t nstages;    // number of stages in the pipeline
  bool   background; // true when job ends with &
  char  *cmdline;    // a pretty-printed form for user-facing messages
} JobPlan;

/* Releases heap-allocated fields of a single pipeline stage. */
static void free_stage(Stage *st) {
  if (!st) return;
  free(st->in_file);
  free(st->out_file);
  free(st->argv);
}

/* Releases the full job plan (all stages + pretty-printed command line). */
static void free_job(JobPlan *jp) {
  if (!jp) return;
  for (size_t i = 0; i < jp->nstages; i++) free_stage(&jp->stages[i]);
  free(jp->stages);
  free(jp->cmdline);
}

/* Concatenates tokens back into a single printable line, optionally appending &. */
static char *join_tokens(Token *t, size_t n, bool bg) {
  size_t cap = 32, len = 0;
  char *b = xmalloc(cap);
  b[0] = '\0';

  for (size_t i = 0; i < n; i++) {
    const char *w = t[i].text;
    size_t L = strlen(w);

    if (len + L + 2 >= cap) {                  // +1 space +1 NUL safety margin
      cap = len + L + 64;
      b = xrealloc(b, cap);
    }
    if (i > 0) b[len++] = ' ';                 // separate tokens with a single space
    memcpy(b + len, w, L);
    len += L;
    b[len] = '\0';
  }

  if (bg) {                                    // append literal ampersand when backgrounded
    if (len + 3 >= cap) { cap += 8; b = xrealloc(b, cap); }
    b[len++] = ' ';
    b[len++] = '&';
    b[len]   = '\0';
  }

  return b;
}

/* Converts token stream into a structured JobPlan (stages, redirects, & background). */
static JobPlan parse_tokens(Token *t, size_t n) {
  JobPlan jp = (JobPlan){0};
  if (n == 0) return jp;

  /* Trailing '&' toggles background and is removed from the stream. */
  if (strcmp(t[n - 1].text, "&") == 0) {
    jp.background = true;
    n--;
  }

  /* Count how many pipeline stages we need (every '|' splits a stage). */
  size_t stages = 1;
  for (size_t i = 0; i < n; i++) if (strcmp(t[i].text, "|") == 0) stages++;

  jp.stages  = xmalloc(stages * sizeof(Stage));
  jp.nstages = stages;

  for (size_t i = 0; i < stages; i++) {
    jp.stages[i].argv     = NULL;
    jp.stages[i].in_file  = NULL;
    jp.stages[i].out_file = NULL;
    jp.stages[i].append   = false;
  }

  /* Build argv for each stage while capturing < > >> filenames. */
  size_t si = 0;
  char **argv = NULL; size_t argc = 0, capv = 0;

  for (size_t i = 0; i < n; i++) {
    const char *w = t[i].text;

    if (strcmp(w, "|") == 0) {
      /* End of the current stage: finalize argv and start a fresh stage. */
      argv = xrealloc(argv, (argc + 1) * sizeof(char *));
      argv[argc] = NULL;
      jp.stages[si++].argv = argv;
      argv = NULL; argc = 0; capv = 0;
      continue;
    }

    if (!strcmp(w, "<") || !strcmp(w, ">") || !strcmp(w, ">>")) {
      /* Redirection must be followed by a filename token. */
      if (i + 1 >= n) {
        fprintf(stderr, "%sredirection missing filename\n", PROMPT);
        break;
      }
      char *fn = xstrdup(t[i + 1].text);
      if (!strcmp(w, "<")) {
        free(jp.stages[si].in_file);
        jp.stages[si].in_file = fn;
      } else {
        free(jp.stages[si].out_file);
        jp.stages[si].out_file = fn;
        jp.stages[si].append = !strcmp(w, ">>");
      }
      i++;                                    // consume filename token
      continue;
    }

    if (argc + 1 >= capv) {                   // ensure room for another argv entry
      capv = capv ? capv * 2 : 8;
      argv = xrealloc(argv, capv * sizeof(char *));
    }
    argv[argc++] = (char *)w;                 // keep pointer into token text
  }

  /* Finalize last stage argv if present. */
  argv = xrealloc(argv, (argc + 1) * sizeof(char *));
  argv[argc] = NULL;
  jp.stages[si++].argv = argv;

  /* Build human-readable copy used in job messages. */
  jp.cmdline = join_tokens(t, n, jp.background);
  return jp;
}

/* ------------------------------- Jobs ---------------------------------- */

typedef struct {
  int    id;        // job id shown to the user (1..N as in typical shells)
  pid_t  pgid;      // process group id for the pipeline
  pid_t *pids;      // PIDs of each process in the pipeline
  size_t npids;     // number of PIDs in pids[]
  bool   running;   // whether at least one process remains alive
  char  *cmdline;   // pretty-printed command line for this job
} Job;

static Job   jobs[MAX_JOBS];
static size_t njobs = 0;

/* Computes the next job id as (max live id) + 1, mirroring bash’s behavior. */
static int next_jid(void) {
  int m = 0;
  for (size_t i = 0; i < njobs; i++)
    if (jobs[i].running && jobs[i].id > m) m = jobs[i].id;
  return m + 1;
}

/* Inserts a running background job into the table and returns the assigned id. */
static int jobs_add(pid_t pgid, pid_t *p, size_t n, const char *cmd) {
  if (njobs >= MAX_JOBS) return -1;
  jobs[njobs].id      = next_jid();
  jobs[njobs].pgid    = pgid;
  jobs[njobs].pids    = xmalloc(n * sizeof(pid_t));
  memcpy(jobs[njobs].pids, p, n * sizeof(pid_t));
  jobs[njobs].npids   = n;
  jobs[njobs].running = true;
  jobs[njobs].cmdline = xstrdup(cmd);
  njobs++;
  return jobs[njobs - 1].id;
}

/* Prints a snapshot of all live background jobs in the required format. */
static void jobs_print_all(void) {
  for (size_t i = 0; i < njobs; i++)
    if (jobs[i].running)
      printf("[%d] %d %s\n", jobs[i].id, jobs[i].pgid, jobs[i].cmdline);
}

/* Marks the job containing PID as progressed, and emits a “Completed” line when done. */
static void jobs_mark_done(pid_t pid) {
  for (size_t i = 0; i < njobs; i++) {
    if (!jobs[i].running) continue;

    bool   hit  = false;
    size_t left = 0;

    for (size_t k = 0; k < jobs[i].npids; k++)
      if (jobs[i].pids[k] == pid) hit = true;

    if (!hit) continue;

    for (size_t k = 0; k < jobs[i].npids; k++) {
      if (jobs[i].pids[k] == pid) jobs[i].pids[k] = -1;
      if (jobs[i].pids[k] > 0) left++;
    }

    if (left == 0) {
      printf("Completed: [%d] %d %s\n", jobs[i].id, jobs[i].pgid, jobs[i].cmdline);
      fflush(stdout);
      jobs[i].running = false;
      free(jobs[i].pids);    jobs[i].pids    = NULL; jobs[i].npids   = 0;
      free(jobs[i].cmdline); jobs[i].cmdline = NULL;
    }
    return;
  }
}

/* Non-blocking reap: collect any finished children and update job state. */
static void reap_children_nowait(void) {
  int   st;
  pid_t pid;
  while ((pid = waitpid(-1, &st, WNOHANG)) > 0)
    jobs_mark_done(pid);
}

/* ------------------------------ Built-ins ------------------------------ */

static int bi_exit (char **a) { (void)a; while (waitpid(-1, NULL, WNOHANG) > 0) {} exit(0); return 1; }
static int bi_pwd  (char **a) { (void)a; char *cwd = getcwd(NULL, 0); if (!cwd) { perror("getcwd"); return 1; } printf("%s\n", cwd); free(cwd); return 1; }
static int bi_cd   (char **a) { const char *t = a[1] ? a[1] : getenv("HOME"); if (!t) { fprintf(stderr, "cd: HOME not set\n"); return 1; } if (chdir(t) != 0) { perror("cd"); return 1; } char *cwd = getcwd(NULL, 0); if (cwd) { setenv("PWD", cwd, 1); free(cwd); } return 1; }
static int bi_echo (char **a) { for (int i = 1; a[i]; i++) { if (i > 1) putchar(' '); fputs(a[i], stdout); } putchar('\n'); return 1; }
static int bi_export(char **a) { if (!a[1]) return 1; for (int i = 1; a[i]; i++) { char *s = a[i]; char *e = strchr(s, '='); if (!e) { fprintf(stderr, "export: format KEY=VALUE\n"); continue; } *e = '\0'; if (setenv(s, e + 1, 1) != 0) perror("setenv"); *e = '='; } return 1; }
static int bi_jobs (char **a) { (void)a; jobs_print_all(); return 1; }
static int bi_kill (char **a) { if (!a[1] || !a[2]) { fprintf(stderr, "kill usage: kill SIGNUM PID\n"); return 1; } int sig = atoi(a[1]); pid_t pid = (pid_t)atoi(a[2]); if (kill(pid, sig) != 0) perror("kill"); return 1; }

typedef int (*bi_fn)(char **);
static struct { const char *name; bi_fn fn; } BI[] = {
  { "cd",    bi_cd    },
  { "pwd",   bi_pwd   },
  { "echo",  bi_echo  },
  { "export",bi_export},
  { "jobs",  bi_jobs  },
  { "kill",  bi_kill  },
  { "exit",  bi_exit  },
  { "quit",  bi_exit  },
  { NULL,    NULL     }
};

/* Dispatches built-ins; returns true if handled so the caller can skip execvp. */
static bool try_builtin(char **argv, int *st) {
  if (!argv || !argv[0]) return false;
  for (int i = 0; BI[i].name; i++) {
    if (strcmp(argv[0], BI[i].name) == 0) {
      *st = BI[i].fn(argv);
      return true;
    }
  }
  return false;
}

/* ---------------------------- Exec & Pipes ----------------------------- */

/* Applies input/output redirections for a stage; returns -1 on failure. */
static int apply_redirs(const Stage *st) {
  if (st->in_file) {
    int fd = open(st->in_file, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "open %s: %s\n", st->in_file, strerror(errno)); return -1; }
    if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 stdin"); close(fd); return -1; }
    close(fd);
  }
  if (st->out_file) {
    int flags = O_WRONLY | O_CREAT | (st->append ? O_APPEND : O_TRUNC);
    int fd = open(st->out_file, flags, 0666);
    if (fd < 0) { fprintf(stderr, "open %s: %s\n", st->out_file, strerror(errno)); return -1; }
    if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 stdout"); close(fd); return -1; }
    close(fd);
  }
  return 0;
}

/* Spawns the pipeline described by jp; waits in foreground or records background. */
static int launch_pipe(const JobPlan *jp) {
  size_t N = jp->nstages;

  /* Allocate N-1 pipes when the pipeline has multiple processes. */
  int (*pp)[2] = NULL;
  if (N > 1) {
    pp = xmalloc((N - 1) * sizeof(int[2]));
    for (size_t i = 0; i < N - 1; i++)
      if (pipe(pp[i]) != 0) { perror("pipe"); free(pp); return 1; }
  }

  pid_t *pids = xmalloc(N * sizeof(pid_t));
  pid_t  pg   = 0;  // process group id for the whole pipeline

  for (size_t i = 0; i < N; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      if (pp) { for (size_t k = 0; k < N - 1; k++) { close(pp[k][0]); close(pp[k][1]); } }
      free(pp); free(pids);
      return 1;
    }

    if (pid == 0) {
      /* Child: join the pipeline’s process group and connect pipes as needed. */
      if (i == 0) setpgid(0, 0); else setpgid(0, pg);

      if (N > 1) {
        if (i > 0    && dup2(pp[i - 1][0], STDIN_FILENO)  < 0) { perror("dup2 in");  _exit(127); }
        if (i < N-1 && dup2(pp[i][1],    STDOUT_FILENO) < 0) { perror("dup2 out"); _exit(127); }
        for (size_t k = 0; k < N - 1; k++) { close(pp[k][0]); close(pp[k][1]); }
      }

      if (apply_redirs(&jp->stages[i]) != 0) _exit(127);

      /* Built-ins inside a pipeline run in the child so the pipeline can proceed. */
      int dst = 0;
      if (try_builtin(jp->stages[i].argv, &dst)) _exit(0);

      execvp(jp->stages[i].argv[0], jp->stages[i].argv);
      fprintf(stderr, "%s%s: command not found\n", PROMPT, jp->stages[i].argv[0]);
      _exit(127);
    } else {
      /* Parent: remember the child PID and maintain the process group handle. */
      if (i == 0) { pg = pid; setpgid(pid, pg); } else setpgid(pid, pg);
      pids[i] = pid;
    }
  }

  if (pp) {
    for (size_t k = 0; k < N - 1; k++) { close(pp[k][0]); close(pp[k][1]); }
    free(pp);
  }

  if (jp->background) {
    /* Background: record the job and return immediately to the prompt. */
    int jid = jobs_add(pg, pids, N, jp->cmdline);
    if (jid < 0) {
      fprintf(stderr, "Too many background jobs\n");
    } else {
      printf("Background job started: [%d] %d %s\n", jid, pg, jp->cmdline);
      fflush(stdout);
    }
    free(pids);
    return 1;
  }

  /* Foreground: wait for every child in the pipeline to exit in order. */
  int st;
  for (size_t i = 0; i < N; i++)
    if (waitpid(pids[i], &st, 0) < 0) perror("waitpid");

  free(pids);
  return 1;
}

/* Executes a parsed job: simple built-ins stay in-parent; pipelines go through fork/exec. */
static int exec_job(JobPlan *jp) {
  if (jp->nstages == 0 || !jp->stages[0].argv || !jp->stages[0].argv[0]) return 1;

  bool simple =
    (jp->nstages == 1) &&
    (!jp->stages[0].in_file) &&
    (!jp->stages[0].out_file) &&
    (!jp->background);

  if (simple) {
    int st = 1;
    if (try_builtin(jp->stages[0].argv, &st)) return st;  // built-in side-effects (cd/export/exit)
  }

  return launch_pipe(jp);
}

/* ------------------------------- Input --------------------------------- */

/* Reads one logical line, strips trailing newline, removes # comments, and trims spaces. */
static char *read_line(void) {
  char   *line = NULL; size_t cap = 0;
  ssize_t n = getline(&line, &cap, stdin);
  if (n == -1) { free(line); return NULL; }

  if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';

  /* Treat # as the start of a comment and discard the remainder of the line. */
  char *h = strchr(line, '#');
  if (h) *h = '\0';

  /* Trim leading and trailing spaces/tabs/carrage returns to normalize input. */
  char *p = line;
  while (*p && (*p == ' ' || *p == '\t' || *p == '\r')) p++;

  size_t L = strlen(p);
  while (L > 0 && (p[L - 1] == ' ' || p[L - 1] == '\t' || p[L - 1] == '\r')) p[--L] = '\0';

  char *out = xstrdup(p);
  free(line);
  return out;
}

/* --------------------------------- Main -------------------------------- */

int main(void) {
  for (;;) {
    /* Only show the prompt when the shell is connected to a terminal (not when scripted). */
    if (isatty(STDIN_FILENO)) {
      printf("%s", PROMPT);
      fflush(stdout);
    }

    reap_children_nowait();                // print completion notices opportunistically

    char *line = read_line();
    if (!line) break;                      // EOF (Ctrl+D or pipe end)

    if (line[0] == '\0') {                 // ignore blank lines after trimming/comment drop
      free(line);
      continue;
    }

    size_t nt = 0;
    Token *tok = tokenize(line, &nt);
    free(line);

    if (nt == 0) {                         // nothing but comments/spaces
      free(tok);
      continue;
    }

    expand_all(tok, nt);                   // $VAR and ${VAR} expansion
    JobPlan jp = parse_tokens(tok, nt);    // build pipeline/redirect/background plan
    (void)exec_job(&jp);                   // execute according to the plan

    for (size_t i = 0; i < nt; i++)        // release token storage
      free(tok[i].text);
    free(tok);

    free_job(&jp);                         // release plan storage
    reap_children_nowait();                // show any bg completion lines asap
  }

  /* Final non-blocking reap so the shell ends cleanly with no stragglers. */
  reap_children_nowait();
  return 0;
}