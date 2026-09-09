#define _GNU_SOURCE

#include "../line-editor/line_editor.h"

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef long (*lfird_function)(void *, void *, void *, long);

static lfird_function next_lfird;
static le_history editor_history;
static int editor_history_ready;
static int target_build_id_state = -1;
static int target_core_build_id_state = -1;

static const unsigned char expected_libsqlplus_build_id[20] = {
    0xd9, 0x86, 0x02, 0xdf, 0x99, 0xfd, 0x30, 0x22, 0x2d, 0x32,
    0x6f, 0xae, 0xa7, 0x86, 0x3d, 0xfd, 0x8b, 0x5d, 0x25, 0x19
};
static const unsigned char expected_core_build_id[20] = {
    0xed, 0x8a, 0xcc, 0x43, 0xb9, 0xae, 0xd0, 0x84, 0xf3, 0x31,
    0x77, 0x89, 0xd6, 0xde, 0x4e, 0xf1, 0x54, 0x21, 0xcb, 0xca
};

static void raw_write(const char *text, size_t length)
{
    while (length != 0) {
        ssize_t written = write(STDERR_FILENO, text, length);
        if (written > 0) {
            text += (size_t)written;
            length -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return;
        }
    }
}

static size_t append_text(char *out, size_t pos, size_t cap, const char *text)
{
    size_t length = strlen(text);
    if (length > cap - pos)
        length = cap - pos;
    memcpy(out + pos, text, length);
    return pos + length;
}

static size_t append_hex(char *out, size_t pos, size_t cap, uintptr_t value)
{
    static const char digits[] = "0123456789abcdef";
    char reversed[2 * sizeof(value)];
    size_t count = 0;

    if (pos < cap)
        out[pos++] = '0';
    if (pos < cap)
        out[pos++] = 'x';
    do {
        reversed[count++] = digits[value & 0xf];
        value >>= 4;
    } while (value != 0 && count < sizeof(reversed));
    while (count != 0 && pos < cap)
        out[pos++] = reversed[--count];
    return pos;
}

static size_t append_signed(char *out, size_t pos, size_t cap, long value)
{
    unsigned long magnitude;
    char reversed[32];
    size_t count = 0;

    if (value < 0) {
        if (pos < cap)
            out[pos++] = '-';
        magnitude = (unsigned long)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned long)value;
    }
    do {
        reversed[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0 && count < sizeof(reversed));
    while (count != 0 && pos < cap)
        out[pos++] = reversed[--count];
    return pos;
}

static void log_call(void *arg1, void *arg2, void *arg3, long arg4,
                     void *return_address)
{
    char line[256];
    size_t pos = 0;
    pos = append_text(line, pos, sizeof(line), "[lfird] a1=");
    pos = append_hex(line, pos, sizeof(line), (uintptr_t)arg1);
    pos = append_text(line, pos, sizeof(line), " a2=");
    pos = append_hex(line, pos, sizeof(line), (uintptr_t)arg2);
    pos = append_text(line, pos, sizeof(line), " a3=");
    pos = append_hex(line, pos, sizeof(line), (uintptr_t)arg3);
    pos = append_text(line, pos, sizeof(line), " a4=");
    pos = append_signed(line, pos, sizeof(line), arg4);
    pos = append_text(line, pos, sizeof(line), " ret=");
    pos = append_hex(line, pos, sizeof(line), (uintptr_t)return_address);
    if (pos < sizeof(line))
        line[pos++] = '\n';
    raw_write(line, pos);
}

static int enabled(const char *name)
{
    const char *value = getenv(name);
    return value != NULL && (value[0] == '1' || value[0] == 'y' ||
                             value[0] == 'Y' || value[0] == 't' ||
                             value[0] == 'T');
}

static lfird_function resolve_next_lfird(void)
{
    lfird_function function = __atomic_load_n(&next_lfird, __ATOMIC_ACQUIRE);
    if (function == NULL) {
        void *symbol = dlsym(RTLD_NEXT, "lfird");
        memcpy(&function, &symbol, sizeof(function));
        if (function != NULL)
            __atomic_store_n(&next_lfird, function, __ATOMIC_RELEASE);
    }
    return function;
}

struct build_id_probe {
    uintptr_t base;
    const unsigned char *expected;
    size_t expected_size;
    int found;
};

static int inspect_build_id(struct dl_phdr_info *info, size_t size, void *data)
{
    struct build_id_probe *probe = (struct build_id_probe *)data;
    size_t i;

    (void)size;
    if ((uintptr_t)info->dlpi_addr != probe->base)
        return 0;
    for (i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *program = &info->dlpi_phdr[i];
        const unsigned char *cursor;
        size_t remaining;

        if (program->p_type != PT_NOTE)
            continue;
        cursor = (const unsigned char *)(info->dlpi_addr + program->p_vaddr);
        remaining = (size_t)program->p_filesz;
        while (remaining >= sizeof(ElfW(Nhdr))) {
            ElfW(Nhdr) header;
            size_t name_size;
            size_t description_size;
            size_t record_size;

            memcpy(&header, cursor, sizeof(header));
            name_size = ((size_t)header.n_namesz + 3u) & ~(size_t)3u;
            description_size = ((size_t)header.n_descsz + 3u) & ~(size_t)3u;
            record_size = sizeof(header) + name_size + description_size;
            if (record_size > remaining)
                break;
            if (header.n_type == NT_GNU_BUILD_ID &&
                header.n_namesz >= 4 &&
                memcmp(cursor + sizeof(header), "GNU", 4) == 0 &&
                header.n_descsz == probe->expected_size &&
                memcmp(cursor + sizeof(header) + name_size,
                       probe->expected, probe->expected_size) == 0) {
                probe->found = 1;
                return 1;
            }
            cursor += record_size;
            remaining -= record_size;
        }
    }
    return 1;
}

static int image_build_id_ok(uintptr_t base, const unsigned char *expected,
                             size_t expected_size, int *state_storage)
{
    int state = __atomic_load_n(state_storage, __ATOMIC_ACQUIRE);
    if (state < 0) {
        struct build_id_probe probe = {base, expected, expected_size, 0};
        int new_state;
        dl_iterate_phdr(inspect_build_id, &probe);
        new_state = probe.found ? 1 : 0;
        __atomic_compare_exchange_n(state_storage, &state,
                                     new_state, 0, __ATOMIC_RELEASE,
                                     __ATOMIC_ACQUIRE);
        state = __atomic_load_n(state_storage, __ATOMIC_ACQUIRE);
    }
    return state == 1;
}

static int target_libsqlplus_build_id_ok(uintptr_t base)
{
    return image_build_id_ok(base, expected_libsqlplus_build_id,
                             sizeof(expected_libsqlplus_build_id),
                             &target_build_id_state);
}

static int target_core_build_id_ok(lfird_function function)
{
    Dl_info info;
    void *object;

    memcpy(&object, &function, sizeof(object));
    if (function == NULL || dladdr(object, &info) == 0 ||
        info.dli_fname == NULL ||
        strstr(info.dli_fname, "libclntshcore.so.19.1") == NULL)
        return 0;
    return image_build_id_ok((uintptr_t)info.dli_fbase, expected_core_build_id,
                             sizeof(expected_core_build_id),
                             &target_core_build_id_state);
}

static int is_safiinp_line_call(void *return_address)
{
    Dl_info info;
    uintptr_t offset;

    if (return_address == NULL || dladdr(return_address, &info) == 0 ||
        info.dli_fname == NULL || strstr(info.dli_fname, "libsqlplus.so") == NULL)
        return 0;
    offset = (uintptr_t)return_address - (uintptr_t)info.dli_fbase;
    /* Return address after call at libsqlplus.so+0x2220b in this exact copy. */
    return offset == 0x22210 &&
           target_libsqlplus_build_id_ok((uintptr_t)info.dli_fbase);
}

long lfird(void *arg1, void *arg2, void *arg3, long arg4)
{
    int saved_errno = errno;
    void *return_address = __builtin_return_address(0);
    int trace = enabled("SQLPLUS_LFIRD_TRACE");
    int replace = enabled("SQLPLUS_LFIRD_REPLACE");

    if (trace)
        log_call(arg1, arg2, arg3, arg4, return_address);

    if (replace && arg3 != NULL && arg4 > 1 && isatty(STDIN_FILENO) &&
        is_safiinp_line_call(return_address) &&
        target_core_build_id_ok(resolve_next_lfird())) {
        char line[LE_LINE_MAX];
        const char *prompt = getenv("SQLPLUS_LFIRD_PROMPT");
        int result;

        if (!editor_history_ready) {
            le_history_init(&editor_history);
            editor_history_ready = 1;
        }
        if (prompt != NULL && prompt[0] == '\0')
            prompt = NULL;
        /* safiinp passes buffer_size - 1 and then writes the terminator at
         * buf[result]; out_cap includes one extra byte for that terminator. */
        result = le_readline(STDIN_FILENO, STDOUT_FILENO, prompt,
                             &editor_history, line,
                             arg4 >= (long)sizeof(line)
                                 ? sizeof(line)
                                 : (size_t)arg4 + 1);
        if (result > 0) {
            memcpy(arg3, line, (size_t)result + 1);
            errno = saved_errno;
            return result;
        }
        errno = saved_errno;
        /* safiinp has distinct recovery branches for -2 and -1.  Preserve
         * the editor's interrupt indication instead of collapsing Ctrl-C
         * into EOF/error.  EOF and other editor failures use -1 here, which
         * is the ordinary non-data path for the original reader. */
        return result == LE_INTERRUPT ? -2 : -1;
    }

    {
        lfird_function function = resolve_next_lfird();
        long result;
        if (function == NULL) {
            errno = saved_errno;
            return -2;
        }
        result = function(arg1, arg2, arg3, arg4);
        if (trace) {
            char line[96];
            size_t pos = append_text(line, 0, sizeof(line), "[lfird] -> ");
            pos = append_signed(line, pos, sizeof(line), result);
            if (pos < sizeof(line))
                line[pos++] = '\n';
            raw_write(line, pos);
        }
        errno = saved_errno;
        return result;
    }
}
