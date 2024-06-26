//==============================================================================
//
//  subs.cpp
//
//  For compiling subs/* headers.  No other file should #include these
//  headers, which will cause the linker (as is intended) to omit them.
//
#include "algorithm"
#include "atomic"
#include "bitset"
#include "cctype"
#include "cfloat"
#include "chrono"
#include "cmath"
#include "condition_variable"
#include "csignal"
#include "cstddef"
#include "cstdint"
#include "cstdio"
#include "cstdlib"
#include "cstring"
#include "ctime"
#include "exception"
#include "filesystem"
#include "fstream"
#include "functional"
#include "iomanip"
#include "ios"
#include "iosfwd"
#include "iostream"
#include "istream"
#include "iterator"
#include "list"
#include "map"
#include "memory"
#include "mutex"
#include "new"
#include "ostream"
#include "queue"
#include "ratio"
#include "set"
#include "sstream"
#include "stack"
#include "string"
#include "system_error"
#include "thread"
#include "typeinfo"
#include "unordered_map"
#include "utility"
#include "vector"

#ifdef OS_WIN
#include "dbghelp.h"
#include "process.h"
#include "windows.h"
#include "winerror.h"
#include "winsock2.h"
#include "ws2tcpip.h"
#endif

#ifdef OS_LINUX
#include "cxxabi.h"
#include "endian.h"
#include "errno.h"
#include "execinfo.h"
#include "in.h"
#include "ioctl.h"
#include "malloc.h"
#include "mman.h"
#include "netdb.h"
#include "poll.h"
#include "pthread.h"
#include "resource.h"
#include "sched.h"
#include "socket.h"
#include "spawn.h"
#include "unistd.h"
#include "wait.h"
#endif
