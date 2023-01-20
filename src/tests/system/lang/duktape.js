function run(arg, metrics, conf, parser_data) {

	var duktape = arg;
	var metric = "metric";
	var val = 25;
	
	var result = duktape + "_" + metric + " " + val;
	//return result, 'test_response';
	return ['test_metric', 'test_response'];
}
