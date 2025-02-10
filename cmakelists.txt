#
# Copyright (C) 2025 AndrÃ© "Wolf3s" Guilherme <andregui17@outlook.com>
# Licenced under Academic Free License version 3.0
# Review w-Open-PS2-Loader README & LICENSE files for further details.
#

cmake_minimum_required(VERSION 3.5)

if(NOT EXISTS ${PS2SDK}  OR NOT EXISTS ${PS2SDK}/ps2dev.cmake)
    message(FATAL_ERROR "PS2SDK or Cmake is not setup. Please setup PS2SDK before building this project")
endif()

set(WOPL_MAJOR_VERSION 0)
set(WOPL_MINOR_VERSION 0)
set(WOPL_PATCHLEVEL 0)
set(WOPL_EXTRAVERSION "WIP")

# How to DEBUG?
# Simply type "cmake -S . -D<debug mode>=ON" to build wOPL with the necessary debugging functionality.
# Debug modes:
#	DEBUG		    	 -	UI-side debug mode (UDPTTY)
#	IOPCORE_DEBUG		 -	UI-side + iopcore debug mode (UDPTTY).
#	INGAME_DEBUG		 -	UI-side + in-game debug mode. IOP core modules will not be built as debug versions (UDPTTY).
#	PPCTTY		         -	UI-side debug mode (PowerPC UART) Requires debug mode enabled.
#	iopcore_ppctty_debug -	UI-side + iopcore debug mode (PowerPC UART).
#	ingame_ppctty_debug	 -	UI-side + in-game debug mode. IOP core modules will not be built as debug versions (PowerPC UART).
#	eesio_debug			 -	UI-side + eecore debug mode (EE SIO)
#	DECI2_DEBUG			 -	UI-side + in-game DECI2 debug mode (EE-side only).

# I want to put my name in my custom build! How can I do it?
# Type "make LOCALVERSION=-foobar"

# ======== START OF CONFIGURABLE SECTION ========
# You can adjust the variables in this section to meet your needs.
# To enable a feature, set its variable's value to 1. To disable, change it to 0.
# Do not COMMENT out the variables!!
# You can also specify variables when executing make: "make RTL=1 IGS=1 PADEMU=1"

option(EXTRA_FEATURES "Check if EXTRA_FEATURES is set, default to OFF" OFF)

# Set RTL and IGS based on EXTRA_FEATURES, but allow user overrides
if(EXTRA_FEATURES)
    option(RTL "Enables/disables Right-To-Left (RTL) language support" ON)
    option(IGS "Enables/disables In Game Screenshot (IGS). NB: It depends on GSM and IGR to work" ON)
    message(STATUS "EXTRA_FEATURES: Activated")
else()
    set(RTL OFF CACHE BOOL "Enable Right-To-Left (RTL) language support" FORCE)
    set(IGS OFF CACHE BOOL "Enable In Game Screenshot (IGS). NB: Depends on GSM and IGR" FORCE)
    message(STATUS "EXTRA_FEATURES: Deactivated")
endif()

option(PADEMU "Enables/disables pad emulator" ON)

option(DTL_T10000 "Enables/disables building of an edition of wOPL that will support the DTL-T10000 (SDK v2.3+)" OFF)

option(NOT_PACKED "Nor stripping neither compressing binary ELF after compiling." OFF)

set(PC_TOOLS "WIN32" CACHE STRING "Enable pc tool building. Choose between: WIN32 or UNIX")
set_property(CACHE TTY_APPROACH PROPERTY STRINGS "WIN32" "UNIX")

# ======== END OF CONFIGURABLE SECTION. DO NOT MODIFY VARIABLES AFTER THIS POINT!! ========
option(DEBUG "Enable debug build" OFF)
if(DEBUG)
    option(EESIO_DEBUG "Enable EE sio debug build" OFF)
    option(INGAME_DEBUG "Enable in game debug build" OFF)
    option(DECI2_DEBUG "Enbale DECI2 debug build" OFF)
    if(NOT DECI2_DEBUG)
        set(TTY_APPROACH "UDP" CACHE STRING "Enable TTY approach. Choose between: UDP or PPC_UART")
        set_property(CACHE TTY_APPROACH PROPERTY STRINGS "UDP" "PPC_UART")
    endif()
    option(IOPCORE_DEBUG "Enbale IOPCORE debug build" OFF)
endif()

# ======== DO NOT MODIFY VALUES AFTER THIS POINT! UNLESS YOU KNOW WHAT YOU ARE DOING ========
set(PROJECT_VERSION_STRING "${WOPL_MAJOR_VERSION}.${WOPL_MINOR_VERSION}.${WOPL_PATCHLEVEL}")
if (WOPL_EXTRAVERSION)
    set(PROJECT_VERSION_STRING "${PROJECT_VERSION_STRING}-${WOPL_EXTRAVERSION}")
endif()

project(w-Open-PS2-Loader
    LANGUAGES C ASM
    VERSION ${WOPL_MAJOR_VERSION}.${WOPL_MINOR_VERSION}.${WOPL_PATCHLEVEL}
)

execute_process(COMMAND git rev-list --count HEAD
                OUTPUT_VARIABLE COMMIT_COUNTS
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
math(EXPR REVISION "${COMMIT_COUNTS} - 2195" OUTPUT_FORMAT DECIMAL)

execute_process(COMMAND git rev-parse --short=7 HEAD
                OUTPUT_VARIABLE GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)

execute_process(COMMAND git diff --quiet
                RESULT_VARIABLE DIRTY_VARIABLE)
                
if(DIRTY_VARIABLE EQUAL 1)
    set(DIRTY "-dirty")
else()
    set(DIRTY "")
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.git")
    set(DIRTY "-dirty")
endif()

execute_process(COMMAND git describe --exact-match --tags
                OUTPUT_VARIABLE GIT_TAG
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)

set(WOPL_VERSION "${PROJECT_VERSION_STRING}")

if(REVISION)
    string(APPEND WOPL_VERSION "-${REVISION}")
endif()
if(GIT_HASH)
    string(APPEND WOPL_VERSION "-${GIT_HASH}")
endif()
if(DIRTY)
    string(APPEND WOPL_VERSION "${DIRTY}")
endif()
if(LOCALVERSION)
    string(APPEND WOPL_VERSION "-${LOCALVERSION}")
endif()

if(GIT_TAG AND NOT GIT_TAG STREQUAL "latest")
    set(WOPL_VERSION "${GIT_TAG}${DIRTY}")
endif()

message(STATUS "WOPL_VERSION: ${WOPL_VERSION}")

# WARNING: Only extra spaces are allowed and ignored at the beginning of the conditional directives (ifeq, ifneq, ifdef, ifndef, else and endif)
# but a tab is not allowed; if the line begins with a tab, it will be considered part of a recipe for a rule!

if(RTL)
  set(RTL_CFLAGS -D__RTL)
endif()

if(DTL_T10000)
  set(DTL_CFLAGS -D_DTL_T10000)
  set(UDNL_OUT $(PS2SDK)/iop/irx/udnl-t300.irx)
else()
  set(UDNL_OUT $(PS2SDK)/iop/irx/udnl.irx)
endif()

if(IGS)
  set(IGS_CFLAGS -DIGS)
endif()

if(PADEMU)
  set(PADEMU_OBJS bt_pademu.o usb_pademu.o ds34usb.o ds34bt.o)
  set(PADEMU_CFLAGS -DPADEMU)
  set(PADEMU_INCS -Imodules/ds34bt/ee -Imodules/ds34usb/ee)
  set(PADEMU_LIBS libds34usb.a libds34bt.a)
endif()

if(DEBUG)
  set(APPEND DEBUG_CFLAGS "-D__DEBUG" -g)
  set(DEBUG_OBJS "")
  set(DEBUG_LDFLAGS "")
  set(SMSTCPIP_INGAME_CFLAGS "")
  set(DEBUG_LIBS "")
  if (DECI2_DEBUG)
    list(APPEND DEBUG_OBJS debug.o drvtif_irx.o tifinet_irx.o deci2_img.o)
    list(APPEND DEBUG_LIBS iopreboot)
  elseif (TTY_APPROACH STREQUAL "UDP")
    list(APPEND DEBUG_OBJS debug.o udptty.o ioptrap.o ps2link.o)
    list(APPEND DEBUG_CFLAGS "-DTTY_UDP")
  elseif (TTY_APPROACH STREQUAL "PPC_UART") 
    list(APPEND DEBUG_OBJS debug.o ppctty.o ioptrap.o)
    list(APPEND DEBUG_CFLAGS "-DTTY_PPC_UART")
  else()
    message(FATAL_ERROR "Unknown value for TTY_APPROACH: ${TTY_APPROACH}
      choose UDP or PPC_UART approaches.")
  endif()
  set(MOD_DEBUG_FLAGS DEBUG=1)
  if (IOPCORE_DEBUG)
    list(APPEND DEBUG_CFLAGS "-D__INGAME_DEBUG")
    list(APPEND EECORE_EXTRA_FLAGS LOAD_DEBUG_MODULES=1)
    lisr(APPEND SMSTCPIP_INGAME_CFLAGS "") 
    if (TTY_APPROACH STREQUAL "UDP")
      list(APPEND DEBUG_OBJS udptty-ingame.o)
    endif()
  elseif(EESIO_DEBUG)
    list(APPEND DEBUG_CFLAGS "-D__EESIO_DEBUG")
    list(APPEND DEBUG_LIBS siocookie)
  elseif (INGAME_DEBUG)
    list(APPEND DEBUG_CFLAGS "-D__INGAME_DEBUG")
    list(APPEND EECORE_EXTRA_FLAGS LOAD_DEBUG_MODULES=1)
    list(APPEND CDVDMAN_DEBUG_FLAGS IOPCORE_DEBUG=1)
    list(APPEND SMSTCPIP_INGAME_CFLAGS "")
    if(DECI2_DEBUG)
      list(APPEND DEBUG_CFLAGS "-D__DECI2_DEBUG")
      list(APPEND EECORE_EXTRA_FLAGS += DECI2_DEBUG=1)
      list(APPEND DEBUG_OBJS drvtif_ingame_irx.o tifinet_ingame_irx.o)
      list(APPEND DECI2_DEBUG=1)
      list(APPEND CDVDMAN_DEBUG_FLAGS USE_DEV9=1) #(clear IOPCORE_DEBUG) dsidb cannot be used to handle exceptions or set breakpoints, so disable output to save resources.
    elseif(TTY_APPROACH STREQUAL "UDP")
      list(APPEND DEBUG_OBJS udptty-ingame.o)
      list(APPEND EECORE_EXTRA_FLAGS "TTY_APPROACH=${TTY_APPROACH}")
    elseif(TTY_APPROACH STREQUAL "PPC_UART")
      list(APPEND EECORE_EXTRA_FLAGS "TTY_APPROACH=${TTY_APPROACH}")
    endif()
  endif()
else()
  set(EE_CFLAGS -O2)
  set(SMSTCPIP_INGAME_CFLAGS INGAME_DRIVER=1)
endif()

set(FRONTEND_OBJS src/appsupport.c src/atlas.c src/bdmsupport.c src/cheatman.c src/debug.c src/dia.c src/dialogs.c src/ethsupport.c src/fntsys.c src/gsm.c src/gui.c src/guigame.c src/hdd.c
src/hddsupport.c src/httpclient.c src/ioman.c src/ioprp.c src/lang_internal.c src/lang.c src/lz4.c src/menusys.c src/nbns.c src/opl.c src/OSDHistory.c src/pad.c src/ps2cnf.c src/renderman.c
src/system.c src/texcache.c src/textures.c src/themes.c src/util.c src/xparam.c src/zso.c)

set(IOP_OBJS iomanx.o filexio.o ps2fs.o usbd.o bdmevent.o 
		bdm.o bdmfs_fatfs.o usbmass_bd.o iLinkman.o IEEE1394_bd.o mx4sio_bd.o 
		ps2atad.o hdpro_atad.o poweroff.o ps2hdd.o xhdd.o genvmc.o lwnbdsvr.o 
		ps2dev9.o smsutils.o ps2ip.o smap.o isofs.o nbns-iop.o 
		sio2man.o padman.o mcman.o mcserv.o 
		httpclient-iop.o netman.o ps2ips.o 
		bdm_mcemu.o hdd_mcemu.o smb_mcemu.o 
		iremsndpatch.o apemodpatch.o f2techioppatch.o cleareffects.o resetspu.o 
		libsd.o audsrv.o)

set(EECORE_OBJS = ee_core.o ioprp.o util.o 
	udnl.o imgdrv.o eesync.o 
	bdm_cdvdman.o bdm_ata_cdvdman.o IOPRP_img.o smb_cdvdman.o 
	hdd_cdvdman.o hdd_hdpro_cdvdman.o cdvdfsv.o 
	ingame_smstcpip.o smap_ingame.o smbman.o smbinit.o)

set(PNG_ASSETS load0 load1 load2 load3 load4 load5 load6 load7 usb usb_bd ilk_bd 
	m4s_bd hdd_bd hdd eth app cross triangle circle square select start left right 
	background info cover disc screen ELF HDL ISO ZSO UL APPS CD DVD Aspect_s Aspect_w Aspect_w1 
	Aspect_w2 Device_1 Device_2 Device_3 Device_4 Device_5 Device_6 Device_all Rating_0 
	Rating_1 Rating_2 Rating_3 Rating_4 Rating_5 Scan_240p Scan_240p1 Scan_480i Scan_480p 
	Scan_480p1 Scan_480p2 Scan_480p3 Scan_480p4 Scan_480p5 Scan_576i Scan_576p Scan_720p 
	Scan_1080i Scan_1080i2 Scan_1080p Vmode_multi Vmode_ntsc Vmode_pal logo case apps_case
	Index_0 Index_1 Index_2 Index_3 Index_4)
	# unused icons - up down l1 l2 l3 r1 r2 r3

set(GFX_OBJS $(PNG_ASSETS:%=%_png.o) poeveticanew.o icon_sys.o icon_icn.o)

set(AUDIO_OBJS bd_connect.o bd_disconnect.o boot.o cancel.o confirm.o cursor.o message.o transition.o)

set(MISC_OBJS conf_theme_wOPL.o icon_sys_A.o icon_sys_C.o icon_sys_J.o)

set(TRANSLATIONS Albanian Arabic Bulgarian Cebuano Croatian Czech Danish Dutch Filipino French 
	German Greek Hungarian Indonesian Italian Japanese Korean Laotian Persian Polish Portuguese 
	Portuguese_BR Romana Russian Ryukyuan SChinese Spanish Swedish TChinese Turkish Vietnamese)

#########################################################################

set(EE_BIN wopl.elf)
set(EE_BIN_STRIPPED wopl_stripped.elf)
set(EE_BIN_PACKED WOPNPS2LD.ELF)
set(EE_VPKD WOPNPS2LD-${WOPL_VERSION})
set(EE_VPKD_ZIP ${EE_VPKD}.zip)
set(EE_SRC_DIR src/)
set(EE_OBJS_DIR "obj/")
set(EE_ASM_DIR = "asm/")
set(LNG_SRC_DIR lng_src/)
set(LNG_TMPL_DIR lng_tmpl/)
set(LNG_DIR lng/)
set(PNG_ASSETS_DIR gfx/)

set(MAPFILE wopl.map)
list(APPEND EE_LDFLAGS "-Wl,-Map,{MAPFILE}")

find_package(ZLIB)
find_package(Ogg)
find_package(Vorbis)
find_package(PNG)
find_package(Freetype)

execute_process(COMMAND ./make_changelog.sh)

add_executable(wopl.elf ${FRONTEND_OBJS})
add_custom_target(stripped 
  COMMENT "Stripping..."
  COMMAND ${EE_STRIP} -o ${EE_BIN_STRIPPED} ${EE_BIN}
  DEPENDS wopl.elf)

add_custom_target(packed 
  COMMENT "Compressing..."
  COMMAND ps2-packer ${EE_BIN_STRIPPED} ${EE_BIN_PACKED} > /dev/null
  DEPENDS stripped)

add_custom_target(VPKD
  COMMAND cp -f ${EE_BIN_PACKED} ${EE_VPKD}
  DEPENDS packed)

add_custom_target(
  VPKD_ZIP ALL
  COMMENT "Creating ZIP package: ${EE_VPKD_ZIP}..."  
  COMMAND zip -r ${EE_VPKD_ZIP} ${EE_BIN_PACKED} DETAILED_CHANGELOG CREDITS LICENSE README.md
  COMMAND echo "Package complete: ${EE_VPKD_ZIP}"
  DEPENDS VPKD
  VERBATIM )

target_link_libraries(wopl.elf PRIVATE $(DEBUG_LIBS) gskit dmakit poweroff fileXio patches mc vux cdvd netman ps2ips audsrv padx elf-loader-nocolour)

list(APPEND EE_CFLAGS -fsingle-precision-constant "-DWOPL_VERSION=\"${WOPL_VERSION}\"")

# There are a few places where the config key/value are truncated, so disable these warnings
list(APPEND EE_CFLAGS += -Wno-format-truncation -Wno-stringop-truncation)
# Generate .d files to track header file dependencies of each object file
list(APPEND EE_CFLAGS += -MMD -MP)

list(TRANSFORM EE_OBJS PREPEND ${EE_OBJS_DIR})

foreach(obj ${EE_OBJS})
    list(APPEND PREPENDED_EE_OBJS "${EE_OBJS_DIR}${obj}")
endforeach()
set(EE_OBJS ${PREPENDED_EE_OBJS})

set(EE_DEPS)
foreach(obj ${EE_OBJS})
    string(REGEX REPLACE "\\.o$" ".d" dep_file ${obj})
    list(APPEND EE_DEPS ${dep_file})
endforeach()

list(APPEND EE_OBJS 
    ${FRONTEND_OBJS} 
    ${GFX_OBJS} 
    ${AUDIO_OBJS} 
    ${MISC_OBJS} 
    ${EECORE_OBJS} 
    ${IOP_OBJS}
)

# To help linking getting rid off unused functions and data
list(APPEND EE_CFLAGS -fdata-sections)
list(APPEND EE_LDFLAGS $(DEBUG_LDFLAGS) -fdata-sections -ffunction-sections -Wl,--gc-sections)

add_compile_definitions(${RTL_CFLAGS} ${DTL_CFLAGS} ${IGS_CFLAGS} ${PADEMU_CFLAGS})
set(CMAKE_C_FLAGS "${EE_CFLAGS}")

set(CMAKE_LD_FLAGS "${EE_LDFLAGS}")

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${GSKIT}/include ${GSKIT}/ee/dma/include ${GSKIT}/ee/gs/include modules/iopcore/common modules/network/common modules/hdd/common include)
add_compile_options("${CMAKE_C_FLAGS}")

if(PC_TOOLS STREQUAL "WIN32" OR PC_TOOLS STREQUAL "UNIX")
    add_custom_target(pc_tools
        COMMAND ${CMAKE_COMMAND} -E echo "Building iso2opl, opl2iso, and genvmc for ${PC_TOOLS}..."
        COMMAND ${CMAKE_MAKE_PROGRAM} -C pc _WIN32=$<IF:$<STREQUAL:${PC_TOOLS},WIN32>,0,1>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/pc
        COMMENT "Building PC tools for ${PC_TOOLS}..."
        VERBATIM
    )
else()
 message(FATAL_ERROR "Only WIN32 or UNIX are available, please type correctly
 or port to another platform")
endif()

set(BIN2C = ${PS2SDK}/bin/bin2c)

function(import_bin2c irx)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${irx}_irx.c
        COMMAND ${BIN2C} ${irx} ${CMAKE_CURRENT_BINARY_DIR}/${irx}_irx.c ${irx}_irx
        MAIN_DEPENDENCY ${irx}
        COMMENT "Converting ${irx} to a C header using bin2c..."
        VERBATIM
    )
endfunction()

import_bin2c(eesync.c)
