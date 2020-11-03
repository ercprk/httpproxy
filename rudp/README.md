# Reliable UDP File Transfer

## Usage

For running the server:
```
make rudpserver
./rudpserver <portnum>
```

For running the client:
```
make rudpclient
./rudpclient <hostIP> <portno> <windowsize> <filename>
```

## Requirements

For the client, the file will be copied to the ./DEST directory.
