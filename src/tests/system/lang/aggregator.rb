def aggregator_method(arg, metrics, conf, parser_data)
	print("arg mruby is: ", parser_data, "\n")
	code = parser_data.split(',')[0].split(':')[1].strip
	puts("code is", code)
	alligator_set_metrics("code_response " + code)
end;
