use std::{
    io::{Read, Seek, SeekFrom, Write},
    marker::PhantomData,
    mem,
    os::raw::{c_int, c_void},
};

#[derive(Error, Debug)]
pub enum Error {
    #[error("unable to allocate buffer")]
    UnableToAllocateBuffer,
    #[error("unable to allocate avio context")]
    UnableToAllocateAvioContext,
}

pub type Result<T> = std::result::Result<T, Error>;

/// Turns any type implementing `Read` into an FFmpeg `AVIOContext`.
pub struct Reader<R> {
    _inner: Box<Box<dyn Read>>,
    ctx: *mut ffmpeg_sys::AVIOContext,
    _r: PhantomData<R>,
}

impl<R: Read> Reader<R> {
    pub fn new(r: R) -> Result<Self> {
        unsafe extern "C" fn avio_reader_read(opaque: *mut c_void, buf: *mut u8, buf_size: c_int) -> c_int {
            let r = opaque as *mut Box<dyn Read>;
            match (*r).read(std::slice::from_raw_parts_mut(buf, buf_size as _)) {
                Ok(n) => {
                    if n == 0 {
                        ffmpeg_sys::AVERROR_EOF
                    } else {
                        n as _
                    }
                }
                Err(_) => ffmpeg_sys::AVERROR_UNKNOWN,
            }
        }

        let r = unsafe { mem::transmute::<Box<dyn Read>, Box<dyn Read>>(Box::new(r)) };
        let r = Box::new(r);

        let ctx = unsafe {
            use ffmpeg_sys::*;

            const BUFFER_SIZE: usize = 4096;
            let buffer = av_malloc(BUFFER_SIZE);
            if buffer.is_null() {
                return Err(Error::UnableToAllocateBuffer);
            }
            let ctx = avio_alloc_context(
                buffer as _,
                BUFFER_SIZE as _,
                0,
                &*r as *const Box<dyn Read> as _,
                Some(avio_reader_read),
                None,
                None,
            );
            if ctx.is_null() {
                av_free(buffer);
                return Err(Error::UnableToAllocateAvioContext);
            }
            ctx
        };

        Ok(Self {
            _inner: r,
            ctx,
            _r: PhantomData,
        })
    }

    // Gets the pointer to the AVIOContext. This pointer is valid for as long as the `Reader` lives.
    pub fn context(&self) -> *mut ffmpeg_sys::AVIOContext {
        self.ctx
    }
}

impl<W> Drop for Reader<W> {
    fn drop(&mut self) {
        unsafe {
            use ffmpeg_sys::av_free;
            av_free((*self.ctx).buffer as _);
            av_free(self.ctx as _);
        }
    }
}

trait WriterSeeker: Write + Seek {}
impl<T: Write + Seek> WriterSeeker for T {}

enum WriterOrWriterSeeker {
    Writer(Box<dyn Write>),
    WriterSeeker(Box<dyn WriterSeeker>),
}

impl WriterOrWriterSeeker {
    fn is_seeker(&self) -> bool {
        matches!(self, WriterOrWriterSeeker::WriterSeeker(_))
    }
}

/// Turns any type implementing `Write` into an FFmpeg `AVIOContext`.
pub struct Writer<W> {
    _inner: Box<WriterOrWriterSeeker>,
    ctx: *mut ffmpeg_sys::AVIOContext,
    _w: PhantomData<W>,
}

impl<W: Write> Writer<W> {
    /// Creates a new AvioWriter that can write, but can't seek. For a seekable writer, use
    /// `new_seekable`.
    pub fn new(w: W) -> Result<Self> {
        // `_w` ensures that `AvioWriter` doesn't outlive the writer.
        let w = unsafe { mem::transmute::<Box<dyn Write>, Box<dyn Write>>(Box::new(w)) };
        Self::new_impl(WriterOrWriterSeeker::Writer(w))
    }
}

impl<W: Write + Seek> Writer<W> {
    /// Creates a new AvioWriter that can both write and seek.
    pub fn new_seekable(w: W) -> Result<Self> {
        // `_w` ensures that `AvioWriter` doesn't outlive the writer.
        let w = unsafe { mem::transmute::<Box<dyn WriterSeeker>, Box<dyn WriterSeeker>>(Box::new(w)) };
        Self::new_impl(WriterOrWriterSeeker::WriterSeeker(w))
    }
}

impl<W: Write> Writer<W> {
    fn new_impl(w: WriterOrWriterSeeker) -> Result<Self> {
        unsafe extern "C" fn avio_writer_write(opaque: *mut c_void, buf: *mut u8, buf_size: c_int) -> c_int {
            let w = opaque as *mut WriterOrWriterSeeker;
            match match &mut *w {
                WriterOrWriterSeeker::Writer(w) => w.write(std::slice::from_raw_parts(buf, buf_size as _)),
                WriterOrWriterSeeker::WriterSeeker(w) => w.write(std::slice::from_raw_parts(buf, buf_size as _)),
            } {
                Ok(n) => n as _,
                Err(_) => ffmpeg_sys::AVERROR_UNKNOWN,
            }
        }

        unsafe extern "C" fn avio_writer_seek(opaque: *mut c_void, offset: i64, whence: c_int) -> i64 {
            let w = opaque as *mut WriterOrWriterSeeker;
            match match &mut *w {
                WriterOrWriterSeeker::Writer(_) => {
                    // This shouldn't be possible.
                    panic!("avio_writer_seek called for unseekable writer");
                }
                WriterOrWriterSeeker::WriterSeeker(w) => w.seek(match whence {
                    ffmpeg_sys::SEEK_CUR => SeekFrom::Current(offset),
                    ffmpeg_sys::SEEK_SET => SeekFrom::Start(if offset < 0 {
                        return ffmpeg_sys::AVERROR_INVALIDDATA as _;
                    } else {
                        offset as u64
                    }),
                    ffmpeg_sys::SEEK_END => SeekFrom::End(offset),
                    _ => return ffmpeg_sys::AVERROR_INVALIDDATA as _,
                }),
            } {
                Ok(n) => n as _,
                Err(_) => ffmpeg_sys::AVERROR_UNKNOWN as _,
            }
        }

        let w = Box::new(w);

        let ctx = unsafe {
            use ffmpeg_sys::*;

            const BUFFER_SIZE: usize = 4096;
            let buffer = av_malloc(BUFFER_SIZE);
            if buffer.is_null() {
                return Err(Error::UnableToAllocateBuffer);
            }
            let ctx = avio_alloc_context(
                buffer as _,
                BUFFER_SIZE as _,
                1,
                &*w as *const WriterOrWriterSeeker as _,
                None,
                Some(avio_writer_write),
                if w.is_seeker() { Some(avio_writer_seek) } else { None },
            );
            if ctx.is_null() {
                av_free(buffer);
                return Err(Error::UnableToAllocateAvioContext);
            }
            ctx
        };

        Ok(Self {
            _inner: w,
            ctx,
            _w: PhantomData,
        })
    }

    // Gets the pointer to the AVIOContext. This pointer is valid for as long as the `Writer` lives.
    pub fn context(&self) -> *mut ffmpeg_sys::AVIOContext {
        self.ctx
    }
}

impl<W> Drop for Writer<W> {
    fn drop(&mut self) {
        unsafe {
            use ffmpeg_sys::av_free;
            av_free((*self.ctx).buffer as _);
            av_free(self.ctx as _);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_reader() {
        let buf = vec![];
        Reader::new(&*buf).unwrap();
    }

    #[test]
    fn test_writer() {
        let mut buf = vec![];
        Writer::new(&mut *buf).unwrap();

        let mut cur = std::io::Cursor::new(vec![]);
        Writer::new_seekable(&mut cur).unwrap();
    }

    #[test]
    fn test_lifetimes() {
        let t = trybuild::TestCases::new();
        t.compile_fail("tests/avio/bad_reader_lifetime.rs");
        t.compile_fail("tests/avio/bad_writer_lifetime.rs");
    }
}
