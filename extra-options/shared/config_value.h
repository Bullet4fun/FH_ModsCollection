#ifndef FH_CONFIG_VALUE_H
#define FH_CONFIG_VALUE_H
#include <wchar.h>

/* GetPrivateProfileString hands back everything after the =: this leaves the value alone,
   without a // comment after it or the spaces before that comment. A single slash counts too: a
   short buffer can end between the comment's two slashes. */
static inline void config_value_trim(wchar_t *value) {
    wchar_t *end = value;
    while (*end && *end != L'/') ++end;
    while (end > value && (end[-1] == L' ' || end[-1] == L'\t')) --end;
    *end = 0;
}
#endif
