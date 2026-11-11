#ifndef FM5_WRAP_H
#define FM5_WRAP_H
/* Wrappers around the COM (WinRT) objects Azimuth hands the game. Every vtable slot jumps on to the
   wrapped object unless the wrapper's kind overrides it, and QueryInterface keeps the game inside the
   wrapper, so a few overridden slots change what the game sees without touching Azimuth. */
#include "extra_options.h"

#define WRAP_SLOTS 48 /* Azimuth's largest vtable the game reaches (UserStatics) has 34 */
#define WRAP_FACES 4  /* interfaces of one object the game can reach through QueryInterface */

typedef struct WrapKind {
    volatile LONG calls[WRAP_SLOTS]; /* first member: the forwarding thunks count calls here */
    const char *name;
    void **primary;          /* vtable of the interface wrap_new is given, when the caller passes none */
    const GUID *special_iid; /* a face reached through QueryInterface with this IID... */
    void **special;          /* ...gets this vtable; every other face forwards each slot */
    LONG reported[WRAP_SLOTS];
} WrapKind;

typedef struct Wrap Wrap;
typedef struct WrapFace { /* what the game holds: the thunks read inner at +8 and owner at +16 */
    void **vtable;
    void *inner;
    Wrap *owner;
} WrapFace;

struct Wrap {
    WrapKind *kind; /* first member: the thunks find the call counters through it */
    volatile LONG refs;
    volatile LONG face_count;
    uintptr_t data; /* the kind's own value */
    WrapFace face[WRAP_FACES];
};

/* An interface method of the object behind a face, by vtable slot. */
#define WRAP_METHOD(object, slot, type) ((type)(*(void ***)(object))[slot])

void wrap_init(void);
/* Fills a vtable: the wrapper's IUnknown in slots 0-2, forwarding in the rest. */
void wrap_vtable(void **table);
/* Takes over the caller's reference to inner. Returns the wrapper's first face (vtable, or the kind's
   primary when NULL), or NULL after releasing inner when out of memory. */
void *wrap_new(WrapKind *kind, void *inner, void **vtable, uintptr_t data);
/* The face if object is one of this module's wrappers (of kind, when not NULL), else NULL. */
WrapFace *wrap_face(void *object, const WrapKind *kind);
ULONG wrap_add_ref(void *face);
ULONG wrap_release(void *face);
/* Counts an overridden slot's call, as the thunks do for forwarded ones. */
void wrap_note(WrapFace *face, unsigned slot);
/* Logs each kind's slots the game used for the first time since the last report. */
void wrap_report(void);
/* Kinds wrap_report watches. */
void wrap_watch(WrapKind *kind);

/* One log line built from pieces (no C library here). */
typedef struct {
    char text[512];
    size_t length;
} WrapLine;
void line_add(WrapLine *line, const char *text);
void line_add_number(WrapLine *line, uint64_t value);
void line_add_wide(WrapLine *line, const wchar_t *text);
void line_add_guid(WrapLine *line, const GUID *guid);
void line_log(WrapLine *line); /* ends the line and appends it to the log */
#endif
