#ifndef SPS_SERVER_H_
#define SPS_SERVER_H_

#include <memory>
#include <brpc/server.h>

#include "sps_bucket.h"


namespace sps {

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

}  // namespace sps

#endif  // SPS_SERVER_H_
