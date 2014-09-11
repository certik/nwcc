test0obj= t0-f.o t0-main.o
test1obj= t1-f.o t1-main.o
test2obj= t2-f.o t2-main.o
symbols0obj= symbols0-f.o symbols0-main.o
tlstest0obj= tls0-f.o tls0-main.o
tlstest1obj= tls1-f.o tls1-main.o
pictest0obj= pic0-f.so pic0-main.o
pictest1obj= pic1-f.so pic1-main.o
pictest2obj= pic2-f.so pic2-main.o
strpass0obj= struct_pass_small-f.o struct_pass_small-main.o
strpass1obj= struct_pass_med-f.o struct_pass_med-main.o
strpass2obj= struct_pass_small-f_O.o struct_pass_small-main_O.o
strpass3obj= struct_pass_med-f_O.o struct_pass_med-main_O.o
std0obj= std0.o
std1obj= std1.o
commonobj= common0.o common1.o
staticredec0obj= staticredec0-f.o staticredec0-main.o
staticredec1obj= staticredec1-f.o staticredec1-main.o

test0: $(test0obj)
	$(CCcomp) $(CCcompflags) t0-f.o t0-main.o -o test0

test1: $(test1obj)
	$(CCcomp) $(CCcompflags) t1-f.o t1-main.o -o test1

test2: $(test2obj)
	$(CCcomp) $(CCcompflags) t2-f.o t2-main.o -o test2


symbols0: $(symbols0obj)
	$(CC) $(CCflags) symbols0-f.o symbols0-main.o -o symbols0

tlstest0: $(tlstest0obj)
	$(CCcomp) $(CCcompflags) tls0-f.o tls0-main.o -o tlstest0 -lpthread

tlstest1: $(tlstest1obj)
	$(CCcomp) $(CCcompflags) tls1-f.o tls1-main.o -o tlstest1 -lpthread

pictest0: $(pictest0obj)
	$(CCcomp) $(CCcompflags) pic0-f.so pic0-main.o -o pictest0

pictest1: $(pictest1obj)
	$(CCcomp) $(CCcompflags) pic1-f.so pic1-main.o -o pictest1
	
pictest2: $(pictest2obj)
	$(CCcomp) $(CCcompflags) pic2-f.so pic2-main.o -o pictest2


strpass0: $(strpass0obj)
	$(CCcomp) $(CCcompflags) struct_pass_small-f.o struct_pass_small-main.o -o strpass0

strpass1: $(strpass1obj)
	$(CCcomp) $(CCcompflags) struct_pass_med-f.o struct_pass_med-main.o -o strpass1

strpass2: $(strpass2obj)
	$(CCcomp) $(CCcompflags) struct_pass_small-f_O.o struct_pass_small-main_O.o -o strpass2

strpass3: $(strpass3obj)
	$(CCcomp) $(CCcompflags) struct_pass_med-f_O.o struct_pass_med-main_O.o -o strpass3

std0: $(std0obj)
	$(CC) std0.o -o std0 -std=c89
std1: $(std1obj)
	$(CC) std1.o -o std1 -std=c89

common: $(commonobj)
	$(CC) common0.o common1.o -o common

staticredec0: $(staticredec0obj)
	$(CC) staticredec0-f.o staticredec0-main.o -o staticredec0

staticredec1: $(staticredec1obj)
	$(CC) staticredec1-f.o staticredec1-main.o -o staticredec1


################### object files ###############

t0-f.o:
	$(CCcomp) $(CCcompflags) -c t0-f.c
t0-main.o:
	$(CC) $(CCflags) -c t0-main.c


t1-f.o:
	$(CCcomp) $(CCcompflags) -c t1-f.c
t1-main.o:
	$(CC) $(CCflags) -c t1-main.c


t2-f.o:
	$(CCcomp) $(CCcompflags) -c t2-f.c
t2-main.o:
	$(CC) $(CCflags) -c t2-main.c

symbols0-f.o:
	$(CC) $(CCflags) -c symbols0-f.c
symbols0-main.o:
	$(CC) $(CCflags) -c symbols0-main.c

tls0-f.o:
	$(CC) $(CCflags) -c tls0-f.c
tls0-main.o:
	$(CC) $(CCflags) -c tls0-main.c

tls1-f.o:
	$(CC) $(CCflags) -c tls1-f.c
tls1-main.o:
	$(CCcomp) $(CCcompflags) -c tls1-main.c


pic0-f.so:
	$(CC) $(CCflags) pic0-f.c $(PICFLAGS) $(SHAREDFLAGS) -o pic0-f.so
pic0-main.o:
	$(CC) $(CCflags) -c pic0-main.c

pic1-f.so:
	$(CC) $(CCflags) pic1-f.c $(PICFLAGS) $(SHAREDFLAGS) -o pic1-f.so
pic1-main.o:
	$(CC) $(CCflags) -c pic1-main.c

pic2-f.so:
	$(CC) $(CCflags) pic2-f.c $(PICFLAGS) $(SHAREDFLAGS) -o pic2-f.so
pic2-main.o:
	$(CC) $(CCflags) -c pic2-main.c


struct_pass_small-f.o:
	$(CCcomp) $(CCcompflags) -c struct_pass_small-f.c
struct_pass_small-main.o:
	$(CC) $(CCflags) -c struct_pass_small-main.c

struct_pass_small-f_O.o:
	$(CCcomp) $(CCcompflags) -c struct_pass_small-f_O.c
struct_pass_small-main_O.o:
	$(CC) $(CCflags) -c struct_pass_small-main_O.c

struct_pass_med-f.o:
	$(CCcomp) $(CCcompflags) -c struct_pass_med-f.c
struct_pass_med-main.o:
	$(CC) $(CCflags) -c struct_pass_med-main.c

struct_pass_med-f_O.o:
	$(CCcomp) $(CCcompflags) -c struct_pass_med-f_O.c
struct_pass_med-main_O.o:
	$(CC) $(CCflags) -c struct_pass_med-main_O.c

std0.o:
	$(CC) -c std0.c -std=c89

std1.o:
	$(CC) -c std1.c -std=c89

common0.o:
	$(CC) -c common0.c
common1.o:
	$(CC) -c common1.c

staticredec0-f.o:
	$(CC) -c staticredec0-f.c
staticredec0-main.o:
	$(CC) -c staticredec0-main.c
	
staticredec1-f.o:
	$(CC) -c staticredec1-f.c
staticredec1-main.o:
	$(CC) -c staticredec1-main.c

clean:
	rm -f nwcc nwcc1 *.o *.so core a.out
