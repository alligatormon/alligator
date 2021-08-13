function alligator_metrics(arg, metrics, conf)
	a = tonumber(arg);
	b = a * a;
	return "lua_script " .. tostring(b);
end
