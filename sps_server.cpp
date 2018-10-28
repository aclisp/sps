#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/strings/string_number_conversions.h>
#include <brpc/server.h>

#include "sps_bucket.h"
#include "sps.pb.h"


DEFINE_int32(port, 8080, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(certificate, "insecure.crt", "Certificate file path to enable SSL");
DEFINE_string(private_key, "insecure.key", "Private key file path to enable SSL");

namespace sps {

Bucket& GetBucket(int64_t uid);

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

SimplePushServer::SimplePushServer(const ServerOptions& options)
    : brpc_server_(new brpc::Server)
    , buckets_(options.bucket_size) {
    for (size_t i = 0; i < options.bucket_size; ++i) {
        buckets_[i].reset(new Bucket(i, options));
    }
}

void remove_from_bucket(Bucket& bucket, UserKey key, void* cid) {
    LOG(INFO) << "just enter remove_from_bucket: " << bucket;
    LOG(INFO) << "would remove this key: " << key.uid << "," << key.device_type;
    Session::Ptr ps = bucket.get_session(key);
    if (ps && ps->connection_id() != cid) {
        LOG(INFO) << "connection id mismatch: session already removed";
        LOG(INFO) << "current session: " << *ps;
        return;
    }

    ps = bucket.del_session(key);
    LOG(INFO) << "removed session: " << noflush;
    if (!ps) {
        LOG(INFO) << "already removed" << noflush;
    } else {
        LOG(INFO) << *ps << noflush;
    }
    LOG(INFO);
}

class PushServiceImpl : public PushService {
public:
    PushServiceImpl() {};
    virtual ~PushServiceImpl() {};
    virtual void subscribe(google::protobuf::RpcController* cntl_base,
                       const HttpRequest* ,
                       HttpResponse* ,
                       google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        const brpc::URI& uri = cntl->http_request().uri();
        const std::string* pUid = uri.GetQuery("uid");
        const std::string* pDeviceType = uri.GetQuery("hid");
        const std::string* pRooms = uri.GetQuery("tid");

        if (pUid == NULL) {
            cntl->SetFailed(EINVAL, "`uid` is required");
            return;
        }
        int64_t uid = 0;
        if (!butil::StringToInt64(*pUid, &uid)) {
            cntl->SetFailed(EINVAL, "`uid` is not a number: %s", pUid->c_str());
            return;
        }

        int device_type = 0;
        if (pDeviceType) {
            if (!butil::StringToInt(*pDeviceType, &device_type)) {
                cntl->SetFailed(EINVAL, "`hid` is not a number: %s", pDeviceType->c_str());
                return;
            }
        }

        UserKey key(uid, device_type);
        Bucket& bucket = GetBucket(uid);
        brpc::ProgressiveAttachment* pa = cntl->CreateProgressiveAttachment(brpc::FORCE_STOP);
        pa->NotifyOnStopped(brpc::NewCallback<Bucket&, UserKey, void*>(remove_from_bucket, bucket, key, pa));
        std::unique_ptr<Session> session(new Session(key, pa));
        if (pRooms) {
            session->set_interested_room(*pRooms);
        }
        bucket.add_session(session.release());

        LOG(INFO) << "subscribe ok: " << bucket << " " << *bucket.get_session(key);
    }
};

}  // namespace sps


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
    return 0;
}
