package main

import "C"

import (
    "fmt"
    "strings"
	"encoding/json"
	"reflect"
	"strconv"

    "github.com/gocql/gocql"
)

var log_level = 0
const (
	L_OFF int = iota
	L_FATAL
	L_ERROR
	L_WARN
	L_INFO
	L_DEBUG
	L_TRACE
)

func logLevelFromMap(conf map[string]interface{}) int {
	level := L_OFF
	if conf["log_level"] != nil {
		val := conf["log_level"].(string)
		if val == "off" {
			level = L_OFF
		} else if val == "fatal" {
			level = L_FATAL
		} else if val == "error" {
			level = L_ERROR
		} else if val == "warn" {
			level = L_WARN
		} else if val == "info" {
			level = L_INFO
		} else if val == "debug" {
			level = L_DEBUG
		} else if val == "trace" {
			level = L_TRACE
		}
	}
	return level
}

func alligatorLog(expectedLevel int, variadic ...any) {
	if log_level >= expectedLevel {
		fmt.Println(variadic...)
	}
}

type query_data_t struct {
    NS string `json:"ns"`
    Field []string `json:"field,omitempty"`
    Make string `json:"make"`
    Expr string `json:"expr"`
}

type parser_data_t struct {
    DB string `json:"db"`
    Queries []string `json:"queries"`
    Type string `json:"type"`
}

//export alligator_call
func alligator_call(script *C.char, data *C.char, arg *C.char, metrics *C.char, conf *C.char, parser_data_str *C.char, response_str *C.char, queries_str *C.char) *C.char {
    strArg := C.GoString(arg)
	queriesStr := C.GoString(queries_str)
	parserDataStr := C.GoString(parser_data_str)

    partsUrl := strings.Split(strArg, "/")
    urls := partsUrl[2]
    keyspace := partsUrl[3]
    usersplit := strings.Split(urls, "@")
    userpass := usersplit[0]
    userpasssplit := strings.Split(userpass, ":")
    user := userpasssplit[0]
    password := userpasssplit[1]

    hosts := strings.Split(usersplit[1], ",")

    //fmt.Println("user", user, "password", password, "keyspace", keyspace, "hosts", hosts)

    cluster := gocql.NewCluster(hosts...)
    cluster.Keyspace = keyspace

    cluster.Authenticator = gocql.PasswordAuthenticator {
            Username: user,
            Password: password,
    }

    session, err := cluster.CreateSession()
    if err != nil {
        alligatorLog(L_ERROR, "Failed to connect to Cassandra: %v", err)
        return C.CString("")
    }
    defer session.Close()

    if queriesStr != "" {
        metricsTree := []string{}
		alligatorLog(L_INFO, "cassandra queriesStr is", queriesStr)
	    queriesDt := []query_data_t{}
	    err := json.Unmarshal([]byte(queriesStr), &queriesDt)
        if err != nil {
		    alligatorLog(L_ERROR, "get query data", queriesStr, " error:", err)
            return C.CString("")
        }

        for _, parserDt := range queriesDt {
		    alligatorLog(L_INFO, "cassandra run query:", parserDt.Expr)

            dataFields := map[string]float64{}
            existsFields := map[string]bool{}
            for _, field := range parserDt.Field {
                dataFields[field] = 0
                existsFields[field] = true
            }
            labelFields := map[string]string{}


		    alligatorLog(L_ERROR, "run cassandra query:", parserDt.Expr)
            iter := session.Query(parserDt.Expr).Iter()
            defer iter.Close()

            res := make(map[string]interface{})
            for iter.MapScan(res) {
                for name, value := range res {
                    v := reflect.ValueOf(value)
                    if existsFields[name] {
                        if v.Kind() == reflect.Bool {
                            if value.(bool) == true {
                                dataFields[name] = 1
                            }
                        } else if v.Kind() == reflect.Int {
                            dataFields[name] += float64(value.(int))
                        } else if v.Kind() == reflect.Int8 {
                            dataFields[name] += float64(value.(int8))
                        } else if v.Kind() == reflect.Int16 {
                            dataFields[name] += float64(value.(int16))
                        } else if v.Kind() == reflect.Int32 {
                            dataFields[name] += float64(value.(int32))
                        } else if v.Kind() == reflect.Int64 {
                            dataFields[name] += float64(value.(int64))
                        } else if v.Kind() == reflect.Uint8 {
                            dataFields[name] += float64(value.(uint8))
                        } else if v.Kind() == reflect.Uint16 {
                            dataFields[name] += float64(value.(uint16))
                        } else if v.Kind() == reflect.Uint32 {
                            dataFields[name] += float64(value.(uint32))
                        } else if v.Kind() == reflect.Uint64 {
                            dataFields[name] += float64(value.(uint64))
                        } else if v.Kind() == reflect.Float32 {
                            dataFields[name] += float64(value.(float32))
                        } else if v.Kind() == reflect.Float64 {
                            dataFields[name] += float64(value.(float64))
                        }
                    } else {
                        var err error
                        if v.Kind() == reflect.Bool {
                            if value.(bool) == true {
                                labelFields[name] = "true"
                            } else {
                                labelFields[name] = "else"
                            }
                        } else if v.Kind() == reflect.Int {
                            labelFields[name] = strconv.Itoa(value.(int))
                        } else if v.Kind() == reflect.Int8 {
                            labelFields[name] = strconv.FormatInt(int64(value.(int8)), 10)
                        } else if v.Kind() == reflect.Int16 {
                            labelFields[name] = strconv.FormatInt(int64(value.(int16)), 10)
                        } else if v.Kind() == reflect.Int32 {
                            labelFields[name] = strconv.FormatInt(int64(value.(int32)), 10)
                        } else if v.Kind() == reflect.Int64 {
                            labelFields[name] = strconv.FormatInt(int64(value.(int64)), 10)
                        } else if v.Kind() == reflect.Uint8 {
                            labelFields[name] = strconv.FormatUint(uint64(value.(uint8)), 10)
                        } else if v.Kind() == reflect.Uint16 {
                            labelFields[name] = strconv.FormatUint(uint64(value.(uint16)), 10)
                        } else if v.Kind() == reflect.Uint32 {
                            labelFields[name] = strconv.FormatUint(uint64(value.(uint32)), 10)
                        } else if v.Kind() == reflect.Uint64 {
                            labelFields[name] = strconv.FormatUint(uint64(value.(uint64)), 10)
                        } else if v.Kind() == reflect.Float32 {
                            labelFields[name] = strconv.FormatFloat(float64(value.(float32)), 'f', 3, 32)
                        } else if v.Kind() == reflect.Float64 {
                            labelFields[name] = strconv.FormatFloat(value.(float64), 'f', 3, 64)
                        } else if v.Kind() == reflect.String {
                            labelFields[name] = value.(string)
                        }

                        if err != nil { 
		                    alligatorLog(L_ERROR, "error convert data", value, "error:", err)
                        }
                    }
                }

                for name, value := range dataFields {
                    MetricSample := name + " {"
                    for label_name, label_value := range labelFields {
				        MetricSample += fmt.Sprintf(`%s="%s"`, label_name, label_value)
                    }
                    MetricSample += "} " + strconv.FormatFloat(value, 'f', 3, 64)
                    metricsTree = append(metricsTree, MetricSample)
                }
            }
        }

        joined := strings.Join(metricsTree, "\n")
        return C.CString(joined)
    }

    if parserDataStr != "" {
	    parserDt := parser_data_t{}
	    err := json.Unmarshal([]byte(parserDataStr), &parserDt)
        if err != nil {
		    alligatorLog(L_ERROR, "get parser data", parserDataStr, " error:", err)
            return C.CString("")
        }

        if parserDt.Type == "insert" {
            for _, metricsStr := range parserDt.Queries {
                err = session.Query(metricsStr).Exec()
                if err != nil {
                    alligatorLog(L_ERROR, "Failed to insert data: %v", err)
                    return C.CString("")
                }
                alligatorLog(L_INFO, "Inserted", metricsStr)
            }
            return C.CString("")
        }
    }
	return C.CString("")
}

func main() {}
