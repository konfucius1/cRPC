compile server
gcc -Wall server.a rpc.c -o test-server
./test-server < cases//server.in

compile client
gcc -Wall client.a rpc.c -o test-client
./test-client < cases//client.in

abc
./test-server < cases/abc/server.in
./test-client < cases/abc/client.in

bad.server
./test-server < cases/bad.server/server.in
./test-client < cases/bad.server/client.in