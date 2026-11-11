#ifndef FM5_PLAYERS_H
#define FM5_PLAYERS_H
/* A second console user for Forza Motorsport 5. Azimuth emulates one signed-in user and pairs every
   pad with it; the wrappers here add a guest (user id 2), list it among the users and pair it with
   the second pad, by device id order. */
#include "wrap.h"

void players_init(void);
/* Every activation factory Azimuth hands the game passes through here: returns what the game gets
   instead (a wrapper, or factory itself), owning the reference factory came with. */
void *players_factory(const wchar_t *class_name, const GUID *iid, void *factory);
#endif
