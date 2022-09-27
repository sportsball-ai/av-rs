.PHONY: all default test clean cleanall install uninstall
WINDOWS ?= FALSE
GDB ?= FALSE
SHARED ?= FALSE
RHEL6 ?= FALSE

INSTALL = install
LIBXCODER_CUSTOM_PARAMETERS ?=

ifeq ($(GDB), TRUE)
	OPTFLAG = -g
else
	OPTFLAG = -O3
endif

PLATFORM = $(shell uname -s)
ifeq ($(findstring MSYS,$(PLATFORM)), MSYS)
	HOST_OS = MINGW
	SUBFIX = .exe
	ARCH = 64
	DLL_MACHINE = i386:x86-64
else ifeq ($(findstring MINGW,$(PLATFORM)), MINGW)
	HOST_OS = MINGW
	SUBFIX = .exe
ifeq ($(findstring MINGW64,$(PLATFORM)), MINGW64)
	ARCH = 64
	DLL_MACHINE = i386:x86-64
else
	ARCH = 32
	DLL_MACHINE = i386
endif
else
	HOST_OS = Linux
	SUBFIX =
endif

CFLAGS = -fPIC -DLIBXCODER_OBJS_BUILD
CPPFLAGS = -D__STDC_FORMAT_MACROS
ifeq ($(RHEL6), TRUE)
CPPFLAGS += -fno-rtti -fno-exceptions
endif
TARGETNAME = xcoder_logan
TARGET = ${TARGETNAME}
TARGET_LIB = lib${TARGETNAME}.a
ifeq ($(HOST_OS), Linux)
TARGET_LIB_SHARED = lib${TARGETNAME}.so
TARGET_VERSION = $(shell grep 'Version: '.* < bin/xcoder_logan.pc  | cut -d ' ' -f 2)
else
TARGET_LIB_SHARED = lib${TARGETNAME}.dll
CFLAGS += -DXCODER_DLL -DLIB_EXPORTS
endif
TARGET_INC1 = ni_device_api_logan.h
TARGET_INC2 = ni_rsrc_api_logan.h
TARGET_INC3 = ni_defs_logan.h
TARGET_INC4 = ni_util_logan.h
TARGET_INC5 = ni_log_logan.h
TARGET_INC6 = ni_bitstream_logan.h
TARGET_INC7 = ni_av_codec_logan.h
TARGET_PC = xcoder_logan.pc
OBJECTS =  ni_nvme_logan.o ni_device_api_priv_logan.o ni_device_api_logan.o ni_util_logan.o ni_log_logan.o ni_rsrc_priv_logan.o ni_rsrc_api_logan.o ni_bitstream_logan.o ni_av_codec_logan.o
LINK_OBJECTS =  ${OBJS_PATH}/ni_nvme_logan.o ${OBJS_PATH}/ni_device_api_priv_logan.o ${OBJS_PATH}/ni_device_api_logan.o ${OBJS_PATH}/ni_util_logan.o ${OBJS_PATH}/ni_log_logan.o ${OBJS_PATH}/ni_rsrc_priv_logan.o ${OBJS_PATH}/ni_rsrc_api_logan.o ${OBJS_PATH}/ni_bitstream_logan.o ${OBJS_PATH}/ni_av_codec_logan.o
ALL_OBJECTS = ni_device_test_logan.o init_rsrc_logan.o test_rsrc_api_logan.o ni_rsrc_mon_logan.o ni_rsrc_update_logan.o ni_rsrc_list_logan.o ${OBJECTS}
SRC_PATH = ./source
OBJS_PATH = ./build
BIN_PATH = ./bin

ifeq ($(CUSTOM_DRIVER), TRUE)
	CFLAGS += -DXCODER_LINUX_CUSTOM_DRIVER
endif

# Read the installation directory from path set in build/xcoder_logan.pc
DESTDIR ?= $(shell sed -n 's/^prefix=\(.*\)/\1/p' $(BIN_PATH)/$(TARGET_PC))
LIBDIR = $(shell sed -n 's/^libdir=\(.*\)/\1/p' $(BIN_PATH)/$(TARGET_PC))
BINDIR = $(shell sed -n 's/^bindir=\(.*\)/\1/p' $(BIN_PATH)/$(TARGET_PC))
INCLUDEDIR = $(shell sed -n 's/^includedir=\(.*\)/\1/p' $(BIN_PATH)/$(TARGET_PC))
SHAREDDIR = $(shell sed -n 's/^shareddir=\(.*\)/\1/p' $(BIN_PATH)/$(TARGET_PC))
ifeq ($(HOST_OS), Linux)
	ifeq ($(PLATFORM), Darwin)
		INCLUDES= -pthread ${OPTFLAG} -lm
	else
		INCLUDES= -pthread -lrt ${OPTFLAG} -lstdc++ -lm
	endif
else
	#CC      = x86_64-w64-mingw32-gcc
	DLLTOOL = dlltool
	INCLUDES= ${OPTFLAG} -lws2_32
	#TARGET_INC_WIN1 = ni_util_logan.h
	#TARGET_INC_WIN2 = ni_log_logan.h
endif

# make target

all:${ALL_OBJECTS}
ifeq ($(HOST_OS), Linux)
	ar rcs $(BIN_PATH)/${TARGET_LIB} ${LINK_OBJECTS}
ifeq ($(SHARED), TRUE)
ifeq ($(PLATFORM), Darwin)
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -shared -Wl -o $(BIN_PATH)/${TARGET_LIB_SHARED}.${TARGET_VERSION} $(LINK_OBJECTS) ${INCLUDES}
else
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -shared -Wl,-soname,${TARGET_LIB_SHARED}.${TARGET_VERSION} -o $(BIN_PATH)/${TARGET_LIB_SHARED}.${TARGET_VERSION} $(LINK_OBJECTS) ${INCLUDES}
endif
endif

	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(BIN_PATH)/${TARGET} $(OBJS_PATH)/ni_device_test_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(BIN_PATH)/test_rsrc_api_logan $(OBJS_PATH)/test_rsrc_api_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(BIN_PATH)/ni_rsrc_mon_logan $(OBJS_PATH)/ni_rsrc_mon_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(BIN_PATH)/ni_rsrc_update_logan $(OBJS_PATH)/ni_rsrc_update_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(BIN_PATH)/ni_rsrc_list_logan $(OBJS_PATH)/ni_rsrc_list_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(BIN_PATH)/init_rsrc_logan $(OBJS_PATH)/init_rsrc_logan.o $(LINK_OBJECTS) ${INCLUDES}

else
ifeq ($(SHARED), TRUE)
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -shared -o $(BIN_PATH)/${TARGET_LIB_SHARED} -Wl,--output-def,$(BIN_PATH)/$(TARGETNAME).def,--out-implib,$(BIN_PATH)/${TARGET_LIB_SHARED}.a $(LINK_OBJECTS) ${INCLUDES}
	${DLLTOOL} -m $(DLL_MACHINE) -D $(BIN_PATH)/${TARGET_LIB_SHARED} -d $(BIN_PATH)/$(TARGETNAME).def -l $(BIN_PATH)/lib${TARGETNAME}.lib
else
	ar rcs $(BIN_PATH)/${TARGET_LIB} ${LINK_OBJECTS}
endif

	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -static -o $(BIN_PATH)/${TARGET} $(OBJS_PATH)/ni_device_test_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -static -o $(BIN_PATH)/test_rsrc_api_logan $(OBJS_PATH)/test_rsrc_api_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -static -o $(BIN_PATH)/ni_rsrc_mon_logan $(OBJS_PATH)/ni_rsrc_mon_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -static -o $(BIN_PATH)/ni_rsrc_update_logan $(OBJS_PATH)/ni_rsrc_update_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -static -o $(BIN_PATH)/ni_rsrc_list_logan $(OBJS_PATH)/ni_rsrc_list_logan.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -static -o $(BIN_PATH)/init_rsrc_logan $(OBJS_PATH)/init_rsrc_logan.o $(LINK_OBJECTS) ${INCLUDES}

endif

ifeq ($(SHARED), TRUE)
	@echo info ${TARGET_LIB_SHARED}
endif
	@echo $(PLATFORM)

test:${ALL_OBJECTS}
	${CC} ${LIBXCODER_CUSTOM_PARAMETERS}  -o $(OBJS_PATH)/${TARGET} $(OBJS_PATH)/*.o ${INCLUDES}
	
install:
	mkdir -p ${INCLUDEDIR}/
	mkdir -p ${LIBDIR}/
	mkdir -p ${LIBDIR}/pkgconfig/
	mkdir -p ${BINDIR}/
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC1} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC2} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC3} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC4} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC5} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC6} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC7} ${INCLUDEDIR}/.

ifeq ($(HOST_OS), Linux)
	${INSTALL} -m 755 ${BIN_PATH}/${TARGET_LIB} ${LIBDIR}/.
ifeq ($(SHARED), TRUE)
	${INSTALL} -m 755 ${BIN_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/.
	(cd ${LIBDIR}; ln -f -s ${TARGET_LIB_SHARED}.${TARGET_VERSION} ${TARGET_LIB_SHARED}; cd -)
ifneq ($(SHAREDDIR),)
	mkdir -p ${SHAREDDIR}/
	${INSTALL} -m 644 ${BIN_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${SHAREDDIR}/.
	(cd ${SHAREDDIR}; ln -f -s ${TARGET_LIB_SHARED}.${TARGET_VERSION} ${TARGET_LIB_SHARED}; cd -)
endif
endif
else
	mkdir -p ${BIN_PATH}/include/
	cp ${SRC_PATH}/${TARGET_INC1} ${BIN_PATH}/include/.
	cp ${SRC_PATH}/${TARGET_INC2} ${BIN_PATH}/include/.
	cp ${SRC_PATH}/${TARGET_INC3} ${BIN_PATH}/include/.
	cp ${SRC_PATH}/${TARGET_INC4} ${BIN_PATH}/include/.
	cp ${SRC_PATH}/${TARGET_INC5} ${BIN_PATH}/include/.
	cp ${SRC_PATH}/${TARGET_INC6} ${BIN_PATH}/include/.
	cp ${SRC_PATH}/${TARGET_INC7} ${BIN_PATH}/include/.

ifeq ($(SHARED), TRUE)
	${INSTALL} -m 755 ${BIN_PATH}/${TARGET_LIB_SHARED} ${BINDIR}/.
	${INSTALL} -m 755 $(BIN_PATH)/lib${TARGETNAME}.lib ${BINDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/${TARGET_LIB_SHARED}.a ${LIBDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/$(TARGETNAME).def ${LIBDIR}/.
ifneq ($(SHAREDDIR),)
	mkdir -p ${SHAREDDIR}/
	${INSTALL} -m 644 ${BIN_PATH}/${TARGET_LIB_SHARED} ${SHAREDDIR}/.
	${INSTALL} -m 755 $(BIN_PATH)/lib${TARGETNAME}.lib ${SHAREDDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/${TARGET_LIB_SHARED}.a ${SHAREDDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/$(TARGETNAME).def ${SHAREDDIR}/.
endif
else
	${INSTALL} -m 755 ${BIN_PATH}/${TARGET_LIB} ${LIBDIR}/.
endif
endif

	${INSTALL} -m 644 ${BIN_PATH}/${TARGET_PC} ${LIBDIR}/pkgconfig/.
	${INSTALL} -m 755 ${BIN_PATH}/ni_rsrc_mon_logan$(SUBFIX) ${BINDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/ni_rsrc_update_logan$(SUBFIX) ${BINDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/ni_rsrc_list_logan$(SUBFIX) ${BINDIR}/.
	${INSTALL} -m 755 ${BIN_PATH}/init_rsrc_logan$(SUBFIX) ${BINDIR}/.

uninstall:
	rm -f ${INCLUDEDIR}/${TARGET_INC1} ${INCLUDEDIR}/${TARGET_INC2} ${INCLUDEDIR}/${TARGET_INC3} ${INCLUDEDIR}/${TARGET_INC4} ${INCLUDEDIR}/${TARGET_INC5} ${INCLUDEDIR}/${TARGET_INC6} ${INCLUDEDIR}/${TARGET_INC7} ${LIBDIR}/${TARGET_LIB} ${LIBDIR}/${TARGET_LIB_SHARED} ${LIBDIR}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/${TARGET_LIB_SHARED}.a ${LIBDIR}/pkgconfig/${TARGET_PC} ${LIBDIR}/$(TARGETNAME).def ${BINDIR}/${TARGET_LIB_SHARED}  ${BINDIR}/lib${TARGETNAME}.lib ${BINDIR}/ni_rsrc_mon_logan$(SUBFIX) ${BINDIR}/ni_rsrc_update_logan$(SUBFIX) ${BINDIR}/ni_rsrc_list_logan$(SUBFIX) ${BINDIR}/init_rsrc_logan$(SUBFIX)
ifneq ($(SHAREDDIR),)
	rm -f ${SHAREDDIR}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${SHAREDDIR}/${TARGET_LIB_SHARED} 
endif

cleanall:clean

clean:
	rm -rf $(BIN_PATH)/*${TARGET}* $(OBJS_PATH)/*.o $(OBJS_PATH)/*.h $(BIN_PATH)/*$(SUBFIX) $(BIN_PATH)/include $(OBJS_PATH)/*.gcno $(OBJS_PATH)/*.gcda

# dependence
%.o : ${SRC_PATH}/%.cpp
	${CC} ${CFLAGS} ${LIBXCODER_CUSTOM_PARAMETERS} ${CPPFLAGS} ${INCLUDES} -c $< -o ${OBJS_PATH}/$@

%.o : ${SRC_PATH}/%.c
	${CC} ${CFLAGS} ${LIBXCODER_CUSTOM_PARAMETERS} ${INCLUDES} -c $< -o ${OBJS_PATH}/$@
