/*
 * log_buffer_usermod.cpp — Registration of LogBufferUsermod as both a Usermod
 * (discoverable via UsermodManager::lookup) and a LogSink (receives formatted
 * log lines from the dispatch layer).
 */

#include "wled.h"
#include "log_buffer_usermod.h"

#ifndef WLED_DISABLE_LOG_BUFFER

// Out-of-line static member definition (C++11 compatible)
LogBufferUsermod* LogBufferUsermod::s_instance = nullptr;

static LogBufferUsermod g_log_buffer_usermod;

// Register with the UsermodManager dynarray (priority 1, unordered with other mods)
REGISTER_USERMOD(g_log_buffer_usermod);

// Register with the LogSink dynarray (priority 5 — after Serial/NetDebug)
REGISTER_LOG_SINK(g_log_buffer_usermod);

#endif // WLED_DISABLE_LOG_BUFFER
