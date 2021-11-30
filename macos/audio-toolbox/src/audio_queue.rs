use super::sys;
use core_audio::{AudioStreamBasicDescription, AudioStreamPacketDescription};
use core_foundation::{result, OSStatus};
use std::{marker::PhantomData, pin::Pin};

type Callback = Box<dyn FnMut(AudioQueueBuffer) + Send>;

pub struct AudioQueue {
    inner: sys::AudioQueueRef,
    _callback: Pin<Box<Callback>>,
}

impl AudioQueue {
    pub fn new_output<F: FnMut(AudioQueueBuffer) + Send + 'static>(asbd: AudioStreamBasicDescription, callback: F) -> Result<Self, OSStatus> {
        unsafe extern "C" fn sys_callback(user_data: *mut std::os::raw::c_void, queue: sys::AudioQueueRef, buffer: sys::AudioQueueBufferRef) {
            let callback = user_data as *mut Callback;
            let buffer = AudioQueueBuffer {
                queue,
                inner: buffer,
                free_on_drop: true,
                _queue: PhantomData,
            };
            (*callback)(buffer);
        }
        let mut callback: Pin<Box<Callback>> = Box::pin(Box::new(callback));
        let mut queue_ref = std::ptr::null_mut();
        let asbd: core_audio::sys::AudioStreamBasicDescription = asbd.into();
        result(
            unsafe {
                sys::AudioQueueNewOutput(
                    &asbd as *const _ as *const _,
                    Some(sys_callback),
                    &mut *callback as *mut Callback as *mut _,
                    std::ptr::null_mut(),
                    std::ptr::null(),
                    0,
                    &mut queue_ref as _,
                )
            }
            .into(),
        )?;
        Ok(Self {
            inner: queue_ref,
            _callback: callback,
        })
    }

    pub fn new_buffer(&mut self, size: usize) -> Result<AudioQueueBuffer, OSStatus> {
        let mut buffer_ref = std::ptr::null_mut();
        result(unsafe { sys::AudioQueueAllocateBuffer(self.inner, size as _, &mut buffer_ref) }.into())?;
        Ok(AudioQueueBuffer {
            queue: self.inner,
            inner: buffer_ref,
            free_on_drop: true,
            _queue: PhantomData,
        })
    }

    pub fn start(&mut self) -> Result<(), OSStatus> {
        result(unsafe { sys::AudioQueueStart(self.inner, std::ptr::null()).into() })
    }

    pub fn stop(&mut self, immediate: bool) -> Result<(), OSStatus> {
        result(unsafe { sys::AudioQueueStop(self.inner, if immediate { 1 } else { 0 }).into() })
    }

    pub fn pause(&mut self) -> Result<(), OSStatus> {
        result(unsafe { sys::AudioQueuePause(self.inner).into() })
    }

    pub fn reset(&mut self) -> Result<(), OSStatus> {
        result(unsafe { sys::AudioQueueReset(self.inner).into() })
    }

    pub fn prime(&mut self, frames: usize) -> Result<usize, OSStatus> {
        let mut prepared = 0u32;
        result(unsafe { sys::AudioQueuePrime(self.inner, frames as _, &mut prepared as _).into() })?;
        Ok(prepared as _)
    }

    /// Sets the volume. 0.0 is silent, 1.0 is full volume.
    pub fn set_volume(&mut self, volume: f64) -> Result<(), OSStatus> {
        result(unsafe { sys::AudioQueueSetParameter(self.inner, sys::kAudioQueueParam_Volume, volume as _).into() })
    }
}

impl Drop for AudioQueue {
    fn drop(&mut self) {
        unsafe { sys::AudioQueueDispose(self.inner, 1) };
    }
}

unsafe impl Send for AudioQueue {}

pub struct AudioQueueBuffer<'a> {
    inner: sys::AudioQueueBufferRef,
    queue: sys::AudioQueueRef,
    free_on_drop: bool,
    _queue: PhantomData<&'a sys::AudioQueueRef>,
}

impl<'a> AudioQueueBuffer<'a> {
    pub fn copy_from_slice(&mut self, data: &[u8]) {
        self.audio_data_buffer_mut().copy_from_slice(data);
        self.set_audio_data_length(data.len());
    }

    /// Sets the length of the audio data.
    ///
    /// # Panics
    ///
    /// Panics if `len` is greater than the buffer's capacity.
    pub fn set_audio_data_length(&mut self, len: usize) {
        unsafe {
            if len > (*self.inner).mAudioDataBytesCapacity as _ {
                panic!("size is greater than capacity");
            }
            (*self.inner).mAudioDataByteSize = len as _;
        }
    }

    pub fn audio_data_buffer_mut(&mut self) -> &mut [u8] {
        unsafe { std::slice::from_raw_parts_mut((*self.inner).mAudioData as _, (*self.inner).mAudioDataBytesCapacity as _) }
    }

    pub fn enqueue(mut self, packet_descriptions: &[AudioStreamPacketDescription]) -> Result<(), OSStatus> {
        let packet_descriptions: Vec<core_audio::sys::AudioStreamPacketDescription> = packet_descriptions.iter().cloned().map(|d| d.into()).collect();
        result(unsafe {
            sys::AudioQueueEnqueueBuffer(
                self.queue,
                self.inner,
                packet_descriptions.len() as _,
                if packet_descriptions.is_empty() {
                    std::ptr::null()
                } else {
                    packet_descriptions.as_ptr() as _
                },
            )
            .into()
        })?;
        self.free_on_drop = false;
        Ok(())
    }
}

impl<'a> Drop for AudioQueueBuffer<'a> {
    fn drop(&mut self) {
        if self.free_on_drop {
            unsafe { sys::AudioQueueFreeBuffer(self.queue, self.inner) };
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use core_audio::{AudioFormat, AudioFormatFlags};
    use mpeg4::AudioDataTransportStream;
    use std::{io::Read, time::Duration};

    #[test]
    fn test_audio_queue() {
        const PERIOD: usize = 128; // ~344.5 Hz or about an F4
        const BUFFER_SIZE: usize = 32 * PERIOD;

        let mut queue = AudioQueue::new_output(
            AudioStreamBasicDescription {
                sample_rate: 44100.0,
                format: AudioFormat::LinearPCM,
                format_flags: AudioFormatFlags::LINEAR_PCM_IS_SIGNED_INTEGER | AudioFormatFlags::IS_PACKED,
                bits_per_channel: 16,
                channels_per_frame: 2,
                bytes_per_frame: 4,
                frames_per_packet: 1,
                bytes_per_packet: 4,
            },
            |buffer| {
                if let Err(code) = buffer.enqueue(&[]) {
                    assert_eq!(-66632, code.as_sys()); // kAudioQueueErr_EnqueueDuringReset, happens when we stop the queue
                }
            },
        )
        .unwrap();
        queue.set_volume(0.0).unwrap();

        for _ in 0..3 {
            let mut buffer = queue.new_buffer(BUFFER_SIZE).unwrap();
            buffer.set_audio_data_length(BUFFER_SIZE);
            let dest = buffer.audio_data_buffer_mut();
            for i in 0..BUFFER_SIZE / 4 {
                let sample: u16 = ((2.0 * std::f64::consts::PI * i as f64 / PERIOD as f64).sin() * std::i16::MAX as f64) as i16 as _;
                dest[i * 4] = (sample >> 8) as _;
                dest[i * 4 + 1] = (sample & 0xff) as _;
                dest[i * 4 + 2] = (sample >> 8) as _;
                dest[i * 4 + 3] = (sample & 0xff) as _;
            }
            buffer.enqueue(&[]).unwrap();
        }

        queue.start().unwrap();
        std::thread::sleep(Duration::from_secs(1));
    }

    #[test]
    fn test_aac() {
        let mut queue = AudioQueue::new_output(
            AudioStreamBasicDescription {
                sample_rate: 48000.0,
                format: AudioFormat::MPEG4AAC,
                format_flags: AudioFormatFlags::empty(),
                bits_per_channel: 0,
                channels_per_frame: 2,
                bytes_per_frame: 0,
                frames_per_packet: 1024,
                bytes_per_packet: 0,
            },
            |_buffer| {},
        )
        .unwrap();
        queue.set_volume(0.0).unwrap();

        const BUFFER_SIZE: usize = 4096;

        let data = {
            let mut f = std::fs::File::open("src/testdata/adts.bin").unwrap();
            let mut data = Vec::new();
            f.read_to_end(&mut data).unwrap();
            data
        };
        let mut data = data.as_slice();
        let mut buf = Vec::new();
        let mut packets = Vec::new();
        while data.len() >= 7 {
            let adts = AudioDataTransportStream::parse(data).unwrap();
            if buf.len() + adts.aac_data.len() > BUFFER_SIZE {
                let mut buffer = queue.new_buffer(BUFFER_SIZE).unwrap();
                buffer.set_audio_data_length(buf.len());
                buffer.audio_data_buffer_mut()[..buf.len()].copy_from_slice(&buf);
                buffer.enqueue(&packets).unwrap();
                buf.clear();
                packets.clear();
            }
            packets.push(AudioStreamPacketDescription {
                offset: buf.len(),
                length: adts.aac_data.len(),
                variable_frames: 0,
            });
            buf.extend_from_slice(adts.aac_data);
            data = &data[adts.frame_length..];
        }
        if !buf.is_empty() {
            let mut buffer = queue.new_buffer(BUFFER_SIZE).unwrap();
            buffer.set_audio_data_length(buf.len());
            buffer.audio_data_buffer_mut()[..buf.len()].copy_from_slice(&buf);
            buffer.enqueue(&packets).unwrap();
        }

        queue.start().unwrap();
        std::thread::sleep(Duration::from_secs(3));
    }
}
