// TODO

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/hash.h>
#include <brpc/server.h>
#include <brpc/restful.h>
#include "sps.pb.h"

DEFINE_int32(port, 8080, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(certificate, "insecure.crt", "Certificate file path to enable SSL");
DEFINE_string(private_key, "insecure.key", "Private key file path to enable SSL");

namespace sps {

struct UserKey {
    struct Hasher {
        size_t operator()(const UserKey& key) const {
            return butil::Hash((char*)&key.uid, 10);
        }
    };
    int64_t uid;
    int16_t device_type;
};

struct RoomKey {
    struct Hasher {
        size_t operator()(const RoomKey& key) const {
            return butil::Hash(key.roomid, 36);
        }
    };
    char roomid[37];
};

class Session : public brpc::SharedObject,
                public butil::LinkNode<Session> {
public:
    typedef butil::intrusive_ptr<Session> Ptr;
    typedef butil::LinkedList<Session> List;
private:
    UserKey key_;
    bthread::Mutex mutex_;
    std::vector<RoomKey> interested_rooms_;
};

class Room : public brpc::SharedObject {
public:
    typedef butil::intrusive_ptr<Room> Ptr;
private:
    RoomKey key_;
    bthread::Mutex mutex_;
    Session::List sessions_;
};

class Bucket : public brpc::SharedObject {
public:
    typedef butil::intrusive_ptr<Bucket> Ptr;
private:
    bthread::Mutex mutex_;
    butil::FlatMap<UserKey, Session::Ptr, UserKey::Hasher> sessions_;
    butil::FlatMap<RoomKey, Room::Ptr, RoomKey::Hasher> rooms_;
};

class SimplePushServer {
public:
private:
    std::unique_ptr<brpc::Server> rpc_server_;
    std::vector<Bucket::Ptr> buckets_;
};

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

}

int main(int argc, char* argv[]) {
    GFLAGS_NS::SetUsageMessage("A simple push server");
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;

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
    return 0;
}
