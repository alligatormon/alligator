package main

import "C"

import (
    "fmt"
)

//export alligator_call
func alligator_call(script *C.char, data *C.char, arg *C.char, metrics *C.char, conf *C.char) *C.char {
	str := C.GoString(arg)
	mstr := C.GoString(metrics)
	cstr := C.GoString(conf)
	fmt.Println("hello from Go! arg is ", str, "metrics is", mstr, "conf is", cstr)
	ret := C.CString(str + " 49")
	return ret
}

func main() {}
