use super::sys;

#[derive(Clone, Debug, Default)]
pub struct Time {
    pub value: i64,
    pub timescale: i32,
    pub flags: u32,
    pub epoch: i64,
}

impl From<Time> for sys::CMTime {
    fn from(t: Time) -> Self {
        Self {
            value: t.value,
            timescale: t.timescale,
            epoch: t.epoch,
            flags: t.flags,
        }
    }
}

impl Time {
    pub fn new(value: i64, timescale: i32) -> Self {
        Self {
            value,
            timescale,
            ..Default::default()
        }
    }
}
