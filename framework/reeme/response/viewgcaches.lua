--����ʵ�ֵ�templatecache
--[[
	set���������һ������tp�Ǳ���v�Ǹ�ʲô���͵�ֵ�����ܳ��ֵ��ַ���ֵ���£�
	'loaded' ��ʾ���v�Ѿ���һ��������loadstring���ع���ĺ������ú���ֻҪ����self��env���þͿ������ģ��Ľ���
	'parsed' ��ʾ���v��һ���ով�����parseTemplate�������ַ�����Ҳ����Lua���룬��Ҫʹ��loadstring���������м��غ����ʹ��

	��get��������Ϊ˫ֵ���ڶ���ֵ����������һ����ʲô���͵�ֵ�����˿��������������ֵ֮�⻹����������ģ�
	'b-code' ��ʾ����ֵ��һ��string.dump�������ַ�����ֱ��ʹ��loadstring�Ϳ��Լ��أ���õ�һ����������setʱ����ĺ���һ��
	
	������ģ�建���ڸ��ֲ��Գ�������������Ч��������ʱ����1000���������������м��㣩��ʱ��ĵ�λ�ǣ��룺
	
	1��lua_code_cache=on:
		�����߳��ں��������棺
			��ʹ�û��� = 0.272s
			ʹ�û���   = 0.008s
			
		�ر��߳��ں��������棺
			��ʹ�û��� = 0.268s
			ʹ�û���   = 0.023s
	
	2��lua_code_cache=off:
		��ģʽ�£��Ƿ����߳��ں�����������û�������
			��ʹ�û��� = 0.267s
			ʹ�û���   = 0.023s
			
	����Ľ����������ģ��ĸ��ӳ̶ȶ�����������Ĳ�ࣨ������ʹ�õ�ģ��Ϊ25KB���ң������ģ������ݱȽϸ��ӣ���������Ҫ����
	�����ݱȽ϶ࣨ���绹�������԰��Ҵ�����ʹ�ã��Ļ����ǲ�໹���һ���Ӵ�����ʹ��ģ�建���ڲ�Ʒ���Ƿǳ��б�Ҫ�ġ�
]]

local cacheFuncs = table.new(0, 128)

local decParams = function(r, uts)
	local ts, pos = string.hasinteger(r)
	if ts == uts then
		local code, tp = string.sub(r, pos + 6), string.sub(r, pos, pos + 5)
		return code, tp
	end
end

return function(sharedName)
	local setfunc = require('reeme.response.view')
	
	if sharedName then
		local caches = ngx.shared[sharedName]
		local pub = {
			--ȡ����ʱ�����fullpath����Ӧ���ļ�ʱ���Ѿ����ˣ��򻺴��ʧЧ
			get = function(self, reeme, fullpath)
				local uts = reeme.fd.fileuts(fullpath)
				
				--���Ȳ��Һ���������
				local r = cacheFuncs[fullpath]
				if r then
					r = decParams(r, uts)
					if not r then
						cacheFuncs[fullpath] = nil
					end
					return r, 'loaded'
				end
				
				--Ȼ�����ǹ����ڴ�
				if caches then
					r = caches:get(fullpath)
					if r then
						local v, t = decParams(r, uts)
						if not v then
							caches:delete(fullpath)
						end
						return v, t
					end
				end
			end,

			--�����ʱ�򣬺������¼fullpath��ģ�������ļ����ļ�����ʱ��
			set = function(self, reeme, fullpath, v, tp, uts)
				if not uts then
					uts = reeme.fd.fileuts(fullpath) or 0
				end
				
				if tp == 'loaded' then
					local r = { uts, 'b-code', string.dump(v) }
					caches:set(fullpath, table.concat(r, ''))
					cacheFuncs[fullpath] = v
					
				elseif tp == 'parsed' then
					local r = { uts, 'parsed', v }
					caches:set(fullpath, table.concat(r, ''))
				end
			end,
		}
		
		setfunc('templatecache', pub)
		return pub
	else
		setfunc('templatecache', nil)
	end
end