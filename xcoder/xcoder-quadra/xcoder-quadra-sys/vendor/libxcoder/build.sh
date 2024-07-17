#!/usr/bin/env bash

target_windows=false;
target_android=false;
include_gdb=false;
self_kill=false;
latency_display=false;
dump_data=false;
tracelog_timestamps=false;
build_linux_virt_io_driver=false;
warn_as_errors=false;
secure_compile=false;
build_doxygen=false;
disable_backtrace_print=false;
info_level_ssim_log=false;
RC=0

while [ "$1" != "" ]; do
    case $1 in
        -h | --help)     echo "Usage: ./build.sh [OPTION]";
                         echo "Compile Quadra libxcoder.";
                         echo "Example: ./build.sh";
                         echo;
                         echo "Options:";
                         echo "-h, --help                   display this help and exit";
                         echo "-w, windows                  compile for Windows";
                         echo "-a, --android                compile for Android";
                         echo "-g, --gdb                    compile with gdb debugging (and without -O3 optimizations)";
                         echo "-k, --with-self-kill         compile with self termination on multiple repeated NVMe errors";
                         echo "-p, --with-latency-display   compile with per frame latency display";
                         echo "-d, --with-data-dump         compile with dumping video transcoding data";
                         echo "-v, --with-linux-virt-io-driver  compile with vm linux virt-io driver";
                         echo "-l, --with-tracelog-timestamps   compile with microsecond timestamps on tracelogs";
                         echo "-e, --warnings-as-errors         compile with '-Werror'. Deprecation macros disabled";
                         echo "-b, --disable-backtrace-print    complie without print backtrace"
                         echo "-s, --secure-compile         compile with more foritication such as strong stack protection and RELRO";
                         echo "-m, --with-info-level-ssim-log   compile with SSIM logging at info level. Default is at debug level";
                         echo "--doxygen                        compile Doxygen (does not compile libxcoder)"; exit 0
        ;;
        -w | windows)                   target_windows=true
        ;;
        -a | --android)                 target_android=true
        ;;
        -g | --gdb)                     include_gdb=true
        ;;
        -k | --with-self-kill)          self_kill=true
        ;;
        -p | --with-latency-display)    latency_display=true
        ;;
        -v | --with-linux-virt-io-driver) build_linux_virt_io_driver=true
        ;;
        -l | --with-tracelog-timestamps)     tracelog_timestamps=true
        ;;
        -d | --with-data-dump)          dump_data=true
        ;;
        -e | --warnings-as-errors)      warn_as_errors=true
        ;;
        -b | --disable-backtrace-print)      disable_backtrace_print=true
        ;;
        -s | --secure-compile)          secure_compile=true
        ;;
        -m | --with-info-level-ssim-log)     info_level_ssim_log=true
        ;;
        --doxygen)                      build_doxygen=true
        ;;
        *)               echo "Usage: ./build.sh [OPTION]..."; echo "Try './build.sh --help' for more information"; exit 1
        ;;
    esac
    shift
done

if $build_doxygen; then
    # get project number from source/ni_defs.h
    PROJ_NUM="$(grep -Poh "NI_XCODER_REVISION\s+\"\K[a-zA-Z0-9]{3}(?=.{5}\")" source/ni_defs.h)"
    PROJ_NUM_FMTD=${PROJ_NUM:0:1}.${PROJ_NUM:1:1}.${PROJ_NUM:2:1}
    `which sed` -i "s/^\(PROJECT_NUMBER\s*=\).*$/\1 $PROJ_NUM_FMTD/" source/doxygen/Doxyfile &&
    doxygen source/doxygen/Doxyfile; RC=$?
    `which sed` -i "s/^\(PROJECT_NUMBER\s*=\).*$/\1/" source/doxygen/Doxyfile
    exit $RC
fi

# handle command line options
extra_make_flags=""
extra_config_flags=""

if $include_gdb; then
    extra_make_flags=" GDB=TRUE"
fi

if $target_windows; then
    extra_make_flags="${extra_make_flags} WINDOWS=TRUE"
    extra_config_flags="${extra_config_flags} --with-win32 --without-self-kill"
    self_kill=false
fi

if $target_android; then
    extra_make_flags="${extra_make_flags} DONT_WRITE_SONAME=TRUE"
    extra_config_flags="${extra_config_flags} --with-android"
fi

if $self_kill; then
    extra_config_flags="${extra_config_flags} --with-self-kill"
else
    extra_config_flags="${extra_config_flags} --without-self-kill"
fi

if $latency_display; then
    extra_config_flags="${extra_config_flags} --with-latency-display"
fi

if $tracelog_timestamps; then
    extra_config_flags="${extra_config_flags} --with-tracelog-timestamps"
fi

if $info_level_ssim_log; then
    extra_config_flags="${extra_config_flags} --with-info-level-ssim-log"
fi

if $dump_data; then
    extra_config_flags="${extra_config_flags} --with-data-dump"
fi

if $build_linux_virt_io_driver; then
    extra_config_flags="${extra_config_flags} --with-linux-virt-io-driver"
fi

if $warn_as_errors; then
    extra_make_flags="${extra_make_flags} WARN_AS_ERROR=TRUE"
fi

if $disable_backtrace_print; then
    extra_config_flags="${extra_config_flags} --without-backtrace-print"
fi

if $secure_compile; then
    extra_make_flags="${extra_make_flags} SECURE_COMPILE=TRUE"
fi

# configure, build, and install
echo bash ./configure $extra_config_flags;
bash ./configure $extra_config_flags;
RC=$?

if [ $RC = 0 ]; then
    echo make all $extra_make_flags
    make all $extra_make_flags
    RC=$?
else
    exit $RC
fi

if [ $RC = 0 ]; then
    if $target_windows; then
        echo make install $extra_make_flags
        make install $extra_make_flags
        RC=$?
    else
        echo sudo make install $extra_make_flags
        sudo make install $extra_make_flags
        RC=$?
    fi
else
    exit $RC
fi

exit $RC
