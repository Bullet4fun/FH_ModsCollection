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
    USER_DISPLAY_INFO = 9,
    USER_IS_GUEST = 10,
    VIEW_GET_AT = 6, /* IVectorView */
    VIEW_SIZE = 7,
    VIEW_INDEX_OF = 8,
    VIEW_GET_MANY = 9,
    CONTROLLER_ID = 6,
    CONTROLLER_TYPE = 7,
    CONTROLLER_USER = 8,
    GAMEPAD_READING = 7, /* IGamepad::GetCurrentReading, an IGamepadReading */
    READING_BUTTONS = 7,
    CONTROLLER_STATICS_ALL = 6, /* Controller::Controllers, and Gamepad::Gamepads on the gamepad statics */
    CONTROLLER_ADDED = 7,       /* add_ControllerAdded; 9 ControllerRemoved, 11 ControllerPairingChanged */
    CONTROLLER_REMOVED = 9,
    CONTROLLER_PAIRING = 11,
    CONTROLLER_ADDED_REMOVE = 8, /* remove_ControllerAdded(token); 10 remove_ControllerRemoved */
    CONTROLLER_REMOVED_REMOVE = 10,
    DISPLAY_NAME = 6, /* IUserDisplayInfo: 6, 8 and 9 all answer the gamertag in Azimuth */
    DISPLAY_NAME_2 = 8,
    DISPLAY_NAME_3 = 9,
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
typedef HRESULT (*RemoveEventFn)(void *, INT64);

static void *user_statics_vt[WRAP_SLOTS], *user_view_vt[WRAP_SLOTS], *guest_vt[WRAP_SLOTS];
static void *controller_statics_vt[WRAP_SLOTS], *gamepad_statics_vt[WRAP_SLOTS], *pad_view_vt[WRAP_SLOTS];
static void *pad_vt[WRAP_SLOTS], *pad_controller_vt[WRAP_SLOTS], *delegate_vt[WRAP_SLOTS], *event_args_vt[WRAP_SLOTS];
static void *display_info_vt[WRAP_SLOTS], *storage_vt[WRAP_SLOTS];

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
static WrapKind kind_display_info = {.name = "GuestDisplayInfo", .primary = display_info_vt};
static WrapKind kind_storage = {.name = "ConnectedStorageSpace", .primary = storage_vt};

static SRWLOCK lock = SRWLOCK_INIT;
static void *const *string_create, *const *string_buffer, *const *string_delete;
static void *user_statics; /* Azimuth's, for the guest's own user object */
static void *guest;        /* the guest's wrapper, kept for the whole session */
static UINT64 first_pad;  /* the device that pressed A first: player 1's, all the others the guest's */
static int first_pad_known;
static void *controller_statics; /* the mod's wrapper: the sender of the events the mod makes */
static void *guest_display;      /* the guest's display info wrapper, kept for the session */
/* Pads the guest plays with, and pads waiting to be shown to the game again as the guest's. */
static UINT64 joined[8], waiting[8];
static unsigned joined_count, waiting_count;
/* One wrapper per device while it is plugged in: the game compares controller pointers, so a pad it
   sees again must be the very object it already holds. */
static struct {
    UINT64 id;
    void *wrapper;
} pads[8];
static unsigned pads_count;
/* Each pad's A button: a pad must be seen with A up before its press counts, so a pad that reads A
   held from the start (a key still down, or an emulated device) never becomes player 1. */
static struct {
    UINT64 id;
    int a_was_up;
} pad_buttons[8];
static unsigned pad_buttons_count;
/* The game's ControllerAdded and ControllerRemoved handlers (the mod's wrappers around them). */
static struct {
    INT64 token;
    void *handler;
    unsigned slot;
} handlers[16];
static unsigned handlers_count;

static int id_in(const UINT64 *ids, unsigned count, UINT64 id) {
    for (unsigned i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

static void id_drop(UINT64 *ids, unsigned *count, UINT64 id) {
    for (unsigned i = 0; i < *count; ++i) {
        if (ids[i] == id) {
            ids[i] = ids[--*count];
            --i;
        }
    }
}

/* Under lock: a pad other than player 1's that is not the guest's yet waits for players_tick. */
static void wait_to_join(UINT64 id) {
    if (first_pad_known && id != first_pad && !id_in(joined, joined_count, id) &&
        !id_in(waiting, waiting_count, id) && waiting_count < ARRAY_COUNT(waiting)) {
        waiting[waiting_count++] = id;
    }
}

static void log_text(const char *a, const char *b) {
    WrapLine line;
        line.length = 0;
    line_add(&line, a);
    line_add(&line, b);
    line_log(&line);
}

/* The guest: user 1's object behind a wrapper with its own id and guest flag. Its Xbox user id and
   hash stay user 1's, whatever they are, so the game can find user 1's garage for it. */
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

void players_strings(void *const *create, void *const *buffer, void *const *destroy) {
    string_create = create;
    string_buffer = buffer;
    string_delete = destroy;
}

/* The guest's name in every name slot of its display info (the game reads it once, at boot). */
static HRESULT display_name(WrapFace *self, unsigned slot, void **out) {
    wrap_note(self, slot);
    if (out && string_create && *string_create &&
        SUCCEEDED(((HRESULT (*)(const wchar_t *, UINT32, void **))*string_create)(L"Guest", 5, out))) {
        return S_OK;
    }
    return WRAP_METHOD(self->inner, slot, GetPointerFn)(self->inner, out);
}
static HRESULT display_name_1(WrapFace *self, void **out) { return display_name(self, DISPLAY_NAME, out); }
static HRESULT display_name_2(WrapFace *self, void **out) { return display_name(self, DISPLAY_NAME_2, out); }
static HRESULT display_name_3(WrapFace *self, void **out) { return display_name(self, DISPLAY_NAME_3, out); }

static HRESULT guest_display_info(WrapFace *self, void **out) {
    wrap_note(self, USER_DISPLAY_INFO);
    void *info = NULL;
    HRESULT result = WRAP_METHOD(self->inner, USER_DISPLAY_INFO, GetPointerFn)(self->inner, &info);
    if (FAILED(result) || !info || !out) {
        if (out) {
            *out = info;
        }
        return result;
    }
    /* Azimuth hands out its own object (whether with a reference is unknown): never released here,
       one wrapper on it for the session. shortcut: leaks one reference if Azimuth added one. */
    AcquireSRWLockExclusive(&lock);
    if (!guest_display) {
        wrap_add_ref(info);
        guest_display = wrap_new(&kind_display_info, info, NULL, 0);
    }
    void *wrapper = guest_display;
    if (wrapper) {
        wrap_add_ref(wrapper);
    }
    ReleaseSRWLockExclusive(&lock);
    *out = wrapper ? wrapper : info;
    return result;
}


/* Pads */
static int device_id(void *object, int controller, UINT64 *id) {
    if (controller) {
        return SUCCEEDED(WRAP_METHOD(object, CONTROLLER_ID, GetU64Fn)(object, id));
    }
    void *as_controller = NULL;
    if (FAILED(WRAP_METHOD(object, 0, QueryFn)(object, &IID_IController, &as_controller)) || !as_controller) {
        return 0;
    }
    int known = SUCCEEDED(WRAP_METHOD(as_controller, CONTROLLER_ID, GetU64Fn)(as_controller, id));
    WRAP_METHOD(as_controller, 2, ULONG (*)(void *))(as_controller);
    return known;
}

/* The device's wrapper, as the interface inner is (a controller or a gamepad). Takes inner's reference. */
static void *pad_wrap(void *inner, int controller) {
    void **vtable = controller ? pad_controller_vt : pad_vt;
    UINT64 id = 0;
    if (!device_id(inner, controller, &id)) {
        return wrap_new(&kind_pad, inner, vtable, 0);
    }
    void *wrapper = NULL;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < pads_count && !wrapper; ++i) {
        if (pads[i].id == id) {
            wrapper = pads[i].wrapper;
            wrap_add_ref(wrapper);
        }
    }
    if (!wrapper && pads_count < ARRAY_COUNT(pads)) {
        wrapper = wrap_new(&kind_pad, inner, vtable, 0);
        if (wrapper) {
            wrap_add_ref(wrapper); // the table's
            pads[pads_count].id = id;
            pads[pads_count++].wrapper = wrapper;
        }
        ReleaseSRWLockExclusive(&lock);
        return wrapper;
    }
    ReleaseSRWLockExclusive(&lock);
    if (!wrapper) { // more devices than the table holds
        return wrap_new(&kind_pad, inner, vtable, 0);
    }
    void *face = NULL;
    WRAP_METHOD(wrapper, 0, QueryFn)(wrapper, controller ? &IID_IController : &IID_IGamepad, &face);
    wrap_release(wrapper);
    if (!face) {
        return wrap_new(&kind_pad, inner, vtable, 0);
    }
    WRAP_METHOD(inner, 2, ULONG (*)(void *))(inner);
    return face;
}

/* A pad that left: when it comes back it joins as the guest again. Player 1's pad comes back with
   another device id, so player 1 is chosen again by the next A press of a pad not the guest's. */
static void pad_gone(UINT64 id) {
    void *wrapper = NULL;
    AcquireSRWLockExclusive(&lock);
    int player_1 = first_pad_known && id == first_pad;
    if (player_1) {
        first_pad_known = 0;
    }
    id_drop(joined, &joined_count, id);
    id_drop(waiting, &waiting_count, id);
    for (unsigned i = 0; i < pad_buttons_count; ++i) { // a pad back gets a new device id: keep room
        if (pad_buttons[i].id == id) {
            pad_buttons[i] = pad_buttons[--pad_buttons_count];
            break;
        }
    }
    for (unsigned i = 0; i < pads_count; ++i) {
        if (pads[i].id == id) {
            wrapper = pads[i].wrapper;
            pads[i] = pads[--pads_count];
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (wrapper) {
        wrap_release(wrapper);
    }
    if (player_1) {
        log_text("PLAYERS: ", "player 1's pad left; the next pad to press A is player 1");
    }
}

/* Until a pad presses A every pad is user 1's, as in Azimuth: the game signs in whoever starts it. */
static HRESULT pad_user(WrapFace *self, void **out) {
    wrap_note(self, CONTROLLER_USER);
    UINT64 id = 0;
    if (out && SUCCEEDED(WRAP_METHOD(self->inner, CONTROLLER_ID, GetU64Fn)(self->inner, &id)) &&
        first_pad_known && id != first_pad && id_in(joined, joined_count, id) &&
        (*out = guest_ref()) != NULL) {
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

static void log_new_pad(void *controller, UINT64 id) {
    WrapLine line;
    line.length = 0;
    line_add(&line, "PLAYERS: device id ");
    line_add_number(&line, id);
    line_add(&line, " reads buttons, type ");
    void *type = NULL;
    if (string_buffer && string_delete && *string_buffer && *string_delete &&
        SUCCEEDED(WRAP_METHOD(controller, CONTROLLER_TYPE, GetPointerFn)(controller, &type))) {
        UINT32 length = 0;
        const wchar_t *text = ((const wchar_t *(*)(void *, UINT32 *))*string_buffer)(type, &length);
        wchar_t name[64];
        UINT32 n = 0;
        for (; text && n < length && n < ARRAY_COUNT(name) - 1; ++n) {
            name[n] = text[n];
        }
        name[n] = 0;
        line_add_wide(&line, name);
        ((HRESULT (*)(void *))*string_delete)(type);
    } else {
        line_add(&line, "unknown");
    }
    line_log(&line);
}

static HRESULT pad_reading(WrapFace *self, void **out) {
    wrap_note(self, GAMEPAD_READING);
    HRESULT result = WRAP_METHOD(self->inner, GAMEPAD_READING, GetPointerFn)(self->inner, out);
    UINT32 buttons = 0;
    void *controller = NULL;
    UINT64 id = 0;
    if (FAILED(result) || !out || !*out || first_pad_known ||
        FAILED(WRAP_METHOD(*out, READING_BUTTONS, GetU32Fn)(*out, &buttons)) ||
        FAILED(WRAP_METHOD(self->inner, 0, QueryFn)(self->inner, &IID_IController, &controller)) || !controller) {
        return result;
    }
    int known = SUCCEEDED(WRAP_METHOD(controller, CONTROLLER_ID, GetU64Fn)(controller, &id));
    int added = 0, first = 0;
    AcquireSRWLockExclusive(&lock);
    unsigned i = 0;
    while (i < pad_buttons_count && pad_buttons[i].id != id) {
        ++i;
    }
    if (known && i == pad_buttons_count && i < ARRAY_COUNT(pad_buttons)) {
        pad_buttons[i].id = id;
        pad_buttons[i].a_was_up = 0;
        ++pad_buttons_count;
        added = 1;
    }
    if (known && i < pad_buttons_count) {
        if (!(buttons & BUTTON_A)) {
            pad_buttons[i].a_was_up = 1;
        } else if (pad_buttons[i].a_was_up && !first_pad_known && !id_in(joined, joined_count, id)) {
            first_pad = id;
            first_pad_known = first = 1;
            for (unsigned j = 0; j < pads_count; ++j) {
                wait_to_join(pads[j].id); // pads already plugged in: the game asked whose they are at boot
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (added) {
        log_new_pad(controller, id);
    }
    WRAP_METHOD(controller, 2, ULONG (*)(void *))(controller);
    if (first) {
        WrapLine line;
        line.length = 0;
        line_add(&line, "PLAYERS: the pad with device id ");
        line_add_number(&line, id);
        line_add(&line, " pressed A first: it is player 1, any other pad joins as the guest");
        line_log(&line);
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
        wrapped = wrap_new(&kind_delegate, handler, NULL, slot); // data: which event it handles
    }
    HRESULT result = WRAP_METHOD(self->inner, slot, AddEventFn)(self->inner, wrapped ? wrapped : handler, token);
    if (wrapped && SUCCEEDED(result) && token && slot != CONTROLLER_PAIRING) {
        AcquireSRWLockExclusive(&lock);
        if (handlers_count < ARRAY_COUNT(handlers)) {
            handlers[handlers_count].token = *token;
            handlers[handlers_count].slot = slot;
            handlers[handlers_count++].handler = wrapped;
            wrapped = NULL; // the list keeps this reference
        }
        ReleaseSRWLockExclusive(&lock);
    }
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

static HRESULT controller_event_remove(WrapFace *self, unsigned slot, INT64 token) {
    wrap_note(self, slot);
    HRESULT result = WRAP_METHOD(self->inner, slot, RemoveEventFn)(self->inner, token);
    void *dropped = NULL;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < handlers_count; ++i) {
        if (handlers[i].token == token && handlers[i].slot == slot - 1) { // add is the slot before remove
            dropped = handlers[i].handler;
            handlers[i] = handlers[--handlers_count];
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (dropped) {
        wrap_release(dropped);
    }
    return result;
}
static HRESULT controller_added_remove(WrapFace *self, INT64 token) {
    return controller_event_remove(self, CONTROLLER_ADDED_REMOVE, token);
}
static HRESULT controller_removed_remove(WrapFace *self, INT64 token) {
    return controller_event_remove(self, CONTROLLER_REMOVED_REMOVE, token);
}

/* The device id of a ControllerAdded or ControllerRemoved event, 0 for any other event. */
static int event_device(WrapFace *self, void *args, UINT64 *id) {
    unsigned event = (unsigned)self->owner->data;
    void *controller = NULL;
    if (!args || (event != CONTROLLER_ADDED && event != CONTROLLER_REMOVED) ||
        FAILED(WRAP_METHOD(args, EVENT_CONTROLLER, GetPointerFn)(args, &controller)) || !controller) {
        return 0;
    }
    int known = device_id(controller, 1, id);
    WRAP_METHOD(controller, 2, ULONG (*)(void *))(controller);
    return known;
}

static HRESULT delegate_invoke(WrapFace *self, void *sender, void *args) {
    wrap_note(self, DELEGATE_INVOKE);
    UINT64 id = 0;
    int device = event_device(self, args, &id);
    if (device && self->owner->data == CONTROLLER_ADDED) {
        AcquireSRWLockExclusive(&lock);
        wait_to_join(id); // plugged in after player 1 chose: the guest's
        ReleaseSRWLockExclusive(&lock);
    }
    void *wrapped = NULL;
    if (args) {
        wrap_add_ref(args);
        wrapped = wrap_new(&kind_event_args, args, NULL, 0);
    }
    HRESULT result = WRAP_METHOD(self->inner, DELEGATE_INVOKE, InvokeFn)(self->inner, sender, wrapped ? wrapped : args);
    if (wrapped) {
        wrap_release(wrapped);
    }
    if (device && self->owner->data == CONTROLLER_REMOVED) {
        pad_gone(id); // after the game let go of the very wrapper it held
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

/* Event arguments the mod makes to show the game a pad again: only the controller. */
typedef struct {
    void **vtable;
    volatile LONG refs;
    void *controller;
} PadEventArgs;

static HRESULT pad_args_query(PadEventArgs *self, const GUID *iid, void **out) {
    (void)iid; // the game only reads the controller of the arguments it gets
    InterlockedIncrement(&self->refs);
    *out = self;
    return S_OK;
}
static ULONG pad_args_add_ref(PadEventArgs *self) { return (ULONG)InterlockedIncrement(&self->refs); }
static ULONG pad_args_release(PadEventArgs *self) {
    LONG refs = InterlockedDecrement(&self->refs);
    if (!refs) {
        wrap_release(self->controller);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)refs;
}
static HRESULT pad_args_iids(PadEventArgs *self, ULONG *count, GUID **iids) {
    (void)self;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT pad_args_none(PadEventArgs *self, void **out) { // class name: none, and trust level: base
    (void)self;
    *out = NULL;
    return S_OK;
}
static HRESULT pad_args_controller(PadEventArgs *self, void **out) {
    wrap_add_ref(self->controller);
    *out = self->controller;
    return S_OK;
}
static void *pad_args_vt[] = {(void *)pad_args_query, (void *)pad_args_add_ref, (void *)pad_args_release,
                              (void *)pad_args_iids,  (void *)pad_args_none,    (void *)pad_args_none,
                              (void *)pad_args_controller};

/* Calls the game's own handlers of one event, past the mod's (no pad really leaves). */
static void fire(unsigned slot, void *args) {
    void *calls[ARRAY_COUNT(handlers)];
    unsigned count = 0;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < handlers_count; ++i) {
        if (handlers[i].slot == slot) {
            calls[count] = handlers[i].handler;
            wrap_add_ref(calls[count++]);
        }
    }
    void *sender = controller_statics;
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i = 0; i < count; ++i) {
        void *game_handler = ((WrapFace *)calls[i])->inner;
        WRAP_METHOD(game_handler, DELEGATE_INVOKE, InvokeFn)(game_handler, sender, args);
        wrap_release(calls[i]);
    }
}

void players_tick(void) {
    UINT64 id = 0;
    AcquireSRWLockShared(&lock);
    int pending = waiting_count != 0;
    if (pending) {
        id = waiting[0];
    }
    ReleaseSRWLockShared(&lock);
    if (!pending) {
        return;
    }
    void *controller = NULL;
    AcquireSRWLockExclusive(&lock);
    if (id_in(waiting, waiting_count, id)) { // still plugged in
        id_drop(waiting, &waiting_count, id);
        if (joined_count < ARRAY_COUNT(joined)) {
            joined[joined_count++] = id;
        }
        for (unsigned i = 0; i < pads_count && !controller; ++i) {
            if (pads[i].id == id) {
                controller = pads[i].wrapper;
                wrap_add_ref(controller);
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    WrapLine line;
    line.length = 0;
    line_add(&line, "PLAYERS: the pad with device id ");
    line_add_number(&line, id);
    line_add(&line, controller ? " joins as the guest" : " left before joining as the guest");
    line_log(&line);
    if (!controller) {
        return;
    }
    void *face = NULL;
    WRAP_METHOD(controller, 0, QueryFn)(controller, &IID_IController, &face);
    wrap_release(controller);
    PadEventArgs *args = face ? HeapAlloc(GetProcessHeap(), 0, sizeof(PadEventArgs)) : NULL;
    if (!args) {
        if (face) {
            wrap_release(face);
        }
        return;
    }
    args->vtable = pad_args_vt;
    args->refs = 1;
    args->controller = face;
    fire(CONTROLLER_REMOVED, args); // the game lets go of the pad it gave user 1...
    fire(CONTROLLER_ADDED, args);   // ...and takes it back, asking whose it is
    pad_args_release(args);
}

/* Save storage (ConnectedStorageSpace statics): logs which user each call is for, and never lets
   Azimuth see the guest's wrapper, which is not its own object: the guest goes to Azimuth as user 1. */
typedef HRESULT (*Call5Fn)(void *, void *, void *, void *, void *, void *);
static volatile LONG storage_logged[WRAP_SLOTS];

static int is_guest_face(void *arg) {
    Wrap *own = guest ? ((WrapFace *)guest)->owner : NULL;
    return own && (char *)arg >= (char *)&own->face[0] && (char *)arg < (char *)&own->face[WRAP_FACES];
}

/* shortcut: passes on five arguments, enough for the statics' user and out pointer; a method with
   more stack arguments would lose them, so check the trace before overriding past slot 13. */
static HRESULT storage_call(WrapFace *self, unsigned slot, void *a, void *b, void *c, void *d, void *e) {
    wrap_note(self, slot);
    void *user_1 = guest ? ((WrapFace *)guest)->inner : NULL;
    LONG seen = 0;
    if (is_guest_face(a)) {
        a = user_1;
        seen = 4;
    } else if (is_guest_face(b)) {
        b = user_1;
        seen = 4;
    } else if (a && (a == user_1 || b == user_1)) {
        seen = 2;
    } else {
        seen = 1;
    }
    if (!(InterlockedOr(&storage_logged[slot], seen) & seen)) {
        WrapLine line;
        line.length = 0;
        line_add(&line, "TRACE: save storage slot ");
        line_add_number(&line, slot);
        line_add(&line, seen == 4 ? " for the guest (handed on as user 1)" : seen == 2 ? " for user 1" : " without a user");
        line_log(&line);
    }
    return WRAP_METHOD(self->inner, slot, Call5Fn)(self->inner, a, b, c, d, e);
}
#define STORAGE_SLOT(n)                                                                               \
    static HRESULT storage_##n(WrapFace *self, void *a, void *b, void *c, void *d, void *e) {         \
        return storage_call(self, n, a, b, c, d, e);                                                  \
    }
STORAGE_SLOT(6)
STORAGE_SLOT(7)
STORAGE_SLOT(8)
STORAGE_SLOT(9)
STORAGE_SLOT(10)
STORAGE_SLOT(11)
STORAGE_SLOT(12)
STORAGE_SLOT(13)

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
    {L"Windows.Xbox.Storage.ConnectedStorageSpace", &kind_storage, NULL, {0}, {0}},
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
                    if (classes[c].kind == &kind_controller_statics) {
                        controller_statics = result;
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
                    pad_view_vt, pad_vt, pad_controller_vt, delegate_vt, event_args_vt, display_info_vt, storage_vt};
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
    guest_vt[USER_DISPLAY_INFO] = (void *)guest_display_info;
    void *storage_slots[] = {(void *)storage_6,  (void *)storage_7,  (void *)storage_8,  (void *)storage_9,
                             (void *)storage_10, (void *)storage_11, (void *)storage_12, (void *)storage_13};
    for (unsigned i = 0; i < ARRAY_COUNT(storage_slots); ++i) {
        storage_vt[6 + i] = storage_slots[i];
    }
    display_info_vt[DISPLAY_NAME] = (void *)display_name_1;
    display_info_vt[DISPLAY_NAME_2] = (void *)display_name_2;
    display_info_vt[DISPLAY_NAME_3] = (void *)display_name_3;
    controller_statics_vt[CONTROLLER_STATICS_ALL] = (void *)statics_all;
    controller_statics_vt[CONTROLLER_ADDED] = (void *)controller_added;
    controller_statics_vt[CONTROLLER_REMOVED] = (void *)controller_removed;
    controller_statics_vt[CONTROLLER_PAIRING] = (void *)controller_pairing;
    controller_statics_vt[CONTROLLER_ADDED_REMOVE] = (void *)controller_added_remove;
    controller_statics_vt[CONTROLLER_REMOVED_REMOVE] = (void *)controller_removed_remove;
    gamepad_statics_vt[CONTROLLER_STATICS_ALL] = (void *)statics_all;
    pad_view_vt[VIEW_GET_AT] = (void *)pad_view_get_at;
    pad_view_vt[VIEW_INDEX_OF] = (void *)pad_view_index_of;
    pad_view_vt[VIEW_GET_MANY] = (void *)pad_view_get_many;
    pad_controller_vt[CONTROLLER_USER] = (void *)pad_user;
    pad_vt[GAMEPAD_READING] = (void *)pad_reading;
    delegate_vt[DELEGATE_INVOKE] = (void *)delegate_invoke;
    event_args_vt[EVENT_CONTROLLER] = (void *)event_controller;
    WrapKind *kinds[] = {&kind_user_statics, &kind_user_view, &kind_guest, &kind_controller_statics,
                         &kind_gamepad_statics, &kind_pad_view, &kind_pad, &kind_delegate, &kind_event_args,
                         &kind_display_info, &kind_storage};
    for (unsigned i = 0; i < ARRAY_COUNT(kinds); ++i) {
        wrap_watch(kinds[i]);
    }
}
