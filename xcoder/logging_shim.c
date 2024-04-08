// This file is compiled into both xcoder-quadra-sys and xcoder-logan-310-sys.
// xcoder-logan-259-sys lacks the needed facilities to leverage this.

#include <stdarg.h>
#include <stdio.h>

#ifdef LOGAN
#include <ni_util_logan.h>
#else
#include <ni_util.h>
#endif

#ifdef LOGAN
void rust_netint_logan_callback(int level, char* message);
#else
void rust_netint_callback(int level, char* message);
#endif

void netint_log_callback(int level, const char* format, va_list args) {
  char buf[2048] = {0};
  size_t buf_len = sizeof(buf) / sizeof(buf[0]);
  int chars_written = snprintf(buf, buf_len, format, args);
  if (chars_written > -1 && chars_written + 1 < ((int) buf_len)) {
      #ifdef LOGAN
      rust_netint_logan_callback(level, buf);
      #else
      rust_netint_callback(level, buf);
      #endif
  }
}

void setup_rust_netint_logging() {
    ni_log_set_callback(netint_log_callback);
}
