package main

import "C"

import (
	"context"
	"encoding/json"
	"fmt"
	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/bson/primitive"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
	"strconv"
	"strings"
	"reflect"
	"time"
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

var skip_db = map[string]bool{}

func MongoParseMetric(Name string, StatsName string, DbName string, Params map[string]interface{}, Identificator string, Doc interface{}, Metrics *[]string) {

	Labels := ""
	if DbName != "" {
		Labels = "{ db=\"" + DbName + "\", "
	}

	if Identificator != "" {
		Labels += "identificator=\"" + Identificator + "\", "
	}

	for LName, LVal := range Params {
		Labels += LName + "=\"" + LVal.(string) + "\", "
	}

	if Labels != "" {
		Labels += "}"
	}

	switch v := Doc.(type) {
	case int32:
		//fmt.Println("\t", Name, v)
		*Metrics = append(*Metrics, "mongodb_"+StatsName+"_"+Name+" "+Labels+" "+strconv.Itoa(int(v)))
	case float64:
		//fmt.Println("\t", Name, v)
		*Metrics = append(*Metrics, "mongodb_"+StatsName+"_"+Name+" "+Labels+" "+fmt.Sprintf("%f", v))
	case primitive.M:
		for ObjName, ObjDoc := range v {
			NewName := Name
			if Params["description"] == nil {
				if strings.Contains(ObjName, " ") {
					Params["description"] = ObjName
				}
			}

			//fmt.Println("Identificator is", Identificator, "Name is", Name)
			if strings.Contains(Name, "-Replication") {
				Params["host"] = Identificator
			} else if strings.Contains(Name, "hosts") {
				Params["host"] = Identificator
			} else if Identificator != "" {
				NewName += "_" + strings.Replace(strings.Replace(Identificator, ".", "_", -1), ":", "_", -1)
			}

			MongoParseMetric(NewName, StatsName, DbName, Params, ObjName, ObjDoc, Metrics)
		}
	default:
		//fmt.Println("\t", Name, v)
	}
}

func MongodbStatsMetrics(Stats *mongo.SingleResult, Metrics *[]string, StatsName string, DbName string) {
	if skip_db[DbName] {
		return
	}
	var document bson.M
	err := Stats.Decode(&document)
	if err != nil {
		alligatorLog(L_ERROR, "err Stats.Decode is", err)
		return
	}

	//fmt.Println(StatsName, ":\n", document)
	for ObjName, Doc := range document {
		MongoParseMetric(ObjName, StatsName, DbName, map[string]interface{}{}, "", Doc, Metrics)
	}
	return
}

func MongoRunCommand(db *mongo.Database, filter bson.M, Metrics *[]string, StatsName string, DbName string) {
	result := db.RunCommand(context.Background(), filter)
	MongodbStatsMetrics(result, Metrics, StatsName, DbName)
}

func MongodbTopMetrics(Stats *mongo.SingleResult, Metrics *[]string) {
	var document bson.M
	err := Stats.Decode(&document)
	if err != nil {
		alligatorLog(L_TRACE, "err Stats.Decode is", err)
		return
	}

	if document["totals"] != nil {
		Totals := document["totals"].(primitive.M)
		for ObjName, Doc := range Totals {
			//fmt.Println("Objname is", ObjName, "Doc is", Doc)
			MongoParseMetric("totals", "Top", "admin", map[string]interface{}{"obj": ObjName}, "", Doc, Metrics)
		}
	}
}

func MongoTopCommand(db *mongo.Database, filter bson.M, Metrics *[]string) {
       result := db.RunCommand(context.Background(), filter)
       MongodbTopMetrics(result, Metrics)
}

func queryMetricsWalk(cursor *mongo.Cursor, results []interface{}, Metrics *[]string, StatsName string, DbName string, collName string) {
    counter := 0
    array := 0
    slice := 0
    iface := 0
    str := 0
    mp := 0
	for _, result := range results {
        counter++
		cursor.Decode(&result)
		output, err := json.Marshal(result)
		if err != nil {
		    alligatorLog(L_ERROR, "query marshal cursor error:", err)
		}
		//fmt.Printf("%s\n", reflect.ValueOf(output))
        Type := reflect.ValueOf(output)
        if (Type.Kind() == reflect.Array) {
            queryMetricsWalk(cursor, result.([]interface{}), Metrics, StatsName + "_", DbName, collName)
            array++
        } else if Type.Kind() == reflect.Slice {
            queryMetricsWalk(cursor, result.([]interface{}), Metrics, StatsName + "_", DbName, collName)
            slice++
        } else if Type.Kind() == reflect.Interface {
            iface++
        } else if Type.Kind() == reflect.String {
            str++
        } else if Type.Kind() == reflect.Map {
            mp++
        }
	}
	fmt.Printf("count: %d, array: %d, slice: %d, iface: %d, str: %d, map: %d\n", counter, array, slice, iface, str, mp)
}

type parser_data_t struct {
    DB string `json:"db"`
    Type string `json:"type"`
}

type query_data_t struct {
    NS string `json:"ns"`
    Field string `json:"field,omitempty"`
    Make string `json:"make"`
    Expr string `json:"expr"`
}

//export alligator_call
func alligator_call(script *C.char, data *C.char, arg *C.char, metrics *C.char, conf *C.char, parser_data_str *C.char, response_str *C.char, queries_str *C.char) *C.char {
	skip_db["local"] = true
	skip_db["config"] = true
	strArg := C.GoString(arg)
	scriptStr := C.GoString(script)
	queriesStr := C.GoString(queries_str)
	parserDataStr := C.GoString(parser_data_str)
	metricsStr := C.GoString(metrics)
	//fmt.Println("hello from Go! arg is ", strArg, "metrics is", metrics, "conf is", conf, "script is", scriptStr, "query", queriesStr, "parser data", parserDataStr)

	Conf := map[string]interface{}{}
	err := json.Unmarshal([]byte(scriptStr), &Conf)

	log_level = logLevelFromMap(Conf)

	AllDbScrape := false
	DbScrape := map[string]bool{}
	if Conf["dbs"] != nil {
		alligatorLog(L_INFO, "dbs param found, check for configuration")
		Dbs := Conf["dbs"].(string)
		DbSplit := strings.Split(Dbs, ",")
		for _, Name := range DbSplit {
			if Name == "*" {
				alligatorLog(L_INFO, "dbs param wildcard found '*', all db collectors have been enabled")
				AllDbScrape = true
				break
			}
			alligatorLog(L_INFO, "dbs add param '", Name, "'")
			DbScrape[Name] = true
		}
	}

	CollScrape := map[string]bool{}
	AllCollScrape := false
	if Conf["colls"] != nil {
		alligatorLog(L_INFO, "colls param found, check for configuration")
		Colls := Conf["colls"].(string)
		CollSplit := strings.Split(Colls, ",")
		for _, Name := range CollSplit {
			if Name == "*" {
				AllCollScrape = true
				alligatorLog(L_INFO, "colls param wildcard found '*', all colls collectors have been enabled")
				break
			}
			alligatorLog(L_INFO, "colls add param '", Name, "'")
			CollScrape[Name] = true
		}
	}

	CollectorsScrape := map[string]bool{}
	AllCollectorsScrape := false
	if Conf["collectors"] != nil {
		alligatorLog(L_INFO, "collectors param found, check for configuration")
		Collectors := Conf["collectors"].(string)
		CollectorsSplit := strings.Split(Collectors, ",")
		for _, Name := range CollectorsSplit {
			if Name == "*" {
				AllCollectorsScrape = true
				alligatorLog(L_INFO, "collectors param wildcard found '*', all collectors have been enabled")
				break
			}
			alligatorLog(L_INFO, "collectors add param '", Name, "'")
			CollectorsScrape[Name] = true
		}
	}

	var Mtree []string

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	client, err := mongo.Connect(ctx, options.Client().ApplyURI(strArg))
	if err != nil {
		alligatorLog(L_ERROR, "err mongo.Connect is", err)
		return C.CString("")
	}

    if parserDataStr != "" {
	    parserDt := parser_data_t{}
	    err := json.Unmarshal([]byte(parserDataStr), &parserDt)
        if err != nil {
		    alligatorLog(L_ERROR, "get parser data", parserDataStr, " error:", err)
            return C.CString("")
        }

        if parserDt.Type == "insert" {
            var doc []interface{}
            bson.UnmarshalExtJSON([]byte(metricsStr), true, &doc)

            if err != nil {
		        alligatorLog(L_ERROR, "insert unmarshal error:", err)
            }
            DB := strings.Split(parserDt.DB, "/")
            dbName := DB[0]
            collName := "alligator_metrics"
            if len(DB) == 2 {
                if DB[1] != "" {
                    collName = DB[1]
                }
            }
	        db := client.Database(dbName).Collection(collName)
            _, err := db.InsertMany(
                context.TODO(),
                doc,
            )
            if err != nil {
		        alligatorLog(L_ERROR, "insertMany error:", err)
            }

            return C.CString("")
        }
    }

    if queriesStr != "" {
		alligatorLog(L_INFO, "mongodb queriesStr is", queriesStr)
	    queriesDt := []query_data_t{}
	    err := json.Unmarshal([]byte(queriesStr), &queriesDt)
        if err != nil {
		    alligatorLog(L_ERROR, "get query data", queriesStr, " error:", err)
            return C.CString("")
        }

        for _, parserDt := range queriesDt {
            var doc interface{}
		    alligatorLog(L_INFO, "mongodb run query:", parserDt.Expr)
            bson.UnmarshalExtJSON([]byte(parserDt.Expr), true, &doc)

            if err != nil {
		        alligatorLog(L_ERROR, "query unmarshal error:", err)
            }
            DB := strings.Split(parserDt.NS, "/")
            dbName := DB[0]
            collName := "alligator_metrics"
            if len(DB) == 2 {
                if DB[1] != "" {
                    collName = DB[1]
                }
            }
	        db := client.Database(dbName).Collection(collName)
            cursor, err := db.Find(
                context.TODO(),
                doc,
            )
            if err != nil {
		        alligatorLog(L_ERROR, "Find error:", err)
            }

            var results []interface{}
            err = cursor.All(context.TODO(), &results)
            if err != nil {
		        alligatorLog(L_ERROR, "query get cursor error:", err)
            }
            text := "["
            for i, result := range results {
                if i != 0 {
                    text += ","
                }
                res, _ := bson.MarshalExtJSON(result, false, false)
                text += string(res)
            }
            text += "]"
	        return C.CString(text)

        }
        return C.CString("")
    }

	if Conf["no_collect"] != nil {
        NoCollect := Conf["no_collect"].(string)
        if NoCollect == "true" {
		    alligatorLog(L_INFO, "mongo skip gather db data")
            return C.CString("")
        }
    }

	db := client.Database("admin")

	if (AllCollectorsScrape || CollectorsScrape["top"]) {
		alligatorLog(L_TRACE, "mongodb run top")
		MongoTopCommand(db, bson.M{"top": ""}, &Mtree)
	}
	if (AllCollectorsScrape || CollectorsScrape["buildInfo"]) {
		alligatorLog(L_TRACE, "mongodb run buildInfo")
		MongoRunCommand(db, bson.M{"buildInfo": ""}, &Mtree, "Build", "admin")
	}
	if (AllCollectorsScrape || CollectorsScrape["isMaster"]) {
		alligatorLog(L_TRACE, "mongodb run isMaster")
		MongoRunCommand(db, bson.M{"isMaster": ""}, &Mtree, "Master", "admin")
	}

	if (AllCollectorsScrape || CollectorsScrape["replSetGetStatus"]) {
		alligatorLog(L_TRACE, "mongodb run replSetGetStatus")
		MongoRunCommand(db, bson.M{"replSetGetStatus": ""}, &Mtree, "ReplicationStatus", "admin")
	}
	if (AllCollectorsScrape || CollectorsScrape["getDiagnosticData"]) {
		alligatorLog(L_TRACE, "mongodb run getDiagnosticData")
		MongoRunCommand(db, bson.M{"getDiagnosticData": ""}, &Mtree, "DiagnosticData", "admin")
	}

	ListDatabaseNames, err := client.ListDatabaseNames(ctx, bson.D{{}})
	alligatorLog(L_TRACE, "list databases:", ListDatabaseNames)
	if err != nil {
		alligatorLog(L_ERROR, "err client.ListDatabaseNames is", err)
		return C.CString("")
	}
	for _, DbName := range ListDatabaseNames {
		alligatorLog(L_INFO, "====DB:", DbName, "===")
		if AllDbScrape || DbScrape[DbName] {
			db := client.Database(DbName)

			CollNames, err := db.ListCollectionNames(ctx, bson.D{{}})
			alligatorLog(L_INFO, "list collections:", CollNames)
			if err != nil {
				alligatorLog(L_ERROR, "====DB:", DbName, "error is", err)
				continue
			}
			for _, CollName := range CollNames {
				alligatorLog(L_INFO, "========DB:", DbName, "Col:", CollName, "========")
				if AllCollScrape || CollScrape[CollName] {
					if (AllCollectorsScrape || CollectorsScrape["coll"]) {
						MongoRunCommand(db, bson.M{"collStats": CollName}, &Mtree, "Collection", DbName)
						MongoRunCommand(db, bson.M{"indexStats": CollName}, &Mtree, "Index", DbName)
						MongoRunCommand(db, bson.M{"dataSize": CollName}, &Mtree, "DataSize", DbName)
					}
				}
			}

			if (AllCollectorsScrape || CollectorsScrape["db"]) {
				MongoRunCommand(db, bson.M{"connPoolStats": ""}, &Mtree, "ConnectionPoolStatus", DbName)
				MongoRunCommand(db, bson.M{"dbStats": ""}, &Mtree, "DBStats", DbName)
				MongoRunCommand(db, bson.M{"features": ""}, &Mtree, "Features", DbName)
				MongoRunCommand(db, bson.M{"serverStatus": ""}, &Mtree, "ServerStatus", DbName)
			}
		}
	}

	Mstring := strings.Join(Mtree[:], "\n")

	return C.CString(Mstring)
}

func main() {}
