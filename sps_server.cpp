// TODO

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/hash.h>
#include <butil/strings/string_split.h>
#include <brpc/server.h>
#include <brpc/restful.h>
#include "sps.pb.h"

DEFINE_int32(port, 8080, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(certificate, "insecure.crt", "Certificate file path to enable SSL");
DEFINE_string(private_key, "insecure.key", "Private key file path to enable SSL");

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

class SimplePushServer {
public:
    typedef std::unique_ptr<SimplePushServer> Ptr;
    explicit SimplePushServer(const ServerOptions& options);
    brpc::Server& brpc_server() { return *brpc_server_; }
    Bucket& bucket(int64_t uid) {
        return *buckets_[uid % buckets_.size()];  // uid promotes to unsigned
    }
private:
    std::unique_ptr<brpc::Server> brpc_server_;
    std::vector<Bucket::Ptr> buckets_;
};

// ---

ServerOptions::ServerOptions()
    : bucket_size(8)
    , suggested_room_count(128)
    , suggested_user_count(1024) {
}

SimplePushServer::SimplePushServer(const ServerOptions& options)
    : brpc_server_(new brpc::Server)
    , buckets_(options.bucket_size) {
    for (size_t i = 0; i < options.bucket_size; ++i) {
        buckets_[i].reset(new Bucket(i, options));
    }
}

Bucket::Bucket(int index, const ServerOptions& options)
    : index_(index) {
    CHECK_EQ(0, sessions_.init(options.suggested_user_count, 70));
    CHECK_EQ(0, rooms_.init(options.suggested_room_count, 70));
    LOG(INFO) << "create bucket[" << index_ << "] of"
              << " room=" << options.suggested_room_count
              << " user=" << options.suggested_user_count;
}

Session::Session(const UserKey& user, brpc::ProgressiveAttachment* pa) {
    key_.uid = user.uid;
    key_.device_type = user.device_type;
    writer_.reset(pa);
    created_ms_ = butil::gettimeofday_ms();
    written_ms_ = 0;
}

void Session::set_interested_room(const std::string& rooms) {
    BAIDU_SCOPED_LOCK(mutex_);
    interested_rooms_.clear();
    std::vector<std::string> pieces;
    butil::SplitString(rooms, ',', &pieces);
    for (std::vector<std::string>::const_iterator it = pieces.begin();
         it != pieces.end(); ++it) {
        if (it->empty()) continue;
        interested_rooms_.emplace_back(RoomKey(*it));
    }
}

void Bucket::add_session(const UserKey& key, Session* session) {
    std::vector<Room::Ptr> interested_rooms;

    {
        BAIDU_SCOPED_LOCK(mutex_);
        sessions_[key].reset(session);
        // create room as needed
        for (std::vector<RoomKey>::const_iterator it = session->interested_rooms_.begin();
             it != session->interested_rooms_.end(); ++it) {
            const RoomKey& rid = *it;
            Room::Ptr room = rooms_[rid];
            if (!room) {
                room.reset(new Room(rid));
                rooms_[rid] = room;
            }
            interested_rooms.push_back(room);
        }
    }

    for (std::vector<Room::Ptr>::iterator it = interested_rooms.begin();
         it != interested_rooms.end(); ++it) {
        (*it)->add_session(session);
    }
}

void Room::add_session(Session* session) {
    BAIDU_SCOPED_LOCK(mutex_);
    sessions_.Append(session);
}

// ---

class PushServiceImpl : public PushService {
public:
    PushServiceImpl() {};
    virtual ~PushServiceImpl() {};
    virtual void subscribe(google::protobuf::RpcController* cntl_base,
                       const HttpRequest* ,
                       HttpResponse* ,
                       google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);

        butil::IOBufBuilder os;
        os << "queries:";
        for (brpc::URI::QueryIterator it = cntl->http_request().uri().QueryBegin();
                it != cntl->http_request().uri().QueryEnd(); ++it) {
            os << ' ' << it->first << '=' << it->second;
        }
        os << "\nbody: " << cntl->request_attachment() << '\n';
        os.move_to(cntl->response_attachment());
    }
};

}  // namespace sps

// ---

static sps::SimplePushServer* GPS = nullptr;

namespace sps {

Bucket& GetBucket(int64_t uid) {
    return GPS->bucket(uid);
}

}  // namespace sps

int main(int argc, char* argv[]) {
    GFLAGS_NS::SetUsageMessage("A simple push server");
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    sps::ServerOptions push_server_options;
    sps::SimplePushServer::Ptr push_server(new sps::SimplePushServer(push_server_options));
    GPS = push_server.get();
    brpc::Server& server = push_server->brpc_server();

    sps::PushServiceImpl push_svc;

    if (server.AddService(&push_svc,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add push_svc";
        return -1;
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.mutable_ssl_options()->default_cert.certificate = FLAGS_certificate;
    options.mutable_ssl_options()->default_cert.private_key = FLAGS_private_key;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start server";
        return -1;
    }

    server.RunUntilAskedToQuit();

    // testing
    LOG(INFO) << "quitting: hit bucket of " << sps::GetBucket(-1).index();

    return 0;
}
