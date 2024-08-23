# Puppeteer configuration context
It allows the use of the Puppeteer JS module to check a list of sites for loading statistics.

## Requirements
The puppeteer context requires installed nodejs and such modules in the system:
```
puppeteer@14.3.0
ps-node-promise-es6
```

## Overview of using puppeteer configuration
```
puppeteer {
    https://google.com;
}
```

## Example in JSON format (extended)
Below is an example with options that only working in the JSON format of the configuration file:
```
"puppeteer" {
    "https://google.com": {
        "headers": {
            "Connection": "close"
            "Host": "google.com"
        },
        "post_data": "body request",
        "timeout": 30000,
        "screenshot": {
            "minimum_code": 400,
            "type": "png",
            "dir": "/var/lib/alligator/"
        }
    }
}
```

## post\_data
The values to send in the HTTP POST request body.


## timeout
Specifies the timeout to wait for a response from resources on the site. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)


## headers, env
This option can set extra headers to request the site.


## screenshot
Enables screenshot functionality for Puppeteer. It works only for specified response codes.

### minimum\_code
Specifies the minimum response code to trigger the capturing of a screenshot.

### type
Specifies the type of screenshot. Currently, only only `png` is supported.

### dir
Specifies the directory to save screenshots.
