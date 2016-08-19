printRow = function(r)
	for k,v in pairs(r) do
		ngx.say(k, '=', v)
	end
	ngx.say('<br/>')
end

local index = {
	__index = {
		index = function(self)
			local m = self.orm('testTable')
			local m2 = self.orm('mytable')
--[[
			local q = m:new()
			q({f = '23424ADFSDCXVSDF@#4@#$#@$@#$@#$', b = 'test123'})
			local r = q:insertInto()
			ngx.say(r.rows, r.insertid, '<br/>')
]]
			local r2 = m2:query():where('sex=1')
			local r = m:query()
				:expr('count(*) as c')
				:excepts('id, a , b , d , f')
				:exec()

			if r then
				--r.f = 'SDLkfjKSDFJ'
				--r:save()
				--r:create()
				
				repeat
					printRow(r)
				until not (r + 1)
				
				r = -r
				repeat
					printRow(r)
				until not (r + 1)
			end
--[[
			local v1, v2 = '', ''
			local s1 = os.clock()
			for i = 1, 10000 do
				local f = string.template('abcdefghi{1}', 'sdlfkjsdlf', 2343, 'sdlfkjsdlf', 2343)
				v1 = v1 .. f
			end
			local e1 = os.clock()
			
			local s2 = os.clock()
			for i = 1, 10000 do
				local f = string.format('abcdefghi%s', 'sdlfkjsdlf', 2343, 'sdlfkjsdlf', 2343)
				v2 = v2 .. f
			end
			local e2 = os.clock()
			
			ngx.say('time1=', tostring(e1 - s1), ' time2=', tostring(e2 - s2))
]]
		end
	}
}

return function(act)
	local c = { }
	return setmetatable(c, index)
end