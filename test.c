/* -*- mode:c; c-file-style:"k&r"; c-basic-offset: 4; tab-width:4; indent-tabs-mode:nil; mode:auto-fill; fill-column:78; -*- */
/* vim: set ts=4 sw=4 et tw=78 fo=cqt wm=0: */

/* @* Place your name here, and any other comments *@
 * @* that deanonymize your work inside this syntax *@
 *      Mark Petersen
 */


#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>


//NOTE: a lot of personal tests were done using gdb and could not be replicated here
//the print statements have also been commented out in case that messes up testing
int main() {
    test1();
    test2();
    test3();
    test4();
    test5();

    return (errno);
}

//see if we can malloc and free each superblock once without a seg fault
int test1() {
    void *a = malloc(32);
    void *b = malloc(64);
    void *c = malloc(128);
    void *d = malloc(256);
    void *e = malloc(512);
    void *f = malloc(1024);
    void *g = malloc(2048);
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    free(g);
    return (errno);
}

//go through superblock list and make sure next variable is 128 bytes over each time
int test2() {
    void *a = malloc(128);
    // printf("Memory address of variable a is %p\n", a);
    for (int i = 1; i < 29; i++) {
        void *b = malloc(128);
        // printf("Memory address of next variable is %p\n", b);
    }
    return (errno);
}

//malloc, free, then malloc the same spaces again to make sure that the same memory addresses are used
int test3() {
    void *a = malloc(32);
    void *b = malloc(64);
    void *c = malloc(128);
    void *d = malloc(128);
    void *e = malloc(128);
    // printf("Memory address of variable a is %p (32)\n", a);
    // printf("Memory address of variable b is %p (64)\n", b);
    // printf("Memory address of variable c is %p (128)\n", c);
    // printf("Memory address of variable d is %p (128)\n", d);
    // printf("Memory address of variable e is %p (128)\n", e);
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    // printf("\n");
    void *f = malloc(32);
    void *g = malloc(64);
    void *h = malloc(128);
    void *i = malloc(128);
    void *j = malloc(128);
    // printf("Memory address of variable f is %p (32)\n", f);
    // printf("Memory address of variable g is %p (64)\n", g);
    // printf("Memory address of variable h is %p (128)\n", h);
    // printf("Memory address of variable i is %p (128)\n", i);
    // printf("Memory address of variable j is %p (128)\n", j);
    return (errno);
}

//check for malloc poisoning
int test4() {
    void *b = malloc(128);
    //printf("Memory address into b is %p\n", b+20);
    free(b);
    return (errno);
}

//check for free poisoning
int test5() {
    void *b = malloc(128);
    free(b);
    //printf("Memory address into b is %p\n", b+20);
    return (errno);
}
