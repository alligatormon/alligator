const puppeteer = require('puppeteer');
const f = require('/var/lib/alligator/argOptions.js')

process.setMaxListeners(0);

function perfMetricsPush(ret, name, resource, source, entryType, initiatorType, nextHopProtocol, val, extra_labels)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", source="' + source.substring(0, 128) + extra_labels + '", entryType="' + entryType + '", initiatorType="' + initiatorType + '", nextHopProtocol="' + nextHopProtocol + '"} ' + val);
}

function resourceMetricsPush(ret, name, resource, val, extra_labels)
{
	ret.push('puppeteer_' + name + ' {' + extra_labels + 'resource="' + resource + '"} ' + val);
}

function labelMetricsPush(ret, name, resource, label, value, val, extra_labels)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", ' + extra_labels + label + '="' + value.replace(/"/g, '_') + '"} ' + val);
}

function label2MetricsPush(ret, name, resource, label1, value1, label2, value2, val, extra_labels)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", ' + extra_labels + label1 + '="' + value1.replace(/"/g, '_') + '", ' + label2 + '="' + value2.replace(/"/g, '_').substring(0,128) + '"} ' + val);
}

(async () => {
	ret = []
	process.argv.forEach(async function (val, index, array) {
		if (index < 2)
			return;

		jsonVal = JSON.parse(val)

		const args = [
			"--disable-setuid-sandbox",
			"--no-sandbox",
			"--blink-settings=imagesEnabled=false",
		];

		stdOptions = {
		args,
		headless: true,
		ignoreHTTPSErrors: true,
		}

		const options = {
		...stdOptions,
		...argOptions,
		};

		const browser = await puppeteer.launch(options);
		const context = await browser.createIncognitoBrowserContext();

		await Promise.all(Object.entries(jsonVal).map(async ([pupKey, pupValue]) => {

			let extra_labels = ""
			if ("add_label" in pupValue)
			{
				labels = pupValue["add_label"]
				Object.entries(labels).forEach(([label_name, label_key]) => {
					extra_labels += label_name + '="' + label_key + '", '
				})
			}
			const page = await context.newPage();

			await page.setCacheEnabled(false);

			await page.setViewport({
				width: 640,
				height: 360,
				deviceScaleFactor: 1,
			});

			if ('headers' in pupValue) {
			//set_headers = {}
			//for (const [key, value] of Object.entries(pupValue["headers"])) {
			//	set_headers[key] = value
			//}
				await page.setExtraHTTPHeaders(pupValue["headers"])
			}

			let resource = pupKey
			resourceMetricsPush(ret, "Info", resource, 1, extra_labels);
			//console.log("fetch:", resource);
			let resp_code = 0;

			page
			.on('console', consoleObj => {
					//console.log("console is", consoleObj.text()))
					labelMetricsPush(ret, "eventConsole", resource, "text", consoleObj.text(), 1, extra_labels);
			})
			.on('pageerror', ({ message }) => {
					//console.log(message)
					labelMetricsPush(ret, "eventPageError", resource, "text", message.replace(/(\r\n|\n|\r)/gm, ""), 1, extra_labels);
			})
			.on('response', response => {
					//console.log(`${response.status()} ${response.url()}`)
					resp_code = response.status()
					labelMetricsPush(ret, "eventSourceResponseStatus", resource, "source", response.url().substring(0, 128), response.status(), extra_labels);
			})
			.on('requestfailed', request => {
					//console.log(`${request.failure().errorText} ${request.url()}`)
					labelMetricsPush(ret, "eventRequestFailed", resource, "source", request.url(), 1, extra_labels);
			})
			//.on('response', response => { console.log(response) })

			if ("post_data" in pupValue) {
				await page.setRequestInterception(true);
				page.on('request', interceptedRequest => {
					var data = {
					'method': 'POST',
					'postData': pupValue["post_data"],
					}
				interceptedRequest.continue(data)
				});
			}

			if ("env" in pupValue) {
				await page.setExtraHTTPHeaders(pupValue["env"])
			}

			timeout_param = pupValue["timeout"] || 30000;
			timeout_param = timeout_param.toString();
			const milliseconds = timeout_param.match(/\d+\s?\w/g)
				.reduce((acc, cur, i) => {
					var multiplier = 1000;
					 switch (cur.slice(-1)) {
						case 'h':
							multiplier *= 60;
						case 'm':
							multiplier *= 60;
						case 's':
							return ((parseInt(cur)?parseInt(cur):0) * multiplier) + acc;
						default:
							return ((parseInt(cur)?parseInt(cur):0) * multiplier) + acc;
					}
					return acc;
				}, 0);

			try {
				await page.goto(resource, { waitUntil: 'networkidle2', timeout: milliseconds, });
			} catch (error) {
				labelMetricsPush(ret, "ErrorFetch", resource, "error", error.message, 1, extra_labels);
			}

			const perfEntries = []
			try {
				const perfEntries = JSON.parse(
					await page.evaluate(() => {
						try {
							JSON.stringify(performance.getEntries())
						}
						catch(err) {
						}
					})
				);
			}
			catch (err) {
			}
			totalSize = () => {
				let totalSize = 0;
				perfEntries.forEach(entry => {
					if (entry.transferSize > 0) {
						totalSize += entry.transferSize;
						[ "startTime", "duration", "workerStart", "redirectStart", "redirectEnd", "fetchStart", "domainLookupStart", "domainLookupEnd", "secureConnectionStart", "connectStart", "connectEnd", "requestStart", "responseStart", "responseEnd", "transferSize", "encodedBodySize", "decodedBodySize"].forEach(metric => perfMetricsPush(ret, metric, resource, entry.name, entry.entryType, entry.initiatorType, entry.nextHopProtocol, entry[metric], extra_labels))
					}
				});
				return totalSize;
			}

			resourceMetricsPush(ret, "totalSize", resource, totalSize(), extra_labels);

			let performanceMetrics = await page._client.send('Performance.getMetrics');
			performanceMetrics.metrics.forEach(entry => {
				resourceMetricsPush(ret, entry.name, resource, entry.value, extra_labels);
			});

			console.log(ret.join("\n"));

			if ("screenshot" in pupValue) {
				let necessary_code = pupValue["screenshot"]["minimum_code"] || 400;
				let fullPage = pupValue["screenshot"]["fullPage"] || false
				let type = pupValue["screenshot"]["type"] || "png"
				let dir = pupValue["screenshot"]["dir"] || "/var/lib/alligator/"
				let utcStr = new Date().toISOString();
				let path = dir + "/" + pupKey.replaceAll("/", "-") + "-" + utcStr + "." + type
				if (resp_code >= necessary_code) {
					await page.screenshot({
						path: path,
						type: type,
						fullPage: fullPage,
					});
				}
			}

			await page.close();
		}));

		await browser.close();
	});
})();
