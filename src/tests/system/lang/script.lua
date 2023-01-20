function alligator_metrics(arg, metrics, conf, parser_data, response)
	a = tonumber(arg);
	b = a * a;
	return "lua_script " .. tostring(b);
end
