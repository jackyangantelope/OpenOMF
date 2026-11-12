/* Local configuration of the unmodified SDL ABI headers for newlib.
 * No SDL device drivers or SDL runtime are built into this port. */
#ifndef SDL_config_h_
#define SDL_config_h_
#define HAVE_LIBC 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_CTYPE_H 1
#define HAVE_STDIO_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MATH_H 1
#define HAVE_FLOAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
/* Local facade only, no binary SDL library: match the SDK/newlib enum ABI. */
#define SDL_NATIVE_SHORT_ENUMS 1
#endif
