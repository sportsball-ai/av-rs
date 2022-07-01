

directly install mingw
    sudo apt update
    sudo apt upgrade
    sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git
    sudo apt-get autoremove

    sudo apt-get install mingw-w64
    sudo apt-get install gcc-mingw-w64-x86-64
    sudo apt-get install mingw-w64-tools

Check and Verify with 
    apt-cache search mingw
        gcc-mingw-w64-x86-64 - GNU C compiler for MinGW-w64 targeting Win64
        mingw-w64 - Development environment targeting 32- and 64-bit Windows
        mingw-w64-tools - Development tools for 32- and 64-bit Windows

cd /usr/x86_64-w64-mingw32/ && sudo mkdir bin

ERROR : yasm/nasm not found or too old. Use --disable-yasm for a crippled build
Fix   : sudo apt-get install yasm
