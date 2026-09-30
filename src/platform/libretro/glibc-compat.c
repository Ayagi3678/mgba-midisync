/* Keep the core loadable on systems with glibc < 2.38.
 * glibc 2.38 headers redirect strtol & co. to __isoc23_* under _GNU_SOURCE.
 * This file is compiled without _GNU_SOURCE and provides hidden local
 * definitions so the .so doesn't import the GLIBC_2.38 symbols. */
#if defined(__linux__)
#undef _GNU_SOURCE
#include <stdlib.h>
#define HIDDEN __attribute__((visibility("hidden")))
HIDDEN long __isoc23_strtol(const char* s, char** e, int b) { return strtol(s, e, b); }
HIDDEN long long __isoc23_strtoll(const char* s, char** e, int b) { return strtoll(s, e, b); }
HIDDEN unsigned long __isoc23_strtoul(const char* s, char** e, int b) { return strtoul(s, e, b); }
HIDDEN unsigned long long __isoc23_strtoull(const char* s, char** e, int b) { return strtoull(s, e, b); }
#endif
