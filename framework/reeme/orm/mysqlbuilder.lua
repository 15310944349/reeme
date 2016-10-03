--���ʽ�ֽ�Ϊtoken����
local cExtLib = findmetatable('REEME_C_EXTLIB')
local _parseExpression, _findToken = cExtLib.sql_expression_parse, cExtLib.find_token

--date/datetime���͵�ԭ��
local datetimeMeta = getmetatable(require('reeme.orm.datetime')())
local queryMeta = require('reeme.orm.model').__index.__queryMetaTable
local specialExprFunctions = { distinct = 1, count = 2, as = 3 }
local mysqlwords = require('reeme.orm.mysqlwords')
local reemext = ffi.load('reemext')
local allOps = { SELECT = 1, UPDATE = 2, INSERT = 3, DELETE = 4 }

--�ϲ���
local builder = table.new(0, 24)

--���ݿ���������
builder.dbTypeName = 'mysql'
--�����������Ϸ�ʽ
builder.conds = { '', 'AND ', 'OR ', 'XOR ', 'NOT ' }
--���������ʽ
builder.validJoins = { inner = 'INNER JOIN', left = 'LEFT JOIN', right = 'RIGHT JOIN', full = 'FULL JOIN' }

--����һ��where��������������processWhere����
builder.parseWhere = function(self, condType, name, value)
	self.condString = nil	--condString��condValuesͬһʱ��ֻ�����һ����ֻ��һ���ǿ�����Ч��
	if not self.condValues then
		self.condValues = {}
	end	
	
	if value == nil then
		--name�����������ʽ
		return { expr = name, c = condType }
	end
	if name == nil and value then
		--value�����������ʽ
		return { expr = value, c = condType }
	end
	
	name = name:trim()
	
	local keyname, puredkeyname, findpos = nil, false, 1
	while true do
		--�ҵ���һ������mysql����������ʱֹͣ
		keyname, findpos = _findToken(name, findpos)
		if not keyname then
			keyname = nil
			break
		end
		if not mysqlwords[keyname] then
			break
		end
	end

	if keyname and #keyname == #name then
		--keyû�ж���ķ��ţ�ֻ��һ�����������
		puredkeyname = true		
	end
	
	local tv = type(value)
	if tv == 'table' then
		local mt = getmetatable(value)
		if mt == queryMeta then
			--�Ӳ�ѯ
			return { puredkeyname = puredkeyname, n = name, sub = value, c = condType }
		end
		
		if mt == datetimeMeta then
			--��������
			value = tostring(value)
			return { expr = puredkeyname and string.format('%s=%s', name, value) or value, c = condType }
		end
		
		--{value}���ֱ��ʽ
		if puredkeyname and #value == 1 then
			return { expr = string.format('%s=%s', name, value[1]), c = condType }
		end

		--��ֵ�Ļ�ֱ�ӱ��棬�����ٴ���
		return { expr = name, value = value, c = condType }

	elseif value == ngx.null then
		--����Ϊnullֵ
		return { expr = puredkeyname and string.format('%s IS NULL', name) or (name .. 'NULL'), c = condType }
	end

	if type(name) == 'string' then
		--key=value
		return { puredkeyname = puredkeyname, expr = name, value = value, c = condType }
	end
end

--����where��������������
builder.processWhere = function(self, condType, k, v)
	local tp = type(k)
	if tp == 'table' then
		if #k > 0 then
			for i = 1, #k do
				local where = builder.parseWhere(self, condType, nil, k[i])
				if where then
					self.condValues[#self.condValues + 1] = where
				else
					error(string.format("call where(%s) function with illegal value(s) call", k[i]))
				end
			end
		else
			for name,val in pairs(k) do
				local where = builder.parseWhere(self, condType, name, val)
				if where then
					self.condValues[#self.condValues + 1] = where
				else
					error(string.format("call where(%s) function with illegal value(s) call", name))
				end
			end
		end
		
		return self
	end
	
	if tp ~= 'string' then
		k = tostring(k)
	end
	if #k > 0 then
		local where = builder.parseWhere(self, condType, k, v)
		if where then
			self.condValues[#self.condValues + 1] = where
		else
			error(string.format("call where(%s) function with illegal value(s) call", k))
		end
	end
	
	return self
end

--����on��������������
builder.processOn = function(self, condType, k, v)
	local tp = type(k)
	if tp == 'string' and #k > 0 then
		local where = builder.parseWhere(self, condType, k, v)
		if where then
			if not self.onValues then
				self.onValues = { where }
			else
				self.onValues[#self.onValues + 1] = where
			end			
		else
			error(string.format("process on(%s) function call failed: illegal value or confilict with declaration of model fields", name))
		end

	elseif tp == 'table' then
		if #k > 0 then
			for i = 1, #k do
				local where = builder.parseWhere(self, condType, nil, k[i])
				if where then
					if not self.onValues then
						self.onValues = { where }
					else
						self.onValues[#self.onValues + 1] = where
					end
				else
					error(string.format("process on(%s) function call failed: illegal value or confilict with declaration of model fields", k[i]))
				end
			end
		else
			for name,val in pairs(k) do
				local where = builder.parseWhere(self, condType, name, val)
				if where then
					if not self.onValues then
						self.onValues = { where }
					else
						self.onValues[#self.onValues + 1] = where
					end
				else
					error(string.format("process on(%s) function call failed: illegal value or confilict with declaration of model fields", name))
				end
			end
		end
	end

	return self
end

--����Where�����е��������ʽ�������ʽ���õ����ֶ����֣����ձ��alias��������������
builder.processTokenedString = function(self, alias, expr, allJoins)
	local tokens, poses = _parseExpression(expr)
	if not tokens or not poses then
		return expr
	end	
	
	--���token��ѭ�����д���
	local sql, adjust = expr, 0
	local fields, aliasab = self.m.__fields, self.aliasAB
	local selfname = self.userAlias or self.m.__name
	local lastField, lastToken

	for i=1, #tokens do
		lastToken = tokens[i]
		local newone
		local a, b = string.cut(lastToken, '.')

		if b then
			--�����Ǳ���.�ֶ���
			if a == selfname then
				--�����Լ���ԭ��������ôb��һ�����ֶ�����
				lastField = fields[b] or fields[aliasab[b]]
				newone = alias .. b
			else
				--�ж��Ƿ�������join�����ı���
				newone = allJoins[a]
				if newone then
					lastField = newone:getField(b)
					newone = newone._alias .. '.' .. b
				end
			end
			
		elseif a == '*' then
			--�����ֶ�
			newone = alias .. '*'

		elseif not mysqlwords[a] then
			--�����Ƿ��ֶ����������Լ����ϲ��ң�û���ҵ���ȥ�����ı��в���
			lastField = self:getField(a)
			if lastField then				
				if self._alias then
					newone = self._alias .. '.' .. a
				end
			else
				for i = 1, #allJoins do
					local m = allJoins[i]
					lastField = m ~= self and m:getField(a) or nil
					if lastField then
						if m._alias then
							newone = m._alias .. '.' .. a
						end
						break
					end
				end
			end
		end

		if newone then
			--�滻�����յı��ʽ
			sql = sql:subreplace(newone, poses[i] + adjust, #lastToken)
			adjust = adjust + #newone - #lastToken
		end
	end
	
	return sql, lastField, lastToken
end


-----------------------------------------------------------------------------------------------------------------------
--��query���õ������ϲ�ΪSQL���
builder.SELECT = function(self, db)
	local sqls = {}
	sqls[#sqls + 1] = 'SELECT'
	
	--main
	local model = self.m
	local alias, allJoins = '', table.new(4, 4)
	
	self.db = db
	if self.joins and #self.joins > 0 then
		if self.userAlias then
			self._alias = self.userAlias
			allJoins[self._alias] = self
		else
			self._alias = '_A'			
		end

		alias = self._alias .. '.'
	end

	allJoins[self.m.__name] = self
	allJoins[#allJoins + 1] = self
		
	local cols = builder.buildColumns(self, sqls, alias)
	
	--joins fields
	if #alias > 0 then
		builder.buildJoinsCols(self, sqls, 1, #cols > 0 and true or false, allJoins)
	end

	--from
	sqls[#sqls + 1] = 'FROM'
	sqls[#sqls + 1] = model.__name
	if #alias > 0 then
		sqls[#sqls + 1] = self._alias
	end

	--joins conditions	
	if #alias > 0 then
		builder.buildJoinsConds(self, sqls, false, allJoins)
	end
	
	--where
	local haveWheres = builder.buildWheres(self, sqls, 'WHERE (', alias, nil, allJoins)
	if haveWheres then
		sqls[#sqls + 1] = ')'
	end
	builder.buildWhereJoins(self, sqls, haveWheres, allJoins)
	
	--order by
	builder.buildOrder(self, sqls, alias)
	--limit
	builder.buildLimits(self, sqls)
	
	--end
	self.db = nil
	allJoins = nil
	
	return table.concat(sqls, ' ')
end
	
builder.UPDATE = function(self, db)
	local sqls = {}
	local model = self.m
	
	sqls[#sqls + 1] = 'UPDATE'
	sqls[#sqls + 1] = model.__name
	
	--has join(s) then alias
	local alias, allJoins = '', table.new(4, 4)
	self.db = db
	if self.joins and #self.joins > 0 then
		if self.userAlias then
			self._alias = self.userAlias
			allJoins[self._alias] = self
		else
			self._alias = '_A'			
		end
				
		alias = self._alias .. '.'
		sqls[#sqls + 1] = self._alias
	end	
	
	allJoins[self.m.__name] = self
	allJoins[#allJoins + 1] = self

	--joins fields
	if allJoins then
		builder.buildJoinsCols(self, nil, 1, false, allJoins)
		
		--joins conditions	
		builder.buildJoinsConds(self, sqls, false, allJoins)
	end
	
	--all values
	if builder.buildKeyValuesSet(self, sqls, alias) > 0 then
		table.insert(sqls, #sqls, 'SET')
	end
	
	--where
	if not builder.buildWheres(self, sqls, 'WHERE', alias, nil, allJoins) then
		if type(self.__where) == 'string' then
			sqls[#sqls + 1] = 'WHERE'
			sqls[#sqls + 1] = builder.processTokenedString(self, alias, self.__where, allJoins)
		else
			--find primary key
			local haveWheres = false
			local idx, vals = model.__fieldIndices, self.keyvals

			if vals then
				for k,v in pairs(idx) do
					if v.type == 1 then
						local v = vals[k]
						if v and v ~= ngx.null then
							builder.processWhere(self, 1, k, v)
							haveWheres = builder.buildWheres(self, sqls, 'WHERE', alias, nil, allJoins)
							break
						end
					end
				end
			end

			if not haveWheres then
				error("Cannot do model update without condition(s)")
				return false
			end
		end
	end
	
	--order by
	if self.orderBy then
		sqls[#sqls + 1] = string.format('ORDER BY %s %s', self.orderBy.name, self.orderBy.order)
	end
	--limit
	builder.buildLimits(self, sqls, true)
	
	--end
	allJoins = nil
	
	return table.concat(sqls, ' ')
end

builder.INSERT = function(self, db)
	local sqls = {}
	local model = self.m
	
	sqls[#sqls + 1] = 'INSERT INTO'
	sqls[#sqls + 1] = model.__name
	
	--all values
	if builder.buildKeyValuesSet(self, sqls, '') > 0 then
		table.insert(sqls, #sqls, 'SET')
	else
		sqls[#sqls + 1] = '() VALUES()'
	end
	
	--end
	return table.concat(sqls, ' ')
end
	
builder.DELETE = function(self)
	local sqls = {}
	local model = self.m
	
	sqls[#sqls + 1] = 'DELETE'
	sqls[#sqls + 1] = 'FROM'
	sqls[#sqls + 1] = model.__name
	
	local allJoins = table.new(1, 1)

	allJoins[self.m.__name] = self
	allJoins[#allJoins + 1] = self
	
	--where
	if not builder.buildWheres(self, sqls, 'WHERE', '', nil, allJoins) then
		if type(self.__where) == 'string' then
			sqls[#sqls + 1] = 'WHERE'
			sqls[#sqls + 1] = builder.processTokenedString(self, '', self.__where, allJoins)
		else
			--find primary or unique
			local haveWheres = false
			local idx, vals = model.__fieldIndices, self.keyvals
			if vals then
				for k,v in pairs(idx) do
					if (v.type == 1 or v.type == 2) and vals[k] then
						builder.processWhere(self, 1, k, vals[k])
						haveWheres = builder.buildWheres(self, sqls, 'WHERE', '', nil, allJoins)
						break
					end
				end
			end

			if not haveWheres then
				error("Cannot do model delete without condition(s)")
				return false
			end
		end
	end
	
	--limit
	builder.buildLimits(self, sqls, true)
	
	--end
	allJoins = nil
	
	return table.concat(sqls, ' ')
end


--����һ��SQLֵ����ֵ�������ֶ���أ�������ֶε����ö�ֵ������Ӧ�Ĵ���
local function buildSqlValue(self, cfg, v)
	local tp = type(v)
	local model = self.m
	local op = allOps[self.op]
	local quoteIt, multiVals = false, false

	--���������ֶε�ֵ���������insert�µĻ����ͱ�����fullCreate�����򱻺���
	if cfg.ai and op == 3 and (not self.fullop or not string.checkinteger(v)) then
		return nil
	end

	if v == nil then
		--ֵΪnil�ֲ���UPDATE���������ֶ�û��Ĭ��ֵ�ֲ�����ΪNULL��������ֶ����͸�һ��Ĭ��ֵ����ֹSQL����
		if op ~= 2 and cfg.default == nil and not cfg.null then
			v = cfg.type == 1 and "''" or '0'
			tp = 'string'
		end
		
	elseif v == ngx.null then
		--NULLֱֵ������
		v = 'NULL'
		
	elseif tp == 'table' then	
		--table���������meta�������ж���ʲôtable
		local mt = getmetatable(v)

		if mt == datetimeMeta then
			--����ʱ��
			v = ngx.quote_sql_str(tostring(v))
		elseif mt == queryMeta then	
			--�Ӳ�ѯ
			error('Cannot support sub-query in key=>value(s) set')
			
		elseif #v == 1 then
			v = v[1]
			tp = type(v)
			
		elseif #v > 1 then
			--���ʽ���ֵ			
			multiVals = true
		else
			return nil
		end
		
	elseif tp == 'cdata' then
		--cdata���ͣ�����Ƿ���boxed int64
		local s = tostring(v)
		local i64type, newv = reemext.cdataisint64(s, #s), s
		if i64type > 0 then
			v = newv:sub(1, #newv - i64type)
		else
			v, quoteIt = newv, true
		end
		tp = 'string'
		
	else
		quoteIt = true
	end

	if v ~= nil then
		if cfg.type == 1 then
			--�ֶ�Ҫ��Ϊ�ַ���
			if multiVals then
				v = string.format("'%s'", table.concat(v, "','"))
			else
				v = tostring(v)
				if quoteIt and (string.byte(v, 1) ~= 39 or string.byte(v, #v) ~= 39) then
					--û�����ã���ô��������
					v = ngx.quote_sql_str(v)
				end
			end
			
		elseif cfg.type == 4 then
			--������ʹ��1��0������
			if multiVals then
				v = table.concat(v, ',')
			else
				v = toboolean(v) and '1' or '0'
			end
			
		elseif cfg.type == 3 then
			--��ֵ/���������ͣ����ֵ����Ϊ������
			if multiVals then
				v = table.concat(v, ',')
				
			elseif not string.checknumeric(v) then
				logger.e(string.format("model '%s': a field named '%s' its type is number but the value is not a number", model.__name, cfg.colname))
				v = nil
			end
			
		elseif multiVals then
			--�����ͣ���ֵ���Ͳ���ÿһ��ֵȥ������ˣ�
			v = table.concat(v, ',')
			
		elseif quoteIt then
			--����ַ��������������͵�ֵ
			local reti, newv = string.checkinteger(v)
			if not reti then
				--����Ͷ����������ͣ����ֵ����Ϊ����
				logger.e(string.format("model '%s': a field named '%s' its type is integer but the value is not a integer", model.__name, cfg.colname))
				v = nil
			end
			
			if newv then
				v = newv
			end
		end
	end
	
	return v
end

-----------------------------------------------------------------------------------------------------------------------
builder.buildColumns = function(self, sqls, alias, returnCols)
	local model = self.m

	--�����л��棬��ֱ��ʹ��
	if self.colCache then
		if returnCols ~= true and #self.colCache > 0 then
			sqls[#sqls + 1] = self.colCache
		end
		return self.colCache
	end
	
	--�������еı��ʽ
	local excepts, express = nil, nil
	if self.expressions then
		local fields = self.m.__fields
		local tbname = self.m.__name
		local skips = 0
		
		for i = 1, #self.expressions do
			local expr = self.expressions[i]

			if skips <= 0 and type(expr) == 'string' then
				local adjust = 0
				local tokens, poses = _parseExpression(expr)
				if tokens then
					local removeCol = false
					for k = 1, #tokens do
						local one, newone = tokens[k], nil

						if one:byte(1) == 39 then
							--����һ���ַ���
							newone = ngx.quote_sql_str(one:sub(2, -2))		
							
						elseif fields[one] then
							--����һ���ֶε�����
							if removeCol then
								if not excepts then
									excepts = {}
								end
								if self.colExcepts then
									for en,_ in pairs(self.colExcepts) do
										excepts[en] = true
									end
								end
								
								excepts[one] = true
							end
							
							newone = alias .. one

						elseif one == tbname then
							newone = alias
							one = one .. '.'

						else
							--���⴦��
							local spec = specialExprFunctions[one:lower()]
							if spec == 1 then
								--������Щ����ı��ʽ��������ʽ���������ֶξͲ��������ֶ��б��г���
								removeCol = true
							elseif spec == 2 then
								--�����excepts���ݣ�������Ϊ�գ���ΪֻҪexcepts������ڣ��ֶξͲ�����*��ʽ���֣������Ͳ�����ϳ�count(*),*�����
								if not excepts then excepts = {} end
							elseif spec == 3 then
								--AS����֮����Ҫ������һ��tokenֱ�Ӹ��Ƽ���
								skips = 2
							end

						end

						if newone then
							expr = expr:subreplace(newone, poses[k] + adjust, #one)
							adjust = adjust + #newone - #one
						end
					end

					self.expressions[i] = expr
				end

			else
				self.expressions[i] = tostring(expr)
			end

			skips = skips - 1
		end
		
		express = table.concat(self.expressions, ',')
	end
	
	if not excepts then
		excepts = self.colExcepts
	end
	
	local cols, colAlias, n2 = nil, self.aliasAB or {}, nil
	if self.colSelects then
		--ֻ��ȡĳ����
		local plains = {}
		if excepts then
			for n,_ in pairs(self.colSelects) do
				if not excepts[n] then
					n2 = colAlias[n]					
					plains[#plains + 1] = n2 and (n .. ' AS ' .. n2) or n
				end
			end
		else
			for n,_ in pairs(self.colSelects) do
				n2 = colAlias[n]
				plains[#plains + 1] = n2 and (n .. ' AS ' .. n2) or n
			end
		end

		cols = table.concat(plains, ',' .. alias)		
		
	elseif excepts then
		--ֻ�ų���ĳ����
		local fps = {}
		local fieldPlain = model.__fieldsPlain

		for i = 1, #fieldPlain do
			local n = fieldPlain[i]
			if not excepts[n] then
				n2 = colAlias[n]
				fps[#fps + 1] = n2 and (n .. ' AS ' .. n2) or n
			end
		end

		cols = #fps > 0 and table.concat(fps, ',' .. alias) or '*'
	else
		--������
		cols = '*'
	end	

	if express then
		cols = #cols > 0 and string.format('%s,%s%s', express, alias, cols) or express
	elseif #cols > 0 then
		cols = alias .. cols
	end

	self.colCache = cols
	if returnCols ~= true and #cols > 0 then
		sqls[#sqls + 1] = cols
	end
	return cols
end

builder.buildKeyValuesSet = function(self, sqls, alias)
	local model = self.m
	local fieldCfgs = model.__fields
	local vals, keyvals = self.keyvals, table.new(0, 8)

	if not vals then
		error(string.format('mysqlbuilder operator "%s" failed, no key=>value(s) set', self.op))
	end
	if type(vals) == 'string' then
		sqls[#sqls + 1] = vals
		return 1
	end

	local colAlias = self.aliasBA or {}
	local fieldCfgs = self.colSelects == nil and model.__fields or self.colSelects

	for name,v in pairs(vals) do
		name = colAlias[name] or name

		local cfg = fieldCfgs[name]
		if cfg then
			v = buildSqlValue(self, cfg, v)
			if v then
				if alias and #alias > 0 then
					keyvals[#keyvals + 1] = string.format("%s%s=%s", alias, name, v)
				else
					keyvals[#keyvals + 1] = string.format("%s=%s", name, v)
				end
			end
		end
	end

	sqls[#sqls + 1] = table.concat(keyvals, ',')
	return #keyvals
end


--���condValues��nil��˵�����ڴ���Ĳ���self�����query�Լ�������
builder.buildWheres = function(self, sqls, condPre, alias, condValues, allJoins)
	local model = self.m
	if not alias then alias = '' end
	
	if not condValues then
		if self.condString then
			if condPre then
				sqls[#sqls + 1] = condPre
			end
			sqls[#sqls + 1] = self.condString
			return true
		end
		
		condValues = self.condValues
	end

	if condValues and #condValues > 0 then
		local ignoreNextCond = true
		local wheres, conds = {}, builder.conds
		local fieldCfg, merges = nil, table.new(6, 0)
		
		for i = 1, #condValues do
			local one, rsql = condValues[i], nil
			local onecond = one.c
			
			if ignoreNextCond then
				onecond = 1
				ignoreNextCond = false
			elseif i > 1 and onecond == 1 then
				--���û��ָ���������ӷ�ʽ����ô�����ǵ�1��������ʱ�򣬾ͻ��Զ��޸�Ϊand
				onecond = 2
			end

			if one.sub then
				--�Ӳ�ѯ
				local subq = one.sub
				subq.limitStart, subq.limitTotal = nil, nil
				
				local expr = builder.processTokenedString(self, alias, one.n, allJoins)				
				local subsql = builder.SELECT(subq, subq.m, self.db)
				
				if subsql then
					if one.puredkeyname then
						rsql = string.format('%s%s IN(%s)', conds[onecond], expr, subsql)
					else
						rsql = string.format('%s%s(%s)', conds[onecond], expr, subsql)
					end
				end
				
			elseif one.expr == '(' then
				--��������ߵ���������������������ӵ�
				rsql = conds[onecond] .. one.expr
				ignoreNextCond = true
			elseif one.expr == ')' then
				--��������ߵ������ӵ�
				rsql = one.expr
			else
				--���ձ��ʽ���н���
				local lastToken
				
				merges[1] = conds[onecond]
				merges[2], fieldCfg, lastToken = builder.processTokenedString(self, alias, one.expr, allJoins)

				if one.value then
					assert(fieldCfg ~= nil, string.format("Field not exists! table name=%s, op=%s, expr=%s", model.__name, self.op, one.expr))

					merges[4] = buildSqlValue(self, fieldCfg, one.value)
					if merges[4] then						
						if mysqlwords[lastToken] == 1 then
							--���һ��token�Ǻ����ĵ��ã�����Զ��ļ������ţ�����������ǲ���д�����ŵģ����д���ŵĿ϶������������ʽ�������ǲ������е�����ģ�
							merges[3] = '('
							merges[5] = ')'
						elseif one.puredkeyname and type(one.value) ~= 'table' then
							--һ�����ֶ�����������û��ʹ��ԭʼֵ����˼���һ�����ں�
							merges[3] = '='
							merges[5] = ''
						else
							merges[3], merges[5] = '', nil
						end

						rsql = table.concat(merges, '')
					else
						--ֵ����
						rsql = one.expr .. '#ERR#'
					end
				else
					merges[3], merges[4], merges[5] = nil, nil, nil
					rsql = table.concat(merges, '')
				end
			end

			wheres[#wheres + 1] = rsql
		end
		
		if condPre then
			sqls[#sqls + 1] = condPre
		end
		sqls[#sqls + 1] = table.concat(wheres, ' ')
		
		return true
	end
	
	return false
end

builder.buildWhereJoins = function(self, sqls, haveWheres, allJoins)
	local cc = self.joins == nil and 0 or #self.joins
	if cc < 1 then
		return
	end

	for i = 1, cc do
		local q = self.joins[i].q
		if builder.buildWheres(q, sqls, haveWheres and 'AND (' or 'WHERE', q._alias .. '.', nil, allJoins) then
			if haveWheres then
				sqls[#sqls + 1] = ')'
			end
			haveWheres = true
		end
		
		builder.buildWhereJoins(q, sqls, haveWheres, allJoins)
	end
end

builder.buildJoinsCols = function(self, sqls, indient, haveCols, allJoins)
	local cc = self.joins == nil and 0 or #self.joins
	if cc < 1 then
		return
	end

	for i = 1, cc do
		local cols = nil
		local q = self.joins[i].q

		if q.userAlias then
			q._alias = q.userAlias
			allJoins[q._alias] = q
		else
			q._alias = ('_' .. string.char(65 + indient))
		end
		allJoins[q.m.__name] = q
		allJoins[#allJoins + 1] = q

		if sqls then
			cols = builder.buildColumns(q, sqls, q._alias .. '.', true)
			if #cols > 0 then
				if haveCols then
					sqls[#sqls + 1] = ','
				end
				
				sqls[#sqls + 1] = cols
				cols, haveCols = true, true
			end
		end
		
		local newIndient = builder.buildJoinsCols(q, sqls, indient + 1, haveCols, allJoins)
		indient = newIndient or (indient + 1)
	end
	
	return indient
end

builder.buildJoinsConds = function(self, sqls, haveOns, allJoins)
	local cc = self.joins == nil and 0 or #self.joins
	if cc < 1 then
		return
	end
	
	local validJoins = builder.validJoins
	
	for i = 1, cc do
		local join = self.joins[i]
		local q = join.q

		sqls[#sqls + 1] = validJoins[join.type]
		sqls[#sqls + 1] = q.m.__name
		sqls[#sqls + 1] = q._alias
		sqls[#sqls + 1] = 'ON('
		
		local pos = #sqls
		if q.onValues == nil or not builder.buildWheres(q, sqls, nil, q._alias .. '.', q.onValues, allJoins) then
			if join.type == 'inner' then
				sqls[#sqls + 1] = '1)'
			else
				table.remove(sqls, pos)
			end
		else
			sqls[#sqls + 1] = ')'
		end
		
		if builder.buildJoinsConds(q, sqls, haveOns, allJoins) then
			haveOns = true
		end
	end
	
	return haveOns
end

builder.buildOrder = function(self, sqls, alias)
	if self.orderBy then
		if type(self.orderBy) == 'string' then
			sqls[#sqls + 1] = 'ORDER BY'
			sqls[#sqls + 1] = self.orderBy
		else
			sqls[#sqls + 1] = string.format('ORDER BY %s%s %s', alias, 		self.orderBy.name, self.orderBy.order)
		end
	end
end

builder.buildLimits = function(self, sqls, ignoreStart)
	if self.limitTotal and self.limitTotal > 0 then
		if ignoreStart or self.limitStart == 0 then
			sqls[#sqls + 1] = string.format('LIMIT %u', self.limitTotal)
		else
			sqls[#sqls + 1] = string.format('LIMIT %u,%u', self.limitStart, self.limitTotal)
		end
	end
end

return builder