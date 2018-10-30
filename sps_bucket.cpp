#include "sps_bucket.h"

#include <butil/logging.h>
#include <butil/strings/string_split.h>
#include <brpc/builtin/common.h>
#include <bthread/unstable.h>


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
    VLOG(1) << "create bucket[" << index_ << "] of"
              << " room=" << options.suggested_room_count
              << " user=" << options.suggested_user_count;
}

Bucket::~Bucket() {
    VLOG(1) << "destroy bucket[" << index_ << "]";
}

Room::Room(const RoomKey& key)
    : key_(key) {
    CHECK_EQ(0, sessions_.init(8, 70));
    VLOG(1) << "create room[" << room_id() << "]";
}

Room::~Room() {
    VLOG(1) << "destroy room[" << room_id() << "]";
}

Session::Session(const UserKey& key, brpc::ProgressiveAttachment* pa, int anti_idle_s)
    : key_(key)
    , writer_(pa)
    , created_us_(butil::gettimeofday_us())
    , written_us_(created_us_)
    , anti_idle_us_(anti_idle_s*1000000L)
    , has_anti_idle_timer_(false) {
    if (anti_idle_us_ > 0) {
        // add a ref for OnAntiIdleTimer which does de-ref.
        Session::Ptr add_ref(this);
        int err = bthread_timer_add(&anti_idle_timer_id_,
                butil::microseconds_to_timespec(created_us_ + anti_idle_us_),
                OnAntiIdleTimer, this);
        if (err) {
            LOG(WARNING) << "fail to create timer: " << berror(err);
        } else {
            add_ref.detach();
            has_anti_idle_timer_ = true;
        }
    }
    VLOG(1) << "create session[" << key_.uid << "," << key_.device_type << "]";
}

Session::~Session() {
    VLOG(1) << "destroy session[" << key_.uid << "," << key_.device_type << "]";
}

void Session::Destroy() {
    if (has_anti_idle_timer_) {
        if (bthread_timer_del(anti_idle_timer_id_) == 0) {
            // The callback is not run yet. Remove the additional ref added
            // before creating the timer.
            Session::Ptr de_ref(this, false);
        }
    }
}

void Session::OnAntiIdleTimer(void* arg) {
    // hold the referenced session.
    Session::Ptr ps(static_cast<Session*>(arg), false/*not add ref*/);
    int64_t written_us = ps->written_us_;
    int64_t now_us = butil::gettimeofday_us();
    if ((now_us - written_us) >= ps->anti_idle_us_) {
        if (ps->writer_) {  // writer could be null when testing
            if (0 != ps->writer_->Write("\r\n", 2)) {
                int err = errno;
                LOG(WARNING) << "fail write to " << *ps << " (" << berror(err) << ") "
                             << "you probably forget to delete anti-idle timer for this session.";
                return;
            }
        }
        ps->written_us_ = now_us;
        written_us = now_us;
    }

    // set up the next timer
    int err = bthread_timer_add(&ps->anti_idle_timer_id_,
            butil::microseconds_to_timespec(written_us + ps->anti_idle_us_),
            OnAntiIdleTimer, ps.get());
    if (err) {
        LOG(WARNING) << "fail to create timer: " << berror(err);
        ps->has_anti_idle_timer_ = false;
    } else {
        ps.detach();
    }
}

int Session::Write(const butil::IOBuf& data) {
    int res;
    if (0 == writer_->Write(data)) {
        res = 0;
    } else {
        res = errno;
    }
    written_us_ = butil::gettimeofday_us();
    return res;
}

void Room::Write(const butil::IOBuf& data) {
    BAIDU_SCOPED_LOCK(mutex_);
    for (Session::Map::iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
        Session::Ptr session = it->second;
        int err = session->Write(data);
        if (err) {
            LOG(WARNING) << "fail write to " << *session << " " << berror(err);
        }
    }
}

void Session::set_interested_room(const std::string& rooms) {
    BAIDU_SCOPED_LOCK(mutex_);
    interested_rooms_.clear();
    std::vector<std::string> pieces;
    butil::SplitString(rooms, ',', &pieces);
    for (const std::string& s : pieces) {
        if (s.empty()) continue;
        interested_rooms_.emplace_back(RoomKey(s));
    }
}

void Bucket::add_session(Session* session) {
    CHECK(session != nullptr);

    Session::Ptr ps(session);
    add_session(ps);
}

void Bucket::add_session(Session::Ptr ps) {
    CHECK(ps.get() != nullptr);
    Session::Ptr old_ps = del_session(ps->key());
    if (old_ps) {
        old_ps->Destroy();
        LOG(WARNING) << "removed existing session: " << *old_ps;
    }

    std::vector<RoomKey> room_keys = ps->interested_rooms();
    std::vector<Room::Ptr> interested_rooms;

    {
        BAIDU_SCOPED_LOCK(mutex_);
        sessions_[ps->key()] = ps;
        // create room as needed
        for (const RoomKey& key : room_keys) {
            Room::Ptr room = rooms_[key];
            if (!room) {
                room.reset(new Room(key));
                rooms_[key] = room;
            }
            interested_rooms.push_back(room);
        }
    }

    for (Room::Ptr pr : interested_rooms) {
        pr->add_session(ps);
    }
}

Session::Ptr Bucket::del_session(const UserKey &key) {
    Session::Ptr ps = get_session(key);
    if (!ps) {
        return ps;
    }

    std::vector<RoomKey> room_keys = ps->interested_rooms();
    std::vector< std::pair<Room::Ptr, bool> > interested_rooms;

    {
        BAIDU_SCOPED_LOCK(mutex_);
        for (const RoomKey& key : room_keys) {
            Room::Ptr* ppr = rooms_.seek(key);
            if (ppr) {
                interested_rooms.push_back(std::make_pair(*ppr, false));
            }
        }
    }

    for (std::pair<Room::Ptr, bool>& pair : interested_rooms) {
        pair.second = (pair.first)->del_session(ps);
    }

    {
        BAIDU_SCOPED_LOCK(mutex_);
        for (std::pair<Room::Ptr, bool>& pair : interested_rooms) {
            if (pair.second) {
                rooms_.erase(pair.first->key());
            }
        }
        sessions_.erase(key);
    }

    return ps;
}

Session::Ptr Bucket::get_session(const UserKey& key) const {
    BAIDU_SCOPED_LOCK(mutex_);
    Session::Ptr* pps = sessions_.seek(key);
    if (pps == NULL) {
        return Session::Ptr();
    } else {
        return *pps;
    }
}

Room::Ptr Bucket::get_room(const RoomKey& key) const {
    BAIDU_SCOPED_LOCK(mutex_);
    Room::Ptr* ppr = rooms_.seek(key);
    if (ppr == NULL) {
        return Room::Ptr();
    } else {
        return *ppr;
    }
}

size_t Bucket::count_session() const {
    BAIDU_SCOPED_LOCK(mutex_);
    return sessions_.size();
}

size_t Bucket::count_room() const {
    BAIDU_SCOPED_LOCK(mutex_);
    return rooms_.size();
}

void Room::add_session(Session::Ptr ps) {
    CHECK(ps.get() != nullptr);

    BAIDU_SCOPED_LOCK(mutex_);
    sessions_[ps->key()] = ps;
}

bool Room::del_session(Session::Ptr ps) {
    CHECK(ps.get() != nullptr);

    BAIDU_SCOPED_LOCK(mutex_);
    sessions_.erase(ps->key());
    return sessions_.empty();
}

size_t Room::size() const {
    BAIDU_SCOPED_LOCK(mutex_);
    return sessions_.size();
}

bool Room::has_session(Session::Ptr ps) const {
    CHECK(ps.get() != nullptr);

    BAIDU_SCOPED_LOCK(mutex_);
    return sessions_.seek(ps->key()) != NULL;
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
       << " rooms=" << rooms_.size();
    size_t crowded = 0;
    for (Room::Map::const_iterator it = rooms_.begin(); it != rooms_.end(); ++it) {
        Room::Ptr room = it->second;
        if (room->size() > crowded) {
            crowded = room->size();
        }
    }
    os << " crowded=" << crowded;
    os << " }";
}

void Bucket::update_session_rooms(const UserKey& key, const std::string& new_rooms) {
    if (session_rooms_unchanged(key, new_rooms)) {
        return;
    }

    Session::Ptr ps = del_session(key);
    if (!ps) {
        return;
    }

    ps->set_interested_room(new_rooms);
    add_session(ps);
}

bool Bucket::session_rooms_unchanged(const UserKey& key, const std::string& new_rooms) const {
    Session::Ptr ps = get_session(key);
    if (!ps) {
        return true;
    }

    std::string cur_rooms;
    for (const RoomKey& key : ps->interested_rooms()) {
        cur_rooms += key.room_id();
        cur_rooms += ",";
    }
    if (!cur_rooms.empty()) {
        cur_rooms.pop_back();  // remove the trailing comma
    }
    if (cur_rooms == new_rooms) {
        return true;
    }
    return false;
}

}  // namespace sps
