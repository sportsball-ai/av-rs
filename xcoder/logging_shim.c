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
#define rust_netint_callback rust_netint_logan_callback
#define netint_log_callback netint_logan_log_callback
#define ni_log_set_callback ni_logan_log_set_callback
#endif

void rust_netint_callback(int level, char* message);

static void netint_log_callback(int level, const char* format, va_list args) {
  char buf[2048] = {0};
  size_t buf_len = sizeof(buf) / sizeof(buf[0]);
  int chars_written = vsnprintf(buf, buf_len, format, args);
  if (chars_written > -1 && chars_written + 1 < ((int) buf_len)) {
      rust_netint_callback(level, buf);
  }
}

void setup_rust_netint_logging() {
    ni_log_set_callback(netint_log_callback);
}
