const puppeteer = require('puppeteer');
const f = require('/var/lib/alligator/argOptions.js')

process.setMaxListeners(0);

function perfMetricsPush(ret, name, resource, source, entryType, initiatorType, nextHopProtocol, val)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", source="' + source.substring(0, 100) + '", entryType="' + entryType + '", initiatorType="' + initiatorType + '", nextHopProtocol="' + nextHopProtocol + '"} ' + val);
}

function resourceMetricsPush(ret, name, resource, val)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '"} ' + val);
}

function labelMetricsPush(ret, name, resource, label, value, val)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", ' + label + '="' + value.replace(/"/g, '_') + '"} ' + val);
}

function label2MetricsPush(ret, name, resource, label1, value1, label2, value2, val)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", ' + label1 + '="' + value1.replace(/"/g, '_') + '", ' + label2 + '="' + value2.replace(/"/g, '_').substring(0,100) + '"} ' + val);
}

(async () => {
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

	ret = []
	process.argv.forEach(async function (val, index, array) {
		if (index < 2)
			return;

		const browser = await puppeteer.launch(options);
		const context = await browser.createIncognitoBrowserContext();
		const page = await context.newPage();

		await page.setCacheEnabled(false);

		await page.setViewport({
			width: 1200,
			height: 780,
			deviceScaleFactor: 1,
		});

		let resource = val;
		//console.log("fetch:", resource);

		page
		  .on('console', consoleObj => {
			//console.log("console is", consoleObj.text()))
			labelMetricsPush(ret, "eventConsole", resource, "text", consoleObj.text(), 1);
		  })
		  .on('pageerror', ({ message }) => {
			//console.log(message)
			labelMetricsPush(ret, "eventPageError", resource, "text", message, 1);
		  })
		  .on('response', response => {
			//console.log(`${response.status()} ${response.url()}`)
			labelMetricsPush(ret, "eventSourceResponseStatus", resource, "source", response.url(), response.status());
		  })
		  .on('requestfailed', request => {
			//console.log(`${request.failure().errorText} ${request.url()}`)
			label2MetricsPush(ret, "eventRequestFailed", resource, "text", request.failure().errorText, "source", request.url(), 1);
		  })
		  //.on('response', response => { console.log(response) })


		try {
			await page.goto(resource, { waitUntil: 'networkidle2', timeout: 30000, });
		} catch (error) {
			//console.log(error);
		}

		const perfEntries = JSON.parse(
			await page.evaluate(() => JSON.stringify(performance.getEntries()))
		);


		totalSize = () => {
			let totalSize = 0;
			perfEntries.forEach(entry => {
				if (entry.transferSize > 0) {
					totalSize += entry.transferSize;
					[ "startTime", "duration", "workerStart", "redirectStart", "redirectEnd", "fetchStart", "domainLookupStart", "domainLookupEnd", "secureConnectionStart", "connectStart", "connectEnd", "requestStart", "responseStart", "responseEnd", "transferSize", "encodedBodySize", "decodedBodySize"].forEach(metric => perfMetricsPush(ret, metric, resource, entry.name, entry.entryType, entry.initiatorType, entry.nextHopProtocol, entry[metric]))
				}
			});
			return totalSize;
		}

		resourceMetricsPush(ret, "totalSize", resource, totalSize());

		let performanceMetrics = await page._client.send('Performance.getMetrics');
		performanceMetrics.metrics.forEach(entry => {
			resourceMetricsPush(ret, entry.name, resource, entry.value);
		});

		console.log(ret.join("\n"));

		await browser.close();
	});
})();

