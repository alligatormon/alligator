TESTS=`printf "%s " tests/system/*/run.sh`
for TEST in $TESTS; do
	echo $TEST
	sh $TEST
done

TESTS=`printf "%s " tests/mock/*/run.sh`
for TEST in $TESTS; do
	echo $TEST
	sh $TEST
done
