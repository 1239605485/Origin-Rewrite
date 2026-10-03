#ifndef OR_TEF_H
#define OR_TEF_H
#include "or_game.h"
bool or_tef_start(OR_ObserverPort port);
bool or_tef_stop(void);
bool or_tef_text(void *unused, const char *text);
#endif
