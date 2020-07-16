use super::SegmentInfo;
use tokio::prelude::*;

#[async_trait]
pub trait SegmentStorage: Send + Unpin + Sized {
    type Segment: AsyncWrite + Send + Unpin;

    /// Returns a Future that creates a new segment using poll_new_segment.
    async fn new_segment(&mut self) -> io::Result<Self::Segment>;

    /// Implementations can override this for a chance to do something with segments once they're
    /// completed. By default the segment is just shut down.
    async fn finalize_segment(&mut self, mut segment: Self::Segment, _info: SegmentInfo) -> io::Result<()> {
        segment.shutdown().await
    }
}

#[async_trait]
impl<T: SegmentStorage> SegmentStorage for &mut T {
    type Segment = T::Segment;

    async fn new_segment(&mut self) -> io::Result<Self::Segment> {
        T::new_segment(self).await
    }

    async fn finalize_segment(&mut self, segment: Self::Segment, info: SegmentInfo) -> io::Result<()> {
        T::finalize_segment(self, segment, info).await
    }
}

pub struct MemorySegmentStorage {
    segments: Vec<(Vec<u8>, SegmentInfo)>,
}

impl MemorySegmentStorage {
    pub fn new() -> Self {
        Self { segments: Vec::new() }
    }

    pub fn segments(&self) -> &Vec<(Vec<u8>, SegmentInfo)> {
        &self.segments
    }
}

#[async_trait]
impl SegmentStorage for MemorySegmentStorage {
    type Segment = Vec<u8>;

    async fn new_segment(&mut self) -> io::Result<Self::Segment> {
        Ok(Vec::new())
    }

    async fn finalize_segment(&mut self, segment: Self::Segment, info: SegmentInfo) -> io::Result<()> {
        debug_assert!(info.size == segment.len());
        self.segments.push((segment, info));
        Ok(())
    }
}
