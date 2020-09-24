// See the file "COPYING" in the main distribution directory for copyright.

#ifndef WEIRDSTATE_H
#define WEIRDSTATE_H

#include <string>
#include <unordered_map>

struct WeirdState {
	WeirdState() { count = 0; sampling_start_time = 0; }
	uint64_t count = 0;
	double sampling_start_time = 0;
};

using WeirdStateMap = std::unordered_map<std::string, WeirdState>;

bool PermitWeird(WeirdStateMap& wsm, const char* name, uint64_t threshold,
                 uint64_t rate, double duration);

#endif // WEIRDSTATE_H

