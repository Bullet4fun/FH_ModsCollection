#include "players.h"
#include "support.h"

/* Slots below are the Xbox One interfaces' vtable order, which the game and Azimuth both follow
   (checked against Azimuth 1.0.4's own methods; maiteuxzanela's notes in PR #5 named most of them).
   0-2 IUnknown, 3-5 IInspectable, the interface's own methods from 6. */
enum {
    USER_STATICS_COUNT = 6, /* answers 1 in Azimuth: how many users are signed in */
    USER_STATICS_USERS = 9,
    USER_STATICS_BY_ID = 14,
    USER_ID = 6,
    USER_IS_GUEST = 10,
    USER_XBOX_HASH = 14,
    USER_XBOX_ID = 15,
    VIEW_GET_AT = 6, /* IVectorView */
    VIEW_SIZE = 7,
    VIEW_INDEX_OF = 8,
    VIEW_GET_MANY = 9,
    CONTROLLER_ID = 6,
    CONTROLLER_USER = 8,
    GAMEPAD_READING = 7, /* IGamepad::GetCurrentReading, an IGamepadReading */
    READING_BUTTONS = 7,
    CONTROLLER_STATICS_ALL = 6, /* Controller::Controllers, and Gamepad::Gamepads on the gamepad statics */
    CONTROLLER_ADDED = 7,       /* add_ControllerAdded; 9 ControllerRemoved, 11 ControllerPairingChanged */
    CONTROLLER_REMOVED = 9,
    CONTROLLER_PAIRING = 11,
    DELEGATE_INVOKE = 3,
    EVENT_CONTROLLER = 6, /* the event arguments' Controller */
};
#define GUEST_USER_ID 2u
#define BUTTON_A 0x10u /* Windows.Xbox.Input.GamepadButtons */

/* Windows.Xbox.Input.IController: Azimuth's gamepads answer it with their second vtable. */
static const GUID IID_IController = {0x9776ff56, 0x9bfd, 0x4581, {0xad, 0x45, 0xb6, 0x45, 0xbb, 0xa5, 0x26, 0xd6}};
/* Windows.Xbox.Input.IGamepad: their first vtable, the game's way from a controller to its readings. */
static const GUID IID_IGamepad = {0x082e402c, 0x07df, 0x45e1, {0xa5, 0xab, 0xa3, 0x12, 0x7a, 0xf1, 0x97, 0xb5}};

typedef HRESULT (*GetPointerFn)(void *, void **);
typedef HRESULT (*QueryFn)(void *, const GUID *, void **);
typedef HRESULT (*GetU32Fn)(void *, UINT32 *);
typedef HRESULT (*GetU64Fn)(void *, UINT64 *);
typedef HRESULT (*ByIdFn)(void *, UINT32, void **);
typedef HRESULT (*IndexOfFn)(void *, void *, UINT32 *, unsigned char *);
typedef HRESULT (*GetManyFn)(void *, UINT32, UINT32, void **, UINT32 *);
typedef HRESULT (*AddEventFn)(void *, void *, INT64 *);
typedef HRESULT (*InvokeFn)(void *, void *, void *);

static void *user_statics_vt[WRAP_SLOTS], *user_view_vt[WRAP_SLOTS], *guest_vt[WRAP_SLOTS];
static void *controller_statics_vt[WRAP_SLOTS], *gamepad_statics_vt[WRAP_SLOTS], *pad_view_vt[WRAP_SLOTS];
static void *pad_vt[WRAP_SLOTS], *pad_controller_vt[WRAP_SLOTS], *delegate_vt[WRAP_SLOTS], *event_args_vt[WRAP_SLOTS];

static WrapKind kind_user_statics = {.name = "UserStatics", .primary = user_statics_vt};
static WrapKind kind_user_view = {.name = "Users", .primary = user_view_vt};
static WrapKind kind_guest = {.name = "GuestUser", .primary = guest_vt};
static WrapKind kind_controller_statics = {.name = "ControllerStatics", .primary = controller_statics_vt};
static WrapKind kind_gamepad_statics = {.name = "GamepadStatics", .primary = gamepad_statics_vt};
static WrapKind kind_pad_view = {.name = "Pads", .primary = pad_view_vt};
static const struct WrapInterface pad_interfaces[] = {{&IID_IController, pad_controller_vt}, {&IID_IGamepad, pad_vt}};
static WrapKind kind_pad = {.name = "Pad", .primary = pad_vt, .interfaces = pad_interfaces, .interface_count = 2};
static WrapKind kind_delegate = {.name = "ControllerEventHandler", .primary = delegate_vt};
static WrapKind kind_event_args = {.name = "ControllerEventArgs", .primary = event_args_vt};

static SRWLOCK lock = SRWLOCK_INIT;
static void *user_statics; /* Azimuth's, for the guest's own user object */
static void *guest;        /* the guest's wrapper, kept for the whole session */
static UINT64 pad_ids[8];  /* device ids seen, ascending */
static unsigned pad_count;
static UINT64 first_pad;  /* the device that pressed A first: player 1's, all the others the guest's */
static int first_pad_known;

static void log_text(const char *a, const char *b) {
    WrapLine line;
        line.length = 0;
    line_add(&line, a);
    line_add(&line, b);
    line_log(&line);
}

/* The guest: user 1's object behind a wrapper with its own id, guest flag and Xbox user id. */
static void *guest_get(void) {
    AcquireSRWLockExclusive(&lock);
    if (!guest && user_statics) {
        void *user = NULL;
        if (SUCCEEDED(WRAP_METHOD(user_statics, USER_STATICS_BY_ID, ByIdFn)(user_statics, 1, &user)) && user) {
            guest = wrap_new(&kind_guest, user, NULL, 0);
            log_text("PLAYERS: ", guest ? "a guest signs in as user 2" : "no memory for the guest");
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return guest;
}

static void *guest_ref(void) {
    void *user = guest_get();
    if (user) {
        wrap_add_ref(user);
    }
    return user;
}

/* Where a device stands among the pads seen so far, by id. */
static int pad_index(UINT64 id) {
    AcquireSRWLockExclusive(&lock);
    unsigned i = 0;
    while (i < pad_count && pad_ids[i] < id) {
        ++i;
    }
    int added = 0;
    if ((i == pad_count || pad_ids[i] != id) && pad_count < ARRAY_COUNT(pad_ids)) {
        for (unsigned j = pad_count; j > i; --j) {
            pad_ids[j] = pad_ids[j - 1];
        }
        pad_ids[i] = id;
        ++pad_count;
        added = 1;
    }
    int index = i < pad_count && pad_ids[i] == id ? (int)i : -1;
    ReleaseSRWLockExclusive(&lock);
    if (added) {
        WrapLine line;
        line.length = 0;
        line_add(&line, "PLAYERS: pad with device id ");
        line_add_number(&line, id);
        line_add(&line, " seen, pads so far: ");
        line_add_number(&line, pad_count);
        line_log(&line);
    }
    return index;
}

/* User statics */
static HRESULT user_statics_count(WrapFace *self, UINT32 *out) {
    wrap_note(self, USER_STATICS_COUNT);
    HRESULT result = WRAP_METHOD(self->inner, USER_STATICS_COUNT, GetU32Fn)(self->inner, out);
    if (SUCCEEDED(result) && out && guest_get()) {
        ++*out;
    }
    return result;
}

static HRESULT user_statics_users(WrapFace *self, void **out) {
    wrap_note(self, USER_STATICS_USERS);
    HRESULT result = WRAP_METHOD(self->inner, USER_STATICS_USERS, GetPointerFn)(self->inner, out);
    if (SUCCEEDED(result) && out && *out && guest_get()) {
        void *view = wrap_new(&kind_user_view, *out, NULL, 0);
        *out = view;
        return view ? result : E_OUTOFMEMORY;
    }
    return result;
}

static HRESULT user_statics_by_id(WrapFace *self, UINT32 id, void **out) {
    wrap_note(self, USER_STATICS_BY_ID);
    if (id == GUEST_USER_ID && out) {
        *out = guest_ref();
        if (*out) {
            return S_OK;
        }
    }
    return WRAP_METHOD(self->inner, USER_STATICS_BY_ID, ByIdFn)(self->inner, id, out);
}

/* The users list, with the guest after Azimuth's user */
static UINT32 inner_size(WrapFace *self) {
    UINT32 size = 0;
    if (FAILED(WRAP_METHOD(self->inner, VIEW_SIZE, GetU32Fn)(self->inner, &size))) {
        size = 0;
    }
    return size;
}

static HRESULT user_view_get_at(WrapFace *self, UINT32 index, void **out) {
    wrap_note(self, VIEW_GET_AT);
    if (out && index == inner_size(self) && (*out = guest_ref()) != NULL) {
        return S_OK;
    }
    return WRAP_METHOD(self->inner, VIEW_GET_AT, ByIdFn)(self->inner, index, out);
}

static HRESULT user_view_size(WrapFace *self, UINT32 *out) {
    wrap_note(self, VIEW_SIZE);
    HRESULT result = WRAP_METHOD(self->inner, VIEW_SIZE, GetU32Fn)(self->inner, out);
    if (SUCCEEDED(result) && out) {
        ++*out;
    }
    return result;
}

static HRESULT user_view_index_of(WrapFace *self, void *value, UINT32 *index, unsigned char *found) {
    wrap_note(self, VIEW_INDEX_OF);
    if (value && value == guest && index && found) {
        *index = inner_size(self);
        *found = 1;
        return S_OK;
    }
    return WRAP_METHOD(self->inner, VIEW_INDEX_OF, IndexOfFn)(self->inner, value, index, found);
}

static HRESULT user_view_get_many(WrapFace *self, UINT32 start, UINT32 capacity, void **items, UINT32 *actual) {
    wrap_note(self, VIEW_GET_MANY);
    UINT32 size = inner_size(self);
    HRESULT result = WRAP_METHOD(self->inner, VIEW_GET_MANY, GetManyFn)(self->inner, start, capacity, items, actual);
    if (SUCCEEDED(result) && items && actual && start + *actual == size && *actual < capacity) {
        void *user = guest_ref();
        if (user) {
            items[(*actual)++] = user;
        }
    }
    return result;
}

/* The guest's own properties; everything else is user 1's */
static HRESULT guest_id(WrapFace *self, UINT32 *out) {
    wrap_note(self, USER_ID);
    if (!out) {
        return E_POINTER;
    }
    *out = GUEST_USER_ID;
    return S_OK;
}

static HRESULT guest_is_guest(WrapFace *self, unsigned char *out) {
    wrap_note(self, USER_IS_GUEST);
    if (!out) {
        return E_POINTER;
    }
    *out = 1;
    return S_OK;
}

/* The game's own string functions: Azimuth emulates WinRT strings, so a string the game gets must
   come from Azimuth's functions, not from Windows' combase. */
static void *const *string_create, *const *string_buffer, *const *string_delete;

void players_strings(void *const *create, void *const *buffer, void *const *destroy) {
    string_create = create;
    string_buffer = buffer;
    string_delete = destroy;
}

/* User 1's string with a "2" after it: another Xbox user id and hash, as unique as user 1's own. */
static HRESULT guest_string(WrapFace *self, unsigned slot, void **out) {
    wrap_note(self, slot);
    void *own = NULL;
    HRESULT result = WRAP_METHOD(self->inner, slot, GetPointerFn)(self->inner, &own);
    if (FAILED(result) || !out) {
        return result;
    }
    if (!string_create || !string_buffer || !string_delete || !*string_create || !*string_buffer || !*string_delete) {
        *out = own; // no way to make another string: the guest keeps user 1's
        return result;
    }
    UINT32 length = 0;
    const wchar_t *text = ((const wchar_t *(*)(void *, UINT32 *))*string_buffer)(own, &length);
    wchar_t buffer[128];
    UINT32 n = 0;
    for (; text && n < length && n < ARRAY_COUNT(buffer) - 1; ++n) {
        buffer[n] = text[n];
    }
    buffer[n++] = L'2';
    ((HRESULT (*)(void *))*string_delete)(own);
    return ((HRESULT (*)(const wchar_t *, UINT32, void **))*string_create)(buffer, n, out);
}

static HRESULT guest_xbox_hash(WrapFace *self, void **out) { return guest_string(self, USER_XBOX_HASH, out); }
static HRESULT guest_xbox_id(WrapFace *self, void **out) { return guest_string(self, USER_XBOX_ID, out); }

/* Pads: the second one's user is the guest */
static void *pad_wrap(void *inner, int controller) {
    return wrap_new(&kind_pad, inner, controller ? pad_controller_vt : pad_vt, 0);
}

/* Until a pad presses A every pad is user 1's, as in Azimuth: the game signs in whoever starts it. */
static HRESULT pad_user(WrapFace *self, void **out) {
    wrap_note(self, CONTROLLER_USER);
    UINT64 id = 0;
    if (out && SUCCEEDED(WRAP_METHOD(self->inner, CONTROLLER_ID, GetU64Fn)(self->inner, &id)) &&
        pad_index(id) >= 0 && first_pad_known && id != first_pad && (*out = guest_ref()) != NULL) {
        static volatile LONG logged;
        if (!InterlockedExchange(&logged, 1)) {
            WrapLine line;
            line.length = 0;
            line_add(&line, "PLAYERS: the game asked whose the pad with device id ");
            line_add_number(&line, id);
            line_add(&line, " is: the guest's");
            line_log(&line);
        }
        return S_OK;
    }
    return WRAP_METHOD(self->inner, CONTROLLER_USER, GetPointerFn)(self->inner, out);
}

/* The first pad seen pressing A becomes player 1's. */
static HRESULT pad_reading(WrapFace *self, void **out) {
    wrap_note(self, GAMEPAD_READING);
    HRESULT result = WRAP_METHOD(self->inner, GAMEPAD_READING, GetPointerFn)(self->inner, out);
    UINT32 buttons = 0;
    if (SUCCEEDED(result) && out && *out && !first_pad_known &&
        SUCCEEDED(WRAP_METHOD(*out, READING_BUTTONS, GetU32Fn)(*out, &buttons)) && (buttons & BUTTON_A)) {
        void *controller = NULL;
        UINT64 id = 0;
        if (SUCCEEDED(WRAP_METHOD(self->inner, 0, QueryFn)(self->inner, &IID_IController, &controller)) && controller) {
            int known = SUCCEEDED(WRAP_METHOD(controller, CONTROLLER_ID, GetU64Fn)(controller, &id));
            WRAP_METHOD(controller, 2, ULONG (*)(void *))(controller);
            AcquireSRWLockExclusive(&lock);
            int first = known && !first_pad_known;
            if (first) {
                first_pad = id;
                first_pad_known = 1;
            }
            ReleaseSRWLockExclusive(&lock);
            if (first) {
                WrapLine line;
                line.length = 0;
                line_add(&line, "PLAYERS: the pad with device id ");
                line_add_number(&line, id);
                line_add(&line, " pressed A first: it is player 1, any other pad is the guest's");
                line_log(&line);
            }
        }
    }
    return result;
}

static HRESULT statics_all(WrapFace *self, void **out) {
    wrap_note(self, CONTROLLER_STATICS_ALL);
    HRESULT result = WRAP_METHOD(self->inner, CONTROLLER_STATICS_ALL, GetPointerFn)(self->inner, out);
    if (SUCCEEDED(result) && out && *out) {
        *out = wrap_new(&kind_pad_view, *out, NULL, self->owner->kind == &kind_controller_statics);
        return *out ? result : E_OUTOFMEMORY;
    }
    return result;
}

static HRESULT pad_view_get_at(WrapFace *self, UINT32 index, void **out) {
    wrap_note(self, VIEW_GET_AT);
    HRESULT result = WRAP_METHOD(self->inner, VIEW_GET_AT, ByIdFn)(self->inner, index, out);
    if (SUCCEEDED(result) && out && *out) {
        *out = pad_wrap(*out, (int)self->owner->data);
        return *out ? result : E_OUTOFMEMORY;
    }
    return result;
}

static HRESULT pad_view_index_of(WrapFace *self, void *value, UINT32 *index, unsigned char *found) {
    wrap_note(self, VIEW_INDEX_OF);
    WrapFace *pad = wrap_face(value, &kind_pad);
    return WRAP_METHOD(self->inner, VIEW_INDEX_OF, IndexOfFn)(self->inner, pad ? pad->inner : value, index, found);
}

static HRESULT pad_view_get_many(WrapFace *self, UINT32 start, UINT32 capacity, void **items, UINT32 *actual) {
    wrap_note(self, VIEW_GET_MANY);
    HRESULT result = WRAP_METHOD(self->inner, VIEW_GET_MANY, GetManyFn)(self->inner, start, capacity, items, actual);
    if (SUCCEEDED(result) && items && actual) {
        for (UINT32 i = 0; i < *actual; ++i) {
            if (items[i]) {
                items[i] = pad_wrap(items[i], (int)self->owner->data); // NULL only when out of memory
            }
        }
    }
    return result;
}

/* Controller events: the game's handler gets the controller wrapped */
static HRESULT controller_event_add(WrapFace *self, unsigned slot, void *handler, INT64 *token) {
    wrap_note(self, slot);
    void *wrapped = NULL;
    if (handler) {
        wrap_add_ref(handler); // works on any COM object: slot 1 is AddRef
        wrapped = wrap_new(&kind_delegate, handler, NULL, 0);
    }
    HRESULT result = WRAP_METHOD(self->inner, slot, AddEventFn)(self->inner, wrapped ? wrapped : handler, token);
    if (wrapped) {
        wrap_release(wrapped); // the event keeps its own reference
    }
    return result;
}

static HRESULT controller_added(WrapFace *self, void *handler, INT64 *token) {
    return controller_event_add(self, CONTROLLER_ADDED, handler, token);
}
static HRESULT controller_removed(WrapFace *self, void *handler, INT64 *token) {
    return controller_event_add(self, CONTROLLER_REMOVED, handler, token);
}
static HRESULT controller_pairing(WrapFace *self, void *handler, INT64 *token) {
    return controller_event_add(self, CONTROLLER_PAIRING, handler, token);
}

static HRESULT delegate_invoke(WrapFace *self, void *sender, void *args) {
    wrap_note(self, DELEGATE_INVOKE);
    void *wrapped = NULL;
    if (args) {
        wrap_add_ref(args);
        wrapped = wrap_new(&kind_event_args, args, NULL, 0);
    }
    HRESULT result = WRAP_METHOD(self->inner, DELEGATE_INVOKE, InvokeFn)(self->inner, sender, wrapped ? wrapped : args);
    if (wrapped) {
        wrap_release(wrapped);
    }
    return result;
}

static HRESULT event_controller(WrapFace *self, void **out) {
    wrap_note(self, EVENT_CONTROLLER);
    HRESULT result = WRAP_METHOD(self->inner, EVENT_CONTROLLER, GetPointerFn)(self->inner, out);
    if (SUCCEEDED(result) && out && *out) {
        *out = pad_wrap(*out, 1);
        return *out ? result : E_OUTOFMEMORY;
    }
    return result;
}

/* Factories */
static struct {
    const wchar_t *name;
    WrapKind *kind;
    void **vtable;       /* Azimuth's statics vtable the overrides were checked against: the first one seen */
    void *inner[4], *wrapper[4];
} classes[] = {
    {L"Windows.Xbox.System.User", &kind_user_statics, NULL, {0}, {0}},
    {L"Windows.Xbox.Input.Controller", &kind_controller_statics, NULL, {0}, {0}},
    {L"Windows.Xbox.Input.Gamepad", &kind_gamepad_statics, NULL, {0}, {0}},
};
static UINT32 factories_logged[64];
static unsigned factories_logged_count;

static int wide_equal(const wchar_t *a, const wchar_t *b) {
    while (*a && *a == *b) {
        ++a, ++b;
    }
    return *a == *b;
}

static void log_factory(const wchar_t *class_name, const GUID *iid, const char *what) {
    UINT32 hash = 2166136261u;
    for (const wchar_t *c = class_name; *c; ++c) {
        hash = (hash ^ *c) * 16777619u;
    }
    for (unsigned i = 0; iid && i < sizeof(*iid); ++i) {
        hash = (hash ^ ((const unsigned char *)iid)[i]) * 16777619u;
    }
    AcquireSRWLockExclusive(&lock);
    int seen = 0;
    for (unsigned i = 0; i < factories_logged_count; ++i) {
        seen |= factories_logged[i] == hash;
    }
    if (!seen && factories_logged_count < ARRAY_COUNT(factories_logged)) {
        factories_logged[factories_logged_count++] = hash;
    }
    ReleaseSRWLockExclusive(&lock);
    if (!seen) {
        WrapLine line;
        line.length = 0;
        line_add(&line, "TRACE: factory ");
        line_add_wide(&line, class_name);
        line_add(&line, " ");
        line_add_guid(&line, iid);
        line_add(&line, what);
        line_log(&line);
    }
}

void *players_factory(const wchar_t *class_name, const GUID *iid, void *factory) {
    if (!class_name || !factory) {
        return factory;
    }
    for (unsigned c = 0; c < ARRAY_COUNT(classes); ++c) {
        if (!wide_equal(class_name, classes[c].name)) {
            continue;
        }
        void *result = NULL;
        int other = 0;
        AcquireSRWLockExclusive(&lock);
        if (!classes[c].vtable) {
            classes[c].vtable = *(void ***)factory;
        }
        if (classes[c].vtable != *(void ***)factory) {
            other = 1; // another interface's statics: its slots mean other things, leave it alone
        } else {
            unsigned i = 0;
            while (i < ARRAY_COUNT(classes[c].inner) && classes[c].inner[i] && classes[c].inner[i] != factory) {
                ++i;
            }
            if (i < ARRAY_COUNT(classes[c].inner) && classes[c].inner[i] == factory) {
                result = classes[c].wrapper[i]; // a singleton: the same wrapper each time
                wrap_add_ref(result);
                WRAP_METHOD(factory, 2, ULONG (*)(void *))(factory);
            } else if (i < ARRAY_COUNT(classes[c].inner)) {
                result = wrap_new(classes[c].kind, factory, NULL, 0); // takes the game's reference
                if (result) {
                    classes[c].inner[i] = factory;
                    classes[c].wrapper[i] = result;
                    wrap_add_ref(result); // the cache's, for the session: factory stays alive behind it
                    if (classes[c].kind == &kind_user_statics) {
                        user_statics = factory;
                    }
                }
            }
        }
        ReleaseSRWLockExclusive(&lock);
        log_factory(class_name, iid, other ? " left alone (another vtable)" : result ? " wrapped" : " not wrapped");
        return result ? result : factory;
    }
    log_factory(class_name, iid, "");
    return factory;
}

void players_init(void) {
    wrap_init();
    void **all[] = {user_statics_vt, user_view_vt, guest_vt, controller_statics_vt, gamepad_statics_vt,
                    pad_view_vt, pad_vt, pad_controller_vt, delegate_vt, event_args_vt};
    for (unsigned i = 0; i < ARRAY_COUNT(all); ++i) {
        wrap_vtable(all[i]);
    }
    user_statics_vt[USER_STATICS_COUNT] = (void *)user_statics_count;
    user_statics_vt[USER_STATICS_USERS] = (void *)user_statics_users;
    user_statics_vt[USER_STATICS_BY_ID] = (void *)user_statics_by_id;
    user_view_vt[VIEW_GET_AT] = (void *)user_view_get_at;
    user_view_vt[VIEW_SIZE] = (void *)user_view_size;
    user_view_vt[VIEW_INDEX_OF] = (void *)user_view_index_of;
    user_view_vt[VIEW_GET_MANY] = (void *)user_view_get_many;
    guest_vt[USER_ID] = (void *)guest_id;
    guest_vt[USER_IS_GUEST] = (void *)guest_is_guest;
    guest_vt[USER_XBOX_HASH] = (void *)guest_xbox_hash;
    guest_vt[USER_XBOX_ID] = (void *)guest_xbox_id;
    controller_statics_vt[CONTROLLER_STATICS_ALL] = (void *)statics_all;
    controller_statics_vt[CONTROLLER_ADDED] = (void *)controller_added;
    controller_statics_vt[CONTROLLER_REMOVED] = (void *)controller_removed;
    controller_statics_vt[CONTROLLER_PAIRING] = (void *)controller_pairing;
    gamepad_statics_vt[CONTROLLER_STATICS_ALL] = (void *)statics_all;
    pad_view_vt[VIEW_GET_AT] = (void *)pad_view_get_at;
    pad_view_vt[VIEW_INDEX_OF] = (void *)pad_view_index_of;
    pad_view_vt[VIEW_GET_MANY] = (void *)pad_view_get_many;
    pad_controller_vt[CONTROLLER_USER] = (void *)pad_user;
    pad_vt[GAMEPAD_READING] = (void *)pad_reading;
    delegate_vt[DELEGATE_INVOKE] = (void *)delegate_invoke;
    event_args_vt[EVENT_CONTROLLER] = (void *)event_controller;
    WrapKind *kinds[] = {&kind_user_statics, &kind_user_view, &kind_guest, &kind_controller_statics,
                         &kind_gamepad_statics, &kind_pad_view, &kind_pad, &kind_delegate, &kind_event_args};
    for (unsigned i = 0; i < ARRAY_COUNT(kinds); ++i) {
        wrap_watch(kinds[i]);
    }
}
