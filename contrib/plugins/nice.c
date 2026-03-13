/*
 * nice.c — QEMU plugin that dispatches custom RISC-V instructions
 *                 (custom-0/1/2/3) to handler functions in a user-supplied
 *                 shared library.
 *
 * The user compiles their own shared library:
 *
 *   Linux/macOS:
 *     gcc -O2 -shared -fPIC \
 *         -I/path/to/include user_nice.c -o libuser_nice.so
 *
 *   Windows:
 *     gcc -O2 -shared \
 *         -I/path/to/include user_nice.c -o user_nice.dll
 *
 * Then pass the library path as a plugin argument:
 *
 *   qemu-system-riscv32 ... \
 *     -plugin /path/to/libnice.so,lib=/path/to/libuser_nice.so \
 *     -kernel app.elf
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Dynamic library loading: use Win32 API on Windows, dlfcn elsewhere */
#ifdef _WIN32
#  include <windows.h>
#  define dl_open(path)   ((void *)LoadLibraryA(path))
#  define dl_sym(h, name) ((void *)GetProcAddress((HMODULE)(h), (name)))
#  define dl_close(h)     FreeLibrary((HMODULE)(h))
static const char *dl_error(void)
{
    static char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, GetLastError(), 0, buf, sizeof(buf), NULL);
    return buf;
}
#else
#  include <dlfcn.h>
#  define dl_open(path)   dlopen((path), RTLD_NOW | RTLD_LOCAL)
#  define dl_sym(h, name) dlsym((h), (name))
#  define dl_close(h)     dlclose(h)
#  define dl_error()      dlerror()
#endif

#include <glib.h>
#include <qemu-plugin.h>
#include "nice_api.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static nice_def_t *g_insn_defs;  /* points into user's .so */
static void              *g_dl_handle;
static bool               g_verbose;

/* -----------------------------------------------------------------------
 * Function-pointer typedefs for CI_CALL_INT handlers
 * ----------------------------------------------------------------------- */

typedef uint64_t (*fn_i_v)(void);      /* 0 inputs; return written to rd if xd=1 */
typedef uint64_t (*fn_i_i)(uint64_t);
typedef uint64_t (*fn_i_i_i)(uint64_t, uint64_t);
typedef uint64_t (*fn_i_i_i_i)(uint64_t, uint64_t, uint64_t);
typedef float (*fn_f_v)(void);
typedef float (*fn_f_f)(float);
typedef float (*fn_f_f_f)(float, float);
typedef float (*fn_f_f_f_f)(float, float, float);
typedef double (*fn_d_v)(void);
typedef double (*fn_d_d)(double);
typedef double (*fn_d_d_d)(double, double);
typedef double (*fn_d_d_d_d)(double, double, double);

/* -----------------------------------------------------------------------
 * Advanced / internal: FPR bit-cast helpers (float/double ↔ uint64_t)
 *
 * Normally not needed when using CI_CALL_F32 / CI_CALL_F64.
 * Kept for edge cases where the user wants the raw uint64_t interface.
 *
 * Use memcpy: compilers (gcc/clang -O2) reduce this to a single register
 * move; no runtime overhead.
 * ----------------------------------------------------------------------- */

static inline float ci_as_f32(uint64_t raw)
{
    float f;
    memcpy(&f, &raw, sizeof(f));
    return f;
}

static inline double ci_as_f64(uint64_t raw)
{
    double d;
    memcpy(&d, &raw, sizeof(d));
    return d;
}

static inline uint64_t ci_from_f32(float f)
{
    uint32_t r = 0;
    memcpy(&r, &f, sizeof(f));
    /* NaN-box for RISC-V: upper 32 bits must be all 1s for valid f32 in FPR */
    return (uint64_t)0xFFFFFFFF00000000ULL | (uint64_t)r;
}

static inline uint64_t ci_from_f64(double d)
{
    uint64_t r;
    memcpy(&r, &d, sizeof(d));
    return r;
}

/* ----------------------------------------------------------------------- */
/* Runtime handler                                                           */
/* ----------------------------------------------------------------------- */

static bool nice_handler(unsigned int vcpu_index,
                                qemu_plugin_nice_info_t *info,
                                void *userdata)
{
    const nice_def_t *e = g_insn_defs;

    for (; e->opcode_type != CI_TABLE_END; e++) {
        if (e->opcode_type != info->opcode_type) continue;
        if (e->funct7      != info->funct7)      continue;
        if (e->rs1_enc != CI_ANY && e->rs1_enc != info->rs1) continue;
        if (e->rs2_enc != CI_ANY && e->rs2_enc != info->rs2) continue;

        /* Match on actual funct3 from instruction (xd|xs1|xs2) */
        uint8_t info_funct3 = (info->xd << 2) | (info->xs1 << 1) | info->xs2;
        if (e->funct3 != info_funct3) continue;

        /*
         * Build argument list dynamically for all 64 (funct7-type × funct3)
         * combinations:
         *   args[0] = rd_val   if is_mac && xd   (accumulator first)
         *   args[N] = rs1_val  if xs1
         *   args[N] = rs2_val  if xs2
         *
         * For PAIR instructions (is_pair=1, riscv32 only): merge even/odd GPR
         * into a single uint64_t; result is split back in op_helper.c.
         */
#define PAIR_JOIN(lo, hi) \
        (info->is_pair \
         ? (((uint64_t)(uint32_t)(hi) << 32) | (uint32_t)(lo)) \
         : (lo))

        uint64_t args[3];
        int n = 0;  // Number of input parameters
        if (info->is_mac) args[n++] = PAIR_JOIN(info->rd_val,  info->rd_val_hi);
        if (info->xs1) args[n++] = PAIR_JOIN(info->rs1_val, info->rs1_val_hi);
        if (info->xs2) args[n++] = PAIR_JOIN(info->rs2_val, info->rs2_val_hi);

        uint64_t result = 0;
        if (e->call_type == CI_CALL_F32) {
            float r = 0.0f;
            switch (n) {
            case 0: r = ((fn_f_v)e->fn)(); break;
            case 1: r = ((fn_f_f)e->fn)(ci_as_f32(args[0])); break;
            case 2: r = ((fn_f_f_f)e->fn)(ci_as_f32(args[0]), ci_as_f32(args[1])); break;
            case 3: r = ((fn_f_f_f_f)e->fn)(ci_as_f32(args[0]), ci_as_f32(args[1]), ci_as_f32(args[2])); break;
            }
            result = ci_from_f32(r); /* NaN-boxes for RISC-V FPR */
        } else if (e->call_type == CI_CALL_F64) {
            double r = 0.0;
            switch (n) {
            case 0: r = ((fn_d_v)e->fn)(); break;
            case 1: r = ((fn_d_d)e->fn)(ci_as_f64(args[0])); break;
            case 2: r = ((fn_d_d_d)e->fn)(ci_as_f64(args[0]), ci_as_f64(args[1])); break;
            case 3: r = ((fn_d_d_d_d)e->fn)(ci_as_f64(args[0]), ci_as_f64(args[1]), ci_as_f64(args[2])); break;
            }
            result = ci_from_f64(r);
        } else { /* CI_CALL_INT */
            switch (n) {
            case 0: result = ((fn_i_v)e->fn)(); break;
            case 1: result = ((fn_i_i)e->fn)(args[0]); break;
            case 2: result = ((fn_i_i_i)e->fn)(args[0], args[1]); break;
            case 3: result = ((fn_i_i_i_i)e->fn)(args[0], args[1], args[2]); break;
            }
        }
#undef PAIR_JOIN
        info->result = result;
        return true;
    }
    /* no match, raises illegal-instruction */
    return false;
}

/* ----------------------------------------------------------------------- */
/* Cleanup                                                                   */
/* ----------------------------------------------------------------------- */

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    if (g_dl_handle) {
        dl_close(g_dl_handle);
        g_dl_handle = NULL;
    }
}

/* ----------------------------------------------------------------------- */
/* Plugin entry point                                                         */
/* ----------------------------------------------------------------------- */

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    const char *lib_path = NULL;

    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "lib=", 4) == 0)
            lib_path = argv[i] + 4;
        else if (strcmp(argv[i], "verbose=on") == 0)
            g_verbose = true;
    }

    if (!lib_path) {
        fprintf(stderr,
                "NICE: missing required argument 'lib=<path>'\n"
                "  Usage: -plugin libnice.so,lib=/path/to/libmy_insns.so\n");
        return -1;
    }

    if (strcmp(info->target_name, "riscv32") != 0 &&
        strcmp(info->target_name, "riscv64") != 0) {
        fprintf(stderr, "NICE: target '%s' is not RISC-V, skipping\n",
                info->target_name);
        return -1;
    }

    g_dl_handle = dl_open(lib_path);
    if (!g_dl_handle) {
        fprintf(stderr, "NICE: failed to load '%s': %s\n",
                lib_path, dl_error());
        return -1;
    }

    g_insn_defs = (nice_def_t *)dl_sym(g_dl_handle, "nice_defs");
    if (!g_insn_defs) {
        fprintf(stderr,
                "NICE: symbol 'nice_defs' not found in '%s': %s\n",
                lib_path, dl_error());
        dl_close(g_dl_handle);
        g_dl_handle = NULL;
        return -1;
    }

    /* Count entries; log each entry only when verbose=on */
    if (g_verbose) {
        int n = 0;
        for (const nice_def_t *e = g_insn_defs;
            e->opcode_type != CI_TABLE_END; e++, n++) {
            char rs1_str[8], rs2_str[8];
            if (e->rs1_enc == CI_ANY) { rs1_str[0]='*'; rs1_str[1]='\0'; }
            else { snprintf(rs1_str, sizeof(rs1_str), "%u", e->rs1_enc); }
            if (e->rs2_enc == CI_ANY) { rs2_str[0]='*'; rs2_str[1]='\0'; }
            else { snprintf(rs2_str, sizeof(rs2_str), "%u", e->rs2_enc); }
            fprintf(stderr,
                    "NICE: [%d] opcode_type=0x%02x funct7=0x%02x "
                    "rs1_enc=%s rs2_enc=%s funct3=%d\n",
                    n, e->opcode_type, e->funct7,
                    rs1_str, rs2_str, e->funct3);
        }
        fprintf(stderr, "NICE: loaded %d instruction(s) from '%s'\n",
                n, lib_path);
    }

    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    qemu_plugin_register_nice_handler(id, nice_handler, NULL);

    return 0;
}
