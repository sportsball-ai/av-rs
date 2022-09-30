
Logan Xcoder Codec Library User Guide:



==============================
To configure and build library
==============================

---------------------------
To default install location:
---------------------------

cd libxcoder_logan
./build.sh


Codec library 'libxcoder_logan.a' is by default installed to         /usr/local/lib
Codec library 'libxcoder_logan.so' is by default installed to        /usr/local/lib
Codec Pkg Config 'xcoder_logan.pc' is by default installed to  /usr/local/lib/pkgconfig
Codec API Header files are by default installed to             /usr/local/include
Standalone test program 'xcoder_logan' is locally generated in       libxcoder_logan/bin


--------------------------
To fully customize install:
--------------------------

./configure --libdir=/custom_lib_folder --bindir=/custom_bin_folder \
--includedir=/custom_include_folder --shareddir=/additional_lib_folder && sudo make install


------------
To uninstall:
------------

sudo make uninstall

or

sudo make uninstall LIBDIR=/custom_lib_folder BINDIR=/custom_bin_folder \
INCLUDEDIR=/custom_include_folder SHAREDDIR=/additional_lib_folder


-------------
Build Options:
-------------

To enable libxcoder self termination on repeated NVMe error
./configure --with-self-kill

To enable libxcoder latency test patch (default: --without-latency-patch)
./configure --with-latency-patch

To set custom installation path for libxcoder_logan.so and pkgconfig files
./configure --libdir custom_lib_folder/

To set custom installation path for binary utilities (ni_rsrc_mon_logan, etc.)
./configure --bindir custom_bin_folder/

To set custom installation path for libxcoder headers
./configure --includedir custom_include_folder/

To set additional installation path for libxcoder_logan.so
./configure --shareddir additional_lib_folder/

==============================
To run standalone test program
==============================
Note: for now, decoding/encoding/transcoding has the following unsupported features:
 - Audio
 - Container, demuxer, muxer
 - H.265 frame parsing
 - Custom user SEI
 - Encoder sequence change
 - Decoder/encoder engine reset/recovery
 - Dolby Vision


------------
Test Decoder:
------------

 cd libxcoder_logan/bin
 sudo ./xcoder_logan -c 0 -i ../test/akiyo_352x288p25.265 -o aki.yuv -m h2a

------------
Test Encoder:
------------

 cd libxcoder_logan/bin
 sudo ./xcoder_logan -c 0 -i ../test/akiyo_352x288p25.yuv -o aki-xcoder.265 -s 352x288 -m y2h
 
---------------
Test Transcoder:
---------------

 cd libxcoder_logan/bin
 sudo ./xcoder_logan -c 0 -i ../test/akiyo_352x288p25.265 -o aki-xcoder-trans.264 -m h2a

---------------
Test Multi-threading Decoding:
---------------

 cd libxcoder/build
 sudo ./xcoder_logan -c 0 -i ../test/akiyo_352x288p25.265 -o aki.yuv -m h2y --dec_async -t 4

 
===================================
To Integrate into user applications
===================================

-------------------
FFmpeg applications:
-------------------

Configure and build FFmpeg with:

./configure --enable-libxcoder_logan && make


------------------
Other applications:
------------------

Codec library: libxcoder_logan.a
API header files: ni_device_api_logan.h etc.

1. Add libxcoder_logan.a as one of libraries to link
2. Add ni_device_api_logan.h etc. in source code calling Codec API, e.g.:

For C
#include "ni_device_api_logan.h"

For C++

extern "C" {
#include "ni_device_api_logan.h"
}
