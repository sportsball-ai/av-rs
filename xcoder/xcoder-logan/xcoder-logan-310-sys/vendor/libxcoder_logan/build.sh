#!/usr/bin/env bash

target_windows=false;
target_android=false;
include_gdb=false;
enable_shared=false;
kill_myself=false;
latency_display=false;
build_linux_custom_driver=false;
signature_file=false;
build_linux_virt_io_driver=false;
tracelog_timestamps=false;
build_doxygen=false;
encoder_sync_query=false;

if [ `whoami` = root ]; then
    read -p "Do you wish to execute with sudo [Y/N]? " -n 1 -r
    echo   
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit
    fi
fi

while [ "$1" != "" ]; do
    case $1 in
        -h | --help)     echo "Usage: ./build.sh [OPTION]";
                         echo "Compile T40X libxcoder.";
                         echo "Example: ./build.sh";
                         echo;
                         echo "Options:";
                         echo "-h, --help                       display this help and exit";
                         echo "-w, windows                      compile for windows";
                         echo "-a, android                      compile for android";
                         echo "-g, --gdb                        compile with gdb debugging (and without -O3 optimizations)";
                         echo "-s, --shared                     compile shared libraries (for Windows, always true in Linux)"
                         echo "-k, --with-self-kill             compile with self termination on multiple repeated NVMe errors";
                         echo "-c, --with-linux-custom-driver   compile with linux custom nvme driver";
                         echo "-q, --with-encoder-sync-query    compile with encoder read sync query";
                         echo "-p, --with-latency-display       compile with per frame latency display";
                         echo "-v, --with-linux-virt-io-driver  compile with vm linux virt-io driver";
                         echo "-l, --with-tracelog-timestamps   compile with nanosecond timestamps on tracelogs";
                         echo "--doxygen                        compile Doxygen (does not compile libxcoder)"; exit 0
        ;;
        -w | windows)                     target_windows=true
        ;;
        -a | --android)                   target_android=true
        ;;
        -g | --gdb)                       include_gdb=true
        ;;
        -s | --shared)                    enable_shared=true
        ;;
        -k | --with-self-kill)            kill_myself=true
        ;;
        -c | --with-linux-custom-driver)  build_linux_custom_driver=true
        ;;
        -q | --with-encoder-sync-query)   encoder_sync_query=true
        ;;
        -p | --with-latency-display)      latency_display=true
        ;;
        -a | --with-signature-file)       signature_file=true
        ;;
        -v | --with-linux-virt-io-driver) build_linux_virt_io_driver=true
        ;;
        -l | --with-tracelog-timestamps)  tracelog_timestamps=true
        ;;
        --doxygen)                        build_doxygen=true
        ;;
        *) echo "Usage: ./build.sh [OPTION]..."; echo "Try './build.sh --help' for more information"; exit 1
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

extra_make_flags=""
extra_config_flags=""
if $target_windows; then
    extra_make_flags="${extra_make_flags} WINDOWS=TRUE"
    extra_config_flags="${extra_config_flags} --with-win32"
else
    enable_shared=true
fi

if $target_android; then
    extra_config_flags="${extra_config_flags} --with-android"
fi

if [[ $(uname -r) == *"el6"* ]]; then
    extra_make_flags="${extra_make_flags} RHEL6=TRUE"
fi

if $include_gdb; then
    extra_make_flags="${extra_make_flags} GDB=TRUE"
fi

if $enable_shared; then
    extra_make_flags="${extra_make_flags} SHARED=TRUE"
fi

if $build_linux_custom_driver; then
    extra_make_flags="${extra_make_flags} CUSTOM_DRIVER=TRUE"
    extra_config_flags="${extra_config_flags} --with-linux-custom-driver"
fi

if $build_linux_virt_io_driver; then
    extra_config_flags="${extra_config_flags} --with-linux-virt-io-driver"
fi

if $tracelog_timestamps; then
    extra_config_flags="${extra_config_flags} --with-tracelog-timestamps"
fi

if $kill_myself; then
    extra_config_flags="${extra_config_flags} --with-self-kill"
else
    extra_config_flags="${extra_config_flags} --without-self-kill"
fi

if $latency_display; then
    extra_config_flags="${extra_config_flags} --with-latency-display"
fi

if $encoder_sync_query; then
    extra_config_flags="${extra_config_flags} --with-encoder-sync-query"
fi

if $signature_file; then
    FILE=ni_session_sign
    if [ ! -f "$FILE" ]; then
        echo "$FILE session signature file does not exist.";
        exit 1
    fi

    max_file_size=256
    actual_size=$(wc -c <"$FILE")
    if [ $actual_size -gt $max_file_size ]; then
        echo "$FILE size can not exceed $max_file_size.";
        exit 1
    fi

    extra_config_flags="${extra_config_flags} --with-signature-file"
fi

echo bash ./configure $extra_config_flags;
bash ./configure $extra_config_flags;

RC=$?;

if [ $RC = 0 ]; then
    echo make all$extra_make_flags
    make all$extra_make_flags;
    RC=$?;
else
    exit $RC;
fi

if [ $RC = 0 ]; then
    if $target_windows; then
        echo make install$extra_make_flags;
        make install$extra_make_flags;
    else
        echo sudo make install$extra_make_flags;
        sudo make install$extra_make_flags;
    fi
    RC=$?;
else
    exit $RC;
fi

sudo ldconfig &> /dev/null;
exit $RC;
