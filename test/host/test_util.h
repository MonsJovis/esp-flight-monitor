/* Minimal host-test harness. No external dependencies — clang + make only.
 *
 * Contract: every test file is test/host/test_<module>.c, defines its own main(),
 * and ends with `return test_summary();`. The Makefile builds one binary per test
 * file, linking it against every host-safe module source.
 */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int  t_run = 0;
static int  t_fail = 0;
static const char *t_group = "";

#define GROUP(name) do { t_group = (name); printf("\n  %s\n", (name)); } while (0)

#define FAIL_(fmt, ...) do {                                                   \
    t_fail++;                                                                  \
    printf("    FAIL  %s:%d  " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);   \
} while (0)

#define CHECK(cond) do {                                                       \
    t_run++;                                                                   \
    if (!(cond)) FAIL_("expected: %s", #cond);                                 \
} while (0)

#define CHECK_STR(actual, expect) do {                                         \
    t_run++;                                                                   \
    const char *a_ = (actual), *e_ = (expect);                                 \
    if (a_ == NULL || e_ == NULL || strcmp(a_, e_) != 0)                       \
        FAIL_("got \"%s\", want \"%s\"", a_ ? a_ : "(null)", e_ ? e_ : "(null)"); \
} while (0)

#define CHECK_INT(actual, expect) do {                                         \
    t_run++;                                                                   \
    long a_ = (long)(actual), e_ = (long)(expect);                             \
    if (a_ != e_) FAIL_("got %ld, want %ld", a_, e_);                          \
} while (0)

#define CHECK_NEAR(actual, expect, tol) do {                                   \
    t_run++;                                                                   \
    double a_ = (double)(actual), e_ = (double)(expect);                       \
    if (fabs(a_ - e_) > (tol))                                                 \
        FAIL_("got %.6f, want %.6f (+/- %.6f)", a_, e_, (double)(tol));        \
} while (0)

/* Load a fixture from test/fixtures/. Caller frees. Fails hard — a missing
 * fixture is a broken test run, not a failing assertion. */
static char *load_fixture(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/../fixtures/%s", TEST_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("    FATAL missing fixture: %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { printf("    FATAL short read\n"); exit(2); }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int test_summary(void)
{
    printf("\n  %d checks, %d failed\n\n", t_run, t_fail);
    return t_fail == 0 ? 0 : 1;
}
