#pragma once

// Compatibility with the POSIX networking header used by tools.cpp.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef inet_pton
#define inet_pton(family, address, buffer) InetPtonA((family), (address), (buffer))
#endif
#ifndef inet_ntop
#define inet_ntop(family, address, buffer, length)                                                 \
  InetNtopA((family), const_cast<void*>(static_cast<const void*>(address)), (buffer), (length))
#endif
#else
#include_next <arpa/inet.h>
#endif
