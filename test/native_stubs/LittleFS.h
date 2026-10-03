/* LittleFS.h — native stub (2026-10-02, for the restore tests in test_ota_sig).
 *
 * An in-memory filesystem with the slice of the arduino-pico API that
 * src/ota/restore.cpp uses: open for writing (truncating, as "w" does), exists,
 * mkdir, remove. The File it hands out is the Arduino.h stub's, writing into
 * one of these files; File::writesUntilFailure( ) makes a later write fail,
 * which is how an I/O error in the middle of a restore is reproduced. */
#pragma once

#include <Arduino.h>
#include <map>
#include <set>
#include <string>

namespace fakefs {
inline std::map<std::string, std::string>& files( ) { static std::map<std::string, std::string> m; return m; }
inline std::set<std::string>& dirs( ) { static std::set<std::string> d; return d; }
inline long& writes_until_failure( ) { return File::writesUntilFailure( ); }
inline void reset( ) { files( ).clear( ); dirs( ).clear( ); writes_until_failure( ) = -1; }
}

struct FakeLittleFS {
	bool exists(const char* p) { return fakefs::files( ).count(p) || fakefs::dirs( ).count(p); }
	bool mkdir(const char* p) { fakefs::dirs( ).insert(p); return true; }
	File open(const char* p, const char* mode) {
		File f;
		if (mode && mode[0] == 'w') f.openInto(&fakefs::files( )[p]);
		return f;
	}
	bool remove(const char* p) { return fakefs::files( ).erase(p) > 0; }
};
inline FakeLittleFS LittleFS;
