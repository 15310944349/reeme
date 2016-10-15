--常规实现的templatecache
--[[
	set函数的最后一个参数tp是表明v是个什么类型的值，可能出现的字符串值如下：
	'loaded' 表示这个v已经是一个经过了loadstring加载过后的函数，该函数只要给出self和env调用就可以完成模板的解析
	'parsed' 表示这个v是一个刚刚经过了parseTemplate翻译后的字符串，也就是Lua代码，需要使用loadstring函数来进行加载后才能使用
	
	而get函数返回为双值，第二个值用于描述第一个是什么类型的值，除了可以是上面的两种值之外还可以是下面的：
	'b-code' 表示返回值是一个string.dump出来的字符串，直接使用loadstring就可以加载，会得到一个函数，和set时传入的函数一样
]]

local cacheFuncs = table.new(0, 128)

return function(sharedName)
	require('reeme.response.view')('templatecache', {
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
end