#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/hash.h>
#include <butil/strings/string_split.h>
#include <brpc/server.h>
#include <brpc/restful.h>
#include <brpc/builtin/common.h>

#include "sps_bucket.h"


namespace sps {

ServerOptions::ServerOptions()
    : bucket_size(8)
    , suggested_room_count(128)
    , suggested_user_count(1024) {
}

Bucket::Bucket(int index, const ServerOptions& options)
    : index_(index) {
    CHECK_EQ(0, sessions_.init(options.suggested_user_count, 70));
    CHECK_EQ(0, rooms_.init(options.suggested_room_count, 70));
    LOG(INFO) << "create bucket[" << index_ << "] of"
              << " room=" << options.suggested_room_count
              << " user=" << options.suggested_user_count;
}

Bucket::~Bucket() {
    LOG(INFO) << "destory bucket[" << index_ << "]";
}

Room::Room(const RoomKey& rid)
    : key_(rid) {
    CHECK_EQ(0, sessions_.init(8, 70));
    LOG(INFO) << "create room[" << room_id() << "]";
}

Room::~Room() {
    LOG(INFO) << "destory room[" << room_id() << "]";
}

Session::Session(const UserKey& user, brpc::ProgressiveAttachment* pa)
    : key_(user.uid) {
    key_.device_type = user.device_type;
    writer_.reset(pa);
    created_us_ = butil::gettimeofday_us();
    written_us_ = 0;

    LOG(INFO) << "create session[" << key_.uid << "," << key_.device_type << "]";
}

Session::~Session() {
    LOG(INFO) << "destory session[" << key_.uid << "," << key_.device_type << "]";
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

void Bucket::add_session(Session* session) {
    Session::Ptr ps(session);
    std::vector<Room::Ptr> interested_rooms;

    {
        BAIDU_SCOPED_LOCK(mutex_);
        sessions_[ps->key_] = ps;
        // create room as needed
        for (std::vector<RoomKey>::const_iterator it = ps->interested_rooms_.begin();
             it != ps->interested_rooms_.end(); ++it) {
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
        (*it)->add_session(ps);
    }
}

void Room::add_session(Session::Ptr ps) {
    BAIDU_SCOPED_LOCK(mutex_);
    sessions_[ps->key_] = ps;
}

std::vector<RoomKey> Session::interested_rooms() const {
    BAIDU_SCOPED_LOCK(mutex_);
    return interested_rooms_;
}

void Session::Describe(std::ostream& os, const brpc::DescribeOptions&) const {
    os << "sps::Session { uid=" << key_.uid
       << " device_type=" << key_.device_type
       << " created_on=" << brpc::PrintedAsDateTime(created_us_)
       << " written_on=" << brpc::PrintedAsDateTime(written_us_);
    std::vector<RoomKey> rooms = interested_rooms();
    for (std::vector<RoomKey>::const_iterator it = rooms.begin();
         it != rooms.end(); ++it) {
        if (it == rooms.begin()) {
            os << " interested_room=";
        }
        os << it->room_id() << ",";
    }
    os << " }";
}

void Bucket::Describe(std::ostream& os, const brpc::DescribeOptions&) const {
    BAIDU_SCOPED_LOCK(mutex_);
    os << "sps::Bucket { index=" << index_
       << " sessions=" << sessions_.size()
       << " rooms=" << rooms_.size()
       << " }";
}

}  // namespace sps