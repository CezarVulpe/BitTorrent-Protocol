# Distributed File Sharing System

This program implements a **distributed file sharing system** using **MPI (Message Passing Interface)** for inter-process communication and **Pthreads** for concurrent execution within each process. The system consists of a **tracker** that manages the file swarm and multiple **peers** that exchange file segments.

## Features

- **Tracker-based Coordination**: The tracker maintains swarm data, keeping track of peers that own file segments.
- **Peer-to-Peer File Transfer**: Peers request and exchange file segments directly, optimizing network usage.
- **Load Balancing**: The tracker sorts peers based on request count to distribute load efficiently.
- **Parallel Execution**: Each peer runs separate threads for downloading and uploading.
- **Synchronization Mechanisms**: Mutexes ensure thread-safe updates of shared data.

## System Components

### **Tracker (Rank 0)**
- **Manages the file swarm**: Maintains a list of files and the peers holding them.
- **Handles peer requests**: Provides a sorted list of peers for requested files.
- **Load balancing**: Tracks request counts and reorders peer lists accordingly.
- **Monitors download completion**: Sends termination signals when all peers have completed downloads.

### **Peers (Ranks > 0)**
- **Maintain owned and desired files**: Reads input files to determine available and requested content.
- **Download Manager**: Requests file segments from available peers, updating peer lists dynamically.
- **Upload Manager**: Responds to requests from other peers and transmits file segments.
- **Synchronization**: Uses mutexes to ensure data consistency between download and upload operations.

## Implementation Details

### **Data Structures**
- `SwarmEntry`: Stores file name and list of peers that own segments.
- `PeerRequestInfo`: Tracks request counts to balance load.
- `FileInfo`: Stores file metadata, including segment hashes.
- `PeerInfo`: Manages peer state, including owned and requested files.

### **Core Functions**
- `find_or_add_file()`: Tracks files and assigns peer ownership.
- `find_or_add_peer()`: Maintains request statistics for load balancing.
- `sort_peers()`: Reorders peer lists based on request counts.
- `tracker()`: Main loop that processes peer requests and manages termination signals.
- `peer()`: Initializes peer state and launches download/upload threads.
- `download_thread_func()`: Handles segment requests and manages received file data.
- `upload_thread_func()`: Serves file segment requests from other peers.

## Execution Flow
1. **Initialization**: Tracker registers all peers and their available files.
2. **File Requests**: Peers send file requests to the tracker, receiving an updated peer list.
3. **Data Transfer**:
   - Download thread retrieves file segments from listed peers.
   - Upload thread serves requested segments.
4. **Load Balancing**: Peer lists are updated every 10 requests.
5. **Completion Handling**: Peers notify the tracker when downloads are complete.
6. **Termination**: Tracker sends stop signals once all peers finish downloads.


