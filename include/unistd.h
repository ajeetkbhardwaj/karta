#pragma once

// Small compatibility layer for the POSIX access() check used by http.cpp.
#ifdef _WIN32
#include <io.h>
#ifndef access
#define access _access
#endif
#ifndef R_OK
#define R_OK 4
#endif
#else
#include_next <unistd.h>
#endif
