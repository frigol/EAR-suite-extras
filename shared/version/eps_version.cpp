#include "eps_version.h"

std::string const& eps::baseVersion() {
    static std::string baseVersion("1.2.2");
    return baseVersion;
}

std::string const& eps::currentVersion() {
#ifdef BAREBONESPROFILE
    static std::string currentVersion("1.2.2-no-tags[bb]");
#else
    static std::string currentVersion("1.2.2-no-tags");
#endif
    return currentVersion;
}

int const eps::versionMajor() {
	return 1;
}

int const eps::versionMinor() {
	return 2;
}

int const eps::versionRevision() {
	return 2;
}

bool const eps::versionInfoAvailable() {
    return false;
}
