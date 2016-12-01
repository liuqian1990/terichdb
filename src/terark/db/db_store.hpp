#ifndef __terark_db_db_store_hpp__
#define __terark_db_db_store_hpp__

#include "db_conf.hpp"
#include "db_context.hpp"
#include <boost/filesystem.hpp>

namespace boost { namespace filesystem {
	inline path operator+(const path& x, terark::fstring y) {
		path z = x;
		z.concat(y.begin(), y.end());
		return z;
	}
	inline path operator+(const path& x, const char* y) {
		path z = x;
		z.concat(y, y + strlen(y));
		return z;
	}
//	class path;
}}

namespace terark {
class rank_select_se;
} // namespace terark

namespace terark { namespace db {

typedef const boost::filesystem::path& PathRef;

class TERARK_DB_DLL Permanentable : public RefCounter {
	TERARK_DB_NON_COPYABLE_CLASS(Permanentable);
public:
	Permanentable();
	~Permanentable();
	///@ object can hold a m_path, when path==m_path, it is a flush
	virtual void save(PathRef path) const = 0;

	virtual void load(PathRef path) = 0;
};
typedef boost::intrusive_ptr<class Permanentable> PermanentablePtr;
typedef boost::intrusive_ptr<class ReadableStore> ReadableStorePtr;

class TERARK_DB_DLL StoreIterator : public RefCounter {
protected:
	ReadableStorePtr m_store;
public:
	ReadableStore* getStore() const { return m_store.get(); }
	virtual ~StoreIterator();
	virtual bool increment(llong* id, valvec<byte>* val) = 0;
	virtual bool seekExact(llong  id, valvec<byte>* val) = 0;
	virtual llong seekLowerBound(llong  id, valvec<byte>* val);
	virtual void reset() = 0;
};
typedef boost::intrusive_ptr<StoreIterator> StoreIteratorPtr;

class TERARK_DB_DLL ForwardPartStoreIterator : public StoreIterator {
protected:
    DbContext *m_ctx;
    size_t m_where;
    size_t m_baseId;
    const bm_uint_t* m_isDel;
    const rank_select_se* m_isPurged;
public:
    ForwardPartStoreIterator(ReadableStore* store, size_t baseId, const bm_uint_t* isDel,
                             const rank_select_se* isPurged, DbContext *ctx);
	virtual ~ForwardPartStoreIterator();
	bool increment(llong* id, valvec<byte>* val) override;
	bool seekExact(llong  id, valvec<byte>* val) override;
	void reset() override;
};
typedef boost::intrusive_ptr<ForwardPartStoreIterator> ForwardPartStoreIteratorPtr;

class TERARK_DB_DLL AppendableStore;
class TERARK_DB_DLL UpdatableStore;
class TERARK_DB_DLL WritableStore;
class TERARK_DB_DLL ReadableIndex;
class TERARK_DB_DLL ReadableStore : virtual public Permanentable {
	TERARK_DB_NON_COPYABLE_CLASS(ReadableStore);
protected:
	byte_t* m_recordsBasePtr;
public:
    bool    m_isFreezed;
public:
	struct TERARK_DB_DLL RegisterStoreFactory {
		typedef std::function<ReadableStore*(const Schema&)> StoreFactory;
		RegisterStoreFactory(const char* fnameSuffix, const StoreFactory&);
	};
#define TERARK_DB_REGISTER_STORE(suffix, StoreClass) \
	static ReadableStore::RegisterStoreFactory \
		regStore_##StoreClass(suffix, [](const Schema& schema){ return new StoreClass(schema); });

	static ReadableStore*
	openStore(const Schema& schema, PathRef segDir, fstring fname);

	ReadableStore();
	~ReadableStore();
	inline  byte* getRecordsBasePtr() const { return m_recordsBasePtr; }
    virtual llong dataFileSize() const; // these two functions for cheap purge , return 0 to force rebuild
    virtual llong dataDictSize() const; // if DictSize * cheapPurgeMultiple >= FileSize , rebuild
	virtual llong dataStorageSize() const = 0;
	virtual llong dataInflateSize() const = 0;
	virtual llong numDataRows() const = 0;
	virtual void getValueAppend(llong id, valvec<byte>* val, DbContext*) const = 0;
	virtual void deleteFiles();
	virtual StoreIterator* createStoreIterForward(DbContext*) const = 0;
	virtual StoreIterator* createStoreIterBackward(DbContext*) const = 0;
	virtual WritableStore* getWritableStore();
	virtual ReadableIndex* getReadableIndex();
	virtual AppendableStore* getAppendableStore();
	virtual UpdatableStore* getUpdatableStore();

    virtual void setStorePath(PathRef);

	void getValue(llong id, valvec<byte>* val, DbContext* ctx) const {
		val->risk_set_size(0);
		getValueAppend(id, val, ctx);
	}

	StoreIterator* createDefaultStoreIterForward(DbContext*) const;
	StoreIterator* createDefaultStoreIterBackward(DbContext*) const;

	StoreIterator* ensureStoreIterForward(DbContext*) const;
	StoreIterator* ensureStoreIterBackward(DbContext*) const;

    virtual void markFrozen();
};

class TERARK_DB_DLL AppendableStore {
public:
	virtual ~AppendableStore();
	virtual llong append(fstring row, DbContext*) = 0;
	virtual void  shrinkToFit() = 0;
    virtual void  shrinkToSize(size_t size) = 0;
};

class TERARK_DB_DLL UpdatableStore {
public:
	virtual ~UpdatableStore();
	virtual void update(llong id, fstring row, DbContext*) = 0;
};

class TERARK_DB_DLL WritableStore : public AppendableStore, public UpdatableStore {
public:
	virtual ~WritableStore();
	virtual void remove(llong id, DbContext*) = 0;
};

class TERARK_DB_DLL MultiPartStore : public ReadableStore {
	class MyStoreIterForward;	friend class MyStoreIterForward;
	class MyStoreIterBackward;	friend class MyStoreIterBackward;

public:
	explicit MultiPartStore();
	~MultiPartStore();

	llong dataInflateSize() const override;
	llong dataStorageSize() const override;
	llong numDataRows() const override;
	void getValueAppend(llong id, valvec<byte>* val, DbContext*) const override;
	StoreIterator* createStoreIterForward(DbContext*) const override;
	StoreIterator* createStoreIterBackward(DbContext*) const override;

	void load(PathRef segDir) override;
	void save(PathRef segDir) const override;

	size_t numParts() const { return m_parts.size(); }
	ReadableStore* getPart(size_t i) const { return m_parts[i].get(); }

    void setStorePath(PathRef) override;

	void addpart(ReadableStore* store);
	void addpartIfNonEmpty(ReadableStore* store);
	ReadableStore* finishParts();

private:
	void syncRowNumVec();

//	SchemaPtr     m_schema;
	valvec<uint32_t> m_rowNumVec;  // parallel with m_parts
	valvec<ReadableStorePtr> m_parts; // partition of row set
};
typedef boost::intrusive_ptr<MultiPartStore> MultiPartStorePtr;

#ifdef _MSC_VER
//warning C4275: non dll-interface class 'std::logic_error' used as base for dll-interface class 'terark::db::ReadRecordException'
#pragma warning(disable:4275)
#endif
class TERARK_DB_DLL DbException : public std::logic_error {
public:
	template<class String>
	DbException(const String& msg) : logic_error(msg) {}
//	using std::logic_error::logic_error;
};
class TERARK_DB_DLL ReadRecordException : public DbException {
public:
	~ReadRecordException();
	ReadRecordException(const char* errType, const std::string& segDir, llong baseId, llong subId);
	ReadRecordException(const ReadRecordException&);
	ReadRecordException& operator=(const ReadRecordException&);
	std::string m_segDir;
	llong m_baseId;
	llong m_subId;
};
class TERARK_DB_DLL ReadDeletedRecordException : public ReadRecordException {
public:
	ReadDeletedRecordException(const std::string& segDir, llong baseId, llong subId);
};
class TERARK_DB_DLL ReadUncommitedRecordException : public ReadRecordException {
public:
	ReadUncommitedRecordException(const std::string& segDir, llong baseId, llong subId);
};

class TERARK_DB_DLL CommitException : public DbException {
public:
	template<class String>
	CommitException(const String& msg) : DbException(msg) {}
//	using DbException::DbException;
};

class TERARK_DB_DLL NeedRetryException : public DbException {
public:
	template<class String>
	NeedRetryException(const String& msg) : DbException(msg) {}
//	using DbException::DbException;
};

class TERARK_DB_DLL WriteThrottleException : public DbException {
public:
	template<class String>
	WriteThrottleException(const String& msg) : DbException(msg) {}
//	using DbException::DbException;
};

class TERARK_DB_DLL StoreInternalException : public DbException {
public:
	template<class String>
    StoreInternalException(const String& msg) : DbException(msg) {}
//	using DbException::DbException;
};

} } // namespace terark::db

#endif // __terark_db_db_store_hpp__
