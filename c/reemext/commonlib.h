size_t ZLibCompress(const void* data, size_t size, char* outbuf, size_t outbufSize, int32_t level)
{
	if (size == 0)
		return 0;

	uLongf destLeng = outbufSize;

	if (level > 9)
		level = 9;
	else if (level < 1)
		level = 1;

	int ret = compress2((Bytef*)outbuf, &destLeng, (const Bytef*)data, size, level);
	if (ret != Z_OK || destLeng > size)
		return 0;

	return destLeng;
}

size_t ZLibDecompress(const void* data, size_t size, void* outmem, size_t outsize)
{
	uLongf destlen = outsize;
	int ret = uncompress((Bytef*)outmem, &destlen, (const Bytef*)data, size);
	if (ret != Z_OK)
	{
		memcpy(outmem, data, std::min(size, outsize));
		return size;
	}

	return destlen;
}

static uint32_t CRC32Table[256];
struct initCRC32Table
{
	initCRC32Table()
	{
		int i, j;
		uint32_t crc;
		for (i = 0; i < 256; i++)
		{
			crc = i;
			for (j = 0; j < 8; j++)
			{
				if (crc & 1)
					crc = (crc >> 1) ^ 0xEDB88320;
				else
					crc = crc >> 1;
			}
			CRC32Table[i] = crc;
		}
	}
} _g_initCRC32Table;

uint32_t CRC32Check(const void* data, size_t size)
{
	uint32_t ret = 0xFFFFFFFF;
	const uint8_t* buf = (const uint8_t*)data;

	for (int i = 0; i < size; i++)
		ret = CRC32Table[((ret & 0xFF) ^ buf[i])] ^ (ret >> 8);
	return ~ret;
}

//////////////////////////////////////////////////////////////////////////
// ������1ת��Ϊ����ֵ������2����Ϊtrue��ʾ����ϸ���������û�Ϊfalse��ʾ���ϸ��⣬�ڲ��ϸ����У����е���ֵ���ַ�����ֻҪ�Ƿ�0��ֵ���κη�nil��ֵ������Ϊ��true
static int lua_toboolean(lua_State* L)
{
	int strict = lua_toboolean(L, 2);
	int t = lua_type(L, 1), r = 0, cc = 0;

	switch (t)
	{
	case LUA_TNUMBER:
	{
		double v = lua_tonumber(L, 1);
		if (v == 0)
			r = 0;
		else if (v == 1 || !strict)
			r = 1;
		cc = 1;
	}
	break;

	case LUA_TSTRING:
	{
		char *endp = 0;
		size_t len = 0;
		const char* s = lua_tolstring(L, 1, &len);

		if (len == 4 && stricmp(s, "true") == 0)
			r = cc = 1;
		else if (len == 5 && stricmp(s, "false") == 0)
			cc = 1;
		else if (len)
		{
			long v = strtoul(s, &endp, 10);
			if (endp - s == len)
			{
				cc = 1;
				r = v ? 1 : 0;
			}
		}
	}
	break;

	case LUA_TBOOLEAN:
		lua_pushvalue(L, 1);
		return 1;

	case LUA_TNIL:
		cc = 1;
		break;

	case LUA_TNONE:
		break;

	default:
		if (!strict)
			r = cc = 1;
		break;
	}

	lua_pushboolean(L, r);
	return cc;
}

// ����Ƿ���userdata��NULL������ǣ��򷵻ز���2��true(��û�в���2ʱ)�����ǣ�ֱ�ӷ���false
static int lua_checknull(lua_State* L)
{
	int t = lua_type(L, 1);
	int top = lua_gettop(L);

	if ((t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA) && lua_touserdata(L, 1) == NULL)
	{
		if (top >= 2)
			lua_pushvalue(L, 2);
		else
			lua_pushboolean(L, 1);
	}
	else
	{
		lua_pushboolean(L, 0);
	}
	return 1;
}

// �жϲ���1��ֵ�Ƿ��ڽ����������в�������һ����ȵģ�������򷵻���ȵ��Ǹ�λ�õĲ��������(��С�����2����Ϊ���ڱȶԵĲ����ӵ�2����ʼ)�����û�У��򷵻�nil
static int lua_hasequal(lua_State* L)
{
	int n = 2, top = lua_gettop(L);
	while (n <= top)
	{
		if (lua_equal(L, 1, n))
		{
			lua_pushinteger(L, n);
			return 1;
		}
		++ n;
	}

	return 0;
}

static int lua_rawhasequal(lua_State* L)
{
	int n = 2, top = lua_gettop(L);
	while (n <= top)
	{
		if (lua_rawequal(L, 1, n))
		{
			lua_pushinteger(L, n);
			return 1;
		}
		++ n;
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
REEME_API int64_t str2int64(const char* str)
{
	char* endp;
	if (str && str[0])
		return strtoll(str, &endp, 10);
	return 0;
}

REEME_API uint64_t str2uint64(const char* str)
{
	char* endp;
	if (str && str[0])
	{
		if (str[0] == '0' && str[1] == 'x')
			return strtoull(str + 2, &endp, 16);
		return strtoull(str, &endp, 10);
	}
	return 0;
}

REEME_API int64_t double2int64(double dbl)
{
	return dbl;
}

REEME_API uint64_t double2uint64(double dbl)
{
	return dbl;
}

REEME_API int64_t ltud2int64(void* p)
{
	return (int64_t)p;
}

REEME_API uint64_t ltud2uint64(void* p)
{
	return (uint64_t)p;
}

REEME_API uint32_t cdataisint64(const char* str, size_t len)
{
	size_t outl;
	if (str)
	{
		int postfix = cdataValueIsInt64((const uint8_t*)str, len, &outl);
		if (outl + postfix == len)
			return postfix;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
const char initcodes[] = {
	"_G['table'].unique = function(tbl)\n"
	"	local cc = #tbl\n"
	"	local s, r = table.new(0, cc), table.new(cc, 0)\n"
	"	for i = 1, cc do\n"
	"		s[tbl[i]] = true\n"
	"	end\n"
	"	local i = 1\n"
	"	for k,_ in pairs(s) do\n"
	"		r[i] = k\n"
	"		i = i + 1\n"
	"	end\n"
	"	return r\n"
	"end\n"

	"local ffi = require('ffi')\n"
	"local reemext = ffi.load('reemext')\n"
	"local int64Buf = ffi.new('char[?]', 32)"
	
	"ffi.cdef[[\n"
	"	int64_t str2int64(const char* str);\n"
	"	uint64_t str2uint64(const char* str);\n"
	"	uint32_t cdataisint64(const char* str, size_t len);\n"
	"	int64_t double2int64(double dbl);\n"
	"	uint64_t double2uint64(double dbl);\n"
	"	int64_t ltud2int64(void* p);\n"
	"	uint64_t ltud2uint64(void* p);\n"
	"	size_t opt_i64toa(int64_t value, char* buffer);\n"
	"	size_t opt_u64toa(uint64_t value, char* buffer);\n"
	"	size_t opt_u64toa_hex(uint64_t value, char* dst, bool useUpperCase);\n"
	"]]\n"	

	"local int64construct = function(a, b)\n"
	"	local t = type(a)\n"
	"	if t == 'string' then a = reemext.str2int64(a)\n"
	"	elseif t == 'number' then a = reemext.double2int64(a)\n"
	"	elseif t == 'lightuserdata' then a = reemext.ltud2int64(a)\n"
	"	elseif t ~= 'cdata' then return error('error construct value by int64') end\n"
	"	if b then\n"
	"		t = type(b)\n"
	"		if t == 'string' then b = reemext.str2int64(b)\n"
	"		elseif t == 'number' then a = reemext.double2int64(b)\n"
	"		elseif t == 'lightuserdata' then b = reemext.ltud2int64(b)\n"
	"		elseif t ~= 'cdata' then return error('error construct value by int64') end\n"
	"		return bit.lshift(a, 32) + b\n"
	"	end\n"
	"	return a\n"
	"end\n"

	"local int64 = {\n"
	"	fromstr = reemext.str2int64,\n"
	"	is = function(str)\n"
	"		if type(str) == 'cdata' then\n"
	"			local s = tostring(str)\n"
	"			return reemext.cdataisint64(s, #s) >= 2, s\n"
	"		end\n"
	"		return false\n"
	"	end,\n"
	"	tostr = function(v)\n"
	"		local len = reemext.opt_i64toa(v, int64Buf)\n"
	"		return ffi.string(int64Buf, len)\n"
	"	end,\n"
	"	value = reemext.ltud2int64\n"
	"}\n"

	"local uint64 = {\n"
	"	fromstr = reemext.str2uint64,\n"
	"	is = function(str)\n"
	"		if type(str) == 'cdata' then\n"
	"			local s = tostring(str)\n"
	"			return reemext.cdataisint64(s, #s) == 3, s\n"
	"		end\n"
	"		return false\n"
	"	end,\n"
	"	make = function(hi, lo)\n"
	"		return bit.lshift(hi, 32) + lo\n"
	"	end,\n"
	"	tostr = function(v)\n"
	"		local len = reemext.opt_u64toa(v, int64Buf)\n"
	"		return ffi.string(int64Buf, len)\n"
	"	end,\n"
	"	tohex = function(v, upcase)\n"
	"		local len = reemext.opt_u64toa_hex(v, int64Buf, upcase or true)\n"
	"		return ffi.string(int64Buf, len, true)\n"
	"	end,\n"
	"	value = reemext.ltud2uint64\n"
	"}\n"

	"_G.int64, _G.uint64, _G.ffi = setmetatable(int64, { __call = function(self, a, b) return int64construct(a, b) end }),"
	"										setmetatable(uint64, { __call = function(self, a, b) return int64construct(a, b) end }),"
	"										ffi\n"

	"local _string = _G.string\n"
	"_string.cut = function(str, p)\n"
	"	if str then\n"
	"		local pos = string.find(str, p, 1, true)\n"
	"		if pos then\n"
	"			return string.sub(str, 1, pos - 1), string.sub(str, pos + 1)\n"
	"		end\n"
	"	end\n"
	"	return str\n"
	"end\n"
};

static int lua_uint64_tolightuserdata(lua_State* L)
{
	const uint64_t* p = (const uint64_t*)lua_topointer(L, 1);
	if (p)
	{
		lua_pushlightuserdata(L, (void*)p[0]);
		return 1;
	}

	luaL_checktype(L, 1, LUA_TCDATA);
	return 0;
}

static void initCommonLib(lua_State* L)
{
	int top = lua_gettop(L);

	luaext_string(L);
	luaext_table(L);
	luaext_utf8str(L);

	// ��������ȫ�ֺ���
	lua_pushcfunction(L, &lua_toboolean);
	lua_setglobal(L, "toboolean");

	lua_pushcfunction(L, &lua_checknull);
	lua_setglobal(L, "checknull");

	lua_pushcfunction(L, &lua_hasequal);
	lua_setglobal(L, "hasequal");

	lua_pushcfunction(L, &lua_rawhasequal);
	lua_setglobal(L, "rawhasequal");

	// ʹ��Lua��������������
	int r = luaL_dostring(L, initcodes);
	assert(r == 0);
	//const char* err = lua_tostring(L, -1);

	// Ϊint64/uint64����һ��key����������ת�����̶���cdataֵΪvoid*ֵ���Ա�Ϊ����ʹ��
	lua_getglobal(L, "int64");
	lua_pushcfunction(L, &lua_uint64_tolightuserdata);
	lua_setfield(L, -2, "key");

	lua_getglobal(L, "uint64");
	lua_pushcfunction(L, &lua_uint64_tolightuserdata);
	lua_setfield(L, -2, "key");

	lua_pop(L, 2);
	
	// ����һ����ffi����
	lua_getglobal(L, "require");
	lua_pushliteral(L, "ffi");
	lua_pcall(L, 1, 1, 0);

	lua_getfield(L, -1, "new");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_FFINew);
	
	lua_getfield(L, -1, "sizeof");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_FFISizeof);

	lua_getglobal(L, "tostring");
	lua_rawseti(L, LUA_REGISTRYINDEX, kLuaRegVal_tostring);

	lua_settop(L, top);
}