#
# This file was taken from RakNet 4.082.
# Please see licenses/RakNet license.txt for the underlying license and related copyright.
#
#
# Modified work: Copyright (c) 2017, SLikeSoft UG (haftungsbeschränkt)
#
# This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
# license found in the license.txt file in the root directory of this source tree.
#

project(AutopatcherMySQLRepository)
FINDMYSQL()
IF(WIN32 AND NOT UNIX)
	FILE(GLOB ALL_HEADER_SRCS *.h ${MySQLInterface_SOURCE_DIR}/MySQLInterFace.h ${Autopatcher_SOURCE_DIR}/ApplyPatch.h)
	FILE(GLOB ALL_CPP_SRCS *.cpp ${MySQLInterface_SOURCE_DIR}/MySQLInterFace.cpp ${Autopatcher_SOURCE_DIR}/ApplyPatch.cpp)
	include_directories(${RAKNETHEADERFILES} ./ ${MySQLInterface_SOURCE_DIR} ${Autopatcher_SOURCE_DIR} ${MYSQL_INCLUDE_DIR} ${BZip2_SOURCE_DIR}) 
	add_library(AutoPatcherMySQLRepository STATIC ${ALL_CPP_SRCS} ${ALL_HEADER_SRCS})
	target_link_libraries (AutoPatcherMySQLRepository ${RAKNET_COMMON_LIBS} ${MYSQL_LIBRARIES})
	VSUBFOLDER(AutoPatcherMySQLRepository "Samples/AutoPatcher/Server/MySQL")
ELSE(WIN32 AND NOT UNIX)
	FILE(GLOB ALL_HEADER_SRCS *.h)
	FILE(GLOB ALL_CPP_SRCS *.cpp)
	include_directories(${RAKNETHEADERFILES} ./ ${MySQLInterface_SOURCE_DIR} ${Autopatcher_SOURCE_DIR} ${MYSQL_INCLUDE_DIR}) 
	add_library(AutoPatcherMySQLRepository STATIC ${ALL_CPP_SRCS} ${ALL_HEADER_SRCS})
	target_link_libraries (AutoPatcherMySQLRepository ${RAKNET_COMMON_LIBS} LibAutopatcher LibMySQLInterface ${MYSQL_LIBRARIES})
ENDIF(WIN32 AND NOT UNIX)
