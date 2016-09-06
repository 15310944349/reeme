// 下面是ASCII字符属性表，1表示符号，2表示大小写字母，3表示数字，4表示可以用于组合整数或小数的符号
static uint8_t sql_where_splits[128] = 
{
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,	1,		// 0~32
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 1,	// 33~47
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3,	// 48~57
	1, 1, 1, 1, 1, 1, 1,	// 58~64
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,	// 65~92
	1, 1, 1, 1, 2, 1,	// 91~96
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,	// 97~122
	1, 1, 1, 1, 1,
};


static void lua_string_addbuf(luaL_Buffer* buf, const char* str, size_t len)
{
	size_t lenleft = len, copy;
	while(lenleft > 0)
	{
		copy = std::min(lenleft, (size_t)(LUAL_BUFFERSIZE - (buf->p - buf->buffer)));
		if (!copy)
		{
			luaL_prepbuffer(buf);
			copy = std::min(lenleft, (size_t)LUAL_BUFFERSIZE);
		}

		memcpy(buf->p, str, copy);
		lenleft -= copy;
		buf->p += copy;
		str += copy;
	}
}

struct BMString
{
	uint32_t	m_flags;
	uint32_t	m_hasNext;
	size_t		m_subLen;
	size_t		m_subMaxLen;	

	void setSub(const char* src, size_t len)
	{
#ifdef REEME_64
		m_subMaxLen = len + 7 >> 3 << 3;
#else
		m_subMaxLen = len + 3 >> 2 << 2;
#endif
		m_subLen = len;
		memcpy(this + 1, src, len);
	}

	void makePreTable()
	{
		size_t pos, i;
		uint8_t* sub = (uint8_t*)(this + 1);
		size_t* pSkips = (size_t*)(sub + m_subMaxLen);
		size_t* pShifts = pSkips + 256;

		memset(pShifts, 0, sizeof(size_t) * m_subLen);

		for (i = 0; i < 256; ++ i)
			pSkips[i] = m_subLen;

		for (pos = m_subLen - 1; ; -- pos)
		{
			uint8_t *good = sub + pos + 1;
			size_t goodLen = m_subLen - pos - 1;

			while (goodLen > 0)
			{
				uint8_t *p = sub + m_subLen - 1 - goodLen;
				while (p >= sub)
				{
					if (memcmp(p, good, goodLen) == 0)
					{
						pShifts[pos] = (m_subLen - pos) + (good - p) - 1;
						break;
					}
					p --;
				}

				if (pShifts[pos] != 0)
					break;

				good ++;
				goodLen --;
			}

			if (pShifts[pos] == 0)
				pShifts[pos] = m_subLen - pos;

			if (pos == 0)
				break;
		}

		i = m_subLen;
		while (i)
			pSkips[*sub ++] = -- i;
	}

	BMString* getNext() const
	{
		if (!m_hasNext)
			return 0;

		uint8_t* sub = (uint8_t*)(this + 1);
		size_t* pTbl = (size_t*)(sub + m_subMaxLen);

		return (BMString*)(pTbl + 256 + m_subLen);
	}

	size_t find(const uint8_t* src, size_t srcLen) const
	{
		const uint8_t* sub = (const uint8_t*)(this + 1);
		const size_t* pSkips = (const size_t*)(sub + m_subMaxLen);
		const size_t* pShifts = pSkips + 256;

		size_t lenSub1 = m_subLen - 1;
		size_t strEnd = lenSub1, subEnd;
		while (strEnd <= srcLen)
		{
			subEnd = lenSub1;
			while (src[strEnd] == sub[subEnd])
			{
				if (subEnd == 0)
					return strEnd;

				strEnd --;
				subEnd --;
			}

			strEnd += std::max(pSkips[src[strEnd]], pShifts[subEnd]);
		}

		return -1;
	}

	static size_t calcAllocSize(size_t subLen)
	{
		size_t sub;
#ifdef REEME_64
		sub = subLen + 7 >> 3 << 3;
#else
		sub = subLen + 3 >> 2 << 2;
#endif
		return (subLen + 256) * sizeof(size_t) + sizeof(BMString) + sub;
	}
};


//////////////////////////////////////////////////////////////////////////
// 按参数1字符串按照参数2字符串中出现的每一个字符进行切分，切分时将根据参数3中设置的标志进行相应的处理，如果参数4存在且为true则切分的结果会以多返回值返回，否则将以table返回切分的结果
// 即使参数1字符串完全不含有参数2字符串中的任何一个字符，也会进行一次切分，只是返回为整个参数1字符串
static int lua_string_split(lua_State* L)
{
	bool exists = false;
	uint8_t checker[256] = { 0 }, ch;
	size_t byLen = 0, srcLen = 0, start = 0;
	uint32_t nFlags = 0, maxSplits = 0, cc = 0;
	int top = lua_gettop(L), retAs = LUA_TTABLE, tblVal = 3;

	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);
	const uint8_t* by = (const uint8_t*)luaL_checklstring(L, 2, &byLen);	

	retAs = lua_type(L, 3);
	if (retAs == LUA_TNUMBER)
	{
		nFlags = luaL_optinteger(L, 3, 0);
		maxSplits = nFlags & 0x0FFFFFFF;

		if (top >= 4)
			retAs = lua_type(L, tblVal = 4);
	}
	
	if (retAs == LUA_TBOOLEAN)
	{
		// is true return plained string(s)
		if (lua_toboolean(L, tblVal))
			retAs = LUA_TNIL;
	}
	else if (retAs == LUA_TTABLE)
	{
		// is table then use it directly
		cc = lua_objlen(L, tblVal);
		exists = true;
	}
	else
	{
		retAs = LUA_TTABLE;
	}

	if (retAs == LUA_TTABLE)
	{
		if (!exists)
		{
			int n1 = std::max(maxSplits, 4U), n2 = 0;

			if (nFlags & 0x40000000)
				std::swap(n1, n2);

			lua_createtable(L, n1, n2);
			tblVal = lua_gettop(L);
		}
	}
	else
		tblVal = 0;

	if (maxSplits == 0)
		maxSplits = 0x0FFFFFFF;

	// 设置标记
	size_t i, endpos;
	for(i = 0; i < byLen; ++ i)
		checker[by[i]] = 1;
	
	// 逐个字符的检测	
	for (i = endpos = 0; i < srcLen; ++ i)
	{
		ch = src[i];
		if (!checker[ch])
			continue;

		endpos = i;
		if (nFlags & 0x20000000)
		{
			// trim
			while(start < endpos && src[start] <= 32)
				start ++;
			while(start < endpos && src[endpos - 1] <= 32)
				endpos --;
		}

		if (start < endpos)
		{
_lastseg:
			// push result
			if (tblVal)
			{
				lua_pushlstring(L, (const char*)src + start, endpos - start);
				if (nFlags & 0x40000000)
				{
					// as key
					lua_pushboolean(L, 1);
					lua_rawset(L, tblVal);
				}
				else
				{
					// as array element
					lua_rawseti(L, tblVal, cc + 1);
				}
			}
			else
			{
				lua_pushlstring(L, (const char*)src + start, endpos - start);
			}

			cc ++;
			if (cc >= maxSplits)
				break;
		}

		start = i + 1;
	}

	if (maxSplits && start < srcLen)
	{
		endpos = srcLen;
		maxSplits = 0;
		goto _lastseg;
	}

	if (cc == 0)
	{
		// no one
		lua_pushvalue(L, 1);
		if (tblVal)
		{
			if (nFlags & 0x40000000)
			{
				// as key
				lua_pushboolean(L, 1);
				lua_rawset(L, tblVal);
			}
			else
			{
				// as array element
				lua_rawseti(L, tblVal, cc + 1);
			}
		}

		return 1;
	}

	return retAs == LUA_TTABLE ? 1 : cc;
}

//////////////////////////////////////////////////////////////////////////
// 对字符串进行左右非可见符号去除，参数2和3如果存在且为true|false分别表示是否要处理左边|右边。如果没有参数2或3则默认左右都处理
static int lua_string_trim(lua_State* L)
{
	size_t len = 0;
	int triml = 1, trimr = 1;
	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &len);
	
	if (lua_isboolean(L, 2))
		triml = lua_toboolean(L, 2);
	if (lua_isboolean(L, 3))
		trimr = lua_toboolean(L, 3);

	const uint8_t* left = src;
	const uint8_t* right = src + len;
	if (triml)
	{
		while(left < right)
		{
			if (left[0] > 32)
				break;
			left ++;
		}
	}
	if (trimr)
	{
		while (left < right)
		{
			if (*(right - 1) > 32)
				break;
			right --;
		}
	}

	if (right - left == len)
		lua_pushvalue(L, 1);
	else
		lua_pushlstring(L, (const char*)left, right - left);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// 对参数1和2的字符串进行比对，参数3可以为一个整数表示要比对的字符串长度，参数3可以是一个布尔值表示是否要忽略大小写比对。参数3如果没有，参数3可以成为参数3
static int lua_string_cmp(lua_State* L)
{	
	int ignoreCase = 0, r = 0;
	size_t alen = 0, blen = 0, cmplen = -1;
	const char* a = luaL_checklstring(L, 1, &alen);
	const char* b = luaL_checklstring(L, 2, &blen);

	if (!a || !b)
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	if (lua_isnumber(L, 3))
	{
		cmplen = luaL_checklong(L, 3);
		if (lua_isboolean(L, 4))
			ignoreCase = lua_toboolean(L, 4);
	}
	else if (lua_isboolean(L, 3))
	{
		ignoreCase = lua_toboolean(L, 3);
	}

	if (ignoreCase)
	{
		if (cmplen == -1)
			r = alen == blen ? stricmp(a, b) == 0 : 0;
		else if (cmplen > alen || cmplen > blen)
			r = 0;
		else
			r = strnicmp(a, b, cmplen) == 0;
	}
	else
	{
		if (cmplen == -1)
			r = alen == blen ? strcmp(a, b) == 0 : 0;
		else if (cmplen > alen || cmplen > blen)
			r = 0;
		else
			r = strncmp(a, b, cmplen) == 0;
	}

	lua_pushboolean(L, r);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// 字符串中所含字符/串的查找，直接使用STL函数，比string.find(xx, xx, 1, true)快
static int lua_string_plainfind(lua_State* L)
{
	size_t len = 0, len2 = 0;
	const char* s = luaL_checklstring(L, 1, &len);
	const char* f = luaL_checklstring(L, 2, &len2);

	if (len && len2 && len2 <= len)
	{
		long t = luaL_optinteger(L, 3, 0);
		if (t > 0 && t <= len)
			s += t - 1;

		f = len2 > 1 ? std::strstr(s, f) : std::strchr(s, f[0]);
		if (f)
		{
			lua_pushinteger(L, f - s + 1);
			return 1;
		}
	}

	return 0;
}

// 字符串中所含字符倒叙查找，仅支持对字符进行查找不支持字符串
static int lua_string_rfindchar(lua_State* L)
{
	size_t len = 0, len2 = 0;
	const char* s = luaL_checklstring(L, 1, &len);
	const char* f = luaL_checklstring(L, 2, &len2);

	if (len && len2 == 1)
	{
		long t = luaL_optinteger(L, 3, 0);
		if (t > 0 && t <= len)
			s += t - 1;

		f = std::strrchr(s, f[0]);
		if (f)
		{
			lua_pushinteger(L, f - s + 1);
			return 1;
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
struct StringReplacePos
{
	size_t		offset;
	size_t		fromLen, toLen;
	const char	*from, *to;
};
struct ReplacePosGreater : public std::binary_function <StringReplacePos&, StringReplacePos&, bool>
{
	bool operator()(const StringReplacePos& a, const StringReplacePos& b) const
	{
		return a.offset < b.offset;
	}
};

// 各种对照模式的字符/字符串替换
static int lua_string_replace(lua_State* L)
{
	int top = lua_gettop(L);
	if (top < 3)
		return 0;

	luaL_Buffer buf;
	size_t srcLen = 0, fromLen = 0, toLen = 0, offset;
	const char* src = luaL_checklstring(L, 1, &srcLen), *srcptr, *foundPos, *from, *to;

	luaL_buffinit(L, &buf);

	int tp1 = lua_type(L, 2), tp2 = lua_type(L, 3);
	if (tp2 == LUA_TTABLE)
	{
		int i;
		StringReplacePos newrep;
		std::list<StringReplacePos> replacePoses;

		if (tp1 == LUA_TTABLE)
		{
			// 字符串数组对字符串数组
			int fromTbLen = lua_objlen(L, 2);
			if (fromTbLen < 1 || fromTbLen != lua_objlen(L, 3))
				return luaL_error(L, "string.replace with two table which length not equal", 0);
			
			for (i = 1; i <= fromTbLen; ++ i)
			{
				lua_rawgeti(L, 2, i);
				from = lua_tolstring(L, -1, &fromLen);
				if (!fromLen)
					return luaL_error(L, "cannot replace from empty string at table(%d) by string.replace", i);

				lua_rawgeti(L, 3, i);
				to = lua_tolstring(L, -1, &toLen);

				srcptr = src;
				while((foundPos = fromLen == 1 ? std::strchr(srcptr, from[0]) : std::strstr(srcptr, from)) != 0)
				{			
					newrep.offset = foundPos - src;
					newrep.fromLen = fromLen;
					newrep.toLen = toLen;
					newrep.from = from;
					newrep.to = to;
					replacePoses.push_back(newrep);
					srcptr = foundPos + fromLen;
				}
			}
		}
		else if (tp1 == LUA_TUSERDATA)
		{
			// BM字符串对字符串数组
			BMString* bms = (BMString*)lua_touserdata(L, 2);
			if (bms->m_flags != 'BMST')
				return luaL_error(L, "not a string returned by string.bmcompile", 0);

			int toTbLen = lua_objlen(L, 3);
			if (toTbLen != bms->m_hasNext + 1)
				return luaL_error(L, "string.replace with two table which length not equal", 0);

			i = 1;
			do 
			{
				lua_rawgeti(L, 3, i ++);
				to = lua_tolstring(L, -1, &toLen);

				offset = fromLen = 0;
				while((offset = bms->find((const uint8_t*)src + fromLen, srcLen - fromLen)) != -1)
				{
					newrep.offset = offset + fromLen;
					newrep.fromLen = bms->m_subLen;
					newrep.toLen = toLen;
					newrep.from = (const char*)(bms + 1);
					newrep.to = to;
					replacePoses.push_back(newrep);

					fromLen += offset + newrep.fromLen;
				}

			} while ((bms = bms->getNext()) != 0);
		}
		else
			return luaL_error(L, "error type for the 2-th parameter of string.replcae", 0);

		// 排序之后按顺序替换
		i = 0;
		offset = 0;
		if (replacePoses.size())
		{
			replacePoses.sort(ReplacePosGreater());
			for (std::list<StringReplacePos>::iterator ite = replacePoses.begin(), iend = replacePoses.end(); ite != iend; ++ ite)
			{
				StringReplacePos& rep = *ite;

				if (i && rep.offset < offset)
				{
					std::string strfrom;
					strfrom.append(rep.from, rep.fromLen);
					return luaL_error(L, "string.replace from '%s' have appeared multiple times", strfrom.c_str());
				}

				lua_string_addbuf(&buf, src + offset, rep.offset - offset);
				lua_string_addbuf(&buf, rep.to, rep.toLen);

				offset = rep.offset + rep.fromLen;
				++ i;
			}

			if (offset < srcLen)
				lua_string_addbuf(&buf, src + offset, srcLen - offset);
		}

		if (i)
			luaL_pushresult(&buf);
		else
			lua_pushvalue(L, 1);

		return 1;
	}
	else if (tp2 == LUA_TSTRING)
	{
		to = lua_tolstring(L, 3, &toLen);

		if (tp1 == LUA_TSTRING)
		{
			// 字符串对字符串替换
			from = lua_tolstring(L, 2, &fromLen);
			if (fromLen > 0)
			{
				srcptr = src;
				for(;;)
				{
					foundPos = fromLen == 1 ? std::strchr(srcptr, from[0]) : std::strstr(srcptr, from);
					if (!foundPos)
						break;

					offset = foundPos - srcptr;
					lua_string_addbuf(&buf, srcptr, offset);
					lua_string_addbuf(&buf, to, toLen);	

					srcptr = foundPos + fromLen;
				}
			
				lua_string_addbuf(&buf, srcptr, srcLen - (srcptr - src));
				luaL_pushresult(&buf);
				return 1;
			}
		}
		else if (tp1 == LUA_TUSERDATA)
		{
			// BM字符串对普通字符串
			BMString* bms = (BMString*)lua_touserdata(L, 2);
			if (bms->m_flags != 'BMST')
				return luaL_error(L, "not a string returned by string.bmcompile", 0);

			offset = fromLen = 0;
			while((offset = bms->find((const uint8_t*)src + fromLen, srcLen - fromLen)) != -1)
			{
				if (offset > fromLen)
					lua_string_addbuf(&buf, src + fromLen, offset - fromLen);
				lua_string_addbuf(&buf, to, toLen);

				fromLen += offset + bms->m_subLen;
			}

			if (fromLen < srcLen)
				lua_string_addbuf(&buf, src + fromLen, srcLen - fromLen);
		}
	}

	lua_pushvalue(L, 1);
	return 1;
}

// 将参数1字符串中由参数3指定的位置开始到结束位置（可以不指定或用参数4指定）的字符串，用参数2来进行替换
static int lua_string_subreplaceto(lua_State* L)
{
	size_t srcLen = 0, repLen = 0;
	const char* src = (const char*)luaL_checklstring(L, 1, &srcLen);
	const char* rep = (const char*)luaL_checklstring(L, 2, &repLen);

	if (srcLen < 1)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	ptrdiff_t startp = luaL_checklong(L, 3) - 1, endp = srcLen - 1;
	if (lua_isnumber(L, 4))
		endp = lua_tointeger(L, 4);

	if (endp < 0)
		endp = srcLen + endp;
	else if (endp >= srcLen)
		endp = srcLen - 1;
	else
		endp --;

	if (endp < startp)
	{
		lua_pushlstring(L, "", 0);
		return 1;
	}

	luaL_Buffer buf;
	luaL_buffinit(L, &buf);

	if (startp > 1)
		lua_string_addbuf(&buf, src, startp - 1);
	lua_string_addbuf(&buf, rep, repLen);
	lua_string_addbuf(&buf, src + endp + 1, srcLen - endp - 1);

	luaL_pushresult(&buf);
	return 1;
}

// 将参数1字符串中由参数3指定的位置开始到指定长度（可以不指定或用参数4指定）的字符串，用参数2来进行替换
// 这个函数和上面的subreplaceto的唯一区别就是一个用的是开始+位置结束，一个用的是开始位置+长度
static int lua_string_subreplace(lua_State* L)
{
	size_t srcLen = 0, repLen = 0;
	const char* src = (const char*)luaL_checklstring(L, 1, &srcLen);
	const char* rep = (const char*)luaL_checklstring(L, 2, &repLen);

	if (srcLen < 1)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	ptrdiff_t startp = luaL_checklong(L, 3) - 1, leng = LONG_MAX;
	if (lua_isnumber(L, 4))
		leng = lua_tointeger(L, 4);

	leng = std::min(leng, (ptrdiff_t)(srcLen - startp));
	if (leng < 1)
	{
		lua_pushvalue(L, 1);
		return 0;
	}

	luaL_Buffer buf;
	luaL_buffinit(L, &buf);

	if (startp > 0)
		lua_string_addbuf(&buf, src, startp);
	lua_string_addbuf(&buf, rep, repLen);
	if (leng)
		lua_string_addbuf(&buf, src + startp + leng, srcLen - startp - leng);

	luaL_pushresult(&buf);
	return 1;
}

// 参数2指定开始位置，然后用参数3指定的结束位置或字符串进行查找以得到一个结束位置，将开始与结束位置之间的字符串取出后返回
// 用法1：string.subto('abcdefghijklmn', 'hijkl') => abcdefg   hijkl是结束位置字符串，所以sub的起点是开始到h之前
// 用法2：string.subto('abcdefghijklmn', 4, 'hijkl') => defg   同上一例，只不过指定了开始位置为第4个字符，因此前面的3个字符被扔掉了
static int lua_string_subto(lua_State* L)
{
	size_t srcLen, toLen = 0;
	int top = std::min(3, lua_gettop(L));
	const char* src = luaL_checklstring(L, 1, &srcLen), *to = 0;
	long start = 0, endp = srcLen;	

	if (top == 3)
		start = luaL_checklong(L, 2) - 1;
	if (start < 0 || srcLen < 1)
		return 0;

	if (lua_isnumber(L, top))
	{
		endp = luaL_checklong(L, top);
		if (endp < 0)
			endp = (long)srcLen + endp;
	}
	else if (lua_isstring(L, top))
	{
		to = luaL_checklstring(L, top, &toLen);
		if (!to || toLen < 1)
			return 0;

		const char* findStart = src + start;
		const char* pos = toLen == 1 ? std::strchr(findStart, to[0]) : std::strstr(findStart, to);
		if (!pos)
			return 0;
		
		if (pos == findStart)
		{
			lua_pushlstring(L, "", 0);
			return 1;
		}

		endp = pos - src - 1;
	}
	else if (lua_isnil(L, top))
	{
		endp = (long)srcLen - 1;
	}
	else
		return 0;

	if (endp < start)
		return 0;

	lua_pushlstring(L, src + start, endp - start + 1);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
// 计算参数2字符串中的每一个字符在参数1字符串中总共出现了多少次，注意统计的是参数2中每一个字符串在参数1中总共出现的次数
static int lua_string_countchars(lua_State* L)
{	
	size_t srcLen = 0, byLen = 0;
	uint8_t checker[256] = { 0 }, ch;

	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);
	const uint8_t* by = (const uint8_t*)luaL_checklstring(L, 2, &byLen);

	while ((ch = *by) != 0)
	{
		checker[ch] = 1;
		by ++;
	}

	int cc = 0;
	for(size_t i = 0; i < srcLen; ++ i)
	{
		if (checker[src[i]])
			cc ++;
	}

	lua_pushinteger(L, cc);
	return 1;
}

// 计算参数2字符串中每一个字符分别在参数1字符串中出现了多少次，返回为一个table
static int lua_string_counteachchars(lua_State* L)
{	
	size_t srcLen = 0, byLen = 0, i;
	uint32_t counts[256] = { 0 };
	uint8_t checker[256] = { 0 }, ch;	

	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);
	const uint8_t* by = (const uint8_t*)luaL_checklstring(L, 2, &byLen);

	for(i = 0; i < byLen; ++ i)
	{
		checker[by[i]] = 1;
		by ++;
	}

	for(i = 0; i < srcLen; ++ i)
	{
		uint8_t ch = src[i];
		if (checker[ch])
			counts[ch] ++;
	}

	int cc = 1;
	lua_createtable(L, byLen, 0);
	for(i = 0; i < byLen; ++ i)
	{
		lua_pushinteger(L, counts[by[i]]);
		lua_rawseti(L, -2, cc ++);
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
// 检测是否是数值或其它方式表示整数的数值，如果符合检测条件，则返回转换后的数值，否则返回nil或参数2（如果有参数2的话）
static int lua_string_checknumeric(lua_State* L)
{
	double d = 0;
	int r = 0, t = lua_gettop(L);

	if (t >= 1)
	{
		if (lua_isnumber(L, 1))
		{
			r = 1;
			d = lua_tonumber(L, 1);
		}
		else
		{
			size_t len = 0;
			char *endp = 0;
			const char* s = (const char*)lua_tolstring(L, 1, &len);

			if (len > 0)
			{
				d = strtod(s, &endp);
				if (endp && endp - s == len)
					r = 1;
			}
		}
	}

	if (r)
	{
		lua_pushnumber(L, d);
		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}

// 检测是否是整数或其它方式表示整数的值，如果符合检测条件，则返回转换后的整数，否则返回nil或参数2（如果有参数2的话）
static int lua_string_checkinteger(lua_State* L)
{
	long long v = 0;
	int r = 0, t = lua_gettop(L);

	if (t >= 1)
	{
		if (lua_isnumber(L, 1))
		{
			r = 1;
			v = lua_tointeger(L, 1);
		}
		else
		{
			size_t len = 0;
			char *endp = 0;
			const char* s = (const char*)lua_tolstring(L, 1, &len);

			if (len > 0)
			{
				v = strtoll(s, &endp, 10);
				if (endp && endp - s == len)
					r = 1;
			}
		}
	}

	if (r)
	{
#ifdef REEME_64
		lua_pushinteger(L, v);
#else
		if (v > 0x7FFFFFFF)
			lua_pushnumber(L, (double)v);
		else
			lua_pushinteger(L, v);
#endif
		return 1;
	}
	if (t >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}

	return 0;
}

// 检测是否是布尔值或其它方式表示的布尔值，如果符合检测条件，则返回转换后的布尔值，否则返回nil或参数2（如果有参数2的话）
static int lua_string_checkboolean(lua_State* L)
{
	int top = lua_gettop(L), r = 0, cc = 0;
	if (top >= 1)
	{
		switch (lua_type(L, 1))
		{
		case LUA_TNUMBER:
		if ((lua_isnumber(L, 1) && lua_tonumber(L, 1) != 0) || lua_tointeger(L, 1) != 0)
			r = 1;
		cc = 1;
		break;

		case LUA_TBOOLEAN:
			r = lua_toboolean(L, 1);
			cc = 1;
			break;

		case LUA_TSTRING:
		{
			size_t len = 0;
			const char* s = lua_tolstring(L, 1, &len);

			if (len == 4 && stricmp(s, "true") == 0)
				r = cc = 1;
			else if (len == 5 && stricmp(s, "false") == 0)
				cc = 1;
			else if (len == 1)
			{
				if (s[0] == '0') cc = 1;
				else if (s[0] == '1') r = cc = 1;
			}
		}
		break;
		}
	}

	if (cc)
	{
		lua_pushboolean(L, r);
		return 1;
	}
	if (top >= 2)
	{
		lua_pushvalue(L, 2);
		return 1;
	}
	return 0;
}

// 检测是否是字符串，如果符合检测条件，则返回该字符串，否则返回nil
static int lua_string_checkstring(lua_State* L)
{
	if (!lua_isstring(L, 1))
		return 0;

	size_t len = 0;
	int top = lua_gettop(L);
	const char* s = lua_tolstring(L, 1, &len);
	if (!s)
	{
		if (top >= 2)
		{
			lua_pushvalue(L, 2);
			return 1;
		}
		return 0;
	}

	int utf8 = 0;
	uint32_t flags = 0;
	ptrdiff_t minl = 0, maxl = len;	

	for(int n = 2; n <= top; n ++)
	{
		int t = lua_type(L, n);
		if (flags & (1 << t))
			return luaL_error(L, "error type of parameter(#%d) for string.checkstring", n);

		flags |= 1 << t;
		if (t == LUA_TNUMBER)
		{
			// 字符串最小和最大长度范围
			minl = lua_tointeger(L, n);
			if (lua_isnumber(L, n + 1))
			{
				// 如果下一个参数也是数值型的话，那么就默认为与本参数一起组成最小到最大长度允许范围
				maxl = lua_tointeger(L, 3);
				n ++;
			}

			if (utf8)
			{
				// 计算UTF8下字符串的长度
				ptrdiff_t cc = 0;
				const char* ptr = s;
				while (ptr - s < len)
				{
					uint8_t hiChar = ptr[0];
					if (!(hiChar & 0x80))
						ptr ++;
					else if ((hiChar & 0xE0) == 0xC0)
						ptr += 2;
					else if ((hiChar & 0xF0) == 0xE0)
						ptr += 3;
					else if ((hiChar & 0xF8) == 0xF0)
						ptr += 4;
					else if ((hiChar & 0xFC) == 0xF8)
						ptr += 5;
					else if ((hiChar & 0xFE) == 0xFC)
						ptr += 6;
					else
						return luaL_error(L, "illegal char in string.checkstring(#1) with utf-8 mode", 0);

					cc ++;
				}

				if (cc < minl || cc > maxl)
				{
					flags = 0;
					break;
				}
			}
			else if (len < minl || len > maxl)
			{
				flags = 0;
				break;
			}
		}
		else if (t == LUA_TSTRING)
		{
			// 正则表达式进行匹配
#ifdef RE2_RE2_H_
			if (!RE2::FullMatch(s, lua_tostring(L, n)))
			{
				flags = 0;
				break;
			}
#else
			lua_getglobal(L, "string");
			lua_getfield(L, "match");
			lua_pushvalue(L, 1);
			lua_pushvalue(L, n);
			lua_pcall(L, 2, 1, 0);
			if (lua_isnil(L, -1))
			{
				flags = 0;
				break;
			}
#endif
		}
		else if (t == LUA_TBOOLEAN)
		{
			// 是否utf8编码的字符串
			utf8 = lua_toboolean(L, n);
		}
		else if (t == LUA_TFUNCTION)
		{
			// 使用检测函数进行检测，返回true表示检测通过
			lua_pushvalue(L, n);
			lua_pushvalue(L, 1);
			if (lua_pcall(L, 1, 1, 0) || lua_toboolean(L, -1) == 0)
			{
				flags = 0;
				break;
			}
		}
	}

	if (flags || top == 1)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
// 使用{N}或{name}做为语法来进行模板替换，连续两个{{表示转义输出一个{
static int lua_string_template(lua_State* L)
{	
	luaL_Buffer buf;
	luaL_Buffer* pBuf = &buf;
	size_t srcLen = 0;
	char ch, *endpos;
	int idx, n = lua_gettop(L);
	int hasTable = lua_istable(L, 2), tostringIdx = 0;
	const char* src = luaL_checklstring(L, 1, &srcLen), *val;

	if (srcLen < 3)
	{
		lua_pushvalue(L, 1);
		return 1;
	}

	luaL_buffinit(L, pBuf);

	size_t i = 0, pos = 0, len, nums = 0, chars = 0, bracketOpened = -1;
	for (i = 0, pos = 0; i < srcLen; ++ i)
	{
		ch = src[i];
		if (bracketOpened != -1)
		{
			if (ch != '}')
			{
				chars ++;
				if (ch >= '0' && ch <= '9')
					nums ++;

				continue;
			}

			bool getVal = false;

			val = 0;
			if (chars == 0)
			{
				// 空引用
			}
			else if (chars == nums)
			{
				// 纯数字，引用后面相应位置的变量
				idx = strtol(src + bracketOpened, &endpos, 10);
				assert(endpos == src + i);

				if (hasTable)
				{
					lua_rawgeti(L, 2, idx);
					idx = -2;
				}

				val = lua_tolstring(L, idx + 1, &chars);
				getVal = true;
			}
			else if (hasTable)
			{
				// 按照变量名来引用
				lua_pushlstring(L, src + bracketOpened, i - bracketOpened);
				lua_rawget(L, 2);

				val = lua_tolstring(L, -1, &chars);
				getVal = true;
				idx = -2;
			}

			if (getVal)
			{
				if (!val)
				{
					// 非字符串类型的值，使用tostring函数来做转换，然后再获取转换后的值。如果是函数，则直接调用一下
					if (lua_isfunction(L, idx + 1))
					{
						lua_pushvalue(L, idx + 1);
						lua_call(L, 0, 1);
					}
					else
					{
						if (!tostringIdx)
						{
							idx = lua_gettop(L);
							lua_getglobal(L, "tostring");
							tostringIdx = idx + 1;	
						}

						lua_pushvalue(L, tostringIdx);
						lua_pushvalue(L, idx);
						lua_call(L, 1, 1);
					}

					val = lua_tolstring(L, -1, &chars);
				}

				if (chars)
					lua_string_addbuf(pBuf, val, chars);
			}
			
			pos = i + 1;
			chars = nums = 0;
			bracketOpened = -1;
			continue;
		}

		if (ch == '{')
		{
			_found:
			len = i - pos;
			if (len)
				lua_string_addbuf(pBuf, src + pos, len);

			if (src[i + 1] == '{')
			{
				// 转义，非变量或关键字	
				luaL_addchar(pBuf, '{');

				++ i;
				pos = i + 1;

				continue;
			}

			bracketOpened = i + 1;
		}
	}

	if (pos < srcLen)
		lua_string_addbuf(pBuf, src + pos, srcLen - pos);

	luaL_pushresult(pBuf);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
#define TP_FIXED	16384

static const char templInitCode[] = { "return function(self, __env__)\nlocal __ret__ = {}\n" };
static const char templReturnCode[] = { "\nreturn table.concat(__ret__, '')\nend" };
static const char tenplSetvarCode[] = { "__ret__[#__ret__+1]=" };
static const char tenplAddstrCode[] = { "__ret__[#__ret__+1]=[==[" };
static const char templSubtemplCode[] = { "subtemplate(self, __env__, " };
static const char templErrTipCode[] = { ", the full template parsed code: <br/><br/>\r\n\r\n" };

class TemplateParser
{
public:
	std::string	buf, errorMsg;
	size_t		offset, srcLen, wrote;
	const char	*src;
	const char	*savedPos;
	const char	*errorStart;
	bool		bracketOpened;

	enum KeywordEndType {
		KEND_NONE,
		KEND_THEN,
		KEND_DO,
	};
	static KeywordEndType checkTemplateKeywords(const char* exp, const char* expEnd, size_t leng)
	{
		char buf[8] = { 0 };
		const size_t revLeng[] = { 0, 4, 2 };
		const char cmps[][8] = { { "if" }, { "elseif" }, { "while" }, { "for" } };

		for (int i = 0; i < 8; ++ i)
		{
			char ch = exp[i];
			if (ch <= 32) break;
			buf[i] = ch;
		}

		KeywordEndType r = KEND_NONE;
		uint64_t a = *(const uint64_t*)buf;

		if (a == *(uint64_t*)cmps[0] || a == *(uint64_t*)cmps[1])
			r = KEND_THEN;
		else if (a == *(uint64_t*)cmps[2] || a == *(uint64_t*)cmps[3])
			r = KEND_DO;

		if (r != KEND_NONE)
		{
			const char* revFind = expEnd - 1;
			while (revFind > exp)
			{
				if ((uint8_t)revFind[0] <= 32 || revFind[0] == '}')
					revFind --;
				else
					break;
			}			

			revFind -= revLeng[r];
			if (r == KEND_THEN && strncmp(revFind, " then", 5))
				return KEND_THEN;
			if (r == KEND_DO && strncmp(revFind, " do", 3))
				return KEND_DO;
		}

		return KEND_NONE;
	}

	size_t append(size_t s)
	{
		if (s == 0)
			return 0;

		size_t used = buf.size() + 400;
		if (used >= TP_FIXED)
			return 0;

		open();

		s = std::min(s, (size_t)(TP_FIXED - used));
		buf.append(src + offset, s);
		offset += s;

		wrote = buf.size();
		return s;
	}

	bool more()
	{
		uint8_t ch;
		size_t add;

		if (offset)
		{
			buf.clear();
			wrote = 0;
		}

		if (offset >= srcLen)
			return false;

		for(;;)
		{
			const char* foundPos = savedPos ? savedPos : (const char*)std::memchr(src + offset, '{', srcLen - offset);
			savedPos = 0;

			if (!foundPos)
				return false;

			size_t pos = foundPos - src;
			if (pos > srcLen - 3)
				break;

			foundPos ++;
			ch = foundPos[0];

			if (ch <= 32)
			{
				append(foundPos - src - offset);
				goto _lastcheck;
			}

			if (ch == '\\')
			{
				ch = foundPos[1];
				if (ch == '=' || ch == ':' | ch == '%')
				{
					// 大括号转义
					add = pos - offset + 1;
					if (pos >= offset && append(add) != add)
					{
						savedPos = foundPos;
						return true;
					}
				}

				offset = pos + 2;
				continue;
			}
			else if (ch == '=' || ch == ':')
			{
				add = pos - offset;
				if (pos >= offset && append(add) != add)
				{
					savedPos = foundPos;
					return true;
				}

				const char* varBegin = foundPos + 1;
				while((uint8_t)varBegin[0] <= 32)
					varBegin ++;

				const char* varEnd = varBegin + 1;
				for ( ; ; varEnd ++)
				{
					uint8_t ch2 = varEnd[0];
					if (ch2 == '}')
						break;
				}

				close();

				// 引用子模板				
				if (ch == ':')
				{
					buf.append(tenplSetvarCode, sizeof(tenplSetvarCode) - 1);
					buf.append(templSubtemplCode, sizeof(templSubtemplCode) - 1);
					buf += '\'';
					buf.append(varBegin, varEnd - varBegin);
					buf.append("\')", 2);					
				}
				else
				{
					buf.append(tenplSetvarCode, sizeof(tenplSetvarCode) - 1);
					buf.append(varBegin, varEnd - varBegin);
				}

				buf += '\n';
				wrote = buf.size();

				open();

				foundPos = varEnd + 1;
				offset = foundPos - src;
				goto _lastcheck;
			}
			else if (ch == '%')
			{
				// 先关闭之前的输出，因为表达式不可能与字符串也不可能与其它的表达式位于同一行
				add = pos - offset;
				if (pos >= offset && append(add) != add)
				{
					savedPos = foundPos - 1;
					return true;
				}

				close();

				// 找到表达式的结束位置
				uint8_t quoted = 0, ln = 0;
				const char* expStart = foundPos + 1;
				const char* totalEnd = src + srcLen;

				while(expStart[0] <= 32)
					expStart ++;

				const char* expEnd = expStart;
				while (expEnd < totalEnd)
				{
					ch = *expEnd ++;
					if (quoted)
					{
						expEnd ++;
						if (ch == '\\')
							expEnd ++;
						else if (ch == '\'' || ch == '"')
							quoted = 0;
						else if (ch == '\n')
							ln = 1;
					}
					else if (ch == '\'' || ch == '"')
					{
						expEnd ++;
						if (quoted)
						{
							errorStart = expStart;
							errorMsg = "not closed string";
						}
						quoted = ch;
					}
					else if (ch == '\n')
						ln = 1;
					else if (ch == '}')
						break;
				}

				if (expEnd <= totalEnd)
				{					
					add = expEnd - expStart - 1;
					if (add >= 14 && memcmp(expStart, "subtemplate", 11) == 0 && sql_where_splits[expStart[11]] == 1)
					{
						// 特殊处理subtemplate
						buf.append(tenplSetvarCode, sizeof(tenplSetvarCode) - 1);
						buf.append(templSubtemplCode, sizeof(templSubtemplCode) - 1);

						expStart += 11;
						while(expStart[0] <= 32)
							expStart ++;

						if (expStart[0] == '(')
							expStart ++;

						add = expEnd - expStart - 1;
						buf.append(expStart, add);

						const char* revFindBracket = expEnd - 1;
						while (revFindBracket > expStart)
						{
							if ((uint8_t)revFindBracket[0] <= 32)
								revFindBracket --;
							else if (revFindBracket[0] == ')')
								revFindBracket = 0;
							else
								break;
						}

						if (revFindBracket)
							buf += ')';
					}
					else
					{
						// 其它的表达式，判断一下是否是某些关键字，如果是的话，则再判断有没有相应的then do语句。这个地方是很容易被忘记的，因此由程序来自动添加
						buf.append(expStart, add);

						if (ln == 0 && add >= 4)
						{
							switch (checkTemplateKeywords(expStart, expEnd, add))
							{
							case KEND_THEN:
								buf.append(" then", 5);
								break;
							case KEND_DO:
								buf.append(" do", 3);
								break;
							}
						}
					}

					buf += '\n';
					wrote = buf.size();

					foundPos = expEnd;
					offset = foundPos - src;
					goto _lastcheck;
				}
				else if (quoted)
				{
					errorStart = expStart;
					errorMsg = "not closed string";
				}
			}

			append(foundPos - src - offset);

_lastcheck:
			if (buf.size() + 400 >= TP_FIXED)
				break;	// 缓存只有400字节的空闲，就不使用了
		}

		return true;
	}

	void open()
	{
		if (!bracketOpened)
		{
			bracketOpened = true;
			buf.append(tenplAddstrCode, sizeof(tenplAddstrCode) - 1);
			wrote = buf.size();
		}
	}
	void close()
	{
		if (bracketOpened)
		{
			bracketOpened = false;
			buf.append("]==]\n", 5);
			wrote = buf.size();
		}
	}
};
static const char *lua_tpl_loader(lua_State *L, void *ud, size_t *size)
{
	TemplateParser* ctx = (TemplateParser*)ud;
	
	if (!ctx->more())
	{
		size_t left = ctx->srcLen - ctx->offset;
		if (left)
		{
			left = std::min(left, (size_t)TP_FIXED - ctx->buf.size());

			ctx->open();
			ctx->buf.append(ctx->src + ctx->offset, left);

			ctx->offset = ctx->srcLen;
		}

		if (ctx->wrote)
		{
			ctx->close();
			*size = ctx->wrote;
			return ctx->buf.c_str();
		}

		if (ctx->src)
		{
			ctx->src = 0;
			*size = sizeof(templReturnCode) - 1;
			return templReturnCode;
		}

		*size = 0;
		return 0;
	}

	ctx->close();

	*size = ctx->wrote;
	ctx->wrote = 0;
	
	return ctx->buf.c_str();
}
static void _init_TemplateParser(TemplateParser& parser, const char* src, size_t srcLen)
{
	parser.savedPos = 0;
	parser.errorStart = 0;
	parser.src = src;
	parser.srcLen = srcLen;
	parser.offset = 0;
	parser.bracketOpened = false;
	parser.wrote = sizeof(templInitCode) - 1;

	parser.buf.reserve(TP_FIXED);
	parser.buf.append(templInitCode, parser.wrote);
}
static int lua_string_parsetemplate(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	size_t srcLen = 0;	
	const char* src = luaL_checklstring(L, 2, &srcLen);
	const char* chunkName = "__templ_tempr__";	

	int hasEnv = lua_istable(L, 3);
	if (lua_isstring(L, 4))
		chunkName = lua_tostring(L, 3);

	TemplateParser parser;	
	_init_TemplateParser(parser, src, srcLen);

	if (hasEnv)
	{
		int r = lua_load(L, &lua_tpl_loader, &parser, chunkName);
		if (parser.errorMsg.length())
		{
			std::string& msg = parser.errorMsg;
			msg += " start at : ";
		
			size_t left = std::min(srcLen - (parser.errorStart - parser.src), (size_t)60);
			msg.append(parser.errorStart, left);

			lua_pushlstring(L, msg.c_str(), msg.length());
		}
		else if (r)
		{
			// 出错，将错误信息组合起来
			size_t len = 0;
			luaL_Buffer buf;
			TemplateParser fullcode;
			const char* err = lua_tolstring(L, -1, &len);

			_init_TemplateParser(fullcode, src, srcLen);					
			
			luaL_buffinit(L, &buf);
			lua_string_addbuf(&buf, err, len);
			lua_string_addbuf(&buf, templErrTipCode, sizeof(templErrTipCode) - 1);

			for (;;)
			{
				const char* ss = lua_tpl_loader(L, &fullcode, &len);
				if (!ss || len == 0)
					break;

				lua_string_addbuf(&buf, ss, len);
			}

			luaL_pushresult(&buf);
		}
		else if (lua_isfunction(L, -1))
		{
			r = lua_pcall(L, 0, 1, 0);
			if (r == 0)
			{
				assert(lua_isfunction(L, -1));

				int top = lua_gettop(L);

				lua_pushvalue(L, 3);
				lua_setfenv(L, top);

				lua_pushvalue(L, 1);
				lua_pushvalue(L, 3);
				lua_pcall(L, 2, 1, 0);
			}
		}
	}
	else
	{
		luaL_Buffer buf;
		luaL_buffinit(L, &buf);

		for (;;)
		{
			size_t s = 0;
			const char* ss = lua_tpl_loader(L, &parser, &s);
			if (!ss || s == 0)
				break;
			
			lua_string_addbuf(&buf, ss, s);
		}

		luaL_pushresult(&buf);
	}


	return 1;
}

//////////////////////////////////////////////////////////////////////////
static int lua_string_bmcompile(lua_State* L)
{
	size_t len;
	const char* src;
	BMString* bms;
	
	if (lua_istable(L, 1))
	{
		luaL_checkstack(L, 1, "string.bmcompile only build from one table");
		
		size_t totals = 0, cc = 0;
		int i, t = lua_objlen(L, 1);
		
		for(i = 1; i <= t; ++ i)
		{
			lua_rawgeti(L, 1, i);

			len = 0;
			src = lua_tolstring(L, -1, &len);
			if (!src || len == 0)
				return luaL_error(L, "string.bmcompile cannot compile a empty string", 0);

			len =  BMString::calcAllocSize(len);
			if (len)
			{
				totals += len;
				cc ++;
			}
		}

		if (cc)
		{			
			char* mem = (char*)lua_newuserdata(L, totals);
			for (i = 2, ++ t; i <= t; ++ i)
			{
				src = lua_tolstring(L, i, &len);
				if (src)
				{
					bms = (BMString*)mem;
					bms->setSub(src, len);
					bms->m_flags = 'BMST';
					bms->m_hasNext = -- cc;
					bms->makePreTable();

					mem += BMString::calcAllocSize(len);
				}
			}

			return 1;
		}
	}
	else
	{
		int n = 1, t = lua_gettop(L);
		for( ; n <= t; ++ n)
		{
			len = 0;
			src = luaL_checklstring(L, 1, &len);
			if (!src || len == 0)
				return luaL_error(L, "string.bmcompile cannot compile a empty string", 0);

			bms = (BMString*)lua_newuserdata(L, BMString::calcAllocSize(len));
			bms->setSub(src, len);
			bms->m_flags = 'BMST';
			bms->m_hasNext = 0;
			bms->makePreTable();
		}

		return t;
	}

	return 0;
}

static int lua_string_bmfind(lua_State* L)
{
	size_t srcLen;
	const uint8_t* src = (const uint8_t*)luaL_checklstring(L, 1, &srcLen);

	BMString* bms = (BMString*)lua_touserdata(L, 2);
	if (!bms || bms->m_flags != 'BMST')
		return luaL_error(L, "not a string returned by string.bmcompile", 0);

	size_t pos = bms->find(src, srcLen);
	if (pos != -1)
	{
		lua_pushinteger(L, pos + 1);
		return 1;
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
// 参数1是JSON字符串，返回值有两个，一个是解出来的Table，另外一个是用掉的字符串的长度。如果第2个返回值为nil则表示解析JSON的时候出错了
// 参数2可以在当{}或[]为空的时候，标识出这是一个Object还是一个Array
static int lua_string_json(lua_State* L)
{
	int top = lua_gettop(L);
	if (!lua_isstring(L, 1))
		return 0;

	size_t len = 0;
	int needSetMarker = 0;
	const char* str = luaL_checklstring(L, 1, &len);

	if (!str || len < 2)
		return 0;

	if (top >= 2)
		needSetMarker = 2;

	lua_newtable(L);

	JSONFile f(L);
	size_t readlen = f.parse(str, len, true, needSetMarker);
	if (readlen == 0)
	{
		// error
		char err[512], summary[64] = { 0 };
		
		f.summary(summary, 63);
		size_t errl = snprintf(err, 512, "JSON parse error: %s, position is approximately at: %s", f.getError(), summary);
		
		lua_pushlstring(L, err, errl);
		return 1;
	}

	lua_pushinteger(L, readlen);
	return 2;
}

//////////////////////////////////////////////////////////////////////////
static void luaext_string(lua_State *L)
{
	const luaL_Reg procs[] = {
		// 字符串切分
		{ "split", &lua_string_split },
		// trim函数
		{ "trim", &lua_string_trim },
		// 字符串比较
		{ "cmp", &lua_string_cmp },

		// 单个字符正向查找（3~5倍性能于string.find(str, by, 1, true)）
		{ "plainfind", &lua_string_plainfind },
		// 单个字符反向查找
		{ "rfindchar", &lua_string_rfindchar },
		// 对字符串进行所有字符出现次数的总计数
		{ "countchars", &lua_string_countchars },
		// 对字符串进行每一个字符出现次数的分别计数
		{ "counteachchars", &lua_string_counteachchars },

		// 字符串快速替换
		{ "replace", &lua_string_replace },
		// 字符串指定位置+结束位置替换
		{ "subreplaceto", &lua_string_subreplaceto },
		// 字符串指定位置+长度替换
		{ "subreplace", &lua_string_subreplace },
		// 字符串查找带截取
		{ "subto", &lua_string_subto },

		// 数值+浮点数字符串检测
		{ "checknumeric", &lua_string_checknumeric },
		// 整数字符串检测
		{ "checkinteger", &lua_string_checkinteger },
		// 布尔型检测（允许数值、字符串和boolean类型）
		{ "checkboolean", &lua_string_checkboolean },
		// 字符串检测
		{ "checkstring", &lua_string_checkstring },

		// 模板解析（不支持语法和关键字，能按照变量名来进行替换）（当字符串较长时，2~3倍性能于string.format）
		{ "template", &lua_string_template },
		// 模板解析
		{ "parseTemplate", &lua_string_parsetemplate },

		// 编译Boyer-Moore子字符串用于查找
		{ "bmcompile", &lua_string_bmcompile },
		// 用编译好的BM字符串进行查找（适合于一次编译，然后在大量的文本中快速的查找一个子串，子串越长性能越优）
		{ "bmfind", &lua_string_bmfind },

		// Json解码（3~4倍性能于cjson）
		{ "json", &lua_string_json },

		{ NULL, NULL }
	};


	lua_getglobal(L, "string");

	// 字符串切分时可用的标志位
	lua_pushliteral(L, "SPLIT_ASKEY");
	lua_pushinteger(L, 0x40000000);
	lua_rawset(L, -3);

	lua_pushliteral(L, "SPLIT_TRIM");
	lua_pushinteger(L, 0x20000000);
	lua_rawset(L, -3);

	// 所有扩展的函数
	luaL_register(L, NULL, procs);

	lua_pop(L, 1);
}