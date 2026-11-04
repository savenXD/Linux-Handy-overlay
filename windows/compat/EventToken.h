// WebView2.h includes "EventToken.h" (Windows SDK). MinGW ships it as lowercase eventtoken.h and Linux
// file systems are case sensitive. This shim is only on the include path for non-MSVC builds.
#pragma once
#include <eventtoken.h>
