// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
//
// Single runtime gate for the fork's opt-in diagnostic traces.
//
// A trace is enabled when the IG_TRACE environment variable is set to a
// non-empty value other than "0". The browser shell sets it only in debug
// mode (?debug=1). Results are cached on first use, so read them from
// function bodies rather than from static initializers.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// True when the named environment variable is set to a non-empty value other than "0".
inline bool wpEnvEnabled(const char *name)
{
	const char *env = getenv(name);
	return env && *env && strcmp(env, "0") != 0;
}

// The general diagnostic trace switch (IG_TRACE), cached after the first call.
inline bool wpTraceEnabled()
{
	static int state = -1;
	if (state < 0)
		state = wpEnvEnabled("IG_TRACE") ? 1 : 0;
	return state == 1;
}

// fprintf(stderr, ...) when tracing is enabled; a cheap check otherwise.
#define WP_TRACE(...) do { if (wpTraceEnabled()) fprintf(stderr, __VA_ARGS__); } while (0)
