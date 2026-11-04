#pragma once

// The rest of the resource code: the name listing of an archive (the archive readers are in resource.h).

// Matches `name` against `pattern` (* ends the match successfully, ? matches any
// character, other characters compare through toupper). An empty pattern matches an empty name,
// and a non-empty name gives `emptyPatternResult`.
int WildcardMatch(const char* name, const char* pattern, int emptyPatternResult);
