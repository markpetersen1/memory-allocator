all: th_alloc test

th_alloc: th_alloc.c
	gcc -fPIC -Wall -Wextra -g -shared th_alloc.c -o th_alloc.so

test: test.c
	gcc -Wall -Wextra -g test.c -o test

handin: clean
	@if [ `git status --porcelain| wc -l` != 0 ] ; then echo "\n\n\n\n\t\tWARNING: YOU HAVE UNCOMMITTED CHANGES\n\n    Consider committing any pending changes and rerunning make handin.\n\n\n\n"; fi
	git tag -f -a handin -m "Lab 2 Handin"
	git push -f origin --tags handin

update:
	git checkout master
	git pull https://github.com/comp530-f18/lab2.git master


clean:
	rm -f test th_alloc.so

