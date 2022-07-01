.PHONY: all default test clean cleanall install uninstall
WINDOWS ?= FALSE
GDB ?= FALSE
SHARED ?= FALSE

INSTALL = install

ifeq ($(GDB), TRUE)
	OPTFLAG = -g
else
	OPTFLAG = -O3
endif

CFLAGS = -fPIC -DLIBXCODER_OBJS_BUILD
CPPFLAGS = -D__STDC_FORMAT_MACROS
TARGETNAME = xcoder
TARGET = ${TARGETNAME}
TARGET_LIB = lib${TARGETNAME}.a
ifeq ($(WINDOWS), FALSE)
TARGET_LIB_SHARED = lib${TARGETNAME}.so
TARGET_VERSION = $(shell grep 'Version: '.* < build/xcoder.pc  | cut -d ' ' -f 2)
else
TARGET_LIB_SHARED = lib${TARGETNAME}.dll
endif
TARGET_INC = ni_device_api.h
TARGET_INC2 = ni_rsrc_api.h
TARGET_INC3 = ni_defs.h
TARGET_INC4 = ni_util.h
TARGET_PC = xcoder.pc
OBJECTS =  ni_nvme.o ni_device_api_priv.o ni_device_api.o ni_util.o ni_rsrc_priv.o ni_rsrc_api.o
LINK_OBJECTS =  ${OBJS_PATH}/ni_nvme.o ${OBJS_PATH}/ni_device_api_priv.o ${OBJS_PATH}/ni_device_api.o ${OBJS_PATH}/ni_util.o ${OBJS_PATH}/ni_rsrc_priv.o ${OBJS_PATH}/ni_rsrc_api.o
ALL_OBJECTS = ni_device_test.o init_rsrc.o test_rsrc_api.o ni_rsrc_mon.o ni_rsrc_update.o ni_rsrc_list.o ${OBJECTS}
SRC_PATH = ./source
OBJS_PATH = ./build

ifeq ($(CUSTOM_DRIVER), TRUE)
    CFLAGS += -DXCODER_LINUX_CUSTOM_DRIVER
endif

# Read the installation directory from path set in build/xcoder.pc
DESTDIR ?= $(shell sed -n 's/^prefix=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
LIBDIR = $(shell sed -n 's/^libdir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
BINDIR = $(shell sed -n 's/^bindir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
INCLUDEDIR = $(shell sed -n 's/^includedir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
SHAREDDIR = $(shell sed -n 's/^shareddir=\(.*\)/\1/p' $(OBJS_PATH)/$(TARGET_PC))
ifeq ($(WINDOWS), FALSE)
	INCLUDES= -pthread -lrt ${OPTFLAG} -lstdc++
else
	#CC      = x86_64-w64-mingw32-gcc
	INCLUDES= ${OPTFLAG} -I./MSVS2019/nvme-1.5/source/ -I./MSVS2019/ni_rsrc_mon/XGetOpt/
	TARGET_INC_WIN1 = nvme.h 
	TARGET_INC_WIN2 = nvmeIoctl.h
	TARGET_INC_WIN3 = nvmeReg.h
	TARGET_INC_WIN4 = ni_util.h
endif

# make target

all:${ALL_OBJECTS}
	ar rcs $(OBJS_PATH)/${TARGET_LIB} ${LINK_OBJECTS}

ifeq ($(SHARED), TRUE)
ifeq ($(WINDOWS), FALSE)
	${CC} -shared -Wl,-soname,${TARGET_LIB_SHARED}.${TARGET_VERSION} -o $(OBJS_PATH)/${TARGET_LIB_SHARED}.${TARGET_VERSION} $(LINK_OBJECTS) ${INCLUDES}
else
	${CC} -o $(OBJS_PATH)/${TARGET_LIB_SHARED} -shared -Wl,--out-implib,$(OBJS_PATH)/${TARGET_LIB_SHARED}.a $(LINK_OBJECTS) ${INCLUDES}
endif
endif

	${CC} -o $(OBJS_PATH)/${TARGET} $(OBJS_PATH)/ni_device_test.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} -o $(OBJS_PATH)/test_rsrc_api $(OBJS_PATH)/test_rsrc_api.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} -o $(OBJS_PATH)/ni_rsrc_mon $(OBJS_PATH)/ni_rsrc_mon.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} -o $(OBJS_PATH)/ni_rsrc_update $(OBJS_PATH)/ni_rsrc_update.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} -o $(OBJS_PATH)/ni_rsrc_list $(OBJS_PATH)/ni_rsrc_list.o $(LINK_OBJECTS) ${INCLUDES}
	${CC} -o $(OBJS_PATH)/init_rsrc $(OBJS_PATH)/init_rsrc.o $(LINK_OBJECTS) ${INCLUDES}
	@echo info ${TARGET_LIB_SHARED}
	
test:${ALL_OBJECTS}
	${CC} -o $(OBJS_PATH)/${TARGET} $(OBJS_PATH)/*.o ${INCLUDES}
	
install:
	mkdir -p ${INCLUDEDIR}/
	mkdir -p ${LIBDIR}/
	mkdir -p ${LIBDIR}/pkgconfig/
	mkdir -p ${BINDIR}/
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC2} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC3} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC4} ${INCLUDEDIR}/.
ifeq ($(WINDOWS), TRUE)
	${INSTALL} -m 644 ./MSVS2019/nvme-1.5/source/${TARGET_INC_WIN1} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ./MSVS2019/nvme-1.5/source/${TARGET_INC_WIN2} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ./MSVS2019/nvme-1.5/source/${TARGET_INC_WIN3} ${INCLUDEDIR}/.
	${INSTALL} -m 644 ${SRC_PATH}/${TARGET_INC_WIN4} ${INCLUDEDIR}/.
endif
	${INSTALL} -m 755 ${OBJS_PATH}/${TARGET_LIB} ${LIBDIR}/.
ifeq ($(SHARED), TRUE)
ifeq ($(WINDOWS), FALSE)
	${INSTALL} -m 755 ${OBJS_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/.
	(cd ${LIBDIR}; ln -f -s ${TARGET_LIB_SHARED}.${TARGET_VERSION} ${TARGET_LIB_SHARED}; cd -)
else
	${INSTALL} -m 755 ${OBJS_PATH}/${TARGET_LIB_SHARED} ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/${TARGET_LIB_SHARED}.a ${LIBDIR}/.
endif
ifneq ($(SHAREDDIR),)
	mkdir -p ${SHAREDDIR}/
	${INSTALL} -m 644 ${OBJS_PATH}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${SHAREDDIR}/.
	(cd ${SHAREDDIR}; ln -f -s ${TARGET_LIB_SHARED}.${TARGET_VERSION} ${TARGET_LIB_SHARED}; cd -)
endif
endif
	${INSTALL} -m 644 ${OBJS_PATH}/${TARGET_PC} ${LIBDIR}/pkgconfig/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_mon ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_update ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/ni_rsrc_list ${BINDIR}/.
	${INSTALL} -m 755 ${OBJS_PATH}/init_rsrc ${BINDIR}/.

uninstall:
	rm -f ${INCLUDEDIR}/${TARGET_INC} ${INCLUDEDIR}/${TARGET_INC2} ${INCLUDEDIR}/${TARGET_INC3} ${INCLUDEDIR}/${TARGET_INC4} ${LIBDIR}/${TARGET_LIB} ${LIBDIR}/${TARGET_LIB_SHARED} ${LIBDIR}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${LIBDIR}/${TARGET_LIB_SHARED}.a ${LIBDIR}/pkgconfig/${TARGET_PC} ${BINDIR}/${TARGET_LIB_SHARED} ${BINDIR}/ni_rsrc_mon ${BINDIR}/ni_rsrc_update ${BINDIR}/ni_rsrc_list ${BINDIR}/init_rsrc
ifneq ($(SHAREDDIR),)
	rm -f ${SHAREDDIR}/${TARGET_LIB_SHARED}.${TARGET_VERSION} ${SHAREDDIR}/${TARGET_LIB_SHARED} 
endif

cleanall:clean
	rm -rf ${TARGET} $(OBJS_PATH)/*${TARGET}* $(OBJS_PATH)/*.o

clean:
	rm -rf ${TARGET} $(OBJS_PATH)/*${TARGET}* $(OBJS_PATH)/*.o

# dependence
%.o : ${SRC_PATH}/%.cpp
	${CC} ${CFLAGS} ${CPPFLAGS} ${INCLUDES} -c $< -o ${OBJS_PATH}/$@

%.o : ${SRC_PATH}/%.c
	${CC} ${CFLAGS} ${INCLUDES} -c $< -o ${OBJS_PATH}/$@
