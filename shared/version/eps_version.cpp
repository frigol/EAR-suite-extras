#include "eps_version.h"

std::string const& eps::baseVersion() {
    static std::string baseVersion("1.0.1");
    return baseVersion;
}

std::string const& eps::currentVersion() {
#ifdef BAREBONESPROFILE
    static std::string currentVersion("1.0.1-no-tags[bb]");
#else
    static std::string currentVersion("1.0.1-no-tags");
#endif
    return currentVersion;
}

int const eps::versionMajor() {
	return 1;
}

int const eps::versionMinor() {
	return 0;
}

int const eps::versionRevision() {
	return 1;
}

bool const eps::versionInfoAvailable() {
    return false;
}
