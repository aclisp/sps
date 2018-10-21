#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/hash.h>
#include <butil/strings/string_split.h>
#include <brpc/server.h>
#include <brpc/restful.h>

#include "sps_bucket.h"
#include "sps.pb.h"

DEFINE_int32(port, 8080, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(certificate, "insecure.crt", "Certificate file path to enable SSL");
DEFINE_string(private_key, "insecure.key", "Private key file path to enable SSL");

namespace sps {

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
