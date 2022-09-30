target_windows=false;
include_gdb=false;
enable_shared=false;
kill_myself=false;
latency_patch=false;
build_old_nvme_driver=false;
build_linux_custom_driver=false;
signature_file=false;
build_linux_virt_io_driver=false;
build_io_rw_macro=true;
build_doxygen=false;
encoder_sync_query=false;
dump_data=false;

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
                         echo "-g, --gdb                        compile with gdb debugging (and without -O3 optimizations)";
                         echo "-s, --shared                     compile shared libraries (for Windows, always true in Linux)"
                         echo "-k, --with-self-kill             compile with self termination on multiple repeated NVMe errors";
                         echo "-c, --with-linux-custom-driver   compile with linux custom nvme driver";
                         echo "-q, --with-encoder-sync-query    compile with encoder read sync query";
                         echo "-p, --with-latency-patch         compile with latency test patch";
                         echo "-o, --with-old-nvme-driver       compile targeting Linux NVMe driver module older than v0.10";
                         echo "-v, --with-linux-virt-io-driver  compile with vm linux virt-io driver";
                         echo "-d, --with-data-dump             compile with dumping video transcoding data";
                         echo "-i, --without-io-rw              compile without enable sending io read/write to xcoder";
                         echo "--doxygen                        compile Doxygen (does not compile libxcoder)"; exit 0
        ;;
        -w | windows)                   target_windows=true
        ;;
        -g | --gdb)                     include_gdb=true
        ;;
        -s | --shared)                  enable_shared=true
        ;;
        -k | --with-self-kill)          kill_myself=true
        ;;
        -o | --with-old-nvme-driver)    build_old_nvme_driver=true
        ;;
        -c | --with-linux-custom-driver)    build_linux_custom_driver=true
        ;;
        -q | --with-encoder-sync-query)     encoder_sync_query=true
        ;;
        -p | --with-latency-patch)      latency_patch=true
        ;;
        -a | --with-signature-file)     signature_file=true
        ;;
        -v | --with-linux-virt-io-driver)     build_linux_virt_io_driver=true
        ;;
        -d | --with-data-dump)          dump_data=true
        ;;
        -i | --without-io-rw)           build_io_rw_macro=false
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

extra_make_flags=""
if $target_windows; then
    extra_make_flags=" WINDOWS=TRUE"
else
    enable_shared=true
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

if $build_old_nvme_driver; then
    extra_config_flags="${extra_config_flags} --with-old-nvme-driver"
fi

if $build_linux_virt_io_driver; then
    extra_config_flags="${extra_config_flags} --with-linux-virt-io-driver"
fi

if $build_io_rw_macro; then
    extra_config_flags="${extra_config_flags} --with-io-rw"
else
    extra_config_flags="${extra_config_flags} --without-io-rw"
fi

if $kill_myself; then
    extra_config_flags="${extra_config_flags} --with-self-kill"
else
    extra_config_flags="${extra_config_flags} --without-self-kill"
fi

if $latency_patch; then
    extra_config_flags="${extra_config_flags} --with-latency-patch"
fi

if $encoder_sync_query; then
    extra_config_flags="${extra_config_flags} --with-encoder-sync-query"
fi

if $dump_data; then
    extra_config_flags="${extra_config_flags} --with-data-dump"
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
echo $extra_make_flags
if $target_windows; then
    if $build_io_rw_macro; then
        echo bash ./configure --with-win32 $extra_config_flags;
        bash ./configure --with-win32 $extra_config_flags;
    else
        echo bash ./configure --with-win32 --with-custom-nvme --without-self-kill $extra_config_flags;
    fi
    RC=$?;
    
    if [ $RC = 0 ]; then
        make all$extra_make_flags;
        RC=$?;
    else
        exit $RC;
    fi
    
    if [ $RC = 0 ]; then
        make install$extra_make_flags;
        RC=$?;
    else
        exit $RC;
    fi
else
    bash ./configure $extra_config_flags;
    RC=$?;
    
    if [ $RC = 0 ]; then
        make all$extra_make_flags;
        RC=$?;
    else
        exit $RC;
    fi
    
    if [ $RC = 0 ]; then
        sudo make install$extra_make_flags;
        RC=$?;
    else
        exit $RC;
    fi
fi
sudo ldconfig &> /dev/null;
exit $RC;
