## Celery

To enable the collection of statistics from celery, use the following option:
```
aggregate {
    flower http://127.0.0.1:5555/;
}
```

### To collect statistics
The flower is must be runned on celery instance. This is a web-interace that provides statistics about celery processing.
