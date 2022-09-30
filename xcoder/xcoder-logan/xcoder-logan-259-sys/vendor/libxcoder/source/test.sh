#When running the script include a rule number as an argument
#For example - ./test.sh D001
for FILE in *.c *.h; do
	vera++ -R $1 $FILE;
done
