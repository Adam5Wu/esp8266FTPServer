# Modifications to original project

## File Handling

- Adjusted to be file system independent.

	This enables painless hook up with VFATFS or future file systems that conforms to or extends the Arduino FS interface.
- Adopt extended FS interface for retrieving modification time and other attributes

	This enables conventional modification-based file sync to work properly.

## Directory Handling

- Implemented this feature, which was left unimplemented due to lack of support in SPIFFS

## Authentication

- Uses functional interface instead of hard coding configurations

	This enables easy hookup of other existing authentication services.
	
## Connection Handling

- Adjusted timeout logic so that long file transfer will not time out in the middle

- FTP port now closes after starting a client connection, and re-opens after its disconnection

	This is mainly because we don't handle concurrent client connection, however, certain client implementation
	(e.g. Windows Explorer) tries to connect concurrently, and load balance file transfer requests across all
	non-closed connections. As a result, certain multi-file transfers will observe excessive delay.

	Closing FTP server port does the trick of rejecting all concurrent connections, and force the client to use
	only a single connection, thereby avoid waiting on the never-to-establish concurrent connections.