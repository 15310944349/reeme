#ifndef _REEME_EXTC_SQLITE_H__
#define _REEME_EXTC_SQLITE_H__

class DBSqlite
{
public:
	typedef MAP_CLASS_NAME<StringPtrKey, const char*> ColumnsMap;

	enum
	{
		kIndexNum = 1,
		kIndexName = 2,
		kIndexAll = 3
	};

	enum Action
	{
		kBegin,
		kCommit,
		kRollback
	} ;

	//!ֵ����
	enum ValueType
	{
		kValueInt = 1,
		kValueNumeric,
		kValueText,
		kValueBlob,
		kValueNull,
		kValueError,
	};

	//!ֵ��
	struct Value
	{
		ValueType		type;
		size_t			len;

		union {
			double		d;
			int32_t		i;
			int64_t		i64;
			const char	*s;
			const void	*p;
		} v;
	};

	//!�����
	class Result
	{
	public:
		virtual void reset() = 0;
		virtual bool next() = 0;
		virtual void drop() = 0;

		virtual const char* getColumnName(int32_t nIndex) const = 0;
		virtual ValueType getType(int32_t nIndex, uint32_t* pnValueLeng = 0) const = 0;

		virtual int32_t getInt(int32_t nIndex) const = 0;
		virtual int64_t getInt64(int32_t nIndex) const = 0;
		virtual double getDouble(int32_t nIndex) const = 0;
		virtual const char* getString(int32_t nIndex, uint32_t* pnValueLeng = 0) const = 0;
		virtual const void* getBlob(int32_t nIndex, uint32_t* pnValueLeng = 0) const = 0;

		virtual int32_t mapCol(const char* name) const = 0;

		inline int32_t getInt(const char* name) const
		{
			int32_t id = mapCol(name);
			if (id != -1)
				return getInt(id);
			return 0;
		}
		inline int64_t getInt64(const char* name) const
		{
			int32_t id = mapCol(name);
			if (id != -1)
				return getInt64(id);
			return 0;
		}
		inline double getDouble(const char* name) const
		{
			int32_t id = mapCol(name);
			if (id != -1)
				return getDouble(id);
			return 0;
		}
		inline const char* getString(const char* name, uint32_t* pnValueLeng = 0) const
		{
			int32_t id = mapCol(name);
			if (id != -1)
				return getString(id);
			return 0;
		}
		inline const void* getBlob(const char* name, uint32_t* pnValueLeng = 0) const
		{
			int32_t id = mapCol(name);
			if (id != -1)
				return getBlob(id);
			return 0;
		}		

		inline bool isNull(int32_t nIndex) const { return getType(nIndex) == kValueNull; }
		inline uint32_t getColsCount() const { return m_nColsCount; }

	protected:
		uint32_t			m_nColsCount;
	};

public:
	//!��ѯ������һ�������
	/*!
	* ִ��һ��SQL��������ִ����֮�󷵻�һ������������ԣ���SELECT���SQL��ʹ��exec��Ч�ʸ��ߡ�����ִ��֮ǰ��������һ��������Ϣ
	* \param sql SQL���
	* \param vals ����SQL�����ʹ��?���SQLITE���滻��ʹ�ñ�����������ֵ�����滻
	* \param count vals����Ĵ�С
	* \param rowIndexType ���صĽ������������ʽ��Ĭ�Ͻ��������ֺ�����������������ʱ�����Ϊ�����ܣ�ֻ��������������������Ч�ʿ��Ը�һ���
	* \return ����SQL�����Ľ������ֻҪSQLû�г������صĽ������Զ����ΪNULL
	*/
	virtual Result* query(const char* sql, const Value* vals = 0, uint32_t count = 0, uint32_t rowIndexType = kIndexAll) = 0;

	//!ִ��һ��SQL���
	/*!
	* �򵥵�ִ��һ��SQL��䣬����ȡ���ؽ���������ʺ�����UPDATE��DELETE��ALERT�����뷵�ؽ������䡣����ִ��֮ǰ��������һ��������Ϣ
	* \param sql SQL���
	* \param vals ����SQL�����ʹ��?���SQLITE���滻��ʹ�ñ�����������ֵ�����滻
	* \param count vals����Ĵ�С
	*/
	virtual bool exec(const char* sql, const Value* vals = 0, uint32_t count = 0) = 0;

	//!�������
	virtual void action(Action k) = 0;

	//!�޸ı�
	/*!
	* ����Sqlite����Ĳ��㣬ALERT TABLE��Ҫֻ����������ֶλ�����ֶ���������ļ򵥲��������Ҫɾ���ֶΣ�Ŀǰ����ʩ�еİ취ֻ�����ؽ�һ��COPY�����±�
	* ���ֶ����úã�Ȼ��ԭ�������COPY��ȥ���ٽ�ԭ��ɾ��������±�������������������Ѿ��ϴ���˲��������ܻ�ķѽϳ���ʱ���Լ��϶�Ĵ��룬��ˣ�
	* �����ṩ������һ���޸ı�ĺ��������������޸ı��Ч���Դﵽ���
	* \param tableName ���ڵı���
	* \param tmpTableName �Ѿ������õ��µĸ��Ʊ�����ƣ����������֮�󣬱������ƻᱻ�޸�Ϊ����1ָ���ı���
	* \param reservedColumns ��Ҫ�������ֶΡ������е��ֶ���StringPtrKey���룬��ָ�����ֶΣ��������ڸ���ʱ���������ԣ�����±���ڵ��ֶζ����ַ�������
	* û��ָ������Ҫ���±���б���ָ����Ĭ��ֵ������������ʱ�����
	* \return ����true��ʾ�����ɹ���������첽�����£�����Զ����true
	*/
	virtual bool updateTable(const char* tableName, const char* tmpTableName, const ColumnsMap& reservedColumns) = 0;

	//!��ȡ���һ�β����ID
	virtual uint64_t getLastInsertId() const = 0;

	//!�ر����ݿⲢdelete this
	virtual void destroy() = 0;
} ;

extern DBSqlite* wrapSqliteDB(void* dbOpened);
#ifdef _WINDOWS
extern DBSqlite* openSqliteDB(const wchar_t* path, int* r = 0);
#endif
extern DBSqlite* openSqliteDB(const char* path, int* r = 0);

#endif