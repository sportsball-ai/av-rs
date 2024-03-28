#include <ni_device_api.h>
#include <ni_rsrc_api.h>
#include <ni_util.h>

const int GC620_I420_ = GC620_I420;
const int GC620_I010_ = GC620_I010;

// Defines a shim used to bridge netint logging and rust logging
void rust_netint_callback(int level, const char* message);
// Part of the prior shim
void netint_log_callback(int level, const char* format, ...);
void setup_rust_netint_logging();
