struct AutoRestoreTop
{
	AutoRestoreTop(lua_State* s)
	{
		L = s;
		t = lua_gettop(L);
	}
	~AutoRestoreTop()
	{
		lua_settop(L, t);
		if (s)
			delete s;
	}

	lua_State		*L;
	ScheduleData	*s;
	int				t;
};


static void packetExecute(Service* service, lua_State* L, PckHeader* hd, bool bForceOnlySaveIt)
{
	AutoRestoreTop art(L);
	ScriptTask scripttmp;
	HTTPRequestTask httpreqtmp;	
	ScheduleData* s = art.s = bForceOnlySaveIt ? 0 : new ScheduleData();

	lua_pushcfunction(L, &lua_string_json);
	lua_pushlstring(L, (char*)(hd + 1), hd->bodyLeng);
	lua_pushnil(L);
	lua_pushboolean(L, 0);
	lua_pcall(L, 3, 1, 0);
	int tblIdx = lua_gettop(L);
	if (!lua_istable(L, tblIdx))
	{
		std::string strJson;
		size_t summaryLeng = std::min(256U, hd->bodyLeng);
		strJson.append((char*)(hd + 1), summaryLeng);

		LOGFMTE("Packet Json Parsed Failed [bodylength=%u] Json summary:\n%s", hd->bodyLeng, strJson.c_str());
		return ;
	}

	// ȡ��������
	lua_getfield(L, tblIdx, "tasks");
	int tasksIdx = lua_gettop(L);
	if (!lua_istable(L, tasksIdx) || lua_objlen(L, -1) == 0)
	{
		LOGFMTE("Packet Json have no task [bodylength=%u]", hd->bodyLeng);
		return ;
	}

	// ȡģʽ���ã������ݲ�ͬ����������ȡ��ͬ�Ĳ���
	lua_getfield(L, tblIdx, "mode");
	const char* mode = luaL_optstring(L, -1, "immediate");

	if (strcmp(mode, "immediate") == 0)
	{
		s->mode = kScheduleImmediate;
	}
	else if (strcmp(mode, "daycycles") == 0)
	{
		s->mode = kScheduleDayCycles;
	}
	else if (strcmp(mode, "interval") == 0)
	{
		s->mode = kScheduleInterval;
	}
	else
	{
		LOGFMTE("Packet Json with invalid mode: %s", mode);
		return ;
	}

	// ȡ�״����е�ʱ���
	lua_getfield(L, tblIdx, "start");
	const char* startDatetime = luaL_optstring(L, -1, NULL);
	if (startDatetime)
	{
	}

	// ȡ������־λ����
	lua_getfield(L, tblIdx, "cpufreed");
	if (luaL_optboolean(L, -1, false))
		s->flags |= kSFlagCPUFreed;

	lua_getfield(L, tblIdx, "halt_result");
	if (luaL_optboolean(L, -1, false))
		s->flags |= kSFlagHaltWhenResultNotMatched;

	lua_getfield(L, tblIdx, "halt_anyerrors");
	if (luaL_optboolean(L, -1, false))
		s->flags |= kSFlagHaltWhenHttpRequestFailed;


	// ����SQL������������ƻ�
	DBSqlite::Value vals1[] = {
		{ DBSqlite::kValueInt, 0, { (int)kScheduleRepeat } },
		{ DBSqlite::kValueInt, 0, { 0 } },
		{ DBSqlite::kValueInt, 0, { 0 } },
		{ DBSqlite::kValueInt, 0, { 0 } },
		{ DBSqlite::kValueNull, 0, { 0 } },
	};

	servoce->db->action(DBSqlite::kBegin);
	service->db->exec("INSERT INTO schedules(create_time,mode,repeat_interval,repeat_times,weekdays,day_time_ranges) VALUES(datetime('now','localtime'),?,?,?,?,?)", vals1, sizeof(vals1) / sizeof(vals1[0]));
	uint64_t insertId = service->db->getLastInsertId();

	// Ȼ�󱣴����е�����
	DBSqlite::Value vals2[] = {
		{ DBSqlite::kValueText, 0, { (int)insertId } },
		{ DBSqlite::kValueText, 0, { 0 } },
		{ DBSqlite::kValueText, 0, { 0 } },
	};

	int validcc = 0;	
	lua_settop(L, tasksIdx);
	for(int iTask = 1; ; ++ iTask)
	{
		lua_rawgeti(L, tasksIdx, iTask);
		if (!lua_istable(L, -1))
			break;

		// �Ȼ�ȡ���������
		lua_getfield(L, -1, "type");

		size_t typelen = 0;
		const char* type = luaL_optlstring(L, -1, "", &typelen);
		if (!type || typelen < 3)
			continue;

		if (strcmp(type, "httpreq") == 0)
		{
			// http��������
			lua_getfield(L, -1, "url");
			lua_getfield(L, -2, "posts");
			lua_getfield(L, -2, "downto");
			lua_getfield(L, -2, "result");

			size_t urllen = 0, postslen = 0, downtolen = 0, resultlen = 0;
			const char* url = luaL_optlstring(L, -2, "", &urllen);
			const char* posts = luaL_optlstring(L, -1, "", &postslen);
			const char* downto = luaL_optlstring(L, -1, "", &downtolen);
			const char* result = luaL_optlstring(L, -1, "", &resultlen);

			if (postslen == 0)
			{
				// ��������base64������post����
				lua_getfield(L, -2, "b64posts");
				posts = luaL_optlstring(L, -1, "", &postslen);
			}

			if (url)
			{
				// ����Ҫ��URL������Ϊ��Ч
				vals2[1].len = urllen;
				vals2[1].v.s = url;
				if (postslen > 1)
				{
					vals2[2].type = DBSqlite::kValueText;
					vals2[2].len = postslen;
					vals2[2].v.s = posts;
				}
				else
				{
					vals2[2].type = DBSqlite::kValueNull;
				}

				if (service->db->exec("INSERT INTO httpreq_tasks(schid,url,posts) VALUES(?,?,?)", vals2, sizeof(vals2) / sizeof(vals2[0])))
				{
					if (s)
					{
						// ��¼����
						s.tasks.push_back(httpreqtmp);
						HTTPRequestTask& httpreq = s.tasks.back();

						httpreq.strUrl.append(url, urllen);
						httpreq.strPosts.append(posts, postslen);
						httpreq.strDownloadTo.append(downto, downtolen);
						httpreq.strResultForce.append(result, resultlen);
					}
					validcc ++;
				}
			}
		}
		else if (strcmp(type, "script") == 0)
		{
			// �ű�����
		}
		else
			LOGFMTE("Packet Json have invalid task type: %s", type);

		lua_settop(L, tasksIdx);
	}

	if (validcc)
	{
		servoce->db->action(DBSqlite::kRollback);
		LOGFMTE("Packet Json have no task after parsed [bodylength=%u]", hd->bodyLeng);
		return ;
	}

	servoce->db->action(DBSqlite::kCommit);

	if (bForceOnlySaveIt)
		return ;

	// ������ƻ����ھ���ӵ�����
	service->addSchedule(s);

	art.s = NULL;
}