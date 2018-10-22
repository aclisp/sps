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
    explicit UserKey(int64_t _uid) : uid(_uid), device_type(0) {}
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
    explicit RoomKey(const std::string& roomid_str) {
        strncpy(roomid, roomid_str.c_str(), sizeof(roomid)-1);
        roomid[sizeof(roomid)-1] = '\0';
    }
    bool operator==(const RoomKey& rhs) const {
        // strncpy pads roomid with (further) null bytes
        return memcmp(roomid, rhs.roomid, sizeof(roomid)-1) == 0;
    }
    const char* room_id() const { return static_cast<const char*>(roomid); }
    char roomid[37];
};

class Session : public brpc::SharedObject,
                public brpc::Describable {
friend class Bucket;
friend class Room;
public:
    typedef butil::intrusive_ptr<Session> Ptr;
    Session(const UserKey& user, brpc::ProgressiveAttachment* pa);
    ~Session();
    void set_interested_room(const std::string& rooms);
    std::vector<RoomKey> interested_rooms() const;
    void Describe(std::ostream& os, const brpc::DescribeOptions&) const;
private:
    UserKey key_;
    butil::intrusive_ptr<brpc::ProgressiveAttachment> writer_;
    int64_t created_us_;
    int64_t written_us_;
    mutable bthread::Mutex mutex_;
    std::vector<RoomKey> interested_rooms_;
};

class Room : public brpc::SharedObject {
public:
    typedef butil::intrusive_ptr<Room> Ptr;
    explicit Room(const RoomKey& rid);
    ~Room();
    void add_session(Session::Ptr ps);
    const char* room_id() const { return key_.room_id(); }
private:
    RoomKey key_;
    mutable bthread::Mutex mutex_;
    butil::FlatMap<UserKey, Session::Ptr, UserKey::Hasher> sessions_;
};

class Bucket : public brpc::SharedObject,
               public brpc::Describable {
public:
    typedef std::unique_ptr<Bucket> Ptr;
    Bucket(int index, const ServerOptions& options);
    ~Bucket();
    int index() const { return index_; }
    void add_session(Session* session);
    void Describe(std::ostream& os, const brpc::DescribeOptions&) const;
private:
    const int index_;
    mutable bthread::Mutex mutex_;
    butil::FlatMap<UserKey, Session::Ptr, UserKey::Hasher> sessions_;
    butil::FlatMap<RoomKey, Room::Ptr, RoomKey::Hasher> rooms_;
};

}  // namespace sps

#endif  // SPS_BUCKET_H_
