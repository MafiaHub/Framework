# MafiaNet Library
#
# Copyright (c) 2024, MafiaHub
# Licensed under MIT-style license

# Source files (explicit list, no GLOB)
set(MAFIANET_SOURCES
    src/_FindFirst.cpp
    src/Base64Encoder.cpp
    src/BitStream.cpp
    src/CCRakNetSlidingWindow.cpp
    src/CCRakNetUDT.cpp
    src/CheckSum.cpp
    src/CloudClient.cpp
    src/CloudCommon.cpp
    src/CloudServer.cpp
    src/CommandParserInterface.cpp
    src/ConnectionGraph2.cpp
    src/ConsoleServer.cpp
    src/DataCompressor.cpp
    src/DirectoryDeltaTransfer.cpp
    src/DR_SHA1.cpp
    src/DS_BytePool.cpp
    src/DS_ByteQueue.cpp
    src/DS_HuffmanEncodingTree.cpp
    src/DS_Table.cpp
    src/DynDNS.cpp
    src/EmailSender.cpp
    src/EpochTimeToString.cpp
    src/FileList.cpp
    src/FileListTransfer.cpp
    src/FileOperations.cpp
    src/FormatString.cpp
    src/FullyConnectedMesh2.cpp
    src/Getche.cpp
    src/Gets.cpp
    src/GetTime.cpp
    src/gettimeofday.cpp
    src/GridSectorizer.cpp
    src/HTTPConnection.cpp
    src/HTTPConnection2.cpp
    src/IncrementalReadInterface.cpp
    src/Itoa.cpp
    src/linux_adapter.cpp
    src/LinuxStrings.cpp
    src/LocklessTypes.cpp
    src/LogCommandParser.cpp
    src/MessageFilter.cpp
    src/NatPunchthroughClient.cpp
    src/NatPunchthroughServer.cpp
    src/NatTypeDetectionClient.cpp
    src/NatTypeDetectionCommon.cpp
    src/NatTypeDetectionServer.cpp
    src/NetworkIDManager.cpp
    src/NetworkIDObject.cpp
    src/osx_adapter.cpp
    src/PacketConsoleLogger.cpp
    src/PacketFileLogger.cpp
    src/PacketizedTCP.cpp
    src/PacketLogger.cpp
    src/PacketOutputWindowLogger.cpp
    src/PluginInterface2.cpp
    src/PS4Includes.cpp
    src/Rackspace.cpp
    src/RakMemoryOverride.cpp
    src/RakNetCommandParser.cpp
    src/RakNetSocket.cpp
    src/RakNetSocket2.cpp
    src/RakNetSocket2_Berkley.cpp
    src/RakNetSocket2_Windows_Linux.cpp
    src/RakNetSocket2_Windows_Linux_360.cpp
    src/RakNetStatistics.cpp
    src/RakNetTransport2.cpp
    src/RakNetTypes.cpp
    src/RakPeer.cpp
    src/RakSleep.cpp
    src/RakString.cpp
    src/RakThread.cpp
    src/RakWString.cpp
    src/Rand.cpp
    src/RandSync.cpp
    src/ReadyEvent.cpp
    src/RelayPlugin.cpp
    src/ReliabilityLayer.cpp
    src/ReplicaManager3.cpp
    src/Router2.cpp
    src/RPC4Plugin.cpp
    src/SecureHandshake.cpp
    src/SendToThread.cpp
    src/SignaledEvent.cpp
    src/SimpleMutex.cpp
    src/SocketLayer.cpp
    src/StatisticsHistory.cpp
    src/StringCompressor.cpp
    src/StringTable.cpp
    src/SuperFastHash.cpp
    src/TableSerializer.cpp
    src/TCPInterface.cpp
    src/TeamBalancer.cpp
    src/TeamManager.cpp
    src/TelnetTransport.cpp
    src/ThreadsafePacketLogger.cpp
    src/TwoWayAuthentication.cpp
    src/UDPForwarder.cpp
    src/UDPProxyClient.cpp
    src/UDPProxyCoordinator.cpp
    src/UDPProxyServer.cpp
    src/VariableDeltaSerializer.cpp
    src/VariableListDeltaTracker.cpp
    src/VariadicSQLParser.cpp
    src/VitaIncludes.cpp
    src/WSAStartupSingleton.cpp
)

set(MAFIANET_CRYPTO_SOURCES
    src/crypto/cryptomanager.cpp
    src/crypto/factory.cpp
    src/crypto/fileencrypter.cpp
    src/crypto/securestring.cpp
)

set(MAFIANET_HEADERS
    include/mafianet/_FindFirst.h
    include/mafianet/alloca.h
    include/mafianet/assert.h
    include/mafianet/AutopatcherPatchContext.h
    include/mafianet/AutopatcherRepositoryInterface.h
    include/mafianet/Base64Encoder.h
    include/mafianet/BitStream.h
    include/mafianet/CCRakNetSlidingWindow.h
    include/mafianet/CCRakNetUDT.h
    include/mafianet/CheckSum.h
    include/mafianet/CloudClient.h
    include/mafianet/CloudCommon.h
    include/mafianet/CloudServer.h
    include/mafianet/commandparser.h
    include/mafianet/CommandParserInterface.h
    include/mafianet/ConnectionGraph2.h
    include/mafianet/ConsoleServer.h
    include/mafianet/DataCompressor.h
    include/mafianet/defineoverrides.h
    include/mafianet/defines.h
    include/mafianet/DirectoryDeltaTransfer.h
    include/mafianet/DR_SHA1.h
    include/mafianet/DS_BinarySearchTree.h
    include/mafianet/DS_BPlusTree.h
    include/mafianet/DS_BytePool.h
    include/mafianet/DS_ByteQueue.h
    include/mafianet/DS_Hash.h
    include/mafianet/DS_Heap.h
    include/mafianet/DS_HuffmanEncodingTree.h
    include/mafianet/DS_HuffmanEncodingTreeFactory.h
    include/mafianet/DS_HuffmanEncodingTreeNode.h
    include/mafianet/DS_LinkedList.h
    include/mafianet/DS_List.h
    include/mafianet/DS_Map.h
    include/mafianet/DS_MemoryPool.h
    include/mafianet/DS_Multilist.h
    include/mafianet/DS_OrderedChannelHeap.h
    include/mafianet/DS_OrderedList.h
    include/mafianet/DS_Queue.h
    include/mafianet/DS_QueueLinkedList.h
    include/mafianet/DS_RangeList.h
    include/mafianet/DS_Table.h
    include/mafianet/DS_ThreadsafeAllocatingQueue.h
    include/mafianet/DS_Tree.h
    include/mafianet/DS_WeightedGraph.h
    include/mafianet/DynDNS.h
    include/mafianet/EmailSender.h
    include/mafianet/EmptyHeader.h
    include/mafianet/EpochTimeToString.h
    include/mafianet/Export.h
    include/mafianet/FileList.h
    include/mafianet/FileListNodeContext.h
    include/mafianet/FileListTransfer.h
    include/mafianet/FileListTransferCBInterface.h
    include/mafianet/FileOperations.h
    include/mafianet/FormatString.h
    include/mafianet/FullyConnectedMesh2.h
    include/mafianet/Getche.h
    include/mafianet/Gets.h
    include/mafianet/GetTime.h
    include/mafianet/gettimeofday.h
    include/mafianet/GridSectorizer.h
    include/mafianet/HTTPConnection.h
    include/mafianet/HTTPConnection2.h
    include/mafianet/IncrementalReadInterface.h
    include/mafianet/InternalPacket.h
    include/mafianet/Itoa.h
    include/mafianet/Kbhit.h
    include/mafianet/linux_adapter.h
    include/mafianet/LinuxStrings.h
    include/mafianet/LocklessTypes.h
    include/mafianet/LogCommandParser.h
    include/mafianet/memoryoverride.h
    include/mafianet/MessageFilter.h
    include/mafianet/MessageIdentifiers.h
    include/mafianet/MTUSize.h
    include/mafianet/NativeFeatureIncludes.h
    include/mafianet/NativeFeatureIncludesOverrides.h
    include/mafianet/NativeTypes.h
    include/mafianet/NatPunchthroughClient.h
    include/mafianet/NatPunchthroughServer.h
    include/mafianet/NatTypeDetectionClient.h
    include/mafianet/NatTypeDetectionCommon.h
    include/mafianet/NatTypeDetectionServer.h
    include/mafianet/NetworkIDManager.h
    include/mafianet/NetworkIDObject.h
    include/mafianet/osx_adapter.h
    include/mafianet/PacketConsoleLogger.h
    include/mafianet/PacketFileLogger.h
    include/mafianet/PacketizedTCP.h
    include/mafianet/PacketLogger.h
    include/mafianet/PacketOutputWindowLogger.h
    include/mafianet/PacketPool.h
    include/mafianet/PacketPriority.h
    include/mafianet/peer.h
    include/mafianet/peerinterface.h
    include/mafianet/PluginInterface2.h
    include/mafianet/PS3Includes.h
    include/mafianet/PS4Includes.h
    include/mafianet/Rackspace.h
    include/mafianet/Rand.h
    include/mafianet/RandSync.h
    include/mafianet/ReadyEvent.h
    include/mafianet/RefCountedObj.h
    include/mafianet/RelayPlugin.h
    include/mafianet/ReliabilityLayer.h
    include/mafianet/ReplicaEnums.h
    include/mafianet/ReplicaManager3.h
    include/mafianet/Router2.h
    include/mafianet/RPC4Plugin.h
    include/mafianet/SecureHandshake.h
    include/mafianet/SendToThread.h
    include/mafianet/SignaledEvent.h
    include/mafianet/SimpleMutex.h
    include/mafianet/SimpleTCPServer.h
    include/mafianet/SingleProducerConsumer.h
    include/mafianet/sleep.h
    include/mafianet/mafianet.h
    include/mafianet/smartptr.h
    include/mafianet/socket.h
    include/mafianet/socket2.h
    include/mafianet/SocketDefines.h
    include/mafianet/SocketIncludes.h
    include/mafianet/SocketLayer.h
    include/mafianet/statistics.h
    include/mafianet/StatisticsHistory.h
    include/mafianet/string.h
    include/mafianet/StringCompressor.h
    include/mafianet/StringTable.h
    include/mafianet/SuperFastHash.h
    include/mafianet/TableSerializer.h
    include/mafianet/TCPInterface.h
    include/mafianet/TeamBalancer.h
    include/mafianet/TeamManager.h
    include/mafianet/TelnetTransport.h
    include/mafianet/thread.h
    include/mafianet/ThreadPool.h
    include/mafianet/ThreadsafePacketLogger.h
    include/mafianet/time.h
    include/mafianet/transport2.h
    include/mafianet/TransportInterface.h
    include/mafianet/TwoWayAuthentication.h
    include/mafianet/types.h
    include/mafianet/UDPForwarder.h
    include/mafianet/UDPProxyClient.h
    include/mafianet/UDPProxyCommon.h
    include/mafianet/UDPProxyCoordinator.h
    include/mafianet/UDPProxyServer.h
    include/mafianet/VariableDeltaSerializer.h
    include/mafianet/VariableListDeltaTracker.h
    include/mafianet/VariadicSQLParser.h
    include/mafianet/version.h
    include/mafianet/VitaIncludes.h
    include/mafianet/WindowsIncludes.h
    include/mafianet/WSAStartupSingleton.h
    include/mafianet/wstring.h
    include/mafianet/XBox360Includes.h
)

set(MAFIANET_CRYPTO_HEADERS
    include/mafianet/crypto/cryptomanager.h
    include/mafianet/crypto/factory.h
    include/mafianet/crypto/fileencrypter.h
    include/mafianet/crypto/ifileencrypter.h
    include/mafianet/crypto/securestring.h
)

# Combine all sources
set(MAFIANET_ALL_SOURCES
    ${MAFIANET_SOURCES}
    ${MAFIANET_CRYPTO_SOURCES}
)

set(MAFIANET_ALL_HEADERS
    ${MAFIANET_HEADERS}
    ${MAFIANET_CRYPTO_HEADERS}
)

# Common target setup function
function(mafianet_configure_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(${target_name}
        PUBLIC
            OpenSSL::SSL
            OpenSSL::Crypto
        PRIVATE
            Threads::Threads
            $<$<PLATFORM_ID:Windows>:ws2_32>
    )

    target_compile_definitions(${target_name}
        PRIVATE
            $<$<CONFIG:Debug>:_DEBUG>
            $<$<CONFIG:Release>:NDEBUG>
    )

    set_target_properties(${target_name} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        FOLDER "Libraries"
    )

    # Source groups for IDE organization
    source_group("Source Files" FILES ${MAFIANET_SOURCES})
    source_group("Source Files\\crypto" FILES ${MAFIANET_CRYPTO_SOURCES})
    source_group("Header Files" FILES ${MAFIANET_HEADERS})
    source_group("Header Files\\crypto" FILES ${MAFIANET_CRYPTO_HEADERS})
endfunction()

# Static library
if(MAFIANET_BUILD_STATIC)
    add_library(MafiaNetStatic STATIC
        ${MAFIANET_ALL_SOURCES}
        ${MAFIANET_ALL_HEADERS}
    )
    add_library(MafiaNet::MafiaNetStatic ALIAS MafiaNetStatic)

    mafianet_configure_target(MafiaNetStatic)

    target_compile_definitions(MafiaNetStatic
        PUBLIC
            $<$<PLATFORM_ID:Windows>:_MAFIANET_LIB>
    )
endif()

# Shared library
if(MAFIANET_BUILD_SHARED)
    add_library(MafiaNet SHARED
        ${MAFIANET_ALL_SOURCES}
        ${MAFIANET_ALL_HEADERS}
    )
    add_library(MafiaNet::MafiaNet ALIAS MafiaNet)

    mafianet_configure_target(MafiaNet)

    target_compile_definitions(MafiaNet
        PRIVATE
            MAFIANET_DLL_EXPORT
        PUBLIC
            $<$<PLATFORM_ID:Windows>:_MAFIANET_DLL>
    )

    if(NOT WIN32)
        set_target_properties(MafiaNet PROPERTIES
            OUTPUT_NAME "mafianet"
        )
    endif()
endif()

# Installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Determine which targets to install
set(MAFIANET_INSTALL_TARGETS)
if(MAFIANET_BUILD_STATIC)
    list(APPEND MAFIANET_INSTALL_TARGETS MafiaNetStatic)
endif()
if(MAFIANET_BUILD_SHARED)
    list(APPEND MAFIANET_INSTALL_TARGETS MafiaNet)
endif()

# Install targets
install(TARGETS ${MAFIANET_INSTALL_TARGETS}
    EXPORT MafiaNetTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Generate and install config
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/MafiaNetConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MafiaNetConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MafiaNet
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MafiaNetConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MafiaNetConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MafiaNetConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MafiaNet
)

install(EXPORT MafiaNetTargets
    FILE MafiaNetTargets.cmake
    NAMESPACE MafiaNet::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MafiaNet
)
