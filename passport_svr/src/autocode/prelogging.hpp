// generated by pktcoder.
class req_version : public Packet {
public:
    enum { ID = 101 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        u32 data;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<u32>::save(_vobj, "data", data);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<u32>::load(_vobj, "data", data);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.data;
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.data;
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class req_version_update_ok : public Packet {
public:
    enum { ID = 102 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        u32 data;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<u32>::save(_vobj, "data", data);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<u32>::load(_vobj, "data", data);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.data;
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.data;
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class req_siginin : public Packet {
public:
    enum { ID = 103 };
    virtual u32 pid() const { return ID; }
};
class req_login : public Packet {
public:
    enum { ID = 104 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        u64 low;
        u64 high;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<u64>::save(_vobj, "low", low);
            PacketDataHelper<u64>::save(_vobj, "high", high);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<u64>::load(_vobj, "low", low);
            PacketDataHelper<u64>::load(_vobj, "high", high);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.low;
        s << v.high;
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.low;
        s >> v.high;
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class req_login_account : public Packet {
public:
    enum { ID = 105 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        str8 data;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<str8>::save(_vobj, "data", data);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<str8>::load(_vobj, "data", data);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.data;
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.data;
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class req_login_PPID : public Packet {
public:
    enum { ID = 106 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        str8 ppid;
        str8 accessToken;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<str8>::save(_vobj, "ppid", ppid);
            PacketDataHelper<str8>::save(_vobj, "accessToken", accessToken);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<str8>::load(_vobj, "ppid", ppid);
            PacketDataHelper<str8>::load(_vobj, "accessToken", accessToken);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.ppid;
        s << v.accessToken;
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.ppid;
        s >> v.accessToken;
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class res_version_ok : public Packet {
public:
    enum { ID = 101 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        u8 data;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<u8>::save(_vobj, "data", data);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<u8>::load(_vobj, "data", data);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.data;
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.data;
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class res_version_refused : public Packet {
public:
    enum { ID = 102 };
    virtual u32 pid() const { return ID; }
};
class res_version_update : public Packet {
public:
    enum { ID = 103 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        u32 version;
        unsigned int file_size;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<u32>::save(_vobj, "version", version);
            PacketDataHelper<unsigned int>::save(_vobj, "file_size", file_size);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<u32>::load(_vobj, "version", version);
            PacketDataHelper<unsigned int>::load(_vobj, "file_size", file_size);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << v.version;
        s << DataStream::ccu(v.file_size);
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        s >> v.version;
        s >> DataStream::cu(v.file_size);
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class res_file_data : public Packet {
public:
    enum { ID = 104 };
    virtual u32 pid() const { return ID; }
    struct _Data {
        std::vector<u8> data;
        void _save(Variable_object &_vobj) const {
            PacketDataHelper<u8>::save(_vobj, "data", data);
        }
        void _load(Variable_object &_vobj) {
            PacketDataHelper<u8>::load(_vobj, "data", data);
        }
    } data;
    virtual void save(DataStream &s) const {
        auto &v = data;
        s << DataStream::ccu(v.data.size());
        if (!v.data.empty()) s << DataStream::cbuf(v.data.data(), v.data.size() * sizeof(u8));
    }
    virtual void load(DataStream &s) {
        auto &v = data;
        size_t c; (void)c;
        s >> DataStream::cu(c);
        v.data.resize(c);
        if (c) s >> DataStream::buf(v.data.data(), c * sizeof(u8));
    }
    virtual void save(Variable_object &vobj) const { data._save(vobj); }
    virtual void load(Variable_object &vobj) { data._load(vobj); }
};
class res_login_failed : public Packet {
public:
    enum { ID = 105 };
    virtual u32 pid() const { return ID; }
};
