const puppeteer = require('puppeteer');
const f = require('/var/lib/alligator/argOptions.js')

process.setMaxListeners(0);

function perfMetricsPush(ret, name, resource, source, entryType, initiatorType, nextHopProtocol, val)
{
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", source="' + source.substring(0, 128) + '", entryType="' + entryType + '", initiatorType="' + initiatorType + '", nextHopProtocol="' + nextHopProtocol + '"} ' + val);
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
	ret.push('puppeteer_' + name + ' {resource="' + resource + '", ' + label1 + '="' + value1.replace(/"/g, '_') + '", ' + label2 + '="' + value2.replace(/"/g, '_').substring(0,128) + '"} ' + val);
}

(async () => {
	ret = []
	process.argv.forEach(async function (val, index, array) {
		if (index < 2)
			return;

    jsonVal = JSON.parse(val)

    for (const [pupKey, pupValue] of Object.entries(jsonVal)) {
      let proxy_settings = ''
      if ('proxy' in pupValue) {
        proxy_settings = '--proxy-server=' + pupValue['proxy']
      }
	    const args = [
	    	"--disable-setuid-sandbox",
	    	"--no-sandbox",
	    	"--blink-settings=imagesEnabled=false",
        proxy_settings,
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
		  const page = await context.newPage();

		  await page.setCacheEnabled(false);

		  await page.setViewport({
		  	width: 1200,
		  	height: 780,
		  	deviceScaleFactor: 1,
		  });

      if ('headers' in pupValue) {
        //set_headers = {}
        //for (const [key, value] of Object.entries(pupValue["headers"])) {
        //  set_headers[key] = value
        //}
        await page.setExtraHTTPHeaders(pupValue["headers"])
      }

		  let resource = pupKey
		  //console.log("fetch:", resource);

		  page
		    .on('console', consoleObj => {
		  	  //console.log("console is", consoleObj.text()))
		  	  labelMetricsPush(ret, "eventConsole", resource, "text", consoleObj.text(), 1);
		    })
		    .on('pageerror', ({ message }) => {
		  	  //console.log(message)
		  	  labelMetricsPush(ret, "eventPageError", resource, "text", message.replace(/(\r\n|\n|\r)/gm, ""), 1);
		    })
		    .on('response', response => {
		  	  //console.log(`${response.status()} ${response.url()}`)
		  	  labelMetricsPush(ret, "eventSourceResponseStatus", resource, "source", response.url().substring(0, 128), response.status());
		    })
		    .on('requestfailed', request => {
		  	  //console.log(`${request.failure().errorText} ${request.url()}`)
		  	  labelMetricsPush(ret, "eventRequestFailed", resource, "source", request.url(), 1);
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


		  try {
		  	await page.goto(resource, { waitUntil: 'networkidle2', timeout: Number(pupValue["timeout"]) || 30000, });
		  } catch (error) {
		  	labelMetricsPush(ret, "ErrorFetch", resource, "error", error.message, 1);
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
    }
	});
})();
