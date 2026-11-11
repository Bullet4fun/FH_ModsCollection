#ifndef FM5_PLAYERS_H
#define FM5_PLAYERS_H
/* A second console user for Forza Motorsport 5. Azimuth emulates one signed-in user and pairs every
   pad with it; the wrappers here add a guest (user id 2), list it among the users and pair it with
   every pad but the one that pressed A first, once the guest's name is asked. */
#include "wrap.h"

void players_init(void);
/* Every activation factory Azimuth hands the game passes through here: returns what the game gets
   instead (a wrapper, or factory itself), owning the reference factory came with. */
void *players_factory(const wchar_t *class_name, const GUID *iid, void *factory);
/* Where the game's WindowsCreateString, WindowsGetStringRawBuffer and WindowsDeleteString are (import
   slots, read at each use); without them the guest keeps user 1's Xbox user id. */
void players_strings(void *const *create, void *const *buffer, void *const *destroy);
/* Asks the guest's name: name holds the default, capacity counts its characters with the end.
   Returns 0 when skipped (the guest is then "Guest"). */
typedef int (*NamePromptFn)(wchar_t *name, unsigned capacity);
/* Called from the mod's own thread: a pad waiting to join as the guest gets the name asked, then the
   game sees that pad leave and come back, and asks again whose it is. */
void players_tick(NamePromptFn prompt);
/* The mod's prompt: a small topmost dialog (name_prompt.c). */
int name_prompt(wchar_t *name, unsigned capacity);
#endif
