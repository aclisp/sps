支持新的协议
---

从 Socket 上读消息时，如何设计一个最高效的 Buffer ？

* 避免 copy
* 用 linked-list
* 试探性的确定 read size
* buffer 里有 offset ，应对消费不完

读完消息，开始分割

```c++
    // [Required by both client and server]
    // The callback to cut a message from `source'.
    // Returned message will be passed to process_request and process_response
    // later and Destroy()-ed by InputMessenger.
    // Returns:
    //   MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA):
    //     `source' does not form a complete message yet.
    //   MakeParseError(PARSE_ERROR_TRY_OTHERS).
    //     `source' does not fit the protocol, the data should be tried by
    //     other protocols. If the data is definitely corrupted (e.g. magic 
    //     header matches but other fields are wrong), pop corrupted part
    //     from `source' before returning.
    //  MakeMessage(InputMessageBase*):
    //     The message is parsed successfully and cut from `source'.
    typedef ParseResult (*Parse)(butil::IOBuf* source, Socket *socket,
                                 bool read_eof, const void *arg);
    Parse parse;
```

