CPP = cpp
CPPFLAGS = -w

all : sysload.py

sysload.py : sysload.h
	$(CPP) $(CPPFLAGS) -E sysload.h | grep -v '^struct \|;$$' > sysload.py

doc : README.html

README.html : README
	rst2html README README.html

clean :
	rm -f sysload.py README.html
