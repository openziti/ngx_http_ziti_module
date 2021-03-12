#ifndef DDEBUG_H
#define DDEBUG_H

#include <ngx_config.h>
#include <ngx_core.h>

#if defined(DDEBUG) && (DDEBUG)

#   define dd_dump_chain_size() { \
        int              n; \
        ngx_chain_t     *cl; \
            \
        for (n = 0, cl = u->out_bufs; cl; cl = cl->next, n++) { \
        } \
            \
        dd("chain size: %d", n); \
    }

#   if (NGX_HAVE_VARIADIC_MACROS)

#       define dd(...) fprintf(stderr, "ziti *** %s: ", __func__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__)

#   else

#include <stdarg.h>
#include <stdio.h>

#include <stdarg.h>

static ngx_inline void
dd(const char * fmt, ...) {
}

#    endif

#else

#   define dd_dump_chain_size()

#   if (NGX_HAVE_VARIADIC_MACROS)

#       define dd(...)

#   else

#include <stdarg.h>

static ngx_inline void
dd(const char * fmt, ...) {
}

#   endif

#endif

#endif /* DDEBUG_H */

