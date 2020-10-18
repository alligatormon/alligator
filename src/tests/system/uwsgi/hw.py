import os
def application(env, start_response):
    start_response('200 OK', [('Content-Type','text/plain')])

    response = "hello, world!"
    return [response.encode('utf-8')]
