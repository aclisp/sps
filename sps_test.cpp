#include <gtest/gtest.h>
#include <butil/logging.h>

#include "sps_bucket.h"


using namespace sps;


class BucketTest : public testing::Test {
protected:
    void SetUp() override {
        bucket_ = new Bucket(0, ServerOptions());
    }
    void TearDown() override {
        delete bucket_;
    }

    Bucket* bucket_;
};

TEST_F(BucketTest, Add_Session) {
    UserKey key(__LINE__);
    std::unique_ptr<Session> session(new Session(key, nullptr));
    ASSERT_EQ(0, session->interested_rooms().size());
    LOG(INFO) << *session;

    session->set_interested_room("earth,mars");
    ASSERT_EQ(2, session->interested_rooms().size());
    ASSERT_EQ(RoomKey("earth"), session->interested_rooms()[0]);
    ASSERT_EQ(RoomKey("mars"), session->interested_rooms()[1]);
    LOG(INFO) << *session;

    bucket_->add_session(session.release());
    ASSERT_TRUE(bucket_->get_room(RoomKey("earth"))->has_session(bucket_->get_session(key)));
    ASSERT_TRUE(bucket_->get_room(RoomKey("mars"))->has_session(bucket_->get_session(key)));
    LOG(INFO) << *bucket_;
}

TEST_F(BucketTest, Del_Session) {
    UserKey key(__LINE__);
    std::unique_ptr<Session> session(new Session(key, nullptr));
    session->set_interested_room("earth,mars");
    bucket_->add_session(session.release());
    bucket_->del_session(key);
    ASSERT_FALSE(bucket_->get_session(key));
    ASSERT_FALSE(bucket_->get_room(RoomKey("earth")));
    ASSERT_FALSE(bucket_->get_room(RoomKey("mars")));
}

TEST_F(BucketTest, Add_and_Del_Session) {
    UserKey key1(__LINE__);
    UserKey key2(__LINE__);
    UserKey key3(__LINE__);

    std::unique_ptr<Session> session1(new Session(key1, nullptr));
    std::unique_ptr<Session> session2(new Session(key2, nullptr));
    std::unique_ptr<Session> session3(new Session(key3, nullptr));

    session1->set_interested_room("earth,mars");
    session2->set_interested_room("mars");
    session3->set_interested_room("earth,mars");

    bucket_->add_session(session1.release());
    bucket_->add_session(session2.release());
    ASSERT_FALSE(bucket_->get_room(RoomKey("mercury")));
    ASSERT_TRUE (bucket_->get_room(RoomKey("earth"))->has_session(bucket_->get_session(key1)));
    ASSERT_FALSE(bucket_->get_room(RoomKey("earth"))->has_session(bucket_->get_session(key2)));
    ASSERT_TRUE (bucket_->get_room(RoomKey("mars"))->has_session(bucket_->get_session(key1)));
    ASSERT_TRUE (bucket_->get_room(RoomKey("mars"))->has_session(bucket_->get_session(key2)));
    LOG(INFO) << *bucket_;

    bucket_->del_session(key1);
    ASSERT_TRUE(bucket_->get_room(RoomKey("mars")).get());
    ASSERT_TRUE(bucket_->get_room(RoomKey("mars"))->has_session(bucket_->get_session(key2)));
    LOG(INFO) << *bucket_;

    bucket_->add_session(session3.release());
    bucket_->del_session(key2);
    ASSERT_TRUE (bucket_->get_room(RoomKey("earth"))->has_session(bucket_->get_session(key3)));
    ASSERT_TRUE (bucket_->get_room(RoomKey("mars"))->has_session(bucket_->get_session(key3)));
    LOG(INFO) << *bucket_;
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
