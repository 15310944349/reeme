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
]]

local cacheFuncs = table.new(0, 128)

return function(sharedName)
	local setfunc = require('reeme.response.view')
	
	if sharedName then
		setfunc('templatecache', {
			get = function(self, reeme, name)
				local r = cacheFuncs[name]
				if r then
					return r, 'loaded'
				end

				local caches = ngx.shared[sharedName]
				if caches then
					r = caches:get(name)
					if r then
						local v, t = string.sub(r, 7), string.sub(r, 1, 6)
						--for debug code
						--[[if t == 'b-code' then
							v = loadstring(v, '__templ_tempr__')
							return v, 'loaded'
						end]]

						return v, t
					end
				end
			end,

			set = function(self, reeme, name, v, tp)
				local caches = ngx.shared[sharedName]
				if tp == 'loaded' then
					local r = { 'b-code', string.dump(v) }
					caches:set(name, table.concat(r, ''))
					cacheFuncs[name] = v
					
				elseif tp == 'parsed' then
					local r = { tp, v }
					caches:set(name, table.concat(r, ''))
				end
			end,
		})
	else
		setfunc('templatecache', nil)
	end
end