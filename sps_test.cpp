#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <butil/logging.h>
#include <butil/hash.h>
#include <butil/strings/string_split.h>
#include <brpc/server.h>
#include <brpc/restful.h>

#include "sps_bucket.h"

using namespace sps;


class BucketTest : public testing::Test {
protected:
    void SetUp() override {
        b_ = new Bucket(0, ServerOptions());
    }
    void TearDown() override {
        delete b_;
    }

    Bucket* b_;
};

TEST_F(BucketTest, AddSession) {
    std::unique_ptr<Session> session(new Session(UserKey(100), nullptr));
    ASSERT_EQ(0, session->interested_rooms().size());
    LOG(INFO) << *session;

    session->set_interested_room("earth,mars");
    ASSERT_EQ(2, session->interested_rooms().size());
    ASSERT_EQ(RoomKey("earth"), session->interested_rooms()[0]);
    ASSERT_EQ(RoomKey("mars"), session->interested_rooms()[1]);
    LOG(INFO) << *session;

    b_->add_session(session.release());
    LOG(INFO) << *b_;
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
