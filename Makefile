CC=gcc
DATE=$(shell date --rfc-3339=seconds | sed -r 's/[\:\ \+\-]+/_/g')
PROJECT=$(shell basename `pwd`)
FLAGS=-O3 -g
CMPCMD=cmp

lzw: lzw.c lzw.h
	$(CC) $(FLAGS) lzw.c -o lzw

clean: clean-tests
	rm -f lzw

test: lzw clean-tests test-micro test-small test-big

clean-tests:
	rm -f *.lzw
	rm -f *.out

test-micro: lzw
	./lzw c test-micro.src test-micro.lzw
	./lzw u test-micro.lzw test-micro.out
	$(CMPCMD) test-micro.src test-micro.out

test-small: lzw
	./lzw c test.src test.lzw
	./lzw u test.lzw test.out
	$(CMPCMD) test.src test.out

test-big: lzw
	./lzw c test-big.src test-big.lzw
	./lzw u test-big.lzw test-big.out
	$(CMPCMD) test-big.src test-big.out

test-vbig: lzw
	./lzw c test-vbig.src test-vbig.lzw
	./lzw u test-vbig.lzw test-vbig.out
	$(CMPCMD) test-vbig.src test-vbig.out
