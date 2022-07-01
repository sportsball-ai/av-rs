
Logan Xcoder Codec Library User Guide:



==============================
To configure and build library
==============================

---------------------------
To default install location:
---------------------------

cd libxcoder
./build.sh


Codec library 'libxcoder.a' is by default installed to         /usr/local/lib
Codec library 'libxcoder.so' is by default installed to        /usr/local/lib
Codec Pkg Config 'xcoder.pc' is by default installed to        /usr/local/lib/pkgconfig
Codec API Header files are by default installed to             /usr/local/include
Standalone test program 'xcoder' is locally generated in       libxcoder/build


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

To enable video data dump (default: --without-data-dump)
./configure --with-data-dump

To enable support for old NVMe drivers, e.g. CentOS 6.5 (default: --without-old-nvme-driver)
./configure --with-old-nvme-driver

To enable libxcoder self termination on repeated NVMe error
./configure --with-self-kill

To enable libxcoder latency test patch (default: --without-latency-patch)
./configure --with-latency-patch

To set custom installation path for libxcoder.so and pkgconfig files
./configure --libdir custom_lib_folder/

To set custom installation path for binary utilities (ni_rsrc_mon, etc.)
./configure --bindir custom_bin_folder/

To set custom installation path for libxcoder headers
./configure --includedir custom_include_folder/

To set additional installation path for libxcoder.so
./configure --shareddir additional_lib_folder/

==============================
To run standalone test program
==============================
Note: for now, decoding/encoding/transcoding is limited to resolutions of: width
      must be multiples of 32, height must be multiples of 16, as in the
      examples below.

------------
Test Decoder:
------------

 cd libxcoder/build
 sudo ./xcoder 0  ../test/akiyo_352x288p25.264 aki.yuv 352 288 decode 0 131040

------------
Test Encoder:
------------

 cd libxcoder/build
 sudo ./xcoder 0  ../test/akiyo_352x288p25.yuv aki-xcoder.265 352 288 encode 1 131040
 
---------------
Test Transcoder:
---------------

 cd libxcoder/build
 sudo ./xcoder 0 ../test/akiyo_352x288p25.264 aki-xcoder-trans.265 352 288 xcode 0-1 131040



 
===================================
To Integrate into user applications
===================================

-------------------
FFmpeg applications:
-------------------

Configure and build FFmpeg with:

./configure --enable-libxcoder && make


------------------
Other applications:
------------------

Codec library: libxcoder.a
API header files: ni_device_api.h ni_rsrc_api.h

1. Add libxcoder.a as one of libraries to link
2. Add ni_device_api.h in source code calling Codec API

For C
#include "ni_device_api.h"

For C++

extern "C" {
#include "ni_device_api.h"
}
