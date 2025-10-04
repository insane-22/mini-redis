# MiniRedis- C++ Redis Clone



## Project Overview

A lightweight, high-performance C++ Redis clone implemented in modern C++, developed as part of the CodeCrafters “Build Your Own Redis” challenge. This project re-implements the core of Redis, including key-value storage, lists, streams, transactions, replication, RDB persistence, and pub/sub messaging.




## Key goals

- Core Server- RESP protocol parser, event loop, command execution 
- Data Structure- Hash tables for key-value storage, Linked lists for Redis lists, Custom stream implementation
- Replication- Master–replica synchronization using PSYNC and command propagation
- RDB Persistence- Snapshotting and RDB file parsing/loading
- Pub/Sub- Real-time messaging with PUBLISH, SUBSCRIBE, UNSUBSCRIBE
- GeoSpatial Commands- Implemented using Sorted sets and mathematical formulations as followed by Redis



## Architecture Overview
The Mini Redis server is structured into modular handlers, each responsible for a Redis data type or subsystem.
```bash
mini-redis/
├── src/
│   ├── Server.cpp              # Main server loop and client handler
│   ├── Handler.cpp             # Command routing and parsing
│   ├── KvStoreHandler.cpp      # Key-Value operations
│   ├── ListStoreHandler.cpp    # List implementation
│   ├── StreamStoreHandler.cpp  # Streams implementation
│   ├── SortedSetHandler.cpp    # Sorted sets implementation
│   ├── GeoHandler.cpp          # GeoSpatial Commands implementation
│   ├── ReplicaClient.cpp       # Replica–Master communication
│   ├── ReplicationManager.cpp  # Handles replication sync & command propagation
│   ├── RdbReader.cpp           # RDB file parsing & persistence
│   └── RdbWriter.cpp           # RDB snapshot creation
├── include/
│   ├── *.hpp                   # Headers for all handlers & managers
├── your_program.sh             # Build & run script
├── CMakeLists.txt              # CMake build configuration
├── vcpkg.json
├── vcpkg-configuration.json
├── your_program.sh             # Executable file 
└── README.md                   # This file
```


## Build and Run

### 1. Clone the repository

```bash
git clone https://github.com/insane-22/mini-redis
cd mini-redis
```

### 2. Quick run (shortcut script)

```bash
./your_program.sh
```
The script compiles the code and starts the server on the default port (script behavior is in your_program.sh)

### 3. Manual build (CMake)
```bash
# create and enter build directory
mkdir -p build && cd build

# if using vcpkg toolchain:
# cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j$(nproc)

# run server (binary name may vary — check build output)
./mini-redis 
```

### 4. Connect client
You need to have redis-cli installed for this
```bash
redis-cli
```

## Example Session
```bash
127.0.0.1:6379> SET name "MiniRedis"
OK
127.0.0.1:6379> GET name
"MiniRedis"
127.0.0.1:6379> LPUSH mylist "a" "b" "c"
(integer) 3
127.0.0.1:6379> XRANGE mystream - +
1) 1) "1733349039-0"
   2) 1) "field"
      2) "value"
127.0.0.1:6379> PUBLISH updates "Server Ready!"
(integer) 1
```

## Supported Commands-
### Basic Commands
- PING - Test server connectivity
- ECHO - Echo messages
- SET key value [EX seconds] - Set key-value pairs with optional expiration
- GET key - Get value by key
- EXISTS key - Check if key exists
- DEL key - Delete key
- INCR key - Increment integer value
### List Commands
- LPUSH key element - Push element to left of list
- RPUSH key element - Push element to right of list
- LPOP key - Pop element from left of list
- RPOP key - Pop element from right of list
- LLEN key - Get list length
### Stream Commands
- XADD key ID field value - Add entry to stream
- XRANGE key start end - Get range of stream entries
- XREAD [STREAMS] key ID - Read from stream
### Transaction Commands
- MULTI - Start transaction
- EXEC - Execute transaction
- DISCARD - Discard transaction
### Pub/Sub Commands
- PUBLISH channel message - Publish message to channel
- SUBSCRIBE channel - Subscribe to channel
- UNSUBSCRIBE channel - Unsubscribe from channel
### Replication Commands
- REPLCONF - Replication configuration
- PSYNC replicationid offset - Partial synchronization
### GeoSpatial commands
- GEOADD - Adds the coordinates of a place to lis
- GEOPOS - Returns the longitude and latitude of the specified location
