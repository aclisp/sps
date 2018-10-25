#ifndef SPS_BUCKET_H_
#define SPS_BUCKET_H_

#include <stddef.h>
#include <stdint.h>
#include <butil/hash.h>
#include <brpc/shared_object.h>
#include <brpc/describable.h>
#include <brpc/progressive_attachment.h>
#include <bthread/mutex.h>
#include <butil/containers/flat_map.h>


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
    explicit UserKey(int64_t uid, int16_t device_type = 0) : uid(uid), device_type(device_type) {}
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
public:
    typedef butil::intrusive_ptr<Session> Ptr;
    typedef butil::FlatMap<UserKey, Session::Ptr, UserKey::Hasher> Map;

    Session(const UserKey& key, brpc::ProgressiveAttachment* pa);
    ~Session();
    void set_interested_room(const std::string& rooms);

    std::vector<RoomKey> interested_rooms() const;
    void Describe(std::ostream& os, const brpc::DescribeOptions&) const;
    const UserKey& key() const { return key_; }

private:
    UserKey key_;
    butil::intrusive_ptr<brpc::ProgressiveAttachment> writer_;
    int64_t created_us_;
    int64_t written_us_;
    mutable bthread::Mutex mutex_;
    std::vector<RoomKey> interested_rooms_;
};

class Room : public brpc::SharedObject {
    friend class Bucket;

public:
    typedef butil::intrusive_ptr<Room> Ptr;
    typedef butil::FlatMap<RoomKey, Room::Ptr, RoomKey::Hasher> Map;

    ~Room();

    const char* room_id() const { return key_.room_id(); }
    const RoomKey& key() const { return key_; }
    bool has_session(Session::Ptr ps) const;
    size_t size() const { return sessions_.size(); }

protected:
    explicit Room(const RoomKey& key);
    void add_session(Session::Ptr ps);
    bool del_session(Session::Ptr ps);

private:
    RoomKey key_;
    mutable bthread::Mutex mutex_;
    Session::Map sessions_;
};

class Bucket : public brpc::SharedObject,
               public brpc::Describable {
public:
    typedef std::unique_ptr<Bucket> Ptr;

    Bucket(int index, const ServerOptions& options);
    ~Bucket();

    void add_session(Session* session);
    void add_session(Session::Ptr ps);
    Session::Ptr del_session(const UserKey& key);
    void update_session_rooms(const UserKey& key, const std::string& new_rooms);

    int index() const { return index_; }
    void Describe(std::ostream& os, const brpc::DescribeOptions&) const;
    Session::Ptr get_session(const UserKey& key) const;
    Room::Ptr get_room(const RoomKey& key) const;

protected:
    bool session_rooms_unchanged(const UserKey& key, const std::string& new_rooms) const;

private:
    const int index_;
    mutable bthread::Mutex mutex_;
    Session::Map sessions_;
    Room::Map rooms_;
};

}  // namespace sps

#endif  // SPS_BUCKET_H_
