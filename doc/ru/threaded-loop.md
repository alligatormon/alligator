**Language / Язык:** [English](../threaded-loop.md) | [Русский](threaded-loop.md)

# Threaded Loop
Этот контекст создаёт пул потоков с активированным event loop для назначения отдельных задач. Обычно используется для aggregator, чтобы распределять разные aggregator по нескольким потокам.

## Name
Задаёт имя threaded loop, которое служит ключом для операций API.

## Threads
Задаёт число потоков в этом пуле.

## Пример

Эта конфигурация создаёт два пула потоков с 3 и 12 потоками. Задача, получающая ресурсы с example.com, будет назначена большему пулу с 12 потоками, а остальные задачи — меньшему пулу с 3 потоками.

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
