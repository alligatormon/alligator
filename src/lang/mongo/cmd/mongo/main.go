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
	"time"
)

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
	var document bson.M
	err := Stats.Decode(&document)
	if err != nil {
		fmt.Println("err Stats.Decode is", err)
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
		fmt.Println("err Stats.Decode2 is", err)
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

//export alligator_call
func alligator_call(script *C.char, data *C.char, arg *C.char, metrics *C.char, conf *C.char, parser_data_str *C.char, response_str *C.char) *C.char {
	strArg := C.GoString(arg)
	//mstr := C.GoString(metrics)
	//cstr := C.GoString(conf)
	scriptStr := C.GoString(script)
	//fmt.Println("hello from Go! arg is ", strArg, "metrics is", mstr, "conf is", cstr, "script is", scriptStr)
	//var document bson.M

	Conf := map[string]interface{}{}
	err := json.Unmarshal([]byte(scriptStr), &Conf)
	AllDbScrape := false
	DbScrape := map[string]interface{}{}
	if Conf["dbs"] != nil {
		Dbs := Conf["dbs"].(string)
		DbSplit := strings.Split(Dbs, ",")
		for _, Name := range DbSplit {
			if Name == "*" {
				AllDbScrape = true
				break
			}
			DbScrape[Name] = true
		}
	}
	CollScrape := map[string]interface{}{}
	AllCollScrape := false
	if Conf["colls"] != nil {
		Colls := Conf["colls"].(string)
		CollSplit := strings.Split(Colls, ",")
		for _, Name := range CollSplit {
			if Name == "*" {
				AllDbScrape = true
				break
			}
			CollScrape[Name] = true
		}
	}

	var Mtree []string

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	client, err := mongo.Connect(ctx, options.Client().ApplyURI(strArg))
	if err != nil {
		fmt.Println("err mongo.Connect is", err)
		return C.CString("")
	}

	db := client.Database("admin")

	MongoTopCommand(db, bson.M{"top": ""}, &Mtree)
	MongoRunCommand(db, bson.M{"buildInfo": ""}, &Mtree, "Build", "admin")
	MongoRunCommand(db, bson.M{"isMaster": ""}, &Mtree, "Master", "admin")
	MongoRunCommand(db, bson.M{"replSetGetStatus": ""}, &Mtree, "ReplicationStatus", "admin")

	ListDatabaseNames, err := client.ListDatabaseNames(ctx, bson.D{{}})
	if err != nil {
		fmt.Println("err client.ListDatabaseNames is", err)
		return C.CString("")
	}
	for _, DbName := range ListDatabaseNames {
		//fmt.Println("====DB:", DbName, "===")
		if AllDbScrape || DbScrape[DbName] != nil {
			db := client.Database(DbName)

			CollNames, err := db.ListCollectionNames(ctx, bson.D{{}})
			if err != nil {
				//fmt.Println("====DB:", DbName, "error is", err)
				continue
			}
			for _, CollName := range CollNames {
				//fmt.Println("====DB:", DbName, "Col:", CollName, "===")
				if AllCollScrape || CollScrape[CollName] != nil {
					MongoRunCommand(db, bson.M{"collStats": CollName}, &Mtree, "Collection", DbName)
					MongoRunCommand(db, bson.M{"dataSize": CollName}, &Mtree, "DataSize", DbName)
				}
			}

			MongoRunCommand(db, bson.M{"connPoolStats": ""}, &Mtree, "ConnectionPoolStatus", DbName)
			MongoRunCommand(db, bson.M{"dbStats": ""}, &Mtree, "DBStats", DbName)
			MongoRunCommand(db, bson.M{"features": ""}, &Mtree, "Features", DbName)
			MongoRunCommand(db, bson.M{"serverStatus": ""}, &Mtree, "ServerStatus", DbName)
		}
	}

	Mstring := strings.Join(Mtree[:], "\n")

	return C.CString(Mstring)
}

func main() {}
