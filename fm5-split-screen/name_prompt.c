#include "players.h"
#include "support.h"

/* A small topmost dialog for the guest's name, built in memory (the mod has no resources).
   Enter or OK takes the name; Esc, Skip or closing it keeps "Guest". */
enum { NAME_EDIT = 100 };

static WORD template_words[512];
static unsigned template_length;

static void put_word(WORD value) { template_words[template_length++] = value; }
static void put_dword(DWORD value) {
    put_word(LOWORD(value));
    put_word(HIWORD(value));
}
static void put_text(const wchar_t *text) {
    do {
        put_word(*text);
    } while (*text++);
}
static void put_box(short x, short y, short cx, short cy) {
    put_word((WORD)x);
    put_word((WORD)y);
    put_word((WORD)cx);
    put_word((WORD)cy);
}
static void put_item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t *text) {
    if (template_length & 1) {
        put_word(0); // items start on a DWORD
    }
    put_dword(style | WS_CHILD | WS_VISIBLE);
    put_dword(0);
    put_box(x, y, cx, cy);
    put_word(id);
    put_word(0xffff);
    put_word(atom);
    put_text(text);
    put_word(0); // no creation data
}

static wchar_t *prompt_name;
static unsigned prompt_capacity;

static INT_PTR CALLBACK prompt_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(dialog, NAME_EDIT, prompt_name);
        SendDlgItemMessageW(dialog, NAME_EDIT, EM_LIMITTEXT, prompt_capacity - 1, 0);
        SendDlgItemMessageW(dialog, NAME_EDIT, EM_SETSEL, 0, -1);
        SetForegroundWindow(dialog);
        SetFocus(GetDlgItem(dialog, NAME_EDIT));
        return FALSE; // focus set here
    }
    if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
        if (LOWORD(wparam) == IDOK) {
            GetDlgItemTextW(dialog, NAME_EDIT, prompt_name, (int)prompt_capacity);
        }
        EndDialog(dialog, LOWORD(wparam) == IDOK);
        return TRUE;
    }
    return FALSE;
}

int name_prompt(wchar_t *name, unsigned capacity) {
    template_length = 0;
    put_dword(DS_MODALFRAME | DS_CENTER | DS_SETFONT | DS_SETFOREGROUND | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    put_dword(WS_EX_TOPMOST);
    put_word(4); // items
    put_box(0, 0, 186, 62);
    put_word(0); // no menu
    put_word(0); // the standard dialog class
    put_text(L"FM5 Split-Screen");
    put_word(9);
    put_text(L"Segoe UI");
    put_item(SS_LEFT, 7, 7, 172, 9, 0xffff, 0x0082, L"A new pad joins as the guest. Name:");
    put_item(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 7, 19, 172, 13, NAME_EDIT, 0x0081, L"");
    put_item(BS_DEFPUSHBUTTON | WS_TABSTOP, 75, 40, 50, 14, IDOK, 0x0080, L"OK");
    put_item(BS_PUSHBUTTON | WS_TABSTOP, 129, 40, 50, 14, IDCANCEL, 0x0080, L"Skip");
    prompt_name = name;
    prompt_capacity = capacity;
    HWND before = GetForegroundWindow();
    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)template_words, NULL,
                                             prompt_proc, 0);
    if (before) {
        SetForegroundWindow(before); // back to the game
    }
    return result == 1;
}
