/* FM5 Split-Screen's wrappers against fake Azimuth objects: forwarding, QueryInterface identity,
   reference counts, the guest user, the users list, the second pad's user and controller events. */
#include <stdio.h>
#include <winstring.h>
#include "../fm5-split-screen/players.h"

static int failures;
#define CHECK(condition)                                                                               \
    do {                                                                                               \
        if (!(condition)) {                                                                            \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                                        \
            ++failures;                                                                                \
        }                                                                                              \
    } while (0)

static const GUID IID_IController = {0x9776ff56, 0x9bfd, 0x4581, {0xad, 0x45, 0xb6, 0x45, 0xbb, 0xa5, 0x26, 0xd6}};
static const GUID IID_Other = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};

/* A fake object with up to two interfaces (faces); face 1 answers IID_IController. */
typedef struct Fake Fake;
typedef struct {
    void **vtable;
    Fake *owner;
} FakeFace;
struct Fake {
    FakeFace face[2];
    LONG refs;
    UINT64 id;
    void *items[4];
    UINT32 item_count;
    void *handler;
};

static HRESULT fake_query(FakeFace *self, const GUID *iid, void **out) {
    FakeFace *face = &self->owner->face[self->owner->face[1].vtable && IsEqualGUID(iid, &IID_IController)];
    ++self->owner->refs;
    *out = face;
    return S_OK;
}
static ULONG fake_add_ref(FakeFace *self) { return (ULONG)++self->owner->refs; }
static ULONG fake_release(FakeFace *self) { return (ULONG)--self->owner->refs; }
static HRESULT fake_class_name(FakeFace *self, UINT32 *out) {
    (void)self;
    *out = 77;
    return S_OK;
}

static Fake user, users, statics, pads[2], pad_list, controller_list, controller_statics, gamepad_statics, args;

static HRESULT user_id(FakeFace *self, UINT32 *out) {
    (void)self;
    *out = 1;
    return S_OK;
}
static HRESULT user_flag(FakeFace *self, unsigned char *out) {
    (void)self;
    *out = 0;
    return S_OK;
}
static HRESULT user_string(FakeFace *self, HSTRING *out) {
    (void)self;
    return WindowsCreateString(L"25", 2, out);
}
static HRESULT statics_count(FakeFace *self, UINT32 *out) {
    (void)self;
    *out = 1;
    return S_OK;
}
static HRESULT statics_users(FakeFace *self, void **out) {
    (void)self;
    ++users.refs;
    *out = &users.face[0];
    return S_OK;
}
static HRESULT statics_by_id(FakeFace *self, UINT32 id, void **out) {
    (void)self;
    *out = NULL;
    if (id != 1) {
        return E_INVALIDARG;
    }
    ++user.refs;
    *out = &user.face[0];
    return S_OK;
}
static HRESULT list_get_at(FakeFace *self, UINT32 index, void **out) {
    if (index >= self->owner->item_count) {
        return E_BOUNDS;
    }
    FakeFace *item = self->owner->items[index];
    ++item->owner->refs;
    *out = item;
    return S_OK;
}
static HRESULT list_size(FakeFace *self, UINT32 *out) {
    *out = self->owner->item_count;
    return S_OK;
}
static HRESULT list_get_many(FakeFace *self, UINT32 start, UINT32 capacity, void **items, UINT32 *actual) {
    *actual = 0;
    for (UINT32 i = start; i < self->owner->item_count && *actual < capacity; ++i) {
        list_get_at(self, i, &items[(*actual)++]);
    }
    return S_OK;
}
static HRESULT pad_id(FakeFace *self, UINT64 *out) {
    *out = self->owner->id;
    return S_OK;
}
static HRESULT pad_user(FakeFace *self, void **out) { return statics_by_id(self, 1, out); }
static Fake reading;
static UINT32 reading_buttons;
static HRESULT reading_get_buttons(FakeFace *self, UINT32 *out) {
    (void)self;
    *out = reading_buttons;
    return S_OK;
}
static HRESULT pad_reading(FakeFace *self, void **out) {
    (void)self;
    ++reading.refs;
    *out = &reading.face[0];
    return S_OK;
}
static HRESULT pads_all(FakeFace *self, void **out) {
    Fake *list = self->owner == &controller_statics ? &controller_list : &pad_list;
    ++list->refs;
    *out = &list->face[0];
    return S_OK;
}
static HRESULT add_event(FakeFace *self, void *handler, INT64 *token) {
    ((ULONG (*)(void *))(*(void ***)handler)[1])(handler);
    self->owner->handler = handler;
    *token = 1;
    return S_OK;
}
static HRESULT args_controller(FakeFace *self, void **out) {
    (void)self;
    ++pads[1].refs;
    *out = &pads[1].face[1];
    return S_OK;
}

static void *base_vtable[16] = {(void *)fake_query, (void *)fake_add_ref, (void *)fake_release, 0, 0, (void *)fake_class_name};
static void *reading_vt[16], *user_vt[16], *statics_vt[16], *list_vt[16], *gamepad_vt[16], *controller_vt[16], *pad_statics_vt[16], *args_vt[16];

static void fake_init(Fake *fake, void **face0, void **face1) {
    fake->face[0].vtable = face0;
    fake->face[0].owner = fake;
    fake->face[1].vtable = face1;
    fake->face[1].owner = fake;
    fake->refs = 1;
}

/* The game's handler: records the controller it gets and that controller's user. */
static void *handled_user;
static HRESULT handler_invoke(void *self, void *sender, void *event_args) {
    (void)self;
    (void)sender;
    void *controller = NULL;
    ((HRESULT (*)(void *, void **))(*(void ***)event_args)[6])(event_args, &controller);
    ((HRESULT (*)(void *, void **))(*(void ***)controller)[8])(controller, &handled_user);
    ((ULONG (*)(void *))(*(void ***)controller)[2])(controller);
    return S_OK;
}
static Fake handler;
static void *handler_vt[16];

#define CALL(object, slot, type) ((type)(*(void ***)(object))[slot])

int main(void) {
    for (int i = 0; i < 16; ++i) {
        reading_vt[i] = user_vt[i] = statics_vt[i] = list_vt[i] = gamepad_vt[i] = controller_vt[i] = pad_statics_vt[i] = args_vt[i] =
            handler_vt[i] = base_vtable[i];
    }
    user_vt[6] = (void *)user_id;
    user_vt[10] = (void *)user_flag;
    user_vt[14] = user_vt[15] = (void *)user_string;
    statics_vt[6] = (void *)statics_count;
    statics_vt[9] = (void *)statics_users;
    statics_vt[14] = (void *)statics_by_id;
    list_vt[6] = (void *)list_get_at;
    list_vt[7] = (void *)list_size;
    list_vt[9] = (void *)list_get_many;
    controller_vt[6] = (void *)pad_id;
    controller_vt[8] = (void *)pad_user;
    pad_statics_vt[6] = (void *)pads_all;
    pad_statics_vt[7] = (void *)add_event;
    args_vt[6] = (void *)args_controller;
    reading_vt[7] = (void *)reading_get_buttons;
    gamepad_vt[7] = (void *)pad_reading;
    fake_init(&reading, reading_vt, NULL);
    handler_vt[3] = (void *)handler_invoke;
    fake_init(&user, user_vt, NULL);
    fake_init(&users, list_vt, NULL);
    users.items[0] = &user.face[0];
    users.item_count = 1;
    fake_init(&statics, statics_vt, NULL);
    for (int i = 0; i < 2; ++i) {
        fake_init(&pads[i], gamepad_vt, controller_vt);
        pads[i].id = i ? 9 : 5;
    }
    fake_init(&pad_list, list_vt, NULL);
    pad_list.items[0] = &pads[0].face[0];
    pad_list.items[1] = &pads[1].face[0];
    pad_list.item_count = 2;
    fake_init(&controller_list, list_vt, NULL);
    controller_list.items[0] = &pads[1].face[1];
    controller_list.item_count = 1;
    fake_init(&controller_statics, pad_statics_vt, NULL);
    fake_init(&gamepad_statics, pad_statics_vt, NULL);
    fake_init(&args, args_vt, NULL);
    fake_init(&handler, handler_vt, NULL);

    players_init();
    static void *create = (void *)WindowsCreateString, *buffer = (void *)WindowsGetStringRawBuffer,
                *destroy = (void *)WindowsDeleteString;
    players_strings(&create, &buffer, &destroy);
    GUID iid = {0};
    // other classes pass through untouched
    CHECK(players_factory(L"Windows.Xbox.UI.SystemUI", &iid, &args.face[0]) == &args.face[0]);

    // the user statics: wrapped once, the same wrapper each time
    void *wrapped_statics = players_factory(L"Windows.Xbox.System.User", &iid, &statics.face[0]);
    CHECK(wrapped_statics && wrapped_statics != &statics.face[0]);
    ++statics.refs; // the game's second activation
    CHECK(players_factory(L"Windows.Xbox.System.User", &iid, &statics.face[0]) == wrapped_statics);
    CHECK(statics.refs == 1); // only the wrapper's
    UINT32 class_name = 0;
    CHECK(CALL(wrapped_statics, 5, HRESULT (*)(void *, UINT32 *))(wrapped_statics, &class_name) == S_OK && class_name == 77);

    UINT32 count = 0;
    CALL(wrapped_statics, 6, HRESULT (*)(void *, UINT32 *))(wrapped_statics, &count);
    CHECK(count == 2);

    // the guest: user 1 behind its own id, guest flag and Xbox user id
    void *guest = NULL, *first = NULL;
    CHECK(CALL(wrapped_statics, 14, HRESULT (*)(void *, UINT32, void **))(wrapped_statics, 2, &guest) == S_OK && guest);
    CHECK(CALL(wrapped_statics, 14, HRESULT (*)(void *, UINT32, void **))(wrapped_statics, 1, &first) == S_OK &&
          first == &user.face[0]);
    UINT32 id = 0;
    unsigned char is_guest = 0;
    CALL(guest, 6, HRESULT (*)(void *, UINT32 *))(guest, &id);
    CALL(guest, 10, HRESULT (*)(void *, unsigned char *))(guest, &is_guest);
    CHECK(id == 2 && is_guest == 1);
    HSTRING xbox_id = NULL;
    CHECK(CALL(guest, 15, HRESULT (*)(void *, HSTRING *))(guest, &xbox_id) == S_OK);
    UINT32 length = 0;
    const wchar_t *text = WindowsGetStringRawBuffer(xbox_id, &length);
    CHECK(length == 3 && text[0] == L'2' && text[1] == L'5' && text[2] == L'2');
    WindowsDeleteString(xbox_id);
    void *same = NULL;
    CHECK(CALL(guest, 0, HRESULT (*)(void *, const GUID *, void **))(guest, &IID_Other, &same) == S_OK && same == guest);
    wrap_release(same);

    // the users list ends with the guest
    void *list = NULL;
    CALL(wrapped_statics, 9, HRESULT (*)(void *, void **))(wrapped_statics, &list);
    UINT32 size = 0;
    CALL(list, 7, HRESULT (*)(void *, UINT32 *))(list, &size);
    CHECK(size == 2);
    void *at = NULL;
    CHECK(CALL(list, 6, HRESULT (*)(void *, UINT32, void **))(list, 1, &at) == S_OK && at == guest);
    wrap_release(at);
    void *items[4] = {0};
    UINT32 actual = 0;
    CALL(list, 9, HRESULT (*)(void *, UINT32, UINT32, void **, UINT32 *))(list, 0, 4, items, &actual);
    CHECK(actual == 2 && items[0] == &user.face[0] && items[1] == guest);
    wrap_release(items[1]);
    --user.refs;
    LONG users_before = users.refs;
    wrap_release(list);
    CHECK(users.refs == users_before - 1);

    // gamepads: the second by device id belongs to the guest, the first to user 1
    void *gamepads_statics = players_factory(L"Windows.Xbox.Input.Gamepad", &iid, &gamepad_statics.face[0]);
    void *gamepads = NULL;
    CALL(gamepads_statics, 6, HRESULT (*)(void *, void **))(gamepads_statics, &gamepads);
    void *pad_objects[2] = {0};
    for (UINT32 i = 0; i < 2; ++i) {
        CALL(gamepads, 6, HRESULT (*)(void *, UINT32, void **))(gamepads, i, &pad_objects[i]);
    }
    void *controllers[2] = {0}, *owners[2] = {0};
    for (int i = 0; i < 2; ++i) {
        CHECK(pad_objects[i] != &pads[i].face[0]);
        CALL(pad_objects[i], 0, HRESULT (*)(void *, const GUID *, void **))(pad_objects[i], &IID_IController, &controllers[i]);
        CHECK(controllers[i] && controllers[i] != pad_objects[i]);
        void *again = NULL;
        CALL(pad_objects[i], 0, HRESULT (*)(void *, const GUID *, void **))(pad_objects[i], &IID_IController, &again);
        CHECK(again == controllers[i]);
        wrap_release(again);
        CALL(controllers[i], 8, HRESULT (*)(void *, void **))(controllers[i], &owners[i]);
    }
    CHECK(owners[0] == &user.face[0] && owners[1] == &user.face[0]); // no pad pressed A yet: all user 1's
    user.refs -= 2;
    // the second pad reads B, then the first reads A: the first is player 1's, the second the guest's
    void *state = NULL;
    reading_buttons = 0x20;
    CALL(pad_objects[1], 7, HRESULT (*)(void *, void **))(pad_objects[1], &state);
    CHECK(state == &reading.face[0]);
    --reading.refs;
    reading_buttons = 0x10;
    CALL(pad_objects[0], 7, HRESULT (*)(void *, void **))(pad_objects[0], &state);
    --reading.refs;
    reading_buttons = 0x10;
    CALL(pad_objects[1], 7, HRESULT (*)(void *, void **))(pad_objects[1], &state); // too late to be player 1
    --reading.refs;
    for (int i = 0; i < 2; ++i) {
        CALL(controllers[i], 8, HRESULT (*)(void *, void **))(controllers[i], &owners[i]);
    }
    CHECK(owners[0] == &user.face[0] && owners[1] == guest);
    wrap_release(owners[1]);
    --user.refs;
    for (int i = 0; i < 2; ++i) {
        wrap_release(controllers[i]);
        wrap_release(pad_objects[i]);
        CHECK(pads[i].refs == 1); // each wrapper gave back both of its faces' references
    }
    wrap_release(gamepads);
    CHECK(pad_list.refs == 1);

    // controller events: the game's handler sees the second pad paired with the guest
    void *controllers_statics = players_factory(L"Windows.Xbox.Input.Controller", &iid, &controller_statics.face[0]);
    INT64 token = 0;
    CALL(controllers_statics, 7, HRESULT (*)(void *, void *, INT64 *))(controllers_statics, &handler.face[0], &token);
    CHECK(token == 1 && controller_statics.handler && controller_statics.handler != &handler.face[0]);
    CALL(controller_statics.handler, 3, HRESULT (*)(void *, void *, void *))(controller_statics.handler, NULL, &args.face[0]);
    CHECK(handled_user == guest);
    wrap_release(handled_user);
    CHECK(args.refs == 1 && pads[1].refs == 1);
    LONG handler_refs = handler.refs;
    wrap_release(controller_statics.handler); // Azimuth removing the event
    CHECK(handler.refs == handler_refs - 1);

    void *all = NULL;
    CALL(controllers_statics, 6, HRESULT (*)(void *, void **))(controllers_statics, &all);
    void *only = NULL;
    CALL(all, 6, HRESULT (*)(void *, UINT32, void **))(all, 0, &only);
    void *owner = NULL;
    CALL(only, 8, HRESULT (*)(void *, void **))(only, &owner); // already an IController: no QueryInterface needed
    CHECK(owner == guest);
    wrap_release(owner);
    wrap_release(only);
    wrap_release(all);
    wrap_release(guest);
    wrap_report();

    printf("%s\n", failures ? "split screen tests FAILED" : "split screen tests passed");
    return failures != 0;
}
