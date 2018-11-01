支持新的协议
---

从 Socket 上读消息时，如何设计一个最高效的 Buffer ？

* **避免 copy**
  - copy 时，拷贝的是管理结构而不是数据

* **用 linked-list**
  - 删除 T* 是 O(1)
  - 新增，无需分配内存：link-node 自带 next / previous T*

* **试探性的确定 read size** ，有两种办法：
  - size 不够，*2 扩充；size 太大，/2 收缩
  - 前十个消息的平均 size * 16

* **buffer 分段非连续**
* **buffer 里有 offset ，应对消费不完**
* 线程内缓存分配的内存
* 引入 TCMalloc

读完消息，开始分割。参见 `void InputMessenger::OnNewMessages(Socket* m)`。所有的消息，都在一个 bthread 里依序分割。

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

这里列举一些常见协议的分割方法：
