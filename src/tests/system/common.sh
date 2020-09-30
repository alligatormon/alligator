error()
{
	echo -e "\033[31mNo $2 in: \e[0m"
	IIFS="$IFS"
	echo "$1"
	IFS="$IIFS"
}

success()
{
	echo -e "\e[32mSuccess: $1 \e[0m"
}
