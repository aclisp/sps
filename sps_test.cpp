#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/rand_util.h>
#include <butil/string_printf.h>
#include <bthread/bthread.h>
#include <bthread/unstable.h>
#include <brpc/server.h>

#include "sps_bucket.h"


DEFINE_int32(sps_test_concurrency, 10000, "the number of bthread that BucketTestMultiThreaded setup with");
DEFINE_int32(sps_test_room_pool_size, 10000, "the number of rooms that a session can join");
DEFINE_int32(sps_test_simulation_sec, 1, "the seconds (approximately) session simulation lasts");
DEFINE_int32(sps_test_dummy_server_port, -1, "the port of brpc dummy server. set to -1 does not start the dummy server.");


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

    Session::Ptr ps = bucket_->del_session(key);
    ASSERT_TRUE(!bucket_->get_session(key));
    ASSERT_FALSE(bucket_->get_session(key));
    ASSERT_FALSE(bucket_->get_room(RoomKey("earth")));
    ASSERT_FALSE(bucket_->get_room(RoomKey("mars")));

    // now change the session's interested room, and add it into bucket again.
    ps->set_interested_room("mercury,earth");
    bucket_->add_session(ps);
    ASSERT_TRUE(bucket_->get_room(RoomKey("earth")).get());
    ASSERT_TRUE(bucket_->get_room(RoomKey("mercury")).get());
    ASSERT_FALSE(bucket_->get_room(RoomKey("mars")));
    ASSERT_EQ(1, bucket_->get_room(RoomKey("earth"))->size());
    ASSERT_EQ(1, bucket_->get_room(RoomKey("mercury"))->size());
    ASSERT_FALSE(!bucket_->get_session(key));
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

class BucketTestMultiThreaded : public testing::Test {
protected:
    void SetUp() override {
        bucket_ = new Bucket(0, ServerOptions());
        start_ = false;
        stop_ = false;
        bthread_cond_init(&start_barrier_, NULL);
        bthread_mutex_init(&start_mutex_, NULL);
        for (int i=0; i<FLAGS_sps_test_concurrency; ++i) {
            bthread_t th;
            bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
            if (bthread_start_background(
                    &th, &attr, simulate_session, this) == 0) {
                threads_.push_back(th);
            } else {
                LOG(ERROR) << "can not create thread for simulate_session";
            }
        }
        for (int i=0; i<FLAGS_sps_test_room_pool_size; ++i) {
            rooms_.push_back(butil::string_printf("room%02d", i));
        }
    }

    void TearDown() override {
        stop_and_join();
        bthread_cond_destroy(&start_barrier_);
        bthread_mutex_destroy(&start_mutex_);
        delete bucket_;
    }

    void stop_and_join() {
        stop_ = true;
        for (std::vector<bthread_t>::iterator it = threads_.begin(); it != threads_.end(); ++it) {
            bthread_t th = *it;
            bthread_join(th, NULL);
        }
    }

    void start() {
        bthread_mutex_lock(&start_mutex_);
        start_ = true;
        bthread_cond_broadcast(&start_barrier_);
        bthread_mutex_unlock(&start_mutex_);
    }

    static void* simulate_session(void* arg) {
        BucketTestMultiThreaded* c = static_cast<BucketTestMultiThreaded*>(arg);
        bthread_mutex_lock(&c->start_mutex_);
        while (!c->start_) {
            bthread_cond_wait(&c->start_barrier_, &c->start_mutex_);
        }
        bthread_mutex_unlock(&c->start_mutex_);

        c->run_simulate_session();
        return NULL;
    }

    void run_simulate_session() {
        bthread_t self = bthread_self();
        std::vector<bthread_t>::iterator it = std::find(threads_.begin(), threads_.end(), self);
        int index = std::distance(threads_.begin(), it);
        UserKey key(index);
        while (!stop_) {
            std::unique_ptr<Session> session(new Session(key, nullptr));
            int i1 = butil::RandInt(0, rooms_.size()-1);
            int i2 = butil::RandInt(i1, std::min(i1+5, int(rooms_.size()-1)));
            int from = std::min(i1, i2);
            int to = std::max(i1, i2);
            std::ostringstream oss;
            for (int i = from; i < to; ++i) {
                oss << rooms_[i] << ",";
            }
            session->set_interested_room(oss.str());
            bucket_->add_session(session.release());
            bthread_usleep(butil::RandInt(100, 1000));
            bucket_->del_session(key);
            bthread_usleep(butil::RandInt(100, 1000));
        }
    }

    Bucket* bucket_;
    bool start_;
    bthread_cond_t start_barrier_;
    bthread_mutex_t start_mutex_;
    std::vector<bthread_t> threads_;
    std::vector<std::string> rooms_;
    volatile bool stop_;
};

TEST_F(BucketTestMultiThreaded, Simulate_Session) {
    start();
    for (int i=0; i<FLAGS_sps_test_simulation_sec*5; ++i) {
        bthread_usleep(200000);
        LOG(INFO) << *bucket_;
    }
    stop_and_join();
    LOG(INFO) << *bucket_;
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::SetUsageMessage("sps testing module");
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_sps_test_dummy_server_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_sps_test_dummy_server_port);
    }
    return RUN_ALL_TESTS();
}
