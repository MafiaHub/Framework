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

project(AutopatcherPostgreRepository)
FINDPOSTGRE()
IF(WIN32 AND NOT UNIX)
	FILE(GLOB ALL_HEADER_SRCS *.h ${PostgreSQLInterface_SOURCE_DIR}/PostgreSQLInterface.h ${Autopatcher_SOURCE_DIR}/ApplyPatch.h ${Autopatcher_SOURCE_DIR}/CreatePatch.h)
	FILE(GLOB ALL_CPP_SRCS *.cpp ${PostgreSQLInterface_SOURCE_DIR}/PostgreSQLInterface.cpp ${Autopatcher_SOURCE_DIR}/ApplyPatch.cpp ${Autopatcher_SOURCE_DIR}/CreatePatch.cpp)
	include_directories(${RAKNETHEADERFILES} ./ ${PostgreSQLInterface_SOURCE_DIR} ${Autopatcher_SOURCE_DIR} ${POSTGRESQL_INCLUDE_DIR} ${BZip2_SOURCE_DIR}) 
	add_library(AutopatcherPostgreRepository STATIC ${ALL_CPP_SRCS} ${ALL_HEADER_SRCS})
	target_link_libraries (AutopatcherPostgreRepository ${RAKNET_COMMON_LIBS} ${POSTGRESQL_LIBRARIES})
	VSUBFOLDER(AutopatcherPostgreRepository "Samples/AutoPatcher/Server/PostgreSQL")
ELSE(WIN32 AND NOT UNIX)
	FILE(GLOB ALL_HEADER_SRCS *.h)
	FILE(GLOB ALL_CPP_SRCS *.cpp)
	include_directories(${RAKNETHEADERFILES} ./ ${PostgreSQLInterface_SOURCE_DIR} ${Autopatcher_SOURCE_DIR} ${POSTGRESQL_INCLUDE_DIR}) 
	add_library(AutopatcherPostgreRepository STATIC ${ALL_CPP_SRCS} ${ALL_HEADER_SRCS})
	target_link_libraries (AutopatcherPostgreRepository ${RAKNET_COMMON_LIBS} LibAutopatcher LibPostgreSQLInterface ${POSTGRESQL_LIBRARIES})
ENDIF(WIN32 AND NOT UNIX)
