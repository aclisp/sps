#ifndef SPS_BUCKET_H_
#define SPS_BUCKET_H_

namespace sps {

struct ServerOptions {
    ServerOptions();
    size_t bucket_size;
    size_t suggested_room_count;
    size_t suggested_user_count;
};

struct UserKey {
    struct Hasher {
        size_t operator()(const UserKey& key) const {
            return butil::Hash((char*)&key.uid, 10);
        }
    };
    bool operator==(const UserKey& rhs) const {
        return uid == rhs.uid
            && device_type == rhs.device_type;
    }
    int64_t uid;
    int16_t device_type;
};

struct RoomKey {
    struct Hasher {
        size_t operator()(const RoomKey& key) const {
            return butil::Hash(key.roomid, 36);
        }
    };
    RoomKey(const std::string& roomid_str) {
        strncpy(roomid, roomid_str.c_str(), sizeof(roomid));
        roomid[sizeof(roomid)-1] = '\0';
    }
    bool operator==(const RoomKey& rhs) const {
        return memcmp(roomid, rhs.roomid, sizeof(roomid)-1);
    }
    char roomid[37];
};

class Session : public brpc::SharedObject,
                public butil::LinkNode<Session> {
friend class Bucket;
public:
    typedef butil::intrusive_ptr<Session> Ptr;
    typedef butil::LinkedList<Session> List;
    Session(const UserKey& user, brpc::ProgressiveAttachment* pa);
    void set_interested_room(const std::string& rooms);
private:
    UserKey key_;
    butil::intrusive_ptr<brpc::ProgressiveAttachment> writer_;
    int64_t created_ms_;
    int64_t written_ms_;
    bthread::Mutex mutex_;
    std::vector<RoomKey> interested_rooms_;
};

class Room : public brpc::SharedObject {
public:
    typedef butil::intrusive_ptr<Room> Ptr;
    Room(const RoomKey& rid) : key_(rid) {}
    void add_session(Session* session);
private:
    RoomKey key_;
    bthread::Mutex mutex_;
    Session::List sessions_;
};

class Bucket : public brpc::SharedObject {
public:
    typedef std::unique_ptr<Bucket> Ptr;
    explicit Bucket(int index, const ServerOptions& options);
    ~Bucket() { LOG(INFO) << "destory bucket[" << index_ << "]"; }
    int index() { return index_; }
    void add_session(const UserKey& key, Session* session);
private:
    const int index_;
    bthread::Mutex mutex_;
    butil::FlatMap<UserKey, Session::Ptr, UserKey::Hasher> sessions_;
    butil::FlatMap<RoomKey, Room::Ptr, RoomKey::Hasher> rooms_;
};

}  // namespace sps

#endif  // SPS_BUCKET_H_
