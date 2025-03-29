# Threaded Loop
This context creates a thread pool with an activated event loop to assign specific tasks. It is typically used for aggregators to separate different aggregators across multiple threads.

## Name
Specifies the name of the threaded loop, which serves as the key for API operations.

## Threads
Specifies the number of threads working in this thread pool.

## Example

This configuration creates two thread pools with 3 and 12 threads. The task that obtains resources from example.com will be assigned to the larger thread pool with 12 threads, while the other task will be assigned to the smaller pool with 3 threads.

``` 
threaded_loop {
        name small;
        threads 3;
}

threaded_loop {
        name large;
        threads 12;
}

aggregate {
        blackbox https://example.com threaded_loop_name=large;
        blackbox https://google.com threaded_loop_name=small;
        blackbox https://linkedin.com threaded_loop_name=small;
}
```
