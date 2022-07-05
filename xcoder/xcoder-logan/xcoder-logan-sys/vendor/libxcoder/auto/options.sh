#!/bin/bash

# variables, parent script must set it:

#####################################################################################
#####################################################################################
# parse user options, do this at first
#####################################################################################
#####################################################################################

#####################################################################################
# output variables
#####################################################################################
help=no

################################################################
# feature options
XCODER_OLD_NVME_DRIVER=NO
XCODER_IO_RW=NO
XCODER_WIN32=NO
XCODER_WIN_NVME_CUSTOM=NO
XCODER_ENCODER_SYNC_QUERY=NO
XCODER_SELF_KILL_ERR=NO
XCODER_LINUX_CUSTOM_DRIVER=NO
XCODER_LINUX_VIRT_IO_DRIVER=NO
XCODER_LATENCY_PATCH=NO
XCODER_SIGNATURE_FILE=NO
XCODER_DUMP_DATA=NO
XCODER_LIBDIR=NO
XCODER_BINDIR=NO
XCODER_INCLUDEDIR=NO
XCODER_SHAREDDIR=NO
XCODER_PREFIX='/usr/local'

#####################################################################################
# menu
#####################################################################################
function show_help() {
    cat << END

Options:
  -h, --help                print this message

  --with-old-nvme-driver    enable old nvme driver support
  --without-old-nvme-driver disable old nvme driver support (default)

  --with-io-rw              enable nvme io read/write support (default)
  --without-io-rw disable nvme io read/write support


  --with-self-kill          enable self-killing process during error feature
  --without-self-kill       disable self-killing process during error feature (default)

  --with-linux-custom-driver     with custom linux driver
  --without-linux-custom-driver  without custom linux driver (default)

  --with-linux-virt-io-driver     with linux virt-io driver
  --without-linux-virt-io-driver  without linux virt-io driver (default)

  --with-encoder-sync-query      with encoder read sync query
  --without-encoder-sync-query   without encoder read sync query (default)

  --with-latency-patch      enable latency-patch feature
  --without-latency-patch   disable latency-patch feature (default)

  --prefix                  Set custom install location preix
                            (default: /usr/local/)
  --libdir                  Set custom install location for libxcoder.so and pkgconfig files
                            (default: /usr/local/lib/)
  --bindir                  Set custom install location for binary utilities (ni_rsrc_mon, etc.)
                            (default: /usr/local/bin/)
  --includedir              Set custom install location for libxcoder headers
                            (default: /usr/local/include/)
  --shareddir               Set additional install location for libxcoder.so
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
            --with-old-nvme-driver)     XCODER_OLD_NVME_DRIVER=YES;;
            --without-old-nvme-driver)  XCODER_OLD_NVME_DRIVER=NO;;
            --with-io-rw)               XCODER_IO_RW=YES;;
            --without-io-rw)            XCODER_IO_RW=NO;;
            --with-win32)               XCODER_WIN32=YES;;                                            
            --without-win32)            XCODER_WIN32=NO;;
            --with-custom-nvme)         XCODER_WIN_NVME_CUSTOM=YES;;
            --without-custom-nvme)      XCODER_WIN_NVME_CUSTOM=NO;;
            --with-self-kill)           XCODER_SELF_KILL_ERR=YES;;
            --without-self-kill)        XCODER_SELF_KILL_ERR=NO;;
            --with-linux-custom-driver)     XCODER_LINUX_CUSTOM_DRIVER=YES;;
            --without-linux-custom-driver)  XCODER_LINUX_CUSTOM_DRIVER=NO;;
            --with-linux-virt-io-driver)    XCODER_LINUX_VIRT_IO_DRIVER=YES;;
            --without-linux-virt-io-driver) XCODER_LINUX_VIRT_IO_DRIVER=NO;;
            --with-encoder-sync-query)      XCODER_ENCODER_SYNC_QUERY=YES;;
            --without-encoder-sync-query)   XCODER_ENCODER_SYNC_QUERY=NO;;
            --with-latency-patch)       XCODER_LATENCY_PATCH=YES;;
            --without-latency-patch)    XCODER_LATENCY_PATCH=NO;;
            --with-signature-file)      XCODER_SIGNATURE_FILE=YES;;
            --without-signature-file)   XCODER_SIGNATURE_FILE=NO;;
            --with-data-dump)           XCODER_DUMP_DATA=YES;;
            --without-data-dump)        XCODER_DUMP_DATA=NO;;
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
    if [ "$XCODER_OLD_NVME_DRIVER" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-old-nvme-driver"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-old-nvme-driver"; fi
    if [ "$XCODER_IO_RW" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-io-rw"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-io-rw"; fi
    if [ "$XCODER_WIN32" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-win32"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-win32"; fi
    if [ "$XCODER_WIN_NVME_CUSTOM" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-custom-nvme"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-win32"; fi
    if [ "$XCODER_SELF_KILL_ERR" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-self-kill"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-self-kill"; fi
    if [ "$XCODER_LINUX_CUSTOM_DRIVER" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-linux-custom-driver"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-linux-custom-driver"; fi
    if [ "$XCODER_LINUX_VIRT_IO_DRIVER" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-linux-virt-io-driver"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-linux-virt-io-driver"; fi
    if [ "$XCODER_ENCODER_SYNC_QUERY" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-encoder-sync-query"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-encoder-sync-query"; fi
    if [ "$XCODER_LATENCY_PATCH" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-latency-patch"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-latency-patch"; fi
    if [ "$XCODER_SIGNATURE_FILE" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-signature-file"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-signature-file"; fi
    if [ "$XCODER_DUMP_DATA" = YES ]; then XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --with-data-dump"; else XCODER_AUTO_CONFIGURE="${XCODER_AUTO_CONFIGURE} --without-data-dump"; fi
    echo "regenerate config: ${XCODER_AUTO_CONFIGURE}"
}

function check_option_conflicts() {
    __check_ok=YES
    # check conflict


    # check variable neccessary
    if [ "$XCODER_WIN32" = RESERVED ]; then echo "you must specify the OS support, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_WIN_NVME_CUSTOM" = RESERVED ]; then echo "you must specify the NVME CUSTOM support, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_OLD_NVME_DRIVER" = RESERVED ]; then echo "you must specify if working with old NVMe driver, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_IO_RW" = RESERVED ]; then echo "you must specify if working with io read/write, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_SELF_KILL_ERR" = RESERVED ]; then echo "you must specify whether to compile self-kill macro on error, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_LINUX_CUSTOM_DRIVER" = RESERVED ]; then echo "you must specify for the multi-namespace & low latency custom NVMe driver, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_LINUX_VIRT_IO_DRIVER" = RESERVED ]; then echo "you must specify for the vm linux virt-io driver, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_ENCODER_SYNC_QUERY" = RESERVED ]; then echo "you must specify if working with encoder read sync query feature, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_LATENCY_PATCH" = RESERVED ]; then echo "you must specify whether to compile latency-patch macro, see: ./configure --help"; __check_ok=NO; fi
    if [ "$XCODER_DUMP_DATA" = RESERVED ]; then echo "you must specify whether to compile data-dump macro, see: ./configure --help"; __check_ok=NO; fi
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
