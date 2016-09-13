#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

#include "reeme.h"

#include "lua.hpp"
#include "re2/re2.h"
#include "re2/regexp.h"
#include "zlib/zlib.h"
#include "crtopt.h"

#include "json.h"
#include "lua_utf8str.h"
#include "lua_string.h"
#include "lua_table.h"
#include "sql.h"
#include "lua_httpreq.h"

#include "commonlib.h"
#include "lua_deamon.h"
#include "sqlite/sqlitewrap.h"

// log4z, get from : https://github.com/zsummer/log4z , thanks
#include "log4z.h"
#include "packets.h"
#include "deamon.h"
#include "win_curlwrap.h"

ILog4zManager* logs = 0;
Service* pService = 0;

#include "sqls.h"
#include "pckexec.h"
#include "console_windows.h"

//////////////////////////////////////////////////////////////////////////
Client::Client(Service* s)
	: service(s)
	, sock(*s->ioService)
{
}

void Client::onReadHeader(const boost::system::error_code& error)
{
	if (error)
	{
		service->removeClient(this, &error);
		return ;
	}

	if (pckHeader.bodyLeng > service->cfgMaxPacketSize)
	{
		service->removeClient(this, NULL);
		return ;
	}

	readLength(pckHeader, pckHeader.bodyLeng);	
}

void Client::onReadData(char* mem, const boost::system::error_code& error)
{
	if (error)
	{
		service->removeClient(this, &error);
		return;
	}

	PckHeader* hd = (PckHeader*)mem;
	char* dst = (char*)(hd + 1);

	if (CRC32Check(dst, hd->bodyLeng) != hd->crc32)
	{
		free(hd);
		service->removeClient(this, NULL);
		return ;
	}


	service->pushPacket(hd);

	start();
}

//////////////////////////////////////////////////////////////////////////
void Service::run()
{
	LOGI("TaskDeamon started");

	bRunning = true;
	while(bEventLooping)
	{
		try
		{
			boost::system::error_code ec;
			ioService->run(ec);

			if (ec)
				LOGW("Run Error: " << ec);
			break;
		}
		catch (std::exception & ex)
		{
			LOGW("Run Catched Exception: " << ex.what());
		}
	}

	// ������ȫ�������ȴ��������˳�
	boost::system::error_code ec;
	acceptor->close(ec);

	clientsListLock.lock();
	Client* c;
	while(c = clients.popFirst())
	{
		c->stop();
		delete c;
	}
	clientsListLock.unlock();

	threadsListLock.lock();
	WorkThread* wt;
	for(size_t i = 0; i < workThreads.size(); ++ i)
	{
		boost::recursive_mutex::scoped_lock lock(taskCondLock);
		taskCond.notify_one();
	}
	while(wt = workThreads.popFirst())
	{
		taskCond.notify_one();
		wt->waitExit();
		delete wt;
	}
	threadsListLock.unlock();

	ioService->reset();

	// ����û��������İ��ڴ�
	flushPackets();

	bRunning = false;
}

void Service::stop()
{
	bEventLooping = false;
	ioService->stop();
}

bool Service::openDB(const char* dbpath)
{
	int r;	

	if (dbpath[0] == '?')
	{
		PATH_STRING strPath;
		getTaskDeamonAtPath(strPath, dbpath + 1);
		db = openSqliteDB(strPath.c_str(), &r);
	}
	else
	{
		db = openSqliteDB(dbpath, &r);
	}

	if (!db)
	{
		LOGFMTE("Open TaskDeamon Database at path [%s] failed, error code: %d", dbpath, r);
		return false;
	}

	uint32_t flags = 0xFFFF;
	const char* sqls[] = { SQLCreateTable_Schedule, SQLCreateTable_HttpReqTasks, SQLCreateTable_ScriptTasks };

	DBSqlite::Result* result = db->query("SELECT name FROM sqlite_master WHERE type = 'table'", 0, 0, DBSqlite::kIndexNum);
	if (!result)
		return false;

	while(result->next())
	{			
		const char* s = result->getString(0);
		if (strcmp(s, "schedules") == 0)
			flags &= ~(1 << 0);
		else if (strcmp(s, "httpreq_tasks") == 0)
			flags &= ~(1 << 1);
		else if (strcmp(s, "scriptrun_tasks") == 0)
			flags &= ~(1 << 2);
	}
	result->drop();

	// ����Ҫִ�е�ִ��
	for (uint32_t i = 0; i < sizeof(sqls) / sizeof(const char*); ++ i)
	{
		if (flags & (1 << i))
		{
			if (!db->exec(sqls[i]))
				return false;
		}
	}

	return true;
}

void Service::flushPackets()
{
	pcksListLock.lock();
	while(packets.size())
	{
		PckHeader* hd = packets.front();
		packets.pop_front();

		packetExecute(this, L, hd, true);
		free(hd);			
	}
	pcksListLock.unlock();
}

void Service::pushPacket(PckHeader* hd)
{
	pcksListLock.lock();
	packets.push_back(hd);
	pcksListLock.unlock();

	boost::recursive_mutex::scoped_lock lock(taskCondLock);
	taskCond.notify_one();
}

bool Service::listenAt(const char* ip, lua_Integer port)
{
	try
	{
		boost::asio::ip::tcp::resolver resolver(*ioService);
		boost::asio::ip::tcp::resolver::query query(
			ip,
			boost::lexical_cast< std::string >(port)
			);
		boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);

		acceptor->open(endpoint.protocol());
		acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(false));
		acceptor->bind(endpoint);
		acceptor->listen(boost::asio::socket_base::max_connections);

		acceptOne();			
	}
	catch (std::exception & ex)
	{
		LOGFMTE("Listen at %s:%u failed\n", ip, (unsigned short)port);
		return false;
	}

	return true;
}

void Service::acceptOne()
{
	Client* c = new Client(this);
	acceptor->async_accept(c->sock, boost::bind(&Service::onAccept, this, c, boost::asio::placeholders::error));
}

void Service::removeClient(Client* c, const boost::system::error_code* err)
{
	clientsListLock.lock();
	bool ok = clients.remove(c);
	clientsListLock.unlock();

	if (ok)
	{
		c->stop();
		delete c;
	}
}

void Service::onAccept(Client* c, const boost::system::error_code& error)
{
	if (error)
	{
		delete c;
		return ;
	}

	clientsListLock.lock();
	clients.append(c);
	clientsListLock.unlock();

	c->start();

	acceptOne();
}

void Service::addSchedule(ScheduleData* p)
{
	scheduleLock.lock();
	ScheduleData* n = schedules.first(), *before = NULL;
	while(n)
	{
		if (p->startTime + 1 <= n->startTime)
			break;

		before = n;
		n = n->next();
	}
	if (n)
		schedules.insertBefore(p, before);
	else
		schedules.append(p);
	scheduleLock.unlock();

	boost::recursive_mutex::scoped_lock lock(taskCondLock);
	taskCond.notify_one();
}

bool Service::parserStartup(const char* pszStartupArgs, size_t nCmdLength)
{
	// ������������һ��JSON�����ص�strStartupJson�ַ�����
	bool bInQuote = false;
	std::string strStartupJson;

	if (pszStartupArgs[0] == '"')
	{
		if (pszStartupArgs[nCmdLength - 1] != '"')
			return false;

		bInQuote = true;
		pszStartupArgs ++;
		nCmdLength -= 2;
	}

	if (strncmp(pszStartupArgs, "json:", 5) == 0)
	{
		pszStartupArgs += 5;
		nCmdLength -= 5;

		if (bInQuote)
		{
			strStartupJson.reserve(nCmdLength);
			for (size_t i = 0; i < nCmdLength; ++ i)
			{
				char ch = pszStartupArgs[i];
				if (ch == '\\')
				{
					++ i;
					continue;
				}

				strStartupJson += ch;
			}
		}
		else
			strStartupJson.append(pszStartupArgs, nCmdLength);
	}
	else if (strncmp(pszStartupArgs, "file:", 5) == 0)
	{
		strStartupJson.append(pszStartupArgs + 5, nCmdLength);
		FILE* fp = fopen(strStartupJson.c_str(), "rt");
		if (!fp)
		{
			LOGFMTE("Open startup file \"%s\" failed\n", pszStartupArgs + 5);
			return false;
		}

		fseek(fp, 0L, SEEK_END);
		long s = ftell(fp);
		fseek(fp, 0L, SEEK_SET);

		strStartupJson.clear();
		if (s > 2)
		{				
			strStartupJson.resize(s);
			fread(const_cast<char*>(strStartupJson.c_str()), 1, s, fp);;
		}
		fclose(fp);
	}

	if (strStartupJson.size() < 2)
	{
		LOGE("Invalid startup json data");
		return false;
	}

	// ��ʼ��LuaJIT����صĺ�����
	L = luaL_newstate();
	luaL_openlibs(L);
	initCommonLib(L);

	// ����JSON
	lua_pushcfunction(L, &lua_string_json);
	lua_pushlstring(L, strStartupJson.c_str(), strStartupJson.length());
	lua_pcall(L, 1, 1, 0);
	int jsonIdx = lua_gettop(L);
	if (!lua_istable(L, jsonIdx))
		return false;


	// �������ĵ�ַ����
	lua_Integer port = 5918;
	const char* host = "127.0.0.1";

	lua_getfield(L, jsonIdx, "listen");
	if (lua_istable(L, -1))
	{
		lua_getfield(L, jsonIdx, "host");
		host = luaL_optstring(L, -1, "127.0.0.1");

		lua_getfield(L, jsonIdx, "port");
		port = luaL_optinteger(L, -1, 5918);
	}

	// �̳߳ص�����߳�����
	lua_Integer nbWorkThreads = 1;

	lua_getfield(L, jsonIdx, "workthreads");
	nbWorkThreads = luaL_optinteger(L, -1, 1);

	// ���ݿ��ļ����λ��
	lua_getfield(L, jsonIdx, "dbpath");
	const char* dbpath = luaL_optstring(L, -1, "?/taskdeamon.sqlite3");

	// ��־
	lua_getfield(L, jsonIdx, "logpath");
	const char* logpath = luaL_optstring(L, -1, "?/taskdeamon-logs");

	// ���ɽ��յĵ���������
	lua_getfield(L, jsonIdx, "maxpacketsize");
	cfgMaxPacketSize = luaL_optinteger(L, -1, UINT_MAX);

	// ��ʼ����־
	logs = ILog4zManager::getInstance();
	if (logpath[0] == '?')
	{
		std::string strPath;
		getTaskDeamonAtPath(strPath, logpath + 1);
		logs->setLoggerPath(0, strPath.c_str());
	}
	else
	{
		logs->setLoggerPath(0, logpath);
	}	
	logs->start();

	LOGI("TaskDeamon starting ...");

	// ������ϣ���������
	if (!this->listenAt(host, port))
		return false;

	// ����DB
	if (!this->openDB(dbpath))
		return false;

	// ��������ɹ�����ʼ���̳߳�
	WorkThread* wt;
	for(uint32_t i = 0; i < nbWorkThreads; ++ i)
	{
		wt = new WorkThread(this, false, i == 0);
		workThreads.append(wt);
		wt->start();
	}

	// ����һ����ʼ�������ݿ��м������ݵ��߳�
	wt = new WorkThread(this, true, false);
	workThreads.append(wt);
	wt->start();

	lua_settop(L, 0);
	return true;
}

#ifdef _WINDOWS
void Service::getTaskDeamonAtPath(std::wstring& path, const char* dir)
{
	size_t dirleng = 0;
	wchar_t szThisModule[512] = { 0 };
	DWORD cchDir = 0, cchModule = GetModuleFileNameW(GetModuleHandle(L"taskdeamon"), szThisModule, 512);

	if (dir)
	{
		if (dir[0] == '/')
			dir ++;

		dirleng = strlen(dir);
		cchDir = MultiByteToWideChar(CP_UTF8, 0, dir, dirleng, 0, 0);
	}

	wchar_t* pszModFilename = wcsrchr(szThisModule, L'\\');
	assert(pszModFilename);
	pszModFilename[1] = 0;
	cchModule = wcslen(szThisModule);

	path.resize(cchModule + cchDir);
	wchar_t* dst = const_cast<wchar_t*>(path.c_str());
	
	wcsncpy(dst, szThisModule, cchModule);
	if (cchDir)
		MultiByteToWideChar(CP_UTF8, 0, dir, dirleng, dst + cchModule, cchDir + 1);

	std::replace(path.begin(), path.end(), L'\\', L'/');
}
#endif
void Service::getTaskDeamonAtPath(std::string& path, const char* dir)
{
#ifdef _WINDOWS
	std::wstring wpath;
	getTaskDeamonAtPath(wpath, dir);

	int cch = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), wpath.length(), 0, 0, 0, 0);
	path.resize(cch);
	WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), wpath.length(), const_cast<char*>(path.c_str()), cch + 1, 0, 0);
#else
#endif
}

//////////////////////////////////////////////////////////////////////////
void WorkThread::onThread()
{
	isRunning = true;

	PckHeader* hd;
	int64_t waitTime = 1000;
	SchedulesList reached;
	boost::posix_time::ptime sTime, eTime;

	while(service->bEventLooping)
	{
		if (isTimeThread)
		{
			// �����ʱ���̣߳���ô��������ִ�мƻ������Ƿ��˸�����ʱ�ļ��
			if (waitTime > 0)
			{
				boost::recursive_mutex& mtx = service->timerCondLock;
				boost::recursive_mutex::scoped_lock locker(mtx);
				service->taskCond.timed_wait(locker, boost::get_system_time() + boost::posix_time::milliseconds(waitTime));
			}

			sTime = boost::posix_time::microsec_clock::local_time();

			service->scheduleLock.lock();
			ScheduleData* n = service->schedules.first();
			if (n)
			{				
				uint64_t msCurrent = sTime.time_of_day().total_milliseconds();

				while(n && service->bEventLooping)
				{
					// һ����һ��û�дﵽʱ�䣬��ô����ľͶ�����Ҫ����ˣ���Ϊ���е�ִ�ж����Ѿ�����ʼʱ��������˵�
					if (msCurrent < n->startTime)
						break;

					ScheduleData* nn = n->next();
					service->schedules.remove(n);
					reached.append(n);
					n = nn;
				}
			}
			service->scheduleLock.unlock();

			if (!service->bEventLooping)
				break;

			if (reached.size())
			{
				// �ӵ�����ִ�е��б���ȥ
				service->scheduleExecLock.lock();
				service->schedulesExecuting.append(reached);
				service->scheduleExecLock.unlock();
			}
		}
		else
		{
			// ��ʱ���̣߳��͵ȴ�����֪ͨ��֪ͨ����������
			boost::recursive_mutex& mtx = service->taskCondLock;
			boost::recursive_mutex::scoped_lock locker(mtx);
			service->taskCond.wait(mtx);
		}

		// �ٽ���packets
		while(service->bEventLooping)
		{
			hd = 0;
			service->pcksListLock.lock();
			if (service->packets.size())
			{
				hd = service->packets.front();
				service->packets.pop_front();
			}
			service->pcksListLock.unlock();

			if (!hd)
				break;

			packetExecute(service, L, hd, false);
			free(hd);

			if (isTimeThread)
				break;	// ʱ���߳�һ��ֻ����һ������Ȼ�󽻸�����߳�ȥ����
		}

		// ִ�мƻ�
		ScheduleData* sch;
		boost::mutex& mtxExec = service->scheduleExecLock;

		mtxExec.lock();
		while((sch = service->schedulesExecuting.popFirst()) != NULL)
		{
			mtxExec.unlock();

			scheduleExecute(pService, L, sch);
			delete sch;

			mtxExec.lock();
		}
		mtxExec.unlock();

		if (isTimeThread)
		{
			// ������һ��ʱ���߳���������ʱ��Ҫ�ȴ��೤ʱ�䡣���ʱ��ȡ���ڱ����߳����д������еĺ�ʱ�������ȴ���1�룬����߳���ִ�кĵ���300ms����ô�ȴ�ʱ��ͻ���700ms
			eTime = boost::posix_time::microsec_clock::local_time();
			waitTime = (eTime - sTime).total_milliseconds();
			if (waitTime > 1000)
				waitTime = 0;
			else
				waitTime = 1000 - waitTime;
		}
	}

	isRunning = false;
}

void WorkThread::onThread2()
{
	isRunning = true;

	// Ȼ���ѯ���ݿ⣬��ȡ�Ѿ������˵�����
	DBSqlite* db = service->db;
	DBSqlite::Result* r1 = db->query("SELECT * FROM schedules WHERE status=0");
	if (!r1)
		return ;

	DBSqlite::Value bindValues[1];	

	while(r1->next() && service->bEventLooping)
	{
		ScheduleData* sc = new ScheduleData();
		sc->scheduleId = r1->getInt("schid");
		sc->mode = (ScheduleMode)r1->getInt("mode");
		sc->flags = r1->getInt("flags");
		sc->repeatInterval = r1->getInt("repeat_interval");
		sc->repeatTimes = r1->getInt("repeat_times");

		bindValues[0].make(sc->scheduleId);

		// �������е�����
		DBSqlite::Result* r2 = db->query("SELECT url,download_to,posts,result_force FROM httpreq_tasks WHERE schid=?", bindValues, 1, DBSqlite::kIndexNum);
		if (r2)
		{
			while(r2->next() && service->bEventLooping)
			{
				HTTPRequestTask* httpreq = sc->hpool.newElement();
				sc->tasks.append(httpreq);

				uint32_t urllen = 0, postslen = 0, downtolen = 0, resultlen = 0;
				const char* url = r2->getString(0, &urllen);
				const char* posts = r2->getString(1, &postslen);
				const char* downto = r2->getString(2, &downtolen);
				const char* result = r2->getString(3, &resultlen);

				httpreq->strUrl.append(url, urllen);
				if (postslen) httpreq->strPosts.append(posts, postslen);
				if (downtolen) httpreq->strDownloadTo.append(downto, downtolen);
				if (resultlen) httpreq->strResultForce.append(result, resultlen);
			}
			r2->drop();
		}
		
		// ���
		service->addSchedule(sc);
	}
	r1->drop();

	isRunning = false;
}

//////////////////////////////////////////////////////////////////////////
// main������ȷ��ȫ��ֻ��һ������
#ifdef _WINDOWS
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	const char* pszStartupArgs = lpCmdLine;
	size_t nCmdLength = strlen(pszStartupArgs);
	if (nCmdLength < 6)
		return 0;

	HANDLE  hSingleInstance = CreateEventW(NULL, FALSE, FALSE, L"reemext.task_service");
	if (hSingleInstance == NULL)
	{
		CloseHandle(hSingleInstance);
		return 0;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// �Ѿ�������
		CloseHandle(hSingleInstance);
		return 0;
	}

	// ����curl��
	HMODULE hCurl = GetModuleHandleW(L"libcurl");
	if (!hCurl)
	{
		hCurl = LoadLibrary(L"libcurl");
		if (!hCurl)
			return 0;
	}

	p_curl_easy_init = (fn_curl_easy_init)GetProcAddress(hCurl, "curl_easy_init");
	p_curl_easy_setopt = (fn_curl_easy_setopt)GetProcAddress(hCurl, "curl_easy_setopt");
	p_curl_easy_perform = (fn_curl_easy_perform)GetProcAddress(hCurl, "curl_easy_perform");
	p_curl_easy_cleanup = (fn_curl_easy_cleanup)GetProcAddress(hCurl, "curl_easy_cleanup");
	p_curl_easy_getinfo = (fn_curl_easy_getinfo)GetProcAddress(hCurl, "curl_easy_getinfo");

#else
int main(int argc, char* argv[])
{
	if (argc != 2)
		return 0;

	const char* pszStartupArgs = argv[1];
	size_t nCmdLength = pszStartupArgs;
	if (nCmdLength < 6)
		return 0;	
#endif

	pService = new Service();

	if (pService->parserStartup(pszStartupArgs, nCmdLength))
	{
		pService->run();
	}
	
	delete pService;
	if (logs) logs->stop();

#ifdef _WINDOWS
	FreeLibrary(hCurl);
	CloseHandle(hSingleInstance);
#else
#endif
	return 0;
}