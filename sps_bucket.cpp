#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/hash.h>
#include <butil/strings/string_split.h>
#include <brpc/server.h>
#include <brpc/restful.h>

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

}  // namespace sps
