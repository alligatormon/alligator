package main

import "C"

import (
	"fmt"
)

//export alligator_call
func alligator_call(script *C.char, data *C.char, arg *C.char, metrics *C.char, conf *C.char, parser_data *C.char, response *C.char, queries *C.char) *C.char {
	_ = script
	_ = data
	_ = parser_data
	_ = response
	_ = queries
	str := C.GoString(arg)
	mstr := C.GoString(metrics)
	cstr := C.GoString(conf)
	fmt.Println("hello from Go! arg is ", str, "metrics is", mstr, "conf is", cstr)
	return C.CString(str + " 49")
}

func main() {}
