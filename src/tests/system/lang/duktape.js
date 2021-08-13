function run(arg, metrics, conf) {

	var duktape = arg;
	var metric = "metric";
	var val = 25;
	
	var result = duktape + "_" + metric + " " + val;
	return result;
}
