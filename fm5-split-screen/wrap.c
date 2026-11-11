#include "wrap.h"
#include "logging.h"
#include "support.h"

/* Forwarding thunk for slot N: count the call in the kind's counters, then jump to the wrapped
   object's own slot N with it as `this`. The arguments stay untouched, whatever the method takes. */
#define WRAP_THUNK(n)                                                                                  \
    ".p2align 4\n"                                                                                     \
    ".globl wrap_forward_" #n "\n"                                                                     \
    "wrap_forward_" #n ":\n"                                                                           \
    "    mov rax, qword ptr [rcx + 16]\n"                                                              \
    "    mov rax, qword ptr [rax]\n"                                                                   \
    "    lock inc dword ptr [rax + 4*" #n "]\n"                                                        \
    "    mov rcx, qword ptr [rcx + 8]\n"                                                               \
    "    mov rax, qword ptr [rcx]\n"                                                                   \
    "    jmp qword ptr [rax + 8*" #n "]\n"
#define WRAP_EACH_SLOT(X)                                                                              \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16)      \
    X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) \
    X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47)

__asm__(".intel_syntax noprefix\n.text\n" WRAP_EACH_SLOT(WRAP_THUNK) ".att_syntax prefix\n");
#define WRAP_DECLARE(n) extern void wrap_forward_##n(void);
WRAP_EACH_SLOT(WRAP_DECLARE)
#define WRAP_ADDRESS(n) (void *)wrap_forward_##n,
static void *const thunks[WRAP_SLOTS] = {WRAP_EACH_SLOT(WRAP_ADDRESS)};

typedef HRESULT (*QueryFn)(void *, const GUID *, void **);
typedef ULONG (*RefFn)(void *);

static void *forward_vtable[WRAP_SLOTS];
static SRWLOCK faces_lock = SRWLOCK_INIT;
static WrapKind *watched[16];
static volatile LONG watched_count;

static int guid_equal(const GUID *a, const GUID *b) { return memory_equal(a, b, sizeof(GUID)); }

static HRESULT wrap_query(WrapFace *self, const GUID *iid, void **out) {
    if (!out) {
        return E_POINTER;
    }
    *out = NULL;
    void *inner = NULL;
    HRESULT result = WRAP_METHOD(self->inner, 0, QueryFn)(self->inner, iid, &inner);
    if (FAILED(result) || !inner) {
        return result;
    }
    Wrap *wrap = self->owner;
    WrapFace *found = NULL;
    int added = 0;
    AcquireSRWLockExclusive(&faces_lock);
    for (LONG i = 0; i < wrap->face_count; ++i) {
        if (wrap->face[i].inner == inner) {
            found = &wrap->face[i];
        }
    }
    if (!found && wrap->face_count < WRAP_FACES) { // another interface of the same object: a new face
        found = &wrap->face[wrap->face_count];
        found->vtable = forward_vtable;
        for (unsigned i = 0; i < wrap->kind->interface_count; ++i) {
            if (guid_equal(iid, wrap->kind->interfaces[i].iid)) {
                found->vtable = wrap->kind->interfaces[i].vtable;
            }
        }
        found->inner = inner; // the face keeps this reference
        found->owner = wrap;
        ++wrap->face_count;
        added = 1;
    }
    ReleaseSRWLockExclusive(&faces_lock);
    if (!found) { // out of faces: the game gets Azimuth's object, past the wrapper
        WrapLine line;
        line.length = 0;
        line_add(&line, "WARNING: ");
        line_add(&line, wrap->kind->name);
        line_add(&line, " has no room for interface ");
        line_add_guid(&line, iid);
        line_log(&line);
        *out = inner;
        return result;
    }
    if (!added) {
        WRAP_METHOD(inner, 2, RefFn)(inner);
    }
    InterlockedIncrement(&wrap->refs);
    *out = found;
    if (added) {
        WrapLine line;
        line.length = 0;
        line_add(&line, "TRACE: ");
        line_add(&line, wrap->kind->name);
        line_add(&line, found->vtable == forward_vtable ? " interface " : " interface (handled) ");
        line_add_guid(&line, iid);
        line_log(&line);
    }
    return result;
}

static ULONG wrap_add_ref_face(WrapFace *self) { return (ULONG)InterlockedIncrement(&self->owner->refs); }

static ULONG wrap_release_face(WrapFace *self) {
    Wrap *wrap = self->owner;
    LONG refs = InterlockedDecrement(&wrap->refs);
    if (!refs) {
        for (LONG i = 0; i < wrap->face_count; ++i) {
            WRAP_METHOD(wrap->face[i].inner, 2, RefFn)(wrap->face[i].inner);
        }
        HeapFree(GetProcessHeap(), 0, wrap);
    }
    return (ULONG)refs;
}

void wrap_vtable(void **table) {
    for (unsigned i = 0; i < WRAP_SLOTS; ++i) {
        table[i] = thunks[i];
    }
    table[0] = (void *)wrap_query;
    table[1] = (void *)wrap_add_ref_face;
    table[2] = (void *)wrap_release_face;
}

void wrap_init(void) { wrap_vtable(forward_vtable); }

void *wrap_new(WrapKind *kind, void *inner, void **vtable, uintptr_t data) {
    Wrap *wrap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Wrap));
    if (!wrap) {
        WRAP_METHOD(inner, 2, RefFn)(inner);
        return NULL;
    }
    wrap->kind = kind;
    wrap->refs = 1;
    wrap->face_count = 1;
    wrap->data = data;
    wrap->face[0].vtable = vtable ? vtable : kind->primary;
    wrap->face[0].inner = inner;
    wrap->face[0].owner = wrap;
    return &wrap->face[0];
}

WrapFace *wrap_face(void *object, const WrapKind *kind) {
    if (!object || (*(void ***)object)[0] != (void *)wrap_query) {
        return NULL;
    }
    WrapFace *face = object;
    return !kind || face->owner->kind == kind ? face : NULL;
}

ULONG wrap_add_ref(void *face) { return WRAP_METHOD(face, 1, RefFn)(face); }
ULONG wrap_release(void *face) { return WRAP_METHOD(face, 2, RefFn)(face); }
void wrap_note(WrapFace *face, unsigned slot) { InterlockedIncrement(&face->owner->kind->calls[slot]); }

void wrap_watch(WrapKind *kind) {
    LONG index = InterlockedIncrement(&watched_count) - 1;
    if (index < (LONG)ARRAY_COUNT(watched)) {
        watched[index] = kind;
    }
}

void wrap_report(void) {
    LONG count = watched_count < (LONG)ARRAY_COUNT(watched) ? watched_count : (LONG)ARRAY_COUNT(watched);
    for (LONG k = 0; k < count; ++k) {
        WrapKind *kind = watched[k];
        for (unsigned slot = 3; slot < WRAP_SLOTS; ++slot) {
            if (kind && kind->calls[slot] && !kind->reported[slot]) {
                kind->reported[slot] = 1;
                WrapLine line;
        line.length = 0;
                line_add(&line, "TRACE: the game uses ");
                line_add(&line, kind->name);
                line_add(&line, " slot ");
                line_add_number(&line, slot);
                line_log(&line);
            }
        }
    }
}

void line_add(WrapLine *line, const char *text) {
    while (*text && line->length < sizeof(line->text) - 3) {
        line->text[line->length++] = *text++;
    }
}

void line_add_number(WrapLine *line, uint64_t value) {
    char digits[21];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    char text[21];
    for (int i = 0; i < count; ++i) {
        text[i] = digits[count - 1 - i];
    }
    text[count] = 0;
    line_add(line, text);
}

static void line_add_hex_digits(WrapLine *line, uint64_t value, int digits) {
    char text[17];
    for (int i = 0; i < digits; ++i) {
        text[i] = "0123456789abcdef"[(value >> (4 * (digits - 1 - i))) & 15];
    }
    text[digits] = 0;
    line_add(line, text);
}

void line_add_wide(WrapLine *line, const wchar_t *text) {
    char ascii[2] = {0};
    while (text && *text) {
        ascii[0] = *text >= 32 && *text < 127 ? (char)*text : '?';
        line_add(line, ascii);
        ++text;
    }
}

void line_add_guid(WrapLine *line, const GUID *guid) {
    if (!guid) {
        line_add(line, "(none)");
        return;
    }
    line_add_hex_digits(line, guid->Data1, 8);
    line_add(line, "-");
    line_add_hex_digits(line, guid->Data2, 4);
    line_add(line, "-");
    line_add_hex_digits(line, guid->Data3, 4);
    line_add(line, "-");
    for (int i = 0; i < 8; ++i) {
        line_add_hex_digits(line, guid->Data4[i], 2);
        if (i == 1) {
            line_add(line, "-");
        }
    }
}

void line_log(WrapLine *line) {
    line->text[line->length++] = '\r';
    line->text[line->length++] = '\n';
    line->text[line->length] = 0;
    log_append(line->text);
    line->length = 0;
}
