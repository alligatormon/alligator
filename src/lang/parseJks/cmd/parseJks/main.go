package main

import "C"

import (
	"crypto/x509"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/pavlo-v-chernykh/keystore-go/v4"
)

var match_keys []string
var password []byte
var Mstring string

func readKeyStore(filename string, password []byte) (keystore.KeyStore, error) {
	f, err := os.Open(filename)
	if err != nil {
		return keystore.KeyStore{}, err
	}

	ks := keystore.New()
	if err := ks.Load(f, password); err != nil {
		return keystore.KeyStore{}, err
	}

	if err := f.Close(); err != nil {
		return keystore.KeyStore{}, err
	}

	return ks, nil
}

func readCert(path string) {
	ks, err := readKeyStore(path, password)
	if err != nil {
		log.Println("parseJks readKeyStore error:", err)
		return
	}

	for _, a := range ks.Aliases() {
		pke, err := ks.GetPrivateKeyEntry(a, password)
		if err != nil {
			log.Println("parseJks error:", err)
			return
		}

		certBytes := pke.CertificateChain[0].Content
		now := time.Now().Unix()

		cert, err := x509.ParseCertificates(certBytes)
		if err != nil {
			log.Println("parseJks error:", err)
			return
		}
		for _, certData := range cert {
			not_before := certData.NotBefore.Unix()
			not_after := certData.NotAfter.Unix()
			not_after_str := strconv.FormatInt(not_after, 10)
			not_before_str := strconv.FormatInt(not_before, 10)
			expire_days := (not_after - now) / 86400
			expire_days_str := strconv.FormatInt(expire_days, 10)
			Labels := ""
			if certData.Subject.CommonName != "" {
				Labels += "common_name=\"" + certData.Subject.CommonName + "\","
			}
			if len(certData.Subject.Country) != 0 {
				Labels += " country=\"" + certData.Subject.Country[0] + "\","
			}
			if len(certData.Subject.Organization) != 0 {
				Labels += " organization_name=\"" + certData.Subject.Organization[0] + "\","
			}
			if len(certData.Subject.OrganizationalUnit) != 0 {
				Labels += " organization_unit=\"" + certData.Subject.OrganizationalUnit[0] + "\","
			}
			if len(certData.Subject.Province) != 0 {
				Labels += " county=\"" + certData.Subject.Province[0] + "\","
			}
			Labels += " serial=\"" + certData.SerialNumber.String() + "\","
			Labels += " issuer=\"" + fmt.Sprintf("%v", certData.Issuer) + "\","
			Labels += " cert=\"" + fmt.Sprintf("%v", path) + "\""
			Labels = strings.ReplaceAll(Labels, `\`, ``)
			Mstring += "x509_cert_expire_days{" + Labels + "} " + expire_days_str + "\n"
			Mstring += "x509_cert_not_after{" + Labels + "} " + not_after_str + "\n"
			Mstring += "x509_cert_not_before{" + Labels + "} " + not_before_str + "\n"
		}
	}
}

func lookCerts(fullpath string, d fs.DirEntry, err error) error {
	if err != nil {
		log.Println("parseJks lookCerts error:", lookCerts)
		return err
	}
	if !d.IsDir() {
		for _, token := range match_keys {
			if strings.Contains(d.Name(), token) {
				readCert(fullpath)
			}
		}
	}
	return nil
}

//export alligator_call
func alligator_call(script *C.char, data *C.char, arg *C.char, metrics *C.char, conf *C.char, parser_data_str *C.char, response_str *C.char, queries_str *C.char) *C.char {
	strArg := C.GoString(arg)
	// example of arg is '/app/src/tests/system/jks .jks password'
	argc := strings.Split(strArg, " ")

	match_keys = strings.Split(argc[1], ",")
	password = []byte(argc[2])
	Mstring = ""

	filepath.WalkDir(argc[0], lookCerts)

	return C.CString(Mstring)
}

func main() {}
