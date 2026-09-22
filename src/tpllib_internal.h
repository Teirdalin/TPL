#pragma once
#include "tpllib.h"
namespace tpllib {
bool initialize(void (*logger)(const char*));
void frame(float dt);
const TPLLib_API* getAPI(uint32_t version);
#ifdef TPLLIB_TESTING
void testFailNextHookSnapshot();
struct ForeignHookProfile;
void testUseForeignProfile(const ForeignHookProfile* profile,unsigned count=1);
#endif
}
