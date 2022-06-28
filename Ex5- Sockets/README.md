# Parallel File Find

## Description
Implementation of a toy client/server architecture: a printable characters counting (PCC) server. 
Clients connect to the server and send it a stream of bytes. The server counts how many of the bytes are printable and returns that number to the client. 
The server also maintains overall statistics on the number of printable characters it has received from all clients. When the server terminates, it prints these statistics to standard output
