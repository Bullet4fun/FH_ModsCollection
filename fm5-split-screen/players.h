#ifndef FM5_PLAYERS_H
#define FM5_PLAYERS_H
/* A second console user for Forza Motorsport 5. Azimuth emulates one signed-in user and pairs every
   pad with it; the wrappers here add a guest (user id 2), list it among the users and pair it with
   every pad but the one that pressed A first. */
#include "wrap.h"

void players_init(void);
/* Every activation factory Azimuth hands the game passes through here: returns what the game gets
   instead (a wrapper, or factory itself), owning the reference factory came with. */
void *players_factory(const wchar_t *class_name, const GUID *iid, void *factory);
/* Where the game's WindowsCreateString, WindowsGetStringRawBuffer and WindowsDeleteString are (import
   slots, read at each use); without them the guest keeps user 1's Xbox user id. */
void players_strings(void *const *create, void *const *buffer, void *const *destroy);
/* Called from the mod's own thread: a pad waiting to join as the guest is shown to the game leaving
   and coming back, so the game asks again whose it is (it asks only when it first sees a pad). */
void players_tick(void);
#endif
