# Simple push server (sps)

SDK-less. Use HTTP v1.1 chunked transfer encoding to push events.

# Definition

* Client is the process running on the mobile device.
* Server is the sps process.
* UID is the user identity number.
* Token could be the JWT.
* Wire is the HTTP connection between Client and Server after a
  successful subscribe.

# Protocol

## Security

Always use HTTPS.

Generate a Self-Signed Certificate. Use this method if you want to use HTTPS
(HTTP over TLS) to secure your Apache HTTP or Nginx web server, and you do
not require that your certificate is signed by a CA.

This command creates a 2048-bit private key (domain.key) and a self-signed
certificate (domain.crt) from scratch:

    openssl req \
       -newkey rsa:2048 -nodes -keyout domain.key \
       -x509 -days 365 -out domain.crt

Answer the CSR information prompt to complete the process.

The -x509 option tells req to create a self-signed certificate. The -days 365
option specifies that the certificate will be valid for 365 days. A temporary
CSR is generated to gather information to associate with the certificate.

## Take online

Prerequisite:

    User has login with UID. Got the Server returned Token.

Client subscribe push events by HTTP GET

    /subscribe?u=<user_identity>[&t=<terminal_identity>][&r=<room_identity>][&i=<anti-idle_seconds>][&o=<opaque>]
    Authorization: <type> <credentials>

Client keeps the HTTP connection once successfully authenticated, and
continue reading from it. Client is considered online by Server as long
as this HTTP connection is open.

Client may provide a `t` (terminal) to uniquely identify the user's
device. Only one Wire is allowed per user per device. A secondary subscribe
replaces the Wire with the new HTTP connection, and quits previous connection.

Client may provide a `r` (room) to specify the interested topics. Note
this is the initial subscribing topics. Currently subscribing topics can
be changed while Wire connects.

Client may provide an `i` (anti-idle) to specify the seconds after which
Server will send anti-idle events.

Client may provide an opaque parameter for reliable events.

## Consume push events

Server sends push events on the Wire using chunked transfer encoding.
It never ends, unless Wire is broken, Client quits, or idle for too long.

## Client quits

Client can quit arbitrarily. When it happens, the Wire is disconnected
intentionally or left idle, until Client re-connects and subscribe again.
Client is considered offline by Server during this stage.

## On idle for too long

Server regularly sends anti-idle events on the Wire. 

## Reliable events

The event has an ID, which is a monotonous increasing number. When Client
consumes a event, it stores its ID as an opaque string. Client always
subscribes to Server with the latest ID it knows.

Server sends all the push events that greater than this ID when Client
quits and subscribe again. Server may remove outdated push events.

## Topics

Client subscribes the interested topics by joining rooms. Each room holds
one topic. All Clients in the room will be notified when a event is published
to the room.

# Environment

Install these on Ubuntu

    sudo apt-get install git g++ make libssl-dev libgflags-dev libprotobuf-dev libprotoc-dev protobuf-compiler libleveldb-dev libsnappy-dev libgoogle-perftools-dev
