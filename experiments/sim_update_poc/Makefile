all: gol1

.PHONY: all gol1 gol2 gol3 gol4 averages.csv run clean

all: averages.csv

gol1: gol1.cpp
	g++ gol1.cpp -pthread -o gol1
	./gol1 2>&1 | tee gol1.out

gol2: gol2.cpp
	g++ gol2.cpp -pthread -o gol2
	./gol2 2>&1 | tee gol2.out

gol3: gol3.cpp
	g++ gol3.cpp -pthread -o gol3
	./gol3 2>&1 | tee gol3.out

gol4: gol4.cpp
	g++ gol4.cpp -pthread -o gol4
	./gol4 2>&1 | tee gol4.out

averages.csv: gol1 gol2 gol3 gol4 average_to_csv.py
	python3 average_to_csv.py gol1.out gol2.out gol3.out gol4.out > averages.csv

run: gol1

clean:
	rm -f gol1 gol2 gol3 gol4 gol1.out gol2.out gol3.out gol4.out averages.csv
