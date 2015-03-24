CC = gcc

mysqltojson.fcgi: mysqltojson.c
	$(CC) mysqltojson.c -o mysqltojson.out -I/usr/local/include -I/usr/local/include/mysql -L/usr/local/lib -L/usr/local/lib/mysql -lstdc++ -lfcgi -lm -lmysqlclient -std=c99 -O3
	# Some commands to copy file to cgi dir
	#-sudo killall tiny.fcgi	# Might be the case that the cgi is not running
	#sudo cp tiny.fcgi /var/www/cgi/

all: mysqltojson.fcgi

# Preprocessor to expand macros
pre:
	$(CC) -E tiny-cgi2.c -lfcgi -lmysqlclient -I/usr/include/mysql -g -std=c99 -Wall -Wextra -O3 > tmp
