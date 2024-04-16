use std::sync::Mutex;

use xcoder_logan_310_sys as sys;

#[derive(Debug, Eq, PartialEq)]
struct LogEntry {
    level: log::Level,
    message: String,
}

#[derive(Default)]
struct MyLog(Mutex<Vec<LogEntry>>);

impl log::Log for MyLog {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }

    fn log(&self, record: &log::Record) {
        let mut l = self.0.lock().expect("not poisoned");
        l.push(LogEntry {
            level: record.level(),
            message: record.args().to_string(),
        })
    }

    fn flush(&self) {}
}

#[test]
fn blah() {
    let log = Box::leak(Box::new(MyLog::default()));
    log::set_logger(log).expect("installing logger should succeed");
    log::set_max_level(log::LevelFilter::Info);
    unsafe {
        sys::setup_rust_netint_logging();
        sys::ni_logan_log(sys::ni_log_level_t_NI_LOG_ERROR, cstr!("foo %s %d").as_ptr(), cstr!("bar").as_ptr(), 1234u32);
    }
    let l = log.0.lock().expect("not poisoned");
    assert_eq!(l.len(), 1);
    assert_eq!(
        &LogEntry {
            level: log::Level::Error,
            message: "foo bar 1234".to_owned(),
        },
        &l[0]
    );
}
