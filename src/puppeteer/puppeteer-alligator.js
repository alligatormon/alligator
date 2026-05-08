const puppeteer = require('puppeteer');
const f = require('/var/lib/alligator/argOptions.js')

process.setMaxListeners(0);

function valueToString(value) {
	if (value === undefined || value === null) {
		return "";
	}
	return String(value);
}

function sanitizeLabelValue(value) {
	return valueToString(value).replace(/"/g, "_").replace(/(\r\n|\n|\r)/gm, "");
}

function metricHelp(metricName) {
	const helpByMetric = {
		puppeteer_Info: "Puppeteer target availability marker.",
		puppeteer_totalSize: "Total transferred response size collected by Performance entries.",
		puppeteer_ErrorFetch: "Fetch/navigation errors while loading the target page.",
		puppeteer_eventConsole: "Console events captured from the target page.",
		puppeteer_eventPageError: "Page runtime errors captured from the target page.",
		puppeteer_eventSourceResponseStatus: "HTTP response status values captured for requested sources.",
		puppeteer_eventRequestFailed: "Failed network requests captured from the target page.",
	};
	return helpByMetric[metricName] || "Puppeteer exported metric value.";
}

function normalizeTransformRules(metricstransform) {
	if (!metricstransform) {
		return [];
	}

	if (Array.isArray(metricstransform)) {
		// Simple rule list format.
		return metricstransform;
	}

	const collected = [];
	const transforms = Array.isArray(metricstransform.transforms) ? metricstransform.transforms : [];
	transforms.forEach((transform) => {
		if (!transform || !Array.isArray(transform.operations)) {
			return;
		}

		transform.operations.forEach((operation) => {
			if (!operation || operation.action !== "update_label" || !operation.label) {
				return;
			}
			const valueActions = Array.isArray(operation.value_actions) ? operation.value_actions : [];
			valueActions.forEach((valueAction) => {
				if (!valueAction || !valueAction.regex) {
					return;
				}
				collected.push({
					metric: transform.include,
					match_type: transform.match_type,
					label: operation.label,
					value_regex: valueAction.regex,
					replacement: valueAction.replacement || valueAction.new_value || "",
					flags: valueAction.flags || "",
					replace_all: valueAction.replace_all === true,
				});
			});
		});
	});

	return collected;
}

function matchesPattern(value, pattern, useRegex, flags) {
	if (!pattern) {
		return true;
	}
	const input = valueToString(value);
	if (useRegex) {
		try {
			const re = new RegExp(pattern, flags || "");
			return re.test(input);
		} catch (err) {
			return false;
		}
	}
	return input === pattern;
}

function applyLabelTransforms(metricName, labels, metricstransform) {
	const rules = normalizeTransformRules(metricstransform);
	if (!rules.length) {
		return labels;
	}

	const output = { ...labels };
	rules.forEach((rule) => {
		if (!rule || !rule.label || !rule.value_regex) {
			return;
		}

		const useMetricRegex = rule.match_type === "regexp" || !!rule.metric_regex;
		const metricPattern = rule.metric_regex || rule.metric || "";
		if (!matchesPattern(metricName, metricPattern, useMetricRegex, rule.metric_flags)) {
			return;
		}

		const useLabelRegex = !!rule.label_regex;
		Object.keys(output).forEach((labelName) => {
			if (!matchesPattern(labelName, rule.label_regex || rule.label, useLabelRegex, rule.label_flags)) {
				return;
			}

			const originalValue = valueToString(output[labelName]);
			try {
				const regexFlags = rule.replace_all ? (rule.flags || "") + "g" : (rule.flags || "");
				const regex = new RegExp(rule.value_regex, regexFlags);
				const replacement = rule.replacement || "";
				output[labelName] = originalValue.replace(regex, replacement);
			} catch (err) {
				// Ignore malformed regex in config and keep original value.
			}
		});
	});

	return output;
}

function pushMetric(ret, metricFamilies, name, labels, val, metricstransform) {
	const metricName = "puppeteer_" + name;
	if (!metricFamilies.has(metricName)) {
		ret.push(`# HELP ${metricName} ${metricHelp(metricName)}`);
		ret.push(`# TYPE ${metricName} gauge`);
		metricFamilies.add(metricName);
	}
	const transformedLabels = applyLabelTransforms(metricName, labels, metricstransform);
	const serialized = Object.entries(transformedLabels)
		.map(([label, value]) => `${label}="${sanitizeLabelValue(value)}"`)
		.join(", ");
	ret.push(`${metricName} {${serialized}} ${val}`);
}

function createMetricPushers(ret, metricFamilies, extra_labels, metricstransform) {
	return {
		perfMetricsPush: function(name, resource, source, entryType, initiatorType, nextHopProtocol, val) {
			const labels = {
				resource: resource,
				source: source.substring(0, 128),
				entryType: entryType,
				initiatorType: initiatorType,
				nextHopProtocol: nextHopProtocol,
				...extra_labels,
			};
			pushMetric(ret, metricFamilies, name, labels, val, metricstransform);
		},
		resourceMetricsPush: function(name, resource, val) {
			const labels = {
				...extra_labels,
				resource: resource,
			};
			pushMetric(ret, metricFamilies, name, labels, val, metricstransform);
		},
		labelMetricsPush: function(name, resource, label, value, val) {
			const labels = {
				resource: resource,
				...extra_labels,
			};
			labels[label] = value;
			pushMetric(ret, metricFamilies, name, labels, val, metricstransform);
		},
		label2MetricsPush: function(name, resource, label1, value1, label2, value2, val) {
			const labels = {
				resource: resource,
				...extra_labels,
			};
			labels[label1] = value1;
			labels[label2] = value2.substring(0, 128);
			pushMetric(ret, metricFamilies, name, labels, val, metricstransform);
		},
	};
}

(async () => {
	ret = []
	metricFamilies = new Set()
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

			let extra_labels = {}
			if ("add_label" in pupValue)
			{
				labels = pupValue["add_label"]
				Object.entries(labels).forEach(([label_name, label_key]) => {
					extra_labels[label_name] = label_key
				})
			}
			const metricstransform = pupValue["metricstransform"] || null;
			const pushers = createMetricPushers(ret, metricFamilies, extra_labels, metricstransform);
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
			const emitConsoleEvents = pupValue["console_events"] === true || pupValue["console_events"] === "true" || pupValue["console_events"] === 1;
			pushers.resourceMetricsPush("Info", resource, 1);
			//console.log("fetch:", resource);
			let resp_code = 0;

			page
			.on('console', consoleObj => {
					//console.log("console is", consoleObj.text()))
					if (emitConsoleEvents) {
						pushers.labelMetricsPush("eventConsole", resource, "text", consoleObj.text(), 1);
					}
			})
			.on('pageerror', ({ message }) => {
					//console.log(message)
					pushers.labelMetricsPush("eventPageError", resource, "text", message.replace(/(\r\n|\n|\r)/gm, ""), 1);
			})
			.on('response', response => {
					//console.log(`${response.status()} ${response.url()}`)
					resp_code = response.status()
					pushers.labelMetricsPush("eventSourceResponseStatus", resource, "source", response.url().substring(0, 128), response.status());
			})
			.on('requestfailed', request => {
					//console.log(`${request.failure().errorText} ${request.url()}`)
					pushers.labelMetricsPush("eventRequestFailed", resource, "source", request.url(), 1);
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

			timeout_param = pupValue["timeout"] || 10;
			timeout_param = timeout_param.toString();
			const milliseconds = timeout_param.match(/\d+\s?\w/g)
				.reduce((acc, cur, i) => {
					var multiplier = 1000;
					 switch (cur.slice(-1)) {
						case 'h':
							multiplier *= 60;
							break;
						case 'm':
							multiplier *= 60;
							break;
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
				pushers.labelMetricsPush("ErrorFetch", resource, "error", error.message, 1);
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
						[ "startTime", "duration", "workerStart", "redirectStart", "redirectEnd", "fetchStart", "domainLookupStart", "domainLookupEnd", "secureConnectionStart", "connectStart", "connectEnd", "requestStart", "responseStart", "responseEnd", "transferSize", "encodedBodySize", "decodedBodySize"].forEach(metric => pushers.perfMetricsPush(metric, resource, entry.name, entry.entryType, entry.initiatorType, entry.nextHopProtocol, entry[metric]))
					}
				});
				return totalSize;
			}

			pushers.resourceMetricsPush("totalSize", resource, totalSize());

			let performanceMetrics = await page._client.send('Performance.getMetrics');
			performanceMetrics.metrics.forEach(entry => {
				pushers.resourceMetricsPush(entry.name, resource, entry.value);
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
