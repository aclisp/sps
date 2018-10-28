#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/strings/string_number_conversions.h>
#include <butil/strings/string_split.h>
#include <brpc/server.h>

#include "sps_bucket.h"
#include "sps.pb.h"


DEFINE_int32(port, 8080, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(certificate, "insecure.crt", "Certificate file path to enable SSL");
DEFINE_string(private_key, "insecure.key", "Private key file path to enable SSL");

namespace sps {

class SimplePushServer;
SimplePushServer* SPS = nullptr;

class SimplePushServer {
public:
    typedef std::unique_ptr<SimplePushServer> Ptr;
    explicit SimplePushServer(const ServerOptions& options);
    brpc::Server& brpc_server() { return *brpc_server_; }
    Bucket& bucket(int64_t uid) {
        return *buckets_[uid % buckets_.size()];  // uid promotes to unsigned
    }
    std::vector<Bucket::Ptr>& buckets() { return buckets_; }
private:
    std::unique_ptr<brpc::Server> brpc_server_;
    std::vector<Bucket::Ptr> buckets_;
};

SimplePushServer::SimplePushServer(const ServerOptions& options)
    : brpc_server_(new brpc::Server)
    , buckets_(options.bucket_size) {
    for (size_t i = 0; i < options.bucket_size; ++i) {
        buckets_[i].reset(new Bucket(i, options));
    }
}

void remove_from_bucket(Bucket& bucket, UserKey key, void* cid) {
    VLOG(1) << "just enter remove_from_bucket: " << bucket;
    VLOG(1) << "would remove this key: " << key.uid << "," << key.device_type;
    Session::Ptr ps = bucket.get_session(key);
    if (ps && ps->connection_id() != cid) {
        VLOG(1) << "connection id mismatch: session already removed";
        VLOG(1) << "current session: " << *ps;
        return;
    }

    ps = bucket.del_session(key);
    VLOG(1) << "removed session: " << noflush;
    if (!ps) {
        VLOG(1) << "already removed" << noflush;
    } else {
        VLOG(1) << *ps << noflush;
    }
    VLOG(1);
}

class PushServiceImpl : public PushService {
public:
    PushServiceImpl() {};
    virtual ~PushServiceImpl() {};

    void subscribe(google::protobuf::RpcController* cntl_base,
                       const HttpRequest* ,
                       HttpResponse* ,
                       google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        const brpc::URI& uri = cntl->http_request().uri();
        const std::string* pRooms = uri.GetQuery("tid");
        UserKey key(0);
        if (!get_user_key_from_uri(uri, cntl, &key)) {
            return;
        }

        Bucket& bucket = SPS->bucket(key.uid);
        brpc::ProgressiveAttachment* pa = cntl->CreateProgressiveAttachment(brpc::FORCE_STOP);
        pa->NotifyOnStopped(brpc::NewCallback<Bucket&, UserKey, void*>(remove_from_bucket, bucket, key, pa));
        std::unique_ptr<Session> session(new Session(key, pa));
        if (pRooms) {
            session->set_interested_room(*pRooms);
        }
        bucket.add_session(session.release());

        VLOG(1) << "subscribe ok: " << bucket << " " << *bucket.get_session(key);
    }

    void notify_to_user(google::protobuf::RpcController* cntl_base,
                        const HttpRequest* ,
                        HttpResponse* ,
                        google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller *cntl = static_cast<brpc::Controller *>(cntl_base);

        const brpc::URI &uri = cntl->http_request().uri();
        UserKey key(0);
        if (!get_user_key_from_uri(uri, cntl, &key)) {
            return;
        }

        Bucket &bucket = SPS->bucket(key.uid);
        Session::Ptr ps = bucket.get_session(key);
        cntl->http_response().set_content_type("text/plain");
        butil::IOBufBuilder os;
        int err = 0;
        if (!ps) {
            os << "offline";
        } else {
            err = ps->Write(cntl->request_attachment());
            if (0 == err) {
                os << "delivered";
            } else {
                os << "error";
            }
        }
        os << "\nuid=" << key.uid << " hid=" << key.device_type << "\n";
        if (err) {
            os << "err=" << err << " " << berror(err) << "\n";
        }
        os.move_to(cntl->response_attachment());
    }

    void notify_to_room(google::protobuf::RpcController* cntl_base,
                         const HttpRequest* ,
                         HttpResponse* ,
                         google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        const brpc::URI& uri = cntl->http_request().uri();
        const std::string* pRooms = uri.GetQuery("tid");
        if (pRooms == NULL) {
            cntl->SetFailed(EINVAL, "`tid` is required");
            return;
        }

        // parse tid
        std::vector<RoomKey> target_rooms;
        std::vector<std::string> pieces;
        butil::SplitString(*pRooms, ',', &pieces);
        for (const std::string& s : pieces) {
            if (s.empty()) continue;
            target_rooms.emplace_back(RoomKey(s));
        }
        if (target_rooms.empty()) {
            cntl->SetFailed(EINVAL, "`tid` is empty");
            return;
        }

        for (const RoomKey& key : target_rooms) {
            for (Bucket::Ptr& pb : SPS->buckets()) {
                Room::Ptr pr = pb->get_room(key);
                if (pr) {
                    pr->Write(cntl->request_attachment());
                }
            }
        }
        cntl->http_response().set_content_type("text/plain");
    }

protected:
    bool get_user_key_from_uri(const brpc::URI& uri, /*in*/brpc::Controller* cntl, /*out*/UserKey* key) {
        const std::string* pUid = uri.GetQuery("uid");
        const std::string* pDeviceType = uri.GetQuery("hid");

        if (pUid == NULL) {
            cntl->SetFailed(EINVAL, "`uid` is required");
            return false;
        }
        int64_t uid = 0;
        if (!butil::StringToInt64(*pUid, &uid)) {
            cntl->SetFailed(EINVAL, "`uid` is not a number: %s", pUid->c_str());
            return false;
        }
        int device_type = 0;
        if (pDeviceType) {
            if (!butil::StringToInt(*pDeviceType, &device_type)) {
                cntl->SetFailed(EINVAL, "`hid` is not a number: %s", pDeviceType->c_str());
                return false;
            }
        }
        if (key) {
            key->uid = uid;
            key->device_type = device_type;
        }
        return true;
    }
};

}  // namespace sps

int main(int argc, char* argv[]) {
    GFLAGS_NS::SetUsageMessage("A simple push server");
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

    sps::ServerOptions push_server_options;
    sps::SimplePushServer::Ptr push_server(new sps::SimplePushServer(push_server_options));
    sps::SPS = push_server.get();
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
    return 0;
}
