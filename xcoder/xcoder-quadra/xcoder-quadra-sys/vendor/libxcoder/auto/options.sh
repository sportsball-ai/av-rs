#!/usr/bin/env bash
# Parse options for ../configure

################################################################
# feature options
XCODER_WIN32=NO
XCODER_ANDROID=NO
XCODER_SELF_KILL_ERR=NO
XCODER_LATENCY_DISPLAY=NO
XCODER_TRACELOG_TIMESTAMPS=NO
XCODER_LINUX_VIRT_IO_DRIVER=NO
XCODER_DUMP_DATA=NO
XCODER_PREFIX='/usr/local'
XCODER_LIBDIR=NO
XCODER_BINDIR=NO
XCODER_INCLUDEDIR=NO
XCODER_SHAREDDIR=NO
XCODER_DISABLE_BACKTRACE_PRINT=NO
XCODER_SSIM_INFO_LEVEL_LOGGING=NO

#####################################################################################
# menu
#####################################################################################
function show_help() {
    cat << END

Options:
  -h, --help                      print this message

  --with-win32                    configure for win32 compile
  --without-win32                 do not configure for win32 compile (default)

  --with-android                  configure for android compile
  --without-android               do not configure for android compile (default)

  --with-self-kill                enable self-killing process during error feature
  --without-self-kill             disable self-killing process during error feature (default)

  --with-latency-display          enable latency-display feature
  --without-latency-display       disable latency-display feature (default)

  --with-tracelog-timestamps      enable tracelog-timestamps feature
  --without-tracelog-timestamps   disable tracelog-timestamps feature (default)

  --with-info-level-ssim-log      enable info-level-ssim-log feature
  --without-info-level-ssim-log   disable info-level-ssim-log feature (default)

  --with-linux-virt-io-driver     with linux virt-io driver
  --without-linux-virt-io-driver  without linux virt-io driver (default)

  --with-data-dump                enable debug dump of video data exchanged on NVMe
  --without-data-dump             disable debug dump of video data exchanged on NVMe (default)

  --with-backtrace-print          enable print backtrace (default)
  --without-backtrace-print       disable print backtrace

  --prefix                        Set custom install location preix
                                  (default: /usr/local/)
  --libdir                        Set custom install location for libxcoder.so and pkgconfig files
                                  (default: /usr/local/lib/)
  --bindir                        Set custom install location for binary utilities (ni_rsrc_mon, etc.)
                                  (default: /usr/local/bin/)
  --includedir                    Set custom install location for libxcoder headers
                                  (default: /usr/local/include/)
  --shareddir                     Set additional install location for libxcoder.so
                                  (ex. /usr/lib/x86_64-linux-gnu/ or /usr/lib64/) (default: NONE)

END
}

# parse a flag with an arg in or after it
# $1 flag pattern, $2 entire flag arg, $3 arg after flag arg
# return 1 if path is in second arg (separated by space), else return 0. Store path in $extract_arg_ret
function extract_arg() {
    unset extract_arg_ret
    # check valid arg flag
    if [ -n "$(printf "%s" ${2} | grep -Eoh "${1}")" ]; then
        # check if path string is connected by '=' or is in following arg
        if [ -n "$(echo "${2}" | grep -Eoh "${1}=")" ]; then
            arg_str=`printf "%s" "${2}" | grep -Poh "${1}=\K.+"`;
            # trim out leading and trailing quotation marks
            extract_arg_ret=`echo "${arg_str}" | sed -e 's/^\(["'\'']\)//' -e 's/\(["'\'']\)$//'`;
            return 0;
        elif [ -n "$(printf "%s" ${2} | grep -Eoh "^${1}$")" ]; then
            arg_str="${3}";
            # trim out leading and trailing quotation marks
            extract_arg_ret=`printf "%s" "${arg_str}" | sed -e 's/^\(["'\'']\)//' -e 's/\(["'\'']\)$//'`;
            return 1;
        else
            echo "Unknown option '$2', exiting";
            exit 1;
        fi
    else
        echo "Target flag '$1' not found in '$2', exiting"; exit 1;
    fi
}

function parse_user_option() {
    while [ "$1" != "" ]; do
        case $1 in
            -h | --help)                show_help; exit 0;;
            --with-win32)               XCODER_WIN32=YES;;
            --without-win32)            XCODER_WIN32=NO;;
            --with-android)             XCODER_ANDROID=YES;;
            --without-android)          XCODER_ANDROID=NO;;
            --with-self-kill)           XCODER_SELF_KILL_ERR=YES;;
            --without-self-kill)        XCODER_SELF_KILL_ERR=NO;;
            --with-latency-display)     XCODER_LATENCY_DISPLAY=YES;;
            --without-latency-display)  XCODER_LATENCY_DISPLAY=NO;;
            --with-tracelog-timestamps)     XCODER_TRACELOG_TIMESTAMPS=YES;;
            --without-tracelog-timestamps)  XCODER_TRACELOG_TIMESTAMPS=NO;;
            --with-linux-virt-io-driver)    XCODER_LINUX_VIRT_IO_DRIVER=YES;;
            --without-linux-virt-io-driver) XCODER_LINUX_VIRT_IO_DRIVER=NO;;
            --with-data-dump)           XCODER_DUMP_DATA=YES;;
            --without-data-dump)        XCODER_DUMP_DATA=NO;;
            --without-backtrace-print)  XCODER_DISABLE_BACKTRACE_PRINT=YES;;
            --with-info-level-ssim-log)     XCODER_SSIM_INFO_LEVEL_LOGGING=YES;;
            --without-info-level-ssim-log)  XCODER_SSIM_INFO_LEVEL_LOGGING=NO;;
            --prefix*)                  extract_arg "\-\-prefix" $1 $2; eprc=$?;
                                        if [ "$eprc" -eq 1 ]; then
                                            shift;
                                        fi
                                        XCODER_PREFIX=$extract_arg_ret;;
            --libdir*)                  extract_arg "\-\-libdir" $1 $2; eprc=$?;
                                        if [ "$eprc" -eq 1 ]; then
                                            shift;
                                        fi
                                        XCODER_LIBDIR=$extract_arg_ret;;
            --bindir*)                  extract_arg "\-\-bindir" $1 $2; eprc=$?;
                                        if [ "$eprc" -eq 1 ]; then
                                            shift;
                                        fi
                                        XCODER_BINDIR=$extract_arg_ret;;
            --includedir*)              extract_arg "\-\-includedir" $1 $2; eprc=$?;
                                        if [ "$eprc" -eq 1 ]; then
                                            shift;
                                        fi
                                        XCODER_INCLUDEDIR=$extract_arg_ret;;
            --shareddir*)               extract_arg "\-\-shareddir" $1 $2; eprc=$?;
                                        if [ "$eprc" -eq 1 ]; then
                                            shift;
                                        fi
                                        XCODER_SHAREDDIR=$extract_arg_ret;;
            *)                          echo "Unknown option $1, exiting"; exit 1;;
        esac
        shift
    done
}

function regenerate_options() {
    # save all config options to macro to write to auto headers file
    XCODER_AUTO_USER_CONFIGURE="$opt"
    # regenerate the options for default values.
    XCODER_AUTO_CONFIGURE="--prefix=${XCODER_PREFIX}"
    XCODER_AUTO_CONFIGURE="--libdir=${XCODER_LIBDIR}"
    XCODER_AUTO_CONFIGURE="--bindir=${XCODER_BINDIR}"
    XCODER_AUTO_CONFIGURE="--includedir=${XCODER_INCLUDEDIR}"
    XCODER_AUTO_CONFIGURE="--shareddir=${XCODER_SHAREDDIR}"
    if [ "$XCODER_WIN32" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-win32"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-win32"; fi
    if [ "$XCODER_ANDROID" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-android"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-android"; fi
    if [ "$XCODER_SELF_KILL_ERR" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-self-kill"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-self-kill"; fi
    if [ "$XCODER_LATENCY_DISPLAY" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-latency-display"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-latency-display"; fi
    if [ "$XCODER_TRACELOG_TIMESTAMPS" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-tracelog-timestamps"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-tracelog-timestamps"; fi
    if [ "$XCODER_DUMP_DATA" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-data-dump"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-data-dump"; fi
    if [ "$XCODER_LINUX_VIRT_IO_DRIVER" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-linux-virt-io-driver"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-linux-virt-io-driver"; fi
    if [ "$XCODER_DISABLE_BACKTRACE_PRINT" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-backtrace-print"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-backtrace-print"; fi
    if [ "$XCODER_SSIM_INFO_LEVEL_LOGGING" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-info-level-ssim-log"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-info-level-ssim-log"; fi
    echo "regenerate config: ${XCODER_AUTO_CONFIGURE}"
}

function check_option_conflicts() {
    __check_ok=YES
    # check conflict


    # check variable neccessary
    if [ "$XCODER_WIN32" = RESERVED ]; then echo "you must specify the OS support, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_ANDROID" = RESERVED ]; then echo "you must specify the OS support, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_SELF_KILL_ERR" = RESERVED ]; then echo "you must specify whether to compile self-kill macro on error, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_LATENCY_DISPLAY" = RESERVED ]; then echo "you must specify whether to compile latency-display macro, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_TRACELOG_TIMESTAMPS" = RESERVED ]; then echo "you must specify whether log messages during trace level are prefixed with timestamps, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_LINUX_VIRT_IO_DRIVER" = RESERVED ]; then echo "you must specify for the vm linux virt-io driver, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_DUMP_DATA" = RESERVED ]; then echo "you must specify whether to compile data-dump macro, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_DISABLE_BACKTRACE_PRINT" = RESERVED ]; then echo "you must specify whether to compile with print backtrace, see: ./configure --help"; __check_ok=NO; fi
}

#####################################################################################
# main
#####################################################################################
# compile user string of options
opt=
for option
do
    opt="$opt `echo $option`"
done

parse_user_option $@
regenerate_options
check_option_conflicts

