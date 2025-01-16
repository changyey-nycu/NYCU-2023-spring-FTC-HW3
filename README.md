## 2023 Fault Tolerant Computing HW3 ##
Implement the e-Voting server and a backup server.

# Built With
* [ubuntu 2204]
* [cmake v3.22.1]
* [Docker v20.10.17]

# prerequire
* install grpc (https://grpc.io/docs/languages/cpp/quickstart/)
* install cmake [sudo apt install -y cmake]
* install libsodium (https://doc.libsodium.org/installation)
* install docker & docker-compose
* install libpqxx [sudo apt install libpqxx-dev]

# change the server port
default address and port is 0.0.0.0:50051 and 0.0.0.0:50052
you can change the port by running
./eVoting_server [localport] [backup_address:port]
./eVoting_backup [localport] [primary_address:port]
./eVoting_client [address:port] [backup_address:port]
If no set the port then it will use the default address

# change the database server setting
The database is postgressql system, built by docker

default database address and port is 0.0.0.0:8001 and 0.0.0:8002
you can change the port and setting example for 8001
1. In docker-compose.yml change port 8001 to other port 
    ports:
      - "8001:5432"

2. change the connection setting in eVoting_server.cc line:38
    #define DBCONNECTINFO "dbname=eVoting user=admin password=passwd hostaddr=127.0.0.1 port=8001"

same way for backup server database

# compile and run
1. In ./cmake/common.cmake Line:51 "$ENV{HOME}/grpc" change to your grpc folder if your grpc folder is in other directory

2. compile : In the root dirctory run
```sh
    cmake -B .
    make
```

3. start database in docker
```sh
    sudo docker-compose up -d
```

4. run server (default address and port is 0.0.0.0:50051 and 0.0.0.0:50052)
./eVoting_server [localport] [backup_address:port]
./eVoting_backup [localport] [primary_address:port]

5. run cilent (default address and port is 0.0.0.0:50051 and 0.0.0.0:50052)
./eVoting_client [address:port] [backup_address:port]

# note
This version still has some login bug, so using file and database both to save public key

