# Event-Driven-Chat-Server
---

## Description
This is an event driven chat server, where users on the same network can send and receive messages.
The Server is event driven using `select` function, meaning it is implemented without using multiple threads.


## How to run
To compile: 
in a linux/unix terminal run: `gcc chatServer.c -o chatServer`
a compiled file will be created with the name proxy.

To run:
in a linux/unix terminal run: `./chatServer <port>`

