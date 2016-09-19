local validTypes = { s = 1, i = 2, n = 3, b = 4, d = 5, t = 6, e = 7 }
local validIndex = { primary = 1, unique = 2, index = 3 }

return {
	parseFields = function(m)
		local aiExists = false
		local fields, plains, indices = {}, {}, {}
		
		for k,v in pairs(m.fields) do
			if type(k) == 'string' then
				local first = v:byte()
				local isai, allownull = false, false
				
				if first == 35 then --#
					v = v:sub(2)
					allownull = true
				elseif first == 42 then --*
					v = v:sub(2)
					if aiExists then
						error('find the second auto-increasement field')
					end
					
					aiExists = true
					isai = true
				end
				
				first = v:byte(2)
				if first ~= 40 then
					error(string.format('syntax error, expet a ( after type when parse field "%s"', k))
				end

				--����ֵ
				local t = validTypes[v:sub(1, 1)]
				if not t then
					return string.format('the data type [%s] of field(%s) is invalid', v:sub(1, 1), k) 
				end
				
				--Ĭ��ֵ��ʼλ��
				local defv = string.plainfind(v, ')', 3)
				if not defv then
					error(string.format('syntax error, expet a ) before default value when parse field "%s"', k))
				end
				
				--��������Ķ���
				local decl = v:sub(3, defv - 1)
				if not decl or #decl < 1 then
					error(string.format('syntax error, expet declaration in t(...) when parse field "%s"', k))
				end

				--ȡ��Ĭ��ֵ
				if defv and #v > defv then
					defv = v:sub(defv + 1)
				else
					defv = nil
				end

				local newf = { ai = isai, null = allownull, type = t, default = defv, colname = k }
									
				if t == 5 then
					--date/datetimeǿ��תΪstring�ͣ�Ȼ���ٴ���
					newf.type = 1
					newf.maxlen = 10
					newf.isDate = true
				elseif t == 6 then
					--date/datetimeǿ��תΪstring�ͣ�Ȼ���ٴ���
					newf.type = 1
					newf.maxlen = 19
					newf.isDate, newf.isDateTime = true, true
				elseif t == 7 then
					--ö�����ͣ�ת�ַ���
					newf.type = 1
					newf.maxlen = 0
					newf.enums = string.split(decl, ',', string.SPLIT_TRIM + string.SPLIT_ASKEY)

					for i = 1, #newf.enums do
						newf.maxlen = math.max(newf.maxlen, #newf.enums[i])
					end

					--���Ĭ��ֵ�Ƿ�Ϸ�
					if defv and not newf.enums[defv] then
						error(string.format('error default enum value when parse field "%s", value "%s" not in declarations', k, defv))
					end
				end
				
				if newf.maxlen == nil and string.countchars(decl, '0123456789') == #decl then
					--ָ������Ч�ĳ���
					newf.maxlen = tonumber(decl)
				end
				
				--�������ֵ�Ͳ���maxl����11λ������Ϊ��64λ����
				if t == 2 and newf.maxlen > 11 then
					newf.isint64 = true
				end

				fields[k] = newf
				plains[#plains + 1] = k
			end
		end
		
		if m.indices then
			for k,v in pairs(m.indices) do
				local tp = validIndex[v]
				if tp ~= nil then
					local cfg = fields[k]				
					local idx = { type = tp }
					
					if cfg and cfg.ai then
						idx.autoInc = true
					end
					
					indices[k] = idx
				end
			end
		end
		
		if #plains > 0 then
			m.__fields = fields
			m.__fieldsPlain = plains
			m.__fieldIndices = indices
			return true
		end
		
		return 'may be no valid field or field(s) declaration error'
	end
}