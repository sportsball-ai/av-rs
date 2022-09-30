#ifndef RS_HEADER
#define RS_HEADER 1
// In order to link against both 3.1.0 and 2.5.9, these symbols need names that don't conflict with 2.5.9 symbols.
#define ni_log ni_logan_log
#define ni_log_set_level ni_logan_log_set_level
#define ni_log_get_level ni_logan_log_get_level
#define ff_to_ni_log_level ff_to_ni_logan_log_level
#define remove_substring_pattern logan_remove_substring_pattern
#define thread_routine logan_thread_routine
#define threadpool_init logan_threadpool_init
#define threadpool_auto_add_task_thread logan_threadpool_auto_add_task_thread
#define threadpool_add_task logan_threadpool_add_task
#define threadpool_destroy logan_threadpool_destroy
#define g_device_reference_table g_logan_device_reference_table
#endif
