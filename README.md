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

Always use HTTPS

## Take online

Prerequisite:
    User has login with UID. Got the Server returned Token.

Client subscribe push events by HTTP GET
    /subscribe?uid=<identity>[&hid=<identity>][&tid=<identity>][&o=<opaque>]
    Authorization: <type> <credentials>

Client keeps the HTTP connection once successfully authenticated, and
continue reading from it. Client is considered online by Server as long
as this HTTP connection is open.

Client may provide a hid (Hardware ID) to uniquely identify the user's
device. Only one Wire is allowed per user per device. A secondary subscribe
replaces the Wire with the new HTTP connection, and quits previous connection.

Client may provide a tid (Topic ID) to specify the interested topics. Note
this is the initial subscribing topics. Currently subscribing topics can
be changed while Wire connects.

Client may provide an opaque parameter for reliable events.

## Consume push events

Server sends push events on the Wire using chunked transfer encoding.
It never ends, unless Wire is broken, Client quits, or idle for too long.

## Client quits

Client can quit arbitrarily. When it happens, the Wire is disconnected
intentionally or left idle, until Client re-connects and subscribe again.
Client is considered offline by Server during this stage.

## On idle for too long

Server regularly sends anti-idle events on the Wire. Client regularly
declares its alive by HTTP POST /subsist if and only if there is no
other requests for a long time.

## Reliable events

The event has an ID, which is a monotonous increasing number. When Client
consumes a event, it stores its ID as an opaque string. Client always
subscribes to Server with the latest ID it knows.

Server sends all the push events that greater than this ID when Client
quits and subscribe again. Server may remove outdated push events.

## Topics


