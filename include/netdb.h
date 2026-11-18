#pragma once

// getaddrinfo and related declarations are provided by Winsock on Windows.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include_next <netdb.h>
#endif
