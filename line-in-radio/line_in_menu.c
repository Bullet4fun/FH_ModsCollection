#include "line_in_capture.h"
#include "logging.h"
#include "support.h"

/* Line-in Radio's rows in the game's Audio options, under Radio Volume: the source (the Songs folder
   or a sound device) and Shuffle. Built the way Extra Options builds its rows
   (extra-options/features/video_options.c): the page's XAML gets two SpinnerControls when the game
   loads it, and their bindings are properties registered on the Audio page's type.

   The XAML comes through a game function, hooked at its entry: Forza Horizon 2's GET_RESOURCE and
   Forza Horizon 3's FH3_LOAD_PAGE. Extra Options only redirects the calls to them, so neither mod
   touches the other's bytes, whichever of the two loads first (their file names decide that, and
   people rename files). These rows go in first, then Extra Options edits the page around them; the
   LI_ prefix is one it doesn't look for. */

enum {
    GET_RESOURCE = 0xaabec0,     // the game's 86 callers read every kind of file through it
    ALLOCATE = 0x3229c0,
    FREE = 0x31c7b0,
    OBJECT_INIT = 0x143eb80,
    OBJECT_CLEANUP = 0x1442050,
    DELETE_OBJECT = 0x257730,
    COLLECTION_TYPE = 0x1476110,
    ALLOCATE_TYPED = 0x14424e0,
    COLLECTION_INIT = 0xaefd60,
    MAKE_STRING = 0x13fe8e0,
    INSERT_ITEM = 0xb52170,
    INT_PROPERTY = 0xed81f0,
    COLLECTION_PROPERTY = 0xed77d0,
    REGISTER_PROPERTY = 0x155ee30,
    BOX_INT = 0x4702d0,
    PROPERTY_CHANGED = 0xcf9c60,
    AUDIO_TYPE_INIT = 0xc803c0,
    AUDIO_TYPE_REGISTRY = 0x35107b0,

    FH3_LOAD_PAGE = 0x21dd6c0,   // void(loader): loader+0x38 is the file's buffer
    FH3_MAKE_BUFFER = 0x208f320,
    FH3_ALLOCATE = 0x1edbd00,
    FH3_DELETE_OBJECT = 0x31eb20,
    FH3_MAKE_LIST = 0x110d5c0,
    FH3_ITEM_TYPE = 0x127ba60,
    FH3_ALLOCATE_TYPED = 0x1f72ce0,
    FH3_ITEM_INIT = 0x2120730,
    FH3_ITEM_VTABLE = 0x44a7b80, // what the Audio page's own option lists hold
    FH3_MAKE_LABEL = 0x1214480,
    FH3_INT_PROPERTY = 0x13b78e0,        // AudioPreset
    FH3_COLLECTION_PROPERTY = 0x13b77e0, // AudioPresetOptions
    FH3_AUDIO_TYPE_INIT = 0x13a9e90,
    FH3_AUDIO_TYPE = 0x1391f00,
    FH3_REGISTER_PROPERTY = 0x1fe9210,
    FH3_BOX_INT = 0x5592b0,
    FH3_PROPERTY_CHANGED = 0x125ac00,
};

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    void *getter, *setter, *unused;
    const char *name;
    void *type;
    uint64_t flags;      // Forza Horizon 3: the owning type
    uint64_t fh3_flags;  // past the end of Forza Horizon 2's descriptor
} NativeProperty;

typedef struct NativePropertyLink {
    NativeProperty *property;
    struct NativePropertyLink *next;
} NativePropertyLink;

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    char *bytes;
    int length;
    int capacity;
} NativeResource;

_Static_assert(sizeof(NativeProperty) == 0x60, "native property descriptor");
_Static_assert(offsetof(NativeResource, bytes) == 0x28, "native resource buffer");

typedef void **(*GetResource)(void *reader, void **output);
enum { SOURCE, SOURCE_CHOICES, SHUFFLE, SHUFFLE_CHOICES, PROPERTY_COUNT };
static const char *const property_names[PROPERTY_COUNT] = {"LineInSource", "LineInSourceChoices", "LineInShuffle",
                                                           "LineInShuffleChoices"};

static unsigned char *image;
static int fh3;
static GetResource next_resource;
static void (*next_load_page)(void *loader);
static NativeProperty properties[PROPERTY_COUNT];
static NativePropertyLink links[PROPERTY_COUNT];
static int registered;
static void *source_choices, *shuffle_choices, *shuffle_locked; // locked: a device plays, Shuffle has nothing to do
#define MAX_DEVICES 32
static wchar_t devices[MAX_DEVICES][128];
static unsigned device_count;
static void *resource_vtable[7];

static void release(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        ((void (*)(void *))(image + (fh3 ? FH3_DELETE_OBJECT : DELETE_OBJECT)))(object);
    }
}

/* One entry of an option list, as the game's Audio page builds it (0x137e1f0): the value and its label. */
static void *fh3_make_item(int value, const wchar_t *text) {
    void *type = ((void *(*)(void))(image + FH3_ITEM_TYPE))();
    unsigned char *item = ((void *(*)(size_t, void *))(image + FH3_ALLOCATE_TYPED))(0x60, type);
    if (!item) {
        return NULL;
    }
    ((void (*)(void *))(image + FH3_ITEM_INIT))(item);
    *(void **)item = image + FH3_ITEM_VTABLE;
    *(int *)(item + 0x50) = value;
    unsigned char *label = NULL;
    ((void **(*)(void **))(image + FH3_MAKE_LABEL))((void **)&label);
    if (label) {
        unsigned length = 0;
        while (text[length]) ++length;
        wchar_t *copy = ((void *(*)(size_t))(image + FH3_ALLOCATE))((length + 1) * sizeof(wchar_t));
        if (copy) {
            memory_copy(copy, text, (length + 1) * sizeof(wchar_t));
            *(wchar_t **)(label + 0x28) = copy;
            *(unsigned *)(label + 0x30) = length;
        }
    }
    *(void **)(item + 0x58) = label;
    return item;
}

static void *fh3_make_choices(const wchar_t *const *labels, unsigned count) {
    void *list = NULL;
    ((void (*)(void **))(image + FH3_MAKE_LIST))(&list);
    if (!list) {
        return NULL;
    }
    for (unsigned i = 0; i < count; ++i) {
        void *item = fh3_make_item((int)i, labels[i]);
        if (!item) {
            release(list);
            return NULL;
        }
        ((void (*)(void *, int, void *))(*(void ***)list)[0x40 / 8])(list, (int)i, item);
        release(item);
    }
    return list;
}

static void property_changed(void *page, NativeProperty *property) {
    if (fh3) {
        ((void (*)(void *, void *))(*(void ***)page)[0x40 / 8])(page, property);
    }
    ((void (*)(void *, void *))(image + (fh3 ? FH3_PROPERTY_CHANGED : PROPERTY_CHANGED)))(page, property);
}

static void *make_choices(const wchar_t *const *labels, unsigned count) {
    if (fh3) {
        return fh3_make_choices(labels, count);
    }
    void *type = ((void *(*)(void *))(image + COLLECTION_TYPE))(NULL);
    void *items = ((void *(*)(size_t, void *))(image + ALLOCATE_TYPED))(0x58, type);
    if (!items) {
        return NULL;
    }
    ((void (*)(void *))(image + COLLECTION_INIT))(items);
    static const unsigned vtables[] = {0x2db73f8, 0x2db7470, 0x2db74d0, 0x2db74e8, 0x2db7508};
    static const unsigned offsets[] = {0, 0x30, 0x38, 0x40, 0x48};
    for (unsigned i = 0; i < ARRAY_COUNT(vtables); ++i) {
        *(void **)((unsigned char *)items + offsets[i]) = image + vtables[i];
    }
    for (unsigned i = 0; i < count; ++i) {
        void *label = NULL;
        ((void **(*)(void **, const wchar_t *))(image + MAKE_STRING))(&label, labels[i]);
        if (!label) {
            release(items);
            return NULL;
        }
        ((void (*)(void *, int, void *))(image + INSERT_ITEM))(items, (int)i, label);
        release(label);
    }
    return items;
}

/* The source list: Songs, then the devices connected now, then the picked device if it is not. */
static void build_source_choices(void) {
    static const wchar_t *labels[MAX_DEVICES + 2];
    static wchar_t picked[128];
    device_count = line_in_list_devices(devices, MAX_DEVICES);
    unsigned count = 0;
    labels[count++] = L"Songs folder";
    for (unsigned i = 0; i < device_count; ++i) labels[count++] = devices[i];
    if (!line_in_use_songs && line_in_device[0]) { // a picked device that is not connected now
        MultiByteToWideChar(CP_UTF8, 0, line_in_device, -1, picked, 128);
        int listed = 0;
        for (unsigned i = 0; i < device_count && !listed; ++i) {
            char name[384];
            WideCharToMultiByte(CP_UTF8, 0, devices[i], -1, name, sizeof(name), NULL, NULL);
            listed = text_equal_ignore_case(name, line_in_device);
        }
        if (!listed && device_count < MAX_DEVICES) {
            memory_copy(devices[device_count++], picked, sizeof(picked));
            labels[count++] = picked;
        }
    }
    void *old = source_choices;
    source_choices = make_choices(labels, count);
    release(old);
}

static int source_index(void) {
    if (line_in_use_songs) {
        return 0;
    }
    for (unsigned i = 0; i < device_count; ++i) {
        char name[384];
        WideCharToMultiByte(CP_UTF8, 0, devices[i], -1, name, sizeof(name), NULL, NULL);
        if (line_in_name_matches(name, line_in_device)) {
            return (int)i + 1;
        }
    }
    return 0;
}

static void **get_value(void **output, NativeProperty *property, void *page) {
    (void)page;
    *output = NULL;
    if (property == &properties[SOURCE_CHOICES] || property == &properties[SHUFFLE_CHOICES]) {
        *output = property == &properties[SOURCE_CHOICES] ? source_choices
                  : line_in_use_songs                    ? shuffle_choices
                                                         : shuffle_locked;
        if (*output) {
            InterlockedIncrement((volatile LONG *)((unsigned char *)*output + 0x10));
        }
        return output;
    }
    int value = property == &properties[SOURCE] ? source_index() : line_in_use_songs && line_in_shuffle;
    return ((void **(*)(void **, const void *))(image + (fh3 ? FH3_BOX_INT : BOX_INT)))(output, &value);
}

static void set_value(NativeProperty *property, void *page, void *boxed) {
    int value;
    if (!boxed || !memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value))) {
        return;
    }
    if (property == &properties[SOURCE]) {
        if (value < 0 || (unsigned)value > device_count || value == source_index()) {
            return;
        }
        if (value == 0) {
            line_in_select(NULL);
        } else {
            static char name[384];
            WideCharToMultiByte(CP_UTF8, 0, devices[value - 1], -1, name, sizeof(name), NULL, NULL);
            line_in_select(name);
        }
        // Shuffle only means something for the Songs folder: its row shows that at once
        property_changed(page, &properties[SHUFFLE_CHOICES]);
        property_changed(page, &properties[SHUFFLE]);
    } else if (property == &properties[SHUFFLE] && line_in_use_songs && (value == 0 || value == 1)) {
        line_in_set_shuffle(value);
    } else {
        return;
    }
    property_changed(page, property);
}

static int register_properties(void) {
    if (registered) {
        return 1;
    }
    static const wchar_t *const shuffle_labels[] = {L"Off", L"On"};
    static const wchar_t *const locked_label[] = {L"Songs folder only"};
    shuffle_choices = make_choices(shuffle_labels, 2);
    shuffle_locked = make_choices(locked_label, 1);
    if (!shuffle_choices || !shuffle_locked) {
        return 0;
    }
    ((void (*)(void))(image + (fh3 ? FH3_AUDIO_TYPE_INIT : AUDIO_TYPE_INIT)))();
    for (unsigned i = 0; i < PROPERTY_COUNT; ++i) {
        int collection = i == SOURCE_CHOICES || i == SHUFFLE_CHOICES;
        NativeProperty *original =
            ((NativeProperty * (*)(void))(image + (fh3 ? (collection ? FH3_COLLECTION_PROPERTY : FH3_INT_PROPERTY)
                                                      : (collection ? COLLECTION_PROPERTY : INT_PROPERTY))))();
        NativeProperty *property = &properties[i];
        if (fh3) {
            memory_copy(property, original, sizeof(*property));
            property->unused = NULL;
        } else {
            ((void (*)(void *))(image + OBJECT_INIT))(property);
            property->vtable = original->vtable;
            memory_copy((unsigned char *)property + 0x28, (unsigned char *)original + 0x28, 0x30);
        }
        property->name = property_names[i];
        property->getter = (void *)get_value;
        property->setter = collection ? NULL : (void *)set_value;
        links[i].property = property;
        if (fh3) {
            ((void (*)(void *, NativePropertyLink *))(image + FH3_REGISTER_PROPERTY))(
                ((void *(*)(void *))(image + FH3_AUDIO_TYPE))(NULL), &links[i]);
        } else {
            ((void (*)(void *, NativePropertyLink *))(image + REGISTER_PROPERTY))(image + AUDIO_TYPE_REGISTRY, &links[i]);
        }
    }
    registered = 1;
    return 1;
}

static const char resources_xaml[] =
    "<local:TileMenuDataItem x:Key=\"LI_Source\" Heading=\"Line-in Radio\" Description=\"What the Line-in Radio "
    "station plays: the music files from the mod's Songs folder, or a sound device, such as a virtual cable "
    "or a spare output your music player plays into.\"/>"
    "<local:TileMenuDataItem x:Key=\"LI_Shuffle\" Heading=\"Shuffle\" Description=\"Plays the songs from the "
    "Songs folder in random order. Off plays them in alphabetical order. Only for the Songs folder.\"/>";
static const char rows_xaml[] =
    "<local:SpinnerControl Tag=\"{StaticResource LI_Source}\" Style=\"{StaticResource "
    "A_Style_SpinnerControl_Default}\" Label=\"Line-in Radio\" SelectedIndex=\"{Binding LineInSource}\" "
    "ItemsSource=\"{Binding LineInSourceChoices}\"/>"
    "<local:SpinnerControl Tag=\"{StaticResource LI_Shuffle}\" Style=\"{StaticResource "
    "A_Style_SpinnerControl_Default}\" Label=\"Shuffle\" SelectedIndex=\"{Binding LineInShuffle}\" "
    "ItemsSource=\"{Binding LineInShuffleChoices}\"/>";

/* The Audio page with the rows: resources before </ResourceDictionary>, rows after the Radio
   Volume slider. 0: not the Audio page, or not the layout this expects. */
size_t line_in_menu_xaml(const char *text, size_t length, char *output, size_t capacity) {
    static const char radio[] = "<Slider Value=\"{Binding RadioVolume}\"";
    if (!text_find(text, length, "x:Class=\"ForzaUI.A_AudioOptions\"") || text_find(text, length, "LI_Source")) {
        return 0;
    }
    const char *resources = text_find(text, length, "</ResourceDictionary>");
    const char *slider = text_find_unique(text, length, radio);
    const char *row_end = slider ? text_find(slider, length - (size_t)(slider - text), "/>") : NULL;
    if (!resources || !row_end || resources > slider) {
        return 0;
    }
    row_end += 2;
    size_t a = (size_t)(resources - text), b = (size_t)(row_end - text);
    size_t total = length + sizeof(resources_xaml) - 1 + sizeof(rows_xaml) - 1;
    if (total + 1 > capacity) {
        return 0;
    }
    char *out = output;
    memory_copy(out, text, a), out += a;
    memory_copy(out, resources_xaml, sizeof(resources_xaml) - 1), out += sizeof(resources_xaml) - 1;
    memory_copy(out, text + a, b - a), out += b - a;
    memory_copy(out, rows_xaml, sizeof(rows_xaml) - 1), out += sizeof(rows_xaml) - 1;
    memory_copy(out, text + b, length - b), out += length - b;
    *out = 0;
    // Forza Horizon 3 only: its Groove Music Shuffle row shows even with Groove Music off, and the
    // list ends with three empty rows that can't be focused. With these two rows, all three fall
    // below the window, where selection can't reach them, and the down arrow never goes away. The
    // last one stays: without it the list looks cut off, and with it everything still fits.
    total = text_remove_elements(output, total, GROOVE_SHUFFLE_ROW);
    return text_remove_elements(output, total, "<local:SpinnerControl Focusable=\"False\" Visibility=");
}

static void *delete_resource(NativeResource *resource, unsigned flags) {
    ((void (*)(void *))(image + OBJECT_CLEANUP))(resource);
    if (flags & 1) {
        ((void (*)(void *))(image + FREE))(resource);
    }
    return resource;
}

static void **get_resource(void *reader, void **output) {
    next_resource(reader, output);
    NativeResource *original = *output;
    char *text;
    int text_size;
    unsigned char first;
    // This runs for every file the game reads: only XAML (a '<', or a byte order mark) gets searched.
    if (!original || !memory_read_pointer(&original->bytes, (void **)&text) || !text ||
        !memory_read(&original->length, &text_size, sizeof(text_size)) || text_size <= 0 || text_size > 262144 ||
        !memory_read(text, &first, sizeof(first)) || (first != '<' && first != 0xef) ||
        !text_find(text, (size_t)text_size, "x:Class=\"ForzaUI.A_AudioOptions\"")) {
        return output;
    }
    size_t capacity = (size_t)original->length + sizeof(resources_xaml) + sizeof(rows_xaml);
    NativeResource *replacement = ((void *(*)(size_t))(image + ALLOCATE))(sizeof(NativeResource) + capacity);
    if (!replacement) {
        return output;
    }
    char *bytes = (char *)(replacement + 1);
    size_t length = line_in_menu_xaml(original->bytes, (size_t)original->length, bytes, capacity);
    if (length) {
        build_source_choices(); // the devices connected now, every time the page opens
    }
    if (!length || !source_choices || !register_properties()) {
        ((void (*)(void *))(image + FREE))(replacement);
        log_append("LINE-IN: the Audio options layout did not match; no Line-in Radio rows.\r\n");
        return output;
    }
    ((void (*)(void *))(image + OBJECT_INIT))(replacement);
    memory_copy(resource_vtable, original->vtable - 1, sizeof(resource_vtable));
    resource_vtable[1] = (void *)delete_resource;
    replacement->vtable = resource_vtable + 1;
    replacement->bytes = bytes;
    replacement->length = (int)length;
    replacement->capacity = (int)capacity;
    *output = replacement;
    release(original);
    return output;
}

static void fh3_load_page(void *loader) {
    next_load_page(loader);
    unsigned char *buffer;
    const char *bytes;
    int size;
    if (!memory_read_pointer((unsigned char *)loader + 0x38, (void **)&buffer) || !buffer ||
        !memory_read_pointer(buffer + 0x28, (void **)&bytes) || !bytes ||
        !memory_read(buffer + 0x30, &size, sizeof(size)) || size <= 0 || size > 262144 ||
        !text_find(bytes, (size_t)size, "x:Class=\"ForzaUI.A_AudioOptions\"")) {
        return;
    }
    size_t capacity = (size_t)size + sizeof(resources_xaml) + sizeof(rows_xaml);
    unsigned char *replacement = NULL;
    ((void **(*)(void **, unsigned))(image + FH3_MAKE_BUFFER))((void **)&replacement, (unsigned)capacity);
    char *data;
    if (!replacement || !memory_read_pointer(replacement + 0x28, (void **)&data)) {
        return;
    }
    size_t length = line_in_menu_xaml(bytes, (size_t)size, data, capacity);
    if (length) {
        build_source_choices(); // the devices connected now, every time the page opens
    }
    if (!length || !source_choices || !register_properties()) {
        release(replacement);
        log_append("LINE-IN: the Audio options layout did not match; no Line-in Radio rows.\r\n");
        return;
    }
    *(int *)(replacement + 0x30) = (int)length;
    *(void **)((unsigned char *)loader + 0x38) = replacement;
    release(buffer);
}

void line_in_menu_install(unsigned char *game_image, int is_fh3) {
    image = game_image;
    fh3 = is_fh3;
    static const unsigned char fh2_prologue[15] = {0x48, 0x83, 0xec, 0x18, 0x48, 0x8b, 0x41, 0x38,
                                                   0xc7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00};
    static const unsigned char fh3_prologue[19] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8d, 0x6c,
                                                   0x24, 0x90, 0x48, 0x81, 0xec, 0x70, 0x01, 0x00, 0x00};
    const unsigned char *prologue = fh3 ? fh3_prologue : fh2_prologue;
    size_t size = fh3 ? sizeof(fh3_prologue) : sizeof(fh2_prologue);
    unsigned char *target = image + (fh3 ? FH3_LOAD_PAGE : GET_RESOURCE);
    void *original = memory_equal(target, prologue, size)
                         ? install_jmp_hook(target, size, fh3 ? (void *)fh3_load_page : (void *)get_resource)
                         : NULL;
    if (fh3) {
        next_load_page = original;
    } else {
        next_resource = original;
    }
    log_append(original ? "LINE-IN: Audio options rows ready.\r\n"
                        : "LINE-IN: the Audio options loader is not the expected one; no Line-in Radio rows.\r\n");
}
